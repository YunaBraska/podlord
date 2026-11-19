#include "api_ai_client.h"
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
#include <QUrlQuery>
#include <algorithm>
#include <array>
#include <memory>

namespace podlord {
namespace {
void limitReply(QNetworkReply* reply, qint64 maxBytes) {
    QObject::connect(reply, &QNetworkReply::downloadProgress, reply, [reply, maxBytes](qint64 received, qint64) {
        if (received > maxBytes) reply->abort();
    });
}
}
QString ApiAiClient::identity() const {
    return QString::fromLatin1(QCryptographicHash::hash(key_.toUtf8(), QCryptographicHash::Sha256).toHex());
}
ApiAiClient::ApiAiClient(QString profile, Provider provider, QUrl base, QObject* parent, CredentialStore* credentials)
    : QObject(parent), profile_(std::move(profile)), provider_(provider),
      base_(base.isEmpty() ? (provider == Provider::Claude ? QUrl("https://api.anthropic.com") : QUrl("https://openrouter.ai")) : std::move(base)),
      credentials_(credentials ? credentials : &systemCredentials_) {
    loginTimer_.setSingleShot(true);
    connect(&loginTimer_, &QTimer::timeout, this, [this] { finishLogin("OpenRouter sign-in timed out. Try again."); });
    if (!base_.isValid() || (base_.scheme() != "https" && !(base_.scheme() == "http" && base_.host() == "127.0.0.1"))
        || base_.host().isEmpty() || !base_.userInfo().isEmpty() || !base_.path().isEmpty() || base_.hasQuery() || base_.hasFragment()) {
        writable_ = false; status_ = "Invalid AI provider endpoint."; return;
    }
    const auto name = provider_ == Provider::Claude ? "claude" : "openrouter";
    const auto loaded = credentials_->readMigrating(profile_, name, name + QStringLiteral("-auth.json"), 8192,
        [](const QByteArray& legacy) {
            const auto document = QJsonDocument::fromJson(legacy);
            return document.isObject() && document.object().value("version") == 1
                && document.object().value("key").isString() ? legacy : QByteArray{};
        });
    if (!loaded.ok) { writable_ = false; status_ = loaded.error; return; }
    if (!loaded.found) return;
    savedBytes_ = loaded.data;
    const auto document = QJsonDocument::fromJson(savedBytes_);
    if (!document.isObject() || document.object().value("version") != 1 || !document.object().value("key").isString()) {
        writable_ = false; status_ = "AI provider credentials are invalid."; return;
    }
    key_ = document.object().value("key").toString();
}
bool ApiAiClient::signIn(const QString& language) {
    if (provider_ != Provider::OpenRouter || !writable_ || !loginState_.isEmpty()) return false;
    if (!callbackServer_.listen(QHostAddress::LocalHost, 0)) {
        status_ = "Cannot open a local callback for OpenRouter sign-in."; emit changed(); return false;
    }
    const auto randomValue = [] {
        std::array<quint32, 8> words;
        QRandomGenerator::system()->generate(words.begin(), words.end());
        return QString::fromLatin1(QByteArray(reinterpret_cast<const char*>(words.data()), sizeof(words))
            .toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
    };
    loginState_ = randomValue();
    loginVerifier_ = randomValue();
    loginLanguage_ = language;
    connect(&callbackServer_, &QTcpServer::newConnection, this, [this] {
        auto* socket = callbackServer_.nextPendingConnection();
        if (!socket) return;
        auto bytes = std::make_shared<QByteArray>();
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
                && parts[1].startsWith("/callback?") && callback.path() == "/callback" && !loginState_.isEmpty()
                && count("state") == 1 && count("code") <= 1 && count("error") <= 1
                && query.queryItemValue("state", QUrl::FullyDecoded) == loginState_;
            const auto code = valid ? query.queryItemValue("code", QUrl::FullyDecoded) : QString{};
            const bool denied = valid && !query.queryItemValue("error", QUrl::FullyDecoded).isEmpty();
            const QByteArray body = localizedAuthCallbackPage(valid && !denied && !code.isEmpty(), loginLanguage_);
            socket->write("HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nCache-Control: no-store\r\nReferrer-Policy: no-referrer\r\nX-Content-Type-Options: nosniff\r\nContent-Security-Policy: default-src 'none'; style-src 'unsafe-inline'; base-uri 'none'; frame-ancestors 'none'\r\nContent-Length: "
                + QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body);
            socket->disconnectFromHost();
            if (!valid) return;
            callbackServer_.close(); loginTimer_.stop();
            disconnect(&callbackServer_, nullptr, this, nullptr);
            if (denied || code.isEmpty() || code.size() > 4096) {
                finishLogin("OpenRouter sign-in was cancelled or rejected."); return;
            }
            QNetworkRequest request(base_.resolved(QUrl("/api/v1/auth/keys")));
            request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
            request.setTransferTimeout(20000);
            request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
            loginReply_ = network_.post(request, QJsonDocument(QJsonObject{{"code", code},
                {"code_verifier", loginVerifier_}, {"code_challenge_method", "S256"}}).toJson(QJsonDocument::Compact));
            auto* reply = loginReply_.data();
            limitReply(reply, 65536);
            connect(reply, &QNetworkReply::finished, this, [this, reply] {
                const bool ok = reply->error() == QNetworkReply::NoError
                    && reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 200
                    && reply->bytesAvailable() <= 65536;
                const auto data = reply->read(65537);
                reply->deleteLater(); loginReply_ = nullptr;
                if (loginState_.isEmpty()) return;
                const auto key = QJsonDocument::fromJson(data).object().value("key").toString();
                loginState_.clear(); loginVerifier_.clear();
                if (!ok || data.size() > 65536 || !setKey(key)) {
                    if (status_.isEmpty()) status_ = "OpenRouter token exchange failed. Sign in again.";
                    emit changed();
                }
            });
        });
        connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
    });
    QUrl authorization = base_.resolved(QUrl("/auth"));
    QUrlQuery query;
    query.addQueryItem("callback_url", QString("http://127.0.0.1:%1/callback").arg(callbackServer_.serverPort()));
    query.addQueryItem("code_challenge", QString::fromLatin1(QCryptographicHash::hash(loginVerifier_.toLatin1(),
        QCryptographicHash::Sha256).toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals)));
    query.addQueryItem("code_challenge_method", "S256");
    query.addQueryItem("state", loginState_);
    authorization.setQuery(query);
    loginTimer_.start(300000);
    if (!QDesktopServices::openUrl(authorization)) {
        finishLogin("Could not open the browser for OpenRouter sign-in."); return false;
    }
    status_ = "Complete OpenRouter sign-in in your browser."; emit changed();
    return true;
}
void ApiAiClient::finishLogin(const QString& status) {
    callbackServer_.close(); loginTimer_.stop();
    disconnect(&callbackServer_, nullptr, this, nullptr);
    if (loginReply_) { loginReply_->abort(); loginReply_ = nullptr; }
    loginState_.clear(); loginVerifier_.clear();
    status_ = status; emit changed();
}
bool ApiAiClient::save() {
    if (!writable_ || profileFailure(profile_) || !QDir().mkpath(profile_)) {
        status_ = "Cannot save private AI provider credentials."; emit changed(); return false;
    }
    const auto path = QDir(profile_).filePath(provider_ == Provider::Claude ? "claude-auth.json" : "openrouter-auth.json");
    QLockFile lock(path + ".lock"); lock.setStaleLockTime(0);
    if (!lock.tryLock(0)) { status_ = "AI credentials are in use by another PodLord process."; emit changed(); return false; }
    const auto name = provider_ == Provider::Claude ? "claude" : "openrouter";
    const auto current = credentials_->read(profile_, name);
    if (!current.ok || (current.found ? current.data : QByteArray{}) != savedBytes_) {
        status_ = current.ok ? "AI credentials changed in another process. Restart PodLord." : current.error;
        emit changed(); return false;
    }
    const auto data = QJsonDocument(QJsonObject{{"version", 1}, {"key", key_}}).toJson();
    QString error;
    const bool saved = key_.isEmpty() ? credentials_->remove(profile_, name, error)
        : credentials_->write(profile_, name, data, error);
    if (!saved) { status_ = error; emit changed(); return false; }
    savedBytes_ = key_.isEmpty() ? QByteArray{} : data;
    return true;
}
bool ApiAiClient::setKey(const QString& key) {
    if (!writable_ || key.isEmpty() || key.size() > 4096
        || std::any_of(key.cbegin(), key.cend(), [](QChar c) { return c.isSpace() || c.category() == QChar::Other_Control; })) return false;
    const auto previous = key_;
    key_ = key;
    if (!save()) { key_ = previous; return false; }
    if (!loginState_.isEmpty()) finishLogin({});
    if (analysisReply_) analysisReply_->abort();
    if (modelsReply_) modelsReply_->abort();
    models_.clear(); status_.clear(); emit changed(); refreshModels();
    return true;
}
void ApiAiClient::clearKey() {
    if (!loginState_.isEmpty()) finishLogin({});
    if (!writable_ || key_.isEmpty()) return;
    const auto previous = key_;
    key_.clear();
    if (!save()) { key_ = previous; return; }
    if (analysisReply_) analysisReply_->abort();
    if (modelsReply_) modelsReply_->abort();
    models_.clear(); status_.clear(); emit changed();
}
void ApiAiClient::refreshModels() {
    if (!configured() || modelsReply_) return;
    loadModels({}, {});
}
void ApiAiClient::loadModels(const QString& after, QVariantList collected) {
    QUrl url = base_.resolved(QUrl(provider_ == Provider::Claude ? "/v1/models" : "/api/v1/models"));
    if (provider_ == Provider::Claude) {
        QUrlQuery query; query.addQueryItem("limit", "100");
        if (!after.isEmpty()) query.addQueryItem("after_id", after);
        url.setQuery(query);
    }
    QNetworkRequest request(url);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    request.setTransferTimeout(20000);
    request.setRawHeader("Authorization", "Bearer " + key_.toUtf8());
    if (provider_ == Provider::Claude) request.setRawHeader("anthropic-version", "2026-10-21");
    const auto key = key_;
    modelsReply_ = network_.get(request);
    auto* reply = modelsReply_.data();
    limitReply(reply, provider_ == Provider::Claude ? 1024 * 1024 : 16 * 1024 * 1024);
    connect(reply, &QNetworkReply::finished, this, [this, reply, key, after, collected = std::move(collected)]() mutable {
        const int limit = provider_ == Provider::Claude ? 1024 * 1024 : 16 * 1024 * 1024;
        const bool ok = reply->error() == QNetworkReply::NoError
            && reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 200 && reply->bytesAvailable() <= limit;
        const auto bytes = reply->read(limit + 1); reply->deleteLater(); modelsReply_ = nullptr;
        if (key != key_) { if (configured()) refreshModels(); return; }
        const auto document = QJsonDocument::fromJson(bytes);
        if (!ok || !document.isObject() || bytes.size() > limit || !document.object().value("data").isArray()) {
            models_.clear(); status_ = provider_ == Provider::Claude ? "Claude model catalog is unavailable. Check the API key and workspace."
                : "OpenRouter model catalog is unavailable. Check the API key and credits.";
            emit changed(); return;
        }
        const auto entries = document.object().value("data").toArray();
        for (const auto& value : entries) {
            const auto item = value.toObject();
            const auto id = item.value("id").toString();
            if (id.isEmpty()) continue;
            if (provider_ == Provider::OpenRouter) {
                const auto outputs = item.value("architecture").toObject().value("output_modalities").toArray();
                if (!outputs.isEmpty() && !outputs.contains("text")) continue;
            }
            collected.append(QVariantMap{{"slug", id}, {"label", item.value(provider_ == Provider::Claude ? "display_name" : "name").toString(id)}});
        }
        if (provider_ == Provider::Claude && document.object().value("has_more").toBool()) {
            const auto next = document.object().value("last_id").toString();
            if (next.isEmpty() || next == after || collected.size() > 2000) {
                models_.clear(); status_ = "Claude model catalog pagination failed."; emit changed(); return;
            }
            loadModels(next, std::move(collected)); return;
        }
        models_ = collected;
        status_ = models_.isEmpty() ? "No text-capable models are available for this provider." : QString{};
        emit changed();
    });
}
void ApiAiClient::cancelAnalysis() { if (analysisReply_) analysisReply_->abort(); }
void ApiAiClient::analyze(const QString& model, const QString& instructions, const QString& input,
    std::function<void(QString, QString)> done) {
    if (!configured() || analysisReply_) { done({}, "AI provider is not configured or is busy."); return; }
    bool listed = false;
    for (const auto& choice : models_) if (choice.toMap().value("slug") == model) listed = true;
    if (!listed) { done({}, "Selected AI model is unavailable for this provider."); return; }
    const QUrl url = base_.resolved(QUrl(provider_ == Provider::Claude ? "/v1/messages" : "/api/v1/chat/completions"));
    QNetworkRequest request(url);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setRawHeader("Authorization", "Bearer " + key_.toUtf8());
    request.setTransferTimeout(180000);
    QJsonObject payload{{"model", model}, {"max_tokens", 1024}};
    if (provider_ == Provider::Claude) {
        request.setRawHeader("anthropic-version", "2026-10-21");
        payload.insert("system", instructions);
        payload.insert("messages", QJsonArray{QJsonObject{{"role", "user"}, {"content", input}}});
    } else payload.insert("messages", QJsonArray{QJsonObject{{"role", "system"}, {"content", instructions}},
        QJsonObject{{"role", "user"}, {"content", input}}});
    const auto key = key_;
    analysisReply_ = network_.post(request, QJsonDocument(payload).toJson(QJsonDocument::Compact));
    emit analysisSent(identity());
    auto* reply = analysisReply_.data();
    limitReply(reply, 65536);
    connect(reply, &QNetworkReply::finished, this, [this, reply, key, done = std::move(done)] {
        const bool ok = reply->error() == QNetworkReply::NoError
            && reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 200 && reply->bytesAvailable() <= 65536;
        const auto bytes = reply->read(65537); reply->deleteLater(); analysisReply_ = nullptr;
        if (key != key_) { done({}, "AI provider credentials changed during analysis."); return; }
        if (!ok || bytes.size() > 65536) { done({}, provider_ == Provider::Claude ? "Claude analysis failed. Check API access and retry later."
            : "OpenRouter analysis failed. Check API access and credits."); return; }
        const auto document = QJsonDocument::fromJson(bytes);
        if (!document.isObject()) { done({}, "AI provider returned an invalid response."); return; }
        QString answer;
        if (provider_ == Provider::Claude) {
            const auto response = document.object();
            if (response.value("stop_reason") == "max_tokens") { done({}, "Claude output limit was reached. Choose another model or retry."); return; }
            for (const auto& part : response.value("content").toArray()) if (part.toObject().value("type") == "text")
                answer += part.toObject().value("text").toString();
        } else {
            const auto choices = document.object().value("choices").toArray();
            const auto choice = choices.isEmpty() ? QJsonObject{} : choices.first().toObject();
            answer = choice.value("message").toObject().value("content").toString();
            if (choice.value("finish_reason") == "length") { done({}, "OpenRouter output limit was reached. Choose another model or retry."); return; }
        }
        done(answer, answer.isEmpty() ? "AI provider returned no text answer." : QString{});
    });
}
} // namespace podlord
