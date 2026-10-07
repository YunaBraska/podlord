#pragma once
#include <QLocale>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

namespace podlord {
/** Shipped reference catalog, including system selection and English fallback. */
QVariantList uiLanguages();
QStringList uiLanguageCodes();
bool validUiLanguage(const QString& setting);
/** Resolves only presentation language; never normalizes Kubernetes values. */
QString resolvedUiLanguage(const QString& setting, const QLocale& system = QLocale::system());
QVariantMap uiText(const QString& setting, const QLocale& system = QLocale::system());
} // namespace podlord
