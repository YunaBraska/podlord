#pragma once
#include "table_layout.h"

namespace podlord {
struct TableViewState final {
    QString filter;
    QString column;
    bool descending = false;
    QMap<QString, QString> fields = {};
    QString mode = {};
    bool operator==(const TableViewState&) const = default;
};
using TableViewStates = QMap<QString, TableViewState>;
using LoadedViewStates = QMap<QString, Result<TableViewStates>>;
/** Session-scoped filters and sorts. Column IDs come from actual model headers. */
class ViewStateStore final {
public:
    ViewStateStore(QString profile, TableSchemas schemas);
    /** Missing state returns empty filters and NONE sorts without creating files. */
    Result<TableViewStates> load(const QString& session) const;
    /** Atomically merges one table. A stale same-table writer conflicts rather than overwriting. */
    Result<TableViewStates> save(const QString& session, const QString& table,
        const TableViewState& value, const TableViewState& expected) const;
    /** Profile-wide named resource filters; the default filter is protected. */
    Result<TableViewStates> loadPresets() const;
    Result<TableViewStates> savePresets(const TableViewStates& desired, const TableViewStates& expected) const;
private:
    const QString profile_;
    const TableSchemas schemas_;
};
}
