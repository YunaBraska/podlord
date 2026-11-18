#include "api_ai_client.h"
#include "incident_ai.h"
#include "memory_credential_store.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTest>
#include <cstdio>
#include <memory>

namespace {
void require(bool condition, const char* message) { if (!condition) { std::fprintf(stderr, "%s\n", message); std::exit(1); } }
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
                    if (firstLine.startsWith("GET /v1/models") || firstLine.startsWith("GET /api/v1/models")) {
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
private:
    bool claude_;
};
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
    require(ai.model().isEmpty() && ai.selectModel(providerName, model) && ai.provider() == providerName
        && ai.model() == model, "Selecting a model did not activate its provider");
    require(ai.setEnabled("cluster-one", true), "Cannot enable incident analysis");
    const auto resourcePath = QStringLiteral("/api/v1/nodes/broken");
    ai.observe("cluster-one", QJsonArray{QJsonObject{{"uid", "node-one"}, {"kind", "Node"}, {"name", "broken"},
        {"path", resourcePath}, {"issue", "NotReady"}, {"status", "NotReady"}, {"problemSeverity", 2},
        {"secret", "secret-private-marker"}}}, "de");
    require(QTest::qWaitFor([&] { return !ai.result("cluster-one", resourcePath).value("summary").toString().isEmpty(); }, 2000),
        "API provider did not reach the public incident result");
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
    QCoreApplication app(argc, argv);
    const auto name = argc < 2 ? QStringLiteral("claude") : QString::fromUtf8(argv[1]);
    if (name == "migration" || name == "unavailable" || name == "conflict" || name == "invalid") storageScenario(name);
    else if (name == "system_store") systemStoreScenario();
    else scenario(name == "claude");
}
