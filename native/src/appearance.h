#pragma once
#include "session_store.h"
#include <QPalette>
#include <QVariantMap>

namespace podlord {
struct Appearance final {
    QString name, variant, intensity;
    QPalette palette;
    QVariantMap colors;
    bool operator==(const Appearance&) const = default;
};
/** Shipped names in legacy catalog order; compiled assets are the only authority. */
QStringList themeNames();
/** Accepts only canonical shipped names, dark/light, and subtle/medium/arcade. */
bool validAppearance(const QString& name, const QString& variant, const QString& intensity);
/** Builds a Qt palette and cached, static texture. Invalid choices fail explicitly. */
Result<Appearance> makeAppearance(const QString& name, const QString& variant, const QString& intensity);
/** Preserves the legacy UTF-16 identity hash and theme-specific identity colors. */
QColor appearanceIdentity(const Appearance& appearance, const QString& value);
/** Reference status semantics use theme colors rather than an identity hash. */
QColor appearanceStatus(const Appearance& appearance, const QString& status);
} // namespace podlord
