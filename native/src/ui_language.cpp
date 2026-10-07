#include "ui_language.h"
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace podlord {
namespace {
struct Language final { QString code, name; QVariantMap text; };
const QList<Language>& catalog() {
    static const QList<Language> languages = [] {
        QFile file(":/podlord/locales.json");
        if (!file.open(QIODevice::ReadOnly)) qFatal("Shipped UI language catalog is unavailable.");
        QJsonParseError error;
        const auto document = QJsonDocument::fromJson(file.readAll(), &error);
        const auto root = document.object();
        if (error.error != QJsonParseError::NoError || root["version"] != 1 || !root["english"].isObject() || !root["languages"].isArray())
            qFatal("Shipped UI language catalog is invalid.");
        const auto english = root["english"].toObject().toVariantMap();
        QList<Language> result;
        for (const auto& value : root["languages"].toArray()) {
            const auto language = value.toObject();
            auto text = english;
            const auto translated = language["text"].toObject().toVariantMap();
            for (auto item = translated.cbegin(); item != translated.cend(); ++item) text.insert(item.key(), item.value());
            result.append({language["code"].toString(), language["name"].toString(), std::move(text)});
        }
        return result;
    }();
    return languages;
}
} // namespace
QVariantList uiLanguages() {
    QVariantList result;
    for (const auto& language : catalog()) result.append(QVariantMap{{"code", language.code}, {"name", language.name}});
    return result;
}
QStringList uiLanguageCodes() {
    QStringList result;
    for (const auto& language : catalog()) result.append(language.code);
    return result;
}
bool validUiLanguage(const QString& setting) {
    for (const auto& language : catalog()) if (language.code == setting) return true;
    return false;
}
QString resolvedUiLanguage(const QString& setting, const QLocale& system) {
    if (setting != "system") return validUiLanguage(setting) ? setting : QString("en");
    for (const auto& candidate : {system.name(QLocale::TagSeparator::Dash), QLocale::languageToCode(system.language())})
        for (const auto& language : catalog())
            if (language.code.compare(candidate, Qt::CaseInsensitive) == 0) return language.code;
    return system.language() == QLocale::Chinese ? QString("zh-Hans") : QString("en");
}
QVariantMap uiText(const QString& setting, const QLocale& system) {
    const auto code = resolvedUiLanguage(setting, system);
    for (const auto& language : catalog()) if (language.code == code) return language.text;
    qFatal("Shipped UI language catalog has no English fallback.");
}
} // namespace podlord
