#include "workspace.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QDateTime>
#include <QStringListModel>
#include <QtCore/qtestsupport_core.h>
#include <algorithm>
#include <cstdio>

namespace {
bool tableScenario(const QString& scenario) {
    using podlord::ResourceTable;
    if (scenario == "table_cached_refresh") {
        ResourceTable rows(nullptr, {"name", "cpu"}, {"Name", "CPU"});
        QJsonObject row{{"path", "a"}, {"name", "alpha"}, {"cpu", 2000},
            {"metricComplete", QJsonObject{{"cpu", false}}}, {"metricStale", true}};
        rows.publish({row});
        if (rows.data(rows.index(0, 0)) != "alpha" || rows.data(rows.index(0, 0)) != "alpha"
            || !rows.data(rows.index(0, 1)).toString().contains("incomplete")
            || !rows.data(rows.index(0, 1), Qt::ToolTipRole).toString().contains("stale")) return false;
        row["name"] = "new<one>\nline"; row["cpu"] = 3000;
        row["metricComplete"] = QJsonObject{{"cpu", true}}; row["metricStale"] = false;
        rows.publish({row});
        const auto metric = rows.data(rows.index(0, 1)).toString();
        return rows.data(rows.index(0, 0)) == "new<one>\nline"
            && rows.data(rows.index(0, 0), Qt::ToolTipRole) == "<span>new&lt;one&gt;<br>line</span>"
            && metric.contains("3000") && !metric.contains("incomplete") && !metric.contains("stale");
    }
    if (scenario == "table_cached_membership") {
        ResourceTable rows(nullptr, {"name"}, {"Name"});
        rows.publish({QJsonObject{{"path", "a"}, {"name", "alpha"}}, QJsonObject{{"path", "z"}, {"name", "zulu"}}});
        if (rows.data(rows.index(0, 0)) != "alpha" || rows.data(rows.index(1, 0)) != "zulu") return false;
        bool readingPosition = true;
        QObject::connect(&rows, &QAbstractItemModel::rowsAboutToBeInserted, [&] {
            readingPosition = readingPosition && rows.data(rows.index(rows.rowCount() - 1, 0)) == "zulu";
        });
        rows.publish({QJsonObject{{"path", "0"}, {"name", "zero"}}, QJsonObject{{"path", "c"}, {"name", "charlie"}},
            QJsonObject{{"path", "z"}, {"name", "zulu"}}});
        return readingPosition && rows.rowCount() == 3 && rows.data(rows.index(0, 0)) == "zero"
            && rows.data(rows.index(1, 0)) == "charlie" && rows.data(rows.index(2, 0)) == "zulu";
    }
    if (scenario == "table_cached_handoff") {
        ResourceTable rows(nullptr, {"name"}, {"Name"});
        rows.publish({QJsonObject{{"path", "a"}, {"name", "alpha"}}, QJsonObject{{"path", "b"}, {"name", "bravo"}}});
        if (rows.data(rows.index(0, 0)) != "alpha" || rows.data(rows.index(1, 0)) != "bravo") return false;
        rows.publish({QJsonObject{{"path", "x"}, {"name", "x-ray"}}, QJsonObject{{"path", "y"}, {"name", "yankee"}},
            QJsonObject{{"path", "z"}, {"name", "zulu"}}}, {}, true);
        if (rows.rowCount() != 3 || rows.data(rows.index(0, 0)) != "x-ray" || rows.data(rows.index(2, 0)) != "zulu") return false;
        rows.publish({QJsonObject{{"path", "w"}, {"name", "whiskey"}}}, {}, true);
        if (rows.rowCount() != 1 || rows.data(rows.index(0, 0)) != "whiskey") return false;
        rows.publish({}, {}, true);
        if (rows.rowCount() != 0) return false;
        rows.publish({QJsonObject{{"path", "a"}, {"name", "final"}}}, {}, true);
        return rows.rowCount() == 1 && rows.data(rows.index(0, 0)) == "final";
    }
    if (scenario == "table_cached_cluster") {
        ResourceTable rows(nullptr, {"cluster"}, {"Cluster"});
        const QJsonArray snapshot{QJsonObject{{"path", "a"}, {"cluster", "fixed"}}, QJsonObject{{"path", "b"}}};
        rows.publish(snapshot, "old");
        if (rows.data(rows.index(0, 0)) != "fixed" || rows.data(rows.index(1, 0), Qt::ToolTipRole) != "<span>old</span>") return false;
        rows.publish(snapshot, "new");
        return rows.data(rows.index(0, 0)) == "fixed" && rows.data(rows.index(1, 0)) == "new"
            && rows.data(rows.index(1, 0), Qt::UserRole + 6) == "new";
    }
    if (scenario == "table_cached_age") {
        ResourceTable rows(nullptr, {"createdAt"}, {"Age"});
        rows.publish({QJsonObject{{"path", "a"}, {"createdAt", QDateTime::currentDateTimeUtc().addMSecs(1000).toString(Qt::ISODateWithMs)}}});
        if (rows.data(rows.index(0, 0)) != "-") return false;
        return QTest::qWaitFor([&] { return rows.data(rows.index(0, 0)).toString().endsWith('s'); }, 5000);
    }
    if (scenario == "table_cached_ordered_repeat") {
        ResourceTable rows(nullptr, {"name"}, {"Name"}, "path", true);
        QJsonArray snapshot{QJsonObject{{"path", "b"}, {"name", "bravo"}}, QJsonObject{{"path", "a"}, {"name", "alpha"}}};
        rows.publish(snapshot);
        if (rows.data(rows.index(0, 0)) != "bravo" || rows.data(rows.index(1, 0)) != "alpha") return false;
        int resets = 0;
        QObject::connect(&rows, &QAbstractItemModel::modelReset, [&] { ++resets; });
        rows.publish(snapshot);
        if (resets != 0 || rows.data(rows.index(0, 0)) != "bravo") return false;
        snapshot[1] = QJsonObject{{"path", "a"}, {"name", "updated"}};
        rows.publish(snapshot);
        return resets == 1 && rows.data(rows.index(1, 0)) == "updated";
    }
    if (scenario == "table_empty_bounds") {
        ResourceTable rows(nullptr, {"name"}, {"Name"});
        return rows.rowCount() == 0 && rows.columnCount() == 1 && rows.row(0).isEmpty()
            && !rows.data(rows.index(0, 0)).isValid()
            && rows.headerData(0, Qt::Horizontal, Qt::DisplayRole) == "Name"
            && rows.headerData(0, Qt::Horizontal, Qt::UserRole) == "name"
            && !rows.headerData(0, Qt::Vertical, Qt::DisplayRole).isValid()
            && !rows.headerData(2, Qt::Horizontal, Qt::DisplayRole).isValid();
    }
    if (scenario == "table_cluster_refresh") {
        ResourceTable rows(nullptr, {"cluster"}, {"Cluster"});
        const QJsonObject explicitCluster{{"path", "a"}, {"cluster", "fixed"}};
        const QJsonObject inheritedCluster{{"path", "b"}};
        rows.publish({explicitCluster, inheritedCluster}, "old");
        if (rows.data(rows.index(0, 0)) != "fixed" || rows.data(rows.index(1, 0)) != "old") return false;
        rows.publish({explicitCluster, inheritedCluster}, "new");
        return rows.rowCount() == 2 && rows.data(rows.index(0, 0), Qt::UserRole + 6) == "fixed"
            && rows.data(rows.index(1, 0), Qt::UserRole + 6) == "new";
    }
    if (scenario == "table_incremental_snapshot") {
        ResourceTable rows(nullptr, {"name"}, {"Name"});
        rows.publish({QJsonObject{{"path", "a"}, {"name", "alpha"}}, QJsonObject{{"path", "b"}, {"name", "bravo"}},
            QJsonObject{{"path", "c"}, {"name", "charlie"}}});
        rows.publish({QJsonObject{{"path", "a"}, {"name", "updated"}}, QJsonObject{{"path", "c"}, {"name", "charlie"}},
            QJsonObject{{"path", "d"}, {"name", "delta"}}});
        return rows.rowCount() == 3 && rows.data(rows.index(0, 0)) == "updated"
            && rows.data(rows.index(1, 0), Qt::UserRole) == "c"
            && rows.data(rows.index(2, 0), Qt::UserRole) == "d";
    }
    if (scenario == "table_ordered_snapshot") {
        ResourceTable rows(nullptr, {"name"}, {"Name"}, "path", true);
        rows.publish({QJsonObject{{"path", "c"}, {"name", "charlie"}}, QJsonObject{{"path", "a"}, {"name", "alpha"}}});
        if (rows.data(rows.index(0, 0), Qt::UserRole) != "c") return false;
        rows.publish({QJsonObject{{"path", "a"}, {"name", "alpha"}}, QJsonObject{{"path", "c"}, {"name", "charlie"}}});
        return rows.rowCount() == 2 && rows.data(rows.index(0, 0), Qt::UserRole) == "a";
    }
    if (scenario == "table_metric_and_age") {
        ResourceTable rows(nullptr, {"cpu", "memory", "storage", "createdAt", "ready", "owner"},
            {"CPU", "Memory", "Storage", "Age", "Ready", "Owner"});
        rows.publish({QJsonObject{{"path", "a"}, {"cpu", 0.25}, {"memory", 1024},
            {"metricComplete", QJsonObject{{"cpu", false}, {"memory", true}}}, {"metricStale", true},
            {"createdAt", QDateTime::currentDateTimeUtc().addDays(1).toString(Qt::ISODateWithMs)},
            {"ready", "1/2"}, {"readyCount", 1}, {"containerCount", 2}}});
        const bool unchanged = rows.data(rows.index(0, 0)).toString().contains("incomplete")
            && rows.data(rows.index(0, 0)).toString().contains("stale")
            && !rows.data(rows.index(0, 1)).toString().contains("incomplete")
            && rows.data(rows.index(0, 2)) == "-" && rows.data(rows.index(0, 3)) == "-"
            && !rows.data(rows.index(0, 3), Qt::UserRole + 6).isValid()
            && rows.data(rows.index(0, 4), Qt::UserRole + 6).toDouble() == 0.5
            && rows.data(rows.index(0, 5)) == "-";
        podlord::ResourceDisplayModel display(&rows);
        if (!unchanged || display.data(display.index(0, 0)) != "0.25m (incomplete) (stale)"
            || display.data(display.index(0, 1)) != "1Ki (stale)" || display.data(display.index(0, 2)) != "-"
            || display.data(display.index(0, 0), Qt::ToolTipRole) != "<span>0.25 mCPU (incomplete) (stale)</span>"
            || display.data(display.index(0, 0), Qt::UserRole + 6).toDouble() != 0.25) return false;
        rows.publish({QJsonObject{{"path", "a"}, {"cpu", 1234.56789}, {"memory", 1073741824.0}, {"storage", 123456789.0},
            {"metricComplete", QJsonObject{{"cpu", true}, {"memory", true}, {"storage", true}}}},
            QJsonObject{{"path", "b"}, {"cpu", 0}, {"memory", 0}, {"storage", 0},
                {"metricComplete", QJsonObject{{"cpu", true}, {"memory", true}, {"storage", true}}}}});
        return display.data(display.index(0, 0)) == "1.23c" && display.data(display.index(0, 1)) == "1Gi"
            && display.data(display.index(0, 2)) == "118Mi"
            && display.data(display.index(0, 0), Qt::UserRole + 6).toDouble() == 1234.56789
            && display.data(display.index(0, 2), Qt::UserRole + 6).toDouble() == 123456789.0
            && display.data(display.index(0, 0), Qt::ToolTipRole) == "<span>1234.5679 mCPU</span>"
            && display.data(display.index(1, 0)) == "0m" && display.data(display.index(1, 1)) == "0B"
            && display.data(display.index(1, 2)) == "0B";
    }
    if (scenario == "table_safe_tooltip") {
        ResourceTable rows(nullptr, {"name"}, {"Name"});
        rows.publish({QJsonObject{{"path", "a"}, {"name", "<b>one\nnext</b>"}}});
        return rows.data(rows.index(0, 0)) == "<b>one\nnext</b>"
            && rows.data(rows.index(0, 0), Qt::ToolTipRole) == "<span>&lt;b&gt;one<br>next&lt;/b&gt;</span>";
    }
    return false;
}
bool run(const QString& scenario) {
    if (scenario.startsWith("table_")) return tableScenario(scenario);
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
        if (scenario == "sort_manual_update") {
            filter.setDynamicSortFilter(false);
            if (!filter.filter("\"Pod\"")) return false;
            a["kind"] = "Widget"; c["kind"] = "Pod"; rows.publish({a, b, c});
            filter.sort(0);
            return ordered() == "bc";
        }
        if (scenario == "sort_display_role") {
            filter.setSortRole(Qt::DisplayRole); filter.sort(2);
            if (ordered() != "cab") return false;
            filter.sort(2, Qt::DescendingOrder);
            return ordered() == "bac";
        }
        if (scenario == "sort_external_model") {
            QStringListModel external({"bravo", "Alpha", "charlie"});
            filter.setSourceModel(&external); filter.setSortRole(Qt::DisplayRole); filter.sort(0);
            if (filter.index(0, 0).data() != "Alpha" || filter.index(2, 0).data() != "charlie") return false;
            filter.sort(0, Qt::DescendingOrder);
            return filter.index(0, 0).data() == "charlie" && filter.index(2, 0).data() == "Alpha";
        }
        if (scenario == "sort_locale") {
            filter.setSortLocaleAware(true); filter.sort(0);
            if (ordered() != "abc") return false;
            filter.sort(0, Qt::DescendingOrder);
            return ordered() == "cba";
        }
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
