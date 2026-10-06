#include "resource_client.h"
#include "container_terminal.h"
#include "resource_metrics.h"
#include "port_forward_transport.h"
#include <QJsonDocument>
#include <QNetworkReply>
#include <QNetworkProxy>
#include <QRegularExpression>
#include <QUrlQuery>
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <memory>

namespace podlord {
namespace {
struct LogPayload final {
    QByteArray bytes;
    bool limited = false, discarding = false;
    bool append(QByteArray chunk, qint64 budget) {
        if (discarding) {
            const auto end = chunk.indexOf('\n');
            if (end < 0) return true;
            chunk.remove(0, end + 1); discarding = false;
        }
        bytes.append(chunk);
        if (bytes.size() > budget) {
            limited = true;
            const auto end = bytes.indexOf('\n', bytes.size() - budget - 1);
            if (end < 0) { bytes.clear(); discarding = true; }
            else bytes.remove(0, end + 1);
        }
        return true;
    }
};
bool segment(const QString& value) {
    static const QRegularExpression allowed("^[a-zA-Z0-9][a-zA-Z0-9._-]*$");
    return allowed.match(value).hasMatch();
}
bool versionPath(const QString& value) {
    const auto parts = value.split('/');
    return (parts.size() == 1 || parts.size() == 2)
        && std::all_of(parts.cbegin(), parts.cend(), segment);
}
bool resourceName(const QString& value) {
    return !value.isEmpty() && value != "." && value != ".." && !value.contains('/')
        && std::none_of(value.cbegin(), value.cend(), [](QChar c) { return c.category() == QChar::Other_Control; });
}
bool validValues(const QJsonObject& document) {
    const auto kind = document["kind"].toString();
    if (!isCoreValueResource(document)) return true;
    static const QRegularExpression key("^[a-zA-Z0-9._-]+$");
    const QStringList fields = kind == "Secret" ? QStringList{"data", "stringData"} : QStringList{"data", "binaryData"};
    for (const auto& field : fields) {
        const auto value = document.value(field);
        if (value.isUndefined()) continue;
        if (!value.isObject()) return false;
        const auto values = value.toObject();
        for (auto entry = values.constBegin(); entry != values.constEnd(); ++entry) {
            if (!key.match(entry.key()).hasMatch() || !entry.value().isString()) return false;
            if ((kind == "Secret" && field == "data") || field == "binaryData")
                if (!QByteArray::fromBase64Encoding(entry.value().toString().toLatin1(), QByteArray::AbortOnBase64DecodingErrors)) return false;
        }
    }
    return true;
}
QString apiVersion(const QString& path) { return path.startsWith("/api/") ? path.section('/', 2, 2) : path.section('/', 2, 3); }
QJsonObject summary(const QJsonObject& object, const QString& path, const QString& kind) {
    const auto metadata = object["metadata"].toObject();
    const auto state = object["status"].toObject();
    const auto spec = object["spec"].toObject();
    const auto version = apiVersion(path);
    QString status = state["phase"].toString(state["type"].toString(spec["type"].toString("Observed")));
    if (version=="v1" && kind=="Pod") status=metadata["deletionTimestamp"].toString().isEmpty() ? state["phase"].toString("Unknown") : "Terminating";
    if (version=="batch/v1" && kind=="Job") status=state["failed"].toInt()>0 ? "Failed" : state["succeeded"].toInt()>0 ? "Complete" : "Running";
    if (version=="batch/v1" && kind=="CronJob" && spec["suspend"].toBool()) status="Suspended";
    if (version == "apps/v1" && kind == "Deployment") status = state["availableReplicas"].toInteger() >= spec["replicas"].toInteger(1) ? "Available" : "Unavailable";
    else if (version == "apps/v1" && kind == "ReplicaSet") {
        const auto desired = spec["replicas"].toInteger(1), ready = state["readyReplicas"].toInteger();
        status = desired == 0 ? "ScaledZero" : ready >= desired ? "Available" : ready > 0 ? "Progressing" : "Unavailable";
    } else if (version == "v1" && kind == "Node") {
        status = "NotReady";
        for (const auto& value : state["conditions"].toArray()) if (value.toObject()["type"] == "Ready") {
            status = value.toObject()["status"] == "True" ? "Ready" : "NotReady";
            break;
        }
    }
    QJsonObject imageSpec;
    if (version == "v1" && kind == "Pod") imageSpec = spec;
    else if (version == "apps/v1" && (kind == "Deployment" || kind == "ReplicaSet" || kind == "StatefulSet" || kind == "DaemonSet")) imageSpec = spec["template"].toObject()["spec"].toObject();
    else if (version == "batch/v1" && kind == "Job") imageSpec = spec["template"].toObject()["spec"].toObject();
    else if (version == "batch/v1" && kind == "CronJob") imageSpec = spec["jobTemplate"].toObject()["spec"].toObject()["template"].toObject()["spec"].toObject();
    QStringList images;
    for (const auto& item : imageSpec["containers"].toArray()) {
        const auto image = item.toObject()["image"].toString();
        if (!image.isEmpty()) images.append(image);
    }
    QJsonArray containers;
    for (const auto& group : {"containers", "initContainers", "ephemeralContainers"})
        for (const auto& item : spec[group].toArray()) {
            const auto name = item.toObject()["name"].toString();
            if (!containers.contains(name)) containers.append(name);
        }
    QJsonArray owners;
    for (const auto& value : metadata["ownerReferences"].toArray()) {
        const auto owner = value.toObject();
        if (!owner["uid"].toString().isEmpty()) owners.append(QJsonObject{{"uid", owner["uid"]}, {"kind", owner["kind"]}, {"apiVersion", owner["apiVersion"]}});
    }
    QJsonObject result{{"path", path}, {"apiVersion", apiVersion(path)}, {"kind", kind}, {"name", metadata["name"]}, {"namespace", metadata["namespace"]},
        {"uid", metadata["uid"]}, {"resourceVersion", metadata["resourceVersion"]},
        {"createdAt", metadata["creationTimestamp"]}, {"status", status},
        {"node", spec["nodeName"]}, {"image", images.join(", ")}, {"containers", containers}, {"owners", owners}};
    QStringList ownerNames;
    for (const auto& value : metadata["ownerReferences"].toArray()) {
        const auto owner=value.toObject(); ownerNames.append(owner["kind"].toString()+"/"+owner["name"].toString());
    }
    result["owner"]=ownerNames.join(", ");
    if (version == "apps/v1" && (kind == "Deployment" || kind == "ReplicaSet" || kind == "StatefulSet")) {
        const auto desired = spec["replicas"].toInteger(1), ready = state["readyReplicas"].toInteger();
        result["ready"] = QString::number(ready)+"/"+QString::number(desired);
        result["readyCount"] = static_cast<double>(ready);
        result["containerCount"] = static_cast<double>(desired);
    }
    QStringList issues;
    const QStringList failureStates{"CrashLoopBackOff", "CreateContainerConfigError", "CreateContainerError", "ErrImagePull", "Error", "Failed", "ImagePullBackOff", "NotReady", "OOMKilled", "Unavailable", "Unknown"};
    if (failureStates.contains(result["status"].toString())) issues.append(result["status"].toString());
    if (version == "v1" && kind == "Pod") {
        qint64 restarts=0; int ready=0; const auto statuses=state["containerStatuses"].toArray();
        for (const auto& group : {"containerStatuses", "initContainerStatuses", "ephemeralContainerStatuses"})
            for (const auto& value : state[group].toArray()) {
                const auto container=value.toObject(); restarts+=container["restartCount"].toInteger();
                if (QString(group)=="containerStatuses" && container["ready"].toBool()) ++ready;
                const auto current=container["state"].toObject();
                const auto waiting=current["waiting"].toObject()["reason"].toString();
                const auto terminated=current["terminated"].toObject();
                if (!waiting.isEmpty() && waiting!="ContainerCreating" && waiting!="PodInitializing") issues.append(waiting);
                if (terminated["exitCode"].toInt()>0) issues.append(terminated["reason"].toString("Failed"));
            }
        if (!statuses.isEmpty()) result["restarts"]=static_cast<double>(restarts);
        if (!spec["containers"].toArray().isEmpty()) result["ready"]=QString::number(ready)+"/"+QString::number(spec["containers"].toArray().size());
        result["readyCount"]=ready;
        result["containerCount"]=spec["containers"].toArray().size();
        if (!issues.isEmpty()) result["status"]=issues.first();
    }
    for (const auto& value : state["conditions"].toArray()) {
        const auto condition=value.toObject(); const auto type=condition["type"].toString();
        if (condition["status"] == "False" && ((version == "v1" && kind == "Node" && type == "Ready") || (version == "apps/v1" && kind == "Deployment" && type == "Available"))) {
            issues.append(condition["reason"].toString("NotReady")); result["status"]="NotReady";
        }
    }
    issues.removeDuplicates(); result["issue"]=issues.join(", "); result["problems"]=!issues.isEmpty();
    const auto references=resourceMetricReferences(object, result);
    for (auto entry=references.begin(); entry!=references.end(); ++entry) result[entry.key()]=entry.value();
    if (kind == "Event" && (version == "v1" || version.startsWith("events.k8s.io/"))) {
        const bool modern = version.startsWith("events.k8s.io/");
        const auto target = object[modern ? "regarding" : "involvedObject"].toObject();
        if (!target["kind"].toString().isEmpty() && !target["name"].toString().isEmpty()) result["owner"] = target["kind"].toString()+"/"+target["name"].toString();
        result["kubernetesEvent"] = true;
        result["eventTargetUid"] = object[modern ? "regarding" : "involvedObject"].toObject()["uid"];
        result["eventTargetName"] = object[modern ? "regarding" : "involvedObject"].toObject()["name"];
        result["eventType"] = object["type"]; result["eventReason"] = object["reason"];
        result["status"] = object["type"].toString("Observed");
        result["eventMessage"] = object[modern ? "note" : "message"];
        const auto series = object["series"].toObject();
        result["eventCount"] = series.contains("count") ? series["count"] : object[modern ? "deprecatedCount" : "count"];
        QString time = series["lastObservedTime"].toString();
        for (const auto& field : {modern ? "deprecatedLastTimestamp" : "lastTimestamp", "eventTime", modern ? "deprecatedFirstTimestamp" : "firstTimestamp"}) {
            if (!time.isEmpty()) break;
            time = object[field].toString();
        }
        result["eventTime"] = time;
    }
    return result;
}
QJsonArray withProblemState(const QJsonArray& snapshot, QDateTime now, QDateTime& next) {
    next = {};
    static const QStringList severeStates{"CrashLoopBackOff", "CreateContainerConfigError", "CreateContainerError", "ErrImagePull", "Error", "Failed", "ImagePullBackOff", "NotReady", "OOMKilled", "Unavailable"};
    const auto deadline = [&](QDateTime at) { if (at > now && (!next.isValid() || at < next)) next = at; };
    QList<double> restarts;
    for (const auto& value : snapshot) {
        const auto row=value.toObject();
        if (row["apiVersion"]=="v1" && row["kind"]=="Pod" && row["status"]=="Running" && row["restarts"].isDouble()) restarts.append(row["restarts"].toDouble());
    }
    std::sort(restarts.begin(), restarts.end());
    const auto percentile = [&](double fraction) {
        const double index=fraction*(restarts.size()-1);
        return std::lerp(restarts[static_cast<qsizetype>(std::floor(index))], restarts[static_cast<qsizetype>(std::ceil(index))], index-std::floor(index));
    };
    const double threshold=restarts.size()<4 ? 3 : std::max(3.0, std::ceil(percentile(.75)+1.5*(percentile(.75)-percentile(.25))));
    QJsonArray result;
    for (const auto& value : snapshot) {
        auto row=value.toObject(); const auto status=row["status"].toString();
        QStringList issues;
        if (!row["issue"].toString().isEmpty()) issues.append(row["issue"].toString());
        if (row["kubernetesEvent"].toBool()) {
            auto at=QDateTime::fromString(row["eventTime"].toString(), Qt::ISODateWithMs);
            if (!at.isValid()) at=QDateTime::fromString(row["createdAt"].toString(), Qt::ISODateWithMs);
            issues.clear();
            if (row["eventType"]=="Warning" && at.isValid()) {
                const auto age=at.msecsTo(now);
                if (age>=0 && age<=1800000) issues.append(row["eventReason"].toString().isEmpty() ? "Warning" : row["eventReason"].toString());
                deadline(at); deadline(at.addMSecs(1800001));
            }
        } else if (status=="Succeeded" || status=="Complete" || status=="Completed") issues.clear();
        else {
            const bool pod=row["apiVersion"]=="v1" && row["kind"]=="Pod";
            const auto created=QDateTime::fromString(row["createdAt"].toString(), Qt::ISODateWithMs);
            const auto age=created.msecsTo(now);
            const bool starting=pod && issues.isEmpty() && created.isValid() && age>=0 && age<=300000 && status!="Warning" && status!="Terminating";
            const bool notReady=!row["ready"].toString().isEmpty() && row["containerCount"].toInt()>row["readyCount"].toInt();
            if (starting && (status=="Pending" || notReady)) deadline(created.addMSecs(300001));
            if (!starting && (status=="Pending" || status=="Warning" || status=="Terminating")) issues.append(status);
            if (!starting && notReady) issues.append(QString("Ready %1/%2").arg(row["readyCount"].toInt()).arg(row["containerCount"].toInt()));
            if (pod && row["restarts"].toDouble()>0 && (status!="Running" || row["restarts"].toDouble()>threshold)) issues.append(status=="Running" ? "Restart outlier" : "Container restarts");
        }
        issues.removeDuplicates(); row["issue"]=issues.join(", "); row["problems"]=!issues.isEmpty();
        static const QStringList activeStates{"Pending", "Progressing", "Running", "Terminating", "Updating", "Warning", "CrashLoopBackOff", "CreateContainerConfigError", "CreateContainerError", "ErrImagePull", "Error", "Failed", "ImagePullBackOff", "NotReady", "OOMKilled", "Unavailable"};
        const auto recent = [&](const QString& field, qint64 ttl) {
            const auto at = QDateTime::fromString(row[field].toString(), Qt::ISODateWithMs);
            if (!at.isValid()) return false;
            deadline(at); deadline(at.addMSecs(ttl + 1));
            const auto age = at.msecsTo(now); return age >= 0 && age <= ttl;
        };
        row["activity"] = row["kubernetesEvent"].toBool()
            ? status != "Observed" && status != "Historical" && recent(row["eventTime"].toString().isEmpty() ? "createdAt" : "eventTime", row["eventType"] == "Warning" ? 1800000 : 300000)
            : activeStates.contains(status, Qt::CaseInsensitive) || recent("changedAt", 900000) || recent("createdAt", 900000);
        const auto problem=row["issue"].toString();
        const bool severe=problem.contains("Crash", Qt::CaseInsensitive) || problem.contains("Error", Qt::CaseInsensitive)
            || problem.contains("Failed", Qt::CaseInsensitive) || problem.contains("Unavailable", Qt::CaseInsensitive)
            || severeStates.contains(status);
        row["problemSeverity"]=issues.isEmpty() ? 0 : severe ? 2 : 1;
        result.append(row);
    }
    return result;
}
} // namespace
bool isCoreValueResource(const QJsonObject& document) {
    const auto kind = document.value("kind");
    return document.value("apiVersion") == "v1" && (kind == "Secret" || kind == "ConfigMap");
}
QVariantList ResourceClient::requestAudit(const QString& id) const {
    QVariantList result;
    const auto live=[&](const Task& task,const QString& status) {
        if (task.id!=id) return;
        result.append(QVariantMap{{"time",QString("-")},{"method",task.read==Read::Apply ? "PATCH" : task.read==Read::Delete ? "DELETE" : "GET"},
            {"path",task.path},{"priority",task.foreground || task.read==Read::Detail || task.change ? "Foreground" : "Background"},
            {"status",status},{"duration",task.started<0 ? QString("-") : QString::number(clock_.elapsed()-task.started)+" ms"},{"outcome",QString("-")}});
    };
    if (running_) live(*running_,"Running");
    for (const auto& task:reads_) live(task,"Running");
    for (const auto& task:queue_) live(task,backoffUntil_>clock_.elapsed() ? "Backoff" : "Queued");
    for (auto it=completedRequests_.crbegin();it!=completedRequests_.crend();++it) {
        auto record=it->toMap(); if (record.take("session").toString()==id) result.append(record);
    }
    return result;
}
ResourceClient::ResourceClient(QObject* parent, std::function<QDateTime()> now) : QObject(parent), now_(std::move(now)) {
    if (!now_) throw std::invalid_argument("Cache clock callback is required.");
    network_.setProxy(QNetworkProxy::NoProxy);
    clock_.start();
    gate_.setSingleShot(true);
    gate_.setTimerType(Qt::PreciseTimer);
    connect(&gate_, &QTimer::timeout, this, &ResourceClient::dispatch);
    expiry_.setSingleShot(true);
    expiry_.setTimerType(Qt::PreciseTimer);
    connect(&expiry_, &QTimer::timeout, this, &ResourceClient::expire);
    sync_.setSingleShot(true);
    connect(&sync_, &QTimer::timeout, this, &ResourceClient::synchronize);
    logSync_.setSingleShot(true);
    logSync_.setTimerType(Qt::PreciseTimer);
    connect(&logSync_, &QTimer::timeout, this, [this] { refreshLogs(false); });
}
ResourceClient::~ResourceClient() {
    enabled_ = false;
    for (const auto& token : terminals_.keys()) stopTerminal(terminals_.value(token).session);
    for (const auto& token : forwards_.keys()) stopPortForward(forwards_.value(token).session, token);
    sync_.stop(); expiry_.stop(); gate_.stop(); logSync_.stop();
    for (auto* reply : network_.findChildren<QNetworkReply*>()) {
        disconnect(reply, nullptr, this, nullptr);
        reply->abort();
    }
}
bool ResourceClient::open(const QString& id, ClusterConnection connection, QStringList namespaces) {
    if (!states_.contains(id)) {
        State state;
        state.lastSync = now_();
        state.namespaces = std::move(namespaces);
        states_.insert(id, std::move(state));
    }
    const auto& previous = states_[id].connection;
    if (previous.authorization != connection.authorization || previous.tls.localCertificate() != connection.tls.localCertificate()
        || previous.credentialReady != connection.credentialReady || previous.expiresAt != connection.expiresAt) {
        ++states_[id].revision; resetConnections_ = true;
    }
    states_[id].connection = std::move(connection);
    for (const auto& state : states_)
        if (state.connection.credentialId == states_[id].connection.credentialId && state.suspended) states_[id].suspended = true;
    states_[id].open = true;
    if (visible_.isEmpty()) visible_ = id;
    if (!states_[id].connection.credentialReady)
        suspend(states_[id].connection.credentialId, "Authentication required. Confirm the credential command before it can run.");
    expire();
    emit changed(id);
    scheduleSync();
    const auto cutoff = now_().addSecs(-25);
    if (!states_[id].collections.isEmpty() && states_[id].failures.isEmpty()
        && std::all_of(states_[id].collections.cbegin(), states_[id].collections.cend(), [&](const auto& collection) { return collection.at >= cutoff; })) return true;
    return refresh(id);
}
bool ResourceClient::close(const QString& id) {
    auto it = states_.find(id);
    if (it == states_.end()) return false;
    it->open = false;
    stopTerminal(id);
    stopSessionForwards(id);
    if (visible_ == id) visible_.clear();
    if (logSession_ == id) hideLogs();
    dropQueued([&](const auto& task) { return task.id == id && task.read != Read::Verify && task.read != Read::VerifyDelete; });
    emit changed(id);
    scheduleSync();
    return true;
}
bool ResourceClient::refresh(const QString& id, bool confirmedAuthentication) {
    expire();
    auto it = states_.find(id);
    if (it == states_.end() || !it->open) return false;
    if (it->suspended && !confirmedAuthentication) return false;
    if (confirmedAuthentication) {
        if (!it->connection.credentialReady || (it->connection.expiresAt.isValid() && it->connection.expiresAt <= now_())) return false;
        for (auto& state : states_) if (state.connection.credentialId == it->connection.credentialId) state.suspended = false;
    }
    it->blocked.clear();
    return refreshSession(id, true);
}
bool ResourceClient::refreshSession(const QString& id, bool foreground) {
    auto it = states_.find(id);
    if (it == states_.end() || !wanted(id) || it->suspended || !enabled_) return false;
    it->lastSync = now_();
    if (it->syncPending > 0) {
        if (foreground) {
            for (auto& task : queue_) if (task.id == id) task.foreground = true;
            if (running_ && running_->id == id) running_->foreground = true;
            for (auto& task : reads_) if (task.id == id) task.foreground = true;
        }
        scheduleSync(); return true;
    }
    it->syncTotal = 0; it->syncCompleted = 0; it->progress = 0;
    if (!foreground && !it->collections.isEmpty()) {
        const auto cutoff = now_().addSecs(-25);
        for (auto collection = it->collections.cbegin(); collection != it->collections.cend(); ++collection) {
            if (collection->at >= cutoff) continue;
            Task task; task.id = id; task.path = collection.key(); task.collection = task.path; task.kind = collection->kind;
            task.namespaced = collection->namespaced; task.deletable = collection->deletable; task.read = Read::List; enqueue(task);
        }
    } else {
        Task core; core.id = id; core.path = "/api"; core.foreground = foreground;
        enqueue(core);
        Task groups = core; groups.path = "/apis"; groups.read = Read::Groups; enqueue(groups);
    }
    scheduleSync(); return true;
}
bool ResourceClient::wanted(const QString& id) const { return states_.contains(id) && states_[id].open && (id == visible_ || settings_.inactiveSyncMinutes > 0); }
bool ResourceClient::configure(ReadSettings settings) {
    if (!settings.valid()) return false;
    if (settings_.requestHardLimitPerMinute == settings.requestHardLimitPerMinute && settings_.inactiveSyncMinutes == settings.inactiveSyncMinutes
        && settings_.logLimitMb == settings.logLimitMb && settings_.yamlLimitMiB == settings.yamlLimitMiB) {
        settings_ = std::move(settings); return true;
    }
    settings_ = settings;
    for (auto state = states_.begin(); state != states_.end(); ++state)
        for (auto& history : state->logs) {
            history.trim(static_cast<qint64>(settings_.logLimitMb) * 1000000);
            if (state.key() == logSession_ && logKey(state.key(), history.path) == logKey_) emit logsChanged(state.key(), history.path);
        }
    showSession(visible_); scheduleSync(); dispatch(); return true;
}
bool ResourceClient::enableRequests(bool enabled) {
    enabled_ = enabled;
    if (!enabled) {
        dropQueued([](const auto&) { return true; }); gate_.stop(); logSync_.stop();
    }
    scheduleSync(); if (enabled) { dispatch(); scheduleLogs(); } return true;
}
bool ResourceClient::showSession(const QString& id) {
    visible_ = id;
    if (logSession_ != id) hideLogs();
    dropQueued([&](const auto& task) { return task.read != Read::Verify && task.read != Read::VerifyDelete
        && (task.forwardToken.isEmpty() && task.terminalToken.isEmpty() ? (!wanted(task.id) || ((task.read == Read::Detail || task.read == Read::Delete) && task.id != visible_)) : !states_[task.id].open); });
    scheduleSync(); dispatch(); return true;
}
bool ResourceClient::setFocused(bool focused) { focused_ = focused; scheduleSync(); return true; }
bool ResourceClient::userActivity() { activityAt_ = clock_.elapsed(); return true; }
void ResourceClient::scheduleSync() {
    sync_.stop();
    if (!enabled_) return;
    const auto now = now_();
    qint64 delay = std::numeric_limits<qint64>::max();
    for (auto it = states_.cbegin(); it != states_.cend(); ++it) {
        if (!wanted(it.key()) || it->suspended || it->resourcePending) continue;
        delay = std::min(delay, std::max<qint64>(1, now.msecsTo(it->lastSync.addMSecs(syncInterval(it.key(), it.value())))));
    }
    if (delay != std::numeric_limits<qint64>::max()) sync_.start(static_cast<int>(std::min<qint64>(delay, std::numeric_limits<int>::max())));
}
qint64 ResourceClient::syncInterval(const QString& id, const State& state) const {
    const bool available = !state.snapshot.isEmpty();
    if (id != visible_) return static_cast<qint64>(settings_.inactiveSyncMinutes) * 60000;
    if (!focused_) return available ? 240000 : 45000;
    if (!available) return state.failures.isEmpty() ? 12000 : 25000;
    const auto idle = clock_.elapsed() - activityAt_;
    return idle < 30000 ? 20000 : idle < 300000 ? 45000 : 120000;
}
bool ResourceClient::synchronize() {
    expire();
    const auto ids = states_.keys();
    for (const auto& id : ids) {
        const auto& state = states_[id];
        if (wanted(id) && !state.resourcePending && !state.suspended && state.lastSync.addMSecs(syncInterval(id, state)) <= now_()) refreshSession(id, false);
    }
    scheduleSync(); return true;
}
bool ResourceClient::inspect(const QString& id, const QString& path) {
    const auto it = states_.constFind(id);
    if (it == states_.cend() || !it->open || it->suspended) return false;
    const auto row = it->resources.value(path).value;
    if (row.isEmpty()) return false;
    for (auto task = queue_.begin(); task != queue_.end();) {
        if (task->id == id && task->read == Read::Detail && task->path != path) { --states_[id].pending; --states_[id].resourcePending; task = queue_.erase(task); }
        else ++task;
    }
    Task task; task.id = id; task.path = path; task.read = Read::Detail; task.foreground = true;
    task.kind = row["kind"].toString(); task.namespaced = !row["namespace"].toString().isEmpty();
    return enqueue(task);
}
bool ResourceClient::dismissInspector(const QString& id) {
    if (!states_.contains(id)) return false;
    dropQueued([&](const auto& task) { return task.id == id && task.read == Read::Detail; });
    return true;
}
QJsonArray ResourceClient::rows(const QString& id) const { return states_.contains(id) ? states_[id].snapshot : QJsonArray{}; }
void ResourceClient::publishRows(const QString& id) {
    auto& state = states_[id];
    QMap<QString, CachedResource> resources;
    QJsonArray snapshot;
    for (const auto& collection : state.collections)
        for (const auto& value : collection.rows) {
            const auto row = value.toObject();
            const auto path = row["path"].toString();
            const auto current = state.resources.value(path);
            resources.insert(path, current.received > collection.received ? current : CachedResource{row, collection.received});
        }
    QMap<QString, QJsonObject> metrics;
    const auto metricKey=[](const QJsonObject& row, const QString& kind) { return kind+'\n'+row["namespace"].toString()+'\n'+row["name"].toString(); };
    for (const auto& cached : resources) if (cached.value["apiVersion"].toString().startsWith("metrics.k8s.io/")) {
        if (cached.value["kind"]=="PodMetrics") metrics[metricKey(cached.value, "Pod")]=cached.value;
        if (cached.value["kind"]=="NodeMetrics") metrics[metricKey(cached.value, "Node")]=cached.value;
    }
    for (auto& cached : resources) if (cached.value["apiVersion"]=="v1") cached.value=withResourceMetrics(cached.value, metrics.value(metricKey(cached.value, cached.value["kind"].toString())), now_());
    QMap<QByteArray, QString> eventPaths;
    QSet<QString> aliases;
    QMap<QString, QJsonObject> previous;
    for (const auto& value : state.snapshot) { const auto row = value.toObject(); previous.insert(row["path"].toString(), row); }
    for (const auto& row : resources) {
        const auto& value = row.value;
        if (!value["kubernetesEvent"].toBool() || value["uid"].toString().isEmpty()) continue;
        const auto identity = QJsonDocument(QJsonArray{value["uid"], value["namespace"], value["name"]}).toJson(QJsonDocument::Compact);
        const auto path = value["path"].toString();
        const auto existing = eventPaths.constFind(identity);
        if (existing == eventPaths.cend()) eventPaths.insert(identity, path);
        else if (value["apiVersion"] == "events.k8s.io/v1") {
            aliases.insert(*existing); eventPaths[identity] = path;
        } else aliases.insert(path);
    }
    for (const auto& row : resources) {
        if (aliases.contains(row.value["path"].toString())) continue;
        if (row.value["apiVersion"].toString().startsWith("metrics.k8s.io/") && (row.value["kind"]=="PodMetrics" || row.value["kind"]=="NodeMetrics")) continue;
        auto visible = row.value;
        visible.remove("fetchedAt");
        const auto old = previous.value(visible["path"].toString());
        if (!old.isEmpty()) {
            if (old["uid"] != visible["uid"] || old["resourceVersion"] != visible["resourceVersion"]) visible["changedAt"] = now_().toString(Qt::ISODateWithMs);
            else if (old.contains("changedAt")) visible["changedAt"] = old["changedAt"];
        }
        snapshot.append(visible);
    }
    snapshot=withProblemState(snapshot, now_(), state.healthExpiry);
    if (state.healthExpiry.isValid()) {
        const auto delay=std::max<qint64>(1, now_().msecsTo(state.healthExpiry));
        if (!expiry_.isActive() || delay<expiry_.remainingTime()) expiry_.start(static_cast<int>(std::min<qint64>(delay, std::numeric_limits<int>::max())));
    }
    const bool different = snapshot != state.snapshot;
    state.resources = std::move(resources);
    if (!different) return;
    state.issueByPath.clear();
    for (const auto& value : snapshot) {
        const auto row=value.toObject(); state.issueByPath.insert(row["path"].toString(), row["issue"].toString());
    }
    state.snapshot = std::move(snapshot);
    reconcileLogs(id);
    emit rowsChanged(id);
}
void ResourceClient::reconcileLogs(const QString& id) {
    auto& state = states_[id];
    if (id == logSession_ && state.logs.contains(logKey_)) {
        const auto path = state.logs.value(logKey_).path;
        if (logKey(id, path) != logKey_) { hideLogs(); if (!showLogs(id, path)) emit logsChanged(id, path); }
    }
}
QJsonObject ResourceClient::detail(const QString& id, const QString& path) const {
    const auto it = states_.constFind(id);
    if (it == states_.cend()) return {};
    const auto detail = it->details.value(path).value;
    const auto row = it->resources.value(path).value;
    return !row.isEmpty() && row["uid"] != detail["uid"] ? QJsonObject{} : detail;
}
QJsonObject ResourceClient::document(const QString& id, const QString& path) const {
    const auto state = states_.constFind(id);
    if (state == states_.cend() || detail(id, path).isEmpty()) return {};
    return state->details.value(path).document;
}
QString ResourceClient::detailStatus(const QString& id, const QString& path) const {
    const auto state = states_.constFind(id);
    if (state == states_.cend() || path.isEmpty()) return "No resource selected";
    const auto pending = [&](const auto& task) { return task.id == id && task.path == path && task.read == Read::Detail; };
    const bool loading = (running_ && pending(*running_)) || std::any_of(reads_.cbegin(), reads_.cend(), pending) || std::any_of(queue_.cbegin(), queue_.cend(), pending);
    QStringList messages;
    if (loading) messages.append("Refreshing detail.");
    const auto fetched = detail(id, path)["fetchedAt"].toString();
    messages.append(fetched.isEmpty() ? "No current detail cached." : "Cached detail from " + fetched + '.');
    if (state->failures.contains(path)) messages.append(state->failures.value(path));
    else if (state->suspended) messages.append(state->failures.value("/authentication"));
    return messages.join(' ');
}
QJsonObject ResourceClient::resource(const QString& id, const QString& path) const {
    const auto it = states_.constFind(id);
    if (it == states_.cend()) return {};
    auto row = it->resources.value(path).value;
    if (row.isEmpty()) row=it->details.value(path).value;
    const auto issue=it->issueByPath.constFind(path);
    if (!row.isEmpty() && issue!=it->issueByPath.cend()) { row["issue"]=*issue; row["problems"]=!issue->isEmpty(); }
    return row;
}
bool ResourceClient::containsResource(const QString& id, const QString& path) const {
    const auto state = states_.constFind(id);
    return state != states_.cend() && state->resources.contains(path);
}
QString ResourceClient::status(const QString& id) const {
    const auto it = states_.constFind(id);
    if (it == states_.cend()) return "No session selected";
    QStringList messages;
    for (auto failure = it->failures.cbegin(); failure != it->failures.cend(); ++failure)
        messages.append(failure.key() + ": " + failure.value());
    QDateTime oldest;
    for (const auto& collection : it->collections)
        if (!oldest.isValid() || collection.at < oldest) oldest = collection.at;
    if (oldest.isValid()) messages.append((!it->failures.isEmpty() || oldest < now_().addSecs(-25) ? "Stale snapshot: " : "Snapshot: ") + oldest.toLocalTime().toString(Qt::ISODate));
    if (it->pending) messages.append(QString("Loading (%1 queued or running)").arg(it->pending));
    return messages.isEmpty() ? "No resource snapshot yet" : messages.join("\n");
}
bool ResourceClient::loading(const QString& id) const { return states_.contains(id) && states_[id].pending > 0; }
bool ResourceClient::syncLoading(const QString& id) const { return states_.contains(id) && states_[id].syncPending > 0; }
double ResourceClient::loadingProgress(const QString& id) const { return states_.value(id).progress; }
bool ResourceClient::initialSyncComplete(const QString& id) const { return states_.contains(id) && states_[id].initialized; }
bool ResourceClient::collectionRead(Read read) { return read == Read::Core || read == Read::Groups || read == Read::Discovery || read == Read::List; }
bool ResourceClient::parallelRead(const Task& task) { return !task.change && task.forwardToken.isEmpty() && (collectionRead(task.read) || task.read == Read::Detail); }
void ResourceClient::updateProgress(const QString& id) {
    auto& state = states_[id];
    if (!state.syncTotal) return;
    if (!state.syncPending && state.open && !state.suspended) { state.progress = 1; state.initialized = true; }
    else if (state.syncPending) state.progress = std::max(state.progress, double(state.syncCompleted) / (state.syncTotal + 1));
}
bool ResourceClient::authenticationRequired(const QString& id) const { return states_.contains(id) && states_[id].suspended; }
std::optional<ClusterConnection> ResourceClient::connection(const QString& id) const {
    const auto found = states_.constFind(id);
    return found == states_.cend() ? std::optional<ClusterConnection>{} : found->connection;
}
QString ResourceClient::logKey(const QString& id, const QString& path) const {
    const auto row = resource(id, path);
    return path + '\n' + row["uid"].toString();
}
const PodLogHistory* ResourceClient::logHistory(const QString& id, const QString& path) const {
    const auto state = states_.constFind(id);
    if (state == states_.cend()) return nullptr;
    const auto history = state->logs.constFind(logKey(id, path));
    return history == state->logs.cend() ? nullptr : &history.value();
}
QStringList ResourceClient::logContainers(const QString& id, const QString& path) const {
    const auto* history = logHistory(id, path);
    if (!history) return {};
    return history->containers.size() > 1 ? QStringList{"*"} + history->containers : history->containers;
}
QString ResourceClient::logSelection(const QString& id, const QString& path) const { const auto* history = logHistory(id, path); return history ? history->selected : QString{}; }
QString ResourceClient::logStatus(const QString& id, const QString& path) const { const auto* history = logHistory(id, path); return history ? history->status() : QString{}; }
PodLogHistory::Removal ResourceClient::logRemoval(const QString& id, const QString& path) const { const auto* history = logHistory(id, path); return history ? history->removal : PodLogHistory::Removal::None; }
bool ResourceClient::logsPaused(const QString& id, const QString& path) const { const auto* history = logHistory(id, path); return history && history->paused; }
QList<LogEntry> ResourceClient::logEntries(const QString& id, const QString& path) const { const auto* history = logHistory(id, path); return history ? history->presented : QList<LogEntry>{}; }
void ResourceClient::dropQueued(const std::function<bool(const Task&)>& obsolete) {
    QList<Task> canceled;
    QList<Task> canceledForwards;
    for (auto task = queue_.begin(); task != queue_.end();) {
        if (!obsolete(*task)) { ++task; continue; }
        auto& state = states_[task->id]; --state.pending;
        if (task->read == Read::Log) --state.logs[task->logKey].pending;
        else --state.resourcePending;
        if (collectionRead(task->read)) { --state.syncPending; --state.syncTotal; }
        if (task->change) canceled.append(*task);
        if (!task->forwardToken.isEmpty()) canceledForwards.append(*task);
        if (!task->terminalToken.isEmpty()) {
            const auto terminal = terminals_.constFind(task->terminalToken);
            if (terminal != terminals_.cend()) terminal->screen->reject("Queued exec canceled before sending.");
        }
        task = queue_.erase(task);
    }
    for (const auto& task : canceledForwards) {
        const auto current = forwards_.constFind(task.forwardToken);
        if (current == forwards_.cend()) continue;
        if (task.read == Read::ForwardSocket) current->transport->rejectStream(task.streamToken);
        else failForward(task.forwardToken, "Queued port-forward resolution canceled before sending.");
    }
    for (const auto& task : canceled) {
        if (task.read == Read::Delete || task.read == Read::VerifyDelete)
            emit resourceDeleteFinished(task.id, task.path, task.change->token, task.read == Read::Delete ? "canceled" : "uncertain",
                task.read == Read::Delete ? "Queued deletion canceled before sending. Nothing deleted."
                                          : "Deletion outcome uncertain; read-back unavailable. No deletion was retried.", false);
        else emit yamlApplyFinished(task.id, task.path, task.change->token, task.read == Read::Apply ? "canceled" : "uncertain",
            task.read == Read::Apply ? "Queued write canceled before sending. Draft retained." : "Read-back unavailable. No write was retried; draft retained.", {});
    }
}
bool ResourceClient::hideLogs() {
    logSync_.stop(); logSession_.clear(); logKey_.clear();
    dropQueued([](const auto& task) { return task.read == Read::Log; });
    return true;
}
bool ResourceClient::showLogs(const QString& id, const QString& path) {
    if (id != visible_ || !states_.contains(id) || !states_[id].open) return false;
    const auto row = resource(id, path);
    if (row["kind"] != "Pod" || row["uid"].toString().isEmpty() || !path.startsWith("/api/v1/namespaces/") || !path.contains("/pods/")) return false;
    QStringList containers;
    for (const auto& value : row["containers"].toArray()) {
        if (!value.isString() || !segment(value.toString())) return false;
        containers.append(value.toString());
    }
    if (containers.isEmpty()) return false;
    const auto key = logKey(id, path);
    if (logSession_ != id || logKey_ != key) hideLogs();
    auto& history = states_[id].logs[key];
    history.path = path; history.uid = row["uid"].toString(); history.containers = containers;
    if (history.selected != "*" && !containers.contains(history.selected)) history.selected = containers.size() == 1 ? containers.first() : "*";
    logSession_ = id; logKey_ = key;
    emit logsChanged(id, path);
    if (history.paused) { scheduleLogs(); return true; }
    refreshLogs(); return true;
}
bool ResourceClient::selectLogContainer(const QString& container) {
    if (logSession_.isEmpty()) return false;
    auto& history = states_[logSession_].logs[logKey_];
    if (container != "*" && !history.containers.contains(container)) return false;
    history.selected = container; history.publish();
    dropQueued([&](const auto& task) { return task.read == Read::Log && task.id == logSession_ && task.logKey == logKey_ && container != "*" && task.container != container; });
    emit logsChanged(logSession_, history.path);
    return refreshLogs();
}
bool ResourceClient::pauseLogs(bool paused) {
    if (logSession_.isEmpty()) return false;
    auto& history = states_[logSession_].logs[logKey_];
    history.paused = paused;
    if (paused) { dropQueued([](const auto& task) { return task.read == Read::Log; }); }
    else { history.publish(); }
    emit logsChanged(logSession_, history.path);
    scheduleLogs();
    return paused || refreshLogs();
}
bool ResourceClient::refreshLogs(bool foreground) {
    expire();
    if (!enabled_ || logSession_.isEmpty() || logSession_ != visible_) return false;
    auto& state = states_[logSession_]; auto& history = state.logs[logKey_];
    if (state.suspended || !state.open || (!foreground && (history.paused || history.pending))) return false;
    if (!foreground && clock_.elapsed() < history.cycleAt + 3000) { scheduleLogs(); return true; }
    if (!history.pending) history.cycleAt = -1;
    const auto selected = history.selected == "*" ? history.containers : QStringList{history.selected};
    for (const auto& container : selected) {
        Task task; task.id = logSession_; task.path = history.path + "/log"; task.read = Read::Log;
        task.logKey = logKey_; task.container = container; task.continuation = container; task.foreground = foreground;
        enqueue(task);
    }
    scheduleLogs(); return true;
}
void ResourceClient::scheduleLogs() {
    logSync_.stop();
    if (!enabled_ || logSession_.isEmpty() || logSession_ != visible_) return;
    const auto& state = states_[logSession_]; const auto& history = state.logs[logKey_];
    if (!state.open || state.suspended || history.paused || history.pending) return;
    const auto delay = std::max<qint64>(1, history.cycleAt + 3000 - clock_.elapsed());
    logSync_.start(static_cast<int>(std::min<qint64>(delay, std::numeric_limits<int>::max())));
}
bool ResourceClient::authenticate(const ClusterConnection& connection, const QString& session) {
    if (!connection.credentialReady) return false;
    for (auto it = states_.begin(); it != states_.end(); ++it) {
        if (it->connection.credentialId != connection.credentialId) continue;
        const auto context = it->connection.contextId;
        it->connection = connection; it->connection.contextId = context;
        ++it->revision; it->suspended = false; emit changed(it.key());
    }
    resetConnections_ = true; expire();
    return refresh(session, true);
}
void ResourceClient::suspend(const QString& credential, const QString& message) {
    for (auto it = states_.begin(); it != states_.end(); ++it)
        if (it->connection.credentialId == credential) { it->suspended = true; it->failures.insert("/authentication", message); emit changed(it.key()); }
    dropQueued([&](const auto& task) { return states_[task.id].suspended; });
    emit authenticationRejected(credential);
    scheduleSync();
    scheduleLogs();
}
void ResourceClient::expire() {
    expiry_.stop();
    const auto now = now_();
    qint64 next = std::numeric_limits<qint64>::max();
    QSet<QString> expired;
    for (auto it = states_.begin(); it != states_.end(); ++it) {
        bool rowsChanged = false, detailChanged = false;
        if (it->healthExpiry.isValid() && it->healthExpiry<=now) rowsChanged=true;
        for (const auto& cached : it->resources) if (!cached.value["metricStale"].toBool()) {
            const auto at=QDateTime::fromString(cached.value["metricAt"].toString(), Qt::ISODateWithMs);
            if (!at.isValid() || cached.value["apiVersion"]!="v1") continue;
            const auto delay=now.msecsTo(at.addSecs(25))+1;
            if (delay<=0) rowsChanged=true; else next=std::min(next, delay);
        }
        for (auto& history : it->logs) {
            if (history.expire(now) && it.key() == logSession_ && logKey(it.key(), history.path) == logKey_) emit logsChanged(it.key(), history.path);
            for (const auto& cursor : history.cursors) next = std::min(next, std::max<qint64>(1, now.msecsTo(cursor.fetched.addSecs(60)) + 1));
        }
        for (auto entry = it->collections.begin(); entry != it->collections.end();) {
            const auto delay = now.msecsTo(entry->at.addSecs(86400)) + 1;
            if (delay <= 0) { entry = it->collections.erase(entry); rowsChanged = true; } else { next = std::min(next, delay); ++entry; }
        }
        for (auto entry = it->details.begin(); entry != it->details.end();) {
            const auto delay = now.msecsTo(entry->at.addSecs(300)) + 1;
            if (delay <= 0) { entry = it->details.erase(entry); detailChanged = true; } else { next = std::min(next, delay); ++entry; }
        }
        if (rowsChanged) publishRows(it.key());
        if (it->healthExpiry.isValid()) next=std::min(next, std::max<qint64>(1, now.msecsTo(it->healthExpiry)));
        if (detailChanged) reconcileLogs(it.key());
        if (rowsChanged || detailChanged) emit changed(it.key());
        if (!it->suspended && it->connection.expiresAt.isValid()) {
            const auto delay = now.msecsTo(it->connection.expiresAt);
            if (delay <= 0) expired.insert(it->connection.credentialId);
            else next = std::min(next, delay);
        }
    }
    for (const auto& credential : expired) suspend(credential, "Credentials expired. Confirm another authentication attempt; cached data is retained.");
    if (next != std::numeric_limits<qint64>::max()) expiry_.start(static_cast<int>(std::min<qint64>(next, std::numeric_limits<int>::max())));
}
bool ResourceClient::enqueue(Task task) {
    expire();
    auto& state = states_[task.id];
    if (task.read == Read::Log && (task.id != logSession_ || task.logKey != logKey_)) return false;
    if (!enabled_ || (task.forwardToken.isEmpty() && task.terminalToken.isEmpty() ? (task.read != Read::Verify && task.read != Read::VerifyDelete && !wanted(task.id)) : !state.open) || state.suspended
        || ((task.read == Read::Detail || task.read == Read::Delete) && task.id != visible_) || (!task.foreground && state.blocked.contains(task.path))) return false;
    if (running_ && running_->read == task.read && running_->id == task.id && running_->path == task.path && running_->continuation == task.continuation && running_->logKey == task.logKey && running_->forwardToken == task.forwardToken && running_->streamToken == task.streamToken && running_->terminalToken == task.terminalToken)
        return task.read != Read::Apply && task.read != Read::Delete && (!task.change || (running_->change && running_->change->token == task.change->token));
    for (auto& active : reads_) if (active.read == task.read && active.id == task.id && active.path == task.path && active.continuation == task.continuation) {
        active.foreground = active.foreground || task.foreground; return true;
    }
    for (auto& waiting : queue_) {
        if (waiting.read == task.read && waiting.id == task.id && waiting.path == task.path && waiting.continuation == task.continuation && waiting.logKey == task.logKey && waiting.forwardToken == task.forwardToken && waiting.streamToken == task.streamToken && waiting.terminalToken == task.terminalToken) {
            if (task.change) return waiting.change && waiting.change->token == task.change->token && task.read != Read::Apply && task.read != Read::Delete;
            waiting.foreground = waiting.foreground || task.foreground;
            return true;
        }
    }
    ++state.pending;
    if (task.read == Read::Log) ++state.logs[task.logKey].pending;
    else ++state.resourcePending;
    if (collectionRead(task.read)) { ++state.syncPending; ++state.syncTotal; }
    queue_.append(std::move(task));
    emit changed(queue_.last().id);
    if (!running_) dispatch();
    return true;
}
void ResourceClient::dispatch() {
    expire();
    if (!enabled_ || running_ || reads_.size() >= 4 || queue_.isEmpty()) { if (queue_.isEmpty()) gate_.stop(); return; }
    const qint64 spacing = settings_.requestHardLimitPerMinute > 0 ? std::max<qint64>(400, (60000 + settings_.requestHardLimitPerMinute - 1) / settings_.requestHardLimitPerMinute) : 400;
    const auto wait = std::max(backoffUntil_, lastStart_ < 0 ? qint64{0} : lastStart_ + spacing) - clock_.elapsed();
    if (wait > 0) { gate_.start(static_cast<int>(std::min<qint64>(wait, std::numeric_limits<int>::max()))); return; }
    auto chosen = std::find_if(queue_.begin(), queue_.end(), [](const auto& task) { return task.change.has_value() || task.read == Read::Detail || (task.read == Read::Log && task.foreground); });
    if (chosen == queue_.end()) chosen = std::find_if(queue_.begin(), queue_.end(), [](const auto& task) { return task.foreground; });
    if (chosen == queue_.end()) chosen = queue_.begin();
    if (!parallelRead(*chosen) && !reads_.isEmpty()) return;
    running_ = *chosen;
    queue_.erase(chosen);
    running_->revision = states_[running_->id].revision;
    if (!running_->received) running_->received = clock_.nsecsElapsed();
    running_->started=clock_.elapsed();
    if (resetConnections_ && reads_.isEmpty()) { network_.clearConnectionCache(); resetConnections_ = false; }
    const Task task = *running_;
    if (!task.terminalToken.isEmpty() && (!terminals_.contains(task.terminalToken) || terminals_.value(task.terminalToken).server != states_[task.id].connection.server)) {
        --states_[task.id].pending; --states_[task.id].resourcePending;
        if (terminals_.contains(task.terminalToken)) terminals_.value(task.terminalToken).screen->reject("The originating exec server is no longer available.");
        running_.reset(); emit changed(task.id); dispatch(); return;
    }
    if (!task.forwardToken.isEmpty() && (!forwards_.contains(task.forwardToken) || forwards_.value(task.forwardToken).server != states_[task.id].connection.server)) {
        --states_[task.id].pending; --states_[task.id].resourcePending;
        failForward(task.forwardToken, "The originating port-forward server is no longer available.");
        running_.reset(); emit changed(task.id); dispatch(); return;
    }
    lastStart_ = clock_.elapsed();
    if (task.read == Read::Log && states_[task.id].logs[task.logKey].cycleAt < 0) states_[task.id].logs[task.logKey].cycleAt = lastStart_;
    const auto& connection = task.change ? task.change->connection : states_[task.id].connection;
    QByteArray base = connection.server.toEncoded();
    while (base.endsWith('/')) base.chop(1);
    QUrl url = QUrl::fromEncoded(base + task.path.toUtf8());
    if (task.read == Read::List || task.read == Read::ForwardPods) {
        QUrlQuery query; query.addQueryItem("limit", "500");
        if (task.read == Read::ForwardPods) query.addQueryItem("labelSelector", task.selector);
        if (!task.continuation.isEmpty()) query.addQueryItem("continue", task.continuation);
        url.setQuery(query);
    } else if (task.read == Read::Log) {
        const auto cursor = states_[task.id].logs[task.logKey].cursors.value(task.container);
        QUrlQuery query; query.addQueryItem("container", task.container); query.addQueryItem("timestamps", "true");
        if (cursor.timestamp.isEmpty()) query.addQueryItem("tailLines", "100");
        else query.addQueryItem("sinceTime", cursor.timestamp);
        url.setQuery(query);
    }
    QNetworkRequest request(url);
    request.setSslConfiguration(connection.tls);
    if (!connection.peerName.isEmpty()) request.setPeerVerifyName(connection.peerName);
    request.setRawHeader("Accept", "application/json");
    if (!connection.authorization.isEmpty()) request.setRawHeader("Authorization", connection.authorization);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    request.setAttribute(QNetworkRequest::AuthenticationReuseAttribute, QNetworkRequest::Manual);
    request.setAttribute(QNetworkRequest::CookieLoadControlAttribute, QNetworkRequest::Manual);
    request.setAttribute(QNetworkRequest::CookieSaveControlAttribute, QNetworkRequest::Manual);
    request.setTransferTimeout(0);
    if (task.read == Read::TerminalSocket) {
        const auto terminal = terminals_.value(task.terminalToken);
        const bool sent = terminal.screen->open(request, terminal.container, terminal.shell);
        if (sent) emit requestStarted(task.id, task.path, lastStart_);
        else terminalHandshakeFinished(task.terminalToken, false, false);
        return;
    }
    if (task.read == Read::ForwardSocket) {
        const auto forward = forwards_.value(task.forwardToken);
        const bool sent = forward.transport->connectStream(task.streamToken, request, forward.remotePort);
        if (sent) emit requestStarted(task.id, task.path, lastStart_);
        else forwardHandshakeFinished(task.forwardToken, task.streamToken, false, false);
        return;
    }
    if (task.read == Read::Apply) request.setRawHeader("Content-Type", "application/json-patch+json");
    if (task.read == Read::Delete) request.setRawHeader("Content-Type", "application/json");
    auto* reply = task.read == Read::Apply
        ? network_.sendCustomRequest(request, "PATCH", QJsonDocument(task.patch).toJson(QJsonDocument::Compact))
        : task.read == Read::Delete ? network_.sendCustomRequest(request, "DELETE", QJsonDocument(QJsonObject{
            {"apiVersion", "v1"}, {"kind", "DeleteOptions"}, {"preconditions", QJsonObject{{"uid", task.change->baseline["uid"]}}}
        }).toJson(QJsonDocument::Compact)) : network_.get(request);
    const auto payload = std::make_shared<LogPayload>();
    const bool parallel = parallelRead(task);
    if (parallel) { reads_.insert(reply, task); running_.reset(); }
    if (task.read == Read::Log) {
        reply->setReadBufferSize(65536);
        connect(reply, &QNetworkReply::readyRead, this, [this, reply, payload] { payload->append(reply->readAll(), static_cast<qint64>(settings_.logLimitMb) * 1000000); });
    }
    connect(reply, &QNetworkReply::finished, this, [this, reply, task, payload, parallel] {
        Task completion = task; completion.foreground = parallel ? reads_.value(reply).foreground : running_->foreground;
        const auto remaining = reply->readAll();
        const auto http = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        bool lengthKnown = false;
        // Qt removes Content-Length when automatically decompressing the response.
        const auto length = reply->header(QNetworkRequest::ContentLengthHeader).toLongLong(&lengthKnown);
        const bool incomplete = task.read != Read::Log && lengthKnown && length != remaining.size();
        const bool transportError = incomplete || (reply->error() != QNetworkReply::NoError
            && !(http == 404 && reply->error() == QNetworkReply::ContentNotFoundError));
        if (task.read == Read::Log) {
            payload->append(remaining, static_cast<qint64>(settings_.logLimitMb) * 1000000);
            states_[task.id].logs[task.logKey].limited = states_[task.id].logs[task.logKey].limited || payload->limited;
        }
        finish(completion, http, task.read == Read::Log ? payload->bytes : remaining,
            transportError, reply->rawHeader("Retry-After"));
        reply->deleteLater();
        if (parallel) reads_.remove(reply); else running_.reset();
        emit changed(task.id);
        dispatch(); scheduleSync(); scheduleLogs();
    });
    emit requestStarted(task.id, task.path, lastStart_);
    if (parallel) dispatch();
}
void ResourceClient::fail(const Task& task, QString message) {
    if (task.read == Read::Log) states_[task.id].logs[task.logKey].failures.insert(task.container, std::move(message));
    else { states_[task.id].failures.insert(task.path, std::move(message)); states_[task.id].blocked.insert(task.path); }
}
void ResourceClient::finish(const Task& task, int http, const QByteArray& bytes, bool transportError, const QByteArray& retryAfter) {
    auto& state = states_[task.id];
    --state.pending;
    if (task.read == Read::Log) --state.logs[task.logKey].pending;
    else --state.resourcePending;
    if (collectionRead(task.read)) { --state.syncPending; ++state.syncCompleted; }
    if (http == 401) {
        fail(task, "Authentication failed. Confirm a new authentication attempt to retry.");
        if (task.revision == state.revision) suspend(state.connection.credentialId, "Authentication failed. Confirm a new authentication attempt to retry.");
    } else if (http == 429) {
        bool valid = false;
        const auto seconds = retryAfter.toLongLong(&valid);
        qint64 delay = 1000;
        if (valid && seconds >= 0) delay = seconds > std::numeric_limits<qint64>::max() / 1000 ? std::numeric_limits<qint64>::max() : seconds * 1000;
        else {
            const auto date = QDateTime::fromString(QString::fromLatin1(retryAfter), Qt::RFC2822Date);
            if (date.isValid()) delay = std::max<qint64>(1000, now_().msecsTo(date));
        }
        const auto now = clock_.elapsed();
        backoffUntil_ = delay > std::numeric_limits<qint64>::max() - now ? std::numeric_limits<qint64>::max() : now + delay;
        fail(task, "Rate limited. Retry-After is active; cached data is retained.");
    } else if (http == 403) fail(task, "Authorization denied; other resource kinds remain available.");
    else if (http >= 300 && http < 400) fail(task, "Redirect refused; credentials were not forwarded.");
    else if (http >= 400) fail(task, QString("API rejected the %1 (HTTP %2).").arg(task.read == Read::Apply || task.read == Read::Delete ? "write" : "read").arg(http));
    else if (transportError || http == 0) fail(task, "Connection or TLS failure; cached data is retained.");
    else if (task.read == Read::Log) {
        auto& history = state.logs[task.logKey];
        if (const auto error = history.append(task.container, bytes, now_(), static_cast<qint64>(settings_.logLimitMb) * 1000000, history.limited)) fail(task, error->message);
        if (task.revision == state.revision && !state.suspended) state.failures.remove("/authentication");
    } else if (task.read == Read::Delete || task.read == Read::VerifyDelete) {
        state.failures.remove(task.path); state.blocked.remove(task.path);
    } else {
        QJsonParseError error;
        const auto document = QJsonDocument::fromJson(bytes, &error);
        if (error.error != QJsonParseError::NoError || !document.isObject()) fail(task, "Malformed API response; cached data is retained.");
        else {
            state.failures.remove(task.path);
            state.blocked.remove(task.path);
            if (task.revision == state.revision && !state.suspended) state.failures.remove("/authentication");
            if (task.read == Read::Verify) {
                const auto root = document.object(), metadata = root["metadata"].toObject();
                const auto baseline = task.change->baseline, identity = baseline["metadata"].toObject();
                if (root["apiVersion"] != baseline["apiVersion"] || root["kind"] != baseline["kind"]
                    || metadata["name"] != identity["name"] || metadata["namespace"].toString() != identity["namespace"].toString()
                    || metadata["uid"].toString().isEmpty() || metadata["resourceVersion"].toString().isEmpty())
                    fail(task, "Malformed read-back resource; draft retained.");
                else { Task detail = task; detail.read = Read::Detail; consume(detail, root); }
            } else if (!task.terminalToken.isEmpty()) consumeTerminal(task, document.object());
            else if (!task.forwardToken.isEmpty()) consumeForward(task, document.object());
            else if (task.read != Read::Apply) consume(task, document.object());
        }
    }
    updateProgress(task.id);
    if (task.read != Read::Log && state.resourcePending == 0) state.lastSync = now_();
    expire();
    if (!task.forwardToken.isEmpty() && state.failures.contains(task.path)) failForward(task.forwardToken, state.failures.value(task.path));
    if (!task.terminalToken.isEmpty() && state.failures.contains(task.path) && terminals_.contains(task.terminalToken)) terminals_.value(task.terminalToken).screen->reject(state.failures.value(task.path));
    if (task.read == Read::Detail) emit detailFinished(task.id, task.path, !state.failures.contains(task.path) && !document(task.id, task.path).isEmpty());
    if (task.read == Read::Apply) finishApply(task, http, bytes, transportError);
    if (task.read == Read::Delete) finishDeletion(task, http, bytes, transportError);
    if (task.read == Read::VerifyDelete) finishDeletionRead(task, http, bytes, transportError);
    if (task.read == Read::Verify) {
        emit detailFinished(task.id, task.path, !state.failures.contains(task.path) && !document(task.id, task.path).isEmpty());
        finishVerify(task);
    }
    if (task.read == Read::Log && task.id == logSession_ && task.logKey == logKey_) emit logsChanged(task.id, state.logs[task.logKey].path);
    completedRequests_.append(QVariantMap{{"session",task.id},{"time",now_().toString(Qt::ISODateWithMs)},
        {"method",task.read==Read::Apply ? "PATCH" : task.read==Read::Delete ? "DELETE" : "GET"},{"path",task.path},
        {"priority",task.foreground || task.read==Read::Detail || task.change ? "Foreground" : "Background"},
        {"status",transportError || http>=400 || state.failures.contains(task.path) ? "Failed" : "Success"},
        {"duration",QString::number(std::max<qint64>(0,clock_.elapsed()-task.started))+" ms"},
        {"outcome",http>0 ? QString("HTTP %1").arg(http) : QString("Transport failure")}});
    if (completedRequests_.size()>200) completedRequests_.removeFirst();
}
void ResourceClient::collections(const QString& id, const QString& path, const QJsonObject& document, bool foreground) {
    if (!document["resources"].isArray()) { Task task; task.id = id; task.path = path; fail(task, "Invalid discovery response."); return; }
    for (const auto& value : document["resources"].toArray()) {
        const auto resource = value.toObject();
        const auto name = resource["name"].toString();
        if (name.contains('/') || !resource["verbs"].toArray().contains("list")) continue;
        if (!segment(name) || !segment(resource["kind"].toString()) || !resource["namespaced"].isBool()) {
            Task task; task.id = id; task.path = path; fail(task, "Invalid resource discovery entry."); continue;
        }
        const bool namespaced = resource["namespaced"].toBool();
        QStringList scopes = namespaced ? states_[id].namespaces : QStringList{};
        if (scopes.isEmpty()) scopes.append(QString{});
        for (const auto& scope : scopes) {
            Task task; task.id = id;
            task.path = path + (scope.isEmpty() ? QString{} : "/namespaces/" + scope) + '/' + name;
            task.collection = task.path; task.kind = resource["kind"].toString();
            task.namespaced = namespaced; task.read = Read::List; task.foreground = foreground;
            task.deletable = resource["verbs"].toArray().contains("delete");
            enqueue(task);
        }
    }
}
void ResourceClient::consume(const Task& task, const QJsonObject& document) {
    auto& state = states_[task.id];
    if (task.read == Read::Core) {
        if (!document["versions"].isArray()) { fail(task, "Invalid core discovery response."); return; }
        for (const auto& value : document["versions"].toArray()) {
            if (!value.isString() || !segment(value.toString())) { fail(task, "Invalid core API version."); continue; }
            Task next; next.id = task.id; next.path = "/api/" + value.toString(); next.read = Read::Discovery;
            next.foreground = task.foreground;
            enqueue(next);
        }
    } else if (task.read == Read::Groups) {
        if (!document["groups"].isArray()) { fail(task, "Invalid group discovery response."); return; }
        for (const auto& value : document["groups"].toArray()) {
            const auto version = value.toObject()["preferredVersion"].toObject()["groupVersion"].toString();
            if (!versionPath(version) || !version.contains('/')) { fail(task, "Invalid preferred API version."); continue; }
            Task next; next.id = task.id; next.path = "/apis/" + version; next.read = Read::Discovery;
            next.foreground = task.foreground;
            enqueue(next);
        }
    } else if (task.read == Read::Discovery) collections(task.id, task.path, document, task.foreground);
    else if (task.read == Read::List) {
        if (!document["items"].isArray() || !document["metadata"].isObject()) { fail(task, "Invalid resource list response."); return; }
        Task next = task;
        for (const auto& value : document["items"].toArray()) {
            const auto object = value.toObject();
            const auto metadata = object["metadata"].toObject();
            const auto name = metadata["name"].toString();
            const auto ns = metadata["namespace"].toString();
            const auto suppliedKind = object.value("kind");
            const auto suppliedVersion = object.value("apiVersion");
            if (!resourceName(name) || (task.namespaced && !segment(ns))
                || (!suppliedVersion.isUndefined() && (!suppliedVersion.isString() || suppliedVersion.toString() != apiVersion(task.path)))
                || (!suppliedKind.isUndefined() && (!suppliedKind.isString() || suppliedKind.toString() != task.kind))) {
                fail(task, "Invalid resource list entry; previous snapshot retained."); return;
            }
            QString path = task.collection;
            if (task.namespaced && state.namespaces.isEmpty()) {
                const auto slash = path.lastIndexOf('/');
                path.insert(slash, "/namespaces/" + ns);
            }
            auto row=summary(object, path + '/' + QString::fromLatin1(QUrl::toPercentEncoding(name)), task.kind);
            row["deletable"] = task.deletable;
            if (apiVersion(task.path).startsWith("metrics.k8s.io/") && (task.kind=="PodMetrics" || task.kind=="NodeMetrics")) {
                const auto parsed=resourceMetricSample(object, row);
                if (const auto* failure=std::get_if<Failure>(&parsed)) { fail(task, failure->message); return; }
                row=std::get<QJsonObject>(parsed);
            }
            next.page.append(row);
        }
        const auto continuation = document["metadata"].toObject().value("continue");
        if (!continuation.isUndefined() && !continuation.isString()) { fail(task, "Invalid list continuation."); return; }
        next.continuation = continuation.toString();
        if (!next.continuation.isEmpty()) {
            if (next.tokens.contains(next.continuation)) { fail(task, "Repeated list continuation; previous snapshot retained."); return; }
            next.tokens.insert(next.continuation);
            enqueue(std::move(next));
        } else {
            const bool different = state.collections.value(task.collection).rows != next.page;
            state.collections.insert(task.collection, {next.page, now_(), task.kind, task.namespaced, next.received, task.deletable});
            bool canonicalChanged = false;
            for (const auto& value : next.page) {
                const auto row = value.toObject();
                auto entry = state.resources.find(row["path"].toString());
                if (entry == state.resources.end()) { canonicalChanged = true; continue; }
                if (entry->received > next.received) continue;
                auto previous = entry->value;
                previous.remove("fetchedAt");
                canonicalChanged = canonicalChanged || previous != row;
                *entry = {row, next.received};
            }
            if (different || canonicalChanged) publishRows(task.id);
        }
    } else {
        const auto kind = document["kind"].toString();
        const auto name = document["metadata"].toObject()["name"].toString();
        const auto ns = document["metadata"].toObject()["namespace"].toString();
        if (document["apiVersion"] != apiVersion(task.path) || kind != task.kind || !resourceName(name) || QUrl::fromPercentEncoding(task.path.section('/', -1).toLatin1()) != name
            || (task.namespaced ? ns != task.path.section('/', -3, -3) : !ns.isEmpty()) || !validValues(document)) { fail(task, "Invalid detail response."); return; }
        auto row = summary(document, task.path, kind);
        auto entry = state.resources.find(task.path);
        auto previous = entry == state.resources.end() ? QJsonObject{} : entry->value;
        row["deletable"] = previous["deletable"].toBool();
        previous.remove("fetchedAt");
        const bool different = previous != row;
        row.insert("fetchedAt", now_().toString(Qt::ISODateWithMs));
        if (entry != state.resources.end() && entry->received > task.received) { fail(task, "A newer resource read is already cached. Refresh the inspector to edit it."); return; }
        state.details.insert(task.path, {row, now_(), document});
        state.unresolvedDeletes.remove(task.path);
        if (entry != state.resources.end()) {
            *entry = {row, task.received};
            if (different) { publishRows(task.id); return; }
        }
        reconcileLogs(task.id);
    }
}
} // namespace podlord
