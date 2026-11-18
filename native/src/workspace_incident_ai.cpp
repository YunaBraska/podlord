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
QVariantList Workspace::incidentAiModelChoices() const { return runtime_->incidentAi.modelChoices(); }
QString Workspace::incidentAiModel() const { return runtime_->incidentAi.model(); }
QString Workspace::incidentAiProvider() const { return runtime_->incidentAi.provider(); }
QVariantList Workspace::chatgptAccounts() const { return runtime_->incidentAi.chatgptAccounts(); }
QString Workspace::chatgptAccount() const { return runtime_->incidentAi.chatgptAccount(); }
QString Workspace::chatgptClientId() const { return runtime_->incidentAi.chatgptClientId(); }
bool Workspace::incidentAiApiKeyConfigured() const { return runtime_->incidentAi.apiKeyConfigured(); }
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
bool Workspace::setIncidentAiProvider(const QString& provider) { return runtime_->incidentAi.setProvider(provider); }
bool Workspace::signInChatGpt(const QString& clientId) { return runtime_->incidentAi.chatgptSignIn(clientId); }
bool Workspace::selectChatGptAccount(const QString& clientId) { return runtime_->incidentAi.chatgptSelectAccount(clientId); }
void Workspace::signOutChatGpt() { runtime_->incidentAi.chatgptSignOut(); }
bool Workspace::setIncidentAiApiKey(const QString& key) { return runtime_->incidentAi.setApiKey(key); }
void Workspace::clearIncidentAiApiKey() { runtime_->incidentAi.clearApiKey(); }
bool Workspace::setIncidentAiReasoning(const QString& reasoning) { return runtime_->incidentAi.setReasoning(reasoning); }
void Workspace::refreshIncidentAiModels() { runtime_->incidentAi.refreshModels(); }
bool Workspace::refreshIncidentAiAnalysis() {
    return runtime_->incidentAi.refresh(sessionConfiguration(active_).value("contextId").toString(), inspectorPath());
}
bool Workspace::copyIncidentAiCommand() {
    const auto command = incidentAiResult().value("command").toString();
    if (command.isEmpty() || !QGuiApplication::clipboard()) return false;
    QGuiApplication::clipboard()->setText(command);
    return true;
}
} // namespace podlord
