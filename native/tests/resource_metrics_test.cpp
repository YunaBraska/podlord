#include "resource_metrics.h"
#include <QCoreApplication>
#include <cmath>
#include <cstdio>

namespace {
const QDateTime measured=QDateTime::fromString("2026-10-03T12:00:00.000Z", Qt::ISODateWithMs);
QJsonObject identity(const QString& kind="Pod") {
    return {{"apiVersion", "v1"}, {"kind", kind}, {"name", "alpha"}, {"namespace", "default"}, {"uid", "uid-alpha"}, {"createdAt", measured.addSecs(-3600).toString(Qt::ISODateWithMs)}};
}
QJsonObject configured() {
    return {{"spec", QJsonObject{{"containers", QJsonArray{QJsonObject{{"name", "worker"}, {"resources", QJsonObject{{"requests", QJsonObject{{"cpu", "10m"}, {"memory", "16Mi"}}}, {"limits", QJsonObject{{"cpu", "100m"}, {"memory", "64Mi"}}}}}}}}}}};
}
QJsonObject sample() {
    return {{"timestamp", measured.toString(Qt::ISODateWithMs)}, {"window", "15s"}, {"containers", QJsonArray{QJsonObject{{"name", "worker"}, {"usage", QJsonObject{{"cpu", "25m"}, {"memory", "32Mi"}}}}}}};
}
bool run(const QString& scenario) {
    if (scenario.startsWith("quantity_")) {
        const QMap<QString,QPair<QString,double>> values{{"quantity_zero", {"0",0}}, {"quantity_cpu", {"25m",25}}, {"quantity_nano", {"250000000n",250}},
            {"quantity_micro", {"1000u",1}}, {"quantity_cores", {".5",500}}, {"quantity_binary", {"32Mi",33554432}}, {"quantity_decimal", {"2M",2000000}},
            {"quantity_exponent", {"2e3",2000}}, {"quantity_positive", {"+2",2}}, {"quantity_exabyte", {"1E",1e18}}, {"quantity_exbibyte", {"1Ei",1152921504606846976.0}}};
        if (values.contains(scenario)) {
            const auto input=values[scenario]; const auto value=podlord::metricQuantity(input.first, QStringList{"quantity_cpu", "quantity_nano", "quantity_micro", "quantity_cores"}.contains(scenario));
            return value && std::abs(*value-input.second)<=std::max(1.0,input.second)*1e-12;
        }
        const QMap<QString,QString> invalid{{"quantity_empty", ""}, {"quantity_negative", "-1"}, {"quantity_invalid", "10MB"}, {"quantity_whitespace", " 1"},
            {"quantity_overflow", "1e999"}, {"quantity_underflow", "1e-999"}, {"quantity_nonfinite", "NaN"}, {"quantity_exponent_overflow", "1e999999999999999"}};
        return invalid.contains(scenario) && !podlord::metricQuantity(invalid[scenario], false);
    }
    auto resource=identity(); auto document=configured();
    if (scenario.startsWith("reference_")) {
        if (scenario=="reference_other_api") resource["apiVersion"]="example.test/v1";
        if (scenario=="reference_other_kind") resource["kind"]="ConfigMap";
        if (scenario=="reference_missing") document={{"spec",QJsonObject{{"containers",QJsonArray{QJsonObject{{"name","worker"}}}}}}};
        if (scenario=="reference_partial" || scenario=="reference_sidecar" || scenario=="reference_init") {
            auto spec=document["spec"].toObject();
            if (scenario=="reference_partial") { auto containers=spec["containers"].toArray(); containers.append(QJsonObject{{"name", "second"}}); spec["containers"]=containers; }
            else spec["initContainers"]=QJsonArray{QJsonObject{{"name", "init"}, {"restartPolicy", scenario=="reference_sidecar" ? "Always" : "Never"}}};
            document["spec"]=spec;
        }
        if (scenario=="reference_pod_level") { auto spec=document["spec"].toObject(); spec["resources"]=QJsonObject{{"requests",QJsonObject{{"cpu","50m"}}}}; document["spec"]=spec; }
        if (scenario=="reference_invalid") { auto spec=document["spec"].toObject(); spec["resources"]=QJsonObject{{"requests",QJsonObject{{"cpu","invalid"}}}}; document["spec"]=spec; }
        if (scenario=="reference_node") { resource["kind"]="Node"; document={{"status",QJsonObject{{"capacity",QJsonObject{{"cpu","4"},{"memory","8Gi"},{"ephemeral-storage","16Gi"}}}}}}; }
        if (scenario=="reference_pvc" || scenario=="reference_pv") {
            resource["kind"]=scenario=="reference_pvc" ? "PersistentVolumeClaim" : "PersistentVolume";
            document={{"spec",QJsonObject{{"resources",QJsonObject{{"requests",QJsonObject{{"storage","1Gi"}}}}},{"capacity",QJsonObject{{"storage","2Gi"}}}}}, {"status",QJsonObject{{"capacity",QJsonObject{{"storage","3Gi"}}}}}};
        }
        const auto result=podlord::resourceMetricReferences(document,resource); const auto refs=result["metricReferences"].toObject(); const auto cpu=refs["cpu"].toObject();
        if (scenario=="reference_other_api" || scenario=="reference_other_kind") return result.isEmpty();
        if (scenario=="reference_missing") return !cpu.contains("request") && !cpu["requestInvalid"].toBool() && !cpu["requestComplete"].toBool();
        if (scenario=="reference_partial") return cpu["request"].toDouble()==10 && !cpu["requestComplete"].toBool();
        if (scenario=="reference_sidecar") return result["metricContainerNames"].toArray().size()==2;
        if (scenario=="reference_init") return result["metricContainerNames"].toArray().size()==1;
        if (scenario=="reference_pod_level") return cpu["request"].toDouble()==50 && cpu["requestComplete"].toBool();
        if (scenario=="reference_invalid") return !cpu.contains("request") && cpu["requestInvalid"].toBool();
        if (scenario=="reference_node") return cpu["capacity"].toDouble()==4000;
        if (scenario=="reference_pvc" || scenario=="reference_pv") return refs.size()==1 && refs["storage"].toObject()["capacity"].toDouble()==(scenario=="reference_pvc" ? 3.0 : 2.0)*1073741824;
        return cpu["request"].toDouble()==10 && cpu["limit"].toDouble()==100 && cpu["requestComplete"].toBool();
    }
    auto input=sample(); auto metricIdentity=identity("PodMetrics"); metricIdentity["apiVersion"]="metrics.k8s.io/v1beta1";
    if (scenario=="sample_timestamp") input["timestamp"]="yesterday";
    if (scenario=="sample_timezone") input["timestamp"]="2026-10-03T12:00:00";
    if (scenario=="sample_window") input["window"]="forever";
    if (scenario=="sample_containers") input["containers"]="bad";
    if (scenario=="sample_duplicate") { auto containers=input["containers"].toArray(); containers.append(containers.first()); input["containers"]=containers; }
    if (scenario=="sample_name") input["containers"]=QJsonArray{QJsonObject{{"usage",QJsonObject{{"cpu","1m"}}}}};
    if (scenario=="sample_usage") input["containers"]=QJsonArray{QJsonObject{{"name","worker"},{"usage","bad"}}};
    if (scenario=="sample_quantity") input["containers"]=QJsonArray{QJsonObject{{"name","worker"},{"usage",QJsonObject{{"cpu","-1m"}}}}};
    if (scenario=="sample_quantity_type") input["containers"]=QJsonArray{QJsonObject{{"name","worker"},{"usage",QJsonObject{{"cpu",1}}}}};
    if (scenario=="sample_node") { metricIdentity["kind"]="NodeMetrics"; input["usage"]=QJsonObject{{"cpu","1"},{"memory","1Gi"}}; }
    const auto parsed=podlord::resourceMetricSample(input, metricIdentity);
    if (scenario.startsWith("sample_")) return scenario=="sample_node" ? std::holds_alternative<QJsonObject>(parsed) && std::get<QJsonObject>(parsed)["metricValues"].toObject()["cpu"].toDouble()==1000 : scenario=="sample_valid" ? std::holds_alternative<QJsonObject>(parsed) : std::holds_alternative<podlord::Failure>(parsed);
    if (!std::holds_alternative<QJsonObject>(parsed)) return false;
    const auto references=podlord::resourceMetricReferences(document,resource);
    for (auto it=references.begin(); it!=references.end(); ++it) resource[it.key()]=it.value();
    auto normalized=std::get<QJsonObject>(parsed);
    if (scenario=="attach_zero") normalized["metricValues"]=QJsonObject{{"worker",QJsonObject{{"cpu",0},{"memory",0}}}};
    if (scenario=="attach_partial") { auto names=resource["metricContainerNames"].toArray(); names.append("missing"); resource["metricContainerNames"]=names; }
    if (scenario=="attach_recreated") resource["createdAt"]=measured.addSecs(1).toString(Qt::ISODateWithMs);
    if (scenario=="attach_future") normalized["metricAt"]=measured.addSecs(1).toString(Qt::ISODateWithMs);
    if (scenario=="attach_no_uid") resource.remove("uid");
    if (scenario=="attach_no_creation") resource.remove("createdAt");
    if (scenario=="attach_missing") normalized={};
    if (scenario=="attach_missing_cpu") normalized["metricValues"]=QJsonObject{{"worker",QJsonObject{{"memory",33554432}}}};
    auto result=podlord::withResourceMetrics(resource,normalized,measured.addSecs(scenario=="attach_stale" ? 26 : 0));
    const auto views=podlord::resourceMetricPresentation(result);
    if (scenario.startsWith("summary_")) {
        QJsonArray rows{result};
        if (scenario=="summary_empty") rows={};
        if (scenario=="summary_missing") { result.remove("cpu"); rows=QJsonArray{result}; }
        if (scenario=="summary_zero") { result["cpu"]=0; rows=QJsonArray{result}; }
        if (scenario=="summary_stale") { result["metricStale"]=true; rows=QJsonArray{result}; }
        if (scenario=="summary_partial") {
            auto missing=result; missing["uid"]="uid-beta"; missing["name"]="beta"; missing.remove("cpu"); rows.append(missing);
        }
        if (scenario=="summary_node" || scenario=="summary_overflow") {
            auto node=identity("Node"); node.remove("namespace");
            const QJsonObject document{{"status",QJsonObject{{"capacity",QJsonObject{{"cpu","4"},{"memory","8Gi"},{"ephemeral-storage","16Gi"}}}}}};
            const auto refs=podlord::resourceMetricReferences(document,node);
            for (auto it=refs.begin();it!=refs.end();++it) node[it.key()]=it.value();
            node["cpu"]=scenario=="summary_overflow" ? 1e308 : 1000.;
            node["memory"]=1073741824.;
            node["metricComplete"]=QJsonObject{{"cpu",true},{"memory",true}};
            rows.append(node);
            if (scenario=="summary_overflow") { node["uid"]="uid-other"; node["name"]="other"; rows.append(node); }
        }
        if (scenario=="summary_storage") {
            rows={};
            for (const auto& kind : {QString("PersistentVolumeClaim"),QString("PersistentVolume")}) {
                auto volume=identity(kind);
                const auto refs=podlord::resourceMetricReferences(QJsonObject{{"spec",QJsonObject{{"capacity",QJsonObject{{"storage","2Gi"}}}}},{"status",QJsonObject{{"capacity",QJsonObject{{"storage","3Gi"}}}}}},volume);
                for (auto it=refs.begin();it!=refs.end();++it) volume[it.key()]=it.value();
                rows.append(volume);
            }
        }
        const auto summary=podlord::resourceMetricSummary(rows);
        if (summary.size()!=5) return false;
        const auto usage=summary[0].toMap()["usage"].toString();
        if (scenario=="summary_empty") return usage=="Unavailable" && summary[3].toMap()["usage"]=="0";
        if (scenario=="summary_missing") return usage.startsWith("Unavailable") && !usage.startsWith("0");
        if (scenario=="summary_zero") return usage.startsWith("0 mCPU");
        if (scenario=="summary_partial") return usage.contains("incomplete");
        if (scenario=="summary_stale") return usage.contains("stale");
        if (scenario=="summary_node") return usage.startsWith("1000 mCPU / 4000 mCPU") && summary[3].toMap()["usage"]=="1" && summary[4].toMap()["usage"]=="1";
        if (scenario=="summary_overflow") return usage.startsWith("Unavailable (overflow)");
        if (scenario=="summary_storage") return summary[2].toMap()["usage"]=="Unavailable / 3 GiB";
        if (scenario=="summary_repeat") return summary==podlord::resourceMetricSummary(rows);
        return scenario=="summary_pod" && usage=="25 mCPU / 100 mCPU";
    }
    if (views.size()!=3) return false;
    const auto cpu=views.first().toMap();
    if (QStringList{"attach_recreated","attach_future","attach_no_uid","attach_no_creation","attach_missing","attach_missing_cpu"}.contains(scenario)) return !result.contains("cpu") && cpu["usage"]=="Unavailable";
    if (scenario=="attach_zero") return result.contains("cpu") && result["cpu"].toDouble()==0 && cpu["usage"]=="0 mCPU";
    if (scenario=="attach_partial") return result["cpu"].toDouble()==25 && cpu["usage"].toString().contains("incomplete");
    if (scenario=="attach_stale") return cpu["usage"].toString().contains("stale") && cpu["timestamp"].toString()==measured.toString(Qt::ISODateWithMs);
    if (scenario=="attach_repeat") return result==podlord::withResourceMetrics(result,normalized,measured);
    if (scenario=="attach_storage") return views.last().toMap()["usage"]=="Unavailable";
    return cpu["usage"]=="25 mCPU" && cpu["references"].toString().contains("250% of request") && cpu["markers"].toList().size()==2 && views[1].toMap()["usage"]=="32 MiB";
}
}
int main(int argc,char** argv) {
    QCoreApplication app(argc,argv); if (argc!=2) return 2;
    const auto scenario=QString::fromLocal8Bit(argv[1]); const bool passed=run(scenario);
    if (!passed) std::fprintf(stderr,"Metric scenario failed: %s\n",qPrintable(scenario));
    return passed ? 0 : 1;
}
