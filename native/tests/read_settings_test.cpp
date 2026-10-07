#include "read_settings.h"
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
bool require(bool condition, const char* message) {
    if (!condition) std::fprintf(stderr, "%s\n", message);
    return condition;
}
bool failure(const Result<ReadSettings>& result, StoreError code) {
    const auto* error = std::get_if<Failure>(&result);
    if (error && error->code != code) std::fprintf(stderr, "Expected %s, received %s: %s\n", qPrintable(errorName(code)), qPrintable(errorName(error->code)), qPrintable(error->message));
    return require(error && error->code == code && !error->message.isEmpty(), "Unexpected settings result or missing error explanation.");
}
bool value(const Result<ReadSettings>& result, ReadSettings expected) {
    const auto* settings = std::get_if<ReadSettings>(&result);
    return require(settings && *settings == expected, "Unexpected settings values.");
}
bool write(const QString& path, const QByteArray& bytes) {
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}
QByteArray read(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    return file.readAll();
}
bool scenario(const QString& name) {
    QTemporaryDir temporary;
    if (!require(temporary.isValid(), "Cannot create isolated test profile.")) return false;
    const auto profile = temporary.filePath("profile");
    const auto path = QDir(profile).filePath("read-settings.json");
    const ReadSettingsStore store(profile);
    if (name.startsWith("water_")) {
        auto desired=ReadSettings{};
        desired.radarWaterEnabled=false; desired.radarWaterSpeedPercent=0;
        if (name=="water_save") return value(store.save(desired,{}),desired) && value(store.load(),desired);
        if (name=="water_maximum") { desired.radarWaterEnabled=true; desired.radarWaterSpeedPercent=100; return value(store.save(desired,{}),desired) && value(store.load(),desired); }
        if (name=="water_invalid_low" || name=="water_invalid_high") {
            desired.radarWaterSpeedPercent=name=="water_invalid_low" ? -1 : 101;
            return failure(store.save(desired,{}),StoreError::InvalidInput) && !QFileInfo::exists(profile);
        }
        if (!QDir().mkpath(profile)) return false;
        QJsonObject root{{"version",4},{"requestHardLimitPerMinute",0},{"inactiveSyncMinutes",0},{"logLimitMb",5},{"yamlLimitMiB",3},
            {"themeName","Sirocco Command"},{"themeVariant","dark"},{"themeIntensity","subtle"}};
        if (name=="water_upgrade") return write(path,QJsonDocument(root).toJson()) && value(store.load(),{}) && value(store.save(desired,{}),desired) && value(store.load(),desired);
        root["version"]=5; root["radarWaterEnabled"]=true; root["radarWaterSpeedPercent"]=45;
        if (name=="water_flag_invalid") root["radarWaterEnabled"]="true";
        else if (name=="water_speed_fraction") root["radarWaterSpeedPercent"]=45.5;
        else if (name=="water_speed_string") root["radarWaterSpeedPercent"]="45";
        else if (name=="water_speed_low") root["radarWaterSpeedPercent"]=-1;
        else if (name=="water_speed_high") root["radarWaterSpeedPercent"]=101;
        else if (name=="water_missing") root.remove("radarWaterEnabled");
        else return false;
        const auto bytes=QJsonDocument(root).toJson();
        return write(path,bytes) && failure(store.load(),StoreError::InvalidData) && read(path)==bytes;
    }
    if (name == "yaml_default") return value(store.load(), {0, 0, 5, 3});
    if (name == "yaml_save") return value(store.save({120, 2, 6, 1}, {}), {120, 2, 6, 1}) && value(store.load(), {120, 2, 6, 1});
    if (name == "yaml_zero") return failure(store.save({0, 0, 5, 0}, {}), StoreError::InvalidInput) && !QFileInfo::exists(profile);
    if (name == "yaml_negative") return failure(store.save({0, 0, 5, -1}, {}), StoreError::InvalidInput) && !QFileInfo::exists(profile);
    if (name == "yaml_conflict") return value(store.save({0, 0, 5, 4}, {}), {0, 0, 5, 4}) && failure(store.save({0, 0, 5, 1}, {}), StoreError::Conflict) && value(store.load(), {0, 0, 5, 4});
    if (name == "missing") return value(store.load(), {}) && require(!QFileInfo::exists(profile), "Read created a missing profile.");
    if (name == "empty_profile") return failure(ReadSettingsStore({}).load(), StoreError::InvalidInput) && failure(ReadSettingsStore({}).save({}, {}), StoreError::InvalidInput);
    if (name == "relative_profile") return failure(ReadSettingsStore("relative").load(), StoreError::InvalidInput) && failure(ReadSettingsStore("relative").save({}, {}), StoreError::InvalidInput);
    if (name == "profile_file") {
        if (!write(profile, "retained")) return false;
        return failure(store.load(), StoreError::ReadFailed) && failure(store.save({}, {}), StoreError::ReadFailed) && read(profile) == "retained";
    }
    if (name == "parent_file") {
        const auto blocker = temporary.filePath("blocker");
        if (!write(blocker, "retained")) return false;
        return failure(ReadSettingsStore(blocker + "/child").save({}, {}), StoreError::WriteFailed) && read(blocker) == "retained";
    }
    if (name == "invalid_limit_low" || name == "invalid_limit_high" || name == "invalid_inactive") {
        const ReadSettings invalid{name == "invalid_limit_low" ? -1 : name == "invalid_limit_high" ? 60001 : 0, name == "invalid_inactive" ? -1 : 0};
        return failure(store.save(invalid, {}), StoreError::InvalidInput) && !QFileInfo::exists(profile);
    }
    if (name == "maximum" || name == "zero" || name == "private") {
        const ReadSettings expected = name == "maximum" ? ReadSettings{60000, std::numeric_limits<int>::max()} : ReadSettings{};
        if (!value(store.save(expected, {}), expected) || !value(store.load(), expected)) return false;
        if (name != "private") return true;
        const auto directory = QFileInfo(profile).permissions();
        const auto file = QFileInfo(path).permissions();
        return require(!(directory & (QFile::ReadGroup | QFile::WriteGroup | QFile::ExeGroup | QFile::ReadOther | QFile::WriteOther | QFile::ExeOther)) &&
                       !(file & (QFile::ReadGroup | QFile::WriteGroup | QFile::ExeGroup | QFile::ReadOther | QFile::WriteOther | QFile::ExeOther)), "Settings profile or file exposes other-user access.");
    }
    if (!QDir().mkpath(profile)) return false;
    if (name == "schema_v3_upgrade") {
        if (!write(path, "{\"version\":3,\"requestHardLimitPerMinute\":120,\"inactiveSyncMinutes\":2,\"logLimitMb\":6,\"yamlLimitMiB\":4}")) return false;
        return value(store.load(), {120, 2, 6, 4}) && value(store.save({120, 2, 6, 5}, {120, 2, 6, 4}), {120, 2, 6, 5}) && value(store.load(), {120, 2, 6, 5}) && QJsonDocument::fromJson(read(path)).object()["version"] == 7;
    }
    if (name == "version_two_yaml_upgrade") {
        if (!write(path, "{\"version\":2,\"requestHardLimitPerMinute\":120,\"inactiveSyncMinutes\":2,\"logLimitMb\":6}")) return false;
        return value(store.load(), {120, 2, 6, 3}) && value(store.save({120, 2, 6, 4}, {120, 2, 6, 3}), {120, 2, 6, 4}) && value(store.load(), {120, 2, 6, 4});
    }
    if (name == "version_one_upgrade") {
        if (!write(path, "{\"version\":1,\"requestHardLimitPerMinute\":120,\"inactiveSyncMinutes\":2}")) return false;
        return value(store.load(), {120, 2, 5}) && value(store.save({120, 2, 6}, {120, 2, 5}), {120, 2, 6}) && value(store.load(), {120, 2, 6}) && QJsonDocument::fromJson(read(path)).object()["version"] == 7;
    }
    if (name == "conflict" || name == "repeat") {
        if (!value(store.save({120, 1}, {}), {120, 1})) return false;
        const auto bytes = read(path);
        return name == "conflict" ? failure(store.save({121, 2}, {}), StoreError::Conflict) && read(path) == bytes : value(store.save({120, 1}, {120, 1}), {120, 1}) && value(store.load(), {120, 1});
    }
    if (name == "concurrent") {
        std::latch ready(2), start(1);
        const auto save = [&](ReadSettings settings) {
            ready.count_down(); start.wait();
            return ReadSettingsStore(profile).save(settings, {});
        };
        auto first = std::async(std::launch::async, save, ReadSettings{120, 1});
        auto second = std::async(std::launch::async, save, ReadSettings{121, 2});
        ready.wait(); start.count_down();
        const auto a = first.get(), b = second.get();
        const bool aWon = std::holds_alternative<ReadSettings>(a), bWon = std::holds_alternative<ReadSettings>(b);
        if (!require(aWon != bWon, "Concurrent stale writers both succeeded or both failed.")) return false;
        const auto& loser = aWon ? b : a;
        const auto* error = std::get_if<Failure>(&loser);
        return require(error && (error->code == StoreError::Busy || error->code == StoreError::Conflict), "Concurrent writer failed ambiguously.") && value(store.load(), std::get<ReadSettings>(aWon ? a : b));
    }
    if (name == "busy") {
        QLockFile lock(path + ".lock");
        if (!lock.tryLock(0)) return false;
        return failure(store.save({}, {}), StoreError::Busy) && !QFileInfo::exists(path);
    }
    if (name == "lock_io") {
        if (!QDir().mkpath(path + ".lock")) return false;
        return failure(store.save({}, {}), StoreError::WriteFailed) && !QFileInfo::exists(path);
    }
    if (name == "settings_directory") {
        if (!QDir().mkpath(path)) return false;
        return failure(store.load(), StoreError::ReadFailed) && failure(store.save({}, {}), StoreError::ReadFailed) && QFileInfo(path).isDir();
    }
#ifdef Q_OS_UNIX
    if (name == "lock_denied") {
        if (!QFile::setPermissions(profile, QFile::ReadOwner | QFile::ExeOwner)) return false;
        const auto result = store.save({}, {});
        const bool restored = QFile::setPermissions(profile, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
        return restored && failure(result, StoreError::WriteFailed) && !QFileInfo::exists(path);
    }
    if (name == "profile_symlink" || name == "settings_symlink") {
        const auto target = temporary.filePath("target");
        if (name == "profile_symlink") {
            const auto link = temporary.filePath("linked");
            if (!QDir().mkpath(target) || !QFile::link(target, link)) return false;
            return failure(ReadSettingsStore(link).load(), StoreError::InvalidInput) && failure(ReadSettingsStore(link).save({}, {}), StoreError::InvalidInput);
        }
        if (!write(target, "retained") || !QFile::link(target, path)) return false;
        return failure(store.load(), StoreError::InvalidData) && failure(store.save({}, {}), StoreError::InvalidData) && read(target) == "retained" && QFileInfo(path).isSymLink();
    }
    if (name == "read_denied") {
        if (!write(path, "retained") || !QFile::setPermissions(path, {})) return false;
        const auto result = store.load();
        const bool restored = QFile::setPermissions(path, QFile::ReadOwner | QFile::WriteOwner);
        return restored && failure(result, StoreError::ReadFailed) && read(path) == "retained";
    }
#endif
    QByteArray bytes;
    if (name == "oversized") bytes = QByteArray(65537, ' ');
    else if (name == "malformed") bytes = "{";
    else if (name == "empty_document") bytes = "";
    else if (name == "array_root") bytes = "[]";
    else if (name == "scalar_root") bytes = "123";
    else {
        QJsonObject root{{"version", 1}, {"requestHardLimitPerMinute", 0}, {"inactiveSyncMinutes", 0}};
        if (name.startsWith("schema_")) {
            const bool appearance = name.startsWith("schema_theme_") || name.startsWith("schema_v4_");
            root["version"] = appearance ? 4 : 3;
            root["logLimitMb"] = 5;
            root["yamlLimitMiB"] = 3;
            if (appearance) {
                root["themeName"] = "Sirocco Command";
                root["themeVariant"] = "dark";
                root["themeIntensity"] = "subtle";
            }
            if (name == "schema_yaml_zero" || name == "schema_v4_yaml_zero") root["yamlLimitMiB"] = 0;
            else if (name == "schema_yaml_negative") root["yamlLimitMiB"] = -1;
            else if (name == "schema_yaml_fraction") root["yamlLimitMiB"] = 1.5;
            else if (name == "schema_yaml_string") root["yamlLimitMiB"] = "3";
            else if (name == "schema_yaml_high") root["yamlLimitMiB"] = 2147483648.0;
            else if (name == "schema_yaml_null") root["yamlLimitMiB"] = QJsonValue::Null;
            else if (name == "schema_yaml_missing") root.remove("yamlLimitMiB");
            else if (name == "schema_v3_extra" || name == "schema_v4_extra") root["extra"] = 0;
            else if (name == "schema_theme_name_number") root["themeName"] = 1;
            else if (name == "schema_theme_variant_null") root["themeVariant"] = QJsonValue::Null;
            else if (name == "schema_theme_intensity_object") root["themeIntensity"] = QJsonObject{};
            else if (name == "schema_theme_name_unknown") root["themeName"] = "Unknown";
            else if (name == "schema_theme_variant_unknown") root["themeVariant"] = "Unknown";
            else if (name == "schema_theme_intensity_unknown") root["themeIntensity"] = "Unknown";
            else return require(false, "Unknown settings schema scenario.");
        } else if (name.startsWith("log_")) {
            root["version"] = 2;
            if (name == "log_zero") root["logLimitMb"] = 0;
            else if (name == "log_negative") root["logLimitMb"] = -1;
            else if (name == "log_fraction") root["logLimitMb"] = 1.5;
            else if (name == "log_string") root["logLimitMb"] = "5";
            else if (name == "log_high") root["logLimitMb"] = 2147483648.0;
            else if (name == "log_null") root["logLimitMb"] = QJsonValue::Null;
            else if (name != "log_missing") return require(false, "Unknown log-size scenario.");
        } else if (name == "extra_key") root["extra"] = 0;
        else if (name == "missing_key") root.remove("inactiveSyncMinutes");
        else if (name == "version_string") root["version"] = "1";
        else if (name == "version_future") root["version"] = 999;
        else {
            const bool limit = name.startsWith("limit_");
            const auto key = limit ? "requestHardLimitPerMinute" : "inactiveSyncMinutes";
            if (name.endsWith("string")) root[key] = "1";
            else if (name.endsWith("negative")) root[key] = -1;
            else if (name.endsWith("fraction")) root[key] = 0.5;
            else if (name.endsWith("high")) root[key] = limit ? 60001.0 : 2147483648.0;
            else if (name.endsWith("null")) root[key] = QJsonValue::Null;
            else return require(false, "Unknown settings scenario.");
        }
        bytes = QJsonDocument(root).toJson(QJsonDocument::Compact);
    }
    if (!write(path, bytes)) return false;
    const auto code = name == "oversized" ? StoreError::ReadFailed : StoreError::InvalidData;
    return failure(store.load(), code) && failure(store.save({120, 1}, {}), code) && require(read(path) == bytes, "Failed read/save overwrote original settings.");
}
} // namespace

int main(int argc, char** argv) {
    QCoreApplication application(argc, argv);
    const auto arguments = application.arguments();
    if (arguments.size() != 2) return 2;
    return scenario(arguments[1]) ? 0 : 1;
}
