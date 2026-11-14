#include "workspace.h"
#include "ui_language.h"

namespace podlord {
QVariantList Workspace::uiLanguages() const { return podlord::uiLanguages(); }
QVariantMap Workspace::uiText() const { return podlord::uiText(settings_.language); }
bool Workspace::uiRightToLeft() const { return QLocale(resolvedUiLanguage(settings_.language)).textDirection() == Qt::RightToLeft; }
bool Workspace::saveUiLanguage(const QString& language) {
    if (!validUiLanguage(language)) {
        settingsError_ = "Choose a shipped UI language or system language.";
        emit changed(); emit languageChanged(); return false;
    }
    auto desired = settings_; desired.language = language;
    return desired == settings_ || savePolicy(desired);
}
} // namespace podlord
