#include "native_gke.h"
#include <QCoreApplication>
#include <QDateTime>
#include <QEventLoop>
#include <QJsonDocument>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTest>
#include <QUrlQuery>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rsa.h>
#include <memory>

using namespace podlord;

namespace {
QByteArray privateKey(int bits = 2048, bool elliptic = false) {
    EVP_PKEY* raw = elliptic ? EVP_PKEY_Q_keygen(nullptr, nullptr, "EC", "prime256v1") : EVP_RSA_gen(bits);
    if (!raw) return {};
    std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)> key(raw, EVP_PKEY_free);
    std::unique_ptr<BIO, decltype(&BIO_free)> output(BIO_new(BIO_s_mem()), BIO_free);
    if (!output || PEM_write_bio_PrivateKey(output.get(), key.get(), nullptr, nullptr, 0, nullptr, nullptr) != 1) return {};
    BUF_MEM* memory = nullptr;
    BIO_get_mem_ptr(output.get(), &memory);
    return memory ? QByteArray(memory->data, memory->length) : QByteArray{};
}

GkeServiceAccount account() { return {"podlord@example.iam.gserviceaccount.com", "0123456789abcdef", privateKey()}; }
ExecCommand command(QStringList arguments = {}) { ExecCommand value; value.program = "/usr/bin/gke-gcloud-auth-plugin"; value.arguments = std::move(arguments); return value; }
ClusterConnection connection(std::optional<GkeServiceAccount> serviceAccount = {}, bool serviceOnly = false) {
    ClusterConnection value;
    value.credentialId = "gke";
    value.credentialReady = false;
    value.exec.emplace();
    value.exec->gke = GkeLogin{std::move(serviceAccount), serviceOnly};
    return value;
}
bool waitFor(const std::function<bool()>& predicate) {
    for (int elapsed = 0; elapsed < 3000 && !predicate(); elapsed += 10) QTest::qWait(10);
    return predicate();
}
QByteArray serviceJson(const GkeServiceAccount& value) {
    return QJsonDocument(QJsonObject{{"type", "service_account"}, {"client_email", value.clientEmail},
        {"private_key_id", value.privateKeyId}, {"private_key", QString::fromUtf8(value.privateKey)},
        {"token_uri", "https://oauth2.googleapis.com/token"}}).toJson(QJsonDocument::Compact);
}

class TokenServer final : public QTcpServer {
public:
    TokenServer() {
        connect(this, &QTcpServer::newConnection, this, [this] {
            auto* socket = nextPendingConnection();
            auto bytes = std::make_shared<QByteArray>();
            connect(socket, &QTcpSocket::readyRead, socket, [this, socket, bytes] {
                bytes->append(socket->readAll());
                const auto headerEnd = bytes->indexOf("\r\n\r\n");
                if (headerEnd < 0) return;
                const auto match = QRegularExpression("Content-Length: (\\d+)", QRegularExpression::CaseInsensitiveOption)
                    .match(QString::fromLatin1(bytes->left(headerEnd)));
                if (!match.hasMatch()) return;
                const auto length = match.captured(1).toInt();
                if (bytes->size() < headerEnd + 4 + length) return;
                const QUrlQuery form(QString::fromUtf8(bytes->mid(headerEnd + 4, length)));
                const auto assertion = form.queryItemValue("assertion", QUrl::FullyDecoded).toLatin1().split('.');
                if (assertion.size() == 3) {
                    const auto claims = QJsonDocument::fromJson(QByteArray::fromBase64(assertion[1], QByteArray::Base64UrlEncoding)).object();
                    validScope = claims.value("scope").toString().contains("/auth/cloud-platform")
                        && claims.value("scope").toString().contains("/auth/userinfo.email");
                    std::unique_ptr<BIO, decltype(&BIO_free)> input(BIO_new_mem_buf(key.constData(), key.size()), BIO_free);
                    std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)> publicKey(
                        input ? PEM_read_bio_PrivateKey(input.get(), nullptr, nullptr, nullptr) : nullptr, EVP_PKEY_free);
                    std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> verify(EVP_MD_CTX_new(), EVP_MD_CTX_free);
                    const auto signedBytes = assertion[0] + '.' + assertion[1];
                    const auto signature = QByteArray::fromBase64(assertion[2], QByteArray::Base64UrlEncoding);
                    validSignature = publicKey && verify
                        && EVP_DigestVerifyInit(verify.get(), nullptr, EVP_sha256(), nullptr, publicKey.get()) == 1
                        && EVP_DigestVerify(verify.get(), reinterpret_cast<const unsigned char*>(signature.constData()),
                            size_t(signature.size()), reinterpret_cast<const unsigned char*>(signedBytes.constData()),
                            size_t(signedBytes.size())) == 1;
                }
                if (holdResponse) return;
                socket->write("HTTP/1.1 " + QByteArray::number(status) + " Response\r\nContent-Type: application/json\r\nContent-Length: "
                    + QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body);
                socket->disconnectFromHost();
            });
        });
        listen(QHostAddress::LocalHost);
    }
    bool validScope = false;
    bool validSignature = false;
    QByteArray key;
    int status = 200;
    bool holdResponse = false;
    QByteArray body = R"({"access_token":"service-token","expires_in":3600,"token_type":"Bearer"})";
};

bool loginScenario(const QString& scenario) {
    int starts = 0, cancels = 0, completions = 0;
    std::function<void(NativeGkeLogin::AuthorizationResult)> callback;
    NativeGkeLogin::Authorize authorize = [&](auto done) { ++starts; callback = std::move(done); };
    auto value = connection();
    TokenServer server;
    if (scenario == "no_play_services_fallback") {
        auto imported = account();
        server.key = imported.privateKey;
        value = connection(std::move(imported));
    }
    NativeGkeLogin login(std::move(value), authorize, [&] { ++cancels; },
        QUrl("http://127.0.0.1:" + QString::number(server.serverPort())));
    std::optional<Result<ClusterConnection>> result;
    QObject::connect(&login, &NativeGkeLogin::completed, &login, [&](Result<ClusterConnection> value) { ++completions; result = std::move(value); });
    if (scenario == "cancel_before_start") login.cancel();
    login.start();
    if (scenario == "reuse") login.start();
    if (scenario == "cancel") {
        login.cancel();
        callback({NativeGkeLogin::AuthorizationStatus::Success, "late-token"});
        return completions == 1 && cancels == 1 && std::holds_alternative<Failure>(*result);
    }
    if (scenario == "success" || scenario == "reuse" || scenario == "personal_expiry" || scenario == "cancel_after_done"
        || scenario == "cancel_before_start" || scenario == "start_after_done")
        callback({NativeGkeLogin::AuthorizationStatus::Success, "personal-token"});
    else if (scenario == "failure") callback({NativeGkeLogin::AuthorizationStatus::Failure, {}});
    else if (scenario == "cancelled") callback({NativeGkeLogin::AuthorizationStatus::Cancelled, {}});
    else if (scenario == "expired_token") callback({NativeGkeLogin::AuthorizationStatus::Success, "expired-token",
        QDateTime::currentDateTimeUtc().addSecs(-5)});
    else if (scenario == "empty_token") callback({NativeGkeLogin::AuthorizationStatus::Success, {}});
    else if (scenario == "long_token") callback({NativeGkeLogin::AuthorizationStatus::Success, QByteArray(16385, 'a')});
    else if (scenario == "space_token") callback({NativeGkeLogin::AuthorizationStatus::Success, "bad token"});
    else if (scenario == "delete_token") callback({NativeGkeLogin::AuthorizationStatus::Success, QByteArray("bad") + char(0x7f)});
    else callback({NativeGkeLogin::AuthorizationStatus::Unavailable, {}});
    if (!waitFor([&] { return result.has_value(); })) return false;
    if (scenario == "cancel_after_done") login.cancel();
    if (scenario == "start_after_done") login.start();
    if (scenario == "success" || scenario == "reuse" || scenario == "personal_expiry" || scenario == "cancel_after_done"
        || scenario == "cancel_before_start" || scenario == "start_after_done") {
        const auto* ready = std::get_if<ClusterConnection>(&*result);
        if (scenario == "personal_expiry") {
            const auto now = QDateTime::currentDateTimeUtc();
            return ready && ready->expiresAt > now.addSecs(60) && ready->expiresAt <= now.addSecs(1800);
        }
        return ready && ready->authorization == "Bearer personal-token" && ready->credentialReady
            && starts == 1 && completions == 1 && cancels == 0;
    }
    if (scenario == "no_play_services_fallback") {
        const auto* ready = std::get_if<ClusterConnection>(&*result);
        return ready && ready->authorization == "Bearer service-token" && ready->credentialReady
            && ready->exec->gke->serviceAccount->privateKey.isEmpty() && server.validScope && server.validSignature;
    }
    return std::holds_alternative<Failure>(*result) && completions == 1;
}

bool malformedKeyScenario(const QString& scenario) {
    if (scenario == "parse_bad_json") return std::holds_alternative<Failure>(parseGkeServiceAccount("{"));
    if (scenario == "parse_array") return std::holds_alternative<Failure>(parseGkeServiceAccount("[]"));
    auto object = QJsonDocument::fromJson(serviceJson(account())).object();
    if (scenario == "parse_missing_type") object.remove("type");
    else if (scenario == "parse_missing_email") object.remove("client_email");
    else if (scenario == "parse_missing_key_id") object.remove("private_key_id");
    else if (scenario == "parse_missing_key") object.remove("private_key");
    else if (scenario == "parse_missing_uri") object.remove("token_uri");
    else if (scenario == "parse_wrong_type") object.insert("type", "authorized_user");
    else if (scenario == "parse_wrong_uri") object.insert("token_uri", "https://example.invalid/token");
    else if (scenario == "parse_bad_email") object.insert("client_email", "invalid");
    else if (scenario == "parse_bad_key_id") object.insert("private_key_id", "invalid");
    else if (scenario == "parse_oversized_key") object.insert("private_key", QString(32769, 'x'));
    else if (scenario == "parse_ec_key") object.insert("private_key", QString::fromUtf8(privateKey(2048, true)));
    else return false;
    return std::holds_alternative<Failure>(parseGkeServiceAccount(QJsonDocument(object).toJson(QJsonDocument::Compact)));
}

bool serviceScenario(const QString& scenario) {
    TokenServer server;
    auto imported = account();
    server.key = imported.privateKey;
    if (scenario == "service_http_failure") server.status = 401;
    else if (scenario == "service_bad_json") server.body = "{";
    else if (scenario == "service_array") server.body = "[]";
    else if (scenario == "service_wrong_type") server.body = R"({"access_token":"token","expires_in":3600,"token_type":"Basic"})";
    else if (scenario == "service_short_expiry") server.body = R"({"access_token":"token","expires_in":30,"token_type":"Bearer"})";
    else if (scenario == "service_long_expiry") server.body = R"({"access_token":"token","expires_in":86401,"token_type":"Bearer"})";
    else if (scenario == "service_empty_token") server.body = R"({"access_token":"","expires_in":3600,"token_type":"Bearer"})";
    else if (scenario == "service_space_token") server.body = R"({"access_token":"bad token","expires_in":3600,"token_type":"Bearer"})";
    else if (scenario == "service_oversized") server.body += QByteArray(65537, ' ');
    else if (scenario == "service_cancel_pending") server.holdResponse = true;
    else if (scenario != "service_only_success") return false;
    NativeGkeLogin login(connection(std::move(imported), true), {}, {},
        QUrl("http://127.0.0.1:" + QString::number(server.serverPort())));
    std::optional<Result<ClusterConnection>> result;
    QObject::connect(&login, &NativeGkeLogin::completed, &login, [&](Result<ClusterConnection> value) { result = std::move(value); });
    login.start();
    if (scenario == "service_cancel_pending") {
        if (!waitFor([&] { return server.validSignature; })) return false;
        login.cancel();
        return waitFor([&] { return result.has_value(); }) && std::holds_alternative<Failure>(*result)
            && std::get<Failure>(*result).message.contains("cancelled", Qt::CaseInsensitive);
    }
    if (!waitFor([&] { return result.has_value(); })) return false;
    if (scenario == "service_only_success") {
        const auto* ready = std::get_if<ClusterConnection>(&*result);
        return ready && ready->authorization == "Bearer service-token"
            && ready->exec->gke->serviceAccount->privateKey.isEmpty()
            && ready->expiresAt > QDateTime::currentDateTimeUtc().addSecs(1800)
            && server.validScope && server.validSignature;
    }
    return std::holds_alternative<Failure>(*result) && server.validSignature;
}

bool invalidStartScenario(const QString& scenario) {
    auto value = connection();
    if (scenario == "start_missing_exec") value.exec.reset();
    else if (scenario == "start_missing_gke") value.exec->gke.reset();
    else if (scenario == "service_missing_key") value.exec->gke->serviceAccountOnly = true;
    else if (scenario == "service_bad_key") value.exec->gke = GkeLogin{GkeServiceAccount{"user@example.com", "0123456789abcdef", "invalid"}, true};
    else if (scenario == "service_weak_key") value.exec->gke = GkeLogin{GkeServiceAccount{"user@example.com", "0123456789abcdef", privateKey(1024)}, true};
    else if (scenario == "service_ec_key") value.exec->gke = GkeLogin{GkeServiceAccount{"user@example.com", "0123456789abcdef", privateKey(2048, true)}, true};
    else return false;
    NativeGkeLogin login(std::move(value), {}, {}, QUrl("http://127.0.0.1:1"));
    std::optional<Result<ClusterConnection>> result;
    QObject::connect(&login, &NativeGkeLogin::completed, &login, [&](Result<ClusterConnection> value) { result = std::move(value); });
    login.start();
    login.start();
    return result && std::holds_alternative<Failure>(*result);
}
} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    const auto scenario = app.arguments().value(1);
#ifdef PODLORD_OPENSSL_FAULT_TESTS
    if (scenario == "service_bio_failure") {
        NativeGkeLogin login(connection(account(), true), {}, {}, QUrl("http://127.0.0.1:1"));
        std::optional<Result<ClusterConnection>> result;
        QObject::connect(&login, &NativeGkeLogin::completed, &login,
            [&](Result<ClusterConnection> value) { result = std::move(value); });
        qputenv("PODLORD_TEST_BIO_ALLOCATION_FAILURE", "1");
        login.start();
        qunsetenv("PODLORD_TEST_BIO_ALLOCATION_FAILURE");
        return result && std::holds_alternative<Failure>(*result) ? 0 : 1;
    }
#endif
    bool passed = false;
    if (scenario.startsWith("browser_config")) {
        const auto client = scenario == "browser_config_missing" ? QString{}
            : scenario == "browser_config_invalid" ? QStringLiteral("client@example.com")
            : scenario == "browser_config_oversized" ? QString(240, '1') + ".apps.googleusercontent.com"
            : scenario == "browser_config_numeric" ? QStringLiteral("123456.apps.googleusercontent.com")
            : QStringLiteral("123456-podlord.apps.googleusercontent.com");
        const auto result = nativeGkeBrowserConfig(client);
        if (scenario == "browser_config_missing" || scenario == "browser_config_invalid" || scenario == "browser_config_oversized")
            passed = std::holds_alternative<Failure>(result);
        else if (const auto* login = std::get_if<OidcLogin>(&result))
            passed = login->issuer == QUrl("https://accounts.google.com") && login->clientId == client
                && login->clientSecret.isEmpty() && login->useAccessToken && !login->deviceCodeGrant
                && login->scopes == QStringList{"https://www.googleapis.com/auth/cloud-platform", "https://www.googleapis.com/auth/userinfo.email"}
                && login->redirectUri == QUrl(scenario == "browser_config_numeric"
                    ? "com.googleusercontent.apps.123456:/oauth2redirect" : "com.googleusercontent.apps.123456-podlord:/oauth2redirect");
    } else if (scenario == "parse_personal") {
        const auto result = nativeGkeConfig(command());
        const auto* login = std::get_if<std::optional<GkeLogin>>(&result);
        passed = login && *login && !(*login)->serviceAccountOnly;
    } else if (scenario == "parse_adc") {
        const auto result = nativeGkeConfig(command({"--use_application_default_credentials"}));
        const auto* login = std::get_if<std::optional<GkeLogin>>(&result);
        passed = login && *login && (*login)->serviceAccountOnly;
    } else if (scenario == "parse_bad_option") {
        passed = std::holds_alternative<Failure>(nativeGkeConfig(command({"--account", "user@example.com"})));
    } else if (scenario == "parse_environment") {
        auto value = command(); value.environment.insert("GOOGLE_APPLICATION_CREDENTIALS", "/tmp/key.json");
        passed = std::holds_alternative<Failure>(nativeGkeConfig(value));
    } else if (scenario == "parse_other_program" || scenario == "parse_windows_program") {
        auto value = command(); value.program = scenario == "parse_other_program" ? "kubectl" : "gke-gcloud-auth-plugin.exe";
        const auto result = nativeGkeConfig(value);
        const auto* login = std::get_if<std::optional<GkeLogin>>(&result);
        passed = login && (scenario == "parse_other_program" ? !login->has_value() : login->has_value());
    } else if (scenario == "parse_service_account") {
        const auto value = account();
        const auto parsed = parseGkeServiceAccount(serviceJson(value));
        const auto* result = std::get_if<GkeServiceAccount>(&parsed);
        passed = result && result->clientEmail == value.clientEmail && result->privateKey == value.privateKey;
    } else if (scenario == "parse_bad_service_account") {
        auto value = account(); value.privateKey = "invalid";
        passed = std::holds_alternative<Failure>(parseGkeServiceAccount(serviceJson(value)));
    } else if (scenario.startsWith("parse_")) {
        passed = malformedKeyScenario(scenario);
    } else if (scenario.startsWith("service_") && scenario != "service_missing_key" && scenario != "service_bad_key"
        && scenario != "service_weak_key" && scenario != "service_ec_key") {
        passed = serviceScenario(scenario);
    } else if (scenario.startsWith("start_") && scenario != "start_after_done"
        || scenario == "service_missing_key" || scenario == "service_bad_key" || scenario == "service_weak_key" || scenario == "service_ec_key") {
        passed = invalidStartScenario(scenario);
    } else if (scenario == "success" || scenario == "failure" || scenario == "cancel"
        || scenario == "no_play_services" || scenario == "no_play_services_fallback" || scenario == "reuse"
        || scenario == "cancelled" || scenario == "expired_token" || scenario == "empty_token" || scenario == "long_token"
        || scenario == "space_token" || scenario == "delete_token" || scenario == "personal_expiry" || scenario == "cancel_after_done"
        || scenario == "cancel_before_start" || scenario == "start_after_done") {
        passed = loginScenario(scenario);
    } else return 2;
    return passed ? 0 : 1;
}
