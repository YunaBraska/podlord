#include "api_ai_client.h"
#include "session_store.h"
#include <QCryptographicHash>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLockFile>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrlQuery>
#include <algorithm>

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
    if (analysisReply_) analysisReply_->abort();
    if (modelsReply_) modelsReply_->abort();
    models_.clear(); status_.clear(); emit changed(); refreshModels();
    return true;
}
void ApiAiClient::clearKey() {
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
