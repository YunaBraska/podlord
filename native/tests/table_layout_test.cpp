#include "table_layout.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QLockFile>
#include <QTemporaryDir>
#include <cstdio>
#include <future>
#include <latch>
#include <limits>

using namespace podlord;
namespace {
bool failure(const Result<TableLayouts>& result, StoreError expected) {
    const auto* error = std::get_if<Failure>(&result);
    if (!error || error->code != expected || error->message.isEmpty()) {
        std::fprintf(stderr, "Expected explicit %s failure.\n", qPrintable(errorName(expected))); return false;
    }
    return true;
}
QJsonArray input(const TableLayout& layout) {
    QJsonArray result;
    for (const auto& column : layout) result.append(QJsonObject{{"id", column.id}, {"visible", column.visible}, {"pinned", column.pinned}, {"width", column.width}});
    return result;
}
QByteArray read(const QString& path) { QFile file(path); return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{}; }
bool write(const QString& path, const QByteArray& value) { QFile file(path); return file.open(QIODevice::WriteOnly) && file.write(value) == value.size(); }
bool scenario(const QString& name) {
    QTemporaryDir temporary;
    if (!temporary.isValid()) return false;
    const TableSchemas schemas{{"resource", {"name", "kind", "namespace"}}, {"event", {"time", "type"}}};
    const auto defaults = defaultTableLayouts(schemas);
    const auto profile = temporary.filePath("profile"), path = QDir(profile).filePath("table-layouts.json");
    const TableLayoutStore store(profile, schemas);
    auto layout = defaults.value("resource");
    if (name.startsWith("alert_upgrade_")) {
        auto expanded = schemas;
        expanded["alert"] = {"enabled", "active", "name", "when", "actions", "soundLabel"};
        auto retained = layout;
        retained[0].pinned = true;
        retained[1].width = 231;
        QJsonObject values{{"resource", input(retained)}, {"event", input(defaults.value("event"))}};
        if (name == "alert_upgrade_unknown") values["unknown"] = QJsonArray{};
        if (name == "alert_upgrade_invalid") {
            auto invalid = input(retained);
            auto column = invalid[0].toObject();
            column["width"] = 0;
            invalid[0] = column;
            values["resource"] = invalid;
        }
        const int version = name == "alert_upgrade_current_missing" ? 7 : 6;
        const auto bytes = QJsonDocument(QJsonObject{{"version", version}, {"layouts", values}}).toJson();
        if (!QDir().mkpath(profile) || !write(path, bytes)) return false;
        const TableLayoutStore upgraded(profile, expanded);
        const auto loaded = upgraded.load();
        if (name != "alert_upgrade_read" && name != "alert_upgrade_save") {
            return failure(loaded, StoreError::InvalidData) && read(path) == bytes;
        }
        if (!std::holds_alternative<TableLayouts>(loaded) || read(path) != bytes) return false;
        const auto actual = std::get<TableLayouts>(loaded);
        if (actual.value("resource") != retained
            || actual.value("alert") != defaultTableLayouts(expanded).value("alert")) return false;
        if (name == "alert_upgrade_read") return true;
        auto columns = actual.value("alert");
        columns.swapItemsAt(0, 2);
        columns[0].pinned = true;
        columns[1].width = 317;
        const auto saved = upgraded.save("alert", input(columns), actual.value("alert"));
        return std::holds_alternative<TableLayouts>(saved)
            && std::get<TableLayouts>(upgraded.load()).value("alert") == columns
            && std::get<TableLayouts>(upgraded.load()).value("resource") == retained
            && QJsonDocument::fromJson(read(path)).object()["version"] == 7;
    }
    if (name.startsWith("values_upgrade_")) {
        auto expanded = schemas;
        expanded["value"] = {"name", "encoding", "preview", "copy", "reveal"};
        auto retained = layout; retained[0].pinned = true; retained[1].width = 231;
        QJsonObject values{{"resource", input(retained)}, {"event", input(defaults.value("event"))}};
        if (name == "values_upgrade_invalid") { auto invalid = input(retained); auto column = invalid[0].toObject(); column["width"] = 0; invalid[0] = column; values["resource"] = invalid; }
        const auto bytes = QJsonDocument(QJsonObject{{"version", name == "values_upgrade_current_missing" ? 6 : 5}, {"layouts", values}}).toJson();
        if (!QDir().mkpath(profile) || !write(path, bytes)) return false;
        const TableLayoutStore upgraded(profile, expanded);
        const auto loaded = upgraded.load();
        if (name == "values_upgrade_invalid" || name == "values_upgrade_current_missing") return failure(loaded, StoreError::InvalidData) && read(path) == bytes;
        if (!std::holds_alternative<TableLayouts>(loaded) || read(path) != bytes) return false;
        const auto actual = std::get<TableLayouts>(loaded);
        if (actual.value("resource") != retained || actual.value("value") != defaultTableLayouts(expanded).value("value")) return false;
        if (name == "values_upgrade_read") return true;
        auto valueColumns = actual.value("value"); valueColumns[0].pinned = true;
        const auto saved = upgraded.save("value", input(valueColumns), actual.value("value"));
        return std::holds_alternative<TableLayouts>(saved) && std::get<TableLayouts>(upgraded.load()).value("value") == valueColumns
            && std::get<TableLayouts>(upgraded.load()).value("resource") == retained;
    }
    if (name.startsWith("inspector_upgrade_")) {
        auto expanded = schemas;
        expanded["inspectorEvent"] = {"time", "message"}; expanded["inspectorLink"] = {"from", "to"};
        auto retained = layout; retained[0].width = 222;
        QJsonObject values{{"resource", input(retained)}, {"event", input(defaults.value("event"))}};
        if (name == "inspector_upgrade_unknown") values["unknown"] = QJsonArray{};
        if (name == "inspector_upgrade_invalid") { auto invalid = input(retained); auto cell = invalid[0].toObject(); cell["width"] = 0; invalid[0] = cell; values["resource"] = invalid; }
        const auto bytes = QJsonDocument(QJsonObject{{"version", name == "inspector_upgrade_current_missing" ? 5 : 4}, {"layouts", values}}).toJson();
        if (!QDir().mkpath(profile) || !write(path, bytes)) return false;
        const TableLayoutStore upgraded(profile, expanded);
        const auto loaded = upgraded.load();
        if (name != "inspector_upgrade_read" && name != "inspector_upgrade_save") return failure(loaded, StoreError::InvalidData) && read(path) == bytes;
        if (!std::holds_alternative<TableLayouts>(loaded) || read(path) != bytes) return false;
        const auto actual = std::get<TableLayouts>(loaded);
        if (actual.value("resource") != retained || actual.value("inspectorEvent") != defaultTableLayouts(expanded).value("inspectorEvent")
            || actual.value("inspectorLink") != defaultTableLayouts(expanded).value("inspectorLink")) return false;
        if (name == "inspector_upgrade_read") return true;
        auto links = actual.value("inspectorLink"); links[0].pinned = true;
        return std::holds_alternative<TableLayouts>(upgraded.save("inspectorLink", input(links), actual.value("inspectorLink")))
            && QJsonDocument::fromJson(read(path)).object()["version"] == 7 && failure(store.load(), StoreError::InvalidData);
    }
    if (name.startsWith("ports_upgrade_")) {
        auto expanded = schemas; expanded["port"] = {"endpoint", "name"};
        auto retained = layout; retained[0].width = 222; retained[1].visible = false;
        QJsonObject values{{"resource", input(retained)}, {"event", input(defaults.value("event"))}};
        if (name == "ports_upgrade_unknown") values["unknown"] = QJsonArray{};
        if (name == "ports_upgrade_invalid") { auto invalid = input(retained); auto column = invalid[0].toObject(); column["width"] = 0; invalid[0] = column; values["resource"] = invalid; }
        const int version = name == "ports_upgrade_current_missing" ? 4 : name == "ports_upgrade_unsupported" ? 5 : 3;
        const auto bytes = QJsonDocument(QJsonObject{{"version", version}, {"layouts", values}}).toJson();
        if (!QDir().mkpath(profile) || !write(path, bytes)) return false;
        const TableLayoutStore upgraded(profile, expanded);
        const auto loaded = upgraded.load();
        if (name != "ports_upgrade_read" && name != "ports_upgrade_save") return failure(loaded, StoreError::InvalidData) && read(path) == bytes;
        if (!std::holds_alternative<TableLayouts>(loaded) || read(path) != bytes) return false;
        const auto actual = std::get<TableLayouts>(loaded);
        if (actual.value("resource") != retained || actual.value("event") != defaults.value("event") || actual.value("port") != defaultTableLayouts(expanded).value("port")) return false;
        if (name == "ports_upgrade_read") return true;
        auto ports = actual.value("port"); ports[0].pinned = true;
        return std::holds_alternative<TableLayouts>(upgraded.save("port", input(ports), actual.value("port")))
            && QJsonDocument::fromJson(read(path)).object()["version"] == 7
            && std::get<TableLayouts>(upgraded.load()).value("resource") == retained
            && failure(store.load(), StoreError::InvalidData);
    }
    if (name=="reference_defaults" || name=="reference_saved") {
        const TableSchemas full{{"resource",{"name","kind","namespace","status","node","image","cluster","cpu","memory","storage","createdAt","ready","restarts","owner","issue"}}};
        const TableLayoutStore reference(profile,full);
        const auto loaded=reference.load(); if (!std::holds_alternative<TableLayouts>(loaded)) return false;
        auto actual=std::get<TableLayouts>(loaded).value("resource");
        const QStringList order{"status","kind","name","namespace","cluster","cpu","memory","storage","createdAt","ready","restarts","node","image","owner","issue"};
        QStringList ids; for (const auto& column:actual) ids.append(column.id);
        if (ids!=order || actual[2].width!=280 || actual[5].width!=90 || actual.last().visible) return false;
        if (name=="reference_defaults") return true;
        auto saved=actual; saved.swapItemsAt(0,2); saved[0].pinned=true; saved[1].width=345;
        if (!std::holds_alternative<TableLayouts>(reference.save("resource",input(saved),actual))) return false;
        return std::get<TableLayouts>(reference.load()).value("resource")==saved;
    }
    if (name.startsWith("upgrade_columns")) {
        const TableSchemas old{{"resource", {"name", "kind", "namespace", "status", "node", "image"}}, {"event", {"time", "type"}}};
        auto retained = defaultTableLayouts(old).value("resource"); retained.swapItemsAt(0, 2); retained[0].pinned = true; retained[1].visible = false; retained[2].width = 450;
        QJsonObject root{{"version", 1}, {"layouts", QJsonObject{{"resource", input(retained)}, {"event", input(defaultTableLayouts(old).value("event"))}}}};
        if (name == "upgrade_columns_invalid") { auto values = input(retained); auto invalid = values[0].toObject(); invalid["id"] = "unknown"; values[0] = invalid; auto tables = root["layouts"].toObject(); tables["resource"] = values; root["layouts"] = tables; }
        if (!QDir().mkpath(profile) || !write(path, QJsonDocument(root).toJson())) return false;
        const auto bytes = read(path);
        auto expanded = old; expanded["resource"].append("cpu"); expanded["resource"].append("memory");
        const TableLayoutStore upgraded(profile, expanded);
        const auto loaded = upgraded.load();
        if (name == "upgrade_columns_invalid") return failure(loaded, StoreError::InvalidData) && read(path) == bytes;
        if (!std::holds_alternative<TableLayouts>(loaded)) return false;
        const auto actual = std::get<TableLayouts>(loaded).value("resource");
        auto expected = retained; expected.append({"cpu"}); expected.append({"memory"});
        if (actual != expected || read(path) != bytes) return false;
        if (name == "upgrade_columns_save") {
            if (!std::holds_alternative<TableLayouts>(upgraded.save("resource", input(actual), actual))) return false;
            return QJsonDocument::fromJson(read(path)).object()["version"] == 7 && std::get<TableLayouts>(upgraded.load()).value("resource") == expected;
        }
        return true;
    }
    if (name == "missing") {
        const auto result = store.load();
        return std::holds_alternative<TableLayouts>(result) && std::get<TableLayouts>(result) == defaults && !QFileInfo::exists(profile);
    }
    if (name == "empty_profile" || name == "relative_profile") {
        const TableLayoutStore invalid(name == "empty_profile" ? QString{} : QString("relative"), schemas);
        return failure(invalid.load(), StoreError::InvalidInput) && failure(invalid.save("resource", input(layout), layout), StoreError::InvalidInput);
    }
    if (name == "unknown_table") return failure(store.save("missing", input(layout), layout), StoreError::InvalidInput) && !QFileInfo::exists(profile);
    if (name == "profile_file") return write(profile, "retained") && failure(store.load(), StoreError::ReadFailed) && failure(store.save("resource", input(layout), layout), StoreError::ReadFailed) && read(profile) == "retained";
    if (name == "parent_file") {
        const auto blocker = temporary.filePath("blocker");
        return write(blocker, "retained") && failure(TableLayoutStore(blocker + "/child", schemas).save("resource", input(layout), layout), StoreError::WriteFailed) && read(blocker) == "retained";
    }
    if (name == "concurrent") {
        std::latch ready(2), start(1);
        const auto save = [&](int width) { auto value = layout; value[0].width = width; ready.count_down(); start.wait(); return store.save("resource", input(value), layout); };
        auto first = std::async(std::launch::async, save, 300), second = std::async(std::launch::async, save, 400);
        ready.wait(); start.count_down();
        const auto a = first.get(), b = second.get();
        const bool won = std::holds_alternative<TableLayouts>(a), otherWon = std::holds_alternative<TableLayouts>(b);
        if (won == otherWon) return false;
        const auto* error = std::get_if<Failure>(won ? &b : &a);
        const auto current = store.load();
        return error && (error->code == StoreError::Busy || error->code == StoreError::Conflict) && std::holds_alternative<TableLayouts>(current) && std::get<TableLayouts>(current) == std::get<TableLayouts>(won ? a : b);
    }
    if (name == "read_during_save") {
        auto writer = std::async(std::launch::async, [&] {
            auto expected = layout;
            for (int width = 300; width < 330; ++width) {
                auto value = expected; value[0].width = width;
                if (!std::holds_alternative<TableLayouts>(store.save("resource", input(value), expected))) return false;
                expected = value;
            }
            return true;
        });
        bool valid = true;
        do { valid = valid && std::holds_alternative<TableLayouts>(store.load()); }
        while (writer.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready);
        return writer.get() && valid;
    }
    if (name == "hide") layout[1].visible = false;
    else if (name == "pin") layout[0].pinned = true;
    else if (name == "order") layout.swapItemsAt(0, 2);
    else if (name == "width") layout[0].width = 450;
    else if (name == "maximum_width") layout[0].width = std::numeric_limits<int>::max();
    if (name == "hide" || name == "pin" || name == "order" || name == "width" || name == "maximum_width" || name == "private" || name == "repeat" || name == "conflict" || name == "other_table") {
        const auto saved = store.save("resource", input(layout), defaults.value("resource"));
        if (!std::holds_alternative<TableLayouts>(saved)) return false;
        const auto loaded = store.load();
        if (!std::holds_alternative<TableLayouts>(loaded) || std::get<TableLayouts>(loaded).value("resource") != layout) return false;
        const auto bytes = read(path);
        if (name == "private") return !(QFileInfo(profile).permissions() & (QFile::ReadGroup | QFile::WriteGroup | QFile::ExeGroup | QFile::ReadOther | QFile::WriteOther | QFile::ExeOther)) && !(QFileInfo(path).permissions() & (QFile::ReadGroup | QFile::WriteGroup | QFile::ExeGroup | QFile::ReadOther | QFile::WriteOther | QFile::ExeOther));
        if (name == "repeat") return std::holds_alternative<TableLayouts>(store.save("resource", input(layout), layout)) && read(path) == bytes;
        if (name == "conflict") { auto next = layout; next[0].width = 500; auto stale = layout; stale[0].width = 400; return std::holds_alternative<TableLayouts>(store.save("resource", input(next), layout)) && failure(store.save("resource", input(stale), layout), StoreError::Conflict) && std::get<TableLayouts>(store.load()).value("resource") == next; }
        if (name == "other_table") {
            auto event = defaults.value("event"); event[0].pinned = true;
            auto resource = layout; resource[1].visible = false;
            if (!std::holds_alternative<TableLayouts>(store.save("event", input(event), defaults.value("event")))) return false;
            const auto changed = store.save("resource", input(resource), layout);
            return std::holds_alternative<TableLayouts>(changed) && std::get<TableLayouts>(changed).value("event") == event && std::get<TableLayouts>(changed).value("resource") == resource;
        }
        return true;
    }
    if (!QDir().mkpath(profile)) return false;
    if (name == "busy") { QLockFile lock(path + ".lock"); return lock.tryLock(0) && failure(store.save("resource", input(layout), layout), StoreError::Busy) && !QFileInfo::exists(path); }
    if (name == "lock_directory") return QDir().mkpath(path + ".lock") && failure(store.save("resource", input(layout), layout), StoreError::WriteFailed) && !QFileInfo::exists(path);
    if (name == "file_directory") return QDir().mkpath(path) && failure(store.load(), StoreError::ReadFailed) && failure(store.save("resource", input(layout), layout), StoreError::ReadFailed) && QFileInfo(path).isDir();
#ifdef Q_OS_UNIX
    if (name == "profile_symlink") {
        const auto linked = temporary.filePath("linked");
        return QFile::link(profile, linked) && failure(TableLayoutStore(linked, schemas).load(), StoreError::InvalidInput) && failure(TableLayoutStore(linked, schemas).save("resource", input(layout), layout), StoreError::InvalidInput);
    }
    if (name == "file_symlink") {
        const auto target = temporary.filePath("target");
        return write(target, "retained") && QFile::link(target, path) && failure(store.load(), StoreError::InvalidData) && failure(store.save("resource", input(layout), layout), StoreError::InvalidData) && read(target) == "retained";
    }
    if (name == "read_denied") {
        if (!write(path, "retained") || !QFile::setPermissions(path, {})) return false;
        const auto result = store.load();
        const bool restored = QFile::setPermissions(path, QFile::ReadOwner | QFile::WriteOwner);
        return restored && failure(result, StoreError::ReadFailed) && read(path) == "retained";
    }
    if (name == "write_denied") {
        if (!QFile::setPermissions(profile, QFile::ReadOwner | QFile::ExeOwner)) return false;
        const auto result = store.save("resource", input(layout), layout);
        const bool restored = QFile::setPermissions(profile, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
        return restored && failure(result, StoreError::WriteFailed) && !QFileInfo::exists(path);
    }
#endif
    auto value = input(layout);
    auto column = value[0].toObject();
    const auto invalid = name.section('_', 1);
    if (name.startsWith("input_") || name.startsWith("stored_")) {
        if (invalid == "missing") column.remove("width");
        else if (invalid == "extra") column["extra"] = true;
        else if (invalid == "id_type") column["id"] = 1;
        else if (invalid == "id_unknown") column["id"] = "unknown";
        else if (invalid == "visible_type") column["visible"] = "true";
        else if (invalid == "pinned_type") column["pinned"] = QJsonValue::Null;
        else if (invalid == "width_zero") column["width"] = 0;
        else if (invalid == "width_negative") column["width"] = -1;
        else if (invalid == "width_fraction") column["width"] = 1.5;
        else if (invalid == "width_text") column["width"] = "170";
        else if (invalid == "width_null") column["width"] = QJsonValue::Null;
        else if (invalid == "width_overflow") column["width"] = 2147483648.0;
        else if (invalid == "pinned_hidden") { column["visible"] = false; column["pinned"] = true; }
        else if (invalid != "duplicate" && invalid != "count" && invalid != "item_type" && invalid != "no_visible") return false;
        value[0] = column;
        if (invalid == "duplicate") value[1] = column;
        if (invalid == "count") value.removeLast();
        if (invalid == "item_type") value[0] = "invalid";
        if (invalid == "no_visible") for (int i = 0; i < value.size(); ++i) { auto hidden = value[i].toObject(); hidden["visible"] = false; value[i] = hidden; }
        if (name.startsWith("input_")) return failure(store.save("resource", value, layout), StoreError::InvalidInput) && !QFileInfo::exists(path);
    }
    QJsonObject encoded{{"resource", value}, {"event", input(defaults.value("event"))}};
    QJsonObject root{{"version", 1}, {"layouts", encoded}};
    if (name == "version") root["version"] = 8;
    else if (name == "extra_root") root["extra"] = 1;
    else if (name == "layout_type") root["layouts"] = QJsonArray{};
    else if (name == "unknown_key") { encoded.remove("event"); encoded["unknown"] = QJsonArray{}; root["layouts"] = encoded; }
    else if (name == "missing_table") { encoded.remove("event"); root["layouts"] = encoded; }
    else if (name == "array_layout") { encoded["resource"] = QJsonObject{}; root["layouts"] = encoded; }
    else if (name != "malformed" && name != "empty" && name != "array_root" && name != "oversized" && !name.startsWith("stored_")) return false;
    const auto bytes = name == "malformed" ? QByteArray("{") : name == "empty" ? QByteArray{} : name == "array_root" ? QByteArray("[]") : name == "oversized" ? QByteArray(65537, ' ') : QJsonDocument(root).toJson();
    if (!write(path, bytes)) return false;
    const auto expected = name == "oversized" ? StoreError::ReadFailed : StoreError::InvalidData;
    return failure(store.load(), expected) && failure(store.save("resource", input(layout), layout), expected) && read(path) == bytes;
}
} // namespace
int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    if (argc != 2) return 2;
    const bool passed = scenario(QString::fromLocal8Bit(argv[1]));
    if (!passed) std::fprintf(stderr, "Table layout scenario failed: %s\n", argv[1]);
    return passed ? 0 : 1;
}
