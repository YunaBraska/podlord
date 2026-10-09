#include "workspace.h"
#include "radar_island.h"
#include <QFile>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlProperty>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QTest>
#include <cstdio>

namespace {
bool setupFailure(const char* message) {
    std::fprintf(stderr, "Radar reference setup failed: %s\n", message);
    return false;
}
QHash<QByteArray, int> roles(QAbstractItemModel* model) {
    QHash<QByteArray, int> result;
    const auto names = model->roleNames();
    for (auto it = names.cbegin(); it != names.cend(); ++it) result.insert(it.value(), it.key());
    return result;
}
bool exercise(const QStringList& args) {
    if (args.size() < 3 || args.size() > 4) return false;
    QFile file(args[2]); if (!file.open(QIODevice::ReadOnly)) return false;
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(file.readAll(), &error).object();
    if (error.error != QJsonParseError::NoError || !document.contains("rows") || !document.contains("blocks")) return false;
    auto rows = document["rows"].toArray();
    QHash<QString, QJsonObject> expected;
    for (const auto value : document["blocks"].toArray()) {
        const auto block = value.toObject();
        if (!block["decoration"].toBool()) expected.insert(block["path"].toString(), block);
    }
    if (expected.isEmpty()) return false;
    podlord::ResourceTable table;
    podlord::ResourceFilter proxy;
    proxy.setSourceModel(&table);
    auto* filter = &proxy;
    auto* cache = &table;
    if (!cache->publish(rows, document["cluster"].toString())) return false;
    podlord::RadarIsland geometry;
    geometry.setWidth(document["width"].toDouble()); geometry.setHeight(document["height"].toDouble());
    geometry.setProperty("identityScope", document["sessionId"].toString());
    geometry.setSource(filter);
    if (!QTest::qWaitFor([&] { return geometry.tiles()->rowCount() >= expected.size(); }, 3000)) return false;
    const auto check = [&](const QHash<QString, QJsonObject>& wanted) {
        const auto names = roles(geometry.tiles()); int matched = 0;
        if (!names.contains("resourcePath") || !names.contains("worldX") || !names.contains("worldY") || !names.contains("terrainColor")) return false;
        for (int row = 0; row < geometry.tiles()->rowCount(); ++row) {
            const auto index = geometry.tiles()->index(row, 0);
            const auto path = index.data(names["resourcePath"]).toString();
            if (!wanted.contains(path)) continue;
            const auto block = wanted[path];
            const auto x = index.data(names["worldX"]).toDouble(), y = index.data(names["worldY"]).toDouble();
            const auto color = index.data(names["terrainColor"]).value<QColor>();
            if (x != block["x"].toDouble() || y != block["y"].toDouble() || color != QColor(block["color"].toString())) {
                std::fprintf(stderr, "Radar mismatch %s: C++ (%g,%g,%s), C# (%g,%g,%s)\n", qPrintable(path), x, y, qPrintable(color.name()), block["x"].toDouble(), block["y"].toDouble(), qPrintable(block["color"].toString()));
                return false;
            }
            ++matched;
        }
        if (matched != wanted.size()) std::fprintf(stderr, "Matched %d of %lld reference resources across %d tiles.\n", matched, static_cast<long long>(wanted.size()), geometry.tiles()->rowCount());
        return matched == wanted.size();
    };
    if (args[1] == "reference") return check(expected);
    if (args[1] == "selection_sort" || args[1] == "selection_filter" || args[1] == "selection_remove") {
        const int selected = filter->rowCount()/2;
        const auto path = filter->index(selected, 0).data(Qt::UserRole).toString();
        if (!geometry.selectResource(selected)) return false;
        if (args[1] == "selection_sort") {
            filter->sort(0, Qt::DescendingOrder);
            const int actual = geometry.currentIndex();
            if (actual < 0 || filter->index(actual, 0).data(Qt::UserRole).toString() != path) return false;
            QTest::qWait(30);
            const auto names = roles(geometry.tiles());
            for (int row = 0; row < geometry.tiles()->rowCount(); ++row) {
                const auto tile = geometry.tiles()->index(row, 0);
                const int index = tile.data(names["resourceIndex"]).toInt();
                if (filter->index(index, 0).data(Qt::UserRole) != tile.data(names["resourcePath"])) return false;
            }
            return check(expected);
        }
        if (args[1] == "selection_filter") {
            if (!filter->filter("\"no-reference-resource-has-this-name\"") || geometry.currentIndex() != -1) return false;
            if (!filter->filter({})) return false;
            QTest::qWait(30);
            const int actual = geometry.currentIndex();
            return actual >= 0 && filter->index(actual, 0).data(Qt::UserRole).toString() == path && check(expected);
        }
        QJsonArray retained;
        for (const auto value : rows) if (value.toObject()["path"].toString() != path) retained.append(value);
        if (!cache->publish(retained, document["cluster"].toString()) || geometry.currentIndex() != -1) return false;
        QTest::qWait(30);
        const auto names = roles(geometry.tiles());
        for (int row = 0; row < geometry.tiles()->rowCount(); ++row)
            if (geometry.tiles()->index(row, 0).data(names["resourcePath"]).toString() == path) return false;
        return true;
    }
    if (args[1] == "cached_scope_replay" || args[1] == "cached_scope_hit") {
        const auto scope = document["sessionId"].toString();
        const int visited = args[1] == "cached_scope_hit" ? 2 : 4;
        for (int index = 0; index < visited; ++index) {
            geometry.setProperty("identityScope", scope + "-revisit-" + QString::number(index));
            QTest::qWait(30);
        }
        geometry.setProperty("identityScope", scope);
        QTest::qWait(30);
        return check(expected);
    }
    if (args[1] == "metric_update") {
        QJsonArray updated;
        for (const auto value : rows) {
            auto row = value.toObject(); row["cpu"] = 42.0; row["metricStale"] = false;
            updated.append(row);
        }
        if (!cache->publish(updated, document["cluster"].toString())) return false;
        QTest::qWait(30);
        return check(expected);
    }
    if (args[1] == "cached_uid_recreation") {
        const auto coordinates = [](QAbstractItemModel* tiles) {
            QMap<QString, QPointF> result;
            const auto names = roles(tiles);
            for (int row = 0; row < tiles->rowCount(); ++row) {
                const auto index = tiles->index(row, 0);
                result.insert(index.data(names["resourcePath"]).toString(),
                    {index.data(names["worldX"]).toDouble(), index.data(names["worldY"]).toDouble()});
            }
            return result;
        };
        const auto before = coordinates(geometry.tiles());
        const auto scope = document["sessionId"].toString();
        geometry.setProperty("identityScope", scope + "-other");
        QTest::qWait(30);
        QJsonArray recreated;
        for (const auto value : rows) {
            auto row = value.toObject();
            row["uid"] = row["uid"].toString() + "-recreated";
            recreated.append(row);
        }
        if (!cache->publish(recreated, document["cluster"].toString())) return false;
        geometry.setProperty("identityScope", scope);
        podlord::RadarIsland fresh;
        fresh.setWidth(geometry.width()); fresh.setHeight(geometry.height());
        fresh.setProperty("identityScope", scope); fresh.setSource(filter);
        return QTest::qWaitFor([&] {
            const auto actual = coordinates(geometry.tiles()), cold = coordinates(fresh.tiles());
            return !cold.isEmpty() && actual == cold && actual != before;
        }, 3000);
    }
    if (args[1] == "reordered") {
        QJsonArray reverse;
        for (auto it = rows.end(); it != rows.begin();) reverse.append(*--it);
        cache->publish(reverse, document["cluster"].toString());
        filter->sort(0, Qt::DescendingOrder); QTest::qWait(30);
        return check(expected);
    }
    if (args[1] == "filtered") {
        QHash<QString, QJsonObject> pods;
        for (auto it = expected.cbegin(); it != expected.cend(); ++it) if (it.value()["kind"] == "Pod") pods.insert(it.key(), it.value());
        filter->filter({}, {{"kind", "\"Pod\""}}); QTest::qWait(30);
        if (pods.isEmpty() || geometry.tiles()->rowCount() != pods.size() || !check(pods)) return false;
        filter->filter({}); QTest::qWait(30);
        return check(expected);
    }
    if (args[1] == "namespaces") {
        const auto names = roles(geometry.tiles()); QSet<QString> visible;
        for (int row = 0; row < geometry.tiles()->rowCount(); ++row) visible.insert(geometry.tiles()->index(row, 0).data(names["resourcePath"]).toString());
        int count = 0;
        for (const auto value : rows) {
            const auto row = value.toObject();
            if (row["kind"] == "Namespace") {
                ++count;
                if (!visible.contains(row["path"].toString())) return false;
                int index = -1;
                for (int candidate = 0; candidate < filter->rowCount(); ++candidate)
                    if (filter->index(candidate,0).data(Qt::UserRole).toString() == row["path"].toString()) { index = candidate; break; }
                if (index < 0 || !geometry.selectResource(index) || geometry.currentIndex() != index) return false;
            }
        }
        return count > 0 && check(expected);
    }
    if (args[1] == "pose") {
        geometry.setViewPose({{"x",12}, {"y",-17}, {"zoom",0.75}});
        if (!check(expected) || !geometry.pan(14,-7) || !check(expected)) return false;
        if (!geometry.resetView() || geometry.viewPose().value("zoom").toDouble() != 1) return false;
        return check(expected);
    }
    const bool renderCheck = args[1] == "render-check";
    if ((args[1] != "render" && !renderCheck) || args.size() != (renderCheck ? 3 : 4) || !check(expected)) return false;
    QTemporaryDir profile; if (!profile.isValid()) return false;
    podlord::Workspace workspace(profile.filePath("profile"));
    if (!QTest::qWaitFor([&] { return !workspace.property("busy").toBool(); }, 3000)) return setupFailure("profile initialization");
    if (!workspace.saveRadarWater(false, 0)) return setupFailure("water preference admission");
    if (!QTest::qWaitFor([&] { return !workspace.property("busy").toBool() && !workspace.radarWaterEnabled(); }, 3000)) return setupFailure("water preference completion");
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("workspace", &workspace);
    engine.loadData(R"qml(import QtQuick
import QtQuick.Window
import "qrc:/podlord/"
Window {
    width: 1208; height: 1240; visible: true; color: "#12212B"
    ResourceRadar { anchors.fill: parent; compact: true; background: Rectangle { color: "#12212B" } }
})qml", QUrl("qrc:/podlord/radar-reference.qml"));
    if (engine.rootObjects().size() != 1) return setupFailure("QML window creation");
    auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
    auto* grid = window ? window->findChild<podlord::RadarIsland*>("resourceRadar") : nullptr;
    if (!grid) return setupFailure("radar control lookup");
    grid->setSource(filter);
    auto* emptyState = grid->findChild<QQuickItem*>("emptyRadar");
    if (!emptyState || !QQmlProperty(emptyState, "visible", &engine).write(false)) return setupFailure("renderer-only empty state");
    grid->setProperty("identityScope", document["sessionId"].toString());
    grid->setViewPose({{"x",0}, {"y",0}, {"zoom",1}});
    if (!QTest::qWaitFor([&] { return grid->tiles()->rowCount() >= expected.size() && window->isExposed(); }, 3000)) return setupFailure("rendered cache population");
    window->setHeight(window->height() + qRound(1200 - grid->height()));
    if (!QTest::qWaitFor([&] { return grid->size() == QSizeF(1200,1200); }, 3000)) {
        std::fprintf(stderr, "Actual viewport: %g x %g\n", grid->width(), grid->height());
        return setupFailure("exact viewport dimensions");
    }
    window->update(); QTest::qWait(80);
    const auto frame = window->grabWindow();
    if (frame.isNull() || frame.devicePixelRatio() != 1) return setupFailure("original-pixel window frame");
    const auto origin = grid->mapToScene({}).toPoint();
    const auto image = frame.copy(QRect(origin, QSize(1200,1200)));
    window->close();
    for (const auto value : document["blocks"].toArray()) {
        const auto block = value.toObject();
        const auto point = QPoint(qRound(600 + block["x"].toDouble()), qRound(600 + block["y"].toDouble()));
        if (!image.rect().contains(point) || image.pixelColor(point) != QColor(block["color"].toString()))
            return setupFailure("rendered terrain/decorations do not match reference centers");
    }
    if (renderCheck) return true;
    return image.save(args[3]);
}
}
int main(int argc, char** argv) {
    QGuiApplication app(argc, argv);
    const bool passed = exercise(app.arguments());
    std::fprintf(passed ? stdout : stderr, "Radar reference scenario %s\n", passed ? "passed" : "failed");
    return passed ? 0 : 1;
}
