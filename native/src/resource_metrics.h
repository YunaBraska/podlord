#pragma once
#include "session_store.h"
#include <QJsonArray>
#include <QVariantList>

namespace podlord {
/** Nonnegative Kubernetes quantities. CPU output is millicores; other resources use base units. */
std::optional<double> metricQuantity(const QString& text, bool cpu);
/** Format a canonical nonnegative quantity; CPU input is millicores, other input is bytes. */
QString formatMetricQuantity(double number, bool cpu, bool compact = false);
/** Configured references only; never substitutes requests, limits or capacity for measured usage. */
QJsonObject resourceMetricReferences(const QJsonObject& document, const QJsonObject& identity);
/** Validate one externally received Metrics API sample after resource metadata validation. */
Result<QJsonObject> resourceMetricSample(const QJsonObject& document, QJsonObject identity);
/** Attach a same-entity cached sample. Older-than-creation samples cannot follow a reused name. */
QJsonObject withResourceMetrics(QJsonObject resource, const QJsonObject& sample, QDateTime now);
/** Format the already-normalized snapshot for native gauges without storage or network access. */
QVariantList resourceMetricPresentation(const QJsonObject& resource);
/** Summarize the canonical filtered cache without double-counting Node and Pod usage.
 * Missing measurements remain unavailable; references never stand in for usage. */
QVariantList resourceMetricSummary(const QJsonArray& resources);
}
