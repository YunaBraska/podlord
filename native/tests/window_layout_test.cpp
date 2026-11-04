#include "window_layout.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLockFile>
#include <QTemporaryDir>
#include <algorithm>
#include <cstdio>
#include <limits>

using namespace podlord;
namespace {
bool failure(const Result<WindowLayout>& result, StoreError expected) {
    const auto* error = std::get_if<Failure>(&result);
    if (!error || error->code != expected || error->message.isEmpty()) {
        std::fprintf(stderr, "Expected explicit %s failure.\n", qPrintable(errorName(expected)));
        return false;
    }
    return true;
}
bool matches(const Result<WindowLayout>& result, const WindowLayout& expected) {
    const auto* value = std::get_if<WindowLayout>(&result);
    if (value && *value == expected) return true;
    if (const auto* error = std::get_if<Failure>(&result))
        std::fprintf(stderr, "Unexpected %s: %s\n", qPrintable(errorName(error->code)), qPrintable(error->message));
    else std::fputs("Window layout differs from expected placement.\n", stderr);
    return false;
}
QByteArray read(const QString& path) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
}
bool write(const QString& path, const QByteArray& bytes) {
    QFile file(path);
    return file.open(QIODevice::WriteOnly | QIODevice::Truncate) && file.write(bytes) == bytes.size() && file.flush();
}
bool scenario(const QString& name) {
    QTemporaryDir temporary;
    if (!temporary.isValid()) return false;
    const QString profile = temporary.filePath("profile"), path = QDir(profile).filePath("window-layout.json");
    const WindowLayoutStore store(profile);
    const QUuid a("11111111-1111-4111-8111-111111111111"), b("22222222-2222-4222-8222-222222222222"),
        c("33333333-3333-4333-8333-333333333333");
    const QString secondary = "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa";
    const QString sa = a.toString(QUuid::WithoutBraces), sb = b.toString(QUuid::WithoutBraces), sc = c.toString(QUuid::WithoutBraces);
    WindowLayout value{{"main", {b, a}, a, QRect(-120, 20, 640, 480), false},
                       {secondary, {c}, {}, QRect(1024, -50, 800, 600), true}};
    const QJsonObject first{{"id", "main"}, {"sessions", QJsonArray{sa, sb}}, {"active", sa},
        {"x", -120}, {"y", 20}, {"width", 640}, {"height", 480}, {"maximized", false}};
    const QJsonObject second{{"id", secondary}, {"sessions", QJsonArray{sc}}, {"active", QJsonValue(QJsonValue::Null)},
        {"x", 1024}, {"y", -50}, {"width", 800}, {"height", 600}, {"maximized", true}};
    const QJsonObject root{{"version", 1}, {"windows", QJsonArray{first, second}}};
    const auto reject = [&](const QByteArray& bytes, StoreError code) {
        if (!QDir().mkpath(profile) || !write(path, bytes)) return false;
        return failure(store.load(), code) && failure(store.save(value, {}), code) && read(path) == bytes;
    };
    const auto rejectRoot = [&](const QJsonObject& object, StoreError code = StoreError::InvalidData) {
        return reject(QJsonDocument(object).toJson(QJsonDocument::Compact), code);
    };
    const auto rejectRow = [&](const QJsonObject& row) {
        auto object = root; object["windows"] = QJsonArray{row, second};
        return rejectRoot(object);
    };
    if (name == "missing")
        return matches(store.load(), {}) && !QFileInfo::exists(profile) && !QFileInfo::exists(path);
    if (name == "roundtrip") {
        const SessionStore sessions(profile);
        SessionCatalog catalog;
        for (int i = 0; i < 3; ++i) {
            const auto created = sessions.create({"window-layout-context-" + QString::number(i), {"default"}}, "Placement fixture " + QString::number(i));
            if (!std::holds_alternative<SessionCatalog>(created)) return false;
            catalog = std::get<SessionCatalog>(created);
        }
        value[0].sessions = {catalog.sessions[1].id, catalog.sessions[0].id};
        value[0].activeSession = catalog.sessions[0].id;
        value[1].sessions = {catalog.sessions[2].id};
        const QString sessionPath = QDir(profile).filePath("sessions.json");
        const auto sessionBytes = read(sessionPath);
        if (sessionBytes.isEmpty() || !matches(store.save(value, {}), value) || !matches(store.load(), value)) return false;
        const auto after = sessions.list();
        if (!std::holds_alternative<SessionCatalog>(after) || std::get<SessionCatalog>(after) != catalog || read(sessionPath) != sessionBytes) return false;
        const auto document = QJsonDocument::fromJson(read(path)).object();
        const auto windows = document["windows"].toArray();
        if (document.keys() != QStringList{"version", "windows"} || windows.size() != 2) return false;
        for (const auto entry : windows)
            if (entry.toObject().keys() != QStringList{"active", "height", "id", "maximized", "sessions", "width", "x", "y"}) return false;
        QStringList sorted;
        for (const auto& id : value[0].sessions) sorted.append(id.toString(QUuid::WithoutBraces));
        std::sort(sorted.begin(), sorted.end());
        return windows[0].toObject()["sessions"].toArray() == QJsonArray::fromStringList(sorted);
    }
    if (name == "empty") {
        return matches(store.save(value, {}), value) && matches(store.save({}, value), {})
            && matches(store.load(), {}) && QJsonDocument::fromJson(read(path)).object()["windows"].toArray().isEmpty();
    }
    if (name == "repeat") {
        if (!matches(store.save(value, {}), value)) return false;
        const auto before = read(path);
        value[0].sessions.remove(b); value[0].sessions.insert(b);
        return matches(store.save(value, value), value) && matches(store.load(), value) && read(path) == before;
    }
    if (name == "conflict") {
        if (!matches(store.save(value, {}), value)) return false;
        auto changed = value; changed[0].maximized = true;
        const WindowLayoutStore peer(profile);
        if (!matches(peer.save(changed, value), changed)) return false;
        const auto before = read(path);
        return failure(store.save(value, value), StoreError::Conflict) && read(path) == before
            && matches(store.load(), changed) && matches(store.save(value, changed), value);
    }
    if (name == "malformed") return reject("{broken", StoreError::InvalidData);
    if (name == "future") {
        auto object = root; object["version"] = 2;
        return rejectRoot(object, StoreError::UnsupportedVersion);
    }
    if (name == "unknown_field") {
        auto object = root; object["extra"] = true;
        if (!rejectRoot(object)) return false;
        auto row = first; row["configuration"] = QJsonObject{{"contextId", "not-placement-data"}};
        return rejectRow(row);
    }
    if (name == "schema") {
        if (!reject("[]", StoreError::InvalidData)) return false;
        for (const QJsonValue version : {QJsonValue("1"), QJsonValue(true), QJsonValue(1.5)}) {
            auto object = root; object["version"] = version;
            if (!rejectRoot(object)) return false;
        }
        auto object = root; object.remove("version");
        if (!rejectRoot(object)) return false;
        object = root; object["windows"] = QJsonObject{};
        if (!rejectRoot(object)) return false;
        object = root; object["windows"] = QJsonArray{true};
        if (!rejectRoot(object)) return false;
        for (const QJsonObject patch : {QJsonObject{{"id", true}}, QJsonObject{{"sessions", "not-an-array"}}, QJsonObject{{"maximized", "false"}}, QJsonObject{{"active", true}}}) {
            auto row = first;
            for (auto it = patch.begin(); it != patch.end(); ++it) row[it.key()] = it.value();
            if (!rejectRow(row)) return false;
        }
        auto row = first; row.remove("id");
        return rejectRow(row);
    }
    if (name == "geometry") {
        const double beyond = double(std::numeric_limits<int>::max()) + 1;
        const QList<QJsonObject> patches{{{"width", 0}}, {{"height", -1}}, {{"width", 1.5}}, {{"x", "0"}},
            {{"x", beyond}}, {{"y", -beyond - 1}}, {{"width", beyond}}, {{"height", beyond}},
            {{"x", std::numeric_limits<int>::max()}, {"width", 2}}, {{"y", std::numeric_limits<int>::max()}, {"height", 2}}};
        for (const auto& patch : patches) {
            auto row = first;
            for (auto it = patch.begin(); it != patch.end(); ++it) row[it.key()] = it.value();
            if (!rejectRow(row)) return false;
        }
        return true;
    }
    if (name == "geometry_boundary") {
        const int low = std::numeric_limits<int>::min(), high = std::numeric_limits<int>::max();
        auto one = first; one["x"] = high; one["y"] = high; one["width"] = 1; one["height"] = 1;
        auto two = second; two["x"] = low; two["y"] = low; two["width"] = high; two["height"] = high;
        auto object = root; object["windows"] = QJsonArray{one, two};
        value[0].geometry = QRect(QPoint(high, high), QPoint(high, high));
        value[1].geometry = QRect(QPoint(low, low), QPoint(-2, -2));
        return QDir().mkpath(profile) && write(path, QJsonDocument(object).toJson(QJsonDocument::Compact))
            && matches(store.load(), value) && matches(store.save(value, value), value) && matches(store.load(), value);
    }
    if (name == "duplicate_window") {
        auto two = second; two["id"] = "main";
        auto object = root; object["windows"] = QJsonArray{first, two};
        return rejectRoot(object);
    }
    if (name == "duplicate_session") {
        auto row = first; row["sessions"] = QJsonArray{sa, a.toString()};
        if (!rejectRow(row)) return false;
        auto two = second; two["sessions"] = QJsonArray{sa};
        auto object = root; object["windows"] = QJsonArray{first, two};
        return rejectRoot(object);
    }
    if (name == "identifiers") {
        for (const auto& id : QStringList{"", "other", QUuid{}.toString(QUuid::WithoutBraces), QUuid(secondary).toString(), secondary.toUpper()}) {
            auto row = first; row["id"] = id;
            if (!rejectRow(row)) return false;
        }
        for (const QJsonValue id : {QJsonValue("not-a-uuid"), QJsonValue(QUuid{}.toString(QUuid::WithoutBraces)), QJsonValue(true)}) {
            auto row = first; row["sessions"] = QJsonArray{id}; row["active"] = QJsonValue(QJsonValue::Null);
            if (!rejectRow(row)) return false;
        }
        return true;
    }
    if (name == "active") {
        for (const auto& id : QStringList{"", "not-a-uuid", QUuid{}.toString(QUuid::WithoutBraces), sc}) {
            auto row = first; row["active"] = id;
            if (!rejectRow(row)) return false;
        }
        auto row = first; row["active"] = QJsonValue(QJsonValue::Null);
        auto object = root; object["windows"] = QJsonArray{row, second}; value[0].activeSession.reset();
        return write(path, QJsonDocument(object).toJson(QJsonDocument::Compact)) && matches(store.load(), value);
    }
    if (name == "read_limit") {
        auto bytes = QJsonDocument(root).toJson(QJsonDocument::Compact);
        bytes.append(QByteArray(1024 * 1024 - bytes.size(), ' '));
        if (!QDir().mkpath(profile) || !write(path, bytes) || !matches(store.load(), value)) return false;
        bytes.append(' ');
        return reject(bytes, StoreError::InvalidData);
    }
    if (name == "write_limit") {
        if (!matches(store.save(value, {}), value)) return false;
        const auto before = read(path);
        WindowLayout large;
        for (int i = 1; i <= 10000; ++i)
            large.append(WindowPlacement{QString("%1-0000-4000-8000-000000000000").arg(i, 8, 16, QLatin1Char('0')), {}, {}, QRect(0, 0, 1, 1), false});
        return failure(store.save(large, value), StoreError::InvalidInput) && read(path) == before && matches(store.load(), value);
    }
    if (name == "invalid_input") {
        if (!matches(store.save(value, {}), value)) return false;
        const auto before = read(path);
        for (int i = 0; i < 10; ++i) {
            auto invalid = value;
            if (i == 0) invalid[0].id = "invalid";
            if (i == 1) invalid[1].id = "main";
            if (i == 2) invalid[0].sessions.insert(QUuid{});
            if (i == 3) invalid[1].sessions.insert(a);
            if (i == 4) invalid[0].activeSession = c;
            if (i == 5) invalid[0].activeSession = QUuid{};
            if (i == 6) invalid[0].geometry = QRect(0, 0, 0, 10);
            if (i == 7) invalid[0].geometry = QRect(QPoint(std::numeric_limits<int>::min(), 0), QPoint(std::numeric_limits<int>::max(), 1));
            if (i == 8) invalid[0].geometry = QRect(0, 0, 10, 0);
            if (i == 9) invalid[0].geometry = QRect(QPoint(0, std::numeric_limits<int>::min()), QPoint(1, std::numeric_limits<int>::max()));
            if (!failure(store.save(invalid, value), StoreError::InvalidInput) || read(path) != before || !matches(store.load(), value)) return false;
        }
        return true;
    }
    if (name == "file_symlink") {
        const QString target = temporary.filePath("retained.json");
        const auto bytes = QJsonDocument(root).toJson(QJsonDocument::Compact);
        if (!QDir().mkpath(profile) || !write(target, bytes) || !QFile::link(target, path) || !QFileInfo(path).isSymLink()) return false;
        if (!failure(store.load(), StoreError::InvalidData) || !failure(store.save(value, {}), StoreError::InvalidData)
            || read(target) != bytes || !QFileInfo(path).isSymLink()) return false;
        if (!QFile::remove(target)) return false;
        return failure(store.load(), StoreError::InvalidData) && failure(store.save(value, {}), StoreError::InvalidData) && QFileInfo(path).isSymLink();
    }
    if (name == "profile_symlink") {
        const QString target = temporary.filePath("retained-profile"), targetPath = QDir(target).filePath("window-layout.json");
        const auto bytes = QJsonDocument(root).toJson(QJsonDocument::Compact);
        if (!QDir().mkpath(target) || !write(targetPath, bytes) || !QFile::link(target, profile) || !QFileInfo(profile).isSymLink()) return false;
        return failure(store.load(), StoreError::InvalidInput) && failure(store.save(value, {}), StoreError::InvalidInput)
            && read(targetPath) == bytes && QFileInfo(profile).isSymLink();
    }
    if (name == "file_directory") {
        return QDir().mkpath(path) && failure(store.load(), StoreError::ReadFailed)
            && failure(store.save(value, {}), StoreError::ReadFailed) && QFileInfo(path).isDir();
    }
    if (name == "profile_file") {
        return write(profile, "retained") && failure(store.load(), StoreError::ReadFailed)
            && failure(store.save(value, {}), StoreError::ReadFailed) && read(profile) == "retained";
    }
    if (name == "profile_invalid") {
        for (const auto& profileName : QStringList{"", "relative-profile"}) {
            const WindowLayoutStore invalid(profileName);
            if (!failure(invalid.load(), StoreError::InvalidInput) || !failure(invalid.save(value, {}), StoreError::InvalidInput)) return false;
        }
        return true;
    }
    if (name == "held_lock") {
        if (!matches(store.save(value, {}), value)) return false;
        const auto before = read(path);
        QLockFile held(path + ".lock"); held.setStaleLockTime(0);
        if (!held.tryLock(0)) return false;
        auto changed = value; changed[0].maximized = true;
        if (!failure(store.save(changed, value), StoreError::Busy) || read(path) != before || !matches(store.load(), value)) return false;
        held.unlock();
        return matches(store.save(changed, value), changed);
    }
    if (name == "lock_directory") {
        if (!matches(store.save(value, {}), value)) return false;
        const auto before = read(path);
        return QDir().mkpath(path + ".lock") && failure(store.save(value, value), StoreError::WriteFailed)
            && read(path) == before && matches(store.load(), value);
    }
    if (name == "lock_symlink") {
        const SessionStore sessions(profile);
        const auto created = sessions.create({"window-layout-lock-context", {"default"}}, "Lock fixture");
        if (!std::holds_alternative<SessionCatalog>(created)) return false;
        const auto catalog = std::get<SessionCatalog>(created);
        value[0].sessions = {catalog.sessions.first().id};
        value[0].activeSession = catalog.sessions.first().id;
        value[1].sessions.clear();
        if (!matches(store.save(value, {}), value)) return false;
        const QString sessionPath = QDir(profile).filePath("sessions.json");
        const QString target = temporary.filePath("retained-lock-target");
        const auto layoutBytes = read(path), sessionBytes = read(sessionPath);
        const QByteArray targetBytes("Retained lock target.\n");
        if (layoutBytes.isEmpty() || sessionBytes.isEmpty() || !write(target, targetBytes)
            || !QFile::link(target, path + ".lock") || !QFileInfo(path + ".lock").isSymLink()) return false;
        auto changed = value; changed[0].maximized = true;
        const auto saved = store.save(changed, value);
        const auto retained = sessions.list();
        return failure(saved, StoreError::WriteFailed) && read(target) == targetBytes
            && read(path) == layoutBytes && read(sessionPath) == sessionBytes
            && QFileInfo(path + ".lock").isSymLink() && matches(store.load(), value)
            && std::holds_alternative<SessionCatalog>(retained) && std::get<SessionCatalog>(retained) == catalog;
    }
    if (name == "permissions") {
        if (!matches(store.save(value, {}), value)) return false;
        const auto mask = QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner
            | QFileDevice::ReadGroup | QFileDevice::WriteGroup | QFileDevice::ExeGroup
            | QFileDevice::ReadOther | QFileDevice::WriteOther | QFileDevice::ExeOther;
        const auto directory = QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner;
        const auto file = QFileDevice::ReadOwner | QFileDevice::WriteOwner;
        if ((QFileInfo(profile).permissions() & mask) != directory || (QFileInfo(path).permissions() & mask) != file) return false;
        if (!QFile::setPermissions(path, file | QFileDevice::ReadGroup | QFileDevice::ReadOther)) return false;
        return matches(store.save(value, value), value) && (QFileInfo(path).permissions() & mask) == file;
    }
    std::fprintf(stderr, "Unknown window-layout scenario: %s\n", qPrintable(name));
    return false;
}
} // namespace

int main(int argc, char** argv) {
    QCoreApplication application(argc, argv);
    if (argc != 2) {
        std::fputs("Expected one window-layout scenario.\n", stderr);
        return 2;
    }
    const QString name = QString::fromLocal8Bit(argv[1]);
    if (scenario(name)) return 0;
    std::fprintf(stderr, "Window-layout scenario failed: %s\n", qPrintable(name));
    return 1;
}
