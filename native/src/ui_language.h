#pragma once
#include <QLocale>
#include <QByteArray>
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
/** Localizes a recognized incident-analysis status while retaining technical error identifiers. */
QString localizedAiStatus(const QString& status, const QString& setting);
/** A self-contained, localized OAuth callback page. */
QByteArray localizedAuthCallbackPage(bool accepted, const QString& setting);
} // namespace podlord
