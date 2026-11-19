#include "chatgpt_client.h"
#include "native_oidc.h"
#include "session_store.h"
#include "ui_language.h"
#include <QCryptographicHash>
#include <QDesktopServices>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLockFile>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRandomGenerator>
#include <QTcpSocket>
#include <QTimeZone>
#include <QUrlQuery>
#include <QUuid>
#include <algorithm>
#include <array>

namespace podlord {
namespace {
QString randomValue() {
    std::array<quint32, 8> words;
    QRandomGenerator::system()->generate(words.begin(), words.end());
    const QByteArray bytes(reinterpret_cast<const char*>(words.data()), sizeof(words));
    return QString::fromLatin1(bytes.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
}
QByteArray form(const QList<QPair<QString, QString>>& fields) {
    QUrlQuery query;
    for (const auto& [name, value] : fields) query.addQueryItem(name, value);
    return query.toString(QUrl::FullyEncoded).toUtf8();
}
QNetworkRequest request(const QUrl& url, const QByteArray& bearer = {}) {
    QNetworkRequest result(url);
    result.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    result.setTransferTimeout(20000);
    result.setRawHeader("Accept", "application/json");
    if (!bearer.isEmpty()) result.setRawHeader("Authorization", "Bearer " + bearer);
    return result;
}
bool validBase(const QUrl& url) {
    return url.isValid() && (url.scheme() == "https" || (url.scheme() == "http" && url.host() == "127.0.0.1"))
        && !url.host().isEmpty() && url.userInfo().isEmpty() && url.path().isEmpty() && !url.hasQuery() && !url.hasFragment();
}
bool goodReply(QNetworkReply* reply, int maxBytes = 1024 * 1024) {
    return reply->error() == QNetworkReply::NoError
        && reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 200
        && reply->bytesAvailable() <= maxBytes;
}
void limitReply(QNetworkReply* reply, qint64 maxBytes) {
    QObject::connect(reply, &QNetworkReply::downloadProgress, reply, [reply, maxBytes](qint64 received, qint64) {
        if (received > maxBytes) reply->abort();
    });
}
} // namespace

ChatGptClient::ChatGptClient(QString profile, QUrl authBase, QUrl apiBase, QObject* parent, CredentialStore* credentials)
    : QObject(parent), profile_(std::move(profile)), authBase_(std::move(authBase)), apiBase_(std::move(apiBase)),
      credentials_(credentials ? credentials : &systemCredentials_) {
    loginTimer_.setSingleShot(true);
    connect(&loginTimer_, &QTimer::timeout, this, [this] { finishLogin("ChatGPT sign-in timed out. Try again."); });
    if (!validBase(authBase_) || !validBase(apiBase_)) { writable_ = false; status_ = "Invalid ChatGPT endpoint."; return; }
    const auto loaded = credentials_->readMigrating(profile_, "chatgpt", "chatgpt-auth.json", 2 * 1024 * 1024,
        [](const QByteArray& legacy) {
            auto root = QJsonDocument::fromJson(legacy).object();
            if (root.value("version") != 1 || !root.value("hostId").toString().startsWith("urn:uuid:")
                || !root.value("activeClientId").isString() || !root.value("accounts").isArray()) return QByteArray{};
            const auto activeId = root.value("activeClientId").toString();
            QJsonArray accounts;
            for (const auto& value : root.value("accounts").toArray()) {
                auto entry = value.toObject();
                if (entry.value("clientId").toString() != activeId) continue;
                if (entry.value("clientId").toString().isEmpty() || entry.value("subject").toString().isEmpty())
                    return QByteArray{};
                entry.insert("idToken", "");
                entry.insert("accessToken", "");
                accounts.append(entry);
                break;
            }
            if (!activeId.isEmpty() && accounts.isEmpty()) return QByteArray{};
            root.insert("accounts", accounts);
            return QJsonDocument(root).toJson(QJsonDocument::Compact);
        });
    if (!loaded.ok) { writable_ = false; status_ = loaded.error; return; }
    if (!loaded.found) return;
    savedBytes_ = loaded.data;
    const auto document = QJsonDocument::fromJson(savedBytes_);
    const auto root = document.object();
    if (!document.isObject() || root.value("version") != 1 || !root.value("hostId").isString()
        || !root.value("accounts").isArray() || !root.value("activeClientId").isString()) {
        writable_ = false; status_ = "ChatGPT credentials are invalid."; return;
    }
    hostId_ = root.value("hostId").toString();
    if (!hostId_.startsWith("urn:uuid:")) { writable_ = false; status_ = "ChatGPT host identity is invalid."; return; }
    activeClientId_ = root.value("activeClientId").toString();
    for (const auto& value : root.value("accounts").toArray()) {
        const auto entry = value.toObject();
        Account account{entry.value("clientId").toString(), entry.value("subject").toString(), entry.value("email").toString(),
            entry.value("idToken").toString(), entry.value("accessToken").toString(), entry.value("refreshToken").toString(),
            entry.value("scopes").toString(), QDateTime::fromString(entry.value("expiresAt").toString(), Qt::ISODateWithMs)};
        if (account.accessToken.isEmpty()) account.expiresAt = {};
        if (account.clientId.isEmpty() || account.subject.isEmpty()) { writable_ = false; status_ = "ChatGPT account data are invalid."; accounts_.clear(); return; }
        for (const auto& other : accounts_) if (other.clientId == account.clientId) {
            writable_ = false; status_ = "Duplicate ChatGPT account registration."; accounts_.clear(); return;
        }
        accounts_.append(account);
    }
    if (!activeClientId_.isEmpty() && !active()) { writable_ = false; status_ = "ChatGPT account selection is invalid."; accounts_.clear(); return; }
}

ChatGptClient::Account* ChatGptClient::active() {
    for (auto& account : accounts_) if (account.clientId == activeClientId_) return &account;
    return nullptr;
}
const ChatGptClient::Account* ChatGptClient::active() const {
    for (const auto& account : accounts_) if (account.clientId == activeClientId_) return &account;
    return nullptr;
}
QString ChatGptClient::activeAccount() const {
    const auto* account = active();
    return account ? (account->email.isEmpty() ? account->subject.left(12) : account->email) : QString{};
}
QString ChatGptClient::activeSubject() const {
    const auto* account = active();
    return account ? account->subject : QString{};
}
bool ChatGptClient::connected() const {
    const auto* account = active();
    return writable_ && account && !account->refreshToken.isEmpty()
        && account->scopes.split(' ', Qt::SkipEmptyParts).contains("chatgpt.tokens.use.direct");
}
bool ChatGptClient::save() {
    if (!writable_ || profileFailure(profile_) || !QDir().mkpath(profile_)) { status_ = "Cannot save private ChatGPT credentials."; emit changed(); return false; }
    const auto path = QDir(profile_).filePath("chatgpt-auth.json");
    QLockFile lock(path + ".lock"); lock.setStaleLockTime(0);
    if (!lock.tryLock(0)) { status_ = "ChatGPT credentials are in use by another PodLord window."; emit changed(); return false; }
    const auto current = credentials_->read(profile_, "chatgpt");
    if (!current.ok || (current.found ? current.data : QByteArray{}) != savedBytes_) {
        status_ = current.ok ? "ChatGPT credentials changed in another process. Restart PodLord." : current.error;
        emit changed(); return false;
    }
    QJsonArray accounts;
    for (const auto& account : accounts_) accounts.append(QJsonObject{{"clientId", account.clientId}, {"subject", account.subject},
        {"email", account.email}, {"idToken", ""}, {"accessToken", ""},
        {"refreshToken", account.refreshToken}, {"scopes", account.scopes},
        {"expiresAt", account.expiresAt.toUTC().toString(Qt::ISODateWithMs)}});
    const auto data = QJsonDocument(QJsonObject{{"version", 1}, {"hostId", hostId_},
        {"activeClientId", activeClientId_}, {"accounts", accounts}}).toJson();
    QString error;
    if (!credentials_->write(profile_, "chatgpt", data, error)) { status_ = error; emit changed(); return false; }
    savedBytes_ = data;
    return true;
}
bool ChatGptClient::signIn(const QString& clientId, const QString& language) {
    if (!writable_ || !state_.isEmpty() || !validBase(authBase_)) return false;
    const auto requestedClientId = clientId.isEmpty() ? retryClientId_ : clientId;
    if (!requestedClientId.isEmpty() && requestedClientId != retryClientId_) {
        bool found = false;
        for (const auto& account : accounts_) if (account.clientId == requestedClientId) found = true;
        if (!found) return false;
    }
    if (hostId_.isEmpty()) {
        hostId_ = "urn:uuid:" + QUuid::createUuid().toString(QUuid::WithoutBraces);
        if (!save()) { hostId_.clear(); return false; }
    }
    if (!callbackServer_.listen(QHostAddress::LocalHost, 0)) {
        status_ = "Cannot open a local callback for ChatGPT sign-in."; emit changed(); return false;
    }
    pendingClientId_ = requestedClientId;
    loginLanguage_ = language;
    state_ = randomValue(); nonce_ = randomValue(); verifier_ = randomValue();
    redirectUri_ = QString("http://127.0.0.1:%1/auth/callback").arg(callbackServer_.serverPort());
    connect(&callbackServer_, &QTcpServer::newConnection, this, [this] {
        auto* socket = callbackServer_.nextPendingConnection();
        if (!socket) return;
        auto* bytes = new QByteArray;
        connect(socket, &QTcpSocket::readyRead, this, [this, socket, bytes] {
            bytes->append(socket->readAll());
            if (bytes->size() > 8192) { socket->disconnectFromHost(); return; }
            const auto end = bytes->indexOf("\r\n\r\n");
            if (end < 0) return;
            const auto line = bytes->left(bytes->indexOf("\r\n"));
            const auto parts = line.split(' ');
            const QUrl callback(parts.size() == 3 ? QString::fromLatin1(parts[1]) : QString{});
            const QUrlQuery query(callback);
            const auto items = query.queryItems(QUrl::FullyDecoded);
            const auto count = [&items](const QString& name) {
                return std::count_if(items.cbegin(), items.cend(), [&name](const auto& item) { return item.first == name; });
            };
            const bool valid = parts.size() == 3 && parts[0] == "GET" && parts[2] == "HTTP/1.1"
                && parts[1].startsWith("/auth/callback?") && callback.path() == "/auth/callback" && !state_.isEmpty()
                && count("state") == 1 && count("code") <= 1 && count("client_id") <= 1 && count("error") <= 1
                && query.queryItemValue("state", QUrl::FullyDecoded) == state_;
            const auto code = valid ? query.queryItemValue("code", QUrl::FullyDecoded) : QString{};
            const auto issued = valid ? query.queryItemValue("client_id", QUrl::FullyDecoded) : QString{};
            const auto denied = valid && !query.queryItemValue("error", QUrl::FullyDecoded).isEmpty();
            const QByteArray body = localizedAuthCallbackPage(valid && !denied && !code.isEmpty(), loginLanguage_);
            socket->write("HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nCache-Control: no-store\r\nReferrer-Policy: no-referrer\r\nX-Content-Type-Options: nosniff\r\nContent-Security-Policy: default-src 'none'; style-src 'unsafe-inline'; base-uri 'none'; frame-ancestors 'none'\r\nContent-Length: "
                + QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body);
            socket->disconnectFromHost();
            if (!valid) return;
            callbackServer_.close(); loginTimer_.stop();
            if (denied || code.isEmpty() || code.size() > 4096) { finishLogin("ChatGPT sign-in was cancelled or rejected."); return; }
            if ((pendingClientId_.isEmpty() && (!issued.startsWith("oaiapp_") || issued.size() > 256))
                || (!pendingClientId_.isEmpty() && !issued.isEmpty() && issued != pendingClientId_)) {
                finishLogin("ChatGPT returned an unexpected client registration."); return;
            }
            exchangeCode(code, pendingClientId_.isEmpty() ? issued : pendingClientId_);
        });
        connect(socket, &QObject::destroyed, this, [bytes] { delete bytes; });
        connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
    });
    QUrl authorization = authBase_.resolved(QUrl("/api/accounts/authorize"));
    QUrlQuery query;
    query.addQueryItem("client_id", requestedClientId.isEmpty() ? "dynamic_agent_client" : requestedClientId);
    if (requestedClientId.isEmpty()) query.addQueryItem("agent_name_hint", "PodLord");
    query.addQueryItem("ext_agent_host_id", hostId_);
    query.addQueryItem("response_type", "code");
    query.addQueryItem("redirect_uri", redirectUri_);
    query.addQueryItem("scope", "openid profile email offline_access resource.invoke chatgpt.tokens.use.direct");
    query.addQueryItem("resource", "https://api.openai.com/v1");
    query.addQueryItem("state", state_); query.addQueryItem("nonce", nonce_);
    query.addQueryItem("code_challenge_method", "S256");
    query.addQueryItem("code_challenge", QString::fromLatin1(QCryptographicHash::hash(verifier_.toLatin1(), QCryptographicHash::Sha256)
        .toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals)));
    if (!requestedClientId.isEmpty()) for (const auto& account : accounts_) if (account.clientId == requestedClientId) {
        if (!account.idToken.isEmpty()) query.addQueryItem("id_token_hint", account.idToken);
        if (!account.email.isEmpty()) query.addQueryItem("login_hint", account.email);
        if (!account.scopes.split(' ', Qt::SkipEmptyParts).contains("chatgpt.tokens.use.direct"))
            query.addQueryItem("prompt", "consent");
    }
    authorization.setQuery(query);
    loginTimer_.start(300000);
    if (!QDesktopServices::openUrl(authorization)) { finishLogin("Could not open the browser for ChatGPT sign-in."); return false; }
    status_ = "Complete ChatGPT sign-in in your browser."; emit changed();
    return true;
}
void ChatGptClient::finishLogin(const QString& error) {
    callbackServer_.close(); loginTimer_.stop();
    disconnect(&callbackServer_, nullptr, this, nullptr);
    if (loginReply_) { loginReply_->abort(); loginReply_ = nullptr; }
    pendingClientId_.clear(); issuedClientId_.clear(); state_.clear(); nonce_.clear(); verifier_.clear(); redirectUri_.clear();
    status_ = error; emit changed();
}
void ChatGptClient::exchangeCode(const QString& code, const QString& issuedClientId) {
    issuedClientId_ = issuedClientId;
    auto req = request(authBase_.resolved(QUrl("/api/accounts/oauth/token")));
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");
    loginReply_ = network_.post(req, form({{"grant_type", "authorization_code"}, {"client_id", issuedClientId},
        {"code", code}, {"code_verifier", verifier_}, {"redirect_uri", redirectUri_}, {"resource", "https://api.openai.com/v1"}}));
    auto* reply = loginReply_.data();
    limitReply(reply, 65536);
    connect(reply, &QNetworkReply::finished, this, [this, reply, issuedClientId] {
        const bool ok = goodReply(reply, 65536);
        const auto bytes = reply->read(65537); reply->deleteLater(); loginReply_ = nullptr;
        if (!ok || bytes.size() > 65536) {
            if (QJsonDocument::fromJson(bytes).object().value("error") == "invalid_grant") retryClientId_ = issuedClientId;
            finishLogin("ChatGPT token exchange failed. Sign in again."); return;
        }
        const auto tokens = QJsonDocument::fromJson(bytes);
        if (!tokens.isObject()) { finishLogin("ChatGPT returned an invalid token response."); return; }
        acceptTokens(tokens.object(), issuedClientId);
    });
}
void ChatGptClient::acceptTokens(const QJsonObject& tokens, const QString& issuedClientId) {
    const auto idToken = tokens.value("id_token").toString();
    const auto accessToken = tokens.value("access_token").toString();
    const auto refreshToken = tokens.value("refresh_token").toString();
    const auto scopes = tokens.value("scope").toString();
    const auto seconds = tokens.value("expires_in").toInt();
    if (idToken.isEmpty() || accessToken.isEmpty() || refreshToken.isEmpty() || seconds < 60 || seconds > 86400
        || tokens.value("token_type").toString().compare("Bearer", Qt::CaseInsensitive) != 0) {
        finishLogin("ChatGPT returned incomplete credentials."); return;
    }
    loginReply_ = network_.get(request(authBase_.resolved(QUrl("/.well-known/jwks.json"))));
    auto* reply = loginReply_.data();
    limitReply(reply, 256 * 1024);
    connect(reply, &QNetworkReply::finished, this, [this, reply, tokens, issuedClientId, scopes, seconds] {
        const bool ok = goodReply(reply, 256 * 1024);
        const auto bytes = reply->read(256 * 1024 + 1); reply->deleteLater(); loginReply_ = nullptr;
        const auto keys = QJsonDocument::fromJson(bytes);
        const auto claims = ok && keys.isObject() ? verifiedOidcIdClaims(tokens.value("id_token").toString(),
            authBase_.toString(), issuedClientId, keys.object(), nonce_) : std::optional<QJsonObject>{};
        if (!claims) { finishLogin("ChatGPT identity verification failed."); return; }
        const auto subject = claims->value("sub").toString();
        for (const auto& account : accounts_)
            if (account.clientId == issuedClientId && account.subject != subject) {
                finishLogin("ChatGPT returned a different account. Nothing was changed."); return;
            }
        const auto prior = accounts_;
        const auto priorActive = activeClientId_;
        Account account{issuedClientId, subject, claims->value("email").toString(), tokens.value("id_token").toString(),
            tokens.value("access_token").toString(), tokens.value("refresh_token").toString(), scopes,
            QDateTime::currentDateTimeUtc().addSecs(seconds)};
        accounts_ = {account};
        activeClientId_ = issuedClientId;
        if (!save()) { accounts_ = prior; activeClientId_ = priorActive; finishLogin(status_); return; }
        retryClientId_.clear();
        models_.clear();
        finishLogin(scopes.split(' ', Qt::SkipEmptyParts).contains("chatgpt.tokens.use.direct")
            ? QString{} : "ChatGPT plan permission was not granted. Reauthorize from settings.");
        if (connected()) refreshModels();
    });
}
void ChatGptClient::ensureAccess(std::function<void(bool)> done) {
    if (!connected()) { status_ = "Connect a ChatGPT account with plan permission first."; emit changed(); done(false); return; }
    if (active()->expiresAt > QDateTime::currentDateTimeUtc().addSecs(120)) { done(true); return; }
    accessWaiters_.append(std::move(done));
    if (refreshReply_) return;
    const auto path = QDir(profile_).filePath("chatgpt-auth.json");
    refreshLock_ = std::make_unique<QLockFile>(path + ".refresh.lock");
    refreshLock_->setStaleLockTime(0);
    const bool locked = refreshLock_->tryLock(0);
    const auto current = locked ? credentials_->read(profile_, "chatgpt") : CredentialStore::ReadResult{};
    if (!locked || !current.ok || !current.found || current.data != savedBytes_) {
        refreshLock_.reset();
        status_ = "ChatGPT credentials are in use or changed elsewhere. Restart PodLord if this persists."; emit changed();
        auto waiters = std::move(accessWaiters_); accessWaiters_.clear();
        for (auto& waiter : waiters) waiter(false);
        return;
    }
    const auto clientId = activeClientId_;
    auto req = request(authBase_.resolved(QUrl("/api/accounts/oauth/token")));
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");
    refreshReply_ = network_.post(req, form({{"grant_type", "refresh_token"}, {"client_id", clientId},
        {"refresh_token", active()->refreshToken}, {"resource", "https://api.openai.com/v1"}}));
    auto* reply = refreshReply_.data();
    limitReply(reply, 65536);
    connect(reply, &QNetworkReply::finished, this, [this, reply, clientId] {
        const bool ok = goodReply(reply, 65536);
        const auto data = reply->read(65537); reply->deleteLater(); refreshReply_ = nullptr;
        const auto tokens = QJsonDocument::fromJson(data).object();
        if (clientId != activeClientId_) {
            refreshLock_.reset();
            auto waiters = std::move(accessWaiters_); accessWaiters_.clear();
            for (auto& waiter : waiters) waiter(false);
            if (connected()) refreshModels();
            return;
        }
        bool refreshed = false;
        if (ok && clientId == activeClientId_ && !tokens.value("access_token").toString().isEmpty()
            && !tokens.value("refresh_token").toString().isEmpty() && tokens.value("expires_in").toInt() >= 60
            && tokens.value("token_type").toString().compare("Bearer", Qt::CaseInsensitive) == 0) {
            auto* account = active();
            const auto previous = *account;
            account->accessToken = tokens.value("access_token").toString();
            account->refreshToken = tokens.value("refresh_token").toString();
            if (tokens.value("scope").isString()) account->scopes = tokens.value("scope").toString();
            account->expiresAt = QDateTime::currentDateTimeUtc().addSecs(tokens.value("expires_in").toInt());
            if (save()) refreshed = connected(); else *account = previous;
        }
        if (!refreshed) {
            if (clientId == activeClientId_ && tokens.value("error").toString() == "invalid_grant" && active()) {
                active()->accessToken.clear(); active()->refreshToken.clear(); active()->idToken.clear(); save();
            }
            status_ = "ChatGPT session could not be renewed. Sign in again or retry later."; emit changed();
        }
        refreshLock_.reset();
        auto waiters = std::move(accessWaiters_); accessWaiters_.clear();
        for (auto& waiter : waiters) waiter(refreshed);
    });
}
void ChatGptClient::refreshModels() {
    if (modelsReply_) return;
    ensureAccess([this](bool ready) {
        if (!ready || !active()) return;
        const auto clientId = activeClientId_;
        modelsReply_ = network_.get(request(apiBase_.resolved(QUrl("/v1/models")), active()->accessToken.toUtf8()));
        auto* reply = modelsReply_.data();
        limitReply(reply, 1024 * 1024);
        connect(reply, &QNetworkReply::finished, this, [this, reply, clientId] {
            const bool ok = goodReply(reply);
            const auto bytes = reply->read(1024 * 1024 + 1); reply->deleteLater(); modelsReply_ = nullptr;
            if (clientId != activeClientId_) { if (connected()) refreshModels(); return; }
            const auto document = QJsonDocument::fromJson(bytes);
            if (!ok || !document.isObject() || bytes.size() > 1024 * 1024) {
                models_.clear(); status_ = "ChatGPT model catalog is unavailable."; emit changed(); return;
            }
            QVariantList models;
            for (const auto& value : document.object().value("models").toArray()) {
                const auto entry = value.toObject();
                const auto slug = entry.value("slug").toString();
                if (entry.value("visibility") == "list" && !slug.isEmpty()) models.append(QVariantMap{{"slug", slug},
                    {"label", entry.value("display_name").toString(slug)}});
            }
            models_ = models;
            status_ = models_.isEmpty() ? "This ChatGPT account has no available models." : QString{};
            emit changed();
        });
    });
}
void ChatGptClient::cancelAnalysis() {
    if (analysisReply_) analysisReply_->abort();
}
void ChatGptClient::analyze(const QString& model, const QString& instructions, const QString& input,
    std::function<void(QString, QString)> done) {
    if (analysisReply_ || model.isEmpty()) { done({}, "ChatGPT analysis is already running or no model was selected."); return; }
    bool listed = false;
    for (const auto& choice : models_) if (choice.toMap().value("slug") == model) listed = true;
    if (!listed) { done({}, "Selected ChatGPT model is unavailable for this account."); return; }
    ensureAccess([this, model, instructions, input, done = std::move(done)](bool ready) mutable {
        if (!ready || !active()) { done({}, status_); return; }
        auto req = request(apiBase_.resolved(QUrl("/v1/responses")), active()->accessToken.toUtf8());
        req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
        req.setTransferTimeout(180000);
        const auto payload = QJsonDocument(QJsonObject{{"model", model}, {"instructions", instructions},
            {"input", QJsonArray{QJsonObject{{"role", "user"}, {"content", input}}}}, {"store", false}, {"stream", true}}).toJson(QJsonDocument::Compact);
        analysisReply_ = network_.post(req, payload);
        emit analysisSent(active()->subject);
        auto* reply = analysisReply_.data();
        limitReply(reply, 1024 * 1024);
        connect(reply, &QNetworkReply::finished, this, [this, reply, done = std::move(done)] {
            const bool ok = goodReply(reply);
            const int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            const auto requestId = reply->rawHeader("openai-request-id").isEmpty()
                ? reply->rawHeader("x-request-id") : reply->rawHeader("openai-request-id");
            auto bytes = reply->read(1024 * 1024 + 1); reply->deleteLater(); analysisReply_ = nullptr;
            if (!ok || bytes.size() > 1024 * 1024) {
                const auto detail = QJsonDocument::fromJson(bytes).object();
                const auto code = detail.value("error").toObject().value("code").toString();
                QString message = QString("ChatGPT request failed (HTTP %1)").arg(httpStatus);
                if (!code.isEmpty() && code.size() <= 100
                    && std::all_of(code.cbegin(), code.cend(), [](QChar c) { return c.isLetterOrNumber() || c == '_' || c == '-'; }))
                    message += ": " + code;
                if (!requestId.isEmpty() && requestId.size() <= 100
                    && std::all_of(requestId.cbegin(), requestId.cend(), [](char c) {
                        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
                    })) message += " [" + QString::fromLatin1(requestId) + "]";
                if (httpStatus == 429) message += ". Check ChatGPT Settings → Usage.";
                done({}, message); return;
            }
            QString answer, error;
            bool completed = false;
            const auto events = bytes.replace("\r\n", "\n").split('\n');
            QByteArray data;
            for (const auto& line : events) {
                if (line.startsWith("data: ")) data += line.mid(6);
                else if (line.isEmpty() && !data.isEmpty()) {
                    const auto event = QJsonDocument::fromJson(data).object();
                    const auto type = event.value("type").toString();
                    if (type == "response.output_text.delta") answer += event.value("delta").toString();
                    else if (type == "response.completed") {
                        completed = true;
                        const auto output = event.value("response").toObject().value("output").toArray();
                        QString finalText;
                        for (const auto& item : output) for (const auto& content : item.toObject().value("content").toArray())
                            if (content.toObject().value("type") == "output_text") finalText += content.toObject().value("text").toString();
                        if (!finalText.isEmpty()) answer = finalText;
                    } else if (type == "response.failed" || type == "response.incomplete")
                        error = event.value("response").toObject().value("error").toObject().value("code").toString(type);
                    data.clear();
                }
            }
            if (!completed || !error.isEmpty() || answer.size() > 65536) {
                done({}, error.isEmpty() ? "ChatGPT stream ended without a completed answer."
                    : error == "subscription_sharing_usage_limit_exceeded" ? "ChatGPT usage limit reached. Check ChatGPT Settings → Usage."
                    : "ChatGPT: " + error); return;
            }
            done(answer, {});
        });
    });
}
void ChatGptClient::signOut() {
    auto* account = active();
    if (!account || !writable_) return;
    if (!state_.isEmpty()) finishLogin({});
    if (refreshReply_) refreshReply_->abort();
    cancelAnalysis(); models_.clear();
    const auto refreshToken = account->refreshToken;
    const auto clientId = account->clientId;
    const auto previous = accounts_;
    const auto previousActive = activeClientId_;
    const auto previousRetry = retryClientId_;
    accounts_.clear(); activeClientId_.clear(); retryClientId_.clear();
    if (!save()) { accounts_ = previous; activeClientId_ = previousActive; retryClientId_ = previousRetry;
        status_ = "Could not clear ChatGPT credentials locally."; emit changed(); return; }
    status_ = "Signed out locally. Checking remote revocation…"; emit changed();
    if (refreshToken.isEmpty()) { status_.clear(); emit changed(); return; }
    revokeReply_ = network_.get(request(authBase_.resolved(QUrl("/.well-known/openid-configuration"))));
    auto* discovery = revokeReply_.data();
    limitReply(discovery, 65536);
    connect(discovery, &QNetworkReply::finished, this, [this, discovery, refreshToken, clientId] {
        const bool ok = goodReply(discovery, 65536);
        const auto config = QJsonDocument::fromJson(discovery->read(65537)).object();
        discovery->deleteLater(); revokeReply_ = nullptr;
        const QUrl endpoint(config.value("revocation_endpoint").toString());
        if (!ok || endpoint.scheme() != authBase_.scheme() || endpoint.host() != authBase_.host()
            || endpoint.port() != authBase_.port() || !endpoint.userInfo().isEmpty()
            || endpoint.hasQuery() || endpoint.hasFragment()) {
            status_ = "Signed out locally. Remote revocation could not be confirmed."; emit changed(); return;
        }
        auto req = request(endpoint);
        req.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");
        revokeReply_ = network_.post(req, form({{"token", refreshToken}, {"token_type_hint", "refresh_token"}, {"client_id", clientId}}));
        auto* reply = revokeReply_.data();
        limitReply(reply, 65536);
        connect(reply, &QNetworkReply::finished, this, [this, reply] {
            const bool revoked = goodReply(reply, 65536); reply->deleteLater(); revokeReply_ = nullptr;
            status_ = revoked ? "Signed out of ChatGPT." : "Signed out locally. Remote revocation could not be confirmed.";
            emit changed();
        });
    });
}
} // namespace podlord
