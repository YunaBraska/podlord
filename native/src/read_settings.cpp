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

namespace podlord {
bool ReadSettings::valid() const { return requestHardLimitPerMinute >= 0 && requestHardLimitPerMinute <= 60000 && inactiveSyncMinutes >= 0 && logLimitMb > 0 && yamlLimitMiB > 0 && radarWaterSpeedPercent >= 0 && radarWaterSpeedPercent <= 100 && collectionCacheHours > 0 && musicVolumePercent >= 0 && musicVolumePercent <= 100 && alertVolumePercent >= 0 && alertVolumePercent <= 100 && validAppearance(themeName, themeVariant) && validUiLanguage(language); }
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
    if (error.error != QJsonParseError::NoError || !document.isObject() || root["version"] != 10 || root.size() != 14)
        return Failure{StoreError::InvalidData, "Unsupported or invalid settings format. The file was left unchanged."};
    const auto integer = [](const QJsonValue& value, int maximum) {
        const int number = value.toInt(-1);
        return number >= 0 && number <= maximum;
    };
    if (!integer(root["requestHardLimitPerMinute"], 60000) || !integer(root["inactiveSyncMinutes"], std::numeric_limits<int>::max()))
        return Failure{StoreError::InvalidData, "Invalid request setting values."};
    if (!integer(root["logLimitMb"], std::numeric_limits<int>::max()) || root["logLimitMb"].toInt() == 0)
        return Failure{StoreError::InvalidData, "Log-size limit must be a positive whole number of MB."};
    if (!integer(root["yamlLimitMiB"], std::numeric_limits<int>::max()) || root["yamlLimitMiB"].toInt() == 0)
        return Failure{StoreError::InvalidData, "YAML limit must be a positive whole number of MiB."};
    if (!root["themeName"].isString() || !root["themeVariant"].isString())
        return Failure{StoreError::InvalidData, "Appearance settings must contain canonical text choices."};
    if (!root["radarWaterEnabled"].isBool() || !integer(root["radarWaterSpeedPercent"], 100))
        return Failure{StoreError::InvalidData, "Invalid radar water settings."};
    if (!root["workspaceRestore"].isBool())
        return Failure{StoreError::InvalidData, "Tab restoration requires an enable flag."};
    if (!root["language"].isString() || !validUiLanguage(root["language"].toString()))
        return Failure{StoreError::InvalidData, "Choose a supported language."};
    if (!integer(root["collectionCacheHours"], std::numeric_limits<int>::max()) || root["collectionCacheHours"].toInt() == 0)
        return Failure{StoreError::InvalidData, "Resource retention must be a positive whole number of hours."};
    if (!integer(root["musicVolumePercent"], 100) || !integer(root["alertVolumePercent"], 100))
        return Failure{StoreError::InvalidData, "Audio volumes must be whole-number percentages from 0 to 100."};
    ReadSettings settings{root["requestHardLimitPerMinute"].toInt(), root["inactiveSyncMinutes"].toInt(), root["logLimitMb"].toInt(), root["yamlLimitMiB"].toInt()};
    settings.themeName = root["themeName"].toString(); settings.themeVariant = root["themeVariant"].toString();
    settings.radarWaterEnabled = root["radarWaterEnabled"].toBool();
    settings.radarWaterSpeedPercent = root["radarWaterSpeedPercent"].toInt();
    settings.workspaceRestore = root["workspaceRestore"].toBool(); settings.language = root["language"].toString();
    settings.collectionCacheHours = root["collectionCacheHours"].toInt();
    settings.musicVolumePercent = root["musicVolumePercent"].toInt(); settings.alertVolumePercent = root["alertVolumePercent"].toInt();
    if (!settings.valid()) return Failure{StoreError::InvalidData, "Invalid profile appearance settings; existing data was retained."};
    return settings;
}
Result<ReadSettings> ReadSettingsStore::save(ReadSettings value, ReadSettings expected) const {
    if (!value.valid()) return Failure{StoreError::InvalidInput, "Choose valid request limits, a positive collection cache lifetime in whole hours, and canonical shipped appearance settings."};
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
    const QJsonObject root{{"version", 10}, {"requestHardLimitPerMinute", value.requestHardLimitPerMinute}, {"inactiveSyncMinutes", value.inactiveSyncMinutes}, {"logLimitMb", value.logLimitMb}, {"yamlLimitMiB", value.yamlLimitMiB}, {"themeName", value.themeName}, {"themeVariant", value.themeVariant}, {"radarWaterEnabled", value.radarWaterEnabled}, {"radarWaterSpeedPercent", value.radarWaterSpeedPercent}, {"workspaceRestore", value.workspaceRestore}, {"language", value.language}, {"collectionCacheHours", value.collectionCacheHours}, {"musicVolumePercent", value.musicVolumePercent}, {"alertVolumePercent", value.alertVolumePercent}};
    QSaveFile file(path); file.setDirectWriteFallback(false);
    const auto bytes = QJsonDocument(root).toJson(QJsonDocument::Indented);
    if (!file.open(QIODevice::WriteOnly) || !file.setPermissions(QFile::ReadOwner | QFile::WriteOwner) || file.write(bytes) != bytes.size() || !file.commit())
        return Failure{StoreError::WriteFailed, "Cannot atomically save private request settings."};
    return value;
}
} // namespace podlord
