#include "workspace.h"
#include <QClipboard>
#include <QGuiApplication>

namespace podlord {
bool Workspace::copyInspectorCell(const QString& table, const QString& identity, int column) {
    const auto* model = table == "inspectorEvent" ? &inspectorEventRows_ : table == "inspectorLink" ? &inspectorLinkRows_ : nullptr;
    if (!model || !QGuiApplication::clipboard() || column < 0 || column >= model->columnCount() || identity.isEmpty()) return false;
    for (int row = 0; row < model->rowCount(); ++row) {
        if (model->index(row, 0).data(Qt::UserRole).toString() != identity) continue;
        QGuiApplication::clipboard()->setText(model->index(row, column).data().toString()); return true;
    }
    return false;
}
}
