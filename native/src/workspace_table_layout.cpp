#include "workspace.h"
#include <QFutureWatcher>
#include <QtConcurrent/QtConcurrentRun>

namespace podlord {
TableSchemas Workspace::tableSchemas(bool includeAuxiliary) const {
    TableSchemas result;
    for (const auto& entry : QList<QPair<QString, const QAbstractItemModel*>>{{"resource", &rows_}, {"event", &eventRows_}, {"port", &portRows_},
        {"inspectorEvent", &inspectorEventRows_}, {"inspectorLink", &inspectorLinkRows_}, {"value", &valueRows_}}) {
        if (!includeAuxiliary && entry.first != "resource" && entry.first != "event") continue;
        QStringList ids;
        for (int column = 0; column < entry.second->columnCount(); ++column)
            ids.append(entry.second->headerData(column, Qt::Horizontal, Qt::UserRole).toString());
        result.insert(entry.first, ids);
    }
    return result;
}
TableSchemas Workspace::layoutSchemas() const {
    auto result = tableSchemas(true);
    QStringList ids;
    const auto* model = alerts_.tableModel();
    for (int column = 0; column < model->columnCount(); ++column)
        ids.append(model->headerData(column, Qt::Horizontal, Qt::UserRole).toString());
    result.insert("alert", ids);
    return result;
}
QVariantList Workspace::tableColumns(const QString& table, const QAbstractItemModel& model, bool defaults) const {
    QVariantList result;
    const auto ids = layoutSchemas().value(table);
    for (const auto& entry : defaults ? defaultTableLayouts(layoutSchemas()).value(table) : tableLayouts_.value(table)) {
        const int column = static_cast<int>(ids.indexOf(entry.id));
        result.append(QVariantMap{{"id", entry.id}, {"column", column}, {"title", model.headerData(column, Qt::Horizontal, Qt::DisplayRole)},
            {"visible", entry.visible}, {"pinned", entry.pinned}, {"width", entry.width}});
    }
    return result;
}
QVariantList Workspace::resourceColumns() const { return tableColumns("resource", rows_); }
QVariantList Workspace::alertColumns() const { return tableColumns("alert", *alerts_.tableModel()); }
QVariantList Workspace::eventColumns() const { return tableColumns("event", eventRows_); }
QVariantList Workspace::portColumns() const { return tableColumns("port", portRows_); }
QVariantList Workspace::defaultTableColumns(const QString& table) const {
    return table=="resource" ? tableColumns(table,rows_,true) : table=="event" ? tableColumns(table,eventRows_,true) : table=="port" ? tableColumns(table,portRows_,true)
        : table=="inspectorEvent" ? tableColumns(table,inspectorEventRows_,true) : table=="inspectorLink" ? tableColumns(table,inspectorLinkRows_,true)
        : table=="value" ? tableColumns(table,valueRows_,true) : table=="alert" ? tableColumns(table,*alerts_.tableModel(),true) : QVariantList{};
}
bool Workspace::reloadTableLayouts() {
    if (tableLayoutSaving_) return false;
    const auto schemas = layoutSchemas();
    if (tableLayouts_.isEmpty()) tableLayouts_ = defaultTableLayouts(schemas);
    const auto result = TableLayoutStore(profile_, schemas).load();
    if (const auto* failure = std::get_if<Failure>(&result)) {
        tableLayoutError_ = failure->message; emit tableLayoutStatusChanged(); return false;
    }
    const auto layouts = std::get<TableLayouts>(result);
    if (layouts != tableLayouts_) { tableLayouts_ = layouts; emit tableLayoutChanged(); }
    tableLayoutError_.clear(); emit tableLayoutStatusChanged(); return true;
}
bool Workspace::saveTableLayout(const QString& table, const QVariantList& columns) {
    if (tableLayoutSaving_) return false;
    const auto schemas = layoutSchemas();
    if (!schemas.contains(table) || columns.size() != schemas.value(table).size()) {
        tableLayoutError_ = "Choose a supported table and its complete column layout.";
        emit tableLayoutStatusChanged(); return false;
    }
    QJsonArray value;
    for (const auto& entry : columns) {
        const auto column = entry.toMap();
        value.append(QJsonObject::fromVariantMap({{"id", column.value("id")}, {"visible", column.value("visible")}, {"pinned", column.value("pinned")}, {"width", column.value("width")}}));
    }
    tableLayoutSaving_ = true; tableLayoutError_.clear(); emit tableLayoutStatusChanged();
    auto* watcher = new QFutureWatcher<Result<TableLayouts>>(this);
    connect(watcher, &QFutureWatcher<Result<TableLayouts>>::finished, this, [this, watcher] {
        const auto result = watcher->result(); watcher->deleteLater();
        tableLayoutSaving_ = false;
        if (const auto* failure = std::get_if<Failure>(&result)) tableLayoutError_ = failure->message;
        else if (const auto& layouts = std::get<TableLayouts>(result); layouts != tableLayouts_) { tableLayouts_ = layouts; emit tableLayoutChanged(); }
        emit tableLayoutStatusChanged();
    });
    watcher->setFuture(QtConcurrent::run([profile = profile_, schemas, table, value, expected = tableLayouts_.value(table)] {
        return TableLayoutStore(profile, schemas).save(table, value, expected);
    }));
    return true;
}
} // namespace podlord
