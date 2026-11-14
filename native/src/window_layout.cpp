#include "window_layout.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLockFile>
#include <QSaveFile>
#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace podlord {
namespace {
constexpr qint64 maximumBytes = 1024 * 1024;

bool validLayout(const WindowLayout& layout) {
    QSet<QString> windows;
    QSet<QUuid> sessions;
    for (const auto& window : layout) {
        const QUuid id(window.id);
        if ((window.id != "main" && (id.isNull() || id.toString(QUuid::WithoutBraces) != window.id))
            || windows.contains(window.id)) return false;
        windows.insert(window.id);
        const qint64 width = qint64(window.geometry.right()) - window.geometry.left() + 1;
        const qint64 height = qint64(window.geometry.bottom()) - window.geometry.top() + 1;
        if (width <= 0 || height <= 0 || width > std::numeric_limits<int>::max()
            || height > std::numeric_limits<int>::max()) return false;
        for (const auto& session : window.sessions) {
            if (session.isNull() || sessions.contains(session)) return false;
            sessions.insert(session);
        }
        if (window.activeSession && !window.sessions.contains(*window.activeSession)) return false;
    }
    return true;
}
} // namespace

WindowLayoutStore::WindowLayoutStore(QString profile) : profile_(std::move(profile)) {}

Result<WindowLayout> WindowLayoutStore::load() const {
    if (const auto failure = profileFailure(profile_)) return *failure;
    const auto invalid = []() -> Result<WindowLayout> {
        return Failure{StoreError::InvalidData, "Invalid window layout; original data retained."};
    };
    const QString path = QDir(profile_).filePath("window-layout.json");
    const QFileInfo info(path);
    if (info.isSymLink()) return invalid();
    if (!info.exists()) return WindowLayout{};
    if (!info.isFile()) return Failure{StoreError::ReadFailed, "Window layout is not a regular file."};
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return Failure{StoreError::ReadFailed, "Cannot read window layout."};
    if (file.size() > maximumBytes) return invalid();
    const auto bytes = file.read(maximumBytes + 1);
    if (file.error() != QFileDevice::NoError) return Failure{StoreError::ReadFailed, "Cannot read window layout."};
    if (bytes.size() > maximumBytes) return invalid();
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(bytes, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) return invalid();
    const auto integer = [](const QJsonValue& value) -> std::optional<qint64> {
        if (!value.isDouble()) return {};
        const double number = value.toDouble();
        if (!std::isfinite(number) || std::trunc(number) != number
            || number < std::numeric_limits<int>::min() || number > std::numeric_limits<int>::max()) return {};
        return static_cast<qint64>(number);
    };
    const auto root = document.object();
    const auto version = integer(root["version"]);
    if (!version) return invalid();
    if (*version != 1) return Failure{StoreError::UnsupportedVersion, "Unsupported window layout version; original data retained."};
    if (root.size() != 2 || !root["windows"].isArray()) return invalid();
    WindowLayout layout;
    for (const auto entry : root["windows"].toArray()) {
        if (!entry.isObject()) return invalid();
        const auto row = entry.toObject();
        if (row.size() != 8 || !row["id"].isString() || !row["sessions"].isArray()
            || (!row["active"].isNull() && !row["active"].isString()) || !row["maximized"].isBool()) return invalid();
        const auto x = integer(row["x"]), y = integer(row["y"]);
        const auto width = integer(row["width"]), height = integer(row["height"]);
        if (!x || !y || !width || !height || *width <= 0 || *height <= 0) return invalid();
        const qint64 right = *x + *width - 1, bottom = *y + *height - 1;
        if (right < std::numeric_limits<int>::min() || right > std::numeric_limits<int>::max()
            || bottom < std::numeric_limits<int>::min() || bottom > std::numeric_limits<int>::max()) return invalid();
        WindowPlacement window{row["id"].toString(), {}, {},
            QRect(QPoint(static_cast<int>(*x), static_cast<int>(*y)),
                  QPoint(static_cast<int>(right), static_cast<int>(bottom))), row["maximized"].toBool()};
        for (const auto reference : row["sessions"].toArray()) {
            if (!reference.isString()) return invalid();
            const QUuid session(reference.toString());
            if (session.isNull() || window.sessions.contains(session)) return invalid();
            window.sessions.insert(session);
        }
        if (row["active"].isString()) window.activeSession = QUuid(row["active"].toString());
        layout.append(std::move(window));
    }
    if (!validLayout(layout)) return invalid();
    return layout;
}

Result<WindowLayout> WindowLayoutStore::save(WindowLayout value, const WindowLayout& expected) const {
    if (const auto failure = profileFailure(profile_)) return *failure;
    if (!validLayout(value)) return Failure{StoreError::InvalidInput, "Choose valid window geometry and distinct window/session identities."};
    QJsonArray windows;
    for (const auto& window : value) {
        QStringList sessions;
        for (const auto& session : window.sessions) sessions.append(session.toString(QUuid::WithoutBraces));
        std::sort(sessions.begin(), sessions.end());
        windows.append(QJsonObject{{"id", window.id}, {"sessions", QJsonArray::fromStringList(sessions)},
            {"active", window.activeSession ? QJsonValue(window.activeSession->toString(QUuid::WithoutBraces)) : QJsonValue(QJsonValue::Null)},
            {"x", window.geometry.x()}, {"y", window.geometry.y()}, {"width", window.geometry.width()},
            {"height", window.geometry.height()}, {"maximized", window.maximized}});
    }
    const auto bytes = QJsonDocument(QJsonObject{{"version", 1}, {"windows", windows}}).toJson(QJsonDocument::Compact);
    if (bytes.size() > maximumBytes) return Failure{StoreError::InvalidInput, "Window layout exceeds the 1 MiB limit."};
    const bool exists = QDir(profile_).exists();
    if (!QDir().mkpath(profile_)) return Failure{StoreError::WriteFailed, "Cannot create window layout profile."};
    if (!exists && !QFile::setPermissions(profile_, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner))
        return Failure{StoreError::WriteFailed, "Cannot protect window layout profile."};
    const QString directory = QFileInfo(profile_).canonicalFilePath();
    if (directory.isEmpty()) return Failure{StoreError::WriteFailed, "Cannot resolve window layout profile."};
    const QString path = QDir(directory).filePath("window-layout.json");
    const QFileInfo lockInfo(path + ".lock");
    if (lockInfo.isDir() || lockInfo.isSymLink()) return Failure{StoreError::WriteFailed, "Invalid window layout lock path."};
    QLockFile lock(path + ".lock");
    lock.setStaleLockTime(0);
    if (!lock.tryLock(0)) {
        if (lock.error() == QLockFile::LockFailedError) return Failure{StoreError::Busy, "Window layout is in use; operation was not applied."};
        return Failure{StoreError::WriteFailed, "Cannot lock window layout."};
    }
    const auto current = WindowLayoutStore(directory).load();
    if (const auto* failure = std::get_if<Failure>(&current)) return *failure;
    if (std::get<WindowLayout>(current) != expected) return Failure{StoreError::Conflict, "Window layout changed; reload before saving."};
    QSaveFile file(path);
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly)
        || !file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner)
        || file.write(bytes) != bytes.size() || !file.commit())
        return Failure{StoreError::WriteFailed, "Cannot atomically save window layout; original data retained."};
    return value;
}

} // namespace podlord
