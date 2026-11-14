#pragma once

#include "session_store.h"
#include <QRect>
#include <QSet>

namespace podlord {

struct WindowPlacement final {
    QString id;
    QSet<QUuid> sessions;
    std::optional<QUuid> activeSession;
    QRect geometry;
    bool maximized = false;
    bool operator==(const WindowPlacement&) const = default;
};
using WindowLayout = QList<WindowPlacement>;

/** Private window placement referencing, but never modifying, the session catalog. */
class WindowLayoutStore final {
public:
    explicit WindowLayoutStore(QString profile);
    /** Reads a strict version-1 layout without creating a missing profile or file. */
    Result<WindowLayout> load() const;
    /** Atomically saves only if the current layout equals expected, retaining rejected data. */
    Result<WindowLayout> save(WindowLayout value, const WindowLayout& expected) const;
private:
    const QString profile_;
};

} // namespace podlord
