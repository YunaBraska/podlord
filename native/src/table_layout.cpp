#include "table_layout.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QLockFile>
#include <QSaveFile>
#include <QSet>
#include <limits>
#include <algorithm>

namespace podlord {
namespace {
std::optional<TableLayout> decode(const QJsonValue& value, const QStringList& ids) {
    if (!value.isArray() || value.toArray().size() != ids.size()) return {};
    TableLayout result;
    QSet<QString> found;
    bool visible = false;
    for (const auto& entry : value.toArray()) {
        if (!entry.isObject()) return {};
        const auto object = entry.toObject();
        const auto id = object["id"].toString();
        const auto width = object["width"];
        if (object.size() != 4 || !object["id"].isString() || !ids.contains(id) || found.contains(id)
            || !object["visible"].isBool() || !object["pinned"].isBool()
            || !width.isDouble() || width.toDouble() < 1 || width.toDouble() > std::numeric_limits<int>::max()
            || width.toDouble() != width.toInt(-1)
            || (object["pinned"].toBool() && !object["visible"].toBool())) return {};
        found.insert(id);
        visible = visible || object["visible"].toBool();
        result.append({id, object["visible"].toBool(), object["pinned"].toBool(), width.toInt()});
    }
    return visible ? std::optional<TableLayout>(result) : std::nullopt;
}
QJsonArray encode(const TableLayout& layout) {
    QJsonArray result;
    for (const auto& column : layout)
        result.append(QJsonObject{{"id", column.id}, {"visible", column.visible}, {"pinned", column.pinned}, {"width", column.width}});
    return result;
}
} // namespace
TableLayouts defaultTableLayouts(const TableSchemas& schemas) {
    TableLayouts result;
    for (auto table = schemas.cbegin(); table != schemas.cend(); ++table) {
        TableLayout layout;
        for (const auto& id : table.value()) layout.append({id});
        if (table.key()=="resource" && table.value().contains("status") && table.value().contains("createdAt")) {
            const QStringList order{"status","kind","name","namespace","cluster","cpu","memory","storage","createdAt","ready","restarts","node","image","owner","issue","uid"};
            const QMap<QString,int> widths{{"status",140},{"kind",150},{"name",280},{"namespace",150},{"cluster",180},{"cpu",90},{"memory",95},{"storage",95},{"createdAt",90},{"ready",90},{"restarts",95},{"node",190},{"image",230},{"owner",220},{"issue",240},{"uid",280}};
            std::stable_sort(layout.begin(),layout.end(),[&](const auto& a,const auto& b) {
                const auto rank=[&](const QString& id) { const int index=order.indexOf(id); return index<0 ? int(order.size()) : index; };
                return rank(a.id)<rank(b.id);
            });
            for (auto& column:layout) { column.width=widths.value(column.id,170); column.visible=column.id!="issue" && column.id!="uid"; }
        }
        if (table.key() == "port") {
            const QMap<QString, int> widths{{"endpoint",180},{"name",260},{"kind",100},{"namespace",130},{"remotePort",100},{"resolvedPort",110},{"status",110}};
            for (auto& column : layout) column.width = widths.value(column.id,170);
        }
        if (table.key() == "value") {
            const QMap<QString, int> widths{{"name",220},{"encoding",110},{"preview",320},{"copy",190},{"reveal",94}};
            for (auto& column : layout) column.width = widths.value(column.id,170);
        }
        if (table.key() == "alert") {
            const QMap<QString, int> widths{{"enabled",46},{"active",110},{"name",210},{"when",220},{"actions",260},{"soundLabel",220}};
            for (auto& column : layout) column.width = widths.value(column.id,170);
        }
        result.insert(table.key(), layout);
        if (table.key() == "inspectorEvent" || table.key() == "inspectorLink") {
            const QMap<QString, int> widths{{"time",180},{"type",110},{"reason",180},{"count",110},{"message",480},
                {"from",240},{"relation",110},{"to",240},{"namespace",160},{"status",160}};
            for (auto& column : result[table.key()]) column.width = widths.value(column.id,170);
        }
    }
    return result;
}
TableLayoutStore::TableLayoutStore(QString profile, TableSchemas schemas) : profile_(std::move(profile)), schemas_(std::move(schemas)) {}
Result<TableLayouts> TableLayoutStore::load() const {
    if (const auto failure = profileFailure(profile_)) return *failure;
    const auto path = QDir(profile_).filePath("table-layouts.json");
    if (QFileInfo(path).isSymLink()) return Failure{StoreError::InvalidData, "Table layouts must not be a symbolic link."};
    if (!QFileInfo::exists(path)) return defaultTableLayouts(schemas_);
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() > 65536) return Failure{StoreError::ReadFailed, "Cannot read private table layouts."};
    const auto bytes = file.readAll();
    if (file.error() != QFileDevice::NoError) return Failure{StoreError::ReadFailed, "Cannot read private table layouts."};
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(bytes, &error);
    const auto root = document.object();
    if (error.error != QJsonParseError::NoError || !document.isObject() || root.size() != 2 || (root["version"] != 1 && root["version"] != 2 && root["version"] != 3 && root["version"] != 4 && root["version"] != 5 && root["version"] != 6 && root["version"] != 7) || !root["layouts"].isObject())
        return Failure{StoreError::InvalidData, "Invalid table layout document; existing data was retained."};
    const auto values = root["layouts"].toObject();
    QStringList added;
    for (auto table = schemas_.cbegin(); table != schemas_.cend(); ++table)
        if (!values.contains(table.key()) && ((table.key() == "port" && root["version"].toInt() <= 3)
            || ((table.key() == "inspectorEvent" || table.key() == "inspectorLink") && root["version"].toInt() <= 4)
            || (table.key() == "value" && root["version"].toInt() <= 5)
            || (table.key() == "alert" && root["version"].toInt() <= 6))) added.append(table.key());
    if (values.size() + added.size() != schemas_.size()) return Failure{StoreError::InvalidData, "Table layouts must describe the supported table types."};
    for (const auto& key : values.keys()) if (!schemas_.contains(key)) return Failure{StoreError::InvalidData, "Unsupported table layout type; existing data was retained."};
    TableLayouts result;
    for (auto table = schemas_.cbegin(); table != schemas_.cend(); ++table) {
        if (added.contains(table.key())) { result.insert(table.key(), defaultTableLayouts(schemas_).value(table.key())); continue; }
        auto layout = decode(values[table.key()], table.value());
        if (!layout && root["version"] == 1 && table.key() == "resource") {
            const QStringList previous{"name", "kind", "namespace", "status", "node", "image"};
            if (std::all_of(previous.cbegin(), previous.cend(), [&](const auto& id) { return table.value().contains(id); })) {
                layout = decode(values[table.key()], previous);
                if (layout) for (const auto& column : defaultTableLayouts(schemas_).value(table.key())) if (!previous.contains(column.id)) layout->append(column);
            }
        }
        if (!layout && root["version"] == 2 && table.key() == "resource" && table.value().contains("uid")) {
            const QStringList previous{"name", "kind", "namespace", "status", "node", "image", "cluster", "cpu", "memory", "storage", "createdAt", "ready", "restarts", "owner", "issue"};
            if (std::all_of(previous.cbegin(), previous.cend(), [&](const auto& id) { return table.value().contains(id); })) {
                layout = decode(values[table.key()], previous);
                if (layout) layout->append({"uid", false, false, 280});
            }
        }
        if (!layout) return Failure{StoreError::InvalidData, "Invalid column layout; existing data was retained."};
        result.insert(table.key(), *layout);
    }
    return result;
}
Result<TableLayouts> TableLayoutStore::save(const QString& table, const QJsonArray& value, const TableLayout& expected) const {
    if (!schemas_.contains(table)) return Failure{StoreError::InvalidInput, "Choose a supported table type."};
    const auto layout = decode(value, schemas_.value(table));
    if (!layout) return Failure{StoreError::InvalidInput, "Keep at least one column visible; pinned columns must be visible and widths must be positive whole pixels."};
    if (const auto failure = profileFailure(profile_)) return *failure;
    const bool created = !QFileInfo::exists(profile_);
    if (!QDir().mkpath(profile_) || (created && !QFile::setPermissions(profile_, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner)))
        return Failure{StoreError::WriteFailed, "Cannot create a private table layout profile."};
    const auto path = QDir(profile_).filePath("table-layouts.json");
    if (QFileInfo(path + ".lock").isDir()) return Failure{StoreError::WriteFailed, "Table layout lock path is a directory."};
    QLockFile lock(path + ".lock"); lock.setStaleLockTime(0);
    if (!lock.tryLock(0)) return Failure{lock.error() == QLockFile::LockFailedError ? StoreError::Busy : StoreError::WriteFailed, "Cannot lock table layouts; retry explicitly."};
    const auto current = load();
    if (const auto* failure = std::get_if<Failure>(&current)) return *failure;
    auto layouts = std::get<TableLayouts>(current);
    if (layouts.value(table) != expected) return Failure{StoreError::Conflict, "This table layout changed in another window. Reload saved layouts before saving."};
    layouts[table] = *layout;
    QJsonObject encoded;
    for (auto entry = layouts.cbegin(); entry != layouts.cend(); ++entry) encoded[entry.key()] = encode(entry.value());
    const auto bytes = QJsonDocument(QJsonObject{{"version", 7}, {"layouts", encoded}}).toJson(QJsonDocument::Indented);
    QSaveFile file(path); file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly) || !file.setPermissions(QFile::ReadOwner | QFile::WriteOwner) || file.write(bytes) != bytes.size() || !file.commit())
        return Failure{StoreError::WriteFailed, "Cannot atomically save private table layouts."};
    return layouts;
}
} // namespace podlord
