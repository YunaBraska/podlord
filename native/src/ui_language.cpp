#include "ui_language.h"
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <algorithm>

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
    Q_UNREACHABLE();
}
QString localizedAiStatus(const QString& status, const QString& setting) {
    if (status.isEmpty()) return {};
    const auto text = uiText(setting);
    const auto translated = text.value("ai.status." + status).toString();
    if (!translated.isEmpty()) return translated;
    const QString prefix = "ChatGPT request failed (HTTP ";
    const auto end = status.indexOf(')', prefix.size());
    if (status.startsWith(prefix) && end > prefix.size()) {
        const auto code = status.mid(prefix.size(), end - prefix.size());
        if (code.size() <= 3 && std::all_of(code.cbegin(), code.cend(), [](QChar c) { return c.isDigit(); })) {
            auto detail = status.mid(end + 1);
            const QString usage = ". Check ChatGPT Settings → Usage.";
            QString translatedUsage;
            if (detail.endsWith(usage)) {
                detail.chop(usage.size());
                translatedUsage = text.value("ai.status." + usage).toString();
            }
            return text.value("ai.status.ChatGPT request failed (HTTP %1)").toString().arg(code)
                + detail + translatedUsage;
        }
    }
    return status;
}
} // namespace podlord
