#include "native_aws_sso.h"
#include "native_eks.h"
#include <QDesktopServices>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrlQuery>
#include <algorithm>

namespace podlord {
namespace {
Failure invalidResponse() { return {StoreError::InvalidData, "AWS SSO returned an invalid response. Confirm another attempt to retry."}; }
bool tokenText(const QString& text, qsizetype limit) {
    return !text.isEmpty() && text.size() <= limit && std::all_of(text.cbegin(), text.cend(), [](QChar c) { return c.unicode() >= 33 && c.unicode() <= 126; });
}
bool browserUrl(const QUrl& url) {
    return url.isValid() && url.scheme() == "https" && !url.host().isEmpty() && url.userInfo().isEmpty() && !url.hasFragment();
}
} // namespace

NativeAwsSso::NativeAwsSso(ClusterConnection connection, QObject* parent)
    : QObject(parent), connection_(std::move(connection)) {
    timer_.setSingleShot(true);
    timer_.setTimerType(Qt::PreciseTimer);
    connect(&timer_, &QTimer::timeout, this, &NativeAwsSso::poll);
}
QString NativeAwsSso::instruction() const { return instruction_; }
NativeAwsSso::~NativeAwsSso() {
    done_ = true;
    timer_.stop();
    if (pending_) { disconnect(pending_, nullptr, this, nullptr); pending_->abort(); }
}
void NativeAwsSso::finish(Result<ClusterConnection> result) {
    if (done_) return;
    done_ = true;
    timer_.stop();
    clientId_.clear(); clientSecret_.clear(); deviceCode_.clear(); accessToken_.clear(); instruction_.clear();
    connection_.exec.reset();
    if (pending_) {
        auto* reply = pending_.data(); pending_ = nullptr;
        reply->abort(); reply->deleteLater();
    }
    emit completed(std::move(result));
}
void NativeAwsSso::cancel() { finish(Failure{StoreError::ReadFailed, "AWS sign-in cancelled. Confirm another attempt to retry."}); }
void NativeAwsSso::send(const QUrl& url, const QJsonObject& body, bool get, std::function<void(int, const QJsonObject&)> done) {
    if (done_) return;
    QNetworkRequest request(url);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    request.setTransferTimeout(20000);
    request.setRawHeader("Accept", "application/json");
    if (get) request.setRawHeader("x-amz-sso_bearer_token", accessToken_.toLatin1());
    else request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    auto* reply = get ? network_.get(request) : network_.post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
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
        if (failed) { finish(Failure{StoreError::ReadFailed, "AWS SSO could not complete the request. Confirm another attempt to retry."}); return; }
        QJsonParseError error;
        const auto json = QJsonDocument::fromJson(bytes, &error);
        if (error.error != QJsonParseError::NoError || !json.isObject()) { finish(invalidResponse()); return; }
        done(status, json.object());
    });
}
void NativeAwsSso::start() {
    if (done_ || started_) return;
    started_ = true;
    if (!connection_.exec || !connection_.exec->eks || !connection_.exec->eks->sso) { finish(invalidResponse()); return; }
    // Revalidate the typed boundary, including callers other than the kubeconfig resolver.
    const auto& eks = *connection_.exec->eks;
    const auto& sso = *eks.sso;
    const QMap<QString, QString> fields{{"region", eks.region}, {"sso_start_url", sso.startUrl.toString()},
        {"sso_region", sso.region}, {"sso_account_id", sso.accountId}, {"sso_role_name", sso.roleName}};
    const auto validated = nativeEksConfig(*connection_.exec, fields);
    if (!std::holds_alternative<std::optional<EksLogin>>(validated) || !std::get<std::optional<EksLogin>>(validated)
        || !std::get<std::optional<EksLogin>>(validated)->sso) { finish(invalidResponse()); return; }
    const auto suffix = sso.region.startsWith("cn-") ? ".amazonaws.com.cn" : ".amazonaws.com";
    oidc_ = QUrl("https://oidc." + sso.region + suffix);
    portal_ = QUrl("https://portal.sso." + sso.region + suffix);
    auto registration = oidc_; registration.setPath("/client/register");
    send(registration, {{"clientName", "Podlord"}, {"clientType", "public"}, {"scopes", QJsonArray{"sso:account:access"}},
        {"grantTypes", QJsonArray{"urn:ietf:params:oauth:grant-type:device_code"}}}, false,
        [this](int status, const QJsonObject& response) {
            const auto id = response.value("clientId").toString(), secret = response.value("clientSecret").toString();
            const auto expiry = response.value("clientSecretExpiresAt").toInteger();
            if (status != 200 || !tokenText(id, 4096) || !tokenText(secret, 8192)
                || expiry <= QDateTime::currentSecsSinceEpoch() + 30) { finish(invalidResponse()); return; }
            clientId_ = id; clientSecret_ = secret;
            deadline_ = QDateTime::fromSecsSinceEpoch(expiry);
            auto device = oidc_; device.setPath("/device_authorization");
            send(device, {{"clientId", id}, {"clientSecret", secret}, {"startUrl", connection_.exec->eks->sso->startUrl.toString()}}, false,
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
void NativeAwsSso::poll() {
    if (QDateTime::currentDateTimeUtc() >= deadline_) {
        finish(Failure{StoreError::ReadFailed, "The AWS sign-in code expired. Confirm another attempt to retry."}); return;
    }
    auto token = oidc_; token.setPath("/token");
    send(token, {{"clientId", clientId_}, {"clientSecret", clientSecret_}, {"deviceCode", deviceCode_},
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
            send(credentials, {}, true, [this](int status, const QJsonObject& response) {
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
                finish(nativeEksToken(std::move(connection_), now));
            });
        });
}
} // namespace podlord
