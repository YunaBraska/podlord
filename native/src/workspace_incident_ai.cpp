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
QVariantList Workspace::incidentAiModelChoices() const { return runtime_->incidentAi.allModelChoices(); }
QString Workspace::incidentAiModel() const { return runtime_->incidentAi.model(); }
QString Workspace::incidentAiProvider() const { return runtime_->incidentAi.provider(); }
QString Workspace::chatgptAccount() const { return runtime_->incidentAi.chatgptAccount(); }
bool Workspace::chatgptConnected() const { return runtime_->incidentAi.chatgptConnected(); }
bool Workspace::incidentAiApiKeyConfigured() const { return runtime_->incidentAi.apiKeyConfigured(); }
QStringList Workspace::incidentAiReasoningOptions() const { return runtime_->incidentAi.reasoningOptions(); }
QString Workspace::incidentAiReasoning() const { return runtime_->incidentAi.reasoning(); }
QString Workspace::incidentAiStatus() const {
    if (incidentAiResult().contains("summary")) return {};
    return localizedAiStatus(runtime_->incidentAi.status(sessionConfiguration(active_).value("contextId").toString()), settings_.language);
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
bool Workspace::selectIncidentAiModel(const QString& provider, const QString& model) { return runtime_->incidentAi.selectModel(provider, model); }
bool Workspace::setIncidentAiProvider(const QString& provider) { return runtime_->incidentAi.setProvider(provider); }
bool Workspace::signInChatGpt() { return runtime_->incidentAi.chatgptSignIn(runtime_->incidentAi.chatgptClientId(), settings_.language); }
void Workspace::signOutChatGpt() { runtime_->incidentAi.chatgptSignOut(); }
bool Workspace::setIncidentAiApiKey(const QString& key) { return runtime_->incidentAi.setApiKey(key); }
void Workspace::clearIncidentAiApiKey() { runtime_->incidentAi.clearApiKey(); }
bool Workspace::setIncidentAiReasoning(const QString& reasoning) { return runtime_->incidentAi.setReasoning(reasoning); }
void Workspace::ensureIncidentAiModels() { runtime_->incidentAi.ensureModelsFresh(); }
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
