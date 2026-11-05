#include "native_aws_login.h"
#include "native_eks.h"
#include <QDesktopServices>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrlQuery>
#include <QUuid>
#include <algorithm>

namespace podlord {
namespace {
Failure invalidResponse() { return {StoreError::InvalidData, "AWS returned an invalid authentication response. Confirm another attempt to retry."}; }
bool tokenText(const QString& text, qsizetype limit) {
    return !text.isEmpty() && text.size() <= limit && std::all_of(text.cbegin(), text.cend(), [](QChar c) { return c.unicode() >= 33 && c.unicode() <= 126; });
}
bool browserUrl(const QUrl& url) {
    return url.isValid() && url.scheme() == "https" && !url.host().isEmpty() && url.userInfo().isEmpty() && !url.hasFragment();
}
} // namespace

NativeAwsLogin::NativeAwsLogin(ClusterConnection connection, QObject* parent)
    : QObject(parent), connection_(std::move(connection)) {
    timer_.setSingleShot(true);
    timer_.setTimerType(Qt::PreciseTimer);
    connect(&timer_, &QTimer::timeout, this, &NativeAwsLogin::poll);
}
QString NativeAwsLogin::instruction() const { return instruction_; }
std::optional<AwsMfaChallenge> NativeAwsLogin::challenge() const {
    if (mfaChallenge_.isEmpty()) return {};
    const auto& role = connection_.exec->eks->roles.first();
    return AwsMfaChallenge{mfaChallenge_, role.arn, role.mfaSerial};
}
bool NativeAwsLogin::submitMfa(const QString& id, const QString& code) {
    if (done_ || id.isEmpty() || id != mfaChallenge_ || code.size() != 6
        || !std::all_of(code.cbegin(), code.cend(), [](QChar c) { return c >= '0' && c <= '9'; })) return false;
    return requestRole(code);
}
NativeAwsLogin::~NativeAwsLogin() {
    done_ = true;
    timer_.stop();
    if (pending_) { disconnect(pending_, nullptr, this, nullptr); pending_->abort(); }
}
void NativeAwsLogin::finish(Result<ClusterConnection> result) {
    if (done_) return;
    done_ = true;
    timer_.stop();
    clientId_.clear(); clientSecret_.clear(); deviceCode_.clear(); accessToken_.clear(); instruction_.clear(); mfaChallenge_.clear();
    connection_.exec.reset();
    if (pending_) {
        auto* reply = pending_.data(); pending_ = nullptr;
        reply->abort(); reply->deleteLater();
    }
    emit completed(std::move(result));
}
void NativeAwsLogin::cancel() { finish(Failure{StoreError::ReadFailed, "AWS sign-in cancelled. Confirm another attempt to retry."}); }
void NativeAwsLogin::sendJson(const QUrl& url, const QJsonObject& body, bool get, std::function<void(int, const QJsonObject&)> done) {
    QNetworkRequest request(url);
    request.setRawHeader("Accept", "application/json");
    if (get) request.setRawHeader("x-amz-sso_bearer_token", accessToken_.toLatin1());
    else request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    send(request, QJsonDocument(body).toJson(QJsonDocument::Compact), get, [this, done = std::move(done)](int status, const QByteArray& bytes) {
        QJsonParseError error;
        const auto json = QJsonDocument::fromJson(bytes, &error);
        if (error.error != QJsonParseError::NoError || !json.isObject()) { finish(invalidResponse()); return; }
        done(status, json.object());
    });
}
void NativeAwsLogin::send(QNetworkRequest request, const QByteArray& body, bool get, std::function<void(int, const QByteArray&)> done) {
    if (done_) return;
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    request.setTransferTimeout(20000);
    auto* reply = get ? network_.get(request) : network_.post(request, body);
    pending_ = reply;
    reply->setReadBufferSize(65537);
    connect(reply, &QNetworkReply::readyRead, this, [this, reply] {
        if (reply->bytesAvailable() > 65536) finish(invalidResponse());
    });
    connect(reply, &QNetworkReply::downloadProgress, this, [this](qint64 received, qint64) {
        if (received > 65536) finish(invalidResponse());
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, done = std::move(done)] {
        if (done_ || pending_ != reply) { reply->deleteLater(); return; }
        pending_ = nullptr;
        const auto status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const auto bytes = reply->read(65537);
        const bool failed = bytes.size() > 65536 || reply->bytesAvailable() > 0 || status < 200 || status >= 500
            || (status < 400 && reply->error() != QNetworkReply::NoError);
        reply->deleteLater();
        if (failed) { finish(Failure{StoreError::ReadFailed, "AWS could not complete the authentication request. Confirm another attempt to retry."}); return; }
        done(status, bytes);
    });
}
void NativeAwsLogin::start() {
    if (done_ || started_) return;
    started_ = true;
    if (!connection_.exec || !connection_.exec->eks) { finish(invalidResponse()); return; }
    // Revalidate the typed boundary, including callers other than the kubeconfig resolver.
    const auto& eks = *connection_.exec->eks;
    AwsProfile profile{{{"region", eks.region}}, eks.roles};
    if (eks.sso) {
        profile.fields.insert("sso_start_url", eks.sso->startUrl.toString()); profile.fields.insert("sso_region", eks.sso->region);
        profile.fields.insert("sso_account_id", eks.sso->accountId); profile.fields.insert("sso_role_name", eks.sso->roleName);
    }
    for (const auto& item : {qMakePair("aws_access_key_id", eks.accessKey), qMakePair("aws_secret_access_key", eks.secretKey), qMakePair("aws_session_token", eks.sessionToken)})
        if (!item.second.isEmpty()) profile.fields.insert(item.first, QString::fromLatin1(item.second));
    auto command = *connection_.exec;
    command.arguments = {"eks", "get-token", "--cluster-name", eks.cluster, "--region", eks.region};
    const auto validated = nativeEksConfig(command, profile);
    if (!std::holds_alternative<std::optional<EksLogin>>(validated) || !std::get<std::optional<EksLogin>>(validated)) { finish(invalidResponse()); return; }
    if (!eks.sso) { assumeRole(); return; }
    const auto& sso = *eks.sso;
    const auto suffix = sso.region.startsWith("cn-") ? ".amazonaws.com.cn" : ".amazonaws.com";
    oidc_ = QUrl("https://oidc." + sso.region + suffix);
    portal_ = QUrl("https://portal.sso." + sso.region + suffix);
    auto registration = oidc_; registration.setPath("/client/register");
    sendJson(registration, {{"clientName", "Podlord"}, {"clientType", "public"}, {"scopes", QJsonArray{"sso:account:access"}},
        {"grantTypes", QJsonArray{"urn:ietf:params:oauth:grant-type:device_code"}}}, false,
        [this](int status, const QJsonObject& response) {
            const auto id = response.value("clientId").toString(), secret = response.value("clientSecret").toString();
            const auto expiry = response.value("clientSecretExpiresAt").toInteger();
            if (status != 200 || !tokenText(id, 4096) || !tokenText(secret, 8192)
                || expiry <= QDateTime::currentSecsSinceEpoch() + 30) { finish(invalidResponse()); return; }
            clientId_ = id; clientSecret_ = secret;
            deadline_ = QDateTime::fromSecsSinceEpoch(expiry);
            auto device = oidc_; device.setPath("/device_authorization");
            sendJson(device, {{"clientId", id}, {"clientSecret", secret}, {"startUrl", connection_.exec->eks->sso->startUrl.toString()}}, false,
                [this](int status, const QJsonObject& response) {
                    const auto code = response.value("deviceCode").toString(), user = response.value("userCode").toString();
                    const QUrl verification(response.value("verificationUri").toString(), QUrl::StrictMode);
                    const QUrl complete(response.value("verificationUriComplete").toString(), QUrl::StrictMode);
                    const auto lifetime = response.value("expiresIn").toInt(), interval = response.value("interval").toInt(5);
                    if (status != 200 || !tokenText(code, 8192) || !tokenText(user, 256) || !browserUrl(verification)
                        || (!complete.isEmpty() && (!browserUrl(complete) || complete.host() != verification.host()))
                        || lifetime <= 0 || lifetime > 3600 || interval < 0 || interval > 600) { finish(invalidResponse()); return; }
                    deviceCode_ = code; interval_ = std::max(5, interval);
                    deadline_ = std::min(deadline_, QDateTime::currentDateTimeUtc().addSecs(lifetime));
                    instruction_ = "Open " + verification.toString() + " in your browser and enter code " + user + ".";
                    emit instructionChanged();
                    if (done_) return;
                    if (!QDesktopServices::openUrl(complete.isEmpty() ? verification : complete)) {
                        finish(Failure{StoreError::ReadFailed, "The system browser could not open AWS sign-in. Confirm another attempt."}); return;
                    }
                    if (!done_) timer_.start(int(std::clamp<qint64>(QDateTime::currentDateTimeUtc().msecsTo(deadline_), 1, interval_ * 1000)));
                });
        });
}
void NativeAwsLogin::poll() {
    if (QDateTime::currentDateTimeUtc() >= deadline_) {
        finish(Failure{StoreError::ReadFailed, "The AWS sign-in code expired. Confirm another attempt to retry."}); return;
    }
    auto token = oidc_; token.setPath("/token");
    sendJson(token, {{"clientId", clientId_}, {"clientSecret", clientSecret_}, {"deviceCode", deviceCode_},
        {"grantType", "urn:ietf:params:oauth:grant-type:device_code"}}, false,
        [this](int status, const QJsonObject& response) {
            if (status == 400) {
                auto error = response.value("error").toString();
                if (error.isEmpty()) error = response.value("__type").toString().section('#', -1);
                if (error == "authorization_pending" || error == "AuthorizationPendingException" || error == "slow_down" || error == "SlowDownException") {
                    if (error == "slow_down" || error == "SlowDownException") interval_ = std::min(600, interval_ + 5);
                    timer_.start(int(std::clamp<qint64>(QDateTime::currentDateTimeUtc().msecsTo(deadline_), 1, interval_ * 1000))); return;
                }
                finish(Failure{StoreError::ReadFailed, "AWS sign-in was denied or expired. Confirm another attempt to retry."}); return;
            }
            const auto access = response.value("accessToken").toString();
            const auto lifetime = response.value("expiresIn").toInt();
            if (status != 200 || !tokenText(access, 32768) || lifetime <= 30 || lifetime > 86400) { finish(invalidResponse()); return; }
            accessToken_ = access;
            clientId_.clear(); clientSecret_.clear(); deviceCode_.clear();
            auto credentials = portal_; credentials.setPath("/federation/credentials");
            const auto& sso = *connection_.exec->eks->sso;
            QUrlQuery query; query.addQueryItem("account_id", sso.accountId); query.addQueryItem("role_name", sso.roleName);
            credentials.setQuery(query);
            sendJson(credentials, {}, true, [this](int status, const QJsonObject& response) {
                if (status != 200 || !response.value("roleCredentials").isObject()) { finish(invalidResponse()); return; }
                const auto fields = response.value("roleCredentials").toObject();
                const auto access = fields.value("accessKeyId").toString(), secret = fields.value("secretAccessKey").toString(), session = fields.value("sessionToken").toString();
                const auto now = QDateTime::currentDateTimeUtc();
                const auto expiry = QDateTime::fromMSecsSinceEpoch(fields.value("expiration").toInteger());
                if (!tokenText(access, 128) || !tokenText(secret, 128) || !tokenText(session, 8192)
                    || !expiry.isValid() || expiry <= now.addSecs(30) || expiry > now.addDays(1)) { finish(invalidResponse()); return; }
                auto& eks = *connection_.exec->eks;
                eks.accessKey = access.toLatin1(); eks.secretKey = secret.toLatin1(); eks.sessionToken = session.toLatin1();
                eks.credentialExpiresAt = expiry;
                eks.sso.reset(); accessToken_.clear(); instruction_.clear();
                assumeRole();
            });
        });
}
void NativeAwsLogin::assumeRole() {
    if (done_) return;
    auto& eks = *connection_.exec->eks;
    if (eks.roles.isEmpty()) { finish(nativeEksToken(std::move(connection_))); return; }
    auto& role = eks.roles.first();
    if (role.sessionName.isEmpty()) role.sessionName = "podlord-" + QUuid::createUuid().toString(QUuid::WithoutBraces);
    if (!role.mfaSerial.isEmpty()) {
        mfaChallenge_ = QUuid::createUuid().toString(QUuid::WithoutBraces);
        instruction_ = "Enter the six-digit code for the configured AWS MFA device.";
        emit instructionChanged();
    } else requestRole({});
}
bool NativeAwsLogin::requestRole(const QString& code) {
    // Only the current challenge or the sequential role transition reaches here.
    // Both own a validated role and no outstanding request.
    const auto signedRequest = nativeAwsRoleRequest(*connection_.exec->eks, connection_.exec->eks->roles.first(), code);
    if (const auto* failure = std::get_if<Failure>(&signedRequest)) { finish(*failure); return false; }
    mfaChallenge_.clear(); instruction_.clear(); emit instructionChanged();
    if (done_) return false;
    const auto& value = std::get<AwsRoleRequest>(signedRequest);
    QNetworkRequest request(value.url);
    request.setRawHeader("Accept", "application/xml");
    for (auto header = value.headers.cbegin(); header != value.headers.cend(); ++header) request.setRawHeader(header.key(), header.value());
    send(request, value.body, false, [this](int status, const QByteArray& bytes) {
        if (status != 200) { finish(Failure{StoreError::ReadFailed, "AWS role sign-in was denied. Confirm another attempt to retry."}); return; }
        const auto parsed = nativeAwsRoleCredentials(bytes);
        if (const auto* failure = std::get_if<Failure>(&parsed)) { finish(*failure); return; }
        const auto& credentials = std::get<AwsTemporaryCredentials>(parsed);
        auto& eks = *connection_.exec->eks;
        eks.accessKey = credentials.accessKey; eks.secretKey = credentials.secretKey; eks.sessionToken = credentials.sessionToken;
        eks.credentialExpiresAt = credentials.expiresAt;
        eks.roles.removeFirst();
        assumeRole();
    });
    return true;
}
} // namespace podlord
