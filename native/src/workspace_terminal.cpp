#include "workspace.h"

namespace podlord {
bool Workspace::canStartTerminal() const {
    const auto row = client_.resource(active_, inspectorPath()); const auto* existing = containerTerminal();
    return !busy_ && !authenticationRequired() && !authenticationRunning() && (!existing || !existing->active())
        && client_.containsResource(active_, inspectorPath()) && row["apiVersion"] == "v1" && row["kind"] == "Pod"
        && row["status"] == "Running" && !row["uid"].toString().isEmpty() && !terminalContainers().isEmpty();
}
QStringList Workspace::terminalContainers() const {
    QStringList names;
    for (const auto& value : client_.resource(active_, inspectorPath())["containers"].toArray()) if (value.isString()) names.append(value.toString());
    return names;
}
bool Workspace::startContainerTerminal(const QString& container, const QString& shell) {
    if (!canStartTerminal()) return false;
    const auto result = client_.startTerminal(active_, inspectorPath(), container, shell);
    if (const auto* failure = std::get_if<Failure>(&result)) { error_ = failure->message; emit changed(); return false; }
    error_.clear(); return setInspectorPage("terminal");
}
bool Workspace::stopContainerTerminal() { return client_.stopTerminal(active_); }
}
