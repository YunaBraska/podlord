#include "pod_logs.h"
#include <QCryptographicHash>
#include <QRegularExpression>
#include <QSet>
#include <QStringDecoder>
#include <algorithm>
#include <tuple>
#include <iterator>

namespace podlord {
namespace {
bool before(const LogEntry& a, const LogEntry& b) { return std::tie(a.order, a.container, a.id) < std::tie(b.order, b.container, b.id); }
bool trimEntries(QList<LogEntry>& entries, qint64& size, qint64 budget) {
    qsizetype count = 0;
    while (count < entries.size() && size > budget) size -= entries[count++].bytes;
    if (count) entries.remove(0, count);
    return count > 0;
}
}
std::optional<Failure> PodLogHistory::append(const QString& container, const QByteArray& bytes, QDateTime at, qint64 budget, bool responseLimited) {
    if (!bytes.isValidUtf8()) return Failure{StoreError::InvalidData, "Invalid UTF-8 log response; cached entries retained."};
    static const QRegularExpression stamp("^(\\d{4}-\\d{2}-\\d{2}T\\d{2}:\\d{2}:\\d{2})(?:\\.(\\d{1,9}))?(Z|[+-]\\d{2}:\\d{2})$");
    QList<LogEntry> incoming;
    const auto previous = cursors.value(container);
    Cursor next = previous;
    QMap<QByteArray, int> occurrences;
    QString occurrenceOrder;
    QSet<QString> retained;
    for (const auto& entry : entries) retained.insert(entry.id);
    const auto containerBytes = container.toUtf8();
    qsizetype offset = bytes.startsWith("\xef\xbb\xbf") ? 3 : 0;
    while (offset < bytes.size()) {
        const auto newline = bytes.indexOf('\n', offset);
        const auto end = newline < 0 ? bytes.size() : newline;
        const QByteArrayView line(bytes.constData() + offset, end - offset);
        offset = end + 1;
        if (line.isEmpty()) continue;
        const auto separator = line.indexOf(' ');
        if (separator < 0) return Failure{StoreError::InvalidData, "Missing or malformed log timestamp; cached entries retained."};
        const auto timestamp = QString::fromUtf8(line.first(separator));
        const auto match = stamp.match(timestamp);
        if (!match.hasMatch()) return Failure{StoreError::InvalidData, "Missing or malformed log timestamp; cached entries retained."};
        const auto date = QDateTime::fromString(match.captured(1) + match.captured(3), Qt::ISODate);
        if (!date.isValid()) return Failure{StoreError::InvalidData, "Invalid log timestamp; cached entries retained."};
        const auto order = date.toUTC().toString("yyyy-MM-ddTHH:mm:ss") + '.' + match.captured(2).leftJustified(9, '0');
        if (order < previous.order) continue;
        if (order != occurrenceOrder) { occurrences.clear(); occurrenceOrder = order; }
        const auto message = line.sliced(separator + 1).toByteArray();
        const auto fingerprint = QCryptographicHash::hash(order.toUtf8() + '\n' + message, QCryptographicHash::Sha256);
        const int occurrence = ++occurrences[fingerprint];
        if (order > next.order) { next.order = order; next.timestamp = order + 'Z'; next.occurrences.clear(); }
        if (order == next.order) next.occurrences[fingerprint] = std::max(next.occurrences.value(fingerprint), occurrence);
        if (order == previous.order && occurrence <= previous.occurrences.value(fingerprint)) continue;
        const auto id = QString::fromLatin1(QCryptographicHash::hash(containerBytes + '\n' + fingerprint + '\n' + QByteArray::number(occurrence), QCryptographicHash::Sha256).toHex());
        if (retained.contains(id)) continue;
        retained.insert(id);
        incoming.append({id, timestamp, order, container, message, timestamp.toUtf8().size() + containerBytes.size() + message.size() + 6});
    }
    next.fetched = at;
    // Keep the last response's boundary counts bounded with its actual payload.
    if (occurrenceOrder == next.order) next.occurrences = occurrences;
    cursors.insert(container, std::move(next));
    failures.remove(container);
    limited = limited || responseLimited;
    std::sort(incoming.begin(), incoming.end(), before);
    if (!incoming.isEmpty()) {
        QList<LogEntry> merged;
        merged.reserve(entries.size() + incoming.size());
        std::merge(entries.cbegin(), entries.cend(), incoming.cbegin(), incoming.cend(), std::back_inserter(merged), before);
        for (const auto& entry : incoming) retainedBytes += entry.bytes;
        entries = std::move(merged);
    }
    trim(budget);
    if (!paused) publish();
    return {};
}
bool PodLogHistory::publish() {
    if (selected == "*") presented = entries;
    else {
        QList<LogEntry> selection;
        for (const auto& entry : entries) if (entry.container == selected) selection.append(entry);
        presented = std::move(selection);
    }
    return true;
}
bool PodLogHistory::trim(qint64 budget) {
    bool removed = trimEntries(entries, retainedBytes, budget);
    if (paused) {
        qint64 size = 0;
        for (const auto& entry : presented) size += entry.bytes;
        removed = trimEntries(presented, size, budget) || removed;
    } else publish();
    limited = limited || removed;
    if (removed) removal = Removal::Limit;
    return removed;
}
bool PodLogHistory::expire(QDateTime now) {
    QSet<QString> expired;
    for (auto it = cursors.begin(); it != cursors.end();) {
        if (it->fetched.addSecs(60) < now) { expired.insert(it.key()); it = cursors.erase(it); } else ++it;
    }
    if (expired.isEmpty()) return false;
    QList<LogEntry> remaining;
    for (const auto& entry : entries) {
        if (expired.contains(entry.container)) retainedBytes -= entry.bytes;
        else remaining.append(entry);
    }
    entries = std::move(remaining);
    removal = Removal::Expiry;
    if (!paused) publish();
    return true;
}
QString PodLogHistory::status() const {
    QStringList result;
    if (paused) result.append("Paused; displayed entries are frozen.");
    if (limited) result.append("History limited by the configured log-size budget.");
    for (auto it = failures.cbegin(); it != failures.cend(); ++it) if (selected == "*" || selected == it.key()) result.append(it.key() + ": " + it.value());
    if (pending) result.append("Refreshing logs.");
    if (presented.isEmpty()) result.append("No retained log entries.");
    return result.join('\n');
}
LogRows::LogRows(QObject* parent) : QAbstractListModel(parent) {}
int LogRows::rowCount(const QModelIndex& parent) const { return parent.isValid() ? 0 : static_cast<int>(entries_.size()); }
QVariant LogRows::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() >= entries_.size()) return {};
    const auto& entry = entries_[index.row()];
    QStringDecoder decoder(QStringDecoder::Utf8, QStringDecoder::Flag::Stateless | QStringDecoder::Flag::ConvertInitialBom);
    if (role == Qt::DisplayRole) {
        const auto prefix = QByteArrayView(entry.text).first(std::min(qsizetype{4096}, entry.text.size()));
        const QString preview = decoder(prefix);
        qsizetype end = std::min(qsizetype{1024}, preview.size());
        if (end > 0 && end < preview.size() && preview[end-1].isHighSurrogate()) --end;
        return entry.timestamp + " [" + entry.container + "] " + preview.left(end)
            + (preview.size() > end || prefix.size() < entry.text.size() ? " [Open entry for full text]" : "");
    }
    if (role == Qt::EditRole) return entry.timestamp + " [" + entry.container + "] " + QString(decoder(entry.text));
    if (role == Qt::UserRole) return entry.id;
    return {};
}
QHash<int, QByteArray> LogRows::roleNames() const { return {{Qt::DisplayRole, "line"}, {Qt::UserRole, "entryId"}}; }
QString LogRows::entryId(int index) const { return index >= 0 && index < entries_.size() ? entries_[index].id : QString{}; }
int LogRows::findEntry(const QString& id) const {
    const auto found = std::find_if(entries_.cbegin(), entries_.cend(), [&](const auto& entry) { return entry.id == id; });
    return found == entries_.cend() ? -1 : static_cast<int>(found - entries_.cbegin());
}
bool LogRows::publish(const QList<LogEntry>& entries) {
    QSet<QString> incoming;
    for (const auto& entry : entries) incoming.insert(entry.id);
    for (int end = static_cast<int>(entries_.size()) - 1; end >= 0;) {
        if (incoming.contains(entries_[end].id)) { --end; continue; }
        int start = end;
        while (start > 0 && !incoming.contains(entries_[start - 1].id)) --start;
        beginRemoveRows({}, start, end); entries_.remove(start, end - start + 1); endRemoveRows(); end = start - 1;
    }
    for (int index = 0; index < entries.size();) {
        if (index < entries_.size() && entries_[index].id == entries[index].id) { ++index; continue; }
        int end = index;
        while (end < entries.size() && (index >= entries_.size() || entries[end].id != entries_[index].id)) ++end;
        beginInsertRows({}, index, end - 1);
        for (int inserted = index; inserted < end; ++inserted) entries_.insert(inserted, entries[inserted]);
        endInsertRows(); index = end;
    }
    return true;
}
} // namespace podlord
