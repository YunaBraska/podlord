#include "chatgpt_client.h"
#include "incident_ai.h"
#include "memory_credential_store.h"
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTest>
#include <QUrlQuery>
#include <algorithm>
#include <cstdio>
#include <memory>
#include <openssl/bn.h>
#include <openssl/core_names.h>
#include <openssl/evp.h>
#include <openssl/rsa.h>

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
class ChatGptBoundary final : public QTcpServer {
public:
    explicit ChatGptBoundary() {
        auto* context = EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, nullptr);
        require(context && EVP_PKEY_keygen_init(context) > 0 && EVP_PKEY_CTX_set_rsa_keygen_bits(context, 2048) > 0,
            "Cannot initialize test signing key");
        EVP_PKEY* key = nullptr;
        require(EVP_PKEY_keygen(context, &key) > 0, "Cannot create test signing key");
        signingKey_.reset(key); EVP_PKEY_CTX_free(context);
        connect(this, &QTcpServer::newConnection, this, [this] {
            while (hasPendingConnections()) {
                auto* socket = nextPendingConnection();
                auto bytes = std::make_shared<QByteArray>();
                connect(socket, &QTcpSocket::readyRead, this, [this, socket, bytes] {
                    bytes->append(socket->readAll());
                    const auto boundary = bytes->indexOf("\r\n\r\n");
                    if (boundary < 0) return;
                    const auto header = bytes->left(boundary);
                    const auto line = header.left(header.indexOf("\r\n"));
                    const auto lengthStart = header.toLower().indexOf("content-length:");
                    const auto length = lengthStart < 0 ? 0 : header.mid(lengthStart + 15).split('\r').first().trimmed().toInt();
                    if (bytes->size() < boundary + 4 + length) return;
                    const auto body = bytes->mid(boundary + 4, length);
                    QByteArray contentType = "application/json";
                    QByteArray payload;
                    int code = 200;
                    if (line.startsWith("GET /.well-known/jwks.json ")) payload = QJsonDocument(jwks()).toJson(QJsonDocument::Compact);
                    else if (line.startsWith("POST /api/accounts/oauth/token ")) {
                        const QUrlQuery fields(QString::fromUtf8(body));
                        if (fields.queryItemValue("grant_type") == "refresh_token"
                            && fields.queryItemValue("client_id") == "oaiapp_test"
                            && fields.queryItemValue("refresh_token") == "private-refresh"
                            && fields.queryItemValue("resource") == "https://api.openai.com/v1") {
                            ++refreshes;
                            if (refreshFailure == "temporary") {
                                code = 503; payload = R"({"error":"server_error"})";
                            } else if (refreshFailure == "expired") {
                                code = 400; payload = R"({"error":"invalid_grant"})";
                            } else payload = QJsonDocument(QJsonObject{{"access_token", "private-access-2"},
                                {"refresh_token", "private-refresh-2"}, {"scope", "openid profile email offline_access resource.invoke chatgpt.tokens.use.direct"},
                                {"token_type", "Bearer"}, {"expires_in", 3600}}).toJson(QJsonDocument::Compact);
                        } else if (fields.queryItemValue("grant_type") != "authorization_code"
                            || fields.queryItemValue("client_id") != "oaiapp_test"
                            || fields.queryItemValue("code") != "one-time-code"
                            || fields.queryItemValue("redirect_uri") != expectedRedirect
                            || fields.queryItemValue("code_verifier").isEmpty()
                            || fields.queryItemValue("resource") != "https://api.openai.com/v1") {
                            code = 400; payload = "{}";
                        } else payload = QJsonDocument(QJsonObject{{"id_token", signedToken()}, {"access_token", "private-access"},
                            {"refresh_token", "private-refresh"}, {"scope", missingDirectScope
                                ? "openid profile email offline_access resource.invoke"
                                : "openid profile email offline_access resource.invoke chatgpt.tokens.use.direct"},
                            {"token_type", "Bearer"}, {"expires_in", shortLived ? 60 : 3600}}).toJson(QJsonDocument::Compact);
                    } else if (line.startsWith("GET /v1/models ")) {
                        const auto fields = header.split('\n');
                        const bool authorized = std::ranges::any_of(fields, [](const QByteArray& field) {
                            const auto separator = field.indexOf(':');
                            if (separator < 0 || field.first(separator).compare("Authorization", Qt::CaseInsensitive) != 0)
                                return false;
                            const auto value = field.sliced(separator + 1).trimmed();
                            return value == "Bearer private-access" || value == "Bearer private-access-2";
                        });
                        if (!authorized) { code = 401; payload = "{}"; }
                        else payload = QJsonDocument(QJsonObject{{"models", QJsonArray{
                            QJsonObject{{"slug", "gpt-test"}, {"display_name", "Test model"}, {"visibility", "list"}, {"max_input_tokens", 9000}},
                            QJsonObject{{"slug", "hidden"}, {"display_name", "Hidden"}, {"visibility", "hidden"}}}}}).toJson(QJsonDocument::Compact);
                    } else if (line.startsWith("POST /v1/responses ")) {
                        submitted = body;
                        const QJsonObject content{{"type", "output_text"},
                            {"text", "{\"summary\":\"Test summary\",\"action\":\"Test action\"}"}};
                        const QJsonObject output{{"content", QJsonArray{content}}};
                        const QJsonObject responseEvent{{"type", "response.completed"},
                            {"response", QJsonObject{{"output", QJsonArray{output}}}}};
                        const auto response = QJsonDocument(responseEvent).toJson(QJsonDocument::Compact);
                        contentType = "text/event-stream";
                        payload = "data: " + response + "\n\n";
                    } else { code = 404; payload = "{}"; }
                    socket->write("HTTP/1.1 " + QByteArray::number(code) + (code == 200 ? " OK" : " Error")
                        + "\r\nContent-Type: " + contentType + "\r\nContent-Length: " + QByteArray::number(payload.size())
                        + "\r\nConnection: close\r\n\r\n" + payload);
                    socket->disconnectFromHost();
                });
                connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            }
        });
    }
    QString base() const { return QString("http://127.0.0.1:%1").arg(serverPort()); }
    QString expectedRedirect, expectedNonce;
    QByteArray submitted;
    bool wrongNonce = false;
    bool shortLived = false;
    bool missingDirectScope = false;
    QString refreshFailure;
    int refreshes = 0;
private:
    std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)> signingKey_{nullptr, EVP_PKEY_free};
    QJsonObject jwks() const {
        BIGNUM *modulus = nullptr, *exponent = nullptr;
        require(EVP_PKEY_get_bn_param(signingKey_.get(), OSSL_PKEY_PARAM_RSA_N, &modulus) > 0
            && EVP_PKEY_get_bn_param(signingKey_.get(), OSSL_PKEY_PARAM_RSA_E, &exponent) > 0, "Cannot read test key");
        QByteArray n(BN_num_bytes(modulus), char(0)), e(BN_num_bytes(exponent), char(0));
        BN_bn2bin(modulus, reinterpret_cast<unsigned char*>(n.data()));
        BN_bn2bin(exponent, reinterpret_cast<unsigned char*>(e.data()));
        BN_free(modulus); BN_free(exponent);
        const auto encode = [](const QByteArray& value) { return QString::fromLatin1(value.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals)); };
        return {{"keys", QJsonArray{QJsonObject{{"kty", "RSA"}, {"kid", "test"}, {"alg", "RS256"}, {"use", "sig"},
            {"n", encode(n)}, {"e", encode(e)}}}}};
    }
    QString signedToken() const {
        const auto encode = [](const QJsonObject& object) {
            return QJsonDocument(object).toJson(QJsonDocument::Compact).toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
        };
        const auto now = QDateTime::currentSecsSinceEpoch();
        const QByteArray message = encode(QJsonObject{{"alg", "RS256"}, {"kid", "test"}}) + '.'
            + encode(QJsonObject{{"iss", base()}, {"aud", "oaiapp_test"}, {"sub", "user-one"}, {"email", "test@example.invalid"},
                {"nonce", wrongNonce ? "wrong" : expectedNonce}, {"iat", now}, {"exp", now + 3600}});
        std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> context(EVP_MD_CTX_new(), EVP_MD_CTX_free);
        require(context && EVP_DigestSignInit(context.get(), nullptr, EVP_sha256(), nullptr, signingKey_.get()) > 0
            && EVP_DigestSignUpdate(context.get(), message.constData(), message.size()) > 0, "Cannot sign test token");
        size_t size = 0;
        require(EVP_DigestSignFinal(context.get(), nullptr, &size) > 0, "Cannot size test signature");
        QByteArray signature(qsizetype(size), char(0));
        require(EVP_DigestSignFinal(context.get(), reinterpret_cast<unsigned char*>(signature.data()), &size) > 0,
            "Cannot finish test signature");
        signature.resize(qsizetype(size));
        return QString::fromLatin1(message + '.' + signature.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
    }
};
QByteArray callback(const QUrl& authorization, ChatGptBoundary& boundary, bool wrongState = false) {
    const QUrlQuery query(authorization);
    boundary.expectedRedirect = query.queryItemValue("redirect_uri", QUrl::FullyDecoded);
    boundary.expectedNonce = query.queryItemValue("nonce", QUrl::FullyDecoded);
    QUrl url(boundary.expectedRedirect);
    QUrlQuery callbackQuery;
    callbackQuery.addQueryItem("state", wrongState ? "wrong" : query.queryItemValue("state", QUrl::FullyDecoded));
    callbackQuery.addQueryItem("code", "one-time-code"); callbackQuery.addQueryItem("client_id", "oaiapp_test");
    url.setQuery(callbackQuery);
    QTcpSocket socket;
    socket.connectToHost("127.0.0.1", url.port());
    require(socket.waitForConnected(1000), "Cannot contact OAuth callback");
    socket.write("GET " + url.toEncoded(QUrl::RemoveScheme | QUrl::RemoveAuthority) + " HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n");
    socket.flush();
    require(QTest::qWaitFor([&] { return socket.state() == QAbstractSocket::UnconnectedState; }, 2000),
        "OAuth callback did not close after responding");
    return socket.readAll();
}
void scenario(const QString& name) {
    QTemporaryDir directory;
    require(directory.isValid(), "Cannot create temporary profile");
    BrowserCapture browser;
    ChatGptBoundary boundary;
    require(boundary.listen(QHostAddress::LocalHost, 0), "Cannot start OAuth boundary");
    boundary.wrongNonce = name == "wrong_nonce";
    boundary.shortLived = name == "refresh" || name == "refresh_transient" || name == "refresh_expired";
    if (name == "refresh_transient") boundary.refreshFailure = "temporary";
    if (name == "refresh_expired") boundary.refreshFailure = "expired";
    boundary.missingDirectScope = name == "reconsent";
    if (name == "migration") {
        const auto profile = directory.filePath("profile");
        require(QDir().mkpath(profile), "Cannot create legacy ChatGPT profile");
        QFile legacy(QDir(profile).filePath("chatgpt-auth.json"));
        const auto scopes = "openid profile email offline_access resource.invoke chatgpt.tokens.use.direct";
        const auto account = [scopes](const QString& id, const QString& email, const QString& token) {
            return QJsonObject{{"clientId", id}, {"subject", id}, {"email", email}, {"idToken", "old-id"},
                {"accessToken", "old-access"}, {"refreshToken", token}, {"scopes", scopes},
                {"expiresAt", QDateTime::currentDateTimeUtc().addSecs(3600).toString(Qt::ISODateWithMs)}};
        };
        const auto data = QJsonDocument(QJsonObject{{"version", 1}, {"hostId", "urn:uuid:test-host"},
            {"activeClientId", "oaiapp_test"}, {"accounts", QJsonArray{
                account("old-app", "other@example.invalid", "old-refresh"),
                account("oaiapp_test", "test@example.invalid", "private-refresh")}}}).toJson();
        require(legacy.open(QIODevice::WriteOnly) && legacy.setPermissions(QFile::ReadOwner | QFile::WriteOwner)
            && legacy.write(data) == data.size(), "Cannot create legacy ChatGPT credential fixture");
        legacy.close();
    }
    MemoryCredentialStore credentials;
    podlord::ChatGptClient client(directory.filePath("profile"), QUrl(boundary.base()), QUrl(boundary.base()), nullptr, &credentials);
    if (name == "migration") {
        const auto stored = credentials.read(directory.filePath("profile"), "chatgpt");
        const auto accounts = QJsonDocument::fromJson(stored.data).object().value("accounts").toArray();
        require(client.connected() && client.activeAccount() == "test@example.invalid"
            && !QFile::exists(directory.filePath("profile/chatgpt-auth.json")) && accounts.size() == 1
            && accounts.first().toObject().value("refreshToken") == "private-refresh"
            && accounts.first().toObject().value("accessToken") == "" && accounts.first().toObject().value("idToken") == "",
            "Legacy ChatGPT accounts were not minimized and migrated");
        client.refreshModels();
        require(QTest::qWaitFor([&] { return client.models().size() == 1 && boundary.refreshes == 1; }, 2000),
            "Migrated ChatGPT session did not refresh before model discovery");
        return;
    }
    require(!client.connected() && client.models().isEmpty(), "ChatGPT connected without sign-in");
    require(client.signIn({}, name == "localized_callback" ? "de" : "en"), "Cannot start ChatGPT sign-in");
    require(QTest::qWaitFor([&] { return browser.last.isValid(); }, 1000), "Browser authorization was not opened");
    const QUrlQuery authorization(browser.last);
    require(authorization.queryItemValue("client_id") == "dynamic_agent_client"
        && authorization.queryItemValue("agent_name_hint") == "PodLord"
        && authorization.queryItemValue("code_challenge_method") == "S256"
        && !authorization.queryItemValue("code_challenge").isEmpty()
        && authorization.queryItemValue("scope").contains("chatgpt.tokens.use.direct"), "OAuth request is incomplete");
    if (name == "wrong_state") {
        callback(browser.last, boundary, true);
        QTest::qWait(50);
        require(!client.connected(), "Wrong OAuth state connected account");
    }
    const auto callbackResponse = callback(browser.last, boundary);
    if (name == "localized_callback")
        require(callbackResponse.contains("text/html") && callbackResponse.contains("PodLord schließt sie ab"),
            "OAuth callback was not localized to German");
    if (name == "wrong_nonce") {
        require(QTest::qWaitFor([&] { return client.status().contains("verification failed"); }, 2000),
            "Invalid ID token was accepted or verification did not finish");
        require(!client.connected(), "Wrong nonce connected account");
        return;
    }
    if (name == "reconsent") {
        require(QTest::qWaitFor([&] { return client.activeClientId() == "oaiapp_test"; }, 2000)
            && !client.connected() && client.activeAccount() == "test@example.invalid",
            "Missing plan permission hid the account needed for recovery");
        browser.last = QUrl{};
        require(client.signIn("oaiapp_test") && QTest::qWaitFor([&] { return browser.last.isValid(); }, 1000),
            "Cannot repeat ChatGPT authorization");
        require(QUrlQuery(browser.last).queryItemValue("prompt") == "consent",
            "Missing plan permission did not request consent again");
        client.signOut();
        require(client.activeAccount().isEmpty() && client.activeClientId().isEmpty() && !client.connected(),
            "Signing out during reauthorization kept a disconnected account");
        return;
    }
    if (name == "refresh_transient") {
        require(QTest::qWaitFor([&] { return client.status() == "ChatGPT session could not be renewed. Retry later."; }, 2000)
            && client.connected() && boundary.refreshes == 1,
            "A temporary refresh failure lost the account or asked for sign-in");
        boundary.refreshFailure.clear();
        client.refreshModels();
        require(QTest::qWaitFor([&] { return client.models().size() == 1 && boundary.refreshes == 2; }, 2000)
            && client.status().isEmpty(),
            "ChatGPT did not recover after a temporary refresh failure");
        return;
    }
    if (name == "refresh_expired") {
        require(QTest::qWaitFor([&] { return client.status() == "ChatGPT session expired. Sign in again."; }, 2000)
            && !client.connected() && client.activeAccount() == "test@example.invalid",
            "An unusable refresh token did not request account reconnection");
        browser.last = QUrl{};
        require(client.signIn(client.activeClientId()) && QTest::qWaitFor([&] { return browser.last.isValid(); }, 1000)
            && QUrlQuery(browser.last).queryItemValue("client_id") == "oaiapp_test",
            "The saved client registration could not reconnect after expiry");
        return;
    }
    require(QTest::qWaitFor([&] { return client.connected() && client.models().size() == 1; }, 2000),
        "Validated account or account-specific model catalog unavailable");
    require(client.activeAccount() == "test@example.invalid" && client.models().first().toMap().value("slug") == "gpt-test",
        "Wrong account or hidden model shown");
    require(client.models().first().toMap().value("contextTokens").toLongLong() == 9000,
        "Advertised ChatGPT context capacity was lost");
    const auto credentialPath = QDir(directory.filePath("profile")).filePath("chatgpt-auth.json");
    require(!QFile::exists(credentialPath)
        && credentials.read(directory.filePath("profile"), "chatgpt").found,
        "ChatGPT credentials were written as a file or not saved in the credential store");
    if (name == "refresh") {
        require(boundary.refreshes == 1 && credentials.read(directory.filePath("profile"), "chatgpt").data.contains("private-refresh-2"),
            "Rotating refresh token was not saved before model discovery");
    }
    if (name == "sign_out") {
        credentials.available = false;
        client.signOut();
        require(client.connected() && !client.status().isEmpty(),
            "Failed system-store deletion silently removed the ChatGPT account");
        credentials.available = true;
        client.signOut();
        require(!client.connected() && client.activeClientId().isEmpty() && client.activeAccount().isEmpty(),
            "Signing out retained an account that the single-account UI cannot manage");
        browser.last = QUrl{};
        require(client.signIn() && QTest::qWaitFor([&] { return browser.last.isValid(); }, 1000)
            && QUrlQuery(browser.last).queryItemValue("client_id") == "dynamic_agent_client",
            "Signing out did not allow a new single-account authorization");
        return;
    }
    QString answer, error;
    client.analyze("gpt-test", "Return JSON", "Observed problem code: OOMKilled", [&](QString value, QString failure) {
        answer = value; error = failure;
    });
    require(QTest::qWaitFor([&] { return !answer.isEmpty() || !error.isEmpty(); }, 2000) && error.isEmpty(),
        "ChatGPT response did not complete");
    const auto payload = QJsonDocument::fromJson(boundary.submitted).object();
    require(payload.value("model") == "gpt-test" && payload.value("store") == false && payload.value("stream") == true
        && payload.value("input").toArray().first().toObject().value("content").toString().contains("OOMKilled")
        && answer.contains("Test summary"), "ChatGPT request or streamed answer is wrong");
    if (name == "incident") {
        const podlord::IncidentAi::RemoteEndpoints endpoints{QUrl(boundary.base()), QUrl(boundary.base()),
            QUrl(boundary.base()), QUrl(boundary.base())};
        podlord::IncidentAi ai(directory.filePath("profile"), QUrl(boundary.base()), nullptr, endpoints, &credentials);
        require(ai.setProvider("chatgpt"), "Cannot choose ChatGPT provider");
        require(QTest::qWaitFor([&] { return ai.models().contains("gpt-test"); }, 2000), "Incident model catalog unavailable");
        require(ai.model().isEmpty() && ai.setModel("gpt-test"), "Incident model was automatic or not selectable");
        require(ai.setEnabled("cluster-one", true), "Cannot enable incident analysis");
        const auto path = QStringLiteral("/api/v1/namespaces/team/pods/broken");
        ai.observe("cluster-one", QJsonArray{QJsonObject{{"uid", "pod-1"}, {"kind", "Pod"}, {"name", "broken"},
            {"namespace", "team"}, {"path", path}, {"issue", "OOMKilled"}, {"status", "OOMKilled"},
            {"problemSeverity", 2}, {"secret", "secret-private-marker"}}}, "de");
        require(QTest::qWaitFor([&] { return !ai.result("cluster-one", path).value("summary").toString().isEmpty(); }, 2000),
            "ChatGPT analysis did not reach the public incident result");
        require(!boundary.submitted.contains("secret-private-marker") && boundary.submitted.contains("OOMKilled"),
            "Incident analysis sent unsafe data or lost the error signal");
    }
}
} // namespace
int main(int argc, char** argv) {
    QGuiApplication app(argc, argv);
    scenario(argc > 1 ? QString::fromUtf8(argv[1]) : "success");
}
#include "chatgpt_client_test.moc"
