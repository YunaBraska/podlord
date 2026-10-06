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
    QString themeIntensity = "subtle";
    bool radarWaterEnabled = true;
    int radarWaterSpeedPercent = 45;
    bool workspaceRestore = true;
    bool operator==(const ReadSettings&) const = default;
    bool valid() const;
};
/** Owns the private profile's read policy; no source or session index is duplicated. */
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
