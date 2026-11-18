#include "workspace.h"
#include "workspace_runtime.h"
#include "ui_language.h"
#include <QGuiApplication>
#include <QClipboard>

namespace podlord {
bool Workspace::incidentAiSupported() const { return runtime_->incidentAi.supported(); }
bool Workspace::incidentAiEnabled() const {
    return runtime_->incidentAi.enabled(sessionConfiguration(active_).value("contextId").toString());
}
QStringList Workspace::incidentAiModels() const { return runtime_->incidentAi.models(); }
QString Workspace::incidentAiModel() const { return runtime_->incidentAi.model(); }
QStringList Workspace::incidentAiReasoningOptions() const { return runtime_->incidentAi.reasoningOptions(); }
QString Workspace::incidentAiReasoning() const { return runtime_->incidentAi.reasoning(); }
QString Workspace::incidentAiStatus() const {
    if (incidentAiResult().contains("summary")) return {};
    return runtime_->incidentAi.status(sessionConfiguration(active_).value("contextId").toString());
}
QVariantMap Workspace::incidentAiResult() const {
    return runtime_->incidentAi.result(sessionConfiguration(active_).value("contextId").toString(), inspectorPath());
}
bool Workspace::setIncidentAiEnabled(bool enabled) {
    const auto context = sessionConfiguration(active_).value("contextId").toString();
    if (context.isEmpty() || !runtime_->incidentAi.setEnabled(context, enabled)) return false;
    if (enabled) runtime_->incidentAi.observe(context, client_.rows(active_), resolvedUiLanguage(settings_.language));
    emit incidentAiChanged();
    return true;
}
bool Workspace::setIncidentAiModel(const QString& model) { return runtime_->incidentAi.setModel(model); }
bool Workspace::setIncidentAiReasoning(const QString& reasoning) { return runtime_->incidentAi.setReasoning(reasoning); }
void Workspace::refreshIncidentAiModels() { runtime_->incidentAi.refreshModels(); }
bool Workspace::copyIncidentAiCommand() {
    const auto command = incidentAiResult().value("command").toString();
    if (command.isEmpty() || !QGuiApplication::clipboard()) return false;
    QGuiApplication::clipboard()->setText(command);
    return true;
}
} // namespace podlord
