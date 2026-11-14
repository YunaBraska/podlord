#pragma once
#include "session_store.h"

namespace podlord {
struct ReadSettings final {
    int requestHardLimitPerMinute = 0;
    int inactiveSyncMinutes = 0;
    int logLimitMb = 5;
    int yamlLimitMiB = 3;
    QString themeName = "Sirocco Command";
    QString themeVariant = "dark";
    bool radarWaterEnabled = true;
    int radarWaterSpeedPercent = 5;
    bool workspaceRestore = true;
    QString language = "system";
    int collectionCacheHours = 24;
    int musicVolumePercent = 35;
    int alertVolumePercent = 100;
    bool operator==(const ReadSettings&) const = default;
    bool valid() const;
};
/** Owns local application preferences and read policy without duplicating source or session indexes. */
class ReadSettingsStore final {
public:
    explicit ReadSettingsStore(QString profile);
    Result<ReadSettings> load() const;
    /** Atomic compare-and-save prevents a stale settings dialog overwriting another window. */
    Result<ReadSettings> save(ReadSettings value, ReadSettings expected) const;
private:
    const QString profile_;
};
} // namespace podlord
