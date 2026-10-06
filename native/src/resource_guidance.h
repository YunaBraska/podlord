#pragma once
#include <QJsonObject>
#include <QVariantList>

namespace podlord {
/** Read-only, bounded explanations of recognized status evidence in a cached API document.
 * Unknown/custom kinds yield no findings, not a health assertion. Never includes status messages,
 * Secret values or inferred root causes. At most eight findings; no transport or persistence. */
QVariantList resourceGuidance(const QJsonObject& document);
}
