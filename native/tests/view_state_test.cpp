#include "view_state.h"
#include "context_catalog.h"
#include "kubeconfig_store.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QLockFile>
#include <QTemporaryDir>
#include <cstdio>

namespace {
using namespace podlord;
const TableSchemas schemas{{"resource", {"name", "namespace"}}, {"event", {"eventTime", "eventCount"}}};
bool fails(const Result<TableViewStates>& result, StoreError code) {
    const auto* failure = std::get_if<Failure>(&result);
    return failure && failure->code == code && !failure->message.isEmpty();
}
bool equals(const Result<TableViewStates>& result, const TableViewStates& expected) {
    const auto* value = std::get_if<TableViewStates>(&result); return value && *value == expected;
}
bool write(const QString& path, const QByteArray& bytes) {
    QFile file(path); return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}
bool run(const QString& scenario) {
    QTemporaryDir temporary; if (!temporary.isValid()) return false;
    const auto profile = temporary.filePath("profile"), views = profile + "/views";
    QString session = "ctx:" + QString(64, 'a'), other = "ctx:" + QString(64, 'b');
    QString path = views + "/" + contextViewName(session);
    const auto prepareContext = [&] {
        const auto imported = KubeconfigStore(profile).importText(temporary.filePath("contexts.yaml"),
            "apiVersion: v1\nkind: Config\nclusters:\n- name: local\n  cluster: {server: 'http://127.0.0.1:1'}\nusers:\n- name: local\n  user: {token: local-view-token}\ncontexts:\n- name: first\n  context: {cluster: local, user: local}\n- name: second\n  context: {cluster: local, user: local}\n");
        const auto* source = std::get_if<SourceSnapshot>(&imported);
        if (!source || source->contexts.size() != 2) return false;
        session = source->contexts[0].id; other = source->contexts[1].id;
        path = views + "/" + contextViewName(session);
        return true;
    };
    const ViewStateStore store(profile, schemas);
    const TableViewState resourceDefault{.limit = 1024};
    const TableViewState value{"alpha", "name", true};
    if (scenario == "empty_schema") return fails(ViewStateStore(profile, {}).load(session), StoreError::InvalidInput) && !QFile::exists(profile);
    if (scenario == "namespace_saved" || scenario == "namespace_event_input") {
        if (!prepareContext()) return false;
        auto scoped = value; scoped.namespaces = {"team"};
        if (scenario == "namespace_event_input") {
            scoped.column.clear(); scoped.descending = false;
            return fails(store.save(session, "event", scoped, {}), StoreError::InvalidInput) && !QFile::exists(path);
        }
        return equals(store.save(session, "resource", scoped, resourceDefault), {{"resource", scoped}, {"event", {}}})
            && equals(store.load(session), {{"resource", scoped}, {"event", {}}});
    }
    if (scenario.startsWith("preset_shape_")) {
        const TableViewStates empty{{"default", resourceDefault}};
        auto desired = empty; desired["Problems"] = {"", "", false, {{"namespace", "team"}}, "problems"};
        if (!equals(store.savePresets(desired, empty), desired)) return false;
        const auto presetPath = profile + "/filter-presets.json";
        QFile file(presetPath); if (!file.open(QIODevice::ReadOnly)) return false;
        auto root = QJsonDocument::fromJson(file.readAll()).object(); file.close();
        auto presets = root["presets"].toObject(), record = presets["Problems"].toObject();
        const auto shape = scenario.mid(QString("preset_shape_").size());
        if (shape == "filter") record["filter"] = false;
        else if (shape == "column") record["column"] = "name";
        else if (shape == "descending") record["descending"] = true;
        else if (shape == "fields") record["fields"] = false;
        else if (shape == "mode") record["mode"] = false;
        else if (shape == "missing") record.remove("filter");
        else if (shape != "entry" && shape != "root" && shape != "extra_root") return false;
        presets["Problems"] = shape == "entry" ? QJsonValue(false) : QJsonValue(record);
        root["presets"] = shape == "root" ? QJsonValue(QJsonArray{}) : QJsonValue(presets);
        if (shape == "extra_root") root["unexpected"] = true;
        const auto bytes = QJsonDocument(root).toJson();
        return write(presetPath, bytes) && fails(store.loadPresets(), StoreError::InvalidData)
            && fails(store.savePresets(empty, desired), StoreError::InvalidData)
            && file.open(QIODevice::ReadOnly) && file.readAll() == bytes;
    }
    if (scenario == "preset_default_immutable") {
        const QJsonObject oldDefault{{"filter", ""}, {"column", ""}, {"descending", false},
            {"fields", QJsonObject{}}, {"mode", ""}, {"limit", 256}};
        const auto bytes = QJsonDocument(QJsonObject{{"version", 2},
            {"presets", QJsonObject{{"default", oldDefault}, {"My 256 rows", oldDefault}}}}).toJson();
        const auto presetPath = profile + "/filter-presets.json";
        if (!QDir().mkpath(profile) || !write(presetPath, bytes)) return false;
        const auto loaded = store.loadPresets();
        QFile original(presetPath);
        return fails(loaded, StoreError::InvalidData)
            && original.open(QIODevice::ReadOnly) && original.readAll() == bytes;
    }
    if (scenario.startsWith("limit_")) {
        if (!prepareContext() || !equals(store.save(session, "resource", value, resourceDefault), {{"resource", value}, {"event", {}}})) return false;
        auto limited = value; limited.limit = 7;
        if (scenario == "limit_invalid_input") { limited.limit = 0; return fails(store.save(session, "resource", limited, value), StoreError::InvalidInput); }
        if (scenario == "limit_event_input") return fails(store.save(session, "event", limited, {}), StoreError::InvalidInput);
        if (scenario == "limit_saved" || scenario == "limit_conflict" || scenario == "limit_other_table") {
            const auto saved = store.save(session, "resource", limited, value);
            if (!std::holds_alternative<TableViewStates>(saved) || !equals(store.load(session), std::get<TableViewStates>(saved))) return false;
            if (scenario == "limit_conflict") return fails(store.save(session, "resource", value, value), StoreError::Conflict);
            if (scenario == "limit_other_table") { const auto merged = store.save(session, "event", {"Warning", "eventTime", false}, {}); return std::holds_alternative<TableViewStates>(merged) && std::get<TableViewStates>(merged)["resource"] == limited; }
            return true;
        }
        QFile input(path); if (!input.open(QIODevice::ReadOnly)) return false;
        auto document = QJsonDocument::fromJson(input.readAll()).object(); input.close();
        auto records = document["views"].toObject(), state = records["resource"].toObject();
        if (scenario == "limit_missing") state.remove("limit");
        else if (scenario == "limit_zero") state["limit"] = 0;
        else if (scenario == "limit_negative") state["limit"] = -1;
        else if (scenario == "limit_large") state["limit"] = 5001;
        else if (scenario == "limit_fraction") state["limit"] = 1.5;
        else if (scenario == "limit_string") state["limit"] = "7";
        else if (scenario == "limit_null") state["limit"] = QJsonValue::Null;
        else return false;
        records["resource"] = state; document["views"] = records;
        const auto bytes = QJsonDocument(document).toJson();
        return write(path, bytes) && fails(store.load(session), StoreError::InvalidData)
            && fails(store.save(session, "resource", limited, value), StoreError::InvalidData)
            && input.open(QIODevice::ReadOnly) && input.readAll() == bytes;
    }
    if (scenario.startsWith("preset_import_")) {
        const auto source = temporary.filePath("saved filters %.json");
        const TableViewStates empty{{"default", resourceDefault}};
        QJsonObject legacy{{"name", "Imported"}, {"nameFilter", "~web"}, {"namespace", "team"}};
        const auto bytes = QJsonDocument(QJsonArray{legacy}).toJson();
        if (!write(source, scenario == "preset_import_malformed" ? QByteArray("{") : scenario == "preset_import_oversized" ? QByteArray(65537, ' ') : bytes)) return false;
        if (scenario == "preset_import_missing") return fails(store.importPresets(temporary.filePath("missing"), empty), StoreError::ReadFailed);
        if (scenario == "preset_import_relative") return fails(store.importPresets("relative.json", empty), StoreError::InvalidInput);
        if (scenario == "preset_import_malformed") return fails(store.importPresets(source, empty), StoreError::InvalidData);
        if (scenario == "preset_import_oversized") return fails(store.importPresets(source, empty), StoreError::ReadFailed);
        auto expected = empty;
        if (scenario == "preset_import_conflict" || scenario == "preset_import_case_conflict") {
            expected[scenario == "preset_import_conflict" ? "Imported" : "imported"] = {"existing", {}, false};
            if (!equals(store.savePresets(expected, empty), expected)) return false;
            return fails(store.importPresets(source, expected), StoreError::Conflict) && equals(store.loadPresets(), expected);
        }
        if (scenario == "preset_import_stale") {
            auto changed = empty; changed["Other"] = {};
            if (!equals(store.savePresets(changed, empty), changed)) return false;
            return fails(store.importPresets(source, empty), StoreError::Conflict) && equals(store.loadPresets(), changed);
        }
        const auto imported = store.importPresets(source, empty);
        if (!std::holds_alternative<TableViewStates>(imported)) return false;
        const auto actual = std::get<TableViewStates>(imported);
        if (actual.value("Imported").fields != QMap<QString, QString>{{"name", "~web"}, {"namespace", "team"}}
            || !equals(store.loadPresets(), actual)) return false;
        QFile original(source);
        if (!original.open(QIODevice::ReadOnly) || original.readAll() != bytes) return false;
        if (scenario == "preset_import_repeat") return equals(store.importPresets(source, actual), actual);
        return scenario == "preset_import_success";
    }
    if (scenario == "legacy_preset_default" || scenario == "legacy_preset_default_invalid") {
        const TableViewStates empty{{"default", resourceDefault}};
        const bool invalid = scenario == "legacy_preset_default_invalid";
        const auto source = temporary.filePath("reference-filters.json");
        const auto bytes = QJsonDocument(QJsonArray{QJsonObject{{"name", "default"}, {"limit", invalid ? "7" : "256"}}}).toJson();
        if (!write(source, bytes)) return false;
        const auto imported = store.importPresets(source, empty);
        QFile original(source);
        return (invalid ? fails(imported, StoreError::InvalidData) && !QFile::exists(profile + "/filter-presets.json")
            : equals(imported, empty) && equals(store.loadPresets(), empty))
            && original.open(QIODevice::ReadOnly) && original.readAll() == bytes;
    }
    if (scenario.startsWith("legacy_preset_")) {
        const auto presetPath = profile + "/filter-presets.json";
        auto full = schemas;
        full["resource"] = {"name", "namespace", "kind", "cluster", "status", "issue", "node", "image", "createdAt", "ready", "restarts", "owner", "cpu", "memory", "storage", "uid"};
        QJsonObject preset{{"name", "Production"}, {"search", "api"}, {"nameFilter", "~web"}, {"namespace", "\"team\""},
            {"age", ">=1h"}, {"cpu", ">500m"}, {"problemsOnly", true}, {"activityOnly", false}, {"limit", "256"}, {"id", ""}};
        if (scenario == "legacy_preset_activity") { preset["problemsOnly"] = false; preset["activityOnly"] = true; }
        if (scenario == "legacy_preset_limit") preset["limit"] = "7";
        if (scenario == "legacy_preset_invalid") preset["cpu"] = 5;
        if (scenario == "legacy_preset_conflicting_modes") preset["activityOnly"] = true;
        QJsonArray source{preset};
        if (scenario == "legacy_preset_duplicate") source.append(preset);
        const auto bytes = QJsonDocument(source).toJson();
        if (!QDir().mkpath(profile) || !write(presetPath, bytes)) return false;
        const ViewStateStore imported(profile, full);
        const auto loaded = imported.loadPresets();
        QFile file(presetPath);
        if (!file.open(QIODevice::ReadOnly) || file.readAll() != bytes) return false;
        if (scenario == "legacy_preset_invalid" || scenario == "legacy_preset_duplicate" || scenario == "legacy_preset_conflicting_modes") return fails(loaded, StoreError::InvalidData);
        if (!std::holds_alternative<TableViewStates>(loaded)) return false;
        auto actual = std::get<TableViewStates>(loaded);
        const auto production = actual.value("Production");
        if (actual.value("default") != resourceDefault || production.filter != "api" || production.column != ""
            || production.mode != (scenario == "legacy_preset_activity" ? "activity" : "problems") || production.limit != (scenario == "legacy_preset_limit" ? 7 : 256)
            || production.fields != QMap<QString, QString>{{"name", "~web"}, {"namespace", "\"team\""}, {"createdAt", ">=1h"}, {"cpu", ">500m"}}) return false;
        if (scenario != "legacy_preset_save") return true;
        auto desired = actual; desired["Renamed"] = desired.take("Production");
        return equals(imported.savePresets(desired, actual), desired) && equals(imported.loadPresets(), desired);
    }
    if (scenario.startsWith("preset_")) {
        const TableViewStates empty{{"default", resourceDefault}};
        auto desired = empty; desired["Problems"] = {"", "", false, {{"namespace", "team"}}, "problems"};
        if (scenario == "preset_missing") return equals(store.loadPresets(), empty) && !QFile::exists(profile);
        if (scenario == "preset_default_delete") { desired.remove("default"); return fails(store.savePresets(desired, empty), StoreError::InvalidData); }
        if (scenario == "preset_default_replace") { desired["default"].filter = "alpha"; return fails(store.savePresets(desired, empty), StoreError::InvalidData); }
        if (scenario == "preset_default_limit") { desired["default"].limit = 7; return fails(store.savePresets(desired, empty), StoreError::InvalidData); }
        if (scenario == "preset_limit_invalid") { desired["Problems"].limit = 0; return fails(store.savePresets(desired, empty), StoreError::InvalidData); }
        if (scenario == "preset_invalid_mode") { desired["Problems"].mode = "unsupported"; return fails(store.savePresets(desired, empty), StoreError::InvalidData); }
        if (scenario == "preset_unknown_field") { desired["Problems"].fields = {{"removed", "alpha"}}; return fails(store.savePresets(desired, empty), StoreError::InvalidData); }
        if (scenario == "preset_case_collision") { desired["problems"] = {}; return fails(store.savePresets(desired, empty), StoreError::InvalidData); }
        if (scenario == "preset_empty_name") { desired[""] = {}; return fails(store.savePresets(desired, empty), StoreError::InvalidData); }
        if (scenario == "preset_large") { desired["Problems"].filter = QString(65536, 'x'); return fails(store.savePresets(desired, empty), StoreError::InvalidInput) && !QFile::exists(profile); }
        if (!equals(store.savePresets(desired, empty), desired)) return false;
        const auto presetPath = profile + "/filter-presets.json";
        if (scenario == "preset_save") return equals(store.loadPresets(), desired);
        if (scenario == "preset_repeat") return equals(store.savePresets(desired, desired), desired);
        if (scenario == "preset_conflict") return fails(store.savePresets(empty, empty), StoreError::Conflict) && equals(store.loadPresets(), desired);
        if (scenario == "preset_busy") { QLockFile lock(presetPath + ".lock"); return lock.tryLock(0) && fails(store.savePresets(empty, desired), StoreError::Busy); }
        if (scenario == "preset_private") return !(QFileInfo(presetPath).permissions() & (QFile::ReadGroup | QFile::WriteGroup | QFile::ReadOther | QFile::WriteOther));
        if (scenario == "preset_symlink") {
            const auto target = temporary.filePath("target");
            return write(target, "{}") && QFile::remove(presetPath) && QFile::link(target, presetPath)
                && fails(store.loadPresets(), StoreError::InvalidData) && fails(store.savePresets(empty, desired), StoreError::InvalidData);
        }
        const auto bytes = scenario == "preset_malformed" ? QByteArray("{") : scenario == "preset_future" ? QByteArray("{\"version\":3,\"presets\":{}}") : QByteArray(65537, ' ');
        const auto code = scenario == "preset_oversized" ? StoreError::ReadFailed : StoreError::InvalidData;
        if (!write(presetPath, bytes) || !fails(store.loadPresets(), code) || !fails(store.savePresets(empty, desired), code)) return false;
        QFile input(presetPath); return input.open(QIODevice::ReadOnly) && input.readAll() == bytes;
    }
    if (scenario == "missing") {
        const auto result = store.load(session);
        return std::holds_alternative<TableViewStates>(result) && std::get<TableViewStates>(result).size() == 2
            && std::get<TableViewStates>(result).value("resource") == resourceDefault && !QFile::exists(profile);
    }
    if (scenario == "empty_profile") return fails(ViewStateStore("", schemas).load(session), StoreError::InvalidInput);
    if (scenario == "relative_profile") return fails(ViewStateStore("relative", schemas).load(session), StoreError::InvalidInput);
    if (scenario == "invalid_id") return fails(store.load("../escape"), StoreError::InvalidInput) && !QFile::exists(profile);
    if (scenario == "noncanonical_id") return fails(store.load("{" + session + "}"), StoreError::InvalidInput) && fails(store.load("12345678-1234-1234-1234-123456789abc"), StoreError::InvalidInput);
    if (scenario == "empty_id") return fails(store.save("", "resource", value, {}), StoreError::InvalidInput);
    if (scenario == "unknown_table") return fails(store.save(session, "unknown", value, {}), StoreError::InvalidInput);
    if (scenario == "unknown_column") return fails(store.save(session, "resource", {"", "removed", false}, {}), StoreError::InvalidInput);
    if (scenario == "descending_none") return fails(store.save(session, "resource", {"", "", true}, {}), StoreError::InvalidInput);
    if (scenario == "profile_file") return write(profile, "file") && fails(store.load(session), StoreError::ReadFailed);
    if (!QDir().mkpath(views)) return false;
    if (scenario == "file_directory") return QDir().mkpath(path) && fails(store.load(session), StoreError::ReadFailed);
    if (scenario == "busy") {
        QLockFile lock(path + ".lock"); return lock.tryLock(0) && fails(store.save(session, "resource", value, resourceDefault), StoreError::Busy);
    }
    if (scenario == "lock_directory") return QDir().mkpath(path + ".lock") && fails(store.save(session, "resource", value, resourceDefault), StoreError::WriteFailed);
    if (scenario == "oversized_input" && !prepareContext()) return false;
    if (scenario == "oversized_input") return fails(store.save(session, "resource", {QString(65536, 'x'), "", false}, resourceDefault), StoreError::InvalidInput) && !QFile::exists(path);
#ifdef Q_OS_UNIX
    if (scenario == "profile_symlink") {
        const auto link = temporary.filePath("linked"); return QFile::link(profile, link) && fails(ViewStateStore(link, schemas).load(session), StoreError::InvalidInput);
    }
    if (scenario == "file_symlink") {
        const auto target = temporary.filePath("target"); return write(target, "{}") && QFile::link(target, path) && fails(store.load(session), StoreError::InvalidData);
    }
#endif
    if (!prepareContext()) return false;
    const auto initial = store.save(session, "resource", value, resourceDefault);
    if (!std::holds_alternative<TableViewStates>(initial)) return false;
    const auto saved = std::get<TableViewStates>(initial);
    if (scenario == "mode_save" || scenario == "mode_invalid" || scenario == "mode_event") {
        auto filtered = value; filtered.mode = scenario == "mode_invalid" ? "unknown" : "problems";
        const auto result = store.save(session, scenario == "mode_event" ? "event" : "resource", filtered, scenario == "mode_event" ? TableViewState{} : value);
        if (scenario != "mode_save") return fails(result, StoreError::InvalidInput) && equals(store.load(session), saved);
        return std::holds_alternative<TableViewStates>(result) && std::get<TableViewStates>(store.load(session))["resource"] == filtered;
    }
    if (scenario == "fields_save" || scenario == "fields_clear" || scenario == "fields_conflict" || scenario == "fields_other_table") {
        auto filtered = value; filtered.fields = {{"name", "\"alpha\" bravo"}, {"namespace", "default"}};
        const auto result = store.save(session, "resource", filtered, value);
        if (!std::holds_alternative<TableViewStates>(result) || std::get<TableViewStates>(store.load(session)).value("resource") != filtered) return false;
        if (scenario == "fields_save") return true;
        if (scenario == "fields_clear") return equals(store.save(session, "resource", value, filtered), saved);
        if (scenario == "fields_conflict") return fails(store.save(session, "resource", value, value), StoreError::Conflict);
        const auto merged = store.save(session, "event", {"Warning", "eventTime", false}, {});
        return std::holds_alternative<TableViewStates>(merged) && std::get<TableViewStates>(merged).value("resource") == filtered;
    }
    if (scenario == "field_unknown_input" || scenario == "field_empty_input") {
        auto filtered = value;
        filtered.fields = scenario == "field_unknown_input" ? QMap<QString, QString>{{"removed", "alpha"}} : QMap<QString, QString>{{"name", ""}};
        return fails(store.save(session, "resource", filtered, value), StoreError::InvalidInput) && equals(store.load(session), saved);
    }
    if (scenario == "save") return equals(store.load(session), saved);
    if (scenario == "repeat") return equals(store.save(session, "resource", value, value), saved);
    if (scenario == "clear") {
        const auto result = store.save(session, "resource", resourceDefault, value);
        return std::holds_alternative<TableViewStates>(result) && std::get<TableViewStates>(result).value("resource") == resourceDefault;
    }
    if (scenario == "conflict") return fails(store.save(session, "resource", {"bravo", "namespace", false}, {}), StoreError::Conflict) && equals(store.load(session), saved);
    if (scenario == "other_table") {
        const TableViewState event{"Warning", "eventCount", false};
        const auto result = store.save(session, "event", event, {});
        return std::holds_alternative<TableViewStates>(result) && std::get<TableViewStates>(result).value("resource") == value
            && std::get<TableViewStates>(result).value("event") == event;
    }
    if (scenario == "other_session") {
        const auto result = store.save(other, "resource", {"bravo", "", false}, resourceDefault);
        return std::holds_alternative<TableViewStates>(result) && equals(store.load(session), saved)
            && std::get<TableViewStates>(store.load(other)).value("resource").filter == "bravo";
    }
    if (scenario == "stable_ids") {
        const ViewStateStore reordered(profile, {{"resource", {"namespace", "name"}}, {"event", {"eventCount", "eventTime"}}});
        return equals(reordered.load(session), saved);
    }
#ifdef Q_OS_UNIX
    if (scenario == "private") {
        const auto permissions = QFileInfo(path).permissions();
        return !(permissions & (QFile::ReadGroup | QFile::WriteGroup | QFile::ReadOther | QFile::WriteOther));
    }
#endif
    QFile input(path); if (!input.open(QIODevice::ReadOnly)) return false;
    auto document = QJsonDocument::fromJson(input.readAll()).object(); input.close();
    auto states = document["views"].toObject(), resource = states["resource"].toObject();
    if (scenario == "version") document["version"] = 8;
    else if (scenario == "version_type") document["version"] = "7";
    else if (scenario == "field_type") resource["fields"] = true;
    else if (scenario == "field_value_type") resource["fields"] = QJsonObject{{"name", 3}};
    else if (scenario == "field_empty") resource["fields"] = QJsonObject{{"name", ""}};
    else if (scenario == "field_unknown") resource["fields"] = QJsonObject{{"removed", "alpha"}};
    else if (scenario == "root_extra") document["unexpected"] = true;
    else if (scenario == "missing_table") states.remove("event");
    else if (scenario == "unknown_table_stored") { states.remove("event"); states["unknown"] = resource; }
    else if (scenario == "missing_field") resource.remove("filter");
    else if (scenario == "extra_field") resource["unexpected"] = true;
    else if (scenario == "filter_type") resource["filter"] = 3;
    else if (scenario == "column_type") resource["column"] = true;
    else if (scenario == "direction_type") resource["descending"] = "DESC";
    else if (scenario == "mode_type") resource["mode"] = false;
    else if (scenario == "stored_mode_unknown") resource["mode"] = "unsupported";
    else if (scenario == "namespaces_type") resource["namespaces"] = false;
    else if (scenario == "namespace_item_type") resource["namespaces"] = QJsonArray{7};
    else if (scenario.startsWith("event_scope_")) {
        auto event = states["event"].toObject();
        if (scenario == "event_scope_mode") event["mode"] = "problems";
        else if (scenario == "event_scope_limit") event["limit"] = 7;
        else if (scenario == "event_scope_namespaces") event["namespaces"] = QJsonArray{"team"};
        else return false;
        states["event"] = event;
    }
    else if (scenario == "column_unknown") resource["column"] = "removed";
    else if (scenario == "none_descending") resource["column"] = "";
    else if (scenario != "malformed" && scenario != "oversized_file" && scenario != "root_array" && scenario != "record_type" && scenario != "views_type") return false;
    if (scenario != "missing_table") states["resource"] = resource;
    if (scenario == "record_type") states["resource"] = false;
    document["views"] = scenario == "views_type" ? QJsonValue(QJsonArray{}) : QJsonValue(states);
    const auto bytes = scenario == "malformed" ? QByteArray("{") : scenario == "oversized_file" ? QByteArray(65537, ' ')
        : scenario == "root_array" ? QByteArray("[]") : QJsonDocument(document).toJson(QJsonDocument::Compact);
    if (!write(path, bytes)) return false;
    const auto code = scenario == "version" ? StoreError::UnsupportedVersion : scenario == "oversized_file" ? StoreError::ReadFailed : StoreError::InvalidData;
    if (!fails(store.load(session), code) || !fails(store.save(session, "resource", {}, value), code)) return false;
    if (!input.open(QIODevice::ReadOnly)) return false;
    return input.readAll() == bytes;
}
}
int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    if (argc != 2) return 2;
    const bool passed = run(QString::fromLocal8Bit(argv[1]));
    if (!passed) std::fprintf(stderr, "View state scenario failed: %s\n", argv[1]);
    return passed ? 0 : 1;
}
