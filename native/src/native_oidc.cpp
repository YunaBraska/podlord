#include "native_oidc.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDesktopServices>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QTimeZone>
#include <QUrlQuery>
#include <algorithm>
#include <array>
#ifdef Q_OS_IOS
#include <Security/Security.h>
#else
#include <openssl/evp.h>
#include <openssl/rsa.h>
#include <openssl/x509.h>
#include <memory>
#endif

namespace podlord {
namespace {
bool localHost(const QString& host) { return host == "localhost" || host == "127.0.0.1" || host == "::1"; }
bool safeUrl(const QUrl& url) {
    return url.isValid() && !url.host().isEmpty() && url.userInfo().isEmpty() && !url.hasFragment()
        && (url.scheme() == "https" || (url.scheme() == "http" && localHost(url.host())));
}
QByteArray form(const QList<QPair<QString, QString>>& fields) {
    QUrlQuery query;
    for (const auto& [name, value] : fields) query.addQueryItem(name, value);
    return query.toString(QUrl::FullyEncoded).toUtf8();
}
Failure invalidLogin() { return {StoreError::InvalidData, "The identity provider returned an invalid OIDC response. No Kubernetes request was sent."}; }
class OidcRedirectRouter final : public QObject {
    Q_OBJECT
public:
    explicit OidcRedirectRouter(QObject* parent) : QObject(parent) {}
    void watch(const QString& scheme) {
        if (schemes_.contains(scheme)) return;
        QDesktopServices::setUrlHandler(scheme, this, "opened");
        schemes_.append(scheme);
    }
    ~OidcRedirectRouter() override {
        for (const auto& scheme : schemes_) QDesktopServices::unsetUrlHandler(scheme);
    }
signals:
    void received(const QUrl& url);
public slots:
    void opened(const QUrl& url) { emit received(url); }
private:
    QStringList schemes_;
};
OidcRedirectRouter* redirectRouter(const QString& scheme) {
    static QPointer<OidcRedirectRouter> router;
    if (!router) router = new OidcRedirectRouter(QCoreApplication::instance());
    router->watch(scheme);
    return router;
}
QString randomValue() {
    std::array<quint32, 8> words;
    QRandomGenerator::system()->generate(words.begin(), words.end());
    const QByteArray bytes(reinterpret_cast<const char*>(words.data()), sizeof(words));
    return QString::fromLatin1(bytes.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
}
QByteArray derLength(qsizetype size) {
    QByteArray bytes;
    do { bytes.prepend(char(size & 0xff)); size >>= 8; } while (size);
    return bytes.size() == 1 && static_cast<uchar>(bytes[0]) < 128
        ? bytes : QByteArray(1, char(0x80 | bytes.size())) + bytes;
}
QByteArray derInteger(QByteArray value) {
    if (static_cast<uchar>(value[0]) >= 128) value.prepend(char(0));
    return QByteArray(1, char(0x02)) + derLength(value.size()) + value;
}
bool verifyRs256(const QJsonObject& key, const QByteArray& message, const QByteArray& signature) {
    const auto modulus = QByteArray::fromBase64Encoding(key.value("n").toString().toLatin1(), QByteArray::Base64UrlEncoding | QByteArray::AbortOnBase64DecodingErrors);
    const auto exponent = QByteArray::fromBase64Encoding(key.value("e").toString().toLatin1(), QByteArray::Base64UrlEncoding | QByteArray::AbortOnBase64DecodingErrors);
    if (!modulus || !exponent || modulus.decoded.size() < 256 || modulus.decoded.size() > 1024
        || exponent.decoded.isEmpty() || exponent.decoded.size() > 8 || modulus.decoded[0] == 0 || exponent.decoded[0] == 0
        || signature.size() != modulus.decoded.size()) return false;
    const auto integers = derInteger(modulus.decoded) + derInteger(exponent.decoded);
    const auto encoded = QByteArray(1, char(0x30)) + derLength(integers.size()) + integers;
#ifdef Q_OS_IOS
    const int bits = int(modulus.decoded.size() * 8);
    CFNumberRef bitCount = CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &bits);
    CFMutableDictionaryRef attributes = CFDictionaryCreateMutable(kCFAllocatorDefault, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    CFDataRef keyData = CFDataCreate(kCFAllocatorDefault, reinterpret_cast<const UInt8*>(encoded.constData()), encoded.size());
    SecKeyRef publicKey = nullptr;
    if (bitCount && attributes && keyData) {
        CFDictionarySetValue(attributes, kSecAttrKeyType, kSecAttrKeyTypeRSA);
        CFDictionarySetValue(attributes, kSecAttrKeyClass, kSecAttrKeyClassPublic);
        CFDictionarySetValue(attributes, kSecAttrKeySizeInBits, bitCount);
        publicKey = SecKeyCreateWithData(keyData, attributes, nullptr);
    }
    CFDataRef signedData = publicKey ? CFDataCreate(kCFAllocatorDefault, reinterpret_cast<const UInt8*>(message.constData()), message.size()) : nullptr;
    CFDataRef signatureData = publicKey ? CFDataCreate(kCFAllocatorDefault, reinterpret_cast<const UInt8*>(signature.constData()), signature.size()) : nullptr;
    const bool valid = publicKey && signedData && signatureData
        && SecKeyVerifySignature(publicKey, kSecKeyAlgorithmRSASignatureMessagePKCS1v15SHA256, signedData, signatureData, nullptr);
    if (signatureData) CFRelease(signatureData);
    if (signedData) CFRelease(signedData);
    if (publicKey) CFRelease(publicKey);
    if (keyData) CFRelease(keyData);
    if (attributes) CFRelease(attributes);
    if (bitCount) CFRelease(bitCount);
    return valid;
#else
    const auto algorithm = QByteArray::fromHex("300d06092a864886f70d0101010500");
    const auto bitString = QByteArray(1, char(0x03)) + derLength(encoded.size() + 1) + QByteArray(1, char(0)) + encoded;
    const auto payload = algorithm + bitString;
    const auto spki = QByteArray(1, char(0x30)) + derLength(payload.size()) + payload;
    const auto* cursor = reinterpret_cast<const unsigned char*>(spki.constData());
    const auto* end = cursor + spki.size();
    std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)> publicKey(d2i_PUBKEY(nullptr, &cursor, spki.size()), EVP_PKEY_free);
    std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> context(EVP_MD_CTX_new(), EVP_MD_CTX_free);
    EVP_PKEY_CTX* parameters = nullptr;
    return publicKey && context && cursor == end
        && EVP_DigestVerifyInit(context.get(), &parameters, EVP_sha256(), nullptr, publicKey.get()) == 1
        && EVP_PKEY_CTX_set_rsa_padding(parameters, RSA_PKCS1_PADDING) == 1
        && EVP_DigestVerifyUpdate(context.get(), message.constData(), size_t(message.size())) == 1
        && EVP_DigestVerifyFinal(context.get(), reinterpret_cast<const unsigned char*>(signature.constData()), size_t(signature.size())) == 1;
#endif
}
std::optional<QDateTime> verifiedIdExpiry(const QString& token, const OidcLogin& oidc, const QJsonObject& jwks, const QString& expectedNonce) {
    const auto parts = token.toLatin1().split('.');
    if (parts.size() != 3 || parts[0].isEmpty() || parts[1].isEmpty() || parts[2].isEmpty()) return {};
    const auto headerBytes = QByteArray::fromBase64Encoding(parts[0], QByteArray::Base64UrlEncoding | QByteArray::AbortOnBase64DecodingErrors);
    const auto claimsBytes = QByteArray::fromBase64Encoding(parts[1], QByteArray::Base64UrlEncoding | QByteArray::AbortOnBase64DecodingErrors);
    const auto signature = QByteArray::fromBase64Encoding(parts[2], QByteArray::Base64UrlEncoding | QByteArray::AbortOnBase64DecodingErrors);
    if (!headerBytes || !claimsBytes || !signature) return {};
    const auto headerDocument = QJsonDocument::fromJson(headerBytes.decoded);
    const auto claimsDocument = QJsonDocument::fromJson(claimsBytes.decoded);
    if (!headerDocument.isObject() || !claimsDocument.isObject()) return {};
    const auto header = headerDocument.object();
    const auto claims = claimsDocument.object();
    if (header.value("alg") != "RS256" || header.contains("crit") || (header.contains("kid") && !header.value("kid").isString())) return {};
    const auto keys = jwks.value("keys").toArray();
    const auto kid = header.value("kid").toString();
    QJsonObject selected;
    int matches = 0;
    for (const auto& entry : keys) {
        if (!entry.isObject()) continue;
        const auto key = entry.toObject();
        if ((kid.isEmpty() && keys.size() != 1) || (!kid.isEmpty() && key.value("kid").toString() != kid)) continue;
        selected = key; ++matches;
    }
    if (matches != 1 || selected.value("kty") != "RSA" || (selected.contains("alg") && selected.value("alg") != "RS256")
        || (selected.contains("use") && selected.value("use") != "sig")
        || (selected.contains("key_ops") && !selected.value("key_ops").toArray().contains("verify"))) return {};
    if (!verifyRs256(selected, parts[0] + '.' + parts[1], signature.decoded)) return {};
    const auto subject = claims.value("sub").toString();
    if (claims.value("iss").toString() != oidc.issuer.toString() || subject.isEmpty() || subject.size() > 255
        || std::any_of(subject.cbegin(), subject.cend(), [](QChar c) { return c.unicode() > 127; })) return {};
    if (!expectedNonce.isEmpty() && claims.value("nonce").toString() != expectedNonce) return {};
    const auto audience = claims.value("aud");
    bool intended = audience.isString() && audience.toString() == oidc.clientId;
    if (audience.isArray()) {
        const auto values = audience.toArray();
        if (values.isEmpty() || std::any_of(values.cbegin(), values.cend(), [](const QJsonValue& v) { return !v.isString(); })) return {};
        intended = values.contains(oidc.clientId);
        if (values.size() > 1 && claims.value("azp").toString() != oidc.clientId) return {};
    }
    if (!intended || (claims.contains("azp") && claims.value("azp").toString() != oidc.clientId)) return {};
    const auto now = QDateTime::currentSecsSinceEpoch();
    const auto exp = claims.value("exp").toInteger(-1);
    const auto iat = claims.value("iat").toInteger(-1);
    if (exp <= now + 30 || iat <= 0 || iat > now + 60
        || (claims.contains("nbf") && (claims.value("nbf").toInteger(-1) < 0 || claims.value("nbf").toInteger(-1) > now + 60))) return {};
    return QDateTime::fromSecsSinceEpoch(exp, QTimeZone::utc());
}
} // namespace

Result<std::optional<OidcLogin>> nativeOidc(const ExecCommand& command) {
    const auto name = QFileInfo(command.program).fileName().toLower();
    int first = -1;
    if (name == "kubectl" && command.arguments.size() >= 2 && command.arguments[0] == "oidc-login" && command.arguments[1] == "get-token") first = 2;
    else if ((name == "kubectl-oidc_login" || name == "kubelogin") && !command.arguments.isEmpty() && command.arguments[0] == "get-token") first = 1;
    if (first < 0) return std::optional<OidcLogin>{};
    bool hasIssuer = false;
    for (int i = first; i < command.arguments.size(); ++i)
        if (command.arguments[i] == "--oidc-issuer-url" || command.arguments[i].startsWith("--oidc-issuer-url=")) hasIssuer = true;
    if (!hasIssuer) return std::optional<OidcLogin>{};
    OidcLogin login;
    QString grant = "auto";
    for (int i = first; i < command.arguments.size(); ++i) {
        const auto argument = command.arguments[i];
        if (argument == "--oidc-use-access-token") { login.useAccessToken = true; continue; }
        if (argument == "--force-refresh") continue;
        const auto equals = argument.indexOf('=');
        const auto key = equals < 0 ? argument : argument.left(equals);
        if (key != "--oidc-issuer-url" && key != "--oidc-client-id" && key != "--oidc-client-secret"
            && key != "--oidc-extra-scope" && key != "--grant-type" && key != "--token-cache-storage" && key != "--token-cache-dir")
            return Failure{StoreError::UnsupportedVersion, "This OIDC credential option is not supported by native sign-in."};
        const auto value = equals >= 0 ? argument.mid(equals + 1) : (++i < command.arguments.size() ? command.arguments[i] : QString{});
        if (value.isEmpty() || value.startsWith("--") || value.contains(QChar::Null))
            return Failure{StoreError::InvalidData, "An OIDC credential option has no valid value."};
        if (key == "--oidc-issuer-url") login.issuer = QUrl(value, QUrl::StrictMode);
        else if (key == "--oidc-client-id") login.clientId = value;
        else if (key == "--oidc-client-secret") login.clientSecret = value;
        else if (key == "--oidc-extra-scope") login.scopes.append(value.split(',', Qt::SkipEmptyParts));
        else if (key == "--grant-type") grant = value;
    }
    if (!safeUrl(login.issuer) || !login.issuer.userInfo().isEmpty() || login.issuer.hasQuery() || login.clientId.isEmpty())
        return Failure{StoreError::InvalidData, "Native OIDC requires a valid HTTPS issuer and client ID."};
    if (grant != "auto" && grant != "device-code")
        return Failure{StoreError::UnsupportedVersion, "This OIDC grant cannot use native device sign-in."};
    login.deviceCodeGrant = grant == "device-code";
    for (const auto& scope : login.scopes) if (scope.isEmpty() || std::any_of(scope.cbegin(), scope.cend(), [](QChar c) { return c.isSpace() || c.category() == QChar::Other_Control; }))
        return Failure{StoreError::InvalidData, "Invalid OIDC scope."};
    return std::optional<OidcLogin>{std::move(login)};
}

Result<std::optional<OidcLogin>> nativeAks(const ExecCommand& command) {
    if (QFileInfo(command.program).fileName().compare("kubelogin", Qt::CaseInsensitive) != 0
        || command.arguments.value(0) != "get-token") return std::optional<OidcLogin>{};
    QMap<QString, QString> options;
    for (int i = 1; i < command.arguments.size(); ++i) {
        const auto argument = command.arguments[i];
        const auto equals = argument.indexOf('=');
        auto key = equals < 0 ? argument : argument.left(equals);
        if (key == "-l") key = "--login";
        if (key != "--login" && key != "--tenant-id" && key != "--server-id" && key != "--client-id"
            && key != "--environment" && key != "--token-cache-dir")
            return Failure{StoreError::UnsupportedVersion, "This AKS credential option is not supported by native sign-in."};
        const auto value = equals >= 0 ? argument.mid(equals + 1) : (++i < command.arguments.size() ? command.arguments[i] : QString{});
        if (value.isEmpty() || value.startsWith("--") || value.contains(QChar::Null) || options.contains(key))
            return Failure{StoreError::InvalidData, "An AKS credential option is missing, duplicated or invalid."};
        options.insert(key, value);
    }
    static const QRegularExpression uuid("^[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}$");
    const auto tenant = options.value("--tenant-id");
    const auto server = options.value("--server-id");
    const auto client = options.value("--client-id");
    if (!uuid.match(tenant).hasMatch() || !uuid.match(server).hasMatch() || !uuid.match(client).hasMatch())
        return Failure{StoreError::InvalidData, "Native AKS sign-in requires tenant, server and client UUIDs from the imported kubeconfig."};
    if (options.value("--environment", "AzurePublicCloud") != "AzurePublicCloud")
        return Failure{StoreError::UnsupportedVersion, "This Azure cloud environment is not supported by native sign-in."};
    const auto mode = options.value("--login", "devicecode");
    if (mode != "devicecode" && mode != "interactive")
        return Failure{StoreError::UnsupportedVersion, "This AKS login mode needs an external identity. Import a device-code or interactive personal-login configuration."};
    OidcLogin login;
    login.issuer = QUrl("https://login.microsoftonline.com/" + tenant + "/v2.0");
    login.deviceEndpoint = QUrl("https://login.microsoftonline.com/" + tenant + "/oauth2/v2.0/devicecode");
    login.clientId = client;
    login.scopes = {server + "/.default"};
    login.useAccessToken = true;
    login.deviceCodeGrant = mode == "devicecode";
    return std::optional<OidcLogin>{std::move(login)};
}

NativeOidcLogin::NativeOidcLogin(ClusterConnection connection, Flow flow, QObject* parent)
    : QObject(parent), connection_(std::move(connection)), flow_(flow) {
    timer_.setSingleShot(true);
    connect(&timer_, &QTimer::timeout, this, [this] {
        if (flow_ == Flow::Browser) finish(Failure{StoreError::ReadFailed, "Browser sign-in expired. Confirm another attempt to retry."});
        else poll();
    });
}
QString NativeOidcLogin::instruction() const { return instruction_; }
void NativeOidcLogin::finish(Result<ClusterConnection> result) {
    if (done_) return;
    done_ = true; timer_.stop();
    state_.clear(); nonce_.clear(); codeVerifier_.clear();
    if (pending_) { auto* reply = pending_.data(); pending_ = nullptr; reply->abort(); reply->deleteLater(); }
    emit completed(std::move(result));
}
void NativeOidcLogin::cancel() { finish(Failure{StoreError::ReadFailed, "Authentication cancelled. Confirm another attempt to retry."}); }
void NativeOidcLogin::send(const QUrl& url, const QByteArray& body, std::function<void(int, const QJsonObject&)> done) {
    if (done_ || !safeUrl(url)) { finish(invalidLogin()); return; }
    QNetworkRequest request(url);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    request.setTransferTimeout(20000);
    request.setRawHeader("Accept", "application/json");
    if (!body.isEmpty()) request.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");
    auto* reply = body.isEmpty() ? network_.get(request) : network_.post(request, body);
    pending_ = reply;
    connect(reply, &QNetworkReply::downloadProgress, this, [reply](qint64 received, qint64) {
        if (received > 65536) { reply->setProperty("oversized", true); reply->abort(); }
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, done = std::move(done)] {
        if (done_ || pending_ != reply) { reply->deleteLater(); return; }
        pending_ = nullptr;
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const auto bytes = reply->read(65537);
        const bool failed = reply->property("oversized").toBool() || bytes.size() > 65536 || reply->bytesAvailable() > 0
            || status < 200 || status >= 500 || (reply->error() != QNetworkReply::NoError && status < 400);
        reply->deleteLater();
        if (failed) { finish(Failure{StoreError::ReadFailed, "OIDC sign-in could not contact the identity provider. Confirm another attempt to retry."}); return; }
        QJsonParseError error;
        const auto document = QJsonDocument::fromJson(bytes, &error);
        if (error.error != QJsonParseError::NoError || !document.isObject()) { finish(invalidLogin()); return; }
        done(status, document.object());
    });
}
void NativeOidcLogin::start() {
    if (!connection_.exec || !connection_.exec->oidc || done_) { finish(invalidLogin()); return; }
    if (flow_ == Flow::Browser) {
        const auto& redirect = connection_.exec->oidc->redirectUri;
        if (!redirect.isValid() || !redirect.scheme().contains('.') || !redirect.authority().isEmpty()
            || redirect.path() != "/oauth2redirect" || redirect.hasQuery() || redirect.hasFragment()) {
            finish(invalidLogin()); return;
        }
    }
    auto discovery = connection_.exec->oidc->issuer;
    QString path = discovery.path();
    if (path.endsWith('/')) path.chop(1);
    discovery.setPath(path + "/.well-known/openid-configuration");
    send(discovery, {}, [this](int status, const QJsonObject& object) {
        const auto& oidc = *connection_.exec->oidc;
        tokenEndpoint_ = QUrl(object.value("token_endpoint").toString(), QUrl::StrictMode);
        jwksEndpoint_ = QUrl(object.value("jwks_uri").toString(), QUrl::StrictMode);
        const QUrl device = oidc.deviceEndpoint.isEmpty() ? QUrl(object.value("device_authorization_endpoint").toString(), QUrl::StrictMode) : oidc.deviceEndpoint;
        const QUrl authorization(object.value("authorization_endpoint").toString(), QUrl::StrictMode);
        if (status != 200 || object.value("issuer").toString() != oidc.issuer.toString() || !safeUrl(tokenEndpoint_)) { finish(invalidLogin()); return; }
        if ((flow_ == Flow::Browser || !oidc.useAccessToken) && !safeUrl(jwksEndpoint_)) { finish(invalidLogin()); return; }
        if (flow_ == Flow::Browser) {
            if (!safeUrl(authorization)) { finish(invalidLogin()); return; }
            requestBrowser(authorization);
            return;
        }
        if (!safeUrl(device)) {
            finish(Failure{StoreError::UnsupportedVersion, "This identity provider has no device authorization endpoint. Use a supported mobile sign-in method."});
            return;
        }
        requestDevice(device);
    });
}
void NativeOidcLogin::requestDevice(const QUrl& endpoint) {
    const auto& oidc = *connection_.exec->oidc;
    QStringList scopes = oidc.scopes; scopes.prepend("openid"); scopes.removeDuplicates();
    QList<QPair<QString, QString>> fields{{"client_id", oidc.clientId}, {"scope", scopes.join(' ')}};
    if (!oidc.clientSecret.isEmpty()) fields.append({"client_secret", oidc.clientSecret});
    send(endpoint, form(fields), [this](int status, const QJsonObject& object) {
        const auto code = object.value("device_code").toString();
        const auto userCode = object.value("user_code").toString();
        const QUrl verification(object.value("verification_uri").toString(), QUrl::StrictMode);
        const QUrl complete(object.value("verification_uri_complete").toString(), QUrl::StrictMode);
        const int lifetime = object.value("expires_in").toInt();
        const int interval = object.value("interval").toInt(5);
        if (status != 200 || code.isEmpty() || userCode.isEmpty() || !safeUrl(verification)
            || (!complete.isEmpty() && !safeUrl(complete)) || lifetime <= 0 || lifetime > 3600 || interval < 0 || interval > 600) {
            finish(invalidLogin()); return;
        }
        deviceCode_ = code; interval_ = std::max(5, interval);
        deadline_ = QDateTime::currentDateTimeUtc().addSecs(lifetime);
        instruction_ = "Open " + verification.toString() + " in your browser and enter code " + userCode + ".";
        emit instructionChanged();
        if (done_) return;
        QDesktopServices::openUrl(complete.isEmpty() ? verification : complete);
        if (!done_) timer_.start(interval_ * 1000);
    });
}
void NativeOidcLogin::requestBrowser(const QUrl& endpoint) {
    const auto& oidc = *connection_.exec->oidc;
    state_ = randomValue(); nonce_ = randomValue(); codeVerifier_ = randomValue();
    QStringList scopes = oidc.scopes; scopes.prepend("openid"); scopes.removeDuplicates();
    QUrl authorization = endpoint;
    QUrlQuery query(authorization);
    query.addQueryItem("response_type", "code");
    query.addQueryItem("client_id", oidc.clientId);
    query.addQueryItem("redirect_uri", oidc.redirectUri.toString());
    query.addQueryItem("scope", scopes.join(' '));
    query.addQueryItem("state", state_);
    query.addQueryItem("nonce", nonce_);
    query.addQueryItem("code_challenge", QString::fromLatin1(QCryptographicHash::hash(codeVerifier_.toLatin1(), QCryptographicHash::Sha256)
        .toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals)));
    query.addQueryItem("code_challenge_method", "S256");
    authorization.setQuery(query);
    connect(redirectRouter(oidc.redirectUri.scheme()), &OidcRedirectRouter::received, this, &NativeOidcLogin::handleRedirect);
    instruction_ = "Complete sign-in in your browser.";
    emit instructionChanged();
    if (done_) return;
    timer_.start(300000);
    if (!QDesktopServices::openUrl(authorization))
        finish(Failure{StoreError::ReadFailed, "The system browser could not open. Confirm another sign-in attempt."});
}
void NativeOidcLogin::handleRedirect(const QUrl& url) {
    if (done_ || flow_ != Flow::Browser || state_.isEmpty() || url.hasFragment()
        || url.adjusted(QUrl::RemoveQuery) != connection_.exec->oidc->redirectUri) return;
    const QUrlQuery query(url);
    const auto states = query.allQueryItemValues("state", QUrl::FullyDecoded);
    if (states.size() != 1 || states.front() != state_) return;
    state_.clear(); timer_.stop();
    const auto codes = query.allQueryItemValues("code", QUrl::FullyDecoded);
    const auto errors = query.allQueryItemValues("error", QUrl::FullyDecoded);
    if (errors.size() == 1 && codes.isEmpty()) {
        finish(Failure{StoreError::ReadFailed, "OIDC sign-in was denied. Confirm another attempt to retry."}); return;
    }
    if (codes.size() != 1 || codes.front().isEmpty() || codes.front().size() > 4096 || !errors.isEmpty()
        || std::any_of(codes.front().cbegin(), codes.front().cend(), [](QChar c) { return c.isSpace() || c.category() == QChar::Other_Control; })) {
        finish(invalidLogin()); return;
    }
    const auto& oidc = *connection_.exec->oidc;
    QList<QPair<QString, QString>> fields{{"grant_type", "authorization_code"}, {"code", codes.front()},
        {"redirect_uri", oidc.redirectUri.toString()}, {"client_id", oidc.clientId}, {"code_verifier", codeVerifier_}};
    if (!oidc.clientSecret.isEmpty()) fields.append({"client_secret", oidc.clientSecret});
    codeVerifier_.clear();
    send(tokenEndpoint_, form(fields), [this](int status, const QJsonObject& object) { acceptToken(status, object); });
}
void NativeOidcLogin::poll() {
    if (QDateTime::currentDateTimeUtc() >= deadline_) {
        finish(Failure{StoreError::ReadFailed, "The sign-in code expired. Confirm another attempt to retry."}); return;
    }
    const auto& oidc = *connection_.exec->oidc;
    QList<QPair<QString, QString>> fields{{"grant_type", "urn:ietf:params:oauth:grant-type:device_code"},
        {"device_code", deviceCode_}, {"client_id", oidc.clientId}};
    if (!oidc.clientSecret.isEmpty()) fields.append({"client_secret", oidc.clientSecret});
    send(tokenEndpoint_, form(fields), [this](int status, const QJsonObject& object) {
        if (status == 400) {
            const auto error = object.value("error").toString();
            if (error == "authorization_pending" || error == "slow_down") {
                if (error == "slow_down") interval_ = std::min(600, interval_ + 5);
                timer_.start(interval_ * 1000); return;
            }
            finish(Failure{StoreError::ReadFailed, "OIDC sign-in was denied or expired. Confirm another attempt to retry."}); return;
        }
        acceptToken(status, object);
    });
}
void NativeOidcLogin::acceptToken(int status, const QJsonObject& object) {
    if (status != 200) { finish(invalidLogin()); return; }
    const auto& oidc = *connection_.exec->oidc;
    const auto token = object.value(oidc.useAccessToken ? "access_token" : "id_token").toString();
    if (token.isEmpty() || std::any_of(token.cbegin(), token.cend(), [](QChar c) { return c.isSpace() || c.category() == QChar::Other_Control; })) {
        finish(invalidLogin()); return;
    }
    if (flow_ == Flow::Browser || !oidc.useAccessToken) {
        const auto idToken = object.value("id_token").toString();
        const int seconds = object.value("expires_in").toInt();
        if (idToken.isEmpty() || (flow_ == Flow::Browser && oidc.useAccessToken && (seconds <= 30 || seconds > 86400))) {
            finish(invalidLogin()); return;
        }
        send(jwksEndpoint_, {}, [this, token, idToken, seconds](int keyStatus, const QJsonObject& keys) {
            const auto expiry = keyStatus == 200 ? verifiedIdExpiry(idToken, *connection_.exec->oidc, keys, nonce_) : std::optional<QDateTime>{};
            if (!expiry) { finish(invalidLogin()); return; }
            auto validUntil = *expiry;
            if (flow_ == Flow::Browser && connection_.exec->oidc->useAccessToken)
                validUntil = std::min(validUntil, QDateTime::currentDateTimeUtc().addSecs(seconds));
            if (validUntil <= QDateTime::currentDateTimeUtc().addSecs(30)) { finish(invalidLogin()); return; }
            connection_.authorization = "Bearer " + token.toUtf8();
            connection_.expiresAt = validUntil;
            connection_.credentialReady = true;
            finish(connection_);
        });
        return;
    }
    const int seconds = object.value("expires_in").toInt();
    if (seconds <= 30 || seconds > 86400) { finish(invalidLogin()); return; }
    connection_.authorization = "Bearer " + token.toUtf8();
    connection_.expiresAt = QDateTime::currentDateTimeUtc().addSecs(seconds);
    connection_.credentialReady = true;
    finish(connection_);
}
} // namespace podlord

#include "native_oidc.moc"
