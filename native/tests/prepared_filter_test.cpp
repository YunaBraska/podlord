#include "workspace.h"
#include <QCoreApplication>
#include <QIdentityProxyModel>
#include <QtConcurrent/QtConcurrentRun>
#include <cstdio>

#define CHECK(condition) do { if (!(condition)) { std::fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition); return 1; } } while (false)

int main(int argc, char** argv) {
    QCoreApplication application(argc, argv);
    const auto now = QDateTime::currentDateTimeUtc();
    const QJsonObject complete{{"cpu", true}, {"memory", true}, {"storage", true}};
    QJsonArray source{
        QJsonObject{{"path", "/pods/z"}, {"name", "Alpha-12"}, {"kind", "Pod"}, {"namespace", "blue"},
            {"status", "Running"}, {"cpu", .25}, {"memory", 8192}, {"restarts", 12},
            {"createdAt", now.addSecs(-3630).toString(Qt::ISODateWithMs)}, {"metricComplete", complete}, {"activity", true}},
        QJsonObject{{"path", "/pods/a"}, {"name", "beta-2"}, {"kind", "Pod"}, {"namespace", "red"},
            {"status", "Error"}, {"cpu", 2}, {"storage", 2147483648.0}, {"restarts", 2},
            {"createdAt", now.addSecs(3630).toString(Qt::ISODateWithMs)}, {"metricComplete", complete},
            {"metricStale", true}, {"problemSeverity", 2}, {"activity", true}},
        QJsonObject{{"path", "/configmaps/b"}, {"name", "literal [a].*"}, {"kind", "ConfigMap"}, {"namespace", "blue"},
            {"status", "Ready"}, {"createdAt", "invalid"}},
        QJsonObject{{"path", "/malformed"}, {"name", 42}, {"kind", false}, {"namespace", QJsonArray{"blue"}}, {"status", QJsonValue::Null}},
        QJsonObject{{"path", "/pods/c"}, {"name", "ALPHA-12"}, {"kind", "Pod"}, {"namespace", "blue"},
            {"status", "Pending"}, {"cpu", 0}, {"ready", "1/2"}, {"readyCount", 1}, {"containerCount", 2},
            {"createdAt", now.addSecs(-330).toString(Qt::ISODateWithMs)}, {"metricComplete", complete}, {"problemSeverity", 1}}
    };
    struct FilterCase {
        const char* label;
        QString expression;
        QMap<QString, QString> fields = {};
        QString mode = {};
    };
    const QList<FilterCase> cases{
        {"empty", ""}, {"case insensitive", "ALPHA"}, {"quoted", "\"Alpha-12\""},
        {"prefix", "~ALPHA"}, {"suffix", "2~"}, {"regex", "/^Alpha-12$/"},
        {"numeric metadata display", "42"}, {"numeric display comparison", ">5"},
        {"literal punctuation", "\"literal [a].*\""}, {"no match", "absent-value"},
        {"name and namespace", "", {{"name", "~alpha"}, {"namespace", "\"blue\""}}},
        {"status", "", {{"status", "Running Error"}}},
        {"quantity", "", {{"cpu", ">=500m"}}},
        {"quantity display", "", {{"cpu", "/CPU/"}}},
        {"memory", "", {{"memory", ">=8Ki"}}},
        {"age", "", {{"createdAt", ">30m"}}},
        {"cluster replacement", "", {{"cluster", "\"replacement\""}}},
        {"problems", "", {}, "problems"}, {"activity", "", {}, "activity"},
        {"filtered problems", "", {{"namespace", "blue"}}, "problems"},
        {"invalid quote", "\"unfinished"}, {"invalid regex", "/[/"},
        {"invalid quantity", "", {{"cpu", "-3m"}}},
        {"unsupported field", "", {{"missing-field", "value"}}}
    };
    podlord::ResourceTable preparedTable, ordinaryTable;
    // The standard Qt proxy keeps the oracle on the ordinary public data path.
    QIdentityProxyModel ordinaryProxy;
    ordinaryProxy.setSourceModel(&ordinaryTable);
    podlord::ResourceFilter actual, expected;
    actual.setSourceModel(&preparedTable);
    expected.setSourceModel(&ordinaryProxy);
    QString cluster = "initial";
    for (int publication = 0; publication < 3; ++publication) {
        if (publication == 1) {
            auto changed = source[0].toObject(); changed["cpu"] = .75; changed["name"] = "replacement-Alpha";
            source[0] = changed; cluster = "replacement";
        }
        CHECK(ordinaryTable.publish(source, cluster));
        if (publication == 1) CHECK(preparedTable.publish(source, cluster));
        else {
            auto future = QtConcurrent::run(&podlord::ResourceTable::prepare, source, cluster, 0);
            future.waitForFinished();
            CHECK(future.resultCount() == 1 && preparedTable.publishPrepared(future.result(), true));
        }
        for (int comparison = 0; comparison < 3; ++comparison) {
            actual.setSortCaseSensitivity(comparison == 1 ? Qt::CaseSensitive : Qt::CaseInsensitive);
            expected.setSortCaseSensitivity(actual.sortCaseSensitivity());
            actual.setSortLocaleAware(comparison == 2); expected.setSortLocaleAware(comparison == 2);
            for (const auto& test : cases) {
                const bool accepted = actual.filter(test.expression, test.fields, test.mode);
                const bool referenceAccepted = expected.filter(test.expression, test.fields, test.mode);
                if (accepted != referenceAccepted || actual.error() != expected.error())
                    std::fprintf(stderr, "Filter mismatch: %s, publication %d, comparison %d\n", test.label, publication, comparison);
                CHECK(accepted == referenceAccepted && actual.error() == expected.error());
                for (int column : {-1, 0, 3, 7, 10, 11}) {
                    for (auto order : {Qt::AscendingOrder, Qt::DescendingOrder}) {
                        actual.sort(column, order); expected.sort(column, order);
                        CHECK(actual.rowCount() == expected.rowCount());
                        for (int row = 0; row < actual.rowCount(); ++row) {
                            if (actual.data(actual.index(row, 0), Qt::UserRole) != expected.data(expected.index(row, 0), Qt::UserRole))
                                std::fprintf(stderr, "Order mismatch: %s, publication %d, column %d, order %d, row %d\n",
                                    test.label, publication, column, int(order), row);
                            CHECK(actual.data(actual.index(row, 0), Qt::UserRole) == expected.data(expected.index(row, 0), Qt::UserRole));
                            for (int cell = 0; cell < actual.columnCount(); ++cell)
                                CHECK(actual.data(actual.index(row, cell)) == expected.data(expected.index(row, cell)));
                        }
                    }
                }
            }
        }
    }
    return 0;
}
