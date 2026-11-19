#include "api_ai_client.h"
#include "incident_ai.h"
#include "memory_credential_store.h"
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTest>
#include <QUrlQuery>
#include <cstdio>
#include <memory>

namespace {
void require(bool condition, const char* message) { if (!condition) { std::fprintf(stderr, "%s\n", message); std::exit(1); } }
class BrowserCapture final : public QObject {
    Q_OBJECT
public:
    BrowserCapture() { QDesktopServices::setUrlHandler("http", this, "opened"); }
    ~BrowserCapture() override { QDesktopServices::unsetUrlHandler("http"); }
    QUrl last;
public slots:
    void opened(const QUrl& url) { last = url; }
};
class Boundary final : public QTcpServer {
public:
    explicit Boundary(bool claude) : claude_(claude) {
        connect(this, &QTcpServer::newConnection, this, [this] {
            while (hasPendingConnections()) {
                auto* socket = nextPendingConnection();
                auto bytes = std::make_shared<QByteArray>();
                connect(socket, &QTcpSocket::readyRead, this, [this, socket, bytes] {
                    bytes->append(socket->readAll());
                    const auto boundary = bytes->indexOf("\r\n\r\n");
                    if (boundary < 0) return;
                    const auto header = bytes->left(boundary);
                    const auto lower = header.toLower();
                    const auto lengthStart = lower.indexOf("content-length:");
                    const auto length = lengthStart < 0 ? 0 : lower.mid(lengthStart + 15).split('\r').first().trimmed().toInt();
                    if (bytes->size() < boundary + 4 + length) return;
                    const auto firstLine = header.left(header.indexOf("\r\n"));
                    const auto body = bytes->mid(boundary + 4, length);
                    int code = lower.contains("authorization: bearer private-test-key") ? 200 : 401;
                    if (claude_ && !lower.contains("anthropic-version: 2026-10-21")) code = 400;
                    QByteArray payload;
                    if (firstLine.startsWith("POST /api/v1/auth/keys") && !claude_) {
                        oauthSubmitted = body;
                        const auto exchange = QJsonDocument::fromJson(body).object();
                        code = exchange.value("code") == "test-code"
                            && !exchange.value("code_verifier").toString().isEmpty()
                            && exchange.value("code_challenge_method") == "S256" ? 200 : 400;
                        payload = QJsonDocument(QJsonObject{{"key", "private-test-key"}}).toJson(QJsonDocument::Compact);
                    } else if (firstLine.startsWith("GET /v1/models") || firstLine.startsWith("GET /api/v1/models")) {
                        const QJsonObject model = claude_ ? QJsonObject{{"id", "claude-test"}, {"display_name", "Claude test"}}
                            : QJsonObject{{"id", "vendor/model"}, {"name", "Router test"},
                                {"architecture", QJsonObject{{"output_modalities", QJsonArray{"text"}}}}};
                        payload = QJsonDocument(QJsonObject{{"data", QJsonArray{model}}, {"has_more", false}}).toJson(QJsonDocument::Compact);
                    } else if (firstLine.startsWith("POST /v1/messages") || firstLine.startsWith("POST /api/v1/chat/completions")) {
                        submitted = body;
                        payload = claude_ ? QJsonDocument(QJsonObject{{"stop_reason", "end_turn"},
                            {"content", QJsonArray{QJsonObject{{"type", "text"}, {"text", "{\"summary\":\"Claude ok\",\"action\":\"Check node\"}"}}}}}).toJson(QJsonDocument::Compact)
                            : QJsonDocument(QJsonObject{{"choices", QJsonArray{QJsonObject{{"finish_reason", "stop"},
                                {"message", QJsonObject{{"content", "{\"summary\":\"Router ok\",\"action\":\"Check node\"}"}}}}}}}).toJson(QJsonDocument::Compact);
                    } else { code = 404; payload = "{}"; }
                    socket->write("HTTP/1.1 " + QByteArray::number(code) + (code == 200 ? " OK" : " Error")
                        + "\r\nContent-Type: application/json\r\nContent-Length: " + QByteArray::number(payload.size())
                        + "\r\nConnection: close\r\n\r\n" + payload);
                    socket->disconnectFromHost();
                });
                connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            }
        });
    }
    QString base() const { return QString("http://127.0.0.1:%1").arg(serverPort()); }
    QByteArray submitted;
    QByteArray oauthSubmitted;
private:
    bool claude_;
};
void oauthScenario() {
    QTemporaryDir directory;
    require(directory.isValid(), "Cannot create OAuth profile");
    BrowserCapture browser;
    Boundary boundary(false);
    require(boundary.listen(QHostAddress::LocalHost, 0), "Cannot start OpenRouter boundary");
    MemoryCredentialStore credentials;
    podlord::ApiAiClient client(directory.filePath("profile"), podlord::ApiAiClient::Provider::OpenRouter,
        QUrl(boundary.base()), nullptr, &credentials);
    require(client.signIn("de") && !client.signIn(), "OpenRouter sign-in did not start exactly once");
    require(QTest::qWaitFor([&] { return browser.last.isValid(); }, 1000), "OpenRouter browser did not open");
    const QUrlQuery authorization(browser.last);
    require(browser.last.path() == "/auth" && authorization.queryItemValue("code_challenge_method") == "S256"
        && !authorization.queryItemValue("code_challenge").isEmpty()
        && !authorization.queryItemValue("state").isEmpty(), "OpenRouter authorization request is incomplete");
    const auto callback = [&](bool wrongState, bool denied = false) {
        const QUrlQuery currentAuthorization(browser.last);
        QUrl uri(currentAuthorization.queryItemValue("callback_url", QUrl::FullyDecoded));
        QUrlQuery query;
        query.addQueryItem("state", wrongState ? "wrong" : currentAuthorization.queryItemValue("state", QUrl::FullyDecoded));
        if (denied) query.addQueryItem("error", "access_denied");
        else query.addQueryItem("code", "test-code");
        uri.setQuery(query);
        QTcpSocket socket;
        socket.connectToHost("127.0.0.1", uri.port());
        require(socket.waitForConnected(1000), "Cannot reach OpenRouter callback");
        socket.write("GET " + uri.toEncoded(QUrl::RemoveScheme | QUrl::RemoveAuthority)
            + " HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n");
        socket.flush();
        QTest::qWait(50);
        return socket.bytesAvailable() ? socket.readAll() : QByteArray{};
    };
    callback(true);
    require(!client.configured() && boundary.oauthSubmitted.isEmpty(), "Wrong OAuth state was accepted");
    const auto response = callback(false);
    require(response.contains("text/html") && response.contains("PodLord schließt sie ab"),
        "OpenRouter callback did not use the selected language");
    require(QTest::qWaitFor([&] { return client.configured() && client.models().size() == 1; }, 3000),
        "OpenRouter sign-in did not save key and load catalog");
    const auto stored = credentials.read(directory.filePath("profile"), "openrouter");
    require(stored.ok && stored.found && stored.data.contains("private-test-key")
        && !QFile::exists(directory.filePath("profile/openrouter-auth.json")),
        "OpenRouter browser key was not saved in the system credential store");
    client.clearKey();
    require(!client.configured() && !credentials.read(directory.filePath("profile"), "openrouter").found,
        "OpenRouter sign-out left a credential");
    browser.last = QUrl{};
    require(client.signIn() && QTest::qWaitFor([&] { return browser.last.isValid(); }, 1000),
        "OpenRouter sign-in could not be repeated");
    callback(false, true);
    require(QTest::qWaitFor([&] { return client.status().contains("cancelled"); }, 1000)
        && !client.configured(),
        "Cancelled OpenRouter sign-in connected an account");
}
void scenario(bool claude) {
    QTemporaryDir directory;
    require(directory.isValid(), "Cannot create test profile");
    Boundary boundary(claude);
    require(boundary.listen(QHostAddress::LocalHost, 0), "Cannot start AI provider boundary");
    const auto provider = claude ? podlord::ApiAiClient::Provider::Claude : podlord::ApiAiClient::Provider::OpenRouter;
    MemoryCredentialStore credentials;
    podlord::ApiAiClient client(directory.filePath("profile"), provider, QUrl(boundary.base()), nullptr, &credentials);
    require(!client.configured() && client.models().isEmpty(), "Provider configured without a key");
    require(!client.setKey("bad key"), "Whitespace in API key was accepted");
    require(client.setKey("private-test-key"), "Cannot save API key");
    require(QTest::qWaitFor([&] { return client.models().size() == 1; }, 2000), "Model catalog was not loaded");
    const auto model = client.models().first().toMap().value("slug").toString();
    require(model == (claude ? "claude-test" : "vendor/model"), "Wrong model in catalog");
    const auto path = QDir(directory.filePath("profile")).filePath(claude ? "claude-auth.json" : "openrouter-auth.json");
    require(!QFile::exists(path) && credentials.read(directory.filePath("profile"), claude ? "claude" : "openrouter").found,
        "Provider key was written as a file or not saved in the credential store");
    QString answer, error;
    client.analyze(model, "Return JSON", "Observed problem code: NotReady", [&](QString value, QString failure) {
        answer = value; error = failure;
    });
    require(QTest::qWaitFor([&] { return !answer.isEmpty() || !error.isEmpty(); }, 2000) && error.isEmpty(),
        "Provider answer did not complete");
    const auto payload = QJsonDocument::fromJson(boundary.submitted).object();
    require(payload.value("model") == model && payload.value("messages").toArray().last().toObject().value("content").toString().contains("NotReady")
        && answer.contains(claude ? "Claude ok" : "Router ok"), "Provider request or answer is wrong");
    const podlord::IncidentAi::RemoteEndpoints endpoints{QUrl(boundary.base()), QUrl(boundary.base()),
        QUrl(boundary.base()), QUrl(boundary.base())};
    podlord::IncidentAi ai(directory.filePath("profile"), QUrl(boundary.base()), nullptr, endpoints, &credentials);
    const auto providerName = claude ? QString("claude") : QString("openrouter");
    require(QTest::qWaitFor([&] {
        for (const auto& item : ai.allModelChoices()) {
            const auto choice = item.toMap();
            if (choice.value("provider") == providerName && choice.value("slug") == model) return true;
        }
        return false;
    }, 2000), "Combined model catalog did not include the configured provider");
    require(ai.accounts().size() == 1 && ai.accounts().first().toMap().value("requests24h").toInt() == 0,
        "Connected account is missing from the independent account list");
    const auto choices = ai.allModelChoices();
    int headers = 0, models = 0;
    for (const auto& value : choices) {
        const auto choice = value.toMap();
        if (choice.value("header").toBool()) {
            ++headers;
            require(choice.value("provider") == providerName && choice.value("slug").toString().isEmpty(),
                "Unconnected provider appeared as a model");
        } else {
            ++models;
            require(choice.value("provider") == providerName && choice.value("slug") == model,
                "Model list contains a placeholder instead of a model");
        }
    }
    require(headers == 1 && models == 1 && !ai.selectModel(providerName, {}),
        "Combined model list contains selectable provider placeholders");
    require(ai.model().isEmpty() && ai.selectModel(providerName, model) && ai.provider() == providerName
        && ai.model() == model, "Selecting a model did not activate its provider");
    require(ai.setEnabled("cluster-one", true), "Cannot enable incident analysis");
    const auto resourcePath = QStringLiteral("/api/v1/nodes/broken");
    ai.observe("cluster-one", QJsonArray{QJsonObject{{"uid", "node-one"}, {"kind", "Node"}, {"name", "broken"},
        {"path", resourcePath}, {"issue", "NotReady"}, {"status", "NotReady"}, {"problemSeverity", 2},
        {"secret", "secret-private-marker"}}}, "de");
    require(QTest::qWaitFor([&] { return !ai.result("cluster-one", resourcePath).value("summary").toString().isEmpty(); }, 2000),
        "API provider did not reach the public incident result");
    require(ai.accounts().first().toMap().value("requests24h").toInt() == 1,
        "The 24-hour count did not record the actual analysis request");
    podlord::IncidentAi reopened(directory.filePath("profile"), QUrl(boundary.base()), nullptr, endpoints, &credentials);
    require(reopened.accounts().size() == 1 && reopened.accounts().first().toMap().value("requests24h").toInt() == 1,
        "The 24-hour request count did not survive restart");
    QFile history(QDir(directory.filePath("profile")).filePath("incident-ai.json"));
    require(history.open(QIODevice::ReadOnly), "Cannot read request history");
    auto settings = QJsonDocument::fromJson(history.readAll()).object();
    history.close();
    auto requests = settings.value("requests").toArray();
    require(requests.size() == 1, "Catalog or login calls were counted as analysis requests");
    auto agedRequest = requests.first().toObject();
    agedRequest.insert("at", QDateTime::currentDateTimeUtc().addSecs(-86401).toString(Qt::ISODateWithMs));
    requests.replace(0, agedRequest);
    settings.insert("requests", requests);
    require(history.open(QIODevice::WriteOnly | QIODevice::Truncate)
        && history.write(QJsonDocument(settings).toJson()) > 0, "Cannot age request history");
    history.close();
    podlord::IncidentAi aged(directory.filePath("profile"), QUrl(boundary.base()), nullptr, endpoints, &credentials);
    require(aged.accounts().size() == 1 && aged.accounts().first().toMap().value("requests24h").toInt() == 0,
        "Expired analysis request remained in the 24-hour count");
    require(!boundary.submitted.contains("secret-private-marker") && boundary.submitted.contains("NotReady"),
        "API provider received unsafe data or lost the problem signal");
    credentials.available = false;
    client.clearKey();
    require(client.configured() && !client.status().isEmpty(),
        "Failed credential-store deletion silently disconnected the provider");
    credentials.available = true;
    client.clearKey();
    require(!client.configured() && client.models().isEmpty()
        && !credentials.read(directory.filePath("profile"), claude ? "claude" : "openrouter").found,
        "Removing API key left provider configured");
}
void accountsScenario() {
    QTemporaryDir directory;
    require(directory.isValid(), "Cannot create account profile");
    Boundary claude(true), router(false);
    require(claude.listen(QHostAddress::LocalHost, 0) && router.listen(QHostAddress::LocalHost, 0),
        "Cannot start account provider boundaries");
    MemoryCredentialStore credentials;
    const auto profile = directory.filePath("profile");
    podlord::ApiAiClient claudeAccount(profile, podlord::ApiAiClient::Provider::Claude, QUrl(claude.base()), nullptr, &credentials);
    podlord::ApiAiClient routerAccount(profile, podlord::ApiAiClient::Provider::OpenRouter, QUrl(router.base()), nullptr, &credentials);
    require(claudeAccount.setKey("private-test-key") && routerAccount.setKey("private-test-key"),
        "Cannot connect two provider accounts");
    const podlord::IncidentAi::RemoteEndpoints endpoints{QUrl(claude.base()), QUrl(claude.base()),
        QUrl(claude.base()), QUrl(router.base())};
    podlord::IncidentAi ai(profile, QUrl(claude.base()), nullptr, endpoints, &credentials);
    require(QTest::qWaitFor([&] { return ai.allModelChoices().size() >= 4; }, 2000),
        "Connected provider models were not loaded");
    require(ai.provider() == "ollama" && ai.model().isEmpty() && ai.accounts().size() == 2,
        "Connecting accounts selected a model or hid an account");
    require(ai.selectModel("claude", "claude-test") && ai.model() == "claude-test", "Cannot select Claude model");
    ai.signOutOpenRouter();
    require(ai.provider() == "claude" && ai.model() == "claude-test" && ai.accounts().size() == 1,
        "Signing out another provider changed the selected model");
    ai.clearClaudeKey();
    require(ai.provider() == "claude" && ai.model().isEmpty() && ai.accounts().isEmpty(),
        "Signing out the selected provider did not clear its model");
    podlord::IncidentAi reopened(profile, QUrl(claude.base()), nullptr, endpoints, &credentials);
    require(reopened.model().isEmpty(), "Signed-out model returned after restart");
}
void storageScenario(const QString& name) {
    QTemporaryDir directory;
    require(directory.isValid(), "Cannot create storage test profile");
    const auto profile = directory.filePath("profile");
    require(QDir().mkpath(profile), "Cannot create storage test directory");
    const auto path = QDir(profile).filePath("claude-auth.json");
    const auto data = name == "invalid" ? QByteArray("not-json")
        : QJsonDocument(QJsonObject{{"version", 1}, {"key", "private-test-key"}}).toJson();
    QFile legacy(path);
    require(legacy.open(QIODevice::WriteOnly) && legacy.setPermissions(QFile::ReadOwner | QFile::WriteOwner)
        && legacy.write(data) == data.size(), "Cannot create legacy credential fixture");
    legacy.close();
    MemoryCredentialStore credentials;
    if (name == "unavailable") credentials.available = false;
    if (name == "conflict") credentials.entries.insert(profile + "/claude", "other-credential");
    podlord::ApiAiClient client(profile, podlord::ApiAiClient::Provider::Claude, {}, nullptr, &credentials);
    if (name == "unavailable" || name == "conflict" || name == "invalid") {
        require(!client.configured() && QFile::exists(path) && !client.status().isEmpty()
            && (!credentials.read(profile, "claude").found || name == "conflict"),
            "Failed migration discarded a legacy credential or hid the error");
        return;
    }
    require(client.configured() && !QFile::exists(path) && credentials.read(profile, "claude").data == data,
        "Legacy API key was not migrated into the system credential store");
    podlord::ApiAiClient restarted(profile, podlord::ApiAiClient::Provider::Claude, {}, nullptr, &credentials);
    require(restarted.configured(), "Migrated API key was unavailable after restart");
}
void systemStoreScenario() {
    QTemporaryDir directory;
    require(directory.isValid(), "Cannot create system store test profile");
    podlord::SystemCredentialStore credentials;
    QString error;
    require(credentials.write(directory.path(), "test", "temporary-test-value", error), "System credential write failed");
    const auto stored = credentials.read(directory.path(), "test");
    require(stored.ok && stored.found && stored.data == "temporary-test-value", "System credential read failed");
    require(credentials.remove(directory.path(), "test", error) && !credentials.read(directory.path(), "test").found,
        "System credential cleanup failed");
}
} // namespace
int main(int argc, char** argv) {
    QGuiApplication app(argc, argv);
    const auto name = argc < 2 ? QStringLiteral("claude") : QString::fromUtf8(argv[1]);
    if (name == "oauth") oauthScenario();
    else if (name == "migration" || name == "unavailable" || name == "conflict" || name == "invalid") storageScenario(name);
    else if (name == "system_store") systemStoreScenario();
    else if (name == "accounts") accountsScenario();
    else scenario(name == "claude");
}
#include "api_ai_client_test.moc"
