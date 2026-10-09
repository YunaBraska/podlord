#include "radar_island.h"
#include "workspace.h"
#include <QPainter>
#include <algorithm>
#include <cmath>
#include <numbers>
#include <limits>
#include <tuple>

namespace podlord {
namespace {
constexpr int ResourceIndex = Qt::UserRole + 20, WorldX = ResourceIndex + 1, WorldY = ResourceIndex + 2;
constexpr int TerrainColor = ResourceIndex + 3;
constexpr double step = 7;
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
QPoint place(const QPointF& target, QSet<QPoint>& occupied, const QString& key) {
    const auto center=cell(target);
    if (!occupied.contains(center)) { occupied.insert(center); return center; }
    for (int radius=1;;++radius) {
        std::optional<QPoint> best;
        std::pair<int,quint32> score;
        const auto consider=[&](const QPoint& candidate) {
            if (occupied.contains(candidate)) return;
            const auto delta=candidate-center;
            const int distance=QPoint::dotProduct(delta,delta);
            if (best && distance>score.first) return;
            const auto candidateScore=std::pair{distance,hash(key+":"+QString::number(candidate.x())+":"+QString::number(candidate.y()))};
            if (!best || candidateScore<score) { best=candidate; score=candidateScore; }
        };
        for (int dx=-radius;dx<=radius;++dx) { consider(center+QPoint(dx,-radius)); consider(center+QPoint(dx,radius)); }
        for (int dy=-radius+1;dy<radius;++dy) { consider(center+QPoint(-radius,dy)); consider(center+QPoint(radius,dy)); }
        if (best) { occupied.insert(*best); return *best; }
    }
}
template<class F> void inBuckets(const QRectF& rect, F consume) {
    const auto first=bucket(cell(rect.topLeft()-QPointF(step,step))), last=bucket(cell(rect.bottomRight()+QPointF(step,step)));
    for (int x=first.x();x<=last.x();++x) for (int y=first.y();y<=last.y();++y) consume(QPoint(x,y));
}
}
int RadarTiles::rowCount(const QModelIndex& parent) const { return parent.isValid() ? 0 : int(entries_.size()); }
QHash<int,QByteArray> RadarTiles::roleNames() const {
    return {{Qt::UserRole,"resourcePath"},{Qt::UserRole+1,"resourceName"},{Qt::UserRole+2,"resourceKind"},
        {Qt::UserRole+3,"resourceNamespace"},{Qt::UserRole+4,"resourceStatus"},{Qt::UserRole+5,"statusColor"},
        {Qt::UserRole+7,"resourceMetrics"},{Qt::UserRole+9,"resourceHealth"},{ResourceIndex,"resourceIndex"},{WorldX,"worldX"},{WorldY,"worldY"},{TerrainColor,"terrainColor"}};
}
QVariant RadarTiles::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row()<0 || index.row()>=entries_.size()) return {};
    const auto& entry=entries_[index.row()];
    if (role==ResourceIndex) return entry.source.row();
    if (role==WorldX) return entry.world.x();
    if (role==WorldY) return entry.world.y();
    if (role==TerrainColor) return terrainColor(entry.source.data(Qt::UserRole+2).toString());
    return entry.source.data(role);
}
bool RadarTiles::publish(const QList<Entry>& entries, bool refresh) {
    QMap<QString,Entry> incoming;
    for (const auto& entry:entries) incoming.insert(entry.path,entry);
    for (int last=int(entries_.size())-1;last>=0;) {
        if (incoming.contains(entries_[last].path)) { --last; continue; }
        int first=last;
        while (first>0 && !incoming.contains(entries_[first-1].path)) --first;
        beginRemoveRows({},first,last); entries_.remove(first,last-first+1); endRemoveRows(); last=first-1;
    }
    int firstChanged=-1,lastChanged=-1;
    for (int position=0;position<entries_.size();++position) {
        const auto entry=incoming.take(entries_[position].path);
        const bool changed=entries_[position].source!=entry.source || entries_[position].world!=entry.world;
        entries_[position]=entry;
        if (changed || refresh) { if (firstChanged<0) firstChanged=position; lastChanged=position; }
    }
    if (firstChanged>=0) emit dataChanged(index(firstChanged,0),index(lastChanged,0));
    for (auto next=incoming.cbegin();next!=incoming.cend();) {
        const auto position=std::lower_bound(entries_.cbegin(),entries_.cend(),next.key(),[](const auto& entry,const auto& path) { return entry.path<path; });
        int index=int(position-entries_.cbegin());
        QList<Entry> added;
        do { added.append(next.value()); ++next; }
        while (next!=incoming.cend() && (position==entries_.cend() || next.key()<position->path));
        beginInsertRows({},index,index+added.size()-1);
        for (const auto& entry:added) entries_.insert(index++,entry);
        endInsertRows();
    }
    return true;
}
RadarIsland::RadarIsland(QQuickItem* parent) : QQuickPaintedItem(parent) {
    setAntialiasing(false);
    connect(this,&QQuickItem::widthChanged,this,[this] { project(); update(); });
    connect(this,&QQuickItem::heightChanged,this,[this] { project(); update(); });
    connect(this,&QQuickItem::visibleChanged,this,[this] { if (isVisible()) schedule(); else tiles_.publish({},false); });
}
void RadarIsland::setSource(QAbstractItemModel* source) {
    if (source_==source) return;
    for (const auto& connection:connections_) disconnect(connection);
    connections_.clear(); source_=source;
    const auto observe=[this](QAbstractItemModel* model) {
        if (!model) return;
        connections_.append(connect(model,&QAbstractItemModel::modelReset,this,&RadarIsland::schedule));
        connections_.append(connect(model,&QAbstractItemModel::rowsInserted,this,&RadarIsland::schedule));
        connections_.append(connect(model,&QAbstractItemModel::rowsRemoved,this,&RadarIsland::schedule));
        connections_.append(connect(model,&QAbstractItemModel::dataChanged,this,&RadarIsland::schedule));
        connections_.append(connect(model,&QAbstractItemModel::layoutChanged,this,&RadarIsland::schedule));
    };
    observe(source);
    if (auto* proxy=qobject_cast<QSortFilterProxyModel*>(source)) observe(proxy->sourceModel());
    schedule(); emit sourceChanged();
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
    schedule();
    emit sourceChanged();
}
void RadarIsland::synchronize() {
    dirty_=false;
    auto* proxy=qobject_cast<QSortFilterProxyModel*>(source_.data());
    auto* cache=qobject_cast<ResourceTable*>(proxy ? proxy->sourceModel() : source_.data());
    QList<Row> rows;
    if (cache) {
        int clusterColumn=-1;
        for (int column=0;column<cache->columnCount();++column) if (cache->headerData(column,Qt::Horizontal,Qt::UserRole)=="cluster") clusterColumn=column;
        const auto cluster=clusterColumn<0 ? QString{} : cache->data(cache->index(0,clusterColumn),Qt::UserRole+6).toString();
        rows.reserve(cache->rowCount());
        for (int index=0;index<cache->rowCount();++index) {
            const auto row=cache->row(index); const auto kind=row["kind"].toString(), name=row["name"].toString();
            const auto scope=row["namespace"].toString("cluster");
            const auto uid=row["uid"].toString();
            const auto identity=identityScope_+":"+kind+":"+scope+":"+name+":"+(uid.isEmpty() ? "-" : uid);
            rows.append({row["path"].toString(),kind,name,scope,row["cluster"].toString(cluster),row["owner"].toString(),identity});
        }
    }
    if (rows!=signature_) {
        signature_=rows;
        if (const auto* terrain=terrains_.object(identityScope_); terrain && terrain->signature==rows) {
            positions_=terrain->positions; resourceBuckets_=terrain->buckets; markers_=terrain->markers;
        } else {
            std::sort(rows.begin(),rows.end(),[](const Row& a,const Row& b) {
                const int aRank = rank(a.kind), bRank = rank(b.kind);
                return std::tie(a.cluster,a.scope,aRank,a.owner,a.kind,a.name,a.path)
                    < std::tie(b.cluster,b.scope,bRank,b.owner,b.kind,b.name,b.path);
            });
            layout(rows);
            terrains_.insert(identityScope_,new Terrain{signature_,positions_,resourceBuckets_,markers_});
        }
    }
    QHash<QString,QPersistentModelIndex> filtered;
    groups_.clear();
    if (source_) for (int row=0;row<source_->rowCount();++row) {
        const auto index=source_->index(row,0);
        // Full-cache identities need stable cache indices, not persistent sorted projections.
        filtered.insert(index.data(Qt::UserRole).toString(),proxy ? proxy->mapToSource(index) : index);
    }
    filtered_.swap(filtered);
    for (const auto& row:signature_) if (filtered_.contains(row.path)) { groups_.insert(row.cluster); groups_.insert(row.cluster+"/"+row.scope); }
    project(true); update(); emit currentResourceChanged();
}
void RadarIsland::layout(const QList<Row>& rows) {
    positions_.clear(); resourceBuckets_.clear(); markers_.clear();
    QSet<QPoint> occupied;
    QHash<QString,QPoint> namespaceAnchors;
    QMap<QString,QMap<QString,QList<Row>>> clusters;
    for (const auto& row:rows) clusters[row.cluster][row.scope].append(row);
    const auto resource=[&](const Row& row,QPoint point) { positions_.insert(row.path,world(point)); resourceBuckets_[bucket(point)].append(row.path); };
    int clusterIndex=0;
    for (auto cluster=clusters.cbegin();cluster!=clusters.cend();++cluster,++clusterIndex) {
        const double angle=-std::numbers::pi/2+clusterIndex*2*std::numbers::pi/clusters.size();
        const QPointF center=clusters.size()==1 ? QPointF{} : QPointF(std::cos(angle)*std::min(84,18+int(clusters.size())*12),std::sin(angle)*std::min(54,14+int(clusters.size())*7));
        const auto core=place(center,occupied,"radar:"+cluster.key()+":cluster:Cluster:"+cluster.key());
        markers_.append({world(core),"Cluster",cluster.key()});
        int namespaceIndex=0;
        for (auto scope=cluster->cbegin();scope!=cluster->cend();++scope,++namespaceIndex) {
            const auto key=cluster.key()+"/"+scope.key();
            const double direction=-std::numbers::pi/2+namespaceIndex*2*std::numbers::pi/cluster->size()+range(key+":namespace-angle",-.16,.16);
            QString anchorIdentity="radar:"+cluster.key()+":cluster:Namespace:"+scope.key();
            for (const auto& row:scope.value()) if (row.kind=="Namespace" && row.name==scope.key()) { anchorIdentity=row.identity; break; }
            const auto anchor=place(world(core)+QPointF(std::cos(direction)*(clusters.size()>1 ? 20 : 28),std::sin(direction)*(clusters.size()>1 ? 14 : 20)),occupied,anchorIdentity);
            namespaceAnchors.insert(key,anchor);
            markers_.append({world(anchor),"Namespace",key});
            QList<Row> resources;
            for (const auto& row:scope.value()) if (row.kind!="Namespace") resources.append(row);
            if (resources.isEmpty()) continue;
            const int arms=resources.size()<=3 ? int(resources.size()) : std::clamp(int(std::ceil(std::sqrt(resources.size())/1.2)),3,10);
            const double spread=std::clamp(arms*.09,.16,.78);
            for (int index=0;index<resources.size();++index) {
                const auto& row=resources[index]; const int arm=index%arms, depth=index/arms;
                const auto identity=row.identity;
                const double theta=direction+(arms==1 ? 0 : (arm/double(arms-1)-.5)*spread)+range(identity+":branch",-.04,.04);
                const double distance=8+ring(row.kind)*6.2+depth*6.4+(row.kind=="Event" ? 8 : 0), drift=range(identity+":drift",-3.5,3.5);
                const auto point=place(world(anchor)+QPointF(std::cos(theta)*distance-std::sin(theta)*drift,std::sin(theta)*distance+std::cos(theta)*drift),occupied,identity);
                resource(row,point);
            }
        }
    }
    // Namespace objects remain selectable without displacing the reference topology.
    for (const auto& row:rows) if (row.kind=="Namespace") {
        const auto anchor=namespaceAnchors.constFind(row.cluster+"/"+row.name);
        resource(row,anchor!=namespaceAnchors.cend() ? anchor.value() : place({},occupied,row.identity));
    }
}
QVariantMap RadarIsland::viewPose() const { return {{"x",pan_.x()},{"y",pan_.y()},{"zoom",zoom_}}; }
bool RadarIsland::validPose(const QVariantMap& pose) {
    bool xOk=false,yOk=false,zOk=false;
    const double x=pose.value("x").toDouble(&xOk),y=pose.value("y").toDouble(&yOk),z=pose.value("zoom").toDouble(&zOk);
    const double extent=std::numeric_limits<int>::max()*step/4;
    return pose.size()==3 && xOk && yOk && zOk && std::isfinite(x) && std::isfinite(y) && std::isfinite(z) && std::abs(x)<extent && std::abs(y)<extent && z>=.55 && z<=3.4;
}
void RadarIsland::setViewPose(const QVariantMap& pose) {
    if (!validPose(pose)) return;
    const double x=pose.value("x").toDouble(),y=pose.value("y").toDouble(),z=pose.value("zoom").toDouble();
    if (pan_==QPointF(x,y) && zoom_==z) return;
    pan_={x,y}; zoom_=z; project(); update(); emit viewPoseChanged();
}
bool RadarIsland::pan(double dx,double dy) {
    if (!std::isfinite(dx) || !std::isfinite(dy)) return false;
    setViewPose({{"x",pan_.x()+dx/zoom_},{"y",pan_.y()+dy/zoom_},{"zoom",zoom_}}); return true;
}
bool RadarIsland::zoomAt(double factor,double x,double y) {
    if (!std::isfinite(factor) || factor<=0 || !std::isfinite(x) || !std::isfinite(y)) return false;
    const double next=std::clamp(zoom_*factor,.55,3.4);
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
    setViewPose({{"x",-point->x()},{"y",-point->y()},{"zoom",zoom>0 ? std::clamp(zoom,.55,3.4) : zoom_}}); return true;
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
    if (path!=currentPath_) { currentPath_=path; emit currentResourceChanged(); }
    return true;
}
bool RadarIsland::resetView() { setViewPose({{"x",0},{"y",0},{"zoom",1}}); return true; }
QRectF RadarIsland::worldViewport() const { return {QPointF(-width()/2/zoom_,-height()/2/zoom_)-pan_,QSizeF(width()/zoom_,height()/zoom_)}; }
void RadarIsland::project(bool refresh) {
    QList<RadarTiles::Entry> visible;
    const auto* proxy=qobject_cast<QSortFilterProxyModel*>(source_.data());
    if (isVisible() && width()>0 && height()>0) {
        const auto viewport=worldViewport().adjusted(-step,-step,step,step);
        inBuckets(viewport,[&](const QPoint& area) {
            const auto found=resourceBuckets_.constFind(area); if (found==resourceBuckets_.cend()) return;
            for (const auto& path:*found) {
                const auto source=filtered_.constFind(path); if (source==filtered_.cend() || !source->isValid()) continue;
                if (source->model()!=(proxy ? proxy->sourceModel() : source_.data())) continue;
                const auto index=proxy ? proxy->mapFromSource(*source) : QModelIndex(*source);
                if (!index.isValid()) continue;
                const auto point=positions_.value(path);
                if (viewport.contains(point)) visible.append({path,index,point});
            }
        });
        std::sort(visible.begin(),visible.end(),[](const auto& a,const auto& b) { return a.path<b.path; });
    }
    tiles_.publish(visible,refresh);
}
void RadarIsland::paint(QPainter* painter) {
    const auto viewport=worldViewport();
    const auto screen=[&](QPointF point) { return QPointF(width()/2,height()/2)+(point+pan_)*zoom_; };
    const auto tile=[&](QPointF world, QColor color) {
        const auto point=screen(world); const double side=5.5*zoom_;
        painter->fillRect(QRectF(point-QPointF(side/2,side/2),QSizeF(side,side)),color);
    };
    const QColor dimmed("#50575B");
    inBuckets(viewport.adjusted(-step,-step,step,step),[&](const QPoint& area) {
        const auto found=resourceBuckets_.constFind(area); if (found==resourceBuckets_.cend()) return;
        for (const auto& path:*found) {
            if (filtered_.contains(path)) continue;
            const auto point=positions_.value(path);
            if (viewport.adjusted(-step,-step,step,step).contains(point)) tile(point,dimmed);
        }
    });
    for (const auto& marker:markers_) {
        if (!viewport.adjusted(-50,-20,50,20).contains(marker.world)) continue;
        tile(marker.world,groups_.contains(marker.group) ? terrainColor(marker.kind) : dimmed);
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
