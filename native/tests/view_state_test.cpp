#include "view_state.h"
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
const QString session = "12345678-1234-1234-1234-123456789abc";
const QString other = "87654321-4321-4321-4321-cba987654321";
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
    const auto path = views + "/" + session + ".json";
    const ViewStateStore store(profile, schemas);
    const TableViewState value{"alpha", "name", true};
    if (scenario.startsWith("auxiliary_")) {
        auto expanded = schemas;
        expanded["port"] = {"endpoint", "name"}; expanded["inspectorEvent"] = {"time", "count"}; expanded["inspectorLink"] = {"from", "to"};
        const QJsonObject resource{{"filter", "alpha"}, {"column", "name"}, {"descending", true}, {"fields", QJsonObject{}}, {"mode", "problems"}};
        const QJsonObject event{{"filter", "Warning"}, {"column", "eventCount"}, {"descending", false}, {"fields", QJsonObject{}}, {"mode", ""}};
        QJsonObject data{{"resource", resource}, {"event", event}};
        if (scenario == "auxiliary_unknown") data["unknown"] = event;
        const auto bytes = QJsonDocument(QJsonObject{{"version", scenario == "auxiliary_current_missing" ? 4 : 3}, {"views", data}}).toJson();
        if (!QDir().mkpath(views) || !write(path, bytes)) return false;
        const ViewStateStore upgraded(profile, expanded);
        const auto loaded = upgraded.load(session);
        if (scenario == "auxiliary_unknown" || scenario == "auxiliary_current_missing") return fails(loaded, StoreError::InvalidData);
        if (!std::holds_alternative<TableViewStates>(loaded)) return false;
        const auto actual = std::get<TableViewStates>(loaded);
        if (actual.size() != 5 || actual["resource"].filter != "alpha" || actual["resource"].mode != "problems" || actual["event"].filter != "Warning"
            || actual["port"] != TableViewState{} || actual["inspectorEvent"] != TableViewState{} || actual["inspectorLink"] != TableViewState{}) return false;
        if (scenario == "auxiliary_read") { QFile file(path); return file.open(QIODevice::ReadOnly) && file.readAll() == bytes; }
        const auto saved = upgraded.save(session, "inspectorLink", {"", "to", true}, {});
        return std::holds_alternative<TableViewStates>(saved) && std::get<TableViewStates>(saved)["resource"] == actual["resource"]
            && fails(store.load(session), StoreError::InvalidData) && equals(upgraded.load(session), std::get<TableViewStates>(saved));
    }
    if (scenario.startsWith("preset_")) {
        const TableViewStates empty{{"default", {}}};
        auto desired = empty; desired["Problems"] = {"", "", false, {{"namespace", "team"}}, "problems"};
        if (scenario == "preset_missing") return equals(store.loadPresets(), empty) && !QFile::exists(profile);
        if (scenario == "preset_default_delete") { desired.remove("default"); return fails(store.savePresets(desired, empty), StoreError::InvalidData); }
        if (scenario == "preset_default_replace") { desired["default"].filter = "alpha"; return fails(store.savePresets(desired, empty), StoreError::InvalidData); }
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
        const auto bytes = scenario == "preset_malformed" ? QByteArray("{") : scenario == "preset_future" ? QByteArray("{\"version\":2,\"presets\":{}}") : QByteArray(65537, ' ');
        const auto code = scenario == "preset_oversized" ? StoreError::ReadFailed : StoreError::InvalidData;
        if (!write(presetPath, bytes) || !fails(store.loadPresets(), code) || !fails(store.savePresets(empty, desired), code)) return false;
        QFile input(presetPath); return input.open(QIODevice::ReadOnly) && input.readAll() == bytes;
    }
    if (scenario == "missing") {
        const auto result = store.load(session);
        return std::holds_alternative<TableViewStates>(result) && std::get<TableViewStates>(result).size() == 2
            && std::get<TableViewStates>(result).value("resource") == TableViewState{} && !QFile::exists(profile);
    }
    if (scenario == "empty_profile") return fails(ViewStateStore("", schemas).load(session), StoreError::InvalidInput);
    if (scenario == "relative_profile") return fails(ViewStateStore("relative", schemas).load(session), StoreError::InvalidInput);
    if (scenario == "invalid_id") return fails(store.load("../escape"), StoreError::InvalidInput) && !QFile::exists(profile);
    if (scenario == "noncanonical_id") return fails(store.load("{" + session + "}"), StoreError::InvalidInput);
    if (scenario == "empty_id") return fails(store.save("", "resource", value, {}), StoreError::InvalidInput);
    if (scenario == "unknown_table") return fails(store.save(session, "unknown", value, {}), StoreError::InvalidInput);
    if (scenario == "unknown_column") return fails(store.save(session, "resource", {"", "removed", false}, {}), StoreError::InvalidInput);
    if (scenario == "descending_none") return fails(store.save(session, "resource", {"", "", true}, {}), StoreError::InvalidInput);
    if (scenario == "profile_file") return write(profile, "file") && fails(store.load(session), StoreError::ReadFailed);
    if (!QDir().mkpath(views)) return false;
    if (scenario == "file_directory") return QDir().mkpath(path) && fails(store.load(session), StoreError::ReadFailed);
    if (scenario == "busy") {
        QLockFile lock(path + ".lock"); return lock.tryLock(0) && fails(store.save(session, "resource", value, {}), StoreError::Busy);
    }
    if (scenario == "lock_directory") return QDir().mkpath(path + ".lock") && fails(store.save(session, "resource", value, {}), StoreError::WriteFailed);
    if (scenario == "oversized_input") return fails(store.save(session, "resource", {QString(65536, 'x'), "", false}, {}), StoreError::InvalidInput) && !QFile::exists(path);
#ifdef Q_OS_UNIX
    if (scenario == "profile_symlink") {
        const auto link = temporary.filePath("linked"); return QFile::link(profile, link) && fails(ViewStateStore(link, schemas).load(session), StoreError::InvalidInput);
    }
    if (scenario == "file_symlink") {
        const auto target = temporary.filePath("target"); return write(target, "{}") && QFile::link(target, path) && fails(store.load(session), StoreError::InvalidData);
    }
#endif
    const auto initial = store.save(session, "resource", value, {});
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
        const auto result = store.save(session, "resource", {}, value);
        return std::holds_alternative<TableViewStates>(result) && std::get<TableViewStates>(result).value("resource") == TableViewState{};
    }
    if (scenario == "conflict") return fails(store.save(session, "resource", {"bravo", "namespace", false}, {}), StoreError::Conflict) && equals(store.load(session), saved);
    if (scenario == "other_table") {
        const TableViewState event{"Warning", "eventCount", false};
        const auto result = store.save(session, "event", event, {});
        return std::holds_alternative<TableViewStates>(result) && std::get<TableViewStates>(result).value("resource") == value
            && std::get<TableViewStates>(result).value("event") == event;
    }
    if (scenario == "other_session") {
        const auto result = store.save(other, "resource", {"bravo", "", false}, {});
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
    if (scenario == "legacy_read" || scenario == "legacy_save" || scenario == "legacy_repeat") {
        document["version"] = 1;
        for (auto state = states.begin(); state != states.end(); ++state) {
            auto old = state.value().toObject(); old.remove("fields"); old.remove("mode"); state.value() = old;
        }
        document["views"] = states;
        const auto bytes = QJsonDocument(document).toJson(QJsonDocument::Compact);
        if (!write(path, bytes) || !equals(store.load(session), saved)) return false;
        if (scenario == "legacy_repeat" && !equals(store.save(session, "resource", value, value), saved)) return false;
        if (scenario != "legacy_save") {
            if (!input.open(QIODevice::ReadOnly)) return false;
            return input.readAll() == bytes;
        }
        auto filtered = value; filtered.fields = {{"name", "alpha"}};
        const auto result = store.save(session, "resource", filtered, value);
        if (!std::holds_alternative<TableViewStates>(result) || std::get<TableViewStates>(store.load(session)).value("resource") != filtered
            || !input.open(QIODevice::ReadOnly)) return false;
        return QJsonDocument::fromJson(input.readAll()).object()["version"] == 4;
    }
    if (scenario == "version") document["version"] = 5;
    else if (scenario == "field_type") resource["fields"] = true;
    else if (scenario == "field_value_type") resource["fields"] = QJsonObject{{"name", 3}};
    else if (scenario == "field_empty") resource["fields"] = QJsonObject{{"name", ""}};
    else if (scenario == "field_unknown") resource["fields"] = QJsonObject{{"removed", "alpha"}};
    else if (scenario == "root_extra") document["unexpected"] = true;
    else if (scenario == "missing_table") states.remove("event");
    else if (scenario == "unknown_table_stored") states["unknown"] = resource;
    else if (scenario == "missing_field") resource.remove("filter");
    else if (scenario == "extra_field") resource["unexpected"] = true;
    else if (scenario == "filter_type") resource["filter"] = 3;
    else if (scenario == "column_type") resource["column"] = true;
    else if (scenario == "direction_type") resource["descending"] = "DESC";
    else if (scenario == "column_unknown") resource["column"] = "removed";
    else if (scenario == "none_descending") resource["column"] = "";
    else if (scenario != "malformed" && scenario != "oversized_file" && scenario != "root_array") return false;
    if (scenario != "missing_table") states["resource"] = resource;
    document["views"] = states;
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
