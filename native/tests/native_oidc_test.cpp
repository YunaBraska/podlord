#include "native_oidc.h"
#include <QCryptographicHash>
#include <QDesktopServices>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTest>
#include <QUrlQuery>
#include <cstdio>
#include <functional>
#include <memory>
#include <openssl/bn.h>
#include <openssl/core_names.h>
#include <openssl/evp.h>
#include <openssl/rsa.h>

namespace {
class BrowserCapture final : public QObject {
    Q_OBJECT
public:
    BrowserCapture() { QDesktopServices::setUrlHandler("https", this, "open"); }
    ~BrowserCapture() override { QDesktopServices::unsetUrlHandler("https"); }
    int opened = 0;
    QUrl last;
public slots:
    void open(const QUrl& url) { last = url; ++opened; }
};

class IdentityBoundary final : public QTcpServer {
public:
    QString expectedRedirect{"berlin.yuna.podlord:/oauth2redirect"};
    explicit IdentityBoundary(QString mode) : mode_(std::move(mode)) {
        if (mode_.startsWith("signed_") || mode_.startsWith("browser_")) {
            auto* context = EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, nullptr);
            if (context && EVP_PKEY_keygen_init(context) > 0 && EVP_PKEY_CTX_set_rsa_keygen_bits(context, 2048) > 0) {
                EVP_PKEY* key = nullptr;
                if (EVP_PKEY_keygen(context, &key) > 0) signingKey_.reset(key);
            }
            EVP_PKEY_CTX_free(context);
        }
        connect(this, &QTcpServer::newConnection, this, [this] {
            while (hasPendingConnections()) {
                auto* socket = nextPendingConnection();
                auto bytes = std::make_shared<QByteArray>();
                connect(socket, &QTcpSocket::readyRead, this, [this, socket, bytes] {
                    bytes->append(socket->readAll());
                    const auto split = bytes->indexOf("\r\n\r\n");
                    if (split < 0) return;
                    const auto header = bytes->left(split);
                    qsizetype length = 0;
                    for (const auto& line : header.split('\n')) {
                        const auto trimmed = line.trimmed();
                        if (trimmed.toLower().startsWith("content-length:")) length = trimmed.mid(15).trimmed().toLongLong();
                    }
                    if (bytes->size() < split + 4 + length) return;
                    socket->disconnect(this);
                    ++requests;
                    const auto path = header.split(' ').value(1);
                    QJsonObject response;
                    int status = 200;
                    if (path == "/.well-known/openid-configuration") {
                        response = {{"issuer", mode_ == "wrong_issuer" ? QStringLiteral("https://wrong.example") : issuer()},
                            {"device_authorization_endpoint", issuer() + "/device"}, {"token_endpoint", issuer() + "/token"},
                            {"jwks_uri", issuer() + "/jwks"}};
                        if (mode_ == "pinned_device") response.remove("device_authorization_endpoint");
                        if (mode_.startsWith("browser_") && mode_ != "browser_missing_auth")
                            response.insert("authorization_endpoint", "https://login.example/authorize");
                        if (mode_ == "discovery_bad_token_url") response["token_endpoint"] = "http://remote.example/token";
                        if (mode_ == "discovery_bad_jwks") response["jwks_uri"] = "http://remote.example/jwks";
                        if (mode_ == "discovery_missing_device") response.remove("device_authorization_endpoint");
                        if (mode_ == "discovery_wrong_status") status = 401;
                        if (mode_ == "discovery_server_error") status = 503;
                    } else if (path == "/device") {
                        response = {{"device_code", "private-device-code"}, {"user_code", "ABCD-EFGH"},
                            {"verification_uri", "https://login.example/verify"}, {"expires_in", 90}, {"interval", 0}};
                        if (mode_ == "malformed") response.remove("device_code");
                        if (mode_ == "device_no_code") response.remove("device_code");
                        if (mode_ == "device_no_user") response.remove("user_code");
                        if (mode_ == "device_bad_uri") response["verification_uri"] = "http://remote.example/verify";
                        if (mode_ == "device_bad_complete") response["verification_uri_complete"] = "http://remote.example/verify";
                        if (mode_ == "device_zero_lifetime") response["expires_in"] = 0;
                        if (mode_ == "device_long_lifetime") response["expires_in"] = 3601;
                        if (mode_ == "device_negative_interval") response["interval"] = -1;
                        if (mode_ == "device_long_interval") response["interval"] = 601;
                        if (mode_ == "device_wrong_status") status = 401;
                        if (mode_ == "device_expired") response["expires_in"] = 1;
                    } else if (path == "/token") {
                        ++tokenRequests;
                        if (mode_ == "denied" || ((mode_ == "pending" || mode_ == "slow_down") && tokenRequests == 1)) {
                            status = 400;
                            response = {{"error", mode_ == "denied" ? "access_denied"
                                : mode_ == "slow_down" ? "slow_down" : "authorization_pending"}};
                        }
                        else if (mode_.startsWith("signed_")) response = {{"id_token", signedToken()}};
                        else if (mode_.startsWith("browser_")) {
                            const QUrlQuery fields(QString::fromUtf8(bytes->mid(split + 4, length)));
                            const auto verifier = fields.queryItemValue("code_verifier", QUrl::FullyDecoded);
                            const auto challenge = QString::fromLatin1(QCryptographicHash::hash(verifier.toLatin1(), QCryptographicHash::Sha256)
                                .toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
                            if (fields.queryItemValue("grant_type") != "authorization_code"
                                || fields.queryItemValue("code") != "test-authorization-code"
                                || fields.queryItemValue("redirect_uri") != expectedRedirect
                                || fields.queryItemValue("client_id") != "podlord-client" || verifier.size() != 43
                                || challenge != expectedChallenge) {
                                status = 400; response = {{"error", "invalid_grant"}};
                            } else response = {{"id_token", signedToken()}, {"access_token", "private-access-token"}, {"expires_in", 3600}};
                        }
                        else if (mode_ == "unsigned_id_token") {
                            const auto header = QJsonDocument(QJsonObject{{"alg", "none"}}).toJson(QJsonDocument::Compact)
                                .toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
                            const auto claims = QJsonDocument(QJsonObject{{"iss", issuer()}, {"aud", "podlord-client"},
                                {"exp", QDateTime::currentDateTimeUtc().addSecs(3600).toSecsSinceEpoch()}})
                                .toJson(QJsonDocument::Compact).toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
                            response = {{"id_token", QString::fromLatin1(header + '.' + claims + ".forged")}};
                        } else {
                            response = {{"access_token", "private-access-token"}, {"expires_in", 3600}};
                            if (mode_ == "token_missing") response.remove("access_token");
                            if (mode_ == "token_space") response["access_token"] = "token with space";
                            if (mode_ == "token_short_lifetime") response["expires_in"] = 30;
                            if (mode_ == "token_long_lifetime") response["expires_in"] = 86401;
                            if (mode_ == "token_wrong_status") status = 401;
                            if (mode_ == "token_server_error") status = 503;
                        }
                        if (mode_ == "browser_missing_id_token") response.remove("id_token");
                        if (mode_ == "browser_missing_access_token") response.remove("access_token");
                        if (mode_ == "browser_short_lifetime") response["expires_in"] = 30;
                        if (mode_ == "browser_long_lifetime") response["expires_in"] = 86401;
                    } else if (path == "/jwks") {
                        ++jwksRequests;
                        response = jwks();
                        if (mode_ == "browser_jwks_server_error") status = 503;
                    } else status = 404;
                    const auto body = mode_ == "discovery_oversized" && path == "/.well-known/openid-configuration"
                        ? QByteArray(70000, 'x') : mode_ == "token_bad_json" && path == "/token"
                        || mode_ == "discovery_bad_json" && path == "/.well-known/openid-configuration"
                        || mode_ == "browser_jwks_bad_json" && path == "/jwks"
                        ? QByteArray("{invalid") : QJsonDocument(response).toJson(QJsonDocument::Compact);
                    socket->write("HTTP/1.1 " + QByteArray::number(status) + " Result\r\nContent-Type: application/json\r\nConnection: close\r\nContent-Length: "
                        + QByteArray::number(body.size()) + "\r\n\r\n" + body);
                    socket->disconnectFromHost();
                });
                connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            }
        });
    }
    QString issuer() const { return QString("http://127.0.0.1:%1").arg(serverPort()); }
    QJsonObject jwks() const {
        if (mode_ == "signed_jwks_non_object") return {{"keys", QJsonArray{42}}};
        if (mode_ == "signed_jwks_empty") return {{"keys", QJsonArray{}}};
        if (signingKey_) {
            BIGNUM* modulus = nullptr;
            BIGNUM* exponent = nullptr;
            const bool extracted = EVP_PKEY_get_bn_param(signingKey_.get(), OSSL_PKEY_PARAM_RSA_N, &modulus) > 0
                && EVP_PKEY_get_bn_param(signingKey_.get(), OSSL_PKEY_PARAM_RSA_E, &exponent) > 0;
            if (extracted) {
                QByteArray n(BN_num_bytes(modulus), char(0));
                QByteArray e(BN_num_bytes(exponent), char(0));
                BN_bn2bin(modulus, reinterpret_cast<unsigned char*>(n.data()));
                BN_bn2bin(exponent, reinterpret_cast<unsigned char*>(e.data()));
                BN_free(modulus); BN_free(exponent);
                const auto encode = [](const QByteArray& bytes) { return QString::fromLatin1(bytes.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals)); };
                QJsonObject key{{"kty", "RSA"}, {"kid", "qa"}, {"alg", "RS256"}, {"use", "sig"},
                    {"n", encode(n)}, {"e", encode(e)}};
                if (mode_ == "signed_jwks_wrong_use") key["use"] = "enc";
                if (mode_ == "signed_jwks_wrong_ops") key["key_ops"] = QJsonArray{"encrypt"};
                if (mode_ == "signed_jwks_bad_modulus") key["n"] = "bad";
                if (mode_ == "signed_jwks_bad_exponent") key["e"] = "bad";
                if (mode_ == "signed_jwks_empty_exponent") key["e"] = "";
                if (mode_ == "signed_jwks_leading_zero") key["n"] = encode(QByteArray(1, char(0)) + n);
                if (mode_ == "signed_jwks_long_modulus") key["n"] = encode(QByteArray(1025, 'a'));
                if (mode_ == "signed_jwks_long_exponent") key["e"] = encode(QByteArray(9, 'a'));
                if (mode_ == "signed_jwks_zero_exponent") key["e"] = encode(QByteArray(1, char(0)) + e);
                if (mode_ == "signed_jwks_missing_kid") key.remove("kid");
                if (mode_ == "signed_jwks_wrong_kty") key["kty"] = "EC";
                if (mode_ == "signed_jwks_wrong_alg") key["alg"] = "HS256";
                if (mode_ == "signed_jwks_duplicate" || mode_ == "signed_no_kid_multiple") return {{"keys", QJsonArray{key, key}}};
                return {{"keys", QJsonArray{key}}};
            }
            BN_free(modulus); BN_free(exponent);
        }
        return {{"keys", QJsonArray{}}};
    }
    QString signedToken() const {
        if (!signingKey_) return {};
        if (mode_ == "signed_malformed_token") return "bad";
        if (mode_ == "signed_empty_part") return "a..b";
        if (mode_ == "signed_empty_header") return ".a.b";
        if (mode_ == "signed_empty_signature") return "a.b.";
        if (mode_ == "signed_bad_base64") return "***.***.***";
        if (mode_ == "signed_bad_claims_base64") return "e30.***.AA";
        if (mode_ == "signed_bad_signature_base64") return "e30.e30.***";
        if (mode_ == "signed_json_array_header") return "W10.e30.AA";
        if (mode_ == "signed_json_array_claims") return "e30.W10.AA";
        const auto now = QDateTime::currentSecsSinceEpoch();
        const auto encode = [](const QJsonObject& object) {
            return QJsonDocument(object).toJson(QJsonDocument::Compact).toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
        };
        QJsonObject header{{"alg", "RS256"}, {"kid", "qa"}};
        if (mode_ == "signed_wrong_algorithm") header["alg"] = "HS256";
        if (mode_ == "signed_critical_header") header["crit"] = QJsonArray{"custom"};
        if (mode_ == "signed_unknown_kid") header["kid"] = "other";
        if (mode_ == "signed_single_key_no_kid") header.remove("kid");
        if (mode_ == "signed_no_kid_multiple") header.remove("kid");
        if (mode_ == "signed_bad_kid_type") header["kid"] = 42;
        QJsonObject claims{{"iss", mode_ == "signed_wrong_issuer" ? QStringLiteral("https://wrong.example") : issuer()},
            {"sub", "test-user"}, {"aud", "podlord-client"}, {"iat", now},
            {"exp", mode_ == "signed_expired" ? now - 1 : now + 3600}};
        if (mode_ == "signed_missing_subject") claims.remove("sub");
        if (mode_ == "signed_unicode_subject") claims["sub"] = QString::fromUtf8("\xc3\xa9");
        if (mode_ == "signed_long_subject") claims["sub"] = QString(256, 'x');
        if (mode_ == "signed_wrong_audience") claims["aud"] = "other-client";
        if (mode_ == "signed_empty_audience") claims["aud"] = QJsonArray{};
        if (mode_ == "signed_nonstring_audience") claims["aud"] = QJsonArray{23};
        if (mode_ == "signed_array_without_client") claims["aud"] = QJsonArray{"other-client"};
        if (mode_ == "signed_wrong_azp") claims["azp"] = "other-client";
        if (mode_ == "signed_audience_array" || mode_ == "signed_audience_without_azp") {
            claims["aud"] = QJsonArray{"podlord-client", "other-client"};
            if (mode_ == "signed_audience_array") claims["azp"] = "podlord-client";
        }
        if (mode_ == "signed_future_issue") claims["iat"] = now + 120;
        if (mode_ == "signed_missing_issue") claims.remove("iat");
        if (mode_ == "signed_future_not_before") claims["nbf"] = now + 120;
        if (mode_ == "signed_negative_not_before") claims["nbf"] = -1;
        if (mode_ == "signed_near_expiry") claims["exp"] = now + 20;
        if (mode_.startsWith("browser_")) claims.insert("nonce", mode_ == "browser_bad_nonce" ? "wrong-nonce" : expectedNonce);
        const auto message = encode(header) + '.' + encode(claims);
        auto* context = EVP_MD_CTX_new();
        if (!context) return {};
        size_t size = size_t(EVP_PKEY_get_size(signingKey_.get()));
        QByteArray signature(qsizetype(size), char(0));
        const bool signedOk = EVP_DigestSignInit(context, nullptr, EVP_sha256(), nullptr, signingKey_.get()) > 0
            && EVP_DigestSignUpdate(context, message.constData(), size_t(message.size())) > 0
            && EVP_DigestSignFinal(context, reinterpret_cast<unsigned char*>(signature.data()), &size) > 0;
        EVP_MD_CTX_free(context);
        if (!signedOk) return {};
        signature.resize(qsizetype(size));
        auto encodedSignature = signature.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
        if (mode_ == "signed_bad_signature") encodedSignature[0] = encodedSignature[0] == 'A' ? 'B' : 'A';
        if (mode_ == "signed_short_signature") encodedSignature = "AA";
        return QString::fromLatin1(message + '.' + encodedSignature);
    }
    int requests = 0;
    int tokenRequests = 0;
    int jwksRequests = 0;
    QString expectedChallenge, expectedNonce;
private:
    QString mode_;
    std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)> signingKey_{nullptr, &EVP_PKEY_free};
};

bool waitFor(const std::function<bool()>& ready, int timeoutMs = 16000) {
    QElapsedTimer timer; timer.start();
    while (!ready() && timer.elapsed() < timeoutMs) QTest::qWait(10);
    return ready();
}

bool parse() {
    enum class Expected { Absent, Ready, Rejected };
    struct ParseCase {
        const char* name;
        QString program;
        QStringList arguments;
        Expected expected;
        bool accessToken = false;
        QStringList scopes;
        QString clientSecret;
        bool deviceCodeGrant = false;
    };
    const QList<ParseCase> cases{
        {"kubectl", "kubectl", {"oidc-login", "get-token", "--oidc-issuer-url=https://issuer.example", "--oidc-client-id=podlord-client"}, Expected::Ready},
        {"kubectl path", "/usr/local/bin/kubectl", {"oidc-login", "get-token", "--oidc-issuer-url=https://issuer.example", "--oidc-client-id=podlord-client"}, Expected::Ready},
        {"plugin executable", "kubectl-oidc_login", {"get-token", "--oidc-issuer-url=https://issuer.example", "--oidc-client-id=podlord-client"}, Expected::Ready},
        {"kubelogin", "kubelogin", {"get-token", "--oidc-issuer-url=https://issuer.example", "--oidc-client-id=podlord-client"}, Expected::Ready},
        {"separate values", "kubectl", {"oidc-login", "get-token", "--oidc-issuer-url", "https://issuer.example", "--oidc-client-id", "podlord-client"}, Expected::Ready},
        {"device grant and options", "kubectl", {"oidc-login", "get-token", "--oidc-issuer-url=https://issuer.example", "--oidc-client-id=podlord-client", "--oidc-client-secret=private-secret", "--oidc-extra-scope=groups,email", "--grant-type=device-code", "--token-cache-storage=none", "--token-cache-dir=/tmp/cache", "--oidc-use-access-token", "--force-refresh"}, Expected::Ready, true, {"groups", "email"}, "private-secret", true},
        {"other executable", "aws", {"oidc-login", "get-token", "--oidc-issuer-url=https://issuer.example", "--oidc-client-id=podlord-client"}, Expected::Absent},
        {"other kubectl command", "kubectl", {"get", "pods", "--oidc-issuer-url=https://issuer.example"}, Expected::Absent},
        {"no issuer", "kubectl", {"oidc-login", "get-token", "--oidc-client-id=podlord-client"}, Expected::Absent},
        {"unknown option", "kubectl", {"oidc-login", "get-token", "--oidc-issuer-url=https://issuer.example", "--oidc-client-id=podlord-client", "--unknown-option"}, Expected::Rejected},
        {"missing issuer value", "kubectl", {"oidc-login", "get-token", "--oidc-issuer-url", "--oidc-client-id=podlord-client"}, Expected::Rejected},
        {"missing client value", "kubectl", {"oidc-login", "get-token", "--oidc-issuer-url=https://issuer.example", "--oidc-client-id="}, Expected::Rejected},
        {"missing client option", "kubectl", {"oidc-login", "get-token", "--oidc-issuer-url=https://issuer.example"}, Expected::Rejected},
        {"nul in client", "kubectl", {"oidc-login", "get-token", "--oidc-issuer-url=https://issuer.example", "--oidc-client-id", QString::fromUtf8(QByteArray("bad\0id", 6))}, Expected::Rejected},
        {"remote plain HTTP", "kubectl", {"oidc-login", "get-token", "--oidc-issuer-url=http://issuer.example", "--oidc-client-id=podlord-client"}, Expected::Rejected},
        {"issuer user info", "kubectl", {"oidc-login", "get-token", "--oidc-issuer-url=https://user@issuer.example", "--oidc-client-id=podlord-client"}, Expected::Rejected},
        {"issuer query", "kubectl", {"oidc-login", "get-token", "--oidc-issuer-url=https://issuer.example?x=1", "--oidc-client-id=podlord-client"}, Expected::Rejected},
        {"issuer fragment", "kubectl", {"oidc-login", "get-token", "--oidc-issuer-url=https://issuer.example/#x", "--oidc-client-id=podlord-client"}, Expected::Rejected},
        {"unsupported grant", "kubectl", {"oidc-login", "get-token", "--oidc-issuer-url=https://issuer.example", "--oidc-client-id=podlord-client", "--grant-type=password"}, Expected::Rejected},
        {"scope whitespace", "kubectl", {"oidc-login", "get-token", "--oidc-issuer-url=https://issuer.example", "--oidc-client-id=podlord-client", "--oidc-extra-scope=groups, team"}, Expected::Rejected},
        {"empty cache option", "kubectl", {"oidc-login", "get-token", "--oidc-issuer-url=https://issuer.example", "--oidc-client-id=podlord-client", "--token-cache-dir="}, Expected::Rejected},
    };
    for (const auto& row : cases) {
        podlord::ExecCommand command;
        command.program = row.program;
        command.arguments = row.arguments;
        const auto parsed = podlord::nativeOidc(command);
        const auto* login = std::get_if<std::optional<podlord::OidcLogin>>(&parsed);
        const bool matched = row.expected == Expected::Rejected
            ? std::holds_alternative<podlord::Failure>(parsed)
            : row.expected == Expected::Absent
                ? login && !login->has_value()
                : login && login->has_value() && login->value().issuer == QUrl("https://issuer.example")
                    && login->value().clientId == "podlord-client" && login->value().useAccessToken == row.accessToken
                    && login->value().scopes == row.scopes && login->value().clientSecret == row.clientSecret
                    && login->value().deviceCodeGrant == row.deviceCodeGrant;
        if (!matched) {
            std::fprintf(stderr, "OIDC parser case failed: %s\n", row.name);
            return false;
        }
    }
    return true;
}

bool aksParse(const QString& scenario) {
    const QString tenant = "11111111-2222-3333-4444-555555555555";
    const QString server = "6dae42f8-4368-4678-94ff-3960e28e3630";
    const QString client = "80faf920-1908-4b52-b5ef-a8e7bedfc67a";
    podlord::ExecCommand command;
    command.program = "/usr/local/bin/kubelogin";
    command.arguments = {"get-token", "--login", "devicecode", "--tenant-id", tenant,
        "--server-id", server, "--client-id", client};
    if (scenario == "aks_browser") command.arguments[2] = "interactive";
    else if (scenario == "aks_missing_client") command.arguments = command.arguments.mid(0, 7);
    else if (scenario == "aks_bad_tenant") command.arguments[4] = "../other";
    else if (scenario == "aks_cli_mode") command.arguments[2] = "azurecli";
    else if (scenario == "aks_unknown_flag") command.arguments.append("--legacy");
    else if (scenario == "aks_sovereign") command.arguments.append({"--environment", "AzureChinaCloud"});
    else if (scenario == "aks_duplicate") command.arguments.append("--tenant-id=" + tenant);
    else if (scenario == "aks_other_program") command.program = "aws";
    else if (scenario == "aks_missing_tenant") command.arguments = {"get-token", "--server-id", server, "--client-id", client};
    else if (scenario == "aks_missing_server") command.arguments = {"get-token", "--tenant-id", tenant, "--client-id", client};
    else if (scenario == "aks_bad_client") command.arguments[8] = "not-a-uuid";
    else if (scenario == "aks_empty_value") command.arguments.append("--token-cache-dir=");
    else if (scenario == "aks_option_as_value") command.arguments.append({"--token-cache-dir", "--login=interactive"});
    else if (scenario == "aks_equals") command.arguments = {"get-token", "--login=interactive", "--tenant-id=" + tenant,
        "--server-id=" + server, "--client-id=" + client};
    else if (scenario == "aks_short_login") command.arguments[1] = "-l";
    const auto parsed = podlord::nativeAks(command);
    if (scenario == "aks_other_program") {
        const auto* login = std::get_if<std::optional<podlord::OidcLogin>>(&parsed);
        return login && !login->has_value();
    }
    if (scenario != "aks_device" && scenario != "aks_browser" && scenario != "aks_equals" && scenario != "aks_short_login")
        return std::holds_alternative<podlord::Failure>(parsed);
    const auto* login = std::get_if<std::optional<podlord::OidcLogin>>(&parsed);
    return login && login->has_value()
        && login->value().issuer == QUrl("https://login.microsoftonline.com/" + tenant + "/v2.0")
        && login->value().deviceEndpoint == QUrl("https://login.microsoftonline.com/" + tenant + "/oauth2/v2.0/devicecode")
        && login->value().clientId == client && login->value().scopes == QStringList{server + "/.default"}
        && login->value().useAccessToken && login->value().deviceCodeGrant == (scenario == "aks_device" || scenario == "aks_short_login");
}

bool deviceFlow(const QString& scenario) {
    IdentityBoundary identity(scenario);
    if (!identity.listen(QHostAddress::LocalHost, 0)) return false;
    BrowserCapture browser;
    podlord::ClusterConnection connection;
    connection.credentialReady = false;
    connection.exec.emplace();
    connection.exec->oidc.emplace();
    connection.exec->oidc->issuer = QUrl(identity.issuer());
    if (scenario == "pinned_device") connection.exec->oidc->deviceEndpoint = QUrl(identity.issuer() + "/device");
    connection.exec->oidc->clientId = "podlord-client";
    if (scenario == "device_with_secret") connection.exec->oidc->clientSecret = "private-secret";
    connection.exec->oidc->useAccessToken = scenario != "unsigned_id_token" && scenario != "discovery_bad_jwks"
        && !scenario.startsWith("signed_");
    podlord::NativeOidcLogin login(std::move(connection));
    std::optional<podlord::Result<podlord::ClusterConnection>> result;
    QObject::connect(&login, &podlord::NativeOidcLogin::completed, &login,
        [&](podlord::Result<podlord::ClusterConnection> value) { result = std::move(value); });
    if (scenario == "cancel") QObject::connect(&login, &podlord::NativeOidcLogin::instructionChanged, &login, [&] { login.cancel(); });
    login.start();
    if (!waitFor([&] { return result.has_value(); }, scenario == "slow_down" ? 22000 : 16000)) return false;
    if (scenario == "success" || scenario == "pending" || scenario == "slow_down"
        || scenario == "pinned_device" || scenario == "device_with_secret") {
        const auto* ready = std::get_if<podlord::ClusterConnection>(&*result);
        return ready && ready->authorization == "Bearer private-access-token" && ready->credentialReady
            && ready->expiresAt > QDateTime::currentDateTimeUtc().addSecs(30)
            && browser.opened == 1 && identity.tokenRequests == (scenario == "pending" || scenario == "slow_down" ? 2 : 1);
    }
    if (scenario == "signed_valid" || scenario == "signed_single_key_no_kid" || scenario == "signed_audience_array") {
        const auto* ready = std::get_if<podlord::ClusterConnection>(&*result);
        return ready && ready->authorization.startsWith("Bearer ey") && ready->credentialReady
            && ready->expiresAt > QDateTime::currentDateTimeUtc().addSecs(30)
            && browser.opened == 1 && identity.tokenRequests == 1 && identity.jwksRequests == 1;
    }
    if (!std::holds_alternative<podlord::Failure>(*result)) return false;
    if (scenario == "unsigned_id_token") {
        const auto failure = std::get<podlord::Failure>(*result);
        return failure.code == podlord::StoreError::InvalidData && browser.opened == 1
            && identity.tokenRequests == 1 && identity.jwksRequests == 1;
    }
    if (scenario.startsWith("signed_")) {
        const auto failure = std::get<podlord::Failure>(*result);
        return failure.code == podlord::StoreError::InvalidData && browser.opened == 1
            && identity.tokenRequests == 1 && identity.jwksRequests == 1;
    }
    if (scenario.startsWith("discovery_")) return browser.opened == 0 && identity.requests == 1;
    if (scenario == "device_expired") return browser.opened == 1 && identity.tokenRequests == 0;
    if (scenario.startsWith("device_")) return browser.opened == 0 && identity.requests == 2;
    if (scenario.startsWith("token_")) return browser.opened == 1 && identity.tokenRequests == 1;
    return scenario == "cancel" ? browser.opened == 0 && identity.requests == 2
        : scenario == "wrong_issuer" ? browser.opened == 0 && identity.requests == 1
        : scenario == "malformed" ? browser.opened == 0 && identity.requests == 2
        : scenario == "denied" && browser.opened == 1 && identity.tokenRequests == 1;
}
bool browserFlow(const QString& scenario) {
    IdentityBoundary identity(scenario);
    if (!identity.listen(QHostAddress::LocalHost, 0)) return false;
    BrowserCapture browser;
    podlord::ClusterConnection connection;
    connection.exec.emplace();
    connection.exec->oidc.emplace();
    connection.exec->oidc->issuer = QUrl(identity.issuer());
    connection.exec->oidc->clientId = "podlord-client";
    if (scenario == "browser_with_secret") connection.exec->oidc->clientSecret = "private-secret";
    const bool google = scenario.startsWith("browser_google_");
    const bool invalidRedirect = scenario.startsWith("browser_redirect_");
    const QString redirectUri = google ? "com.googleusercontent.apps.123456-podlord:/oauth2redirect" : "berlin.yuna.podlord:/oauth2redirect";
    connection.exec->oidc->redirectUri = QUrl(redirectUri);
    identity.expectedRedirect = redirectUri;
    if (scenario == "browser_redirect_http") connection.exec->oidc->redirectUri = QUrl("https://login.example/oauth2redirect");
    if (scenario == "browser_redirect_host") connection.exec->oidc->redirectUri = QUrl("berlin.yuna.podlord://host/oauth2redirect");
    if (scenario == "browser_redirect_query") connection.exec->oidc->redirectUri.setQuery("unexpected=value");
    if (scenario == "browser_redirect_fragment") connection.exec->oidc->redirectUri.setFragment("unexpected");
    if (scenario == "browser_redirect_path") connection.exec->oidc->redirectUri.setPath("/other");
    if (scenario == "browser_redirect_relative") connection.exec->oidc->redirectUri = QUrl("/oauth2redirect");
    connection.exec->oidc->useAccessToken = google || scenario == "browser_access_token" || scenario == "browser_missing_id_token"
        || scenario == "browser_missing_access_token" || scenario == "browser_short_lifetime" || scenario == "browser_long_lifetime";
    podlord::NativeOidcLogin login(std::move(connection), podlord::NativeOidcLogin::Flow::Browser);
    std::optional<podlord::Result<podlord::ClusterConnection>> result;
    QObject::connect(&login, &podlord::NativeOidcLogin::completed, &login,
        [&](podlord::Result<podlord::ClusterConnection> value) { result = std::move(value); });
    if (scenario == "browser_cancel" || scenario == "browser_google_cancel") QObject::connect(&login, &podlord::NativeOidcLogin::instructionChanged, &login, [&] { login.cancel(); });
    login.start();
    if (invalidRedirect)
        return result && std::holds_alternative<podlord::Failure>(*result) && browser.opened == 0 && identity.requests == 0;
    if (scenario == "browser_cancel" || scenario == "browser_google_cancel" || scenario == "browser_missing_auth")
        return waitFor([&] { return result.has_value(); }) && std::holds_alternative<podlord::Failure>(*result)
            && browser.opened == 0 && identity.tokenRequests == 0;
    if (!waitFor([&] { return browser.opened == 1; })) return false;
    const QUrlQuery authorization(browser.last);
    const auto state = authorization.queryItemValue("state", QUrl::FullyDecoded);
    identity.expectedNonce = authorization.queryItemValue("nonce", QUrl::FullyDecoded);
    identity.expectedChallenge = authorization.queryItemValue("code_challenge", QUrl::FullyDecoded);
    if (browser.last.scheme() != "https" || browser.last.host() != "login.example"
        || authorization.queryItemValue("response_type") != "code"
        || authorization.queryItemValue("client_id") != "podlord-client"
        || authorization.queryItemValue("redirect_uri") != redirectUri
        || authorization.queryItemValue("code_challenge_method") != "S256"
        || state.size() != 43 || identity.expectedNonce.size() != 43 || identity.expectedChallenge.size() != 43) return false;
    QUrl redirect(redirectUri);
    QUrlQuery response;
    response.addQueryItem("state", scenario == "browser_bad_state" ? "wrong-state" : state);
    if (scenario == "browser_duplicate_state") response.addQueryItem("state", state);
    if (scenario == "browser_denied") response.addQueryItem("error", "access_denied");
    else if (scenario != "browser_missing_code" && scenario != "browser_duplicate_error") response.addQueryItem("code",
        scenario == "browser_space_code" ? "invalid code" : scenario == "browser_control_code" ? QString(QChar(1))
            : scenario == "browser_empty_code" ? QString{} : scenario == "browser_long_code" ? QString(4097, 'x') : "test-authorization-code");
    if (scenario == "browser_duplicate_code") response.addQueryItem("code", "another-code");
    if (scenario == "browser_code_and_error") response.addQueryItem("error", "access_denied");
    if (scenario == "browser_duplicate_error") { response.addQueryItem("error", "access_denied"); response.addQueryItem("error", "other"); }
    redirect.setQuery(response);
    if (scenario == "browser_google_wrong_callback") {
        QUrl wrong = redirect;
        wrong.setAuthority("another-app");
        if (!QDesktopServices::openUrl(wrong)) return false;
        QTest::qWait(50);
        if (result.has_value() || identity.tokenRequests != 0) return false;
    }
    if (scenario == "browser_wrong_path" || scenario == "browser_fragment") {
        QUrl wrong = redirect;
        if (scenario == "browser_wrong_path") wrong.setPath("/wrong");
        else wrong.setFragment("unexpected");
        if (!QDesktopServices::openUrl(wrong)) return false;
        QTest::qWait(50);
        if (result.has_value() || identity.tokenRequests != 0) return false;
    }
    if (!QDesktopServices::openUrl(redirect)) return false;
    if (scenario == "browser_bad_state" || scenario == "browser_duplicate_state") {
        QTest::qWait(50);
        if (result.has_value() || identity.tokenRequests != 0) return false;
        response.removeAllQueryItems("state"); response.addQueryItem("state", state); redirect.setQuery(response);
        if (!QDesktopServices::openUrl(redirect)) return false;
    }
    if ((scenario == "browser_replay" || scenario == "browser_google_replay") && !QDesktopServices::openUrl(redirect)) return false;
    if (!waitFor([&] { return result.has_value(); })) return false;
    if (scenario == "browser_denied" || scenario == "browser_missing_code" || scenario == "browser_space_code"
        || scenario == "browser_control_code" || scenario == "browser_empty_code" || scenario == "browser_duplicate_error"
        || scenario == "browser_long_code" || scenario == "browser_duplicate_code" || scenario == "browser_code_and_error")
        return std::holds_alternative<podlord::Failure>(*result) && identity.tokenRequests == 0 && identity.jwksRequests == 0;
    if (scenario == "browser_bad_nonce")
        return std::holds_alternative<podlord::Failure>(*result) && identity.tokenRequests == 1 && identity.jwksRequests == 1;
    if (scenario == "browser_jwks_server_error" || scenario == "browser_jwks_bad_json")
        return std::holds_alternative<podlord::Failure>(*result) && identity.tokenRequests == 1 && identity.jwksRequests == 1;
    if (scenario == "browser_missing_id_token" || scenario == "browser_missing_access_token"
        || scenario == "browser_short_lifetime" || scenario == "browser_long_lifetime")
        return std::holds_alternative<podlord::Failure>(*result) && identity.tokenRequests == 1 && identity.jwksRequests == 0;
    const auto* ready = std::get_if<podlord::ClusterConnection>(&*result);
    return ready && ready->credentialReady && ready->expiresAt > QDateTime::currentDateTimeUtc().addSecs(30)
        && ready->authorization == (google || scenario == "browser_access_token" ? "Bearer private-access-token" : ready->authorization)
        && (google || scenario == "browser_access_token" || ready->authorization.startsWith("Bearer ey"))
        && identity.tokenRequests == 1 && identity.jwksRequests == 1;
}

bool invalidStart(const QString& scenario) {
    podlord::ClusterConnection connection;
    if (scenario != "start_missing_exec") connection.exec.emplace();
    if (scenario == "start_invalid_issuer" || scenario == "start_repeat" || scenario == "start_cancel_pending") {
        connection.exec->oidc.emplace();
        connection.exec->oidc->issuer = QUrl(scenario == "start_cancel_pending" ? "http://127.0.0.1:1" : "https://user@example.com");
        connection.exec->oidc->useAccessToken = true;
    }
    podlord::NativeOidcLogin login(std::move(connection));
    int completions = 0;
    QObject::connect(&login, &podlord::NativeOidcLogin::completed, &login,
        [&](podlord::Result<podlord::ClusterConnection> result) {
            if (std::holds_alternative<podlord::Failure>(result)) ++completions;
        });
    login.start();
    if (scenario == "start_repeat") login.start();
    if (scenario == "start_cancel_pending") login.cancel();
    QTest::qWait(20);
    return completions == 1;
}
} // namespace

int main(int argc, char** argv) {
    QGuiApplication app(argc, argv);
    const auto args = app.arguments();
    const auto scenario = args.value(1);
    const bool passed = scenario == "parse" ? parse()
        : scenario.startsWith("start_") ? invalidStart(scenario)
        : scenario.startsWith("aks_") ? aksParse(scenario)
        : scenario.startsWith("browser_") ? browserFlow(scenario)
        : QStringList{"success", "pinned_device", "cancel", "wrong_issuer", "malformed", "denied", "pending", "slow_down", "unsigned_id_token",
            "discovery_bad_token_url", "discovery_bad_jwks", "discovery_missing_device", "discovery_wrong_status",
            "discovery_server_error", "discovery_bad_json", "discovery_oversized", "device_with_secret",
            "device_no_code", "device_no_user", "device_bad_uri",
            "device_bad_complete", "device_zero_lifetime", "device_long_lifetime", "device_negative_interval",
            "device_long_interval", "device_wrong_status", "device_expired", "token_missing", "token_space", "token_short_lifetime",
            "token_long_lifetime", "token_wrong_status", "token_server_error", "token_bad_json",
            "signed_valid", "signed_bad_signature", "signed_wrong_issuer", "signed_expired",
            "signed_malformed_token", "signed_empty_part", "signed_bad_base64", "signed_bad_claims_base64",
            "signed_bad_signature_base64", "signed_bad_kid_type",
            "signed_empty_header", "signed_empty_signature", "signed_json_array_header", "signed_json_array_claims",
            "signed_short_signature", "signed_jwks_non_object", "signed_jwks_empty", "signed_jwks_long_modulus",
            "signed_jwks_long_exponent", "signed_jwks_zero_exponent",
            "signed_no_kid_multiple", "signed_jwks_bad_exponent", "signed_jwks_empty_exponent", "signed_jwks_leading_zero",
            "signed_wrong_algorithm", "signed_critical_header", "signed_unknown_kid", "signed_single_key_no_kid",
            "signed_missing_subject", "signed_unicode_subject", "signed_wrong_audience", "signed_audience_array",
            "signed_audience_without_azp", "signed_future_issue", "signed_future_not_before", "signed_near_expiry",
            "signed_jwks_wrong_use", "signed_jwks_wrong_ops", "signed_jwks_bad_modulus", "signed_jwks_missing_kid",
            "signed_jwks_wrong_kty", "signed_jwks_wrong_alg", "signed_jwks_duplicate", "signed_long_subject",
            "signed_empty_audience", "signed_nonstring_audience", "signed_array_without_client", "signed_wrong_azp",
            "signed_missing_issue", "signed_negative_not_before"}.contains(scenario) && deviceFlow(scenario);
    if (!passed) std::fprintf(stderr, "Native OIDC scenario failed: %s\n", qPrintable(scenario));
    return passed ? 0 : 1;
}

#include "native_oidc_test.moc"
