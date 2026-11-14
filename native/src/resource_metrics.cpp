#include "resource_metrics.h"
#include <array>
#include <QRegularExpression>
#include <cmath>
#include <algorithm>

namespace podlord {
std::optional<double> metricQuantity(const QString& text, bool cpu) {
    static const QRegularExpression format("^(\\+?(?:[0-9]+(?:\\.[0-9]*)?|\\.[0-9]+))([eE][+-]?[0-9]+|[numkMGTPE]|[KMGTPE]i)?$");
    const auto matched=format.match(text); if (!matched.hasMatch()) return {};
    const auto suffix=matched.captured(2); double scale=1;
    static const QMap<QString,double> units{{"",1},{"n",1e-9},{"u",1e-6},{"m",1e-3},{"k",1e3},{"M",1e6},{"G",1e9},{"T",1e12},{"P",1e15},{"E",1e18},
        {"Ki",1024.0},{"Mi",1048576.0},{"Gi",1073741824.0},{"Ti",1099511627776.0},{"Pi",1125899906842624.0},{"Ei",1152921504606846976.0}};
    if (suffix.size()>1 && (suffix.startsWith('e') || (suffix.startsWith('E') && suffix!="Ei"))) {
        bool valid=false; const auto exponent=suffix.mid(1).toInt(&valid); if (!valid) return {}; scale=std::pow(10.0, exponent);
    } else { if (!units.contains(suffix)) return {}; scale=units[suffix]; }
    const double value=matched.captured(1).toDouble()*scale*(cpu ? 1000 : 1);
    if (value==0) { static const QRegularExpression nonzero("[1-9]"); if (matched.captured(1).contains(nonzero)) return {}; }
    return std::isfinite(value) ? std::optional<double>(value) : std::nullopt;
}
QJsonObject resourceMetricReferences(const QJsonObject& document, const QJsonObject& identity) {
    if (identity["apiVersion"]!="v1") return {};
    const auto kind=identity["kind"].toString(); const auto spec=document["spec"].toObject(); QJsonObject result;
    if (kind=="Pod") {
        QJsonArray containers=spec["containers"].toArray();
        for (const auto& value : spec["initContainers"].toArray()) if (value.toObject()["restartPolicy"]=="Always") containers.append(value);
        QJsonArray names;
        for (const auto& value : containers) names.append(value.toObject()["name"]);
        result["metricContainerNames"]=names;
        QJsonObject references;
        for (const auto& metric : {"cpu", "memory", "storage"}) {
            const auto field=QString(metric)=="storage" ? "ephemeral-storage" : metric; QJsonObject reference{{"source", "Container configuration"}};
            for (const auto& category : {"requests", "limits"}) {
                const auto key=QString(category)=="requests" ? "request" : "limit"; double sum=0; int count=0; bool invalid=false;
                for (const auto& value : containers) {
                    const QJsonValue input=value.toObject().value("resources").toObject().value(category).toObject().value(field);
                    if (input.isUndefined()) continue;
                    const auto number=input.isString() ? metricQuantity(input.toString(), QString(metric)=="cpu") : std::nullopt;
                    if (!number) { invalid=true; continue; } sum+=*number; ++count;
                }
                if (count) reference[key]=sum;
                reference[QString(key)+"Complete"]=count==containers.size() && !containers.isEmpty();
                reference[QString(key)+"Invalid"]=invalid || !std::isfinite(sum);
                const QJsonValue podLevel=spec.value("resources").toObject().value(category).toObject().value(field);
                if (!podLevel.isUndefined()) {
                    const auto number=podLevel.isString() ? metricQuantity(podLevel.toString(), QString(metric)=="cpu") : std::nullopt;
                    reference["source"]="Container configuration with Pod-level overrides where set"; reference[QString(key)+"Complete"]=number.has_value(); reference[QString(key)+"Invalid"]=!number;
                    if (number) reference[key]=*number; else reference.remove(key);
                }
            }
            references[metric]=reference;
        }
        result["metricReferences"]=references;
    } else if (kind=="Node" || kind=="PersistentVolumeClaim" || kind=="PersistentVolume") {
        QJsonObject references;
        for (const auto& metric : {"cpu", "memory", "storage"}) {
            if (kind!="Node" && QString(metric)!="storage") continue;
            const auto field=kind=="Node" && QString(metric)=="storage" ? "ephemeral-storage" : metric;
            const auto status=document["status"].toObject();
            const auto allocatable=status["allocatable"].toObject().value(field);
            const QJsonValue capacity=kind=="PersistentVolume" ? spec["capacity"].toObject()[field]
                : kind=="Node" && allocatable.isString() ? allocatable : status["capacity"].toObject()[field];
            QJsonObject reference{{"source", kind=="Node" ? "Node capacity" : "Volume provisioning"}};
            if (capacity.isString()) if (const auto number=metricQuantity(capacity.toString(), QString(metric)=="cpu")) reference["capacity"]=*number;
            const QJsonValue request=spec["resources"].toObject()["requests"].toObject()[field];
            if (request.isString()) if (const auto number=metricQuantity(request.toString(), QString(metric)=="cpu")) reference["request"]=*number;
            references[metric]=reference;
        }
        result["metricReferences"]=references;
    }
    return result;
}
Result<QJsonObject> resourceMetricSample(const QJsonObject& document, QJsonObject identity) {
    const auto at=QDateTime::fromString(document["timestamp"].toString(), Qt::ISODateWithMs);
    static const QRegularExpression timestamp("T.*(?:Z|[+-][0-9]{2}:[0-9]{2})$"), duration("^(?:[0-9]+(?:\\.[0-9]+)?(?:ns|us|\\x{00B5}s|ms|s|m|h))+$");
    if (!at.isValid() || !timestamp.match(document["timestamp"].toString()).hasMatch() || !duration.match(document["window"].toString()).hasMatch()) return Failure{StoreError::InvalidData, "Invalid Metrics API timestamp or measurement window; previous measurements retained."};
    const bool pod=identity["kind"]=="PodMetrics"; QJsonObject values;
    const auto usage=[&](const QJsonValue& input) -> Result<QJsonObject> {
        if (!input.isObject()) return Failure{StoreError::InvalidData, "Invalid Metrics API usage; previous measurements retained."};
        QJsonObject parsed; const auto object=input.toObject();
        for (const auto& metric : {"cpu", "memory"}) if (object.contains(metric)) {
            const auto number=object[metric].isString() ? metricQuantity(object[metric].toString(), QString(metric)=="cpu") : std::nullopt;
            if (!number) return Failure{StoreError::InvalidData, "Invalid Metrics API quantity; previous measurements retained."}; parsed[metric]=*number;
        }
        return parsed;
    };
    if (pod) {
        if (!document["containers"].isArray()) return Failure{StoreError::InvalidData, "Invalid Metrics API container list; previous measurements retained."};
        for (const auto& value : document["containers"].toArray()) {
            const auto container=value.toObject(); const auto name=container["name"].toString();
            if (!value.isObject() || name.isEmpty() || values.contains(name)) return Failure{StoreError::InvalidData, "Invalid or duplicate metric container; previous measurements retained."};
            const auto parsed=usage(container["usage"]); if (const auto* failure=std::get_if<Failure>(&parsed)) return *failure;
            values[name]=std::get<QJsonObject>(parsed);
        }
    } else {
        const auto parsed=usage(document["usage"]); if (const auto* failure=std::get_if<Failure>(&parsed)) return *failure; values=std::get<QJsonObject>(parsed);
    }
    identity["metricAt"]=at.toUTC().toString(Qt::ISODateWithMs); identity["metricWindow"]=document["window"]; identity["metricValues"]=values; return identity;
}
QJsonObject withResourceMetrics(QJsonObject resource, const QJsonObject& sample, QDateTime now) {
    for (const auto& field : {"cpu", "memory", "storage", "metricAt", "metricWindow", "metricStale", "metricComplete"}) resource.remove(field);
    const auto kind=resource["kind"].toString();
    if (resource["apiVersion"]!="v1" || (kind!="Pod" && kind!="Node") || sample.isEmpty()) return resource;
    const auto created=QDateTime::fromString(resource["createdAt"].toString(), Qt::ISODateWithMs), at=QDateTime::fromString(sample["metricAt"].toString(), Qt::ISODateWithMs);
    if (!created.isValid() || !at.isValid() || at<created || at>now || resource["uid"].toString().isEmpty()) return resource;
    const auto values=sample["metricValues"].toObject(); QJsonObject complete;
    for (const auto& metric : {"cpu", "memory"}) {
        if (kind=="Node") { if (values.contains(metric)) { resource[metric]=values[metric]; complete[metric]=true; } }
        else {
            double sum=0; int count=0; const auto names=resource["metricContainerNames"].toArray();
            for (const auto& name : names) { const QJsonValue value=values[name.toString()].toObject()[metric]; if (value.isDouble()) { sum+=value.toDouble(); ++count; } }
            if (count && std::isfinite(sum)) { resource[metric]=sum; complete[metric]=count==names.size(); }
        }
    }
    resource["metricComplete"]=complete; resource["metricAt"]=sample["metricAt"]; resource["metricWindow"]=sample["metricWindow"]; resource["metricStale"]=at.msecsTo(now)>metricFreshnessMilliseconds; return resource;
}
QString formatMetricQuantity(double number, bool cpu, bool compact) {
    if (cpu) return compact ? number >= 1000 ? QString::number(number / 1000, 'g', 3) + "c" : QString::number(number, 'g', 3) + "m" : QString::number(number, 'g', 4)+" mCPU";
    const QStringList units{"B", "KiB", "MiB", "GiB", "TiB", "PiB", "EiB"}; int unit=0;
    while (number>=1024 && unit<units.size()-1) { number/=1024; ++unit; }
    return QString::number(number, 'g', compact ? 3 : 4)+(compact ? "" : " ")+(compact && unit > 0 ? units[unit].chopped(1) : units[unit]);
}
ResourceMetricContribution resourceMetricContribution(const QJsonObject& row) {
    ResourceMetricContribution result;
    if (row["apiVersion"] != "v1") return result;
    const auto kind = row["kind"].toString();
    result.group = kind == "Node" ? 0 : kind == "Pod" ? 1 : kind == "PersistentVolumeClaim" ? 2 : kind == "PersistentVolume" ? 3 : -1;
    if (result.group < 0) return result;
    const std::array<QString,3> ids{"cpu", "memory", "storage"};
    for (int metric = 0; metric < 3; ++metric) {
        if ((metric < 2 && result.group > 1) || (metric == 2 && result.group == 1)) continue;
        auto& total = result.metrics[metric]; ++total.eligible;
        const auto used = row[ids[metric]];
        if (used.isDouble()) {
            total.used = used.toDouble(); ++total.observed;
            total.incomplete = !row["metricComplete"].toObject()[ids[metric]].toBool();
            total.stale = row["metricStale"].toBool();
            if (total.stale) total.oldestStaleMeasurement = QDateTime::fromString(row["metricAt"].toString(), Qt::ISODateWithMs);
        }
        const auto reference = row["metricReferences"].toObject()[ids[metric]].toObject();
        const QString key = result.group == 1 ? "limit" : "capacity";
        if (reference[key].isDouble()) {
            total.reference = reference[key].toDouble(); ++total.referenced;
            total.incomplete = total.incomplete || (reference.contains(key + "Complete") && !reference[key + "Complete"].toBool());
        }
    }
    return result;
}
ResourceMetricSummary& ResourceMetricSummary::add(const ResourceMetricContribution& contribution) {
    if (contribution.group < 0 || contribution.group >= int(counts.size())) return *this;
    ++counts[contribution.group];
    for (int metric = 0; metric < 3; ++metric) {
        auto& total = totals[contribution.group][metric];
        const auto& value = contribution.metrics[metric];
        total.used += value.used; total.reference += value.reference;
        total.eligible += value.eligible; total.observed += value.observed; total.referenced += value.referenced;
        total.incomplete = total.incomplete || value.incomplete; total.stale = total.stale || value.stale;
        if (value.oldestStaleMeasurement.isValid() && (!total.oldestStaleMeasurement.isValid() || value.oldestStaleMeasurement < total.oldestStaleMeasurement))
            total.oldestStaleMeasurement = value.oldestStaleMeasurement;
    }
    return *this;
}
QVariantList ResourceMetricSummary::presentation() const {
    const std::array<QString,3> ids{"cpu", "memory", "storage"};
    QVariantList result;
    for (int metric=0; metric<3; ++metric) {
        const int group=counts[0]>0 ? 0 : metric<2 ? 1 : counts[2]>0 ? 2 : 3;
        const auto& total=totals[group][metric];
        const bool measured=total.observed>0 && std::isfinite(total.used);
        const bool referenced=total.referenced>0 && std::isfinite(total.reference);
        const QString scope=group==0 ? "Nodes" : group==1 ? "Pods" : group==2 ? "PersistentVolumeClaims" : "PersistentVolumes";
        QString usage=measured ? formatMetricQuantity(total.used,metric==0) : "Unavailable";
        if (total.observed>0 && !std::isfinite(total.used)) usage+=" (overflow)";
        if (measured && (total.observed<total.eligible || total.incomplete)) usage+=" (incomplete)";
        if (referenced) usage+=" / "+formatMetricQuantity(total.reference,metric==0)+(total.referenced<total.eligible ? " (partial reference)" : "");
        QString display = measured ? formatMetricQuantity(total.used, metric == 0, true) : "Unavailable";
        if (referenced) display += " / " + formatMetricQuantity(total.reference, metric == 0, true);
        if (measured && (total.observed < total.eligible || total.incomplete)) display += " [partial]";
        const auto maximum=std::max(measured ? total.used : 0,referenced ? total.reference : 0);
        const QString label=metric==0 ? "CPU" : metric==1 ? "Memory" : "Storage";
        const auto description = label + ": " + usage + " (" + scope + ")";
        result.append(QVariantMap{{"id",ids[metric]},{"label",label},{"usage",usage},{"display",display},{"bar",true},{"available",measured},
            {"fraction",measured && maximum>0 ? total.used/maximum : 0},{"description",description}});
    }
    result.append(QVariantMap{{"id","pods"},{"label","Pods"},{"usage",QString::number(counts[1])},{"bar",false},{"fraction",0},{"description","Matching Pods"}});
    result.append(QVariantMap{{"id","nodes"},{"label","Nodes"},{"usage",QString::number(counts[0])},{"bar",false},{"fraction",0},{"description","Matching Nodes"}});
    return result;
}
QVariantList resourceMetricSummary(const QJsonArray& resources) {
    ResourceMetricSummary summary;
    for (const auto& resource : resources) summary.add(resourceMetricContribution(resource.toObject()));
    return summary.presentation();
}
QVariantList resourceMetricPresentation(const QJsonObject& resource) {
    const auto references=resource["metricReferences"].toObject(); if (references.isEmpty()) return {};
    QVariantList result;
    for (const auto& id : {"cpu", "memory", "storage"}) {
        const auto reference=references[id].toObject(); if (reference.isEmpty()) continue;
        const auto usage=resource[id]; const auto complete=resource["metricComplete"].toObject()[id].toBool();
        double maximum=usage.toDouble(); QStringList labels, presentLabels; QVariantList markers;
        for (const auto& key : {"request", "limit", "capacity"}) {
            if (QString(key)=="capacity" && !reference.contains(key) && reference.contains("requestComplete")) continue;
            const auto value=reference[key]; if (value.isDouble()) maximum=std::max(maximum, value.toDouble());
            QString label=QString(key)+": "+(reference[QString(key)+"Invalid"].toBool() ? "invalid" : value.isDouble() ? formatMetricQuantity(value.toDouble(), QString(id)=="cpu") : "not set");
            if (value.isDouble() && reference.contains(QString(key)+"Complete") && !reference[QString(key)+"Complete"].toBool()) label+=" (incomplete)";
            labels.append(label);
            if (value.isDouble() || reference[QString(key)+"Invalid"].toBool()) presentLabels.append(label);
        }
        for (const auto& key : {"request", "limit", "capacity"}) if (reference[key].isDouble()) markers.append(QVariantMap{{"label", key}, {"position", maximum>0 ? reference[key].toDouble()/maximum : 0}});
        QString measured=usage.isDouble() ? formatMetricQuantity(usage.toDouble(), QString(id)=="cpu")+(complete ? "" : " (incomplete)") : "Unavailable";
        result.append(QVariantMap{{"id", id}, {"label", QString(id)=="cpu" ? "CPU" : QString(id)=="memory" ? "Memory" : "Storage"}, {"usage", measured}, {"available", usage.isDouble()},
            {"references", labels.join("\n")}, {"presentReferences", presentLabels.join(", ")},
            {"fraction", maximum>0 && usage.isDouble() ? usage.toDouble()/maximum : 0}, {"markers", markers}, {"source", reference["source"].toString()},
            {"timestamp", usage.isDouble() ? resource["metricAt"].toString() : QString{}}, {"window", usage.isDouble() ? resource["metricWindow"].toString() : QString{}}});
    }
    return result;
}
}
