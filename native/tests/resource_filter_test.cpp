#include "workspace.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QDateTime>
#include <QtCore/qtestsupport_core.h>
#include <algorithm>
#include <cstdio>

namespace {
bool run(const QString& scenario) {
    if (scenario == "benchmark" || scenario == "metadata_benchmark") {
        podlord::ResourceTable rows;
        QJsonArray snapshot;
        for (int index=0; index<5000; ++index) {
            const auto name=QString("workload-%1").arg(index,4,10,QChar('0'));
            snapshot.append(QJsonObject{{"path","/api/v1/namespaces/benchmark/pods/"+name},{"name",name},
                {"kind","Pod"},{"namespace","benchmark"},{"status","Running"},{"restarts",index%4}});
        }
        rows.publish(snapshot,"benchmark-cluster");
        if (scenario == "metadata_benchmark") {
            const QList<int> roles{Qt::UserRole, Qt::UserRole+1, Qt::UserRole+2, Qt::UserRole+3,
                Qt::UserRole+4, Qt::UserRole+9, Qt::UserRole+10};
            QList<double> samples;
            qsizetype expected = -1;
            for (int iteration = 0; iteration < 35; ++iteration) {
                QElapsedTimer timer; timer.start();
                qsizetype checksum = 0;
                for (int row = 0; row < rows.rowCount(); ++row)
                    for (const auto role : roles)
                        checksum += rows.data(rows.index(row, iteration % rows.columnCount()), role).toString().size();
                if (expected < 0) expected = checksum;
                if (checksum == 0 || checksum != expected) return false;
                if (iteration >= 5) samples.append(timer.nsecsElapsed()/1000000.0);
            }
            std::sort(samples.begin(), samples.end());
            const auto evidence = QJsonDocument(QJsonObject{{"boundary", "public cached table metadata"}, {"rows", 5000},
                {"roles_per_row", roles.size()}, {"samples", samples.size()}, {"p50_ms", samples[15]},
                {"p95_ms", samples[28]}, {"max_ms", samples.last()}, {"checksum", qint64(expected)}}).toJson(QJsonDocument::Compact);
            std::printf("%s\n", evidence.constData());
            return true;
        }
        podlord::ResourceFilter filter; filter.setSourceModel(&rows);
        const QList<QPair<QString,int>> queries{{"\"Pod\"",5000},{"~workload-00",100},
            {"/^workload-00[0-9]{2}$/",100},{"missing",0},{"workload-0001 workload-4999",2}};
        QList<double> milliseconds;
        for (int iteration=0; iteration<55; ++iteration) {
            const auto& query=queries[iteration%queries.size()];
            QElapsedTimer timer; timer.start();
            if (!filter.filter(query.first) || filter.rowCount()!=query.second) return false;
            if (iteration>=5) milliseconds.append(timer.nsecsElapsed()/1000000.0);
        }
        std::sort(milliseconds.begin(),milliseconds.end());
        const auto evidence=QJsonDocument(QJsonObject{{"boundary","public cached resource model filter"},{"rows",5000},
            {"samples",milliseconds.size()},{"p50_ms",milliseconds[25]},{"p95_ms",milliseconds[47]},
            {"max_ms",milliseconds.last()},{"queries","exact, prefix, regex, missing, alternatives"}}).toJson(QJsonDocument::Compact);
        std::printf("%s\n",evidence.constData()); return true;
    }
    podlord::ResourceTable rows(nullptr, {"name", "kind", "restarts"}, {"Name", "Kind", "Restarts"});
    rows.publish(QJsonArray{
        QJsonObject{{"path", "a"}, {"name", "alpha"}, {"kind", "Pod"}, {"restarts", 10}},
        QJsonObject{{"path", "b"}, {"name", "bravo"}, {"kind", "Pod"}, {"restarts", 2}},
        QJsonObject{{"path", "c"}, {"name", "custom-widget"}, {"kind", "Widget"}}});
    podlord::ResourceFilter filter; filter.setSourceModel(&rows);
    if (scenario.startsWith("sort_")) {
        const auto ordered = [&] {
            QString paths;
            for (int row = 0; row < filter.rowCount(); ++row) paths += filter.index(row, 0).data(Qt::UserRole).toString();
            return paths;
        };
        filter.sort(0);
        if (ordered() != "abc") return false;
        auto a = rows.row(0), b = rows.row(1), c = rows.row(2);
        if (scenario == "sort_refresh") { a["name"] = "zulu"; rows.publish({a, b, c}); return ordered() == "bca"; }
        if (scenario == "sort_insert") { rows.publish({a, b, c, QJsonObject{{"path", "d"}, {"name", "aardvark"}}}); return ordered() == "dabc"; }
        if (scenario == "sort_remove") { rows.publish({a, c}); return ordered() == "ac"; }
        if (scenario == "sort_missing") {
            a["name"] = ""; rows.publish({a, b, c});
            if (ordered() != "bca") return false;
            filter.sort(0, Qt::DescendingOrder); return ordered() == "cba";
        }
        if (scenario == "sort_ties") {
            a["name"] = "same"; b["name"] = "same"; c["name"] = "same"; rows.publish({a, b, c});
            if (ordered() != "abc") return false;
            filter.sort(0, Qt::DescendingOrder); return ordered() == "abc";
        }
        if (scenario == "sort_changed_column") { filter.sort(2); return ordered() == "bac"; }
        if (scenario == "sort_numeric") {
            filter.sort(2); a["restarts"] = 1; b["restarts"] = 20; c["restarts"] = 0;
            rows.publish({a, b, c}); return ordered() == "cab";
        }
        if (scenario == "sort_none") { filter.sort(0, Qt::DescendingOrder); filter.sort(-1); return ordered() == "abc"; }
        if (scenario == "sort_unicode") {
            a["name"] = QString::fromUtf8("\xc3\x85ngstr\xc3\xb6m"); b["name"] = "Zebra"; c["name"] = QString::fromUtf8("\xc3\x96rn");
            rows.publish({a, b, c}); return ordered() == "bac";
        }
        if (scenario == "sort_cluster") {
            podlord::ResourceTable clusters(nullptr, {"cluster"}, {"Cluster"});
            a["cluster"] = "bravo"; b["cluster"] = "alpha";
            clusters.publish({a, b, c}, "zulu"); filter.setSourceModel(&clusters); filter.sort(0);
            if (ordered() != "bac") return false;
            clusters.publish({a, b, c}, "aardvark"); return ordered() == "cba";
        }
        if (scenario == "sort_future_age") {
            podlord::ResourceTable ages(nullptr, {"createdAt"}, {"Age"});
            a["createdAt"] = QDateTime::currentDateTimeUtc().addMSecs(500).toString(Qt::ISODateWithMs);
            ages.publish({a}); filter.setSourceModel(&ages); filter.sort(0);
            if (filter.index(0, 0).data(Qt::UserRole + 6).isValid()) return false;
            return QTest::qWaitFor([&] { return filter.index(0, 0).data(Qt::UserRole + 6).isValid(); }, 3000);
        }
        return false;
    }
    QString expression, expected;
    bool valid = true;
    if (scenario == "contains") { expression="alp"; expected="a"; }
    else if (scenario == "case") { expression="ALPHA"; expected="a"; }
    else if (scenario == "exact") { expression="\"alpha\""; expected="a"; }
    else if (scenario == "exact_excludes") expression="\"alp\"";
    else if (scenario == "exact_operator") { expression="=Pod"; expected="ab"; }
    else if (scenario == "alternatives") { expression="alpha bravo"; expected="ab"; }
    else if (scenario == "mixed") { expression="\"alpha\" /widget$/"; expected="ac"; }
    else if (scenario == "prefix") { expression="~ALP"; expected="a"; }
    else if (scenario == "prefix_excludes") expression="~pha";
    else if (scenario == "suffix") { expression="AVO~"; expected="b"; }
    else if (scenario == "suffix_excludes") expression="bra~";
    else if (scenario == "regex") { expression="/^a.*a$/"; expected="a"; }
    else if (scenario == "regex_case") { expression="/^BRAVO$/"; expected="b"; }
    else if (scenario == "regex_escape") { expression="/\\d+/"; expected="ab"; }
    else if (scenario == "regex_empty") { expression="//"; expected="abc"; }
    else if (scenario == "number") { expression="2"; expected="b"; }
    else if (scenario == "greater") { expression=">2"; expected="a"; }
    else if (scenario == "greater_equal") { expression=">=2"; expected="ab"; }
    else if (scenario == "greater_alias") { expression="=>2"; expected="ab"; }
    else if (scenario == "less") { expression="<10"; expected="b"; }
    else if (scenario == "less_equal") { expression="<=10"; expected="ab"; }
    else if (scenario == "less_alias") { expression="=<10"; expected="ab"; }
    else if (scenario == "number_alternatives") { expression="=10 =2"; expected="ab"; }
    else if (scenario == "negative") { expression=">-1"; expected="ab"; }
    else if (scenario == "blank") { expression=" \t\n "; expected="abc"; }
    else if (scenario == "empty") expected="abc";
    else if (scenario == "invalid_regex") { expression="/[/"; valid=false; }
    else if (scenario == "unclosed_regex") { expression="/alpha"; valid=false; }
    else if (scenario == "unclosed_quote") { expression="\"alpha"; valid=false; }
    else if (scenario == "invalid_mixed") { expression="alpha /[/"; valid=false; }
    else if (scenario == "regex_budget") {
        rows.publish(QJsonArray{QJsonObject{{"path","a"},{"name",QString(30000,'a')+"!"}}});
        expression="/(*NO_START_OPT)(a+)+$/"; valid=false;
    } else if (scenario == "escaped_quote") {
        rows.publish(QJsonArray{QJsonObject{{"path","a"},{"name","say \"hello\""}}});
        expression="\"say \\\"hello\\\"\""; expected="a";
    } else if (scenario == "escaped_slash") {
        rows.publish(QJsonArray{QJsonObject{{"path","a"},{"name","registry/image"}}});
        expression="/registry\\/image/"; expected="a";
    } else if (scenario == "unicode") {
        rows.publish(QJsonArray{QJsonObject{{"path","a"},{"name",QString::fromUtf8("\xc3\x85ngstr\xc3\xb6m")}}});
        expression=QString::fromUtf8("\xc3\xa5NGSTR\xc3\xb6m"); expected="a";
    } else if (scenario == "repeat") {
        if (!filter.filter("alpha bravo") || !filter.filter("alpha bravo")) return false;
        expression="alpha bravo"; expected="ab";
    } else if (scenario == "clear") {
        filter.filter("/[/"); expression=""; expected="abc";
    } else if (scenario == "source_update") {
        if (!filter.filter("~new")) return false;
        rows.publish(QJsonArray{QJsonObject{{"path","a"},{"name","new-alpha"}}});
        expression="~new"; expected="a";
    } else if (scenario == "source_update_batch") {
        if (!filter.filter("~new")) return false;
        int notifications = 0;
        bool completeNotification = false;
        QObject observer;
        QObject::connect(&rows, &QAbstractItemModel::dataChanged, &observer,
            [&](const QModelIndex&, const QModelIndex&, const QList<int>& roles) {
                ++notifications;
                completeNotification = roles.contains(Qt::DisplayRole) && roles.contains(Qt::UserRole + 9);
            });
        rows.publish(QJsonArray{
            QJsonObject{{"path","a"},{"name","new-alpha"},{"kind","Pod"},{"restarts",10},{"problemSeverity",2}},
            QJsonObject{{"path","b"},{"name","bravo"},{"kind","Pod"},{"restarts",2}},
            QJsonObject{{"path","c"},{"name","custom-widget"},{"kind","Widget"}}}, "next-cluster");
        if (notifications != 1 || !completeNotification) {
            std::fprintf(stderr, "Cached snapshot emitted %d updates instead of one complete update.\n", notifications);
            return false;
        }
        expression="~new"; expected="a";
    } else if (scenario == "sort") {
        expression="alpha bravo"; expected="ba"; filter.sort(0,Qt::DescendingOrder);
    } else return false;
    const auto accepted=filter.filter(expression);
    QCoreApplication::processEvents();
    QString actual;
    for(int row=0;row<filter.rowCount();++row) actual+=filter.data(filter.index(row,0),Qt::UserRole).toString();
    if (accepted==valid && actual==expected) return true;
    std::fprintf(stderr,"Filter %s: accepted=%d expected=%d rows=%s expected=%s\n",qPrintable(scenario),accepted,valid,qPrintable(actual),qPrintable(expected));
    return false;
}
}
int main(int argc,char** argv) {
    QCoreApplication app(argc,argv); if(argc!=2) return 2;
    return run(QString::fromLocal8Bit(argv[1])) ? 0 : 1;
}
