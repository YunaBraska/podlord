#pragma once
#include "session_store.h"
#include <QJsonArray>

namespace podlord {
struct TableColumnLayout final {
    QString id;
    bool visible = true;
    bool pinned = false;
    int width = 170;
    bool operator==(const TableColumnLayout&) const = default;
};
using TableLayout = QList<TableColumnLayout>;
using TableLayouts = QMap<QString, TableLayout>;
using TableSchemas = QMap<QString, QStringList>;
/** Column IDs come from actual model headers, not a second column catalog. */
TableLayouts defaultTableLayouts(const TableSchemas& schemas);
/** Private atomic layouts; stale writers conflict only for their own table type. */
class TableLayoutStore final {
public:
    TableLayoutStore(QString profile, TableSchemas schemas);
    Result<TableLayouts> load() const;
    Result<TableLayouts> save(const QString& table, const QJsonArray& value, const TableLayout& expected) const;
private:
    const QString profile_;
    const TableSchemas schemas_;
};
} // namespace podlord
