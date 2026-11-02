#include "workspace.h"
#include <QAbstractItemModelTester>
#include <QCoreApplication>
#include <QSemaphore>
#include <QSignalSpy>
#include <QThread>
#include <QThreadPool>
#include <QtConcurrent/QtConcurrentRun>
#include <cstdio>

#define CHECK(value) do { if (!(value)) { std::fprintf(stderr, "Failed line %d: %s\n", __LINE__, #value); return 1; } } while (false)

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    const QString scenario = app.arguments().value(1);
    using Table = podlord::ResourceTable;
    const auto now = QDateTime::currentDateTimeUtc();
    const QJsonObject alpha{{"apiVersion", "v1"}, {"kind", "Pod"}, {"path", "/pods/a"}, {"uid", "first-uid"},
        {"name", "alpha"}, {"namespace", "one"}, {"status", "Running"}, {"cpu", 12.5}, {"memory", 4096.0},
        {"createdAt", now.addSecs(-60).toString(Qt::ISODateWithMs)}, {"metricComplete", QJsonObject{{"cpu", true}, {"memory", true}}},
        {"metricReferences", QJsonObject{{"cpu", QJsonObject{{"limit", 50.0}}}, {"memory", QJsonObject{{"limit", 8192.0}}}}}};
    const QJsonObject beta{{"apiVersion", "v1"}, {"kind", "ConfigMap"}, {"path", "/configmaps/b"}, {"name", "beta"},
        {"namespace", "two"}, {"status", "Ready"}, {"createdAt", now.addSecs(60).toString(Qt::ISODateWithMs)}};
    const QJsonArray source{alpha, beta};
    if (QStringList{"handoff_raw_grow", "handoff_raw_shrink", "handoff_raw_same", "handoff_prepared_grow", "handoff_prepared_shrink"}.contains(scenario)) {
        Table table;
        QAbstractItemModelTester checker(&table,QAbstractItemModelTester::FailureReportingMode::Fatal);
        auto replacement = beta; replacement["name"] = "replacement-beta";
        const bool grow = scenario.endsWith("grow");
        const bool same = scenario.endsWith("same");
        const QJsonArray before = grow ? QJsonArray{beta} : same ? QJsonArray{alpha} : source;
        const QJsonArray after = grow ? QJsonArray{alpha,replacement} : QJsonArray{replacement};
        CHECK(table.publish(before,"before",true));
        QSignalSpy changedRows(&table,&QAbstractItemModel::dataChanged);
        QSignalSpy inserted(&table,&QAbstractItemModel::rowsInserted), removed(&table,&QAbstractItemModel::rowsRemoved), reset(&table,&QAbstractItemModel::modelReset);
        if (scenario.startsWith("handoff_prepared")) {
            auto future = QtConcurrent::run(&Table::prepare,after,QString("after"),0);
            future.waitForFinished();
            CHECK(future.resultCount() == 1 && table.publishPrepared(future.result(),true));
        } else CHECK(table.publish(after,"after",true));
        CHECK(table.rowCount() == after.size() && changedRows.size() == 1);
        if (same) CHECK(inserted.isEmpty() && removed.isEmpty() && reset.isEmpty());
        CHECK(table.data(table.index(0,0)).toString() == "replacement-beta");
        CHECK(table.data(table.index(0,6)).toString() == "after");
        return 0;
    }
    if (scenario == "canceled_entry") {
        QPromise<Table::PreparedSnapshot> promise;
        promise.start();
        auto canceled = promise.future(); canceled.cancel();
        Table::prepare(promise, source, "cluster", 0); promise.finish();
        CHECK(canceled.isCanceled() && canceled.resultCount() == 0);
        return 0;
    }
    if (scenario == "cancel") {
        QThreadPool pool; pool.setMaxThreadCount(1);
        QSemaphore entered, release;
        auto blocker = QtConcurrent::run(&pool, [&] { entered.release(); release.acquire(); });
        entered.acquire();
        auto future = QtConcurrent::run(&pool, &Table::prepare, source, QString("cluster"), 0);
        future.cancel(); release.release(); blocker.waitForFinished(); future.waitForFinished();
        CHECK(future.isCanceled() && future.resultCount() == 0);
        return 0;
    }
    QThread* worker = nullptr;
    auto future = QtConcurrent::run([&](QPromise<Table::PreparedSnapshot>& promise) {
        worker = QThread::currentThread();
        Table::prepare(promise, scenario == "empty" ? QJsonArray{} : source, "cluster", 0);
    });
    future.waitForFinished();
    CHECK(worker != app.thread() && future.resultCount() == 1);
    const auto prepared = future.result();
    Table actual;
    if (scenario == "stale" || scenario == "recreated") {
        auto changed = alpha;
        changed[scenario == "stale" ? "name" : "uid"] = "replacement";
        CHECK(actual.publish({changed, beta}, "cluster"));
        auto outdated = prepared;
        CHECK(!actual.retainPreparedSort(outdated));
        QSignalSpy changedRows(&actual, &QAbstractItemModel::dataChanged);
        CHECK(!actual.publishPrepared(prepared));
        CHECK(changedRows.isEmpty());
        CHECK(actual.row(1)[scenario == "stale" ? "name" : "uid"] == "replacement");
        return 0;
    }
    if (scenario == "schema_identity" || scenario == "schema_order") {
        QStringList fields, captions;
        for (int column = 0; column < actual.columnCount(); ++column) {
            fields.append(actual.headerData(column, Qt::Horizontal, Qt::UserRole).toString());
            captions.append(actual.headerData(column, Qt::Horizontal, Qt::DisplayRole).toString());
        }
        Table other(nullptr, fields, captions, scenario == "schema_identity" ? "uid" : "path", scenario == "schema_order");
        CHECK(other.publish(source, "cluster"));
        const auto first = other.row(0), second = other.row(1);
        auto incompatible = prepared;
        CHECK(!other.publishPrepared(incompatible, true) && !other.retainPreparedSort(incompatible));
        CHECK(other.row(0) == first && other.row(1) == second);
        return 0;
    }
    if (scenario == "schema") {
        Table other(nullptr, {"name"}, {"Name"}, "id");
        CHECK(other.publish({QJsonObject{{"id", "retained"}, {"name", "original"}}}));
        CHECK(!other.publishPrepared(prepared, true) && other.rowCount() == 1 && other.row(0)["name"] == "original");
        auto incompatible = prepared;
        CHECK(!other.retainPreparedSort(incompatible));
        CHECK(!actual.publishPrepared(Table::PreparedSnapshot{}, true));
        return 0;
    }
    CHECK(actual.publishPrepared(prepared, true));
    if (scenario == "canonical") {
        Table reference; CHECK(reference.publish(source, "cluster"));
        CHECK(actual.rowCount() == reference.rowCount());
        for (int row = 0; row < actual.rowCount(); ++row) CHECK(actual.row(row) == reference.row(row));
        return 0;
    }
    if (scenario == "unchanged") {
        QSignalSpy changes(&actual, &QAbstractItemModel::dataChanged);
        QSignalSpy inserts(&actual, &QAbstractItemModel::rowsInserted), removes(&actual, &QAbstractItemModel::rowsRemoved);
        CHECK(actual.publishPrepared(prepared, true));
        CHECK(changes.isEmpty() && inserts.isEmpty() && removes.isEmpty());
        return 0;
    }
    if (scenario == "metadata") {
        CHECK(actual.metadata(-1, Qt::UserRole).isEmpty() && actual.metadata(actual.rowCount(), Qt::UserRole).isEmpty());
        CHECK(actual.metadata(0, Qt::UserRole - 1).isEmpty() && actual.metadata(0, Qt::UserRole + 5).isEmpty());
        auto malformed = alpha;
        malformed["name"] = 42; malformed["kind"] = false;
        malformed["namespace"] = QJsonArray{"not-text"}; malformed["status"] = QJsonObject{{"not", "text"}};
        const QJsonArray entries{malformed, QJsonObject{{"path", "/missing"}}};
        auto next = QtConcurrent::run(&Table::prepare, entries, QString("cluster"), -1);
        next.waitForFinished();
        Table reference; CHECK(reference.publish(entries, "cluster"));
        CHECK(actual.publishPrepared(next.result(), true));
        for (int row = 0; row < actual.rowCount(); ++row)
            for (int role = Qt::UserRole; role <= Qt::UserRole + 5; ++role)
                CHECK(actual.data(actual.index(row, 0), role) == reference.data(reference.index(row, 0), role));
        return 0;
    }
    if (scenario == "empty") {
        CHECK(actual.rowCount() == 0);
        podlord::ResourceMetricSummary summary;
        CHECK(actual.accumulateMetrics(-1, summary).presentation() == podlord::resourceMetricSummary({}));
        CHECK(actual.accumulateMetrics(0, summary).presentation() == podlord::resourceMetricSummary({}));
        return 0;
    }
    if (scenario == "metrics") {
        podlord::ResourceMetricSummary all, subset;
        for (int row = 0; row < actual.rowCount(); ++row) actual.accumulateMetrics(row, all);
        CHECK(all.presentation() == podlord::resourceMetricSummary(source));
        actual.accumulateMetrics(1, subset);
        CHECK(subset.presentation() == podlord::resourceMetricSummary({alpha}));
        CHECK(podlord::ResourceMetricSummary{}.add(podlord::ResourceMetricContribution{8, {}}).presentation() == podlord::resourceMetricSummary({}));
        Table cold; CHECK(cold.publish(source, "cluster"));
        podlord::ResourceMetricSummary coldSummary;
        for (int row = 0; row < cold.rowCount(); ++row) cold.accumulateMetrics(row, coldSummary);
        CHECK(coldSummary.presentation() == all.presentation());
        return 0;
    }
    if (scenario == "metrics_oldest") {
        auto newer = alpha, older = alpha;
        newer["createdAt"] = now.addSecs(-3600).toString(Qt::ISODateWithMs);
        older["createdAt"] = newer["createdAt"];
        newer["metricStale"] = true; older["metricStale"] = true;
        newer["metricAt"] = now.addSecs(-120).toString(Qt::ISODateWithMs);
        const auto oldest = now.addSecs(-240).toString(Qt::ISODateWithMs);
        older["metricAt"] = oldest; older["path"] = "/pods/b"; older["name"] = "beta"; older["uid"] = "second-uid";
        const QJsonArray samples{newer, older};
        auto pending = QtConcurrent::run(&Table::prepare, samples, QString("cluster"), -1);
        pending.waitForFinished();
        CHECK(actual.publishPrepared(pending.result(), true));
        podlord::ResourceMetricSummary summary;
        for (int row = 0; row < actual.rowCount(); ++row) actual.accumulateMetrics(row, summary);
        CHECK(summary.presentation() == podlord::resourceMetricSummary(samples));
        CHECK(summary.presentation().first().toMap()["description"].toString().contains("Oldest stale measurement: " + oldest));
        return 0;
    }
    if (scenario == "handoff") {
        QSignalSpy removed(&actual, &QAbstractItemModel::rowsRemoved), reset(&actual, &QAbstractItemModel::modelReset);
        Table::PreparedSnapshot second;
        auto next = alpha; next["path"] = "/pods/next"; next["name"] = "next";
        auto pending = QtConcurrent::run(&Table::prepare, QJsonArray{next}, QString("other-cluster"), -1);
        pending.waitForFinished(); second = pending.result();
        CHECK(actual.publishPrepared(second, true));
        CHECK(removed.size() == 1 && reset.isEmpty() && actual.rowCount() == 1 && actual.row(0)["name"] == "next");
        CHECK(actual.publishPrepared(prepared, true) && actual.rowCount() == 2);
        CHECK(actual.data(actual.index(1, 6)).toString() == "cluster");
        return 0;
    }
    if (scenario == "filter_sort") {
        podlord::ResourceFilter filtered; filtered.setSourceModel(&actual);
        CHECK(filtered.filter("=alpha") && filtered.rowCount() == 1);
        CHECK(filtered.data(filtered.index(0, 0)).toString() == "alpha");
        CHECK(filtered.filter("")); filtered.sort(0, Qt::DescendingOrder);
        CHECK(filtered.data(filtered.index(0, 0)).toString() == "beta");
        filtered.sort(7, Qt::AscendingOrder);
        CHECK(filtered.data(filtered.index(0, 0)).toString() == "alpha");
        filtered.sort(-1); CHECK(filtered.data(filtered.index(0, 0)).toString() == "beta");
        return 0;
    }
    if (scenario == "cluster") {
        CHECK(actual.publish(source, "other-cluster"));
        CHECK(actual.data(actual.index(1, 6)).toString() == "other-cluster");
        auto previousCluster = prepared;
        CHECK(!actual.retainPreparedSort(previousCluster));
        CHECK(!actual.publishPrepared(prepared));
        Table retained; CHECK(retained.publishPrepared(prepared, true));
        CHECK(retained.data(retained.index(1, 6)).toString() == "cluster");
        return 0;
    }
    if (scenario == "age") {
        CHECK(actual.data(actual.index(1, 10)).toString() == "1m");
        CHECK(actual.data(actual.index(0, 10)).toString() == "-");
        auto fresh = alpha; fresh["createdAt"] = now.addSecs(-1).toString(Qt::ISODateWithMs);
        CHECK(actual.publish({fresh, beta}, "cluster"));
        CHECK(actual.data(actual.index(1, 10)).toString() == "1s");
        return 0;
    }
    if (scenario == "values" || scenario == "repeat") {
        Table reference; CHECK(reference.publish(source, "cluster"));
        const QStringList identities{"path", "name", "kind", "namespace", "status"};
        for (int row = 0; row < actual.rowCount(); ++row)
            for (int field = 0; field < identities.size(); ++field)
                CHECK(actual.metadata(row, Qt::UserRole + field) == reference.row(row).value(identities[field]).toString());
        for (int iteration = 0; iteration < (scenario == "repeat" ? 20 : 1); ++iteration) {
            auto retained = prepared;
            CHECK(actual.retainPreparedSort(retained));
            CHECK(actual.publishPrepared(retained));
            for (int row = 0; row < actual.rowCount(); ++row)
                for (int column = 0; column < actual.columnCount(); ++column)
                    for (const int role : {int(Qt::DisplayRole), int(Qt::ToolTipRole), int(Qt::UserRole), Qt::UserRole + 1, Qt::UserRole + 2,
                        Qt::UserRole + 3, Qt::UserRole + 4, Qt::UserRole + 5, Qt::UserRole + 6, Qt::UserRole + 12})
                        CHECK(actual.data(actual.index(row, column), role) == reference.data(reference.index(row, column), role));
        }
        return 0;
    }
    std::fprintf(stderr, "Unknown scenario\n"); return 2;
}
