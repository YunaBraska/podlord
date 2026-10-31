#include "radar_island.h"
#include "workspace.h"
#include "kind_glyph.h"
#include <QFontMetricsF>
#include <QPainter>
#include <QQuickWindow>
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <optional>
#include <numbers>
#include <tuple>

namespace podlord {
namespace {
constexpr int ResourceIndex = Qt::UserRole + 20, WorldX = ResourceIndex + 1, WorldY = ResourceIndex + 2;
constexpr int TerrainColor = ResourceIndex + 3;
constexpr int AnimationScale = RadarTiles::animationScaleRole;
constexpr double step = 7;
constexpr double minimumZoom = .01, maximumZoom = 3.4;
double oscillation(qint64 elapsed, qint64 halfPeriod, double low) {
    const double phase = double(std::max<qint64>(0, elapsed) % (halfPeriod * 2)) / halfPeriod;
    return 1 - (1 - low) * (phase <= 1 ? phase : 2 - phase);
}
quint32 hash(const QString& text) {
    quint32 result = 17;
    for (const auto ch : text) result = result * 31 + ch.unicode();
    return result & 0x7fffffffu;
}
double range(const QString& key, double low, double high) { return low + (hash(key) % 10000) / 9999. * (high-low); }
QPoint cell(const QPointF& point) { return {int(std::nearbyint(point.x()/step)), int(std::nearbyint(point.y()/step))}; }
QPoint bucket(const QPoint& point) { return {int(std::floor(point.x()/16.)), int(std::floor(point.y()/16.))}; }
QPointF world(const QPoint& point) { return QPointF(point)*step; }
int rank(const QString& kind) {
    static const QHash<QString,int> ranks{{"Cluster",0},{"Namespace",1},{"Node",2},{"PersistentVolume",3},{"StorageClass",3},
        {"PersistentVolumeClaim",4},{"ConfigMap",5},{"Secret",5},{"ServiceAccount",5},{"Deployment",6},{"StatefulSet",6},
        {"DaemonSet",6},{"Job",6},{"CronJob",6},{"ReplicaSet",7},{"Pod",8},{"Service",9},{"EndpointSlice",9},
        {"Ingress",10},{"Gateway",10},{"HTTPRoute",10},{"NetworkPolicy",10},{"Event",11}};
    return ranks.value(kind,12);
}
int ring(const QString& kind, int fallback = 4) {
    static const QHash<QString,int> rings{{"Cluster",0},{"Node",1},{"PersistentVolume",1},{"StorageClass",1},
        {"CustomResourceDefinition",1},{"GatewayClass",1},{"Namespace",2},{"ConfigMap",3},{"Secret",3},
        {"ServiceAccount",3},{"PersistentVolumeClaim",3},{"Pod",5},{"Service",5},{"EndpointSlice",5},
        {"Ingress",6},{"Gateway",6},{"HTTPRoute",6},{"GRPCRoute",6},{"NetworkPolicy",6},{"Event",7},
        {"Deployment",4},{"StatefulSet",4},{"DaemonSet",4},{"Job",4},{"CronJob",4},{"ReplicaSet",4}};
    return rings.value(kind,fallback);
}
QColor terrainColor(const QString& kind) {
    static const QList<QColor> colors{{"#6B7378"},{"#6B7378"},{"#2E5941"},{"#4E6A43"},{"#665A3F"},{"#7D7048"},{"#286473"},{"#1B4357"}};
    return colors[ring(kind,3)];
}
QPoint place(const QPointF& target, QSet<QPoint>& occupied, QHash<QPoint,double>& frontier, const QString& key, const std::array<double,3>& coast) {
    const auto coastalDistance=[&](const QPoint& point) {
        const double angle=std::atan2(double(point.y()),double(point.x()));
        const double shore=1+.28*std::cos(2*angle+coast[0])+.18*std::sin(3*angle+coast[1])+.11*std::cos(5*angle+coast[2]);
        return (double(point.x())*point.x()+double(point.y())*point.y())/(shore*shore);
    };
    const auto offer=[&](const QPoint& point) {
        if (!occupied.contains(point) && !frontier.contains(point)) frontier.insert(point,coastalDistance(point));
    };
    if (frontier.isEmpty())
        for (const auto& delta:{QPoint(-1,0),QPoint(1,0),QPoint(0,-1),QPoint(0,1)}) offer(delta);
    auto center=cell(target);
    // Area bounds growth, not a shared circular outline. The fixed identity
    // profile guides only real frontier cells and never paints synthetic land.
    const auto radius=std::ceil(std::sqrt((occupied.size()+1)/std::numbers::pi)*1.15+2);
    const auto distance=std::sqrt(coastalDistance(center));
    if (distance>radius) center={qRound(center.x()*radius/distance),qRound(center.y()*radius/distance)};
    std::optional<QPoint> best;
    std::tuple<bool,qint64,quint32,int,int> score;
    for (auto edge=frontier.cbegin();edge!=frontier.cend();++edge) {
        const auto candidate=edge.key();
        const auto delta=candidate-center;
        const bool outside=edge.value()>radius*radius;
        const qint64 squared=qint64(delta.x())*delta.x()+qint64(delta.y())*delta.y();
        if (best && std::pair{outside,squared}>std::pair{std::get<0>(score),std::get<1>(score)}) continue;
        const auto next=std::tuple{outside,squared,hash(key+":"+QString::number(candidate.x())+":"+QString::number(candidate.y())),candidate.x(),candidate.y()};
        if (!best || next<score) { best=candidate; score=next; }
    }
    occupied.insert(*best); frontier.remove(*best);
    for (const auto& delta:{QPoint(-1,0),QPoint(1,0),QPoint(0,-1),QPoint(0,1)})
        offer(*best+delta);
    return *best;
}
template<class F> void inBuckets(const QRectF& rect, F consume) {
    if (rect.isEmpty()) return;
    const auto first=bucket(cell(rect.topLeft()-QPointF(step,step))), last=bucket(cell(rect.bottomRight()+QPointF(step,step)));
    for (int x=first.x();x<=last.x();++x) for (int y=first.y();y<=last.y();++y) consume(QPoint(x,y));
}
}
int RadarTiles::rowCount(const QModelIndex& parent) const { return parent.isValid() ? 0 : int(entries_.size()); }
QHash<int,QByteArray> RadarTiles::roleNames() const {
    static const QHash<int,QByteArray> names{{Qt::UserRole,"resourcePath"},{Qt::UserRole+1,"resourceName"},{Qt::UserRole+2,"resourceKind"},
        {Qt::UserRole+3,"resourceNamespace"},{Qt::UserRole+4,"resourceStatus"},
        {Qt::UserRole+7,"resourceMetrics"},{Qt::UserRole+9,"resourceHealth"},{ResourceIndex,"resourceIndex"},{WorldX,"worldX"},{WorldY,"worldY"},{TerrainColor,"terrainColor"},{AnimationScale,"animationScale"}};
    return names;
}
QVariant RadarTiles::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row()<0 || index.row()>=entries_.size()) return {};
    const auto& entry=entries_[index.row()];
    if (role==ResourceIndex) return entry.source.isValid() ? entry.resourceIndex : -1;
    if (role==WorldX) return entry.world.x();
    if (role==WorldY) return entry.world.y();
    if (role==AnimationScale) return entry.animation==Animation::Pulse ? oscillation(frameTime_-entry.started,400,.8) : 1.;
    if (role==TerrainColor) return terrainColor(entry.kind);
    return entry.source.data(role);
}
bool RadarTiles::publish(const QList<Entry>& entries, bool refresh) {
    // Projection already supplies unique paths in order. Reconcile that list directly.
    const auto contains=[&](const QString& path) {
        const auto found=std::lower_bound(entries.cbegin(),entries.cend(),path,[](const auto& entry,const auto& key) { return entry.path<key; });
        return found!=entries.cend() && found->path==path;
    };
    for (int last=int(entries_.size())-1;last>=0;) {
        if (contains(entries_[last].path)) { --last; continue; }
        int first=last;
        while (first>0 && !contains(entries_[first-1].path)) --first;
        beginRemoveRows({},first,last); entries_.remove(first,last-first+1); endRemoveRows(); last=first-1;
    }
    int firstChanged=-1,lastChanged=-1,firstIndexChanged=-1,lastIndexChanged=-1;
    auto next=entries.cbegin();
    for (int position=0;position<entries_.size();++position) {
        while (next->path<entries_[position].path) ++next;
        auto entry=*next++;
        if (entries_[position].animation==entry.animation) entry.started=entries_[position].started;
        const bool changed=entries_[position].source!=entry.source || entries_[position].world!=entry.world || entries_[position].animation!=entry.animation;
        const bool indexChanged=entries_[position].resourceIndex!=entry.resourceIndex;
        entries_[position]=entry;
        if (changed || refresh) { if (firstChanged<0) firstChanged=position; lastChanged=position; }
        else if (indexChanged) { if (firstIndexChanged<0) firstIndexChanged=position; lastIndexChanged=position; }
    }
    if (firstChanged>=0) emit dataChanged(index(firstChanged,0),index(lastChanged,0));
    if (firstIndexChanged>=0) emit dataChanged(index(firstIndexChanged,0),index(lastIndexChanged,0),{ResourceIndex});
    int position=0,candidate=0;
    while (candidate<entries.size()) {
        if (position<entries_.size() && entries_[position].path==entries[candidate].path) { ++position; ++candidate; continue; }
        const int first=candidate;
        while (candidate<entries.size() && (position==entries_.size() || entries[candidate].path<entries_[position].path)) ++candidate;
        const int count=candidate-first;
        beginInsertRows({},position,position+count-1);
        entries_.insert(position,count,Entry{});
        std::copy_n(entries.cbegin()+first,count,entries_.begin()+position);
        endInsertRows();
        position+=count;
    }
    return true;
}
void RadarTiles::refreshSourceIndices() {
    int first=-1,last=-1;
    for (int row=0;row<entries_.size();++row) {
        auto& entry=entries_[row];
        const auto resourceIndex=entry.source.row();
        if (entry.resourceIndex==resourceIndex) continue;
        entry.resourceIndex=resourceIndex;
        if (first<0) first=row;
        last=row;
    }
    if (first>=0) emit dataChanged(index(first,0),index(last,0),{ResourceIndex});
}
bool RadarTiles::advanceFrame(qint64 at) {
    if (frameTime_==at) return false;
    frameTime_=at;
    int first=-1,last=-1;
    for (int row=0;row<entries_.size();++row) if (entries_[row].animation==Animation::Pulse) {
        if (first<0) first=row;
        last=row;
    }
    if (first>=0) emit dataChanged(index(first,0),index(last,0),{AnimationScale});
    return true;
}
RadarIsland::RadarIsland(QQuickItem* parent) : QQuickPaintedItem(parent) {
    setAntialiasing(false);
    animationClock_.start();
    animationTimer_.setInterval(16);
    animationTimer_.setTimerType(Qt::PreciseTimer);
    connect(&animationTimer_,&QTimer::timeout,this,[this] {
        syncAnimationClock();
        if (animationTimer_.isActive() && tiles_.advanceFrame(animationClock_.elapsed())) update();
    });
    connect(this,&QQuickItem::activeFocusChanged,this,[this] { update(); });
    connect(this,&QQuickItem::windowChanged,this,[this](QQuickWindow* window) {
        disconnect(frameConnection_);
        if (window) frameConnection_=connect(window,&QQuickWindow::afterAnimating,this,&RadarIsland::syncAnimationClock);
        syncAnimationClock();
    });
    connect(this,&QQuickItem::widthChanged,this,[this] { project(); update(); });
    connect(this,&QQuickItem::heightChanged,this,[this] { project(); update(); });
    connect(this,&QQuickItem::visibleChanged,this,[this] {
        if (isVisible()) schedule();
        else { animationTimer_.stop(); tiles_.publish({},false); }
    });
}
RadarIsland::~RadarIsland() {
    animationTimer_.stop();
    disconnect(frameConnection_);
    for (const auto& connection:connections_) disconnect(connection);
    for (const auto& connection:alertConnections_) disconnect(connection);
    disconnect(this,nullptr,this,nullptr);
}
void RadarIsland::setAlerts(Alerts* alerts) {
    if (alerts_==alerts) return;
    for (const auto& connection:alertConnections_) disconnect(connection);
    alertConnections_.clear(); alerts_=alerts;
    const auto refresh=[this] { if (dirty_) schedule(); else { project(); update(); } };
    if (alerts_) {
        alertConnections_.append(connect(alerts_.data(),&Alerts::presentationChanged,this,refresh));
        alertConnections_.append(connect(alerts_.data(),&Alerts::rulesChanged,this,refresh));
        alertConnections_.append(connect(alerts_.data(),&QObject::destroyed,this,refresh));
    }
    refresh(); emit alertsChanged();
}
void RadarIsland::setEffectsEnabled(bool enabled) {
    if (effectsEnabled_==enabled) return;
    effectsEnabled_=enabled; project(); update(); emit effectsEnabledChanged();
}
void RadarIsland::setGlyphFill(const QColor& color) {
    if (!color.isValid() || glyphFill_==color) return;
    glyphFill_=color; update(); emit glyphFillChanged();
}
void RadarIsland::setInspectedPath(const QString& path) {
    if (inspectedPath_==path) return;
    inspectedPath_=path; update(); emit inspectedPathChanged();
}
void RadarIsland::setHoveredPath(const QString& path) {
    if (hoveredPath_==path) return;
    hoveredPath_=path; update(); emit hoveredPathChanged();
}
void RadarIsland::setFocusedPath(const QString& path) {
    if (focusedPath_==path) return;
    focusedPath_=path; update(); emit focusedPathChanged();
}
void RadarIsland::setSource(QAbstractItemModel* source) {
    if (source_==source) return;
    source_=source;
    observeSource(); emit sourceChanged();
}
void RadarIsland::observeSource() {
    for (const auto& connection:connections_) disconnect(connection);
    connections_.clear(); geometryDirty_=true; filteredDirty_=true;
    const auto rebind=[this] { observeSource(); if (!source_) emit sourceChanged(); };
    const auto observe=[this,rebind](QAbstractItemModel* model,bool geometry) {
        if (!model) return;
        const auto invalidate=[this,geometry] { geometryDirty_=geometryDirty_ || geometry; filteredDirty_=true; schedule(); };
        connections_.append(connect(model,&QAbstractItemModel::modelReset,this,invalidate));
        connections_.append(connect(model,&QAbstractItemModel::rowsInserted,this,invalidate));
        connections_.append(connect(model,&QAbstractItemModel::rowsRemoved,this,invalidate));
        connections_.append(connect(model,&QAbstractItemModel::dataChanged,this,invalidate));
        connections_.append(connect(model,&QAbstractItemModel::layoutChanged,this,[this,geometry,invalidate](const QList<QPersistentModelIndex>&,QAbstractItemModel::LayoutChangeHint hint) {
            // A proxy's vertical sort cannot change membership or stable source indices.
            if (!geometry && hint==QAbstractItemModel::VerticalSortHint && !dirty_ && isVisible()) {
                // Persistent proxy indices have already followed the sort. The
                // cached drawing and resource metadata have not changed.
                tiles_.refreshSourceIndices();
                emit currentResourceChanged();
            }
            else if (!geometry && hint==QAbstractItemModel::VerticalSortHint) schedule();
            else invalidate();
        }));
        connections_.append(connect(model,&QObject::destroyed,this,rebind));
    };
    auto* source=source_.data();
    auto* proxy=qobject_cast<QSortFilterProxyModel*>(source);
    observe(source,!proxy);
    if (proxy) {
        observe(proxy->sourceModel(),true);
        connections_.append(connect(proxy,&QAbstractProxyModel::sourceModelChanged,this,rebind));
    }
    schedule();
}
void RadarIsland::schedule() {
    dirty_=true;
    if (!isVisible() || pending_) return;
    pending_=true;
    QMetaObject::invokeMethod(this,[this] { pending_=false; if (isVisible() && dirty_) synchronize(); },Qt::QueuedConnection);
}
void RadarIsland::setIdentityScope(const QString& scope) {
    if (identityScope_ == scope) return;
    identityScope_ = scope;
    geometryDirty_=true;
    schedule();
    emit sourceChanged();
}
void RadarIsland::synchronize() {
    dirty_=false;
    const bool refreshMetadata=geometryDirty_;
    auto* proxy=qobject_cast<QSortFilterProxyModel*>(source_.data());
    if (geometryDirty_) {
        geometryDirty_=false;
        auto* cache=qobject_cast<ResourceTable*>(proxy ? proxy->sourceModel() : source_.data());
        QList<Row> rows;
        if (cache) {
            int clusterColumn=-1;
            for (int column=0;column<cache->columnCount();++column) if (cache->headerData(column,Qt::Horizontal,Qt::UserRole)=="cluster") clusterColumn=column;
            const auto cluster=clusterColumn<0 ? QString{} : cache->data(cache->index(0,clusterColumn),Qt::UserRole+6).toString();
            rows.reserve(cache->rowCount());
            for (int index=0;index<cache->rowCount();++index) {
                const auto row=cache->row(index); const auto kind=row["kind"].toString(), name=row["name"].toString();
                const auto scope=row["namespace"].toString();
                const auto uid=row["uid"].toString();
                const auto identity=identityScope_+":"+kind+":"+scope+":"+name+":"+(uid.isEmpty() ? "-" : uid);
                rows.append({row["path"].toString(),kind,name,scope,row["cluster"].toString(cluster),row["owner"].toString(),identity});
            }
        }
        if (rows!=signature_) {
            signature_=rows;
            if (const auto* terrain=terrains_.object(identityScope_); terrain && terrain->signature==rows) {
                positions_=terrain->positions; resourceBuckets_=terrain->buckets; markers_=terrain->markers; markerBuckets_=terrain->markerBuckets; terrainBounds_=terrain->bounds;
            } else {
                std::sort(rows.begin(),rows.end(),[](const Row& a,const Row& b) {
                    const int aRank = rank(a.kind), bRank = rank(b.kind);
                    return std::tie(a.cluster,a.scope,aRank,a.owner,a.kind,a.name,a.path)
                        < std::tie(b.cluster,b.scope,bRank,b.owner,b.kind,b.name,b.path);
                });
                layout(rows);
                terrains_.insert(identityScope_,new Terrain{signature_,positions_,resourceBuckets_,markers_,markerBuckets_,terrainBounds_});
            }
        }
    }
    if (filteredDirty_) {
        filteredDirty_=false;
        QHash<QString,QPersistentModelIndex> filtered;
        groups_.clear();
        if (source_) for (int row=0;row<source_->rowCount();++row) {
            const auto index=source_->index(row,0);
            // Full-cache identities need stable cache indices, not persistent sorted projections.
            filtered.insert(index.data(Qt::UserRole).toString(),proxy ? proxy->mapToSource(index) : index);
        }
        filtered_.swap(filtered);
        for (const auto& row:signature_) if (filtered_.contains(row.path)) {
            groups_.insert(row.cluster);
            groups_.insert(row.cluster+"/"+(row.kind=="Namespace" ? row.name : row.scope));
        }
    }
    project(refreshMetadata); update(); emit currentResourceChanged();
}
void RadarIsland::layout(const QList<Row>& rows) {
    positions_.clear(); resourceBuckets_.clear(); markers_.clear(); markerBuckets_.clear(); terrainBounds_={};
    struct District final { QString key, label, group; QList<Row> rows; QList<QPoint> cells; QPoint anchor; QRectF labelBounds; int width=0, height=0, x=0, y=0; };
    struct Cluster final { QString name, label; QList<District> districts; QRectF labelBounds; int width=0, height=0, x=0, y=0; };
    const auto caption=[](const QString& value) { return value.size()>48 ? value.left(45)+"..." : value; };
    const auto bounds=[](const QString& label,bool cluster) {
        QFont font; font.setPixelSize(cluster ? 12 : 10); font.setBold(cluster);
        return QFontMetricsF(font).boundingRect(label).adjusted(-3,-2,3,2);
    };
    QMap<QString,QMap<QString,QList<Row>>> grouped;
    for (const auto& row:rows) grouped[row.cluster][row.kind=="Namespace" ? row.name : row.scope].append(row);
    QList<Cluster> clusters;
    for (auto groupedCluster=grouped.cbegin();groupedCluster!=grouped.cend();++groupedCluster) {
        Cluster cluster; cluster.name=groupedCluster.key(); cluster.label=caption(cluster.name.isEmpty() ? QStringLiteral("Cluster") : cluster.name); cluster.labelBounds=bounds(cluster.label,true);
        int area=0,maxWidth=0;
        for (auto groupedDistrict=groupedCluster->cbegin();groupedDistrict!=groupedCluster->cend();++groupedDistrict) {
            District district;
            district.key=groupedDistrict.key(); district.label=caption(district.key.isEmpty() ? QStringLiteral("Cluster resources") : district.key);
            district.group=cluster.name+"/"+district.key; district.rows=groupedDistrict.value(); district.labelBounds=bounds(district.label,false);
            QSet<QPoint> occupied; occupied.insert(QPoint{});
            QHash<QPoint,double> frontier;
            const auto shoreKey=identityScope_+":"+district.group;
            const std::array<double,3> coast{range("coast-two:"+shoreKey,-std::numbers::pi,std::numbers::pi),
                range("coast-three:"+shoreKey,-std::numbers::pi,std::numbers::pi),range("coast-five:"+shoreKey,-std::numbers::pi,std::numbers::pi)};
            QHash<QString,QPoint> local;
            QList<Row> resources;
            for (const auto& row:district.rows)
                if (row.kind=="Namespace" && row.name==district.key) local.insert(row.path,QPoint{});
                else resources.append(row);
            const double direction=-std::numbers::pi/2+cluster.districts.size()*2*std::numbers::pi/groupedCluster->size()+range(district.group+":namespace-angle",-.16,.16);
            const int arms=resources.size()<=3 ? int(resources.size()) : std::clamp(int(std::ceil(std::sqrt(resources.size())/1.2)),3,10);
            const double spread=std::clamp(arms*.09,.16,.78);
            for (int index=0;index<resources.size();++index) {
                const auto& row=resources[index]; const int arm=index%arms,depth=index/arms;
                if (local.isEmpty()) { local.insert(row.path,QPoint{}); continue; }
                const double family=range(district.group+":"+row.kind+":"+row.owner+":coast-branch",-.7,.7);
                const double theta=direction+family+(arms==1 ? 0 : (arm/double(arms-1)-.5)*spread)+range(row.identity+":branch",-.04,.04);
                const double distance=8+ring(row.kind)*6.2+depth*6.4+(row.kind=="Event" ? 8 : 0),drift=range(row.identity+":drift",-3.5,3.5);
                local.insert(row.path,place(QPointF(std::cos(theta)*distance-std::sin(theta)*drift,std::sin(theta)*distance+std::cos(theta)*drift),occupied,frontier,row.identity,coast));
            }
            int left=0,right=0,top=0,bottom=0;
            for (const auto& point:occupied) {
                left=std::min(left,point.x()); right=std::max(right,point.x());
                top=std::min(top,point.y()); bottom=std::max(bottom,point.y());
            }
            district.anchor={2-left,4-top};
            for (const auto& row:district.rows) district.cells.append(local.value(row.path)+district.anchor);
            district.width=right-left+5;
            district.height=bottom-top+8;
            area+=district.width*district.height; maxWidth=std::max(maxWidth,district.width);
            cluster.districts.append(district);
        }
        const int target=std::max(maxWidth,int(std::ceil(std::sqrt(area*1.35))));
        int x=2,y=6,rowHeight=0,right=0,bottom=0;
        for (auto& district:cluster.districts) {
            if (x>2 && x+district.width>target+2) { x=2; y+=rowHeight+4; rowHeight=0; }
            district.x=x; district.y=y; x+=district.width+4; rowHeight=std::max(rowHeight,district.height);
            right=std::max(right,district.x+district.width); bottom=std::max(bottom,district.y+district.height);
        }
        cluster.width=right+2; cluster.height=bottom+2;
        clusters.append(cluster);
    }
    int area=0,maxWidth=0;
    for (const auto& cluster:clusters) { area+=cluster.width*cluster.height; maxWidth=std::max(maxWidth,cluster.width); }
    const int target=std::max(maxWidth,int(std::ceil(std::sqrt(area*1.4))));
    int x=0,y=0,rowHeight=0;
    for (auto& cluster:clusters) {
        if (x>0 && x+cluster.width>target) { x=0; y+=rowHeight+12; rowHeight=0; }
        cluster.x=x; cluster.y=y; x+=cluster.width+12; rowHeight=std::max(rowHeight,cluster.height);
    }
    QPoint offset;
    if (!clusters.isEmpty()) {
        const auto& cluster=clusters.first(); const auto& district=cluster.districts.first();
        // The starting district stays in view instead of centering empty water.
        offset=-(QPoint(cluster.x+district.x,cluster.y+district.y)+district.anchor);
    }
    const auto resource=[&](const Row& row,const QPoint& point) {
        positions_.insert(row.path,world(point)); resourceBuckets_[bucket(point)].append(row.path);
    };
    for (const auto& cluster:clusters) {
        const QPoint origin=offset+QPoint(cluster.x,cluster.y);
        const auto clusterPoint=origin+QPoint(cluster.width/2,2);
        const auto clusterMarker=markers_.size();
        markers_.append({world(clusterPoint),cluster.labelBounds,"Cluster",cluster.name,cluster.label,true,{}});
        const auto packed=QRectF(world(origin),QSizeF(cluster.width*step,cluster.height*step));
        terrainBounds_=terrainBounds_.isEmpty() ? packed : terrainBounds_.united(packed);
        for (const auto& district:cluster.districts) {
            const auto districtOrigin=origin+QPoint(district.x,district.y);
            const auto anchor=districtOrigin+district.anchor;
            QRectF land;
            for (int index=0;index<district.rows.size();++index) {
                const auto point=districtOrigin+district.cells[index]; resource(district.rows[index],point);
                const QRectF body(world(point)-QPointF(2.75,2.75),QSizeF(5.5,5.5));
                land=land.isEmpty() ? body : land.united(body);
            }
            markers_.append({world(anchor),district.labelBounds,district.key.isEmpty() ? "Cluster" : "Namespace",district.group,district.label,false,land});
            auto& clusterLand=markers_[clusterMarker].area;
            clusterLand=clusterLand.isEmpty() ? land : clusterLand.united(land);
        }
    }
    for (int index=0;index<markers_.size();++index)
        inBuckets(markers_[index].area,[&](const QPoint& area) { markerBuckets_[area].append(index); });
}
QVariantMap RadarIsland::viewPose() const { return {{"x",pan_.x()},{"y",pan_.y()},{"zoom",zoom_}}; }
bool RadarIsland::validPose(const QVariantMap& pose) {
    bool xOk=false,yOk=false,zOk=false;
    const double x=pose.value("x").toDouble(&xOk),y=pose.value("y").toDouble(&yOk),z=pose.value("zoom").toDouble(&zOk);
    const double extent=std::numeric_limits<int>::max()*step/4;
    return pose.size()==3 && xOk && yOk && zOk && std::isfinite(x) && std::isfinite(y) && std::isfinite(z) && std::abs(x)<extent && std::abs(y)<extent && z>=minimumZoom && z<=maximumZoom;
}
void RadarIsland::setViewPose(const QVariantMap& pose) {
    if (!validPose(pose)) return;
    const double x=pose.value("x").toDouble(),y=pose.value("y").toDouble(),z=pose.value("zoom").toDouble();
    if (pan_==QPointF(x,y) && zoom_==z) return;
    pan_={x,y}; zoom_=z; project(); update(); emit viewPoseChanged();
}
void RadarIsland::setTextColor(const QColor& color) {
    if (!color.isValid() || textColor_==color) return;
    textColor_=color; update(); emit textColorChanged();
}
bool RadarIsland::pan(double dx,double dy) {
    if (!std::isfinite(dx) || !std::isfinite(dy)) return false;
    setViewPose({{"x",pan_.x()+dx/zoom_},{"y",pan_.y()+dy/zoom_},{"zoom",zoom_}}); return true;
}
bool RadarIsland::zoomAt(double factor,double x,double y) {
    if (!std::isfinite(factor) || factor<=0 || !std::isfinite(x) || !std::isfinite(y)) return false;
    const double next=std::clamp(zoom_*factor,minimumZoom,maximumZoom);
    const QPointF offset(x-width()/2,y-height()/2);
    const auto nextPan=pan_+offset/next-offset/zoom_;
    setViewPose({{"x",nextPan.x()},{"y",nextPan.y()},{"zoom",next}}); return true;
}
bool RadarIsland::focusResource(int index,double zoom) {
    if (!source_ || index<0 || index>=source_->rowCount() || !std::isfinite(zoom)) return false;
    if (dirty_) synchronize();
    const auto path=source_->index(index,0).data(Qt::UserRole).toString();
    const auto point=positions_.constFind(path);
    if (point==positions_.cend()) return false;
    selectResource(index);
    setViewPose({{"x",-point->x()},{"y",-point->y()},{"zoom",zoom>0 ? std::clamp(zoom,minimumZoom,maximumZoom) : zoom_}}); return true;
}
int RadarIsland::currentIndex() const {
    const auto found=filtered_.constFind(currentPath_);
    if (!source_ || found==filtered_.cend() || !found->isValid()) return -1;
    const auto* proxy=qobject_cast<QSortFilterProxyModel*>(source_.data());
    if (found->model()!=(proxy ? proxy->sourceModel() : source_.data())) return -1;
    return proxy ? proxy->mapFromSource(*found).row() : found->row();
}
bool RadarIsland::selectResource(int index) {
    if (index < -1 || (index>=0 && (!source_ || index>=source_->rowCount()))) return false;
    const auto path=index<0 ? QString{} : source_->index(index,0).data(Qt::UserRole).toString();
    if (path!=currentPath_) { currentPath_=path; update(); emit currentResourceChanged(); }
    return true;
}
bool RadarIsland::resetView() { setViewPose({{"x",0},{"y",0},{"zoom",1}}); return true; }
QString RadarIsland::resourceAt(double x,double y) {
    if (!isVisible() || !source_ || !std::isfinite(x) || !std::isfinite(y) || x<0 || y<0 || x>=width() || y>=height()) return {};
    if (dirty_) synchronize();
    const QPointF point=(QPointF(x,y)-QPointF(width()/2,height()/2))/zoom_-pan_;
    const auto candidates=resourceBuckets_.constFind(bucket(cell(point)));
    if (candidates==resourceBuckets_.cend()) return {};
    const auto& entries=tiles_.entries();
    for (const auto& path:*candidates) {
        const auto found=std::lower_bound(entries.cbegin(),entries.cend(),path,[](const auto& entry,const auto& key) { return entry.path<key; });
        if (found==entries.cend() || found->path!=path || !found->source.isValid()) continue;
        const double scale=found->animation==RadarTiles::Animation::Pulse ? oscillation(tiles_.frameTime()-found->started,400,.8) : 1;
        if (std::abs(point.x()-found->world.x())<=2.75*scale && std::abs(point.y()-found->world.y())<=2.75*scale) return path;
    }
    return {};
}
QVariantMap RadarIsland::resourceInfo(const QString& path,bool details) const {
    if (!isVisible() || !source_ || path.isEmpty()) return {};
    const auto& entries=tiles_.entries();
    const auto found=std::lower_bound(entries.cbegin(),entries.cend(),path,[](const auto& entry,const auto& key) { return entry.path<key; });
    if (found==entries.cend() || found->path!=path || !found->source.isValid()) return {};
    const auto index=tiles_.index(int(found-entries.cbegin()),0);
    QVariantMap result;
    const auto roles=tiles_.roleNames();
    for (auto role=roles.cbegin();role!=roles.cend();++role)
        if (details || role.key()>=ResourceIndex || role.key()==Qt::UserRole)
            result.insert(QString::fromUtf8(role.value()),tiles_.data(index,role.key()));
    return result;
}
bool RadarIsland::traverseVisible(int direction) {
    if (!isVisible() || !source_ || direction<-1 || direction>1) return false;
    if (dirty_) synchronize();
    const auto& entries=tiles_.entries();
    if (entries.isEmpty()) return false;
    const auto current=std::lower_bound(entries.cbegin(),entries.cend(),currentPath_,[](const auto& entry,const auto& key) { return entry.path<key; });
    const int position=direction==0 ? 0 : current!=entries.cend() && current->path==currentPath_
        ? int(current-entries.cbegin())+direction : direction>0 ? 0 : int(entries.size())-1;
    if (position<0 || position>=entries.size() || !entries[position].source.isValid()) return false;
    return selectResource(entries[position].source.row());
}
bool RadarIsland::fitView() {
    if (!isVisible() || !source_ || width()<=16 || height()<=32) return false;
    if (dirty_) synchronize();
    const auto& bounds=terrainBounds_;
    if (bounds.isEmpty()) return false;
    const auto center=bounds.center();
    const double zoom=std::clamp(std::min((width()-16)/bounds.width(),(height()-32)/bounds.height()),minimumZoom,maximumZoom);
    setViewPose({{"x",-center.x()},{"y",-center.y()+8/zoom},{"zoom",zoom}});
    return true;
}
QImage RadarIsland::previewImage() {
    if (!isVisible()) return {};
    if (dirty_) synchronize();
    QImage image(320,200,QImage::Format_ARGB32_Premultiplied); image.fill(QColor("#071619"));
    const auto& bounds=terrainBounds_;
    if (bounds.isEmpty()) return image;
    QPainter painter(&image);
    const auto scale=std::min(304./bounds.width(),184./bounds.height());
    painter.translate(160,100); painter.scale(scale,scale); painter.translate(-bounds.center());
    for (const auto& row:signature_) {
        const auto point=positions_.value(row.path);
        painter.fillRect(QRectF(point-QPointF(2.75,2.75),QSizeF(5.5,5.5)),terrainColor(row.kind));
    }
    return image;
}
QRectF RadarIsland::worldViewport() const { return {QPointF(-width()/2/zoom_,-height()/2/zoom_)-pan_,QSizeF(width()/zoom_,height()/zoom_)}; }
void RadarIsland::project(bool refresh) {
    using Animation=RadarTiles::Animation;
    QList<RadarTiles::Entry> visible;
    const auto* proxy=qobject_cast<QSortFilterProxyModel*>(source_.data());
    if (isVisible() && width()>0 && height()>0) {
        const auto onScreen=worldViewport();
        const auto viewport=onScreen.adjusted(-step,-step,step,step);
        inBuckets(viewport.intersected(terrainBounds_),[&](const QPoint& area) {
            const auto found=resourceBuckets_.constFind(area); if (found==resourceBuckets_.cend()) return;
            for (const auto& path:*found) {
                const auto source=filtered_.constFind(path); if (source==filtered_.cend() || !source->isValid()) continue;
                if (source->model()!=(proxy ? proxy->sourceModel() : source_.data())) continue;
                const auto index=proxy ? proxy->mapFromSource(*source) : QModelIndex(*source);
                if (!index.isValid()) continue;
                const auto point=positions_.value(path);
                if (!viewport.contains(point)) continue;
                RadarTiles::Entry tile;
                tile.path=path; tile.source=index; tile.resourceIndex=index.row(); tile.world=point;
                tile.kind=index.data(Qt::UserRole+2).toString();
                tile.color=terrainColor(tile.kind);
                const int health=index.data(Qt::UserRole+9).toInt();
                tile.announce=QColor(health==2 ? "#FF5C5C" : health==1 ? "#FFE866" : "#7DFFC3");
                const auto effect=alerts_ ? alerts_->effect(path) : QVariantMap{};
                const auto tone=effect.value(QStringLiteral("color")).toString();
                tile.glow=effect.contains(QStringLiteral("color"));
                if (tone=="status") { if (health==1 || health==2) tile.color=tile.announce; }
                else if (tone=="fresh") tile.color=QColor("#7DFFC3");
                else if (!tone.isEmpty()) { const QColor color(tone); if (color.isValid()) tile.color=color; }
                if (effectsEnabled_ && alerts_ && !alerts_->reducedMotion() && onScreen.intersects(QRectF(point-QPointF(2.75,2.75),QSizeF(5.5,5.5)))) {
                    const auto animation=effect.value(QStringLiteral("animation")).toString();
                    if (animation=="blink") tile.animation=Animation::Blink;
                    else if (animation=="pulse") tile.animation=Animation::Pulse;
                    else if (animation=="outline") tile.animation=Animation::Outline;
                    else if (animation=="sweep") tile.animation=Animation::Sweep;
                }
                tile.started=animationClock_.elapsed();
                visible.append(std::move(tile));
            }
        });
        std::sort(visible.begin(),visible.end(),[](const auto& a,const auto& b) { return a.path<b.path; });
    }
    tiles_.publish(visible,refresh);
    syncAnimationClock();
}
void RadarIsland::syncAnimationClock() {
    const auto viewport=worldViewport();
    const bool moving=effectsEnabled_ && isVisible() && window() && window()->isExposed()
        && std::any_of(tiles_.entries().cbegin(),tiles_.entries().cend(),[&](const auto& tile) {
            return tile.source.isValid() && (tile.animation==RadarTiles::Animation::Blink || tile.animation==RadarTiles::Animation::Pulse || tile.animation==RadarTiles::Animation::Sweep)
                && viewport.intersects(QRectF(tile.world-QPointF(2.75,2.75),QSizeF(5.5,5.5)));
        });
    if (!moving) animationTimer_.stop();
    else if (!animationTimer_.isActive()) animationTimer_.start();
}
void RadarIsland::paint(QPainter* painter) {
    const auto viewport=worldViewport();
    const auto screen=[&](QPointF point) { return QPointF(width()/2,height()/2)+(point+pan_)*zoom_; };
    const auto tile=[&](QPointF world, QColor color) {
        const auto point=screen(world); const double side=5.5*zoom_;
        painter->fillRect(QRectF(point-QPointF(side/2,side/2),QSizeF(side,side)),color);
    };
    const QColor dimmed("#50575B");
    QList<int> visibleMarkers;
    const double labelMargin=160/zoom_;
    inBuckets(viewport.adjusted(-labelMargin,-labelMargin,labelMargin,labelMargin).intersected(terrainBounds_),[&](const QPoint& area) { visibleMarkers.append(markerBuckets_.value(area)); });
    std::sort(visibleMarkers.begin(),visibleMarkers.end());
    visibleMarkers.erase(std::unique(visibleMarkers.begin(),visibleMarkers.end()),visibleMarkers.end());
    inBuckets(viewport.adjusted(-step,-step,step,step).intersected(terrainBounds_),[&](const QPoint& area) {
        const auto found=resourceBuckets_.constFind(area); if (found==resourceBuckets_.cend()) return;
        for (const auto& path:*found) {
            if (filtered_.contains(path)) continue;
            const auto point=positions_.value(path);
            if (viewport.adjusted(-step,-step,step,step).contains(point)) tile(point,dimmed);
        }
    });
    QList<std::pair<QRectF,int>> labels;
    for (const int index:visibleMarkers) {
        const auto& marker=markers_[index];
        if (!viewport.intersects(marker.area)) continue;
        const bool active=groups_.contains(marker.group);
        if (viewport.contains(marker.world)) tile(marker.world,active ? terrainColor(marker.kind) : dimmed);
        const bool clusterLabel=marker.overview;
        if ((clusterLabel && zoom_>=1.35) || (!clusterLabel && zoom_<.85)) continue;
        QRectF label=marker.labelBounds;
        label.setWidth(std::min(label.width(),std::max(0.,width()-8)));
        if (label.width()<=0 || label.height()>height()-8) continue;
        const QRectF land(screen(marker.area.topLeft()),marker.area.size()*zoom_);
        const auto size=label.size();
        const std::array<QPointF,4> centers{{{land.center().x(),land.top()-size.height()/2-6},
            {land.center().x(),land.bottom()+size.height()/2+6},
            {land.left()-size.width()/2-6,land.center().y()},
            {land.right()+size.width()/2+6,land.center().y()}}};
        for (const auto& center:centers) {
            label.moveCenter(center);
            label.moveLeft(std::clamp(label.left(),4.,width()-label.width()-4));
            label.moveTop(std::clamp(label.top(),4.,height()-label.height()-4));
            if (label.adjusted(-4,-4,4,4).intersects(land)
                || std::any_of(labels.cbegin(),labels.cend(),[&](const auto& shown) { return shown.first.adjusted(-4,-2,4,2).intersects(label); })) continue;
            const auto topLeft=(label.topLeft()-QPointF(width()/2,height()/2))/zoom_-pan_;
            const QRectF query(topLeft,label.size()/zoom_);
            bool blocked=false;
            inBuckets(query.adjusted(-step-2/zoom_,-step-2/zoom_,step+2/zoom_,step+2/zoom_).intersected(terrainBounds_),[&](const QPoint& area) {
                if (blocked) return;
                for (const auto& path:resourceBuckets_.value(area)) {
                    const auto point=screen(positions_.value(path)); const double side=5.5*zoom_+3;
                    if (label.adjusted(-2,-2,2,2).intersects(QRectF(point-QPointF(side/2,side/2),QSizeF(side,side)))) { blocked=true; break; }
                }
            });
            if (!blocked) { labels.append({label,index}); break; }
        }
    }
    const auto now=tiles_.frameTime();
    for (const auto& entry:tiles_.entries()) {
        if (!entry.source.isValid()) continue;
        const auto elapsed=now-entry.started;
        const double side=5.5*zoom_;
        const double scale=entry.animation==RadarTiles::Animation::Pulse ? oscillation(elapsed,400,.8) : 1;
        const double opacity=entry.animation==RadarTiles::Animation::Blink ? oscillation(elapsed,350,.35) : 1;
        const QRectF box(-side/2,-side/2,side,side);
        const auto outline=[&](const QRectF& bounds,const QColor& color,double width) {
            painter->fillRect(QRectF(bounds.left(),bounds.top(),bounds.width(),width),color);
            painter->fillRect(QRectF(bounds.left(),bounds.bottom()-width,bounds.width(),width),color);
            painter->fillRect(QRectF(bounds.left(),bounds.top()+width,width,std::max(0.,bounds.height()-2*width)),color);
            painter->fillRect(QRectF(bounds.right()-width,bounds.top()+width,width,std::max(0.,bounds.height()-2*width)),color);
        };
        painter->save();
        painter->translate(screen(entry.world)); painter->scale(scale,scale);
        if (entry.glow) { painter->setOpacity(opacity*.35); outline(box.adjusted(-1.5,-1.5,1.5,1.5),entry.color,2); }
        painter->setOpacity(opacity); painter->fillRect(box,entry.color);
        if (entry.path==hoveredPath_ || entry.path==focusedPath_ || entry.path==inspectedPath_ || (hasActiveFocus() && entry.path==currentPath_))
            outline(box,QColor("#7DFFC3"),1.2);
        if (side>=9) KindGlyph::draw(painter,box,entry.kind,glyphFill_,entry.color);
        if (entry.animation==RadarTiles::Animation::Outline) outline(box.adjusted(-1,-1,1,1),entry.announce,2);
        else if (entry.animation==RadarTiles::Animation::Sweep) {
            const double width=std::max(1.,side*.12);
            const double progress=double(std::max<qint64>(0,elapsed)%800)/800;
            painter->fillRect(QRectF(box.left()+progress*std::max(0.,side-width),box.top(),width,side),entry.announce);
        }
        painter->restore();
    }
    // Labels are a foreground annotation, never terrain or an input target.
    for (const auto& [label,index]:labels) {
        const auto& marker=markers_[index];
        QFont font; font.setPixelSize(marker.overview ? 12 : 10); font.setBold(marker.overview); painter->setFont(font);
        painter->fillRect(label.adjusted(-1,-1,1,1),QColor("#061621"));
        painter->setPen(groups_.contains(marker.group) ? textColor_ : textColor_.darker(140));
        painter->drawText(label,Qt::AlignCenter,QFontMetricsF(font).elidedText(marker.label,Qt::ElideRight,label.width()));
    }
}

RadarWater::RadarWater(QQuickItem* parent) : QQuickPaintedItem(parent) {
    setAntialiasing(false); clock_.start();
    connect(this,&QQuickItem::visibleChanged,this,&RadarWater::syncTimer);
    connect(this,&QQuickItem::windowChanged,this,[this] { syncTimer(); });
    connect(&timer_,&QTimer::timeout,this,[this] {
        if (clock_.elapsed() < interactionUntil_) { syncTimer(); return; }
        phase_=(phase_+1)&1023; update(); syncTimer();
    });
}
void RadarWater::setViewPose(const QVariantMap& pose) {
    if (!RadarIsland::validPose(pose) || pose_==pose) return;
    pose_=pose; interactionUntil_=clock_.elapsed()+200; syncTimer(); update(); emit viewPoseChanged();
}
void RadarWater::setColor(const QColor& color) { if (!color.isValid() || color_==color) return; color_=color; update(); emit colorChanged(); }
void RadarWater::setPlaying(bool playing) { if (playing_==playing) return; playing_=playing; syncTimer(); emit playingChanged(); }
void RadarWater::setSpeedPercent(int speed) {
    if (speed<0 || speed>100 || speed_==speed) return;
    speed_=speed; syncTimer(); update(); emit speedPercentChanged();
}
bool RadarWater::noteRequest() {
    const auto now=clock_.elapsed();
    while (!requests_.isEmpty() && (requests_.head()<=now-60000 || requests_.size()>=240)) requests_.dequeue();
    requests_.enqueue(now); return true;
}
int RadarWater::interval() {
    while (!requests_.isEmpty() && requests_.head()<=clock_.elapsed()-60000) requests_.dequeue();
    return std::clamp(qRound(460-speed_*.01*380-requests_.size()/240.*40),60,520);
}
void RadarWater::syncTimer() {
    if (!playing_ || !isVisible() || !window() || speed_<=0) { timer_.stop(); return; }
    const int next=std::max(interval(),int(std::max(qint64{0},interactionUntil_-clock_.elapsed())));
    if (!timer_.isActive() || timer_.interval()!=next) timer_.start(next);
}
void RadarWater::paint(QPainter* painter) {
    painter->fillRect(boundingRect(),color_);
    if (speed_<=0) return;
    const double zoom=pose_["zoom"].toDouble(), panX=pose_["x"].toDouble(), panY=pose_["y"].toDouble();
    painter->fillRect(boundingRect(),QColor("#15071C27"));
    const double drift=phase_*(.08+speed_*.01*.38)*(.65+requests_.size()/240.*.8);
    const auto mod=[](double value) { const auto result=std::fmod(value,18.); return result<0 ? result+18 : result; };
    const double offsetX=mod(panX*zoom*.65+drift), offsetY=mod(panY*zoom*.35+drift*.45);
    static const QColor shades[]{QColor("#302E7282"),QColor("#2A205766"),QColor("#36359394")};
    for (int row=0;row<int(std::ceil(height()/18))+2;++row) for (int column=0;column<int(std::ceil(width()/18))+2;++column) {
        const double x=column*18+offsetX-18,y=row*18+offsetY-18;
        const int worldColumn=int(std::floor(((x-width()/2)/zoom-panX)/18)), worldRow=int(std::floor(((y-height()/2)/zoom-panY)/18));
        const int value=(quint32(worldColumn)*17+quint32(worldRow)*29+quint32(phase_)*3)&63;
        const int shade=value==0 ? 0 : value==11 || value==37 ? 1 : (worldColumn+phase_+(worldRow%5)*2)%17==0 ? 2 : -1;
        if (shade<0) continue;
        const double inset=shade==2 ? 6.1 : 5.2;
        painter->fillRect(QRectF(x+inset,y+inset,18-inset*2,18-inset*2),shades[shade]);
    }
}
}
