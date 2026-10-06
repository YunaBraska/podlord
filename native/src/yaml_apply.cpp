#include "yaml_apply.h"
#include "resource_client.h"
#include <QJsonDocument>
#include <QSet>
#include <yaml-cpp/yaml.h>
#include <algorithm>

namespace podlord {
void emitYaml(YAML::Emitter& emitter, const QJsonValue& value) {
    if (value.isObject()) {
        emitter << YAML::BeginMap;
        const auto object = value.toObject();
        for (auto entry = object.constBegin(); entry != object.constEnd(); ++entry) {
            emitter << YAML::Key << YAML::DoubleQuoted << entry.key().toStdString() << YAML::Value;
            emitYaml(emitter, entry.value());
        }
        emitter << YAML::EndMap;
    } else if (value.isArray()) {
        emitter << YAML::BeginSeq;
        for (const auto& item : value.toArray()) emitYaml(emitter, item);
        emitter << YAML::EndSeq;
    } else if (value.isString()) emitter << YAML::DoubleQuoted << value.toString().toStdString();
    else if (value.isBool()) emitter << value.toBool();
    else if (value.isDouble()) {
        const auto integer = value.toInteger();
        if (value.toDouble() == static_cast<double>(integer)) emitter << static_cast<long long>(integer);
        else emitter << value.toDouble();
    } else emitter << YAML::Null;
}
QString resourceYaml(QJsonObject document) {
    if (document.isEmpty()) return {};
    if (isCoreValueResource(document) && document["kind"] == "Secret") {
        for (const auto& field : {"data", "stringData"}) {
            if (!document.contains(field)) continue;
            auto values = document[field].toObject();
            for (auto entry = values.begin(); entry != values.end(); ++entry) entry.value() = "[hidden]";
            document[field] = values;
        }
        auto metadata = document["metadata"].toObject();
        auto annotations = metadata["annotations"].toObject();
        if (annotations.contains("kubectl.kubernetes.io/last-applied-configuration")) {
            annotations["kubectl.kubernetes.io/last-applied-configuration"] = "[hidden]";
            metadata["annotations"] = annotations; document["metadata"] = metadata;
        }
    }
    YAML::Emitter emitter; emitter.SetDoublePrecision(17);
    emitYaml(emitter, document);
    return QString::fromUtf8(emitter.c_str()) + '\n';
}
namespace {
QString pointerKey(QString key) { return key.replace('~', "~0").replace('/', "~1"); }
QJsonArray changes(const QJsonValue& before, const QJsonValue& after, const QString& path = {}) {
    if (before == after) return {};
    if (before.isObject() && after.isObject()) {
        QJsonArray result;
        const auto a = before.toObject(), b = after.toObject();
        QSet<QString> keys;
        for (auto it = a.begin(); it != a.end(); ++it) keys.insert(it.key());
        for (auto it = b.begin(); it != b.end(); ++it) keys.insert(it.key());
        auto sorted = keys.values(); std::sort(sorted.begin(), sorted.end());
        for (const auto& key : sorted)
            for (const auto& change : changes(a.value(key), b.value(key), path + '/' + pointerKey(key))) result.append(change);
        return result;
    }
    QJsonObject operation{{"op", after.isUndefined() ? "remove" : before.isUndefined() ? "add" : "replace"}, {"path", path}};
    if (!after.isUndefined()) operation["value"] = after;
    return {operation};
}
bool secretPath(const QJsonObject& document, const QString& path) {
    return isCoreValueResource(document) && document["kind"] == "Secret"
        && (path == "/data" || path.startsWith("/data/") || path == "/stringData" || path.startsWith("/stringData/")
            || path == "/metadata/annotations" || path == "/metadata"
            || path == "/metadata/annotations/kubectl.kubernetes.io~1last-applied-configuration");
}
QJsonValue atPointer(QJsonValue document, const QString& path) {
    for (auto key : path.mid(1).split('/')) {
        key.replace("~1", "/").replace("~0", "~");
        if (!document.isObject()) return QJsonValue::Undefined;
        document = document.toObject().value(key);
    }
    return document;
}
QString display(const QJsonValue& value) {
    if (value.isUndefined()) return "(absent)";
    const auto encoded = QJsonDocument(QJsonArray{value}).toJson(QJsonDocument::Compact);
    return QString::fromUtf8(encoded.mid(1, encoded.size() - 2));
}
QJsonValue merge(const QJsonValue& base, const QJsonValue& mine, const QJsonValue& server, const QString& path,
                 const QMap<QString, bool>& choices, QStringList& conflicts) {
    if (mine == base) return server;
    if (server == base || mine == server) return mine;
    if (base.isObject() && mine.isObject() && server.isObject()) {
        const auto a = base.toObject(), b = mine.toObject(), c = server.toObject();
        QSet<QString> keys;
        for (auto it = a.begin(); it != a.end(); ++it) keys.insert(it.key());
        for (auto it = b.begin(); it != b.end(); ++it) keys.insert(it.key());
        for (auto it = c.begin(); it != c.end(); ++it) keys.insert(it.key());
        auto sorted = keys.values(); std::sort(sorted.begin(), sorted.end());
        QJsonObject result;
        for (const auto& key : sorted) {
            const auto value = merge(a.value(key), b.value(key), c.value(key), path + '/' + pointerKey(key), choices, conflicts);
            if (!value.isUndefined()) result[key] = value;
        }
        return result;
    }
    if (choices.contains(path)) return choices[path] ? mine : server;
    conflicts.append(path);
    return server;
}
} // namespace

bool yamlChangesObserved(const QJsonObject& original, const QJsonObject& desired, const QJsonObject& current) {
    const auto originalMetadata = original["metadata"].toObject(), currentMetadata = current["metadata"].toObject();
    if (currentMetadata["uid"] != originalMetadata["uid"] || current["apiVersion"] != original["apiVersion"]
        || current["kind"] != original["kind"] || currentMetadata["name"] != originalMetadata["name"]
        || currentMetadata["namespace"] != originalMetadata["namespace"]) return false;
    for (const auto& value : changes(original, desired)) {
        const auto operation = value.toObject();
        const auto actual = atPointer(current, operation["path"].toString());
        if (operation["op"] == "remove" ? !actual.isUndefined() : actual != operation["value"]) return false;
    }
    return true;
}
YamlMerge mergeYaml(const QJsonObject& original, const QJsonObject& desired, const QJsonObject& current, const QMap<QString, bool>& choices) {
    YamlMerge result;
    result.document = merge(original, desired, current, {}, choices, result.conflicts).toObject();
    return result;
}
// Candidate assembly belongs to the parser translation unit; patch construction stays JSON-only.
Result<PreparedYaml> finishYaml(QJsonObject desired, const QJsonObject& original, qint64 byteLimit, const QJsonObject& hiddenDraft) {
    qint64 expandedBytes = QJsonDocument(desired).toJson(QJsonDocument::Compact).size();
    const auto restore = [&](QJsonValue before, QJsonValue retained) {
        if (!retained.isString() || retained.toString().size() > byteLimit) return false;
        const auto encodedSize = [](QJsonValue value) { return QJsonDocument(QJsonArray{value}).toJson(QJsonDocument::Compact).size() - 2; };
        const auto extra = encodedSize(retained) - encodedSize(before);
        if (extra > byteLimit - expandedBytes) return false;
        expandedBytes += extra; return true;
    };
    if (isCoreValueResource(original) && original["kind"] == "Secret") {
        const auto source = hiddenDraft.isEmpty() ? original : hiddenDraft;
        for (const auto* field : {"data", "stringData"}) {
            if (!desired.contains(field)) continue;
            if (!desired[field].isObject()) return Failure{StoreError::InvalidInput, "Secret values must be string mappings. Nothing was sent."};
            auto values = desired[field].toObject();
            const auto retained = source[field].toObject();
            for (auto it = values.begin(); it != values.end(); ++it) {
                if (!it.value().isString()) return Failure{StoreError::InvalidInput, "Secret values must be strings. Nothing was sent."};
                if (it.value() == "[hidden]") {
                    if (!retained.contains(it.key())) return Failure{StoreError::InvalidInput, "A new Secret value cannot be a masked placeholder. Nothing was sent."};
                    if (!restore(it.value(), retained[it.key()])) return Failure{StoreError::InvalidInput, "Restored Secret exceeds the configured YAML limit. Nothing was sent."};
                    it.value() = retained[it.key()];
                }
            }
            desired[field] = values;
        }
        auto metadata = desired["metadata"].toObject();
        auto annotations = metadata["annotations"].toObject();
        const QString key = "kubectl.kubernetes.io/last-applied-configuration";
        if (annotations[key] == "[hidden]") {
            const auto retained = source["metadata"].toObject()["annotations"].toObject();
            if (!retained.contains(key)) return Failure{StoreError::InvalidInput, "A masked annotation has no original value. Nothing was sent."};
            if (!restore(annotations[key], retained[key])) return Failure{StoreError::InvalidInput, "Restored annotation exceeds the configured YAML limit. Nothing was sent."};
            annotations[key] = retained[key]; metadata["annotations"] = annotations; desired["metadata"] = metadata;
        }
    }
    // The parser already bounded expanded JSON. Mask restoration may add original Secret bytes.
    if (expandedBytes > byteLimit) return Failure{StoreError::InvalidInput, "Expanded resource exceeds the configured YAML limit. Nothing was sent."};
    if (desired.value("apiVersion") == "v1" && desired.value("kind") == "Secret" &&
        desired.contains("stringData")) {
        auto data = desired.value("data").toObject();
        const auto input = desired.value("stringData").toObject();
        desired.remove("stringData");
        if (!input.isEmpty() && !desired.contains("data")) desired.insert("data", data);
        qint64 canonicalBytes = QJsonDocument(desired).toJson(QJsonDocument::Compact).size();
        for (auto entry = input.begin(); entry != input.end(); ++entry) {
            const auto utf8 = entry.value().toString().toUtf8();
            const qint64 encodedBytes = ((utf8.size() + 2) / 3) * 4;
            qint64 delta = encodedBytes + 2;
            const auto previous = data.constFind(entry.key());
            if (previous != data.constEnd())
                delta -= QJsonDocument(QJsonArray{previous.value()}).toJson(QJsonDocument::Compact).size() - 2;
            else
                delta += QJsonDocument(QJsonArray{entry.key()}).toJson(QJsonDocument::Compact).size() - 2
                    + 1 + (data.isEmpty() ? 0 : 1);
            if (delta > byteLimit - canonicalBytes)
                return Failure{StoreError::InvalidInput, "Expanded Secret data exceeds the configured YAML limit. Nothing was sent."};
            canonicalBytes += delta;
            data.insert(entry.key(), QString::fromLatin1(utf8.toBase64()));
        }
        if (!input.isEmpty()) desired.insert("data", data);
    }
    const auto operations = changes(original, desired);
    if (operations.isEmpty()) return Failure{StoreError::InvalidInput, "No resource fields changed. Nothing was sent."};
    const auto metadata = original["metadata"].toObject();
    QJsonArray patch{QJsonObject{{"op", "test"}, {"path", "/metadata/uid"}, {"value", metadata["uid"]}},
                     QJsonObject{{"op", "test"}, {"path", "/metadata/resourceVersion"}, {"value", metadata["resourceVersion"]}}};
    qint64 bytes = QJsonDocument(patch).toJson(QJsonDocument::Compact).size();
    QString preview;
    for (const auto& value : operations) {
        const auto operation = value.toObject();
        const auto size = QJsonDocument(operation).toJson(QJsonDocument::Compact).size() + 1;
        if (size > byteLimit - bytes) return Failure{StoreError::InvalidInput, "Patch exceeds the configured YAML limit. Nothing was sent."};
        bytes += size; patch.append(operation);
        const auto path = operation["path"].toString();
        const bool hidden = secretPath(original, path);
        preview += operation["op"].toString() + " " + path + "\n- "
            + (hidden ? QString("[hidden]") : display(atPointer(original, path))) + "\n+ "
            + (hidden ? QString("[hidden]") : display(operation.value("value"))) + "\n\n";
    }
    return PreparedYaml{std::move(desired), std::move(patch), std::move(preview)};
}
} // namespace podlord
