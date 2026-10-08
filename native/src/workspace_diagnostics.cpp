#include "workspace.h"
#include <QClipboard>
#include <QGuiApplication>

namespace podlord {
QString Workspace::refreshSettingsDiagnostics() {
    const auto snapshot=settingsDiagnostics();
    QJsonArray metrics,requests;
    for (const auto& value:snapshot.value("metrics").toList()) {
        auto metric=value.toMap();
        metric.insert("id",metric.value("id",metric.value("label")));
        metrics.append(QJsonObject::fromVariantMap(metric));
    }
    QMap<QString,int> occurrences;
    for (const auto& value:snapshot.value("requests").toList()) {
        auto request=value.toMap();
        bool measured=false;
        const auto duration=request.value("duration").toString().section(' ',0,0).toLongLong(&measured);
        request.insert("sortValues",QVariantMap{{"duration",measured && duration>=0 ? QVariant(duration) : QVariant{}}});
        const auto identity=request.value("time").toString()+'\n'+request.value("method").toString()+'\n'+request.value("path").toString();
        request.insert("id",identity+'\n'+QString::number(occurrences[identity]++));
        requests.append(QJsonObject::fromVariantMap(request));
    }
    diagnosticRows_.publish(metrics);
    requestAuditRows_.publish(requests);
    return snapshot.value("sampledAt").toString();
}
bool Workspace::sortDiagnosticColumn(const QString& table,int column) {
    auto* model=table=="diagnostic" ? &diagnosticTable_ : table=="audit" ? &requestAuditTable_ : nullptr;
    if (!model || column<0 || column>=model->columnCount()) return false;
    if (model->sortColumn()!=column) { model->sort(column,Qt::AscendingOrder); }
    else if (model->sortOrder()==Qt::AscendingOrder) { model->sort(column,Qt::DescendingOrder); }
    else { model->sort(-1); }
    emit diagnosticsPresentationChanged(); return true;
}
bool Workspace::copyDiagnosticCell(const QString& table,const QString& identity,int column) {
    const auto* model=table=="diagnostic" ? &diagnosticRows_ : table=="audit" ? &requestAuditRows_ : nullptr;
    if (!model || identity.isEmpty() || column<0 || column>=model->columnCount() || !QGuiApplication::clipboard()) return false;
    for (int row=0;row<model->rowCount();++row) {
        if (model->index(row,0).data(Qt::UserRole).toString()!=identity) continue;
        QGuiApplication::clipboard()->setText(model->index(row,column).data().toString()); return true;
    }
    return false;
}
}
