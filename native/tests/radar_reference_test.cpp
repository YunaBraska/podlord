#include "workspace.h"
#include "radar_island.h"
#include <QFile>
#include <QGuiApplication>
#include <QFont>
#include <QPainter>
#include <QScopeGuard>
#include <QJsonDocument>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlProperty>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QTest>
#include <QSignalSpy>
#include <QAbstractItemModelTester>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <iterator>
#include <limits>
#include <memory>

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
QMap<QString,QPointF> coordinates(QAbstractItemModel* tiles) {
    QMap<QString,QPointF> result;
    const auto names=roles(tiles);
    for (int row=0;row<tiles->rowCount();++row) {
        const auto index=tiles->index(row,0);
        result.insert(index.data(names["resourcePath"]).toString(),{index.data(names["worldX"]).toDouble(),index.data(names["worldY"]).toDouble()});
    }
    return result;
}
bool camera(const QString& scenario) {
    podlord::ResourceTable table;
    if (!table.publish(QJsonArray{QJsonObject{{"path","/api/v1/namespaces/default/pods/camera"},
        {"name","camera"},{"namespace","default"},{"kind","Pod"},{"uid","camera-uid"},{"status","Running"}}},"local")) return false;
    podlord::RadarIsland radar;
    radar.setWidth(240); radar.setHeight(200); radar.setSource(&table);
    if (!QTest::qWaitFor([&] { return radar.tiles()->rowCount()==1; },3000)) return false;
    radar.setViewPose({{"x",12},{"y",-17},{"zoom",.75}});
    const auto original=radar.viewPose();
    const auto terrain=coordinates(radar.tiles());
    const auto capture=[&] {
        QImage image(240,200,QImage::Format_ARGB32_Premultiplied); image.fill(Qt::transparent);
        QPainter painter(&image); radar.paint(&painter);
        return image;
    };
    const auto before=capture();
    QSignalSpy changed(&radar,&podlord::RadarIsland::viewPoseChanged);
    const double nan=std::numeric_limits<double>::quiet_NaN();
    const QMap<QString,QPair<QString,QVariant>> invalidFields{
        {"pose_x_text",{"x",QStringLiteral("invalid")}}, {"pose_y_text",{"y",QStringLiteral("invalid")}},
        {"pose_zoom_text",{"zoom",QStringLiteral("invalid")}}, {"pose_x_nan",{"x",nan}},
        {"pose_y_nan",{"y",nan}}, {"pose_zoom_nan",{"zoom",nan}},
        {"pose_x_extent",{"x",1e12}}, {"pose_y_extent",{"y",-1e12}},
        {"pose_zoom_low",{"zoom",.54}}, {"pose_zoom_high",{"zoom",3.41}}};
    if (invalidFields.contains(scenario)) {
        auto pose=original; const auto field=invalidFields.value(scenario); pose[field.first]=field.second;
        radar.setViewPose(pose); radar.setViewPose(pose);
    } else if (scenario=="pose_missing" || scenario=="pose_extra") {
        auto pose=original;
        if (scenario=="pose_missing") pose.remove("zoom"); else pose.insert("unknown",1);
        radar.setViewPose(pose); radar.setViewPose(pose);
    } else {
        const auto attempt=[&] {
            if (scenario=="pan_x_nan") return radar.pan(nan,1);
            if (scenario=="pan_y_nan") return radar.pan(1,nan);
            if (scenario=="zoom_factor_nan") return radar.zoomAt(nan,120,100);
            if (scenario=="zoom_factor_zero") return radar.zoomAt(0,120,100);
            if (scenario=="zoom_anchor_x_nan") return radar.zoomAt(1.18,nan,100);
            if (scenario=="zoom_anchor_y_nan") return radar.zoomAt(1.18,120,nan);
            return true;
        };
        if (attempt() || attempt()) return false;
    }
    if (radar.viewPose()!=original || !changed.isEmpty() || coordinates(radar.tiles())!=terrain || capture()!=before) {
        std::fprintf(stderr,"Invalid camera input changed the visible map: %s\n",qPrintable(scenario));
        return false;
    }
    // Invalid input must not poison later legitimate navigation or move island coordinates.
    return radar.pan(14,7) && changed.size()==1 && radar.viewPose()!=original
        && coordinates(radar.tiles())==terrain && capture()!=before;
}
bool sourceLifecycle(const QString& scenario) {
    const auto rows=[](const QString& name) {
        return QJsonArray{QJsonObject{{"path","/api/v1/namespaces/default/pods/"+name},
            {"name",name},{"namespace","default"},{"kind","Pod"},{"uid",name+"-uid"},{"status","Running"}}};
    };
    podlord::ResourceTable first,second;
    if (!first.publish(rows("first"),"local") || !second.publish(rows("second"),"local")) return false;
    auto source=std::make_unique<podlord::ResourceFilter>(); source->setSourceModel(&first);
    podlord::RadarIsland radar; radar.setWidth(2000); radar.setHeight(2000); radar.setSource(source.get());
    if (!QTest::qWaitFor([&] { return radar.tiles()->rowCount()==1; },3000)) return false;
    if (scenario=="source_destroy") {
        source.reset();
        if (!QTest::qWaitFor([&] { return radar.tiles()->rowCount()==0 && radar.currentIndex()==-1; },3000)) return false;
    } else {
        source->setSourceModel(&second);
        if (!QTest::qWaitFor([&] { return coordinates(radar.tiles()).contains("/api/v1/namespaces/default/pods/second"); },3000)) return false;
    }
    QSignalSpy detachedUpdates(&radar,&podlord::RadarIsland::currentResourceChanged);
    if (!first.publish(rows("detached"),"local")) return false;
    QTest::qWait(30);
    if (!detachedUpdates.isEmpty()) return false;
    if (scenario=="source_destroy") return radar.tiles()->rowCount()==0;
    if (!second.publish(rows("updated"),"local")) return false;
    return QTest::qWaitFor([&] { const auto visible=coordinates(radar.tiles());
        return visible.size()==1 && visible.contains("/api/v1/namespaces/default/pods/updated"); },3000)
        && radar.selectResource(0) && radar.currentIndex()==0;
}
bool hierarchy(const QString& check = {}) {
    QJsonArray rows;
    QMap<QString,QPair<QString,QString>> grouping;
    int podCount=0;
    const QStringList clusters{"alpha","beta","gamma"};
    const QList<int> counts{720,240,80};
    const auto add=[&](const QString& cluster,const QString& name,const QString& kind,const QString& nameSpace) {
        const auto path="/clusters/"+cluster+"/"+kind.toLower()+"/"+name;
        QJsonObject row{{"path",path},{"name",name},{"kind",kind},{"cluster",cluster},{"uid",path+"-uid"},{"status","Ready"}};
        if (!nameSpace.isEmpty()) row["namespace"]=nameSpace;
        rows.append(row); grouping.insert(path,{cluster,kind=="Namespace" ? name : nameSpace.isEmpty() ? QStringLiteral("Cluster resources") : nameSpace});
        if (kind=="Pod") ++podCount;
    };
    for (int clusterIndex=0;clusterIndex<clusters.size();++clusterIndex) {
        const auto& cluster=clusters[clusterIndex];
        for (int node=0;node<6;++node) add(cluster,"node-"+QString::number(node),"Node",{});
        for (int scope=0;scope<3;++scope) {
            const auto nameSpace="namespace-"+QString::number(scope);
            add(cluster,nameSpace,"Namespace",{});
            const int count=counts[clusterIndex]/3+(scope<counts[clusterIndex]%3 ? 1 : 0);
            for (int pod=0;pod<count;++pod) add(cluster,"pod-"+QString::number(scope)+"-"+QString::number(pod),"Pod",nameSpace);
        }
    }
    podlord::ResourceTable table; podlord::ResourceFilter filter; filter.setSourceModel(&table);
    if (!table.publish(rows,{})) return false;
    podlord::RadarIsland geometry; geometry.setWidth(20000); geometry.setHeight(20000); geometry.setProperty("identityScope","hierarchy"); geometry.setSource(&filter);
    QAbstractItemModelTester projections(geometry.tiles(),QAbstractItemModelTester::FailureReportingMode::Fatal);
    if (!QTest::qWaitFor([&] { return geometry.tiles()->rowCount()==rows.size(); },3000)) return false;
    const auto before=coordinates(geometry.tiles());
    if (check == "preview") {
        geometry.setWidth(2000); geometry.setHeight(2000);
        const auto image=geometry.previewImage();
        if (image.size()!=QSize(320,200) || !image.textKeys().isEmpty()) return false;
        geometry.setViewPose({{"x",80},{"y",-40},{"zoom",.55}});
        if (geometry.previewImage()!=image) return false;
        if (!filter.filter({},{{"kind","\"Pod\""}})
            || !QTest::qWaitFor([&] { return geometry.tiles()->rowCount()<=podCount; },3000)
            || geometry.previewImage()!=image) return false;
        geometry.setVisible(false);
        return geometry.previewImage().isNull();
    }
    if (check == "labels") {
        geometry.setWidth(160); geometry.setHeight(100);
        geometry.setViewPose({{"x",0},{"y",0},{"zoom",.55}}); geometry.setTextColor(Qt::white);
        QImage image(160,100,QImage::Format_ARGB32_Premultiplied); image.fill(Qt::transparent);
        QPainter painter(&image); geometry.paint(&painter); painter.end();
        int ink=0;
        for (int y=0;y<35;++y) for (int x=0;x<image.width();++x) if (image.pixelColor(x,y)==QColor(Qt::white)) ++ink;
        return ink>5;
    }
    if (check == "font") {
        const auto previous=QGuiApplication::font();
        const auto restore=qScopeGuard([&] { QGuiApplication::setFont(previous); });
        auto stretched=previous; stretched.setStretch(200); QGuiApplication::setFont(stretched);
        podlord::RadarIsland other; other.setWidth(20000); other.setHeight(20000);
        other.setProperty("identityScope","hierarchy"); other.setSource(&filter);
        return QTest::qWaitFor([&] { return other.tiles()->rowCount()==rows.size(); },3000)
            && coordinates(other.tiles())==before;
    }
    QSet<QPoint> unique;
    for (const auto& point:before) unique.insert(point.toPoint());
    if (before.size()!=rows.size() || unique.size()!=before.size()) return false;
    QMap<QString,QRectF> clusterBounds,districtBounds;
    for (auto point=before.cbegin();point!=before.cend();++point) {
        const auto group=grouping.value(point.key());
        const auto include=[&](QMap<QString,QRectF>& bounds,const QString& key) {
            const QRectF cell(point.value()-QPointF(1,1),QSizeF(2,2)); bounds[key]=bounds.contains(key) ? bounds.value(key).united(cell) : cell;
        };
        include(clusterBounds,group.first); include(districtBounds,group.first+"/"+group.second);
    }
    const auto separated=[](const QMap<QString,QRectF>& bounds) {
        for (auto left=bounds.cbegin();left!=bounds.cend();++left) for (auto right=std::next(left);right!=bounds.cend();++right)
            if (left.value().adjusted(-3,-3,3,3).intersects(right.value().adjusted(-3,-3,3,3))) return false;
        return true;
    };
    if (clusterBounds.size()!=3 || districtBounds.size()!=12 || !separated(clusterBounds)) return false;
    for (const auto& cluster:clusters) {
        QMap<QString,QRectF> local;
        for (auto district=districtBounds.cbegin();district!=districtBounds.cend();++district) if (district.key().startsWith(cluster+"/")) local.insert(district.key(),district.value());
        if (local.size()!=4 || !separated(local)) return false;
    }
    QString namespacePath;
    for (auto group=grouping.cbegin();group!=grouping.cend();++group) if (group.value().second=="namespace-1" && group.key().contains("/namespace/")) { namespacePath=group.key(); break; }
    int namespaceIndex=-1;
    for (int row=0;row<filter.rowCount();++row) if (filter.index(row,0).data(Qt::UserRole).toString()==namespacePath) { namespaceIndex=row; break; }
    if (namespaceIndex<0 || !geometry.selectResource(namespaceIndex) || geometry.currentIndex()!=namespaceIndex) return false;
    if (!filter.filter({},{{"kind","\"Pod\""}}) || !QTest::qWaitFor([&] { return geometry.tiles()->rowCount()==podCount; },3000)) return false;
    const auto filtered=coordinates(geometry.tiles());
    for (auto point=filtered.cbegin();point!=filtered.cend();++point) if (before.value(point.key())!=point.value()) return false;
    if (!filter.filter({}) || !QTest::qWaitFor([&] { return geometry.tiles()->rowCount()==rows.size(); },3000) || coordinates(geometry.tiles())!=before) return false;
    QJsonArray reversed; for (auto it=rows.end();it!=rows.begin();) reversed.append(*--it);
    if (!table.publish(reversed,{}) || !QTest::qWaitFor([&] { return coordinates(geometry.tiles())==before; },3000)) return false;
    return true;
}
bool exercise(const QStringList& args) {
    if (args.size()==2 && (args[1]=="source_rebind" || args[1]=="source_destroy")) return sourceLifecycle(args[1]);
    if (args.size()==2 && args[1].startsWith("camera_")) return camera(args[1].mid(7));
    if (args.size()==2 && args[1]=="hierarchy") return hierarchy();
    if (args.size()==2 && args[1]=="hierarchy_font") return hierarchy("font");
    if (args.size()==2 && args[1]=="hierarchy_labels") return hierarchy("labels");
    if (args.size()==2 && args[1]=="hierarchy_preview") return hierarchy("preview");
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
    geometry.setWidth(std::max(20000., document["width"].toDouble())); geometry.setHeight(std::max(20000., document["height"].toDouble()));
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
            if (!std::isfinite(x) || !std::isfinite(y) || color != QColor(block["color"].toString())) {
                std::fprintf(stderr, "Radar mismatch %s: C++ (%g,%g,%s), expected color %s\n", qPrintable(path), x, y, qPrintable(color.name()), qPrintable(block["color"].toString()));
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
    QSize populationViewport(1208,1240);
    for (const auto& point : coordinates(geometry.tiles())) {
        populationViewport.setWidth(std::max(populationViewport.width(), int(std::ceil(std::abs(point.x())*2))+64));
        populationViewport.setHeight(std::max(populationViewport.height(), int(std::ceil(std::abs(point.y())*2))+128));
    }
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
    window->resize(populationViewport);
    grid->setSource(filter);
    auto* emptyState = grid->findChild<QQuickItem*>("emptyRadar");
    if (!emptyState || !QQmlProperty(emptyState, "visible", &engine).write(false)) return setupFailure("renderer-only empty state");
    grid->setProperty("identityScope", document["sessionId"].toString());
    grid->setViewPose({{"x",0}, {"y",0}, {"zoom",1}});
    if (!QTest::qWaitFor([&] { return grid->tiles()->rowCount() >= expected.size() && window->isExposed(); }, 3000)) return setupFailure("rendered cache population");
    window->resize(1208,1240);
    if (!QTest::qWaitFor([&] { return grid->width()==1200 && grid->height()>0 && grid->height()<=1240; }, 3000)) return setupFailure("capture viewport restoration");
    window->setHeight(window->height() + qRound(1200 - grid->height()));
    if (!QTest::qWaitFor([&] { return grid->size() == QSizeF(1200,1200); }, 3000)) {
        std::fprintf(stderr, "Actual viewport: %g x %g\n", grid->width(), grid->height());
        return setupFailure("exact viewport dimensions");
    }
    window->update(); QTest::qWait(80);
    const auto rendered=coordinates(grid->tiles());
    if (rendered.isEmpty()) return setupFailure("restored viewport contains no rendered resources");
    const auto cached=coordinates(geometry.tiles());
    const auto safeViewport=QRectF(QPointF{},grid->size()).adjusted(4,4,-4,-4);
    const auto renderedRoles=roles(grid->tiles());
    QMap<QString,QColor> renderedColors;
    for (int row=0;row<grid->tiles()->rowCount();++row) {
        const auto tile=grid->tiles()->index(row,0);
        renderedColors.insert(tile.data(renderedRoles["resourcePath"]).toString(),tile.data(renderedRoles["terrainColor"]).value<QColor>());
    }
    const auto frame = window->grabWindow();
    if (frame.isNull() || frame.devicePixelRatio() != 1) return setupFailure("original-pixel window frame");
    const auto origin = grid->mapToScene({}).toPoint();
    const auto image = frame.copy(QRect(origin, QSize(1200,1200)));
    window->close();
    int checkedPixels=0;
    for (auto resource=rendered.cbegin();resource!=rendered.cend();++resource) {
        const auto original=cached.constFind(resource.key());
        if (original==cached.cend() || original.value()!=resource.value()) {
            std::fprintf(stderr,"Rendered resource differs from cached geometry: %s (%g,%g)\n",qPrintable(resource.key()),resource.value().x(),resource.value().y());
            return setupFailure("rendered resource is missing from or displaced within cached geometry");
        }
        const auto center=QPointF(600,600)+resource.value();
        // Projection includes overscan tiles whose centers are outside the clipped image.
        if (!safeViewport.contains(center)) continue;
        const auto point=QPoint(qRound(center.x()),qRound(center.y()));
        if (!image.rect().contains(point) || image.pixelColor(point)!=renderedColors.value(resource.key())) {
            std::fprintf(stderr,"Rendered pixel mismatch: %s pixel=(%d,%d) actual=%s expected=%s\n",qPrintable(resource.key()),point.x(),point.y(),
                qPrintable(image.rect().contains(point) ? image.pixelColor(point).name(QColor::HexArgb) : QStringLiteral("outside")),qPrintable(renderedColors.value(resource.key()).name(QColor::HexArgb)));
            return setupFailure("rendered resources do not match cached geometry");
        }
        ++checkedPixels;
    }
    for (auto resource=cached.cbegin();resource!=cached.cend();++resource)
        if (safeViewport.contains(QPointF(600,600)+resource.value()) && !rendered.contains(resource.key())) {
            std::fprintf(stderr,"Visible cached resource missing from projection: %s (%g,%g)\n",qPrintable(resource.key()),resource.value().x(),resource.value().y());
            return setupFailure("viewport projection omitted a safely visible cached resource");
        }
    if (checkedPixels==0) return setupFailure("viewport contained no safely visible resource pixels to verify");
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
