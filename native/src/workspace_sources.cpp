#include "workspace.h"
#include <QClipboard>
#include <QGuiApplication>

namespace podlord {
bool Workspace::refreshSourceTable() {
    QJsonArray rows;
    for (const auto& value : contexts()) {
        auto row = QJsonObject::fromVariantMap(value.toMap());
        row["status"] = row["usable"].toBool() ? "Supported" : row["detail"].toString();
        row["rename"] = "Rename"; row["remove"] = "Remove";
        rows.append(row);
    }
    sourceRows_.setAppearance(appearance_);
    sourceRows_.publish(rows);
    return true;
}
QVariantList Workspace::sourceColumns() const { return tableColumns("source", sourceRows_); }
bool Workspace::sortSourceColumn(int column) {
    if (column < 0 || column >= 7) return false;
    if (sourceTable_.sortColumn() != column) { sourceTable_.sort(column, Qt::AscendingOrder); }
    else if (sourceTable_.sortOrder() == Qt::AscendingOrder) { sourceTable_.sort(column, Qt::DescendingOrder); }
    else { sourceTable_.sort(-1); }
    emit sourcesPresentationChanged();
    return true;
}
bool Workspace::copySourceCell(const QString& identity, int column) {
    if (identity.isEmpty() || column < 0 || column >= sourceRows_.columnCount() || !QGuiApplication::clipboard()) return false;
    for (int row = 0; row < sourceRows_.rowCount(); ++row) {
        if (sourceRows_.index(row,0).data(Qt::UserRole) != identity) continue;
        QGuiApplication::clipboard()->setText(sourceRows_.index(row,column).data().toString());
        return true;
    }
    return false;
}
}
