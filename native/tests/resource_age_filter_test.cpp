#include "workspace.h"
#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <cstdio>

namespace {
bool run(const QString& scenario) {
    using namespace podlord;
    ResourceTable rows;
    const auto now = QDateTime::currentDateTimeUtc();
    rows.publish(QJsonArray{
        QJsonObject{{"path", "a"}, {"name", "alpha"}, {"uid", "uid-alpha"}, {"createdAt", now.addSecs(-90).toString(Qt::ISODateWithMs)}},
        QJsonObject{{"path", "b"}, {"name", "bravo"}, {"uid", "uid-bravo"}, {"createdAt", now.addSecs(-7200).toString(Qt::ISODateWithMs)}},
        QJsonObject{{"path", "c"}, {"name", "charlie"}},
        QJsonObject{{"path", "d"}, {"name", "future"}, {"createdAt", now.addSecs(7200).toString(Qt::ISODateWithMs)}},
        QJsonObject{{"path", "e"}, {"name", "malformed"}, {"createdAt", "invalid"}}});
    ResourceFilter filter; filter.setSourceModel(&rows);
    if (scenario.startsWith("layout_")) {
        QTemporaryDir temporary; if (!temporary.isValid()) return false;
        QStringList ids;
        for (int index = 0; index < rows.columnCount(); ++index) ids.append(rows.headerData(index, Qt::Horizontal, Qt::UserRole).toString());
        if (!ids.contains("uid")) return false;
        auto previous = ids; previous.removeAll("uid");
        const TableSchemas oldSchemas{{"resource", previous}};
        auto retained = defaultTableLayouts(oldSchemas).value("resource");
        retained.swapItemsAt(0, 2); retained[0].pinned = true; retained[1].visible = false; retained[2].width = 431;
        QJsonArray encoded;
        for (const auto& column : retained) encoded.append(QJsonObject{{"id", column.id}, {"visible", column.visible}, {"pinned", column.pinned}, {"width", column.width}});
        const auto path = temporary.filePath("table-layouts.json");
        QFile file(path);
        auto data = QJsonDocument(QJsonObject{{"version", 2}, {"layouts", QJsonObject{{"resource", encoded}}}}).toJson();
        if (scenario == "layout_invalid") {
            auto invalid = encoded[0].toObject(); invalid["id"] = "unknown"; encoded[0] = invalid;
            data = QJsonDocument(QJsonObject{{"version", 2}, {"layouts", QJsonObject{{"resource", encoded}}}}).toJson();
        }
        if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size()) return false; file.close();
        const TableLayoutStore store(temporary.path(), {{"resource", ids}});
        const auto loaded = store.load();
        if (scenario == "layout_invalid") return std::holds_alternative<Failure>(loaded) && file.open(QIODevice::ReadOnly) && file.readAll() == data;
        const auto* layouts = std::get_if<TableLayouts>(&loaded); if (!layouts) return false;
        const auto actual = layouts->value("resource");
        auto expected = retained; expected.append({"uid", false, false, 280});
        if (actual != expected || !file.open(QIODevice::ReadOnly) || file.readAll() != data) return false; file.close();
        if (scenario == "layout_read") return true;
        if (scenario != "layout_save") return false;
        QJsonArray desired;
        for (auto column : actual) { if (column.id == "uid") column.visible = true; desired.append(QJsonObject{{"id", column.id}, {"visible", column.visible}, {"pinned", column.pinned}, {"width", column.width}}); }
        const auto saved = store.save("resource", desired, actual);
        const auto restored = store.load();
        return std::holds_alternative<TableLayouts>(saved) && std::holds_alternative<TableLayouts>(restored)
            && std::get<TableLayouts>(restored) == std::get<TableLayouts>(saved);
    }
    const QMap<QString, QPair<QString, QString>> cases{
        {"seconds", {">=90 <91", "a"}}, {"bare", {">=90 <120", "a"}},
        {"minutes", {">=1m <2m", "a"}}, {"hours", {">=1h <3h", "b"}},
        {"days", {"<1d", "ab"}}, {"weeks", {"<1w", "ab"}},
        {"compound", {"=1m30s", "a"}}, {"alias", {"=>90secs =<2mins", "a"}},
        {"millis", {">=90000ms <91000ms", "a"}}, {"hour_alias", {">=1hr <3hrs", "b"}},
        {"day_alias", {"<1day <2days", "ab"}}, {"week_alias", {"<1week <2weeks", "ab"}},
        {"spaced", {">= 1m < 2m", "a"}}, {"spaced_unit", {">=1 min <2 mins", "a"}},
        {"conflict", {">=2h <1h", ""}}, {"exact_alternatives", {"=90s =7200s", "ab"}},
        {"mixed", {">=60 =90 =7200", "ab"}}, {"text", {"\"1m\"", "a"}},
        {"bare_alternatives", {"90 7200", "ab"}},
        {"regex", {"/^2h$/", "b"}}, {"prefix", {"~1", "a"}},
        {"suffix", {"h~", "b"}}, {"missing", {"\"-\"", "cde"}},
        {"empty", {"", "abcde"}}, {"blank", {"  ", "abcde"}},
        {"zero", {"=now", ""}}, {"invalid_unit", {">1year", ""}},
        {"negative", {">-1s", ""}}, {"fraction", {">0.5h", ""}},
        {"missing_operand", {">=", ""}}, {"bad_operator", {">>=1m", ""}},
        {"overflow", {">999999999999999999999999999999999999999999s", ""}},
        {"trailing", {">1hgarbage", ""}}, {"invalid_regex", {"/[/", ""}},
        {"pattern_operand", {"> /2h/", ""}},
        {"uid_contains", {"uid-al", "a"}}, {"uid_exact", {"\"UID-BRAVO\"", "b"}},
        {"uid_regex", {"/^uid-(alpha|bravo)$/", "ab"}}, {"uid_missing", {"nonexistent", ""}}};
    if (scenario == "clock_update") {
        if (!filter.filter("", {{"createdAt", "<2m"}}) || filter.rowCount() != 1) return false;
        rows.publish(QJsonArray{QJsonObject{{"path", "a"}, {"createdAt", now.addSecs(-150).toString(Qt::ISODateWithMs)}}});
        return filter.rowCount() == 0 && filter.filter("", {{"createdAt", "<2m"}}) && filter.rowCount() == 0;
    }
    if (scenario == "uid_global") return filter.filter("uid-bravo") && filter.rowCount() == 1 && filter.data(filter.index(0, 0), Qt::UserRole) == "b";
    if (scenario == "uid_sort") {
        int uid = -1; for (int index = 0; index < rows.columnCount(); ++index) if (rows.headerData(index, Qt::Horizontal, Qt::UserRole) == "uid") uid = index;
        if (uid < 0) return false; filter.sort(uid, Qt::DescendingOrder);
        return filter.data(filter.index(0, uid)).toString() == "uid-bravo" && filter.data(filter.index(1, uid)).toString() == "uid-alpha";
    }
    if (!cases.contains(scenario)) return false;
    const auto [expression, expected] = cases.value(scenario);
    const bool invalid = QStringList{"invalid_unit", "negative", "fraction", "missing_operand", "bad_operator", "overflow", "trailing", "invalid_regex", "pattern_operand"}.contains(scenario);
    const auto field = scenario.startsWith("uid_") ? QString("uid") : QString("createdAt");
    const bool accepted = filter.filter("", {{field, expression}});
    QString actual; for (int row = 0; row < filter.rowCount(); ++row) actual += filter.data(filter.index(row, 0), Qt::UserRole).toString();
    if (accepted == !invalid && actual == expected && (invalid ? !filter.error().isEmpty() : filter.error().isEmpty())) return true;
    std::fprintf(stderr, "%s accepted=%d expected=%d rows=%s expected=%s error=%s\n", qPrintable(scenario), accepted, !invalid, qPrintable(actual), qPrintable(expected), qPrintable(filter.error()));
    return false;
}
}
int main(int argc, char** argv) {
    const QCoreApplication application(argc, argv);
    return argc == 2 && run(QString::fromLocal8Bit(argv[1])) ? 0 : 1;
}
