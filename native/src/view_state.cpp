#include "view_state.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QLockFile>
#include <QSaveFile>

namespace podlord {
int resourceDisplayLimit(const QString& text) {
    bool valid = false;
    const int value = text.trimmed().toInt(&valid);
    return !valid || value <= 0 ? 256 : std::min(value, 5000);
}
namespace {
bool validLimit(const QJsonValue& value) {
    return value.isDouble() && value.toDouble() == value.toInt() && value.toInt() >= 1 && value.toInt() <= 5000;
}
bool validSession(const QString& session) {
    const auto id = QUuid::fromString(session);
    return !id.isNull() && id.toString(QUuid::WithoutBraces) == session;
}
bool validState(const TableViewState& value, const QStringList& columns) {
    if (value.limit < 1 || value.limit > 5000) return false;
    if (!QStringList{"", "problems", "activity"}.contains(value.mode)) return false;
    if (value.column.isEmpty() ? value.descending : !columns.contains(value.column)) return false;
    for (auto field = value.fields.cbegin(); field != value.fields.cend(); ++field)
        if (!columns.contains(field.key()) || field.value().isEmpty()) return false;
    return true;
}
QJsonObject encode(const TableViewState& value) {
    QJsonObject fields;
    for (auto field = value.fields.cbegin(); field != value.fields.cend(); ++field) fields.insert(field.key(), field.value());
    return {{"filter", value.filter}, {"column", value.column}, {"descending", value.descending}, {"fields", fields}, {"mode", value.mode}, {"limit", value.limit}};
}
}
ViewStateStore::ViewStateStore(QString profile, TableSchemas schemas) : profile_(std::move(profile)), schemas_(std::move(schemas)) {}
Result<TableViewStates> ViewStateStore::load(const QString& session) const {
    if (!validSession(session) || schemas_.isEmpty()) return Failure{StoreError::InvalidInput, "Choose a valid session and supported table types."};
    const auto folder = QDir(profile_).filePath("views");
    if (const auto failure = profileFailure(profile_)) return *failure;
    if (const auto failure = profileFailure(folder)) return *failure;
    const auto path = QDir(folder).filePath(session + ".json");
    if (QFileInfo(path).isSymLink()) return Failure{StoreError::InvalidData, "Saved views must not be symbolic links."};
    TableViewStates result;
    for (auto table = schemas_.cbegin(); table != schemas_.cend(); ++table) result.insert(table.key(), {});
    if (!QFileInfo::exists(path)) return result;
    QFile file(path);
    if (!QFileInfo(path).isFile() || !file.open(QIODevice::ReadOnly) || file.size() > 65536)
        return Failure{StoreError::ReadFailed, "Cannot read private session views."};
    const auto bytes = file.readAll();
    if (file.error() != QFileDevice::NoError) return Failure{StoreError::ReadFailed, "Cannot read private session views."};
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(bytes, &error);
    const auto root = document.object();
    if (error.error != QJsonParseError::NoError || !document.isObject() || root.size() != 2 || !root["version"].isDouble() || !root["views"].isObject())
        return Failure{StoreError::InvalidData, "Invalid saved views; existing data was retained."};
    const bool legacy = root["version"] == 1;
    const bool limits = root["version"] == 6;
    const bool modes = root["version"] == 3 || root["version"] == 4 || root["version"] == 5 || limits;
    if (!legacy && root["version"] != 2 && !modes) return Failure{StoreError::UnsupportedVersion, "Unsupported saved-view version; existing data was retained."};
    const auto views = root["views"].toObject();
    QStringList added;
    for (auto table = schemas_.cbegin(); table != schemas_.cend(); ++table)
        if (!views.contains(table.key()) && ((root["version"].toInt() <= 3
            && QStringList{"port", "inspectorEvent", "inspectorLink"}.contains(table.key()))
            || (root["version"].toInt() <= 4 && table.key() == "value"))) added.append(table.key());
    for (const auto& table : views.keys()) if (!schemas_.contains(table)) return Failure{StoreError::InvalidData, "Unsupported saved table; existing data was retained."};
    if (views.size() + added.size() != schemas_.size()) return Failure{StoreError::InvalidData, "Saved views must describe the supported table types."};
    for (auto table = schemas_.cbegin(); table != schemas_.cend(); ++table) {
        if (added.contains(table.key())) continue;
        const auto value = views[table.key()].toObject();
        if (!views[table.key()].isObject() || value.size() != (limits ? 6 : legacy ? 3 : modes ? 5 : 4) || !value["filter"].isString() || !value["column"].isString() || !value["descending"].isBool()
            || (!legacy && !value["fields"].isObject()) || (modes && !value["mode"].isString()) || (limits && !validLimit(value["limit"])))
            return Failure{StoreError::InvalidData, "Invalid saved filter or sort; existing data was retained."};
        TableViewState state{value["filter"].toString(), value["column"].toString(), value["descending"].toBool(), {}};
        state.mode = value["mode"].toString();
        if (limits) state.limit = value["limit"].toInt();
        const auto fields = value["fields"].toObject();
        for (auto field = fields.begin(); field != fields.end(); ++field) {
            if (!field.value().isString() || field.value().toString().isEmpty() || !table.value().contains(field.key()))
                return Failure{StoreError::InvalidData, "Invalid saved field filter; existing data was retained."};
            state.fields.insert(field.key(), field.value().toString());
        }
        if (!validState(state, table.value()) || (table.key() != "resource" && (!state.mode.isEmpty() || state.limit != 256))) return Failure{StoreError::InvalidData, "Unsupported saved column, mode or resource limit."};
        result[table.key()] = state;
    }
    return result;
}
Result<TableViewStates> ViewStateStore::save(const QString& session, const QString& table,
    const TableViewState& value, const TableViewState& expected) const {
    if (!validSession(session) || !schemas_.contains(table) || !validState(value, schemas_.value(table)) || (table != "resource" && (!value.mode.isEmpty() || value.limit != 256)))
        return Failure{StoreError::InvalidInput, "Choose a valid session, table and sort column."};
    const auto folder = QDir(profile_).filePath("views");
    if (const auto failure = profileFailure(profile_)) return *failure;
    if (const auto failure = profileFailure(folder)) return *failure;
    const bool profileCreated = !QFileInfo::exists(profile_);
    const bool folderCreated = !QFileInfo::exists(folder);
    if (!QDir().mkpath(folder)
        || (profileCreated && !QFile::setPermissions(profile_, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner))
        || (folderCreated && !QFile::setPermissions(folder, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner)))
        return Failure{StoreError::WriteFailed, "Cannot create private saved-view storage."};
    const auto path = QDir(folder).filePath(session + ".json");
    if (QFileInfo(path + ".lock").isDir() || QFileInfo(path + ".lock").isSymLink())
        return Failure{StoreError::WriteFailed, "Saved-view lock path is not a regular lock file."};
    QLockFile lock(path + ".lock"); lock.setStaleLockTime(0);
    if (!lock.tryLock(0)) return Failure{lock.error() == QLockFile::LockFailedError ? StoreError::Busy : StoreError::WriteFailed, "Cannot lock saved views; reload before retrying."};
    const auto current = load(session);
    if (const auto* failure = std::get_if<Failure>(&current)) return *failure;
    auto states = std::get<TableViewStates>(current);
    if (states.value(table) != expected) return Failure{StoreError::Conflict, "This table's saved view changed in another window. Reload saved filters and sorts before editing them again."};
    if (states.value(table) == value) return states;
    states[table] = value;
    QJsonObject encoded;
    for (auto state = states.cbegin(); state != states.cend(); ++state) encoded[state.key()] = encode(state.value());
    const auto bytes = QJsonDocument(QJsonObject{{"version", 6}, {"views", encoded}}).toJson(QJsonDocument::Indented);
    if (bytes.size() > 65536) return Failure{StoreError::InvalidInput, "The saved session view exceeds the 64 KiB document boundary."};
    QSaveFile file(path); file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly) || !file.setPermissions(QFile::ReadOwner | QFile::WriteOwner)
        || file.write(bytes) != bytes.size() || !file.commit()) return Failure{StoreError::WriteFailed, "Cannot atomically save session filters and sorts."};
    return states;
}
namespace {
QJsonObject encodePresets(const TableViewStates& presets) {
    QJsonObject entries;
    for (auto entry = presets.cbegin(); entry != presets.cend(); ++entry) entries[entry.key()] = encode(entry.value());
    return {{"version", 2}, {"presets", entries}};
}
Result<TableViewStates> decodePresets(const QJsonObject& root, const QStringList& columns) {
    const bool limits = root["version"] == 2;
    if (root.size() != 2 || (root["version"] != 1 && !limits) || !root["presets"].isObject())
        return Failure{StoreError::InvalidData, "Invalid or unsupported filter presets; existing data was retained."};
    TableViewStates result; QSet<QString> names;
    const auto entries = root["presets"].toObject();
    for (auto entry = entries.begin(); entry != entries.end(); ++entry) {
        const auto name = entry.key(); const auto value = entry.value().toObject();
        if (name.isEmpty() || name != name.trimmed() || name.size() > 128 || names.contains(name.toCaseFolded())
            || !entry.value().isObject() || value.size() != (limits ? 6 : 5) || !value["filter"].isString() || value["column"] != ""
            || value["descending"] != false || !value["fields"].isObject() || !value["mode"].isString() || (limits && !validLimit(value["limit"])))
            return Failure{StoreError::InvalidData, "Invalid filter preset name or record; existing data was retained."};
        names.insert(name.toCaseFolded());
        TableViewState state{value["filter"].toString(), {}, false, {}, value["mode"].toString()};
        if (limits) state.limit = value["limit"].toInt();
        const auto fields = value["fields"].toObject();
        for (auto field = fields.begin(); field != fields.end(); ++field) {
            if (!field.value().isString()) return Failure{StoreError::InvalidData, "Invalid preset field expression."};
            state.fields.insert(field.key(), field.value().toString());
        }
        if (!validState(state, columns)) return Failure{StoreError::InvalidData, "Unsupported preset field or mode."};
        result.insert(name, state);
    }
    if (!result.contains("default") || result.value("default") != TableViewState{})
        return Failure{StoreError::InvalidData, "The default filter preset cannot be renamed, replaced or deleted."};
    return result;
}
Result<TableViewStates> decodeLegacyPresets(const QJsonArray& source, const QStringList& columns) {
    const QMap<QString, QString> fields{{"nameFilter", "name"}, {"namespace", "namespace"}, {"kind", "kind"},
        {"cluster", "cluster"}, {"status", "status"}, {"issue", "issue"}, {"age", "createdAt"}, {"node", "node"},
        {"image", "image"}, {"ready", "ready"}, {"restarts", "restarts"}, {"owner", "owner"},
        {"cpu", "cpu"}, {"memory", "memory"}, {"storage", "storage"}};
    TableViewStates result{{"default", {}}};
    QSet<QString> names;
    for (const auto& entry : source) {
        if (!entry.isObject()) return Failure{StoreError::InvalidData, "Invalid legacy filter record; the original file was retained."};
        const auto record = entry.toObject();
        const auto name = record["name"].toString();
        if (!record["name"].isString() || names.contains(name.toCaseFolded()))
            return Failure{StoreError::InvalidData, "Invalid or duplicate legacy filter name; the original file was retained."};
        names.insert(name.toCaseFolded());
        for (auto field = record.begin(); field != record.end(); ++field) {
            const bool mode = field.key() == "problemsOnly" || field.key() == "activityOnly";
            if (mode ? !field->isBool() : !field->isString() && !field->isNull())
                return Failure{StoreError::InvalidData, "Invalid legacy filter field; the original file was retained."};
            if (!mode && !fields.contains(field.key()) && !QStringList{"name", "search", "id", "limit"}.contains(field.key()))
                return Failure{StoreError::InvalidData, "Unsupported legacy filter field; the original file was retained."};
        }
        if (record["problemsOnly"].toBool() && record["activityOnly"].toBool())
            return Failure{StoreError::InvalidData, "Conflicting legacy Problems/Activity modes; review the original preset before importing it."};
        TableViewState value{record["search"].toString(), {}, false, {},
            record["activityOnly"].toBool() ? "activity" : record["problemsOnly"].toBool() ? "problems" : ""};
        for (auto field = fields.cbegin(); field != fields.cend(); ++field)
            if (const auto expression = record[field.key()].toString(); !expression.isEmpty()) value.fields[field.value()] = expression;
        value.limit = resourceDisplayLimit(record["limit"].toString());
        // The reference clears Id on load; Limit is a display cap, not a cache predicate.
        result[name.compare("default", Qt::CaseInsensitive) == 0 ? QString("default") : name] = value;
    }
    return decodePresets(encodePresets(result), columns);
}
Result<TableViewStates> readPresets(const QString& path, const QStringList& columns) {
    if (QFileInfo(path).isSymLink()) return Failure{StoreError::InvalidData, "Filter presets must not be a symbolic link."};
    if (!QFileInfo(path).isFile()) return Failure{StoreError::ReadFailed, "Choose a readable regular saved-filter JSON file."};
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() > 65536) return Failure{StoreError::ReadFailed, "Cannot read private filter presets."};
    const auto bytes = file.read(65537); QJsonParseError error;
    if (bytes.size() > 65536) return Failure{StoreError::ReadFailed, "The saved-filter file exceeds the 64 KiB document boundary."};
    const auto document = QJsonDocument::fromJson(bytes, &error);
    if (file.error() != QFileDevice::NoError || error.error != QJsonParseError::NoError || (!document.isObject() && !document.isArray()))
        return Failure{StoreError::InvalidData, "Invalid filter presets; existing data was retained."};
    if (document.isArray()) return decodeLegacyPresets(document.array(), columns);
    return decodePresets(document.object(), columns);
}
}
Result<TableViewStates> ViewStateStore::loadPresets() const {
    if (const auto failure = profileFailure(profile_)) return *failure;
    const auto path = QDir(profile_).filePath("filter-presets.json");
    if (!QFileInfo::exists(path) && !QFileInfo(path).isSymLink()) return TableViewStates{{"default", {}}};
    return readPresets(path, schemas_.value("resource"));
}
Result<TableViewStates> ViewStateStore::importPresets(const QString& path, const TableViewStates& expected) const {
    if (!QDir::isAbsolutePath(path)) return Failure{StoreError::InvalidInput, "Choose an absolute saved-filter file."};
    const auto imported = readPresets(path, schemas_.value("resource"));
    if (const auto* failure = std::get_if<Failure>(&imported)) return *failure;
    auto desired = expected;
    const auto& presets = std::get<TableViewStates>(imported);
    for (auto entry = presets.cbegin(); entry != presets.cend(); ++entry) {
        if (entry.key() == "default") continue;
        for (auto saved = expected.cbegin(); saved != expected.cend(); ++saved)
            if (saved.key().compare(entry.key(), Qt::CaseInsensitive) == 0 && (saved.key() != entry.key() || saved.value() != entry.value()))
                return Failure{StoreError::Conflict, "An imported filter name is already in use. Rename it before importing; no filters were changed."};
        desired[entry.key()] = entry.value();
    }
    return savePresets(desired, expected);
}
Result<TableViewStates> ViewStateStore::savePresets(const TableViewStates& desired, const TableViewStates& expected) const {
    const auto root = encodePresets(desired); const auto validated = decodePresets(root, schemas_.value("resource"));
    if (const auto* failure = std::get_if<Failure>(&validated)) return *failure;
    const auto bytes = QJsonDocument(root).toJson();
    if (bytes.size() > 65536) return Failure{StoreError::InvalidInput, "Filter presets exceed the 64 KiB document boundary."};
    if (const auto failure = profileFailure(profile_)) return *failure;
    const bool created = !QFileInfo::exists(profile_);
    if (!QDir().mkpath(profile_) || (created && !QFile::setPermissions(profile_, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner)))
        return Failure{StoreError::WriteFailed, "Cannot create private filter storage."};
    const auto path = QDir(profile_).filePath("filter-presets.json");
    if (QFileInfo(path + ".lock").isSymLink() || QFileInfo(path + ".lock").isDir()) return Failure{StoreError::WriteFailed, "Invalid filter preset lock path."};
    QLockFile lock(path + ".lock"); lock.setStaleLockTime(0);
    if (!lock.tryLock(0)) return Failure{lock.error() == QLockFile::LockFailedError ? StoreError::Busy : StoreError::WriteFailed, "Cannot lock filter presets; retry explicitly."};
    const auto current = loadPresets();
    if (const auto* failure = std::get_if<Failure>(&current)) return *failure;
    if (std::get<TableViewStates>(current) != expected) return Failure{StoreError::Conflict, "Filter presets changed in another window. Reload before saving."};
    if (desired == expected) return desired;
    QSaveFile file(path); file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly) || !file.setPermissions(QFile::ReadOwner | QFile::WriteOwner)
        || file.write(bytes) != bytes.size() || !file.commit()) return Failure{StoreError::WriteFailed, "Cannot atomically save filter presets."};
    return desired;
}
}
