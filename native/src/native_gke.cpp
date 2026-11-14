#include "native_gke.h"
#include "native_oidc.h"
#include <QDateTime>
#include <QFileInfo>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QSslKey>
#include <QUrlQuery>
#include <algorithm>
#if defined(Q_OS_DARWIN)
#include <Security/Security.h>
#else
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <memory>
#endif
#ifdef Q_OS_ANDROID
#include <QJniEnvironment>
#include <QJniObject>
#include <QMutex>
#include <QMutexLocker>
#include <QCoreApplication>
#include <atomic>
#include <mutex>
#endif

namespace podlord {
namespace {
constexpr auto cloudScope = "https://www.googleapis.com/auth/cloud-platform";
constexpr auto emailScope = "https://www.googleapis.com/auth/userinfo.email";
Failure invalidToken() { return {StoreError::InvalidData, "Google authorization returned an invalid access token; no Kubernetes request was sent."}; }
QByteArray base64Url(const QByteArray& value) { return value.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals); }

Result<QByteArray> sign(const QByteArray& keyBytes, const QByteArray& message) {
    const QSslKey parsed(keyBytes, QSsl::Rsa);
    if (parsed.isNull() || parsed.length() < 2048 || parsed.length() > 8192)
        return Failure{StoreError::InvalidData, "Google service-account RSA key is invalid or too weak."};
#if defined(Q_OS_DARWIN)
    const auto der = parsed.toDer();
    CFDataRef keyData = CFDataCreate(kCFAllocatorDefault, reinterpret_cast<const UInt8*>(der.constData()), der.size());
    if (!keyData) return Failure{StoreError::InvalidData, "Google service-account RSA key could not be loaded."};
    const void* names[] = {kSecAttrKeyType, kSecAttrKeyClass};
    const void* values[] = {kSecAttrKeyTypeRSA, kSecAttrKeyClassPrivate};
    CFDictionaryRef attributes = CFDictionaryCreate(kCFAllocatorDefault, names, values, 2,
        &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    SecKeyRef key = attributes ? SecKeyCreateWithData(keyData, attributes, nullptr) : nullptr;
    if (attributes) CFRelease(attributes);
    CFRelease(keyData);
    if (!key) return Failure{StoreError::InvalidData, "Google service-account RSA key could not be loaded."};
    CFDataRef input = CFDataCreate(kCFAllocatorDefault, reinterpret_cast<const UInt8*>(message.constData()), message.size());
    CFDataRef signedData = input && SecKeyIsAlgorithmSupported(key, kSecKeyOperationTypeSign,
        kSecKeyAlgorithmRSASignatureMessagePKCS1v15SHA256)
        ? SecKeyCreateSignature(key, kSecKeyAlgorithmRSASignatureMessagePKCS1v15SHA256, input, nullptr) : nullptr;
    if (input) CFRelease(input);
    CFRelease(key);
    if (!signedData) return Failure{StoreError::InvalidData, "Google service-account assertion could not be signed."};
    const QByteArray signature(reinterpret_cast<const char*>(CFDataGetBytePtr(signedData)), CFDataGetLength(signedData));
    CFRelease(signedData);
    return signature;
#else
    std::unique_ptr<BIO, decltype(&BIO_free)> input(BIO_new_mem_buf(keyBytes.constData(), keyBytes.size()), BIO_free);
    std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)> key(input ? PEM_read_bio_PrivateKey(input.get(), nullptr, nullptr, nullptr) : nullptr, EVP_PKEY_free);
    std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> context(EVP_MD_CTX_new(), EVP_MD_CTX_free);
    if (!key || EVP_PKEY_base_id(key.get()) != EVP_PKEY_RSA || EVP_PKEY_bits(key.get()) < 2048 || !context
        || EVP_DigestSignInit(context.get(), nullptr, EVP_sha256(), nullptr, key.get()) != 1
        || EVP_DigestSignUpdate(context.get(), message.constData(), size_t(message.size())) != 1)
        return Failure{StoreError::InvalidData, "Google service-account RSA key is invalid or unsupported."};
    size_t size = 0;
    if (EVP_DigestSignFinal(context.get(), nullptr, &size) != 1 || size > 1024)
        return Failure{StoreError::InvalidData, "Google service-account assertion could not be signed."};
    QByteArray signature(qsizetype(size), '\0');
    if (EVP_DigestSignFinal(context.get(), reinterpret_cast<unsigned char*>(signature.data()), &size) != 1)
        return Failure{StoreError::InvalidData, "Google service-account assertion could not be signed."};
    signature.resize(qsizetype(size));
    return signature;
#endif
}
} // namespace

Result<GkeServiceAccount> parseGkeServiceAccount(const QByteArray& json) {
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(json, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject())
        return Failure{StoreError::InvalidData, "Google service-account key must be a JSON object."};
    const auto object = document.object();
    for (const auto& name : {"type", "client_email", "private_key_id", "private_key", "token_uri"})
        if (!object.value(name).isString()) return Failure{StoreError::InvalidData, "Google service-account key is missing required string fields."};
    const auto email = object.value("client_email").toString();
    const auto keyId = object.value("private_key_id").toString();
    const auto key = object.value("private_key").toString().toUtf8();
    static const QRegularExpression emailPattern("\\A[A-Za-z0-9][A-Za-z0-9._-]{0,127}@[A-Za-z0-9.-]{1,190}\\z");
    static const QRegularExpression keyPattern("\\A[0-9a-fA-F]{8,128}\\z");
    if (object.value("type") != "service_account" || object.value("token_uri") != "https://oauth2.googleapis.com/token"
        || !emailPattern.match(email).hasMatch() || !keyPattern.match(keyId).hasMatch() || key.size() > 32768
        || QSslKey(key, QSsl::Rsa).length() < 2048)
        return Failure{StoreError::InvalidData, "Google service-account key is invalid, unsupported or weaker than 2048-bit RSA."};
    return GkeServiceAccount{email, keyId, key};
}

Result<std::optional<GkeLogin>> nativeGkeConfig(const ExecCommand& command) {
    const auto executable = QFileInfo(command.program).fileName();
    if (executable.compare("gke-gcloud-auth-plugin", Qt::CaseInsensitive) != 0
        && executable.compare("gke-gcloud-auth-plugin.exe", Qt::CaseInsensitive) != 0)
        return std::optional<GkeLogin>{};
    if (!command.environment.isEmpty())
        return Failure{StoreError::UnsupportedVersion, "GKE exec environment overrides are unavailable in native mobile sign-in."};
    if (command.arguments.isEmpty()) return std::optional<GkeLogin>{GkeLogin{}};
    if (command.arguments == QStringList{"--use_application_default_credentials"})
        return std::optional<GkeLogin>{GkeLogin{{}, true}};
    return Failure{StoreError::UnsupportedVersion, "This GKE credential option is unavailable in native mobile sign-in."};
}

Result<OidcLogin> nativeGkeBrowserConfig(const QString& clientId) {
    if (clientId.isEmpty())
        return Failure{StoreError::UnsupportedVersion, "This Podlord build has no registered Google iOS OAuth client. Import a Google service-account key for this GKE context."};
    static const QRegularExpression pattern("\\A[0-9]+(?:-[a-z0-9]+)?\\.apps\\.googleusercontent\\.com\\z");
    if (clientId.size() > 255 || !pattern.match(clientId).hasMatch())
        return Failure{StoreError::InvalidData, "The Podlord Google iOS OAuth client ID is invalid. No browser or Kubernetes request was started."};
    auto fields = clientId.split('.');
    std::reverse(fields.begin(), fields.end());
    OidcLogin login;
    login.issuer = QUrl("https://accounts.google.com");
    login.clientId = clientId;
    login.scopes = {cloudScope, emailScope};
    login.useAccessToken = true;
    login.redirectUri = QUrl(fields.join('.') + ":/oauth2redirect");
    return login;
}

#ifdef Q_OS_ANDROID
struct NativeGkeLogin::Android final {
    struct Registry final { QMutex mutex; QHash<jlong, NativeGkeLogin*> owners; std::atomic<jlong> next{1}; };
    static Registry& registry() { static Registry value; return value; }
    static void JNICALL nativeComplete(JNIEnv* environment, jclass, jlong request, jint status, jstring token) {
        const auto bytes = token ? QJniObject::fromLocalRef(environment->NewLocalRef(token)).toString().toUtf8() : QByteArray{};
        NativeGkeLogin* owner = nullptr;
        {
            QMutexLocker lock(&registry().mutex);
            owner = registry().owners.take(request);
            if (owner) QMetaObject::invokeMethod(owner, [owner, status, bytes] {
                if (owner->android_) owner->android_->request = 0;
                owner->acceptAuthorization({AuthorizationStatus(status), bytes});
            }, Qt::QueuedConnection);
        }
    }
    static bool registerNatives() {
        static std::once_flag once;
        static bool registered = false;
        std::call_once(once, [] {
            const JNINativeMethod methods[] = {{const_cast<char*>("nativeComplete"), const_cast<char*>("(JILjava/lang/String;)V"), reinterpret_cast<void*>(nativeComplete)}};
            QJniEnvironment environment;
            registered = environment.registerNativeMethods("berlin/yuna/podlord/GkeAuthorization", methods, 1);
        });
        return registered;
    }
    explicit Android(NativeGkeLogin* value) : owner(value) {}
    bool start() {
        if (!registerNatives() || !QNativeInterface::QAndroidApplication::isActivityContext()) return false;
        request = registry().next.fetch_add(1);
        { QMutexLocker lock(&registry().mutex); registry().owners.insert(request, owner); }
        const auto id = request;
        QNativeInterface::QAndroidApplication::runOnAndroidMainThread([id] {
            const auto activity = QNativeInterface::QAndroidApplication::context();
            const bool started = activity.isValid() && activity.callMethod<jboolean>("startGkeAuthorization", id);
            if (!started) nativeComplete(nullptr, nullptr, id, jint(AuthorizationStatus::Failure), nullptr);
            return QVariant{};
        });
        return true;
    }
    void cancel() {
        if (!request) return;
        const auto id = request;
        request = 0;
        { QMutexLocker lock(&registry().mutex); registry().owners.remove(id); }
        if (!QNativeInterface::QAndroidApplication::isActivityContext()) return;
        QNativeInterface::QAndroidApplication::runOnAndroidMainThread([id] {
            const auto activity = QNativeInterface::QAndroidApplication::context();
            if (activity.isValid()) activity.callMethod<jboolean>("cancelGkeAuthorization", id);
            return QVariant{};
        });
    }
    NativeGkeLogin* owner;
    jlong request = 0;
};
#else
struct NativeGkeLogin::Android final { explicit Android(NativeGkeLogin*) {} bool start() { return false; } void cancel() {} };
#endif

NativeGkeLogin::NativeGkeLogin(ClusterConnection connection, QObject* parent)
    : QObject(parent), connection_(std::move(connection)), serviceAccountEndpoint_("https://oauth2.googleapis.com/token"), android_(std::make_unique<Android>(this)) {}
NativeGkeLogin::NativeGkeLogin(ClusterConnection connection, Authorize authorize, std::function<void()> cancel,
    QUrl endpoint, QObject* parent)
    : QObject(parent), connection_(std::move(connection)), authorize_(std::move(authorize)), cancelAuthorization_(std::move(cancel)),
      serviceAccountEndpoint_(std::move(endpoint)), android_(std::make_unique<Android>(this)) {}
NativeGkeLogin::~NativeGkeLogin() {
    android_->cancel();
    if (browser_) {
        disconnect(browser_, nullptr, this, nullptr);
        browser_->cancel();
    }
}

void NativeGkeLogin::start() {
    if (started_) return;
    started_ = true;
    if (!connection_.exec || !connection_.exec->gke) { finish(Failure{StoreError::InvalidInput, "No valid GKE credential was confirmed."}); return; }
    const auto& gke = *connection_.exec->gke;
    if (gke.serviceAccountOnly) {
        if (!gke.serviceAccount) { finish(Failure{StoreError::UnsupportedVersion, "Import a Google service-account key for this GKE context."}); return; }
        requestServiceAccount(); return;
    }
    if (authorize_) {
        authorize_([this](AuthorizationResult result) { acceptAuthorization(std::move(result)); });
        return;
    }
#ifdef Q_OS_IOS
    const auto configuration = nativeGkeBrowserConfig(QString::fromLatin1(PODLORD_GKE_IOS_CLIENT_ID));
    if (const auto* failure = std::get_if<Failure>(&configuration)) {
        if (gke.serviceAccount) requestServiceAccount();
        else finish(*failure);
        return;
    }
    auto browserConnection = connection_;
    browserConnection.exec->oidc = std::get<OidcLogin>(configuration);
    auto* login = new NativeOidcLogin(std::move(browserConnection), NativeOidcLogin::Flow::Browser, this);
    browser_ = login;
    connect(login, &NativeOidcLogin::completed, this, [this, login](Result<ClusterConnection> result) {
        browser_.clear();
        login->deleteLater();
        if (done_) return;
        if (const auto* failure = std::get_if<Failure>(&result)) { finish(*failure); return; }
        const auto& ready = std::get<ClusterConnection>(result);
        acceptAuthorization({AuthorizationStatus::Success, ready.authorization.mid(7), ready.expiresAt});
    });
    login->start();
#else
    if (!android_->start()) acceptAuthorization({AuthorizationStatus::Unavailable, {}});
#endif
}

void NativeGkeLogin::cancel() {
    if (!started_ || done_) return;
    done_ = true;
    auto* reply = pending_.data();
    pending_.clear();
    if (cancelAuthorization_) cancelAuthorization_();
    android_->cancel();
    if (browser_) browser_->cancel();
    if (reply) { reply->abort(); reply->deleteLater(); }
    emit completed(Failure{StoreError::ReadFailed, "GKE sign-in was cancelled. Confirm another attempt to retry."});
}

void NativeGkeLogin::acceptAuthorization(AuthorizationResult result) {
    if (done_) return;
    if (result.status == AuthorizationStatus::Unavailable && connection_.exec->gke->serviceAccount) {
        requestServiceAccount(); return;
    }
    if (result.status == AuthorizationStatus::Cancelled) {
        finish(Failure{StoreError::ReadFailed, "GKE sign-in was cancelled. Confirm another attempt to retry."}); return;
    }
    if (result.status == AuthorizationStatus::Unavailable) {
        finish(Failure{StoreError::UnsupportedVersion, "Google Play Services authorization is unavailable. Import a Google service-account key for this GKE context."}); return;
    }
    if (result.status != AuthorizationStatus::Success) {
        finish(Failure{StoreError::ReadFailed, "Google authorization failed. Verify the Podlord Android OAuth registration, then confirm another attempt."}); return;
    }
    const auto& token = result.accessToken;
    if (token.isEmpty() || token.size() > 16384 || std::any_of(token.cbegin(), token.cend(), [](char value) {
        const auto byte = static_cast<unsigned char>(value); return byte <= 0x20 || byte == 0x7f;
    })) { finish(invalidToken()); return; }
    // Play Services does not publish an expiry for this token; never cache it indefinitely.
    const auto now = QDateTime::currentDateTimeUtc();
    connection_.expiresAt = result.expiresAt.isValid() ? result.expiresAt : now.addSecs(1800);
    if (connection_.expiresAt <= now) { finish(invalidToken()); return; }
    connection_.authorization = "Bearer " + token;
    connection_.credentialReady = true;
    if (connection_.exec->gke->serviceAccount) connection_.exec->gke->serviceAccount->privateKey.clear();
    finish(connection_);
}

void NativeGkeLogin::requestServiceAccount() {
    const auto& account = *connection_.exec->gke->serviceAccount;
    const auto now = QDateTime::currentDateTimeUtc().toSecsSinceEpoch();
    const auto header = base64Url(QJsonDocument(QJsonObject{{"alg", "RS256"}, {"typ", "JWT"}, {"kid", account.privateKeyId}}).toJson(QJsonDocument::Compact));
    const auto claims = base64Url(QJsonDocument(QJsonObject{{"iss", account.clientEmail}, {"scope", QString(cloudScope) + ' ' + emailScope},
        {"aud", "https://oauth2.googleapis.com/token"}, {"iat", now}, {"exp", now + 3600}}).toJson(QJsonDocument::Compact));
    const auto unsignedJwt = header + '.' + claims;
    const auto signature = sign(account.privateKey, unsignedJwt);
    if (const auto* failure = std::get_if<Failure>(&signature)) { finish(*failure); return; }
    QUrlQuery fields;
    fields.addQueryItem("grant_type", "urn:ietf:params:oauth:grant-type:jwt-bearer");
    fields.addQueryItem("assertion", QString::fromLatin1(unsignedJwt + '.' + base64Url(std::get<QByteArray>(signature))));
    QNetworkRequest request(serviceAccountEndpoint_);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");
    pending_ = network_.post(request, fields.toString(QUrl::FullyEncoded).toUtf8());
    pending_->setReadBufferSize(65536);
    connect(pending_, &QNetworkReply::finished, this, [this] {
        if (done_ || !pending_) return;
        const auto reply = pending_;
        pending_.clear();
        const auto status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const auto bytes = reply->read(65537);
        reply->deleteLater();
        QJsonParseError error;
        const auto document = QJsonDocument::fromJson(bytes, &error);
        if (status != 200 || bytes.size() > 65536 || error.error != QJsonParseError::NoError || !document.isObject()) {
            finish(Failure{StoreError::ReadFailed, "Google service-account token exchange failed. Confirm another attempt after checking the imported key."}); return;
        }
        const auto object = document.object();
        const auto token = object.value("access_token").toString().toUtf8();
        const auto seconds = object.value("expires_in").toInt();
        if (object.value("token_type") != "Bearer" || seconds <= 30 || seconds > 86400) { finish(invalidToken()); return; }
        acceptAuthorization({AuthorizationStatus::Success, token, QDateTime::currentDateTimeUtc().addSecs(seconds)});
    });
}

void NativeGkeLogin::finish(Result<ClusterConnection> result) {
    if (done_) return;
    done_ = true;
    emit completed(std::move(result));
}
} // namespace podlord
