#pragma once
#include "session_store.h"
#include <QJsonArray>
#include <QVariantList>
#include <array>

namespace podlord {
/** Metrics remain fresh at this age and become stale on the following millisecond. */
inline constexpr qint64 metricFreshnessMilliseconds = 25000;
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
/** Typed, snapshot-owned measurement contribution. References are never measurements. */
struct ResourceMetricTotal final {
    double used = 0, reference = 0;
    int eligible = 0, observed = 0, referenced = 0;
    bool incomplete = false, stale = false;
    QDateTime oldestStaleMeasurement;
};
struct ResourceMetricContribution final {
    int group = -1;
    std::array<ResourceMetricTotal, 3> metrics{};
};
ResourceMetricContribution resourceMetricContribution(const QJsonObject& resource);
/** Reduces prepared contributions in visible order without decoding JSON again. */
struct ResourceMetricSummary final {
    std::array<std::array<ResourceMetricTotal, 3>, 4> totals{};
    std::array<int, 4> counts{};
    ResourceMetricSummary& add(const ResourceMetricContribution& contribution);
    QVariantList presentation() const;
};
/** Summarize the canonical filtered cache without double-counting Node and Pod usage.
 * Missing measurements remain unavailable; references never stand in for usage. */
QVariantList resourceMetricSummary(const QJsonArray& resources);
}
