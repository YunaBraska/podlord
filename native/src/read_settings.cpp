#include "read_settings.h"
#include "appearance.h"
#include "ui_language.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QLockFile>
#include <QSaveFile>
#include <limits>
#include <array>

namespace podlord {
bool ReadSettings::valid() const { return requestHardLimitPerMinute >= 0 && requestHardLimitPerMinute <= 60000 && inactiveSyncMinutes >= 0 && logLimitMb > 0 && yamlLimitMiB > 0 && radarWaterSpeedPercent >= 0 && radarWaterSpeedPercent <= 100 && validAppearance(themeName, themeVariant) && validUiLanguage(language); }
ReadSettingsStore::ReadSettingsStore(QString profile) : profile_(std::move(profile)) {}
Result<ReadSettings> ReadSettingsStore::load() const {
    if (const auto failure = profileFailure(profile_)) return *failure;
    const auto path = QDir(profile_).filePath("read-settings.json");
    if (QFileInfo(path).isSymLink()) return Failure{StoreError::InvalidData, "Read settings must not be a symbolic link."};
    if (!QFileInfo::exists(path)) return ReadSettings{};
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() > 65536) return Failure{StoreError::ReadFailed, "Cannot read profile request settings."};
    const auto bytes = file.readAll();
    if (file.error() != QFileDevice::NoError) return Failure{StoreError::ReadFailed, "Cannot read profile request settings."};
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(bytes, &error);
    const auto root = document.object();
    const int version = root["version"].toInt(-1);
    constexpr std::array fieldCounts{3, 4, 5, 8, 10, 11, 12, 11};
    if (error.error != QJsonParseError::NoError || !document.isObject() || version < 1 || version > static_cast<int>(fieldCounts.size()) || root.size() != fieldCounts[version - 1])
        return Failure{StoreError::InvalidData, "Invalid profile request settings; existing data was retained."};
    const auto integer = [](const QJsonValue& value, int maximum) {
        const int number = value.toInt(-1);
        return number >= 0 && number <= maximum;
    };
    if (!integer(root["requestHardLimitPerMinute"], 60000) || !integer(root["inactiveSyncMinutes"], std::numeric_limits<int>::max()))
        return Failure{StoreError::InvalidData, "Invalid profile request setting values."};
    if (version >= 2 && (!integer(root["logLimitMb"], std::numeric_limits<int>::max()) || root["logLimitMb"].toInt() == 0))
        return Failure{StoreError::InvalidData, "Log-size limit must be a positive whole number of MB."};
    if (version >= 3 && (!integer(root["yamlLimitMiB"], std::numeric_limits<int>::max()) || root["yamlLimitMiB"].toInt() == 0))
        return Failure{StoreError::InvalidData, "YAML limit must be a positive whole number of MiB."};
    ReadSettings settings{root["requestHardLimitPerMinute"].toInt(), root["inactiveSyncMinutes"].toInt(), version >= 2 ? root["logLimitMb"].toInt() : 5, version >= 3 ? root["yamlLimitMiB"].toInt() : 3};
    if (version >= 4) {
        if (!root["themeName"].isString() || !root["themeVariant"].isString() || (version >= 8 && root.contains("themeIntensity")))
            return Failure{StoreError::InvalidData, "Appearance settings must contain canonical text choices."};
        settings.themeName = root["themeName"].toString(); settings.themeVariant = root["themeVariant"].toString();
    }
    if (version >= 5) {
        if (!root["radarWaterEnabled"].isBool() || !integer(root["radarWaterSpeedPercent"],100))
            return Failure{StoreError::InvalidData, "Radar water requires an enable flag and a whole-number speed from 0 to 100."};
        settings.radarWaterEnabled = root["radarWaterEnabled"].toBool();
        settings.radarWaterSpeedPercent = root["radarWaterSpeedPercent"].toInt();
    }
    if (version >= 6) {
        if (!root["workspaceRestore"].isBool()) return Failure{StoreError::InvalidData, "Workspace restoration requires an enable flag."};
        settings.workspaceRestore = root["workspaceRestore"].toBool();
    }
    if (version >= 7) {
        if (!root["language"].isString() || !validUiLanguage(root["language"].toString()))
            return Failure{StoreError::InvalidData, "UI language must be a shipped language code or system; existing settings were retained."};
        settings.language = root["language"].toString();
    }
    if (!settings.valid()) return Failure{StoreError::InvalidData, "Invalid profile appearance settings; existing data was retained."};
    return settings;
}
Result<ReadSettings> ReadSettingsStore::save(ReadSettings value, ReadSettings expected) const {
    if (!value.valid()) return Failure{StoreError::InvalidInput, "Choose valid request limits and canonical shipped appearance settings."};
    if (const auto failure = profileFailure(profile_)) return *failure;
    const bool created = !QFileInfo::exists(profile_);
    if (!QDir().mkpath(profile_) || (created && !QFile::setPermissions(profile_, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner)))
        return Failure{StoreError::WriteFailed, "Cannot create a private settings profile."};
    const auto path = QDir(profile_).filePath("read-settings.json");
    if (QFileInfo(path + ".lock").isDir())
        return Failure{StoreError::WriteFailed, "Request settings lock path is a directory."};
    QLockFile lock(path + ".lock"); lock.setStaleLockTime(0);
    if (!lock.tryLock(0)) {
        if (lock.error() == QLockFile::LockFailedError)
            return Failure{StoreError::Busy, "Request settings are being changed; retry explicitly."};
        return Failure{StoreError::WriteFailed, "Cannot lock private request settings."};
    }
    const auto current = load();
    if (const auto* failure = std::get_if<Failure>(&current)) return *failure;
    if (std::get<ReadSettings>(current) != expected) return Failure{StoreError::Conflict, "Request settings changed in another window. Reload before saving."};
    const QJsonObject root{{"version", 8}, {"requestHardLimitPerMinute", value.requestHardLimitPerMinute}, {"inactiveSyncMinutes", value.inactiveSyncMinutes}, {"logLimitMb", value.logLimitMb}, {"yamlLimitMiB", value.yamlLimitMiB}, {"themeName", value.themeName}, {"themeVariant", value.themeVariant}, {"radarWaterEnabled", value.radarWaterEnabled}, {"radarWaterSpeedPercent", value.radarWaterSpeedPercent}, {"workspaceRestore", value.workspaceRestore}, {"language", value.language}};
    QSaveFile file(path); file.setDirectWriteFallback(false);
    const auto bytes = QJsonDocument(root).toJson(QJsonDocument::Indented);
    if (!file.open(QIODevice::WriteOnly) || !file.setPermissions(QFile::ReadOwner | QFile::WriteOwner) || file.write(bytes) != bytes.size() || !file.commit())
        return Failure{StoreError::WriteFailed, "Cannot atomically save private request settings."};
    return value;
}
} // namespace podlord
