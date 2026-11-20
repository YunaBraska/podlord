#include "workspace.h"
#include "workspace_runtime.h"
#include "ui_language.h"
#include <QClipboard>
#include <QDir>
#include <QGuiApplication>
#include <QRegularExpression>

namespace podlord {
bool Workspace::incidentAiSupported() const { return runtime_->incidentAi.supported(); }
bool Workspace::incidentAiEnabled() const {
    return runtime_->incidentAi.enabled(sessionConfiguration(active_).value("contextId").toString());
}
QStringList Workspace::incidentAiModels() const { return runtime_->incidentAi.models(); }
QVariantList Workspace::incidentAiModelChoices() const { return runtime_->incidentAi.allModelChoices(); }
QVariantList Workspace::incidentAiAccounts() const { return runtime_->incidentAi.accounts(); }
QString Workspace::incidentAiModel() const { return runtime_->incidentAi.model(); }
bool Workspace::incidentAiReady() const { return runtime_->incidentAi.ready(); }
QString Workspace::incidentAiProvider() const { return runtime_->incidentAi.provider(); }
QString Workspace::chatgptAccount() const { return runtime_->incidentAi.chatgptAccount(); }
bool Workspace::chatgptConnected() const { return runtime_->incidentAi.chatgptConnected(); }
QStringList Workspace::incidentAiReasoningOptions() const { return runtime_->incidentAi.reasoningOptions(); }
QString Workspace::incidentAiReasoning() const { return runtime_->incidentAi.reasoning(); }
QString Workspace::incidentAiStatus() const {
    if (incidentAiResult().contains("summary")) return {};
    return localizedAiStatus(runtime_->incidentAi.status(sessionConfiguration(active_).value("contextId").toString()), settings_.language);
}
QVariantMap Workspace::incidentAiResult() const {
    const auto contextId = sessionConfiguration(active_).value("contextId").toString();
    auto result = runtime_->incidentAi.result(contextId, inspectorPath());
    if (!result.contains("summary")) return result;
    const auto kind = inspectorSummary_["kind"].toString().toLower();
    const auto name = inspectorSummary_["name"].toString();
    const auto nameSpace = inspectorSummary_["namespace"].toString();
    static const QRegularExpression kindPattern("^[a-z][a-z0-9]{0,63}$");
    const auto safe = [](const QString& value) {
        if (value.isEmpty() || value.size() > 4096) return false;
        for (const auto character : value) if (character.isNull() || character.isLowSurrogate() || character.isHighSurrogate()
            || character.unicode() < 0x20 || character.unicode() == 0x7f) return false;
        return true;
    };
    if (!kindPattern.match(kind).hasMatch() || !safe(name) || (!nameSpace.isEmpty() && !safe(nameSpace))) return result;
    for (const auto& source : sources_.sources) {
        if (!QDir::isAbsolutePath(source.sourcePath) || !safe(source.sourcePath)) continue;
        for (const auto& context : source.contexts) {
            if (context.id != contextId || !safe(context.name)) continue;
#ifdef Q_OS_WIN
            const auto quote = [](QString value) { return "'" + value.replace("'", "''") + "'"; };
#else
            const auto quote = [](QString value) { return "'" + value.replace("'", "'\\''") + "'"; };
#endif
            QString command = "kubectl --kubeconfig " + quote(source.sourcePath) + " --context " + quote(context.name);
            if (!nameSpace.isEmpty()) command += " --namespace " + quote(nameSpace);
            result["command"] = command + " describe " + kind + " " + quote(name);
            return result;
        }
    }
    return result;
}
QString Workspace::incidentAiRequest() const {
    const auto context = sessionConfiguration(active_).value("contextId").toString();
    return runtime_->incidentAi.requestPreview(context, inspectorPath());
}
QVariantMap Workspace::incidentAiDiagnostic() const { return runtime_->incidentAi.diagnosticResult(); }
bool Workspace::copyIncidentAiCommand() {
    const auto command = incidentAiResult().value("command").toString();
    if (command.isEmpty() || !QGuiApplication::clipboard()) return false;
    QGuiApplication::clipboard()->setText(command);
    return true;
}
bool Workspace::setIncidentAiEnabled(bool enabled) {
    const auto context = sessionConfiguration(active_).value("contextId").toString();
    if (context.isEmpty() || !runtime_->incidentAi.setEnabled(context, enabled)) return false;
    runtime_->incidentAi.observe(context, client_.rows(active_), resolvedUiLanguage(settings_.language));
    emit incidentAiChanged();
    return true;
}
bool Workspace::setIncidentAiModel(const QString& model) { return runtime_->incidentAi.setModel(model); }
bool Workspace::selectIncidentAiModel(const QString& provider, const QString& model) { return runtime_->incidentAi.selectModel(provider, model); }
bool Workspace::setIncidentAiProvider(const QString& provider) { return runtime_->incidentAi.setProvider(provider); }
bool Workspace::signInChatGpt() { return runtime_->incidentAi.chatgptSignIn(runtime_->incidentAi.chatgptClientId(), settings_.language); }
bool Workspace::signInOpenRouter() { return runtime_->incidentAi.openRouterSignIn(settings_.language); }
void Workspace::signOutChatGpt() { runtime_->incidentAi.chatgptSignOut(); }
bool Workspace::setClaudeAiKey(const QString& key) { return runtime_->incidentAi.setClaudeKey(key); }
void Workspace::clearClaudeAiKey() { runtime_->incidentAi.clearClaudeKey(); }
void Workspace::signOutOpenRouter() { runtime_->incidentAi.signOutOpenRouter(); }
void Workspace::refreshIncidentAiUsage() { runtime_->incidentAi.refreshUsage(); }
bool Workspace::setIncidentAiReasoning(const QString& reasoning) { return runtime_->incidentAi.setReasoning(reasoning); }
void Workspace::ensureIncidentAiModels() { runtime_->incidentAi.ensureModelsFresh(); }
bool Workspace::refreshIncidentAiAnalysis() {
    const auto context = sessionConfiguration(active_).value("contextId").toString();
    if (context.isEmpty() || inspectorPath().isEmpty()) return false;
    runtime_->incidentAi.observe(context, client_.rows(active_), resolvedUiLanguage(settings_.language));
    return runtime_->incidentAi.refresh(context, inspectorPath());
}
} // namespace podlord
