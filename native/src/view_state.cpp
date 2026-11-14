#include "context_catalog.h"
#include "view_state.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QLockFile>
#include <QSaveFile>
#include <QRegularExpression>

namespace podlord {
int resourceDisplayLimit(const QString& text) {
    bool valid = false;
    const int value = text.trimmed().toInt(&valid);
    return !valid || value <= 0 ? DefaultResourceDisplayLimit : std::min(value, 5000);
}
namespace {
bool validLimit(const QJsonValue& value) {
    return value.isDouble() && value.toDouble() == value.toInt() && value.toInt() >= 1 && value.toInt() <= 5000;
}
bool validSession(const QString& session) {
    static const QRegularExpression context("\\Actx:[0-9a-f]{64}\\z");
    return context.match(session).hasMatch();
}
bool validState(const TableViewState& value, const QStringList& columns) {
    if (value.limit < 1 || value.limit > 5000) return false;
    static const QRegularExpression name("^[a-z0-9](?:[a-z0-9-]{0,61}[a-z0-9])?$");
    for (qsizetype index = 0; index < value.namespaces.size(); ++index)
        if (!name.match(value.namespaces[index]).hasMatch() || (index > 0 && value.namespaces[index] <= value.namespaces[index-1])) return false;
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
    if (!validSession(session) || schemas_.isEmpty()) return Failure{StoreError::InvalidInput, "Choose a context and supported table types."};
    const auto folder = QDir(profile_).filePath("views");
    if (const auto failure = profileFailure(profile_)) return *failure;
    if (const auto failure = profileFailure(folder)) return *failure;
    const auto path = QDir(folder).filePath(contextViewName(session));
    if (QFileInfo(path).isSymLink()) return Failure{StoreError::InvalidData, "Saved views must not be symbolic links."};
    TableViewStates result;
    for (auto table = schemas_.cbegin(); table != schemas_.cend(); ++table)
        result.insert(table.key(), table.key() == "resource" ? TableViewState{.limit = DefaultResourceDisplayLimit} : TableViewState{});
    if (!QFileInfo::exists(path)) return result;
    QFile file(path);
    if (!QFileInfo(path).isFile() || !file.open(QIODevice::ReadOnly) || file.size() > 65536)
        return Failure{StoreError::ReadFailed, "Cannot read private context settings."};
    const auto bytes = file.readAll();
    if (file.error() != QFileDevice::NoError) return Failure{StoreError::ReadFailed, "Cannot read private context settings."};
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(bytes, &error);
    const auto root = document.object();
    if (error.error != QJsonParseError::NoError || !document.isObject() || root.size() != 2 || !root["version"].isDouble() || !root["views"].isObject())
        return Failure{StoreError::InvalidData, "Invalid context settings. The file was left unchanged."};
    if (root["version"] != 7)
        return Failure{StoreError::UnsupportedVersion, "Unsupported context settings format. The file was left unchanged."};
    const auto views = root["views"].toObject();
    if (views.size() != schemas_.size()) return Failure{StoreError::InvalidData, "Context settings must describe the supported table types."};
    for (const auto& table : views.keys()) if (!schemas_.contains(table)) return Failure{StoreError::InvalidData, "Unsupported saved table. The file was left unchanged."};
    for (auto table = schemas_.cbegin(); table != schemas_.cend(); ++table) {
        const auto value = views[table.key()].toObject();
        if (!views[table.key()].isObject() || value.size() != 7 || !value["filter"].isString() || !value["column"].isString()
            || !value["descending"].isBool() || !value["fields"].isObject() || !value["mode"].isString() || !validLimit(value["limit"]) || !value["namespaces"].isArray())
            return Failure{StoreError::InvalidData, "Invalid saved filter or sort. The file was left unchanged."};
        TableViewState state{value["filter"].toString(), value["column"].toString(), value["descending"].toBool(), {}};
        state.mode = value["mode"].toString(); state.limit = value["limit"].toInt();
        for (const auto& name : value["namespaces"].toArray()) {
            if (!name.isString()) return Failure{StoreError::InvalidData, "Invalid namespace access scope."};
            state.namespaces.append(name.toString());
        }
        const auto fields = value["fields"].toObject();
        for (auto field = fields.begin(); field != fields.end(); ++field) {
            if (!field.value().isString() || field.value().toString().isEmpty() || !table.value().contains(field.key()))
                return Failure{StoreError::InvalidData, "Invalid saved field filter. The file was left unchanged."};
            state.fields.insert(field.key(), field.value().toString());
        }
        if (!validState(state, table.value()) || (table.key() != "resource" && (!state.mode.isEmpty() || state.limit != 256 || !state.namespaces.isEmpty())))
            return Failure{StoreError::InvalidData, "Unsupported saved column, mode or resource limit."};
        result[table.key()] = state;
    }
    return result;
}
Result<TableViewStates> ViewStateStore::save(const QString& session, const QString& table,
    const TableViewState& value, const TableViewState& expected) const {
    if (!validSession(session) || !schemas_.contains(table) || !validState(value, schemas_.value(table)) || (table != "resource" && (!value.mode.isEmpty() || value.limit != 256)))
        return Failure{StoreError::InvalidInput, "Choose a context, table and sort column."};
    if (table != "resource" && !value.namespaces.isEmpty())
        return Failure{StoreError::InvalidInput, "Namespace access scope belongs to context resources."};
    const auto folder = QDir(profile_).filePath("views");
    if (const auto failure = profileFailure(profile_)) return *failure;
    if (const auto failure = profileFailure(folder)) return *failure;
    const bool profileCreated = !QFileInfo::exists(profile_);
    const bool folderCreated = !QFileInfo::exists(folder);
    if (!QDir().mkpath(folder)
        || (profileCreated && !QFile::setPermissions(profile_, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner))
        || (folderCreated && !QFile::setPermissions(folder, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner)))
        return Failure{StoreError::WriteFailed, "Cannot create private saved-view storage."};
    const auto path = QDir(folder).filePath(contextViewName(session));
    if (QFileInfo(path + ".lock").isDir() || QFileInfo(path + ".lock").isSymLink())
        return Failure{StoreError::WriteFailed, "Saved-view lock path is not a regular lock file."};
    QLockFile lock(path + ".lock"); lock.setStaleLockTime(0);
    if (!lock.tryLock(0)) return Failure{lock.error() == QLockFile::LockFailedError ? StoreError::Busy : StoreError::WriteFailed, "Cannot lock context settings. Try again."};
    {
        const auto catalog = readContextCatalog(profile_);
        if (const auto* failure = std::get_if<Failure>(&catalog)) return *failure;
        const auto& sources = std::get<ContextCatalog>(catalog).sources;
        if (std::none_of(sources.begin(), sources.end(), [&](const auto& ids) { return ids.contains(session); }))
            return Failure{StoreError::NotFound, "This context was removed. Its settings were not recreated."};
    }
    const auto current = load(session);
    if (const auto* failure = std::get_if<Failure>(&current)) return *failure;
    auto states = std::get<TableViewStates>(current);
    if (states.value(table) != expected) return Failure{StoreError::Conflict, "These table settings changed in another window. Reopen the table settings and try again."};
    if (states.value(table) == value) return states;
    states[table] = value;
    QJsonObject encoded;
    for (auto state = states.cbegin(); state != states.cend(); ++state) {
        auto record = encode(state.value());
        {
            QJsonArray namespaces;
            for (const auto& name : state->namespaces) namespaces.append(name);
            record.insert("namespaces", namespaces);
        }
        encoded[state.key()] = record;
    }
    const auto bytes = QJsonDocument(QJsonObject{{"version", 7}, {"views", encoded}}).toJson(QJsonDocument::Indented);
    if (bytes.size() > 65536) return Failure{StoreError::InvalidInput, "The context settings exceeds the 64 KiB document boundary."};
    QSaveFile file(path); file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly) || !file.setPermissions(QFile::ReadOwner | QFile::WriteOwner)
        || file.write(bytes) != bytes.size() || !file.commit()) return Failure{StoreError::WriteFailed, "Cannot atomically save context filters and sorts."};
    return states;
}
namespace {
QJsonObject encodePresets(const TableViewStates& presets) {
    QJsonObject entries;
    for (auto entry = presets.cbegin(); entry != presets.cend(); ++entry) entries[entry.key()] = encode(entry.value());
    return {{"version", 2}, {"presets", entries}};
}
Result<TableViewStates> decodePresets(const QJsonObject& root, const QStringList& columns) {
    if (root.size() != 2 || root["version"] != 2 || !root["presets"].isObject())
        return Failure{StoreError::InvalidData, "Invalid or unsupported filter presets; existing data was retained."};
    TableViewStates result; QSet<QString> names;
    const auto entries = root["presets"].toObject();
    for (auto entry = entries.begin(); entry != entries.end(); ++entry) {
        const auto name = entry.key(); const auto value = entry.value().toObject();
        if (name.isEmpty() || name != name.trimmed() || name.size() > 128 || names.contains(name.toCaseFolded())
            || !entry.value().isObject() || value.size() != 6 || !value["filter"].isString() || value["column"] != ""
            || value["descending"] != false || !value["fields"].isObject() || !value["mode"].isString() || !validLimit(value["limit"]))
            return Failure{StoreError::InvalidData, "Invalid filter preset name or record; existing data was retained."};
        names.insert(name.toCaseFolded());
        TableViewState state{value["filter"].toString(), {}, false, {}, value["mode"].toString()};
        state.limit = value["limit"].toInt();
        const auto fields = value["fields"].toObject();
        for (auto field = fields.begin(); field != fields.end(); ++field) {
            if (!field.value().isString()) return Failure{StoreError::InvalidData, "Invalid preset field expression."};
            state.fields.insert(field.key(), field.value().toString());
        }
        if (!validState(state, columns)) return Failure{StoreError::InvalidData, "Unsupported preset field or mode."};
        result.insert(name, state);
    }
    auto defaultPreset = result.value("default");
    if (!result.contains("default") || defaultPreset != TableViewState{.limit = DefaultResourceDisplayLimit})
        return Failure{StoreError::InvalidData, "The default filter preset cannot be renamed, replaced or deleted."};
    result["default"] = defaultPreset;
    return result;
}
Result<TableViewStates> decodeLegacyPresets(const QJsonArray& source, const QStringList& columns) {
    const QMap<QString, QString> fields{{"nameFilter", "name"}, {"namespace", "namespace"}, {"kind", "kind"},
        {"cluster", "cluster"}, {"status", "status"}, {"issue", "issue"}, {"age", "createdAt"}, {"node", "node"},
        {"image", "image"}, {"ready", "ready"}, {"restarts", "restarts"}, {"owner", "owner"},
        {"cpu", "cpu"}, {"memory", "memory"}, {"storage", "storage"}};
    TableViewStates result{{"default", TableViewState{.limit = DefaultResourceDisplayLimit}}};
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
        value.limit = record.contains("limit") ? resourceDisplayLimit(record["limit"].toString()) : 256;
        // The reference clears Id on load; Limit is a display cap, not a cache predicate.
        const auto key = name.compare("default", Qt::CaseInsensitive) == 0 ? QString("default") : name;
        if (key == "default" && value.limit == 256) value.limit = DefaultResourceDisplayLimit;
        result[key] = value;
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
    if (!QFileInfo::exists(path) && !QFileInfo(path).isSymLink())
        return TableViewStates{{"default", TableViewState{.limit = DefaultResourceDisplayLimit}}};
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
