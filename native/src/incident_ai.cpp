#include "incident_ai.h"
#include "session_store.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QLockFile>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSaveFile>
#include <algorithm>

namespace podlord {
constexpr qint64 cacheLifetimeSeconds = 90LL * 24 * 60 * 60;
bool IncidentAi::supported() const {
#if defined(Q_OS_ANDROID) || defined(Q_OS_IOS)
    return false;
#else
    return true;
#endif
}
IncidentAi::IncidentAi(QString profile, QUrl endpoint, QObject* parent)
    : QObject(parent), profile_(std::move(profile)), endpoint_(std::move(endpoint)) {
    dispatchTimer_.setSingleShot(true);
    connect(&dispatchTimer_, &QTimer::timeout, this, &IncidentAi::dispatch);
    const auto path = QDir(profile_).filePath("incident-ai.json");
    QFile file(path);
    if (QFileInfo(path).isSymLink() || (file.exists() && (!file.open(QIODevice::ReadOnly) || file.size() > 2 * 1024 * 1024))) {
        writable_ = false;
        status_ = "AI settings could not be read. Automatic analysis is off.";
        return;
    }
    if (file.isOpen()) {
        savedBytes_ = file.readAll();
        QJsonParseError parseError;
        const auto document = QJsonDocument::fromJson(savedBytes_, &parseError);
        const auto root = document.object();
        if (parseError.error != QJsonParseError::NoError || !document.isObject() || root["version"] != 1
            || !root["enabledContexts"].isArray() || !root["records"].isArray() || !root["model"].isString()) {
            writable_ = false;
            status_ = "AI settings are invalid. Automatic analysis is off.";
            return;
        }
        for (const auto& value : root["enabledContexts"].toArray()) if (value.isString() && !value.toString().isEmpty()) enabled_.insert(value.toString());
        model_ = root["model"].toString();
        if (root["reasoning"].isString()) reasoning_ = root["reasoning"].toString();
        const auto expiration = QDateTime::currentDateTimeUtc().addSecs(-cacheLifetimeSeconds);
        bool expired = false;
        for (const auto& value : root["records"].toArray()) {
            const auto entry = value.toObject();
            const auto key = entry["key"].toString();
            const auto answer = entry["answer"].toObject();
            const auto created = QDateTime::fromString(entry["created"].toString(), Qt::ISODateWithMs);
            if (key.size() == 64 && !answer["summary"].toString().isEmpty() && !answer["action"].toString().isEmpty()
                && created.isValid()) {
                if (created > expiration) records_.insert(key, {answer, created});
                else expired = true;
            }
        }
        if (expired) save();
    }
    if (endpoint_.scheme() != "http" || !QStringList{"127.0.0.1", "localhost", "::1"}.contains(endpoint_.host())
        || endpoint_.path() != "" || !endpoint_.userInfo().isEmpty()) {
        writable_ = false; enabled_.clear(); status_ = "Ollama endpoint must be local loopback."; return;
    }
    if (supported()) refreshModels();
}
bool IncidentAi::enabled(const QString& context) const { return supported() && !context.isEmpty() && enabled_.contains(context); }
bool IncidentAi::save() {
    if (!writable_) return false;
    if (profileFailure(profile_)) { status_ = "Cannot save private AI settings."; emit changed(); return false; }
    if (!QDir().mkpath(profile_)) { status_ = "Cannot create private AI settings directory."; emit changed(); return false; }
    const auto path = QDir(profile_).filePath("incident-ai.json");
    if (QFileInfo(path).isSymLink()) { status_ = "AI settings path must not be a symbolic link."; emit changed(); return false; }
    QLockFile lock(path + ".lock"); lock.setStaleLockTime(0);
    if (!lock.tryLock(0)) { status_ = "AI settings are being changed elsewhere. Retry after restarting PodLord."; emit changed(); return false; }
    QFile current(path);
    if (current.exists()) {
        if (!current.open(QIODevice::ReadOnly) || current.size() > 2 * 1024 * 1024 || current.readAll() != savedBytes_) {
            status_ = "AI settings changed in another process. Restart PodLord before editing them."; emit changed(); return false;
        }
    } else if (!savedBytes_.isEmpty()) {
        status_ = "AI settings were removed in another process. Restart PodLord before editing them."; emit changed(); return false;
    }
    QJsonArray enabled, records;
    QStringList contexts = enabled_.values(); contexts.sort();
    for (const auto& context : contexts) enabled.append(context);
    QList<QPair<QString, Record>> ordered;
    for (auto entry = records_.cbegin(); entry != records_.cend(); ++entry) ordered.append({entry.key(), entry.value()});
    std::sort(ordered.begin(), ordered.end(), [](const auto& a, const auto& b) { return a.second.created > b.second.created; });
    for (const auto& [key, record] : ordered.mid(0, 500))
        records.append(QJsonObject{{"key", key}, {"answer", record.answer}, {"created", record.created.toUTC().toString(Qt::ISODateWithMs)}});
    QSaveFile file(path); file.setDirectWriteFallback(false);
    const auto data = QJsonDocument(QJsonObject{{"version", 1}, {"enabledContexts", enabled}, {"model", model_},
        {"reasoning", reasoning_}, {"records", records}}).toJson();
    if (!file.open(QIODevice::WriteOnly) || !file.setPermissions(QFile::ReadOwner | QFile::WriteOwner)
        || file.write(data) != data.size() || !file.commit()) {
        status_ = "Cannot save private AI settings."; emit changed(); return false;
    }
    savedBytes_ = data;
    while (records_.size() > 500) records_.remove(ordered.takeLast().first);
    return true;
}
bool IncidentAi::setEnabled(const QString& context, bool value) {
    if (!supported() || context.isEmpty() || !writable_) return false;
    const bool previous = enabled_.contains(context);
    if (previous == value) return true;
    if (value) enabled_.insert(context); else enabled_.remove(context);
    if (!save()) { if (previous) enabled_.insert(context); else enabled_.remove(context); return false; }
    if (!value) stopObserving(context);
    else if (models_.isEmpty()) refreshModels();
    emit changed();
    return true;
}
void IncidentAi::stopObserving(const QString& context) {
    contextStatus_.remove(context);
    if (inFlightContext_ == context && activeReply_) activeReply_->abort();
    snapshots_.remove(context);
    activeKeys_.remove(context);
    for (auto item = visible_.begin(); item != visible_.end();) item = item->context == context ? visible_.erase(item) : ++item;
    QQueue<Incident> retained;
    while (!queue_.isEmpty()) { const auto incident = queue_.dequeue(); if (incident.context == context) queued_.remove(incident.key); else retained.enqueue(incident); }
    queue_ = std::move(retained);
}
void IncidentAi::retainContexts(const QSet<QString>& contexts) {
    bool changedState = false;
    const auto observed = snapshots_.keys();
    for (const auto& context : observed) if (!contexts.contains(context)) { stopObserving(context); changedState = true; }
    if (changedState) emit changed();
}
bool IncidentAi::setModel(const QString& model) {
    if (!writable_ || !models_.contains(model)) return false;
    if (model_ == model) return true;
    const auto previous = model_;
    model_ = model;
    const auto previousReasoning = reasoning_;
    reasoning_ = "auto";
    if (!save()) { model_ = previous; reasoning_ = previousReasoning; return false; }
    if (activeReply_) activeReply_->abort();
    modelReady_ = false;
    queue_.clear(); queued_.clear(); visible_.clear();
    contextStatus_.clear();
    refreshReasoning();
    emit changed();
    return true;
}
bool IncidentAi::setReasoning(const QString& reasoning) {
    if (!writable_ || !reasoningOptions_.contains(reasoning)) return false;
    if (reasoning_ == reasoning) return true;
    const auto previous = reasoning_;
    reasoning_ = reasoning;
    if (!save()) { reasoning_ = previous; return false; }
    if (activeReply_) activeReply_->abort();
    if (reasoning_ == "auto") thinkValue_ = reasoningOptions_.contains("medium") ? QJsonValue("medium") : QJsonValue(QJsonValue::Undefined);
    else if (reasoning_ == "on") thinkValue_ = QJsonValue(true);
    else if (reasoning_ == "off") thinkValue_ = QJsonValue(false);
    else thinkValue_ = QJsonValue(reasoning_);
    queue_.clear(); queued_.clear(); visible_.clear();
    contextStatus_.clear();
    const auto snapshots = snapshots_;
    for (auto item = snapshots.cbegin(); item != snapshots.cend(); ++item) observe(item.key(), item.value().first, item.value().second);
    emit changed();
    return true;
}
void IncidentAi::refreshModels() {
    if (!supported() || modelRefreshRunning_) return;
    modelRefreshRunning_ = true;
    QNetworkRequest request(endpoint_.resolved(QUrl("/api/tags")));
    request.setTransferTimeout(5000);
    auto* reply = network_.get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        modelRefreshRunning_ = false;
        const auto bytes = reply->readAll();
        const auto error = reply->error();
        reply->deleteLater();
        if (error != QNetworkReply::NoError || bytes.size() > 256 * 1024) {
            models_.clear(); modelReady_ = false; status_ = "The local Ollama service is unavailable."; emit changed(); return;
        }
        const auto root = QJsonDocument::fromJson(bytes).object();
        QStringList available;
        for (const auto& value : root["models"].toArray()) {
            const auto entry = value.toObject();
            const auto name = entry["name"].toString();
            if (!name.isEmpty() && !name.endsWith(":cloud")) available.append(name);
        }
        available.removeDuplicates(); available.sort();
        models_ = available;
        if (models_.isEmpty()) { modelReady_ = false; status_ = "No local Ollama model installed. Install one with ollama pull <model>."; }
        else {
            serviceRetryAfter_ = {};
            retryAfter_.clear();
            contextStatus_.clear();
            if (!models_.contains(model_)) {
                const auto previous = model_;
                model_.clear();
                if (!previous.isEmpty() && !save()) { model_ = previous; modelReady_ = false; emit changed(); return; }
            }
            if (model_.isEmpty()) {
                modelReady_ = false;
                reasoningOptions_ = {"auto"};
                thinkValue_ = QJsonValue(QJsonValue::Undefined);
                status_ = "Choose an installed Ollama chat model to start analysis.";
            } else {
                status_.clear();
                refreshReasoning();
            }
        }
        emit changed();
    });
}
void IncidentAi::refreshReasoning() {
    modelReady_ = false;
    queue_.clear(); queued_.clear();
    const auto selected = model_;
    QNetworkRequest request(endpoint_.resolved(QUrl("/api/show")));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setTransferTimeout(5000);
    auto* reply = network_.post(request, QJsonDocument(QJsonObject{{"model", selected}}).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [this, reply, selected] {
        const auto bytes = reply->readAll();
        const bool ok = reply->error() == QNetworkReply::NoError && reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 200
            && bytes.size() <= 256 * 1024;
        reply->deleteLater();
        if (selected != model_) return;
        const auto root = QJsonDocument::fromJson(bytes).object();
        if (!ok || !root["capabilities"].toArray().contains("completion")) {
            reasoningOptions_ = {"auto"}; thinkValue_ = QJsonValue(QJsonValue::Undefined);
            status_ = "Selected Ollama model cannot generate chat or its capabilities are unavailable.";
            emit changed(); return;
        }
        reasoningOptions_ = {"auto"};
        for (const auto& value : root["thinking"].toObject()["values"].toArray()) {
            if (value.isBool()) reasoningOptions_.append(value.toBool() ? "on" : "off");
            else if (value.isString() && value.toString().size() <= 32) reasoningOptions_.append(value.toString());
        }
        reasoningOptions_.removeDuplicates();
        if (!reasoningOptions_.contains(reasoning_)) {
            const auto previous = reasoning_;
            reasoning_ = "auto";
            if (!save()) { reasoning_ = previous; emit changed(); return; }
        }
        if (reasoning_ == "auto") thinkValue_ = reasoningOptions_.contains("medium") ? QJsonValue("medium") : QJsonValue(QJsonValue::Undefined);
        else if (reasoning_ == "on") thinkValue_ = QJsonValue(true);
        else if (reasoning_ == "off") thinkValue_ = QJsonValue(false);
        else thinkValue_ = QJsonValue(reasoning_);
        modelReady_ = true; status_.clear();
        const auto snapshots = snapshots_;
        for (auto item = snapshots.cbegin(); item != snapshots.cend(); ++item) observe(item.key(), item.value().first, item.value().second);
        emit changed();
    });
}
void IncidentAi::observe(const QString& context, const QJsonArray& rows, const QString& language) {
    if (!enabled(context)) return;
    const auto now = QDateTime::currentDateTimeUtc();
    bool expired = false;
    for (auto item = records_.begin(); item != records_.end();) {
        if (item->created.secsTo(now) >= cacheLifetimeSeconds) { item = records_.erase(item); expired = true; }
        else ++item;
    }
    if (expired) save();
    for (auto item = retryAfter_.begin(); item != retryAfter_.end();) item = item.value() <= now ? retryAfter_.erase(item) : ++item;
    QJsonArray safeRows;
    for (const auto& value : rows) {
        const auto row = value.toObject();
        QJsonObject safe;
        for (const auto& field : {"uid", "kind", "name", "namespace", "path", "owners", "owner", "issue", "status", "problemSeverity",
                                  "node", "volumeClaims", "boundVolume",
                                  "kubernetesEvent", "eventType", "eventReason", "eventTargetUid"})
            if (row.contains(field)) safe.insert(field, row.value(field));
        safeRows.append(safe);
    }
    snapshots_[context] = {safeRows, language};
    if (!modelReady_) return;
    for (auto item = visible_.begin(); item != visible_.end();) item = item->context == context ? visible_.erase(item) : ++item;
    QSet<QString> activeKeys;
    QMap<QString, QJsonObject> byUid;
    QMap<QString, QJsonObject> nodes, claims, volumes;
    for (const auto& value : safeRows) {
        const auto row = value.toObject();
        if (!row["uid"].toString().isEmpty()) byUid.insert(row["uid"].toString(), row);
        const auto kind = row["kind"].toString(), name = row["name"].toString();
        if (name.isEmpty()) continue;
        if (kind == "Node") nodes.insert(name, row);
        else if (kind == "PersistentVolumeClaim") claims.insert(row["namespace"].toString() + '\n' + name, row);
        else if (kind == "PersistentVolume") volumes.insert(name, row);
    }
    for (const auto& value : safeRows) {
        const auto source = value.toObject();
        const bool event = source["kubernetesEvent"].toBool();
        if (source["problemSeverity"].toInt() == 0 || (event && source["eventType"] != "Warning")) continue;
        const auto row = event ? byUid.value(source["eventTargetUid"].toString()) : source;
        if (row.isEmpty() || !QStringList{"Pod", "Deployment", "ReplicaSet", "StatefulSet", "Node", "PersistentVolumeClaim"}.contains(row["kind"].toString())) continue;
        QString code;
        static const QStringList recognized{"CrashLoopBackOff", "ImagePullBackOff", "ErrImagePull", "InvalidImageName",
            "CreateContainerConfigError", "CreateContainerError", "OOMKilled", "Evicted", "Unschedulable",
            "ProgressDeadlineExceeded", "NotReady"};
        static const QStringList recognizedEvents{"FailedScheduling", "FailedMount", "FailedAttachVolume", "FailedCreatePodSandBox"};
        if (event) { if (recognizedEvents.contains(source["eventReason"].toString())) code = source["eventReason"].toString(); }
        else {
            const auto issues = row["issue"].toString().split(',', Qt::SkipEmptyParts);
            for (const auto& issue : issues) if (recognized.contains(issue.trimmed())) { code = issue.trimmed(); break; }
            if (code.isEmpty() && recognized.contains(row["status"].toString())) code = row["status"].toString();
        }
        if (code.isEmpty()) continue;
        auto root = row;
        QSet<QString> visited;
        for (int depth = 0; depth < 4; ++depth) {
            const auto owners = root["owners"].toArray();
            if (owners.isEmpty()) break;
            const auto uid = owners.first().toObject()["uid"].toString();
            if (uid.isEmpty() || visited.contains(uid)) break;
            if (!byUid.contains(uid)) {
                const auto owner = root["owner"].toString().section(", ", 0, 0);
                if (owner.contains('/')) root = QJsonObject{{"kind", owner.section('/', 0, 0)}, {"name", owner.section('/', 1)}};
                break;
            }
            visited.insert(uid); root = byUid.value(uid);
        }
        const auto nameSpace = row["namespace"].toString();
        const auto group = root["kind"].toString() + '/' + root["name"].toString();
        QSet<QString> relatedSignals;
        if (row["kind"] == "Pod") {
            const auto node = nodes.value(row["node"].toString());
            const auto nodeStatus = node["status"].toString();
            if (nodeStatus == "Ready" || nodeStatus == "NotReady") relatedSignals.insert("Assigned Node: " + nodeStatus);
            for (const auto& value : row["volumeClaims"].toArray()) {
                const auto claim = claims.value(nameSpace + '\n' + value.toString());
                const auto claimStatus = claim["status"].toString();
                if (QStringList{"Pending", "Lost", "Bound"}.contains(claimStatus)) relatedSignals.insert("Referenced claim: " + claimStatus);
                const auto volume = volumes.value(claim["boundVolume"].toString());
                const auto volumeStatus = volume["status"].toString();
                if (QStringList{"Pending", "Available", "Bound", "Released", "Failed"}.contains(volumeStatus))
                    relatedSignals.insert("Bound volume: " + volumeStatus);
            }
        } else if (row["kind"] == "PersistentVolumeClaim") {
            const auto volume = volumes.value(row["boundVolume"].toString());
            const auto volumeStatus = volume["status"].toString();
            if (QStringList{"Pending", "Available", "Bound", "Released", "Failed"}.contains(volumeStatus))
                relatedSignals.insert("Bound volume: " + volumeStatus);
        }
        QStringList related = relatedSignals.values(); related.sort();
        const auto thinkSignature = thinkValue_.isUndefined() ? QString("default") : thinkValue_.toVariant().toString();
        const auto identity = context + '\n' + nameSpace + '\n' + group + '\n' + code + '\n' + related.join("\n")
            + '\n' + model_ + '\n' + thinkSignature + '\n' + language + "\n2";
        const auto key = QString::fromLatin1(QCryptographicHash::hash(identity.toUtf8(), QCryptographicHash::Sha256).toHex());
        activeKeys.insert(key);
        const auto kind = row["kind"].toString();
        const auto docs = code == "OOMKilled" ? "https://kubernetes.io/docs/tasks/configure-pod-container/assign-memory-resource/"
            : code == "Unschedulable" || code == "FailedScheduling" ? "https://kubernetes.io/docs/concepts/scheduling-eviction/assign-pod-node/"
            : code == "FailedMount" || code == "FailedAttachVolume" ? "https://kubernetes.io/docs/concepts/storage/volumes/"
            : code == "ImagePullBackOff" || code == "ErrImagePull" || code == "InvalidImageName" ? "https://kubernetes.io/docs/concepts/containers/images/"
            : code == "ProgressDeadlineExceeded" ? "https://kubernetes.io/docs/concepts/workloads/controllers/deployment/"
            : "https://kubernetes.io/docs/tasks/debug/debug-application/debug-pods/";
        const Incident incident{key, context, code, kind, row["name"].toString(), nameSpace,
            row["path"].toString(), language, model_, reasoning_, thinkSignature, docs, related};
        if (!visible_.contains(context + '\n' + incident.path)) visible_.insert(context + '\n' + incident.path, incident);
        if (records_.contains(key) || queued_.contains(key) || inFlight_ == key || retryAfter_.value(key) > QDateTime::currentDateTimeUtc()) continue;
        if (queue_.size() >= 32) continue;
        queue_.enqueue(incident); queued_.insert(key);
    }
    activeKeys_[context] = activeKeys;
    QQueue<Incident> retained;
    while (!queue_.isEmpty()) {
        const auto incident = queue_.dequeue();
        if (incident.context == context && !activeKeys.contains(incident.key)) queued_.remove(incident.key);
        else retained.enqueue(incident);
    }
    queue_ = std::move(retained);
    if (inFlightContext_ == context && !activeKeys.contains(inFlight_) && activeReply_) {
        contextStatus_.remove(context);
        activeReply_->abort();
    }
    dispatch();
    emit changed();
}
bool IncidentAi::refresh(const QString& context, const QString& path) {
    if (!enabled(context) || !modelReady_) return false;
    const auto found = visible_.constFind(context + '\n' + path);
    if (found == visible_.cend() || !activeKeys_.value(context).contains(found->key)) return false;
    const auto incident = *found;
    const auto previous = records_.constFind(incident.key);
    if (previous != records_.cend()) {
        const auto record = *previous;
        records_.remove(incident.key);
        if (!save()) { records_.insert(incident.key, record); return false; }
    }
    retryAfter_.remove(incident.key);
    if (inFlight_ == incident.key) { emit changed(); return true; }
    QQueue<Incident> retained;
    while (!queue_.isEmpty()) {
        const auto next = queue_.dequeue();
        if (next.key == incident.key) queued_.remove(next.key);
        else retained.enqueue(next);
    }
    queue_ = std::move(retained);
    Incident requested = incident; requested.manual = true;
    queue_.prepend(requested); queued_.insert(requested.key);
    dispatch(); emit changed();
    return true;
}
void IncidentAi::dispatch() {
    if (!inFlight_.isEmpty() || queue_.isEmpty() || !modelReady_) return;
    const auto now = QDateTime::currentDateTimeUtc();
    const bool manual = queue_.head().manual;
    if (!manual && serviceRetryAfter_ > now) { dispatchTimer_.start(int(now.msecsTo(serviceRetryAfter_))); return; }
    if (!manual && lastRequest_.isValid() && lastRequest_.msecsTo(now) < 30000) {
        dispatchTimer_.start(int(30000 - lastRequest_.msecsTo(now))); return;
    }
    Incident incident;
    bool found = false;
    QQueue<Incident> deferred;
    qint64 waitMs = 0;
    while (!queue_.isEmpty()) {
        const auto next = queue_.dequeue(); queued_.remove(next.key);
        if (!enabled(next.context) || records_.contains(next.key)) continue;
        auto& recent = recent_[next.context];
        recent.erase(std::remove_if(recent.begin(), recent.end(), [now](const auto& at) { return at.secsTo(now) >= 3600; }), recent.end());
        if (!next.manual && recent.size() >= 5) {
            deferred.enqueue(next); queued_.insert(next.key);
            const auto remaining = 3600000 - recent.first().msecsTo(now);
            waitMs = waitMs == 0 ? remaining : std::min(waitMs, remaining);
            continue;
        }
        incident = next; recent.append(now); found = true; break;
    }
    while (!deferred.isEmpty()) queue_.enqueue(deferred.dequeue());
    if (!found) { if (waitMs > 0) dispatchTimer_.start(int(waitMs)); return; }
    inFlight_ = incident.key; inFlightContext_ = incident.context; lastRequest_ = now;
    contextStatus_[incident.context] = "Analyzing a cluster problem with local Ollama…"; emit changed();
    const QString system = "You explain Kubernetes incidents to a junior developer. The input contains a recognized problem code, resource kind and optional related status labels. "
        "Related statuses are matched by local Kubernetes references, but do not prove causality. "
        "Give one short factual explanation and one practical next step in the requested language. State uncertainty. "
        "Do not invent observed facts, links, commands, secrets, logs, or repairs. Return only a valid JSON object with exactly two string fields: summary and action. No Markdown fences.";
    QJsonObject payload{{"model", model_}, {"stream", false},
        {"messages", QJsonArray{QJsonObject{{"role", "system"}, {"content", system}},
            QJsonObject{{"role", "user"}, {"content", "Language: " + incident.language + "\nResource kind: " + incident.kind
                + "\nObserved problem code: " + incident.code + "\nRelated observed statuses: " + incident.related.join("; ")}}}}};
    if (!thinkValue_.isUndefined()) payload.insert("think", thinkValue_);
    QNetworkRequest request(endpoint_.resolved(QUrl("/api/chat")));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setTransferTimeout(180000);
    auto* reply = network_.post(request, QJsonDocument(payload).toJson(QJsonDocument::Compact));
    activeReply_ = reply;
    connect(reply, &QNetworkReply::finished, this, [this, reply, incident] {
        const auto bytes = reply->readAll();
        const auto ok = reply->error() == QNetworkReply::NoError && reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 200
            && bytes.size() <= 65536;
        reply->deleteLater();
        activeReply_ = nullptr; inFlight_.clear(); inFlightContext_.clear();
        const auto currentThink = thinkValue_.isUndefined() ? QString("default") : thinkValue_.toVariant().toString();
        if (!enabled(incident.context) || !activeKeys_.value(incident.context).contains(incident.key)
            || model_ != incident.model || reasoning_ != incident.reasoning || currentThink != incident.thinkSignature) {
            dispatch(); return;
        }
        const auto response = QJsonDocument::fromJson(bytes).object();
        const auto answer = QJsonDocument::fromJson(response["message"].toObject()["content"].toString().toUtf8()).object();
        const auto summary = answer["summary"].toString().trimmed();
        const auto action = answer["action"].toString().trimmed();
        if (ok && !summary.isEmpty() && summary.size() <= 600 && !action.isEmpty() && action.size() <= 600) {
            records_.insert(incident.key, {QJsonObject{{"summary", summary}, {"action", action}}, QDateTime::currentDateTimeUtc()});
            contextStatus_.remove(incident.context); serviceRetryAfter_ = {}; save();
        } else {
            retryAfter_.insert(incident.key, QDateTime::currentDateTimeUtc().addSecs(900));
            serviceRetryAfter_ = QDateTime::currentDateTimeUtc().addSecs(900);
            contextStatus_[incident.context] = "Ollama analysis failed. Retry after 15 minutes or refresh the model list.";
        }
        emit changed(); dispatch();
    });
}
QVariantMap IncidentAi::result(const QString& context, const QString& path) const {
    if (!enabled(context)) return {};
    const auto found = visible_.constFind(context + '\n' + path);
    if (found == visible_.cend()) return {};
    const auto record = records_.constFind(found->key);
    if (record == records_.cend() || record->created.secsTo(QDateTime::currentDateTimeUtc()) >= cacheLifetimeSeconds)
        return {{"code", found->code}, {"related", found->related}, {"pending", queued_.contains(found->key) || inFlight_ == found->key}};
    const auto quote = [](QString value) { value.replace(QChar(39), "'\\''"); return "'" + value + "'"; };
    QString command = "kubectl ";
    if (!found->nameSpace.isEmpty()) command += "-n " + quote(found->nameSpace) + ' ';
    command += "describe " + quote(found->kind.toLower()) + ' ' + quote(found->name);
    return {{"code", found->code}, {"related", found->related}, {"summary", record->answer["summary"].toString()},
        {"action", record->answer["action"].toString()}, {"documentation", found->documentation},
        {"command", command}, {"created", record->created.toLocalTime().toString(Qt::ISODate)}};
}
} // namespace podlord
