#include "workspace.h"

namespace podlord {
bool Workspace::publishAppearance() {
    if (appearance_.name == settings_.themeName && appearance_.variant == settings_.themeVariant) return true;
    const auto result = makeAppearance(settings_.themeName, settings_.themeVariant);
    if (const auto* failure = std::get_if<Failure>(&result)) { settingsError_ = failure->message; emit changed(); return false; }
    appearance_ = std::get<Appearance>(result);
    rows_.setAppearance(appearance_);
    eventRows_.setAppearance(appearance_);
    portRows_.setAppearance(appearance_);
    inspectorEventRows_.setAppearance(appearance_);
    inspectorLinkRows_.setAppearance(appearance_);
    emit appearanceChanged(); return true;
}
bool Workspace::saveAppearance(const QString& name, const QString& variant) {
    if (!validAppearance(name, variant)) { settingsError_ = "Choose a shipped theme and dark/light."; emit changed(); emit appearanceChanged(); return false; }
    auto desired = settings_;
    desired.themeName = name; desired.themeVariant = variant;
    if (desired == settings_) return true;
    return savePolicy(desired);
}
} // namespace podlord
