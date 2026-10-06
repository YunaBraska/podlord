#include "yaml_draft_check.h"
#include "yaml_apply.h"
#include "workspace.h"
#include <QHash>
#include <QSet>
#include <QJsonDocument>
#include <QFutureWatcher>
#include <QtConcurrent/QtConcurrentRun>
#include <cmath>
#include <limits>
#include <algorithm>
#include <vector>
#include <yaml-cpp/yaml.h>

namespace podlord {
Result<PreparedYaml> finishYaml(QJsonObject desired, const QJsonObject& original, qint64 byteLimit, const QJsonObject& hiddenDraft);
namespace {
struct Parsed final { YAML::Node root; std::vector<YAML::Node> order; };
Failure invalid(const QString& message) { return {StoreError::InvalidInput, message}; }
Result<Parsed> parseDraft(const QString& text, const QJsonObject& original) {
    const auto metadata = original["metadata"].toObject();
    if (original["apiVersion"].toString().isEmpty() || original["kind"].toString().isEmpty()
        || metadata["name"].toString().isEmpty() || metadata["uid"].toString().isEmpty() || metadata["resourceVersion"].toString().isEmpty()
        || (metadata.contains("namespace") && !metadata["namespace"].isString()))
        return invalid("No complete original resource identity is available. Nothing was sent.");
    if (!text.isValidUtf16()) return invalid("YAML contains invalid Unicode. Draft unchanged; nothing was sent.");
    if (text.contains(QChar{0})) return invalid("YAML contains a NUL character. Draft unchanged; nothing was sent.");
    try {
        const auto documents = YAML::LoadAll(text.toStdString());
        if (documents.size() != 1 || !documents.front().IsMap()) return invalid("Exactly one YAML resource mapping is required. Nothing was sent.");
        Parsed result{documents.front(), {}};
        struct Visit final { YAML::Node node; bool finished; };
        std::vector<Visit> pending{{result.root, false}};
        QHash<int, bool> visited;
        while (!pending.empty()) {
            const auto visit = pending.back(); pending.pop_back();
            if (!visit.node.IsMap() && !visit.node.IsSequence()) { result.order.push_back(visit.node); continue; }
            const auto mark = visit.node.Mark().pos;
            if (visit.finished) { visited[mark] = true; result.order.push_back(visit.node); continue; }
            const auto found = visited.constFind(mark);
            if (found != visited.cend()) {
                if (!found.value()) return invalid("Cyclic YAML aliases cannot represent a Kubernetes JSON resource. Nothing was sent.");
                continue;
            }
            visited.insert(mark, false); pending.push_back({visit.node, true});
            if (visit.node.IsMap()) {
                QSet<QString> keys;
                for (const auto& entry : visit.node) {
                    if (!entry.first.IsScalar()) return invalid("YAML mapping keys must be scalar strings. Nothing was sent.");
                    const auto key = QString::fromStdString(entry.first.Scalar());
                    if (keys.contains(key)) return invalid("Duplicate YAML mapping keys are not allowed. Nothing was sent.");
                    keys.insert(key); pending.push_back({entry.second, false});
                }
            } else for (const auto& entry : visit.node) pending.push_back({YAML::Node(entry), false});
        }
        const auto same = [](const YAML::Node& node, const QString& expected) { return node.IsDefined() && node.IsScalar() && QString::fromStdString(node.Scalar()) == expected; };
        const auto& root = result.root;
        if (!same(root["apiVersion"], original["apiVersion"].toString()) || !same(root["kind"], original["kind"].toString()))
            return invalid("The resource API version and kind must match the original target. Nothing was sent.");
        const auto draft = root["metadata"];
        if (!draft.IsDefined() || !draft.IsMap()) return invalid("Resource metadata must be a mapping. Nothing was sent.");
        for (const auto* field : {"name", "uid", "resourceVersion"})
            if (!same(draft[field], metadata[field].toString())) return invalid("The resource name, UID and resourceVersion must match the original target. Nothing was sent.");
        const auto scope = metadata["namespace"].toString();
        if ((!scope.isEmpty() || draft["namespace"].IsDefined()) && !same(draft["namespace"], scope)) return invalid("The namespace must match the original target scope. Nothing was sent.");
        return result;
    } catch (const YAML::Exception& error) {
        if (error.mark.line >= 0 && error.mark.column >= 0) return invalid(QString("YAML syntax error at line %1, column %2. Draft unchanged; nothing was sent.").arg(error.mark.line + 1).arg(error.mark.column + 1));
        return invalid("YAML could not be parsed. Draft unchanged; nothing was sent.");
    }
}
qint64 scalarBytes(const QJsonValue& value) { return QJsonDocument(QJsonArray{value}).toJson(QJsonDocument::Compact).size() - 2; }
Result<QJsonObject> convert(const Parsed& parsed, qint64 limit) {
    struct Value final { QJsonValue json; qint64 bytes; };
    QHash<int, Value> values;
    for (const auto& node : parsed.order) {
        const auto mark = node.Mark().pos;
        if (values.contains(mark)) continue;
        const auto tag = node.Tag();
        if (tag != "?" && tag != "!" && !tag.empty() && tag != "tag:yaml.org,2002:str" && tag != "tag:yaml.org,2002:bool"
            && tag != "tag:yaml.org,2002:int" && tag != "tag:yaml.org,2002:float" && tag != "tag:yaml.org,2002:null"
            && tag != "tag:yaml.org,2002:map" && tag != "tag:yaml.org,2002:seq")
            return invalid("Unsupported YAML tag; use JSON-compatible values. Nothing was sent.");
        QJsonValue json;
        qint64 bytes = 2;
        if (node.IsMap() || node.IsSequence()) {
            QJsonObject object; QJsonArray array;
            for (const auto& entry : node) {
                const auto child = node.IsMap() ? entry.second : YAML::Node(entry);
                const auto value = values.value(child.Mark().pos);
                const auto key = node.IsMap() ? QString::fromStdString(entry.first.Scalar()) : QString{};
                const auto extra = value.bytes + 1 + (node.IsMap() ? scalarBytes(key) + 1 : 0);
                if (extra > limit - bytes) return invalid("Expanded YAML exceeds the configured limit. Nothing was sent.");
                bytes += extra;
                if (node.IsMap()) object[key] = value.json; else array.append(value.json);
            }
            json = node.IsMap() ? QJsonValue(object) : QJsonValue(array);
        } else if (node.IsNull()) { json = QJsonValue::Null; bytes = 4; }
        else {
            const auto text = QString::fromStdString(node.Scalar());
            bool boolean = false; long long integer = 0; unsigned long long unsignedInteger = 0; double number = 0;
            const bool implicit = tag == "?" || tag.empty();
            const auto digits = text.startsWith('+') || text.startsWith('-') ? text.mid(1) : text;
            const bool integerLiteral = !digits.isEmpty() && std::all_of(digits.cbegin(), digits.cend(), [](QChar c) { return c >= '0' && c <= '9'; });
            if (tag == "!" || tag == "tag:yaml.org,2002:str") json = text;
            else if ((implicit || tag == "tag:yaml.org,2002:bool") && YAML::convert<bool>::decode(node, boolean)) json = boolean;
            else if ((implicit || tag == "tag:yaml.org,2002:int") && YAML::convert<long long>::decode(node, integer)) json = QJsonValue(static_cast<qint64>(integer));
            else if ((implicit || tag == "tag:yaml.org,2002:int") && (integerLiteral || YAML::convert<unsigned long long>::decode(node, unsignedInteger))) return invalid("An integer exceeds the supported signed 64-bit range. Nothing was sent.");
            else if ((implicit || tag == "tag:yaml.org,2002:float") && YAML::convert<double>::decode(node, number)) {
                if (!std::isfinite(number)) return invalid("Non-finite numbers cannot be sent as Kubernetes JSON. Nothing was sent.");
                json = number;
            } else if (tag != "?" && !tag.empty()) return invalid("A YAML scalar does not match its explicit type. Nothing was sent.");
            else json = text;
            bytes = scalarBytes(json);
        }
        if (bytes > limit) return invalid("Expanded YAML exceeds the configured limit. Nothing was sent.");
        values.insert(mark, {json, bytes});
    }
    return values.value(parsed.root.Mark().pos).json.toObject();
}
} // namespace
YamlDraftCheck checkYamlDraft(const QString& text, const QJsonObject& original) {
    const auto result = parseDraft(text, original);
    if (const auto* failure = std::get_if<Failure>(&result)) return {false, failure->message};
    return {true, "Local YAML syntax and target identity checked. Not sent; server schema and apply safety are not checked."};
}
Result<PreparedYaml> prepareYaml(const QString& text, const QJsonObject& original, qint64 limit, const QJsonObject& hiddenDraft) {
    if (limit <= 0 || text.size() > limit || text.toUtf8().size() > limit) return invalid("YAML exceeds the configured limit. Nothing was sent.");
    const auto parsed = parseDraft(text, original);
    if (const auto* failure = std::get_if<Failure>(&parsed)) return *failure;
    const auto converted = convert(std::get<Parsed>(parsed), limit);
    if (const auto* failure = std::get_if<Failure>(&converted)) return *failure;
    const auto desired = std::get<QJsonObject>(converted);
    const auto a = original["metadata"].toObject(), b = desired["metadata"].toObject();
    for (const auto* field : {"apiVersion", "kind"}) if (desired[field] != original[field]) return invalid("Target identity must retain its original JSON string type. Nothing was sent.");
    for (const auto* field : {"name", "uid", "resourceVersion"}) if (a[field] != b[field]) return invalid("Target metadata must retain its original JSON string types. Nothing was sent.");
    if (a["namespace"].toString() != b["namespace"].toString() || (b.contains("namespace") && !b["namespace"].isString())) return invalid("Target namespace must retain its original JSON string type. Nothing was sent.");
    return finishYaml(desired, original, limit, hiddenDraft);
}
bool Workspace::checkYamlDraft() {
    if (!yamlDraft_ || pendingLeave_ || yamlChecking_ || yamlApplyLocked()) return false;
    const qint64 limit = static_cast<qint64>(settings_.yamlLimitMiB) * 1048576;
    if (yamlDraft_->text.size() > limit || yamlDraft_->text.toUtf8().size() > limit) {
        yamlCheckStatus_ = "YAML exceeds the configured limit. Nothing was sent."; emit yamlCheckChanged(); return false;
    }
    const auto session = yamlDraft_->session, path = yamlDraft_->path, text = yamlDraft_->text;
    const auto baseline = yamlDraft_->document;
    const auto token = QUuid::createUuid().toString(QUuid::WithoutBraces); yamlCheckToken_ = token;
    yamlChecking_ = true; yamlCheckStatus_ = "Checking local YAML syntax and target identity. Nothing sent."; emit yamlCheckChanged();
    auto* watcher = new QFutureWatcher<YamlDraftCheck>(this);
    connect(watcher, &QFutureWatcher<YamlDraftCheck>::finished, this, [this, watcher, session, path, text, token] {
        const auto result = watcher->result(); watcher->deleteLater(); yamlChecking_ = false;
        const bool relevant = yamlCheckToken_ == token; yamlCheckToken_.clear();
        if (relevant && yamlDraft_ && yamlDraft_->session == session && yamlDraft_->path == path && yamlDraft_->text == text) yamlCheckStatus_ = result.message;
        emit yamlCheckChanged();
    });
    watcher->setFuture(QtConcurrent::run([text, baseline] { return podlord::checkYamlDraft(text, baseline); }));
    return true;
}
bool Workspace::clearYamlCheck() {
    yamlCheckToken_.clear();
    if (yamlCheckStatus_.isEmpty()) return false;
    yamlCheckStatus_.clear(); emit yamlCheckChanged(); return true;
}
} // namespace podlord
