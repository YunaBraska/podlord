#pragma once
#include "session_store.h"
#include <QAbstractListModel>
#include <QByteArray>
#include <QMap>

namespace podlord {
struct LogEntry final {
    QString id, timestamp, order, container;
    QByteArray text;
    qint64 bytes = 0;
};
/** In-memory Pod history. All containers share one UTF-8 retained-text budget. */
struct PodLogHistory final {
    enum class Removal { None, Limit, Expiry };
    Removal removal = Removal::None;
    struct Cursor final { QString timestamp, order; QMap<QByteArray, int> occurrences; QDateTime fetched; };
    QString path, uid, selected;
    QStringList containers;
    QList<LogEntry> entries, presented;
    QMap<QString, Cursor> cursors;
    QMap<QString, QString> failures;
    bool paused = false, limited = false;
    int pending = 0;
    qint64 cycleAt = -3000, retainedBytes = 0;
    std::optional<Failure> append(const QString& container, const QByteArray& bytes, QDateTime at, qint64 budget, bool responseLimited);
    bool publish();
    bool trim(qint64 budget);
    bool expire(QDateTime now);
    QString status() const;
};
/** Virtualized presentation; stable entry IDs survive incremental insert/remove updates. */
class LogRows final : public QAbstractListModel {
    Q_OBJECT
public:
    explicit LogRows(QObject* parent = nullptr);
    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;
    bool publish(const QList<LogEntry>& entries);
    Q_INVOKABLE QString entryId(int index) const;
    Q_INVOKABLE int findEntry(const QString& id) const;
private:
    QList<LogEntry> entries_;
};
} // namespace podlord
