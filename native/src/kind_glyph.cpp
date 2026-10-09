#include "kind_glyph.h"
#include <QHash>
#include <QPainter>
#include <QPainterPath>
#include <algorithm>

namespace podlord {
namespace {
enum class Fill { None, Main, Ink };
struct Part { QPainterPath path; Fill fill; bool outline; };
using Glyph = QList<Part>;
const QHash<QString, Glyph>& catalog() {
    static const auto value = [] {
        QHash<QString, Glyph> result;
        Glyph parts;
        const auto polygon = [&](std::initializer_list<QPointF> points, Fill fill = Fill::Main) {
            QPainterPath path; auto point = points.begin(); path.moveTo(*point++);
            for (; point != points.end(); ++point) path.lineTo(*point);
            path.closeSubpath();
            parts.append({path, fill, true});
        };
        const auto line = [&](double x, double y, double endX, double endY) {
            QPainterPath path; path.moveTo(x, y); path.lineTo(endX, endY);
            parts.append({path, Fill::None, true});
        };
        const auto rect = [&](double x, double y, double w, double h, Fill fill = Fill::Main, bool outline = true) {
            QPainterPath path; path.addRect(x, y, w, h); parts.append({path, fill, outline});
        };
        const auto ellipse = [&](double x, double y, double rx, double ry, Fill fill = Fill::Main, bool outline = true) {
            QPainterPath path; path.addEllipse(QPointF(x, y), rx, ry); parts.append({path, fill, outline});
        };
        const auto finish = [&](const QStringList& kinds) { for (const auto& kind : kinds) result.insert(kind, parts); parts.clear(); };
        const auto diamond = [&] { polygon({{.5,.06},{.94,.5},{.5,.94},{.06,.5}}); };
        diamond(); finish({""});
        rect(0,0,1,1); line(.5,.1,.5,.9); line(.1,.5,.9,.5); finish({"Namespace"});
        ellipse(.5,.5,.38,.38); line(.5,.12,.5,.88); line(.12,.5,.88,.5);
        ellipse(.5,.5,.09,.09,Fill::Ink,false); finish({"Cluster"});
        rect(.12,.34,.76,.44); for (const auto x : {.24,.44,.64}) rect(x,.48,.12,.12,Fill::Ink,false);
        line(.5,.34,.5,.12); line(.38,.18,.5,.12); line(.62,.18,.5,.12); finish({"Node"});
        polygon({{.5,.06},{.88,.28},{.88,.72},{.5,.94},{.12,.72},{.12,.28}});
        ellipse(.5,.5,.12,.12,Fill::Ink,false); finish({"Pod"});
        polygon({{.1,.82},{.1,.42},{.32,.26},{.44,.42},{.62,.28},{.78,.44},{.9,.44},{.9,.82}});
        rect(.68,.12,.16,.28); rect(.24,.58,.14,.14,Fill::Ink,false); rect(.48,.58,.14,.14,Fill::Ink,false); finish({"Deployment"});
        rect(.2,.16,.6,.2); rect(.16,.4,.68,.2); rect(.12,.64,.76,.2); finish({"ReplicaSet"});
        ellipse(.5,.26,.34,.14); rect(.16,.26,.68,.44,Fill::Main,false);
        line(.16,.26,.16,.7); line(.84,.26,.84,.7); ellipse(.5,.7,.34,.14); line(.2,.48,.8,.48);
        finish({"StatefulSet","PersistentVolume","PersistentVolumeClaim"});
        diamond(); line(.5,.22,.5,.78); line(.22,.5,.78,.5); finish({"DaemonSet"});
        diamond(); ellipse(.5,.5,.1,.1,Fill::Ink,false); finish({"Job"});
        ellipse(.5,.5,.38,.38); line(.5,.5,.5,.24); line(.5,.5,.7,.62); finish({"CronJob"});
        ellipse(.5,.5,.18,.18); line(.5,.32,.5,.08); line(.38,.62,.16,.86); line(.62,.62,.84,.86);
        ellipse(.5,.08,.08,.08); ellipse(.16,.86,.08,.08); ellipse(.84,.86,.08,.08); finish({"Service"});
        rect(.14,.42,.72,.42); rect(.34,.56,.32,.28,Fill::Ink,false); line(.2,.42,.5,.14); line(.8,.42,.5,.14);
        finish({"Ingress","Gateway","HTTPRoute","GRPCRoute"});
        line(.18,.5,.5,.22); line(.5,.22,.82,.5); line(.5,.22,.5,.82);
        ellipse(.18,.5,.11,.11); ellipse(.5,.22,.11,.11); ellipse(.82,.5,.11,.11); ellipse(.5,.82,.11,.11); finish({"EndpointSlice"});
        polygon({{.5,.08},{.86,.22},{.78,.68},{.5,.92},{.22,.68},{.14,.22}}); line(.5,.18,.5,.78); finish({"NetworkPolicy"});
        polygon({{.22,.1},{.62,.1},{.82,.3},{.82,.9},{.22,.9}});
        line(.62,.1,.62,.3); line(.62,.3,.82,.3); line(.34,.48,.7,.48); line(.34,.64,.7,.64); finish({"ConfigMap"});
        rect(.18,.46,.64,.4); polygon({{.32,.46},{.32,.28},{.5,.14},{.68,.28},{.68,.46}},Fill::None);
        rect(.46,.62,.08,.12,Fill::Ink,false); finish({"Secret"});
        ellipse(.5,.28,.17,.17); polygon({{.18,.88},{.28,.56},{.72,.56},{.82,.88}}); finish({"ServiceAccount"});
        polygon({{.58,.04},{.24,.5},{.5,.5},{.38,.96},{.78,.4},{.52,.4}}); finish({"Event"});
        polygon({{.5,.08},{.86,.28},{.5,.48},{.14,.28}});
        polygon({{.14,.28},{.5,.48},{.5,.9},{.14,.7}});
        polygon({{.86,.28},{.5,.48},{.5,.9},{.86,.7}}); finish({"CustomResourceDefinition"});
        ellipse(.4,.4,.27,.27,Fill::None); line(.6,.6,.9,.9); finish({"Search"});
        line(.2,.2,.8,.8); line(.2,.8,.8,.2); finish({"Close"});
        for (const auto y : {.25,.5,.75}) line(.15,y,.85,y); finish({"Menu"});
        rect(.15,.15,.7,.7,Fill::None); line(.6,.15,.6,.85); finish({"Sidebar"});
        rect(.15,.15,.7,.7,Fill::None); line(.38,.15,.38,.85); line(.62,.15,.62,.85); finish({"Columns"});
        line(.7,.2,.3,.5); line(.3,.5,.7,.8); finish({"Previous"});
        line(.3,.2,.7,.5); line(.7,.5,.3,.8); finish({"Next"});
        line(.2,.5,.8,.5); finish({"ZoomOut"});
        line(.2,.5,.8,.5); line(.5,.2,.5,.8); finish({"ZoomIn"});
        QPainterPath reset; reset.arcMoveTo(.18,.18,.64,.64,135); reset.arcTo(.18,.18,.64,.64,135,-290);
        parts.append({reset,Fill::None,true}); line(.15,.12,.15,.42); line(.15,.42,.45,.42); finish({"Reset"});
        for (const auto y : {.25,.5,.75}) line(.15,y,.85,y);
        ellipse(.35,.25,.07,.07); ellipse(.65,.5,.07,.07); ellipse(.4,.75,.07,.07); finish({"Filters"});
        polygon({{.14,.36},{.35,.36},{.62,.15},{.62,.85},{.35,.64},{.14,.64}},Fill::None);
        line(.75,.35,.9,.5); line(.9,.5,.75,.65); finish({"Volume"});
        polygon({{.14,.36},{.35,.36},{.62,.15},{.62,.85},{.35,.64},{.14,.64}},Fill::None);
        line(.72,.35,.92,.65); line(.72,.65,.92,.35); finish({"Mute"});
        polygon({{.2,.68},{.68,.2},{.82,.34},{.34,.82},{.16,.86}},Fill::None); finish({"Pencil"});
        polygon({{.08,.5},{.28,.28},{.5,.2},{.72,.28},{.92,.5},{.72,.72},{.5,.8},{.28,.72}},Fill::None);
        ellipse(.5,.5,.13,.13,Fill::Main,false);
        result.insert("Visible", parts);
        line(.16,.84,.84,.16); finish({"Hidden"});
        return result;
    }();
    return value;
}
}
KindGlyph::KindGlyph(QQuickItem* parent) : QQuickPaintedItem(parent) { setAntialiasing(true); }
void KindGlyph::setKind(const QString& value) {
    if (kind_ == value) return;
    kind_ = value; update(); emit kindChanged();
}
void KindGlyph::setFill(const QColor& value) {
    if (!value.isValid() || fill_ == value) return;
    fill_ = value; update(); emit fillChanged();
}
void KindGlyph::setStroke(const QColor& value) {
    if (!value.isValid() || stroke_ == value) return;
    stroke_ = value; update(); emit strokeChanged();
}
void KindGlyph::paint(QPainter* painter) {
    draw(painter, boundingRect(), kind_, fill_, stroke_);
}
void KindGlyph::draw(QPainter* painter, const QRectF& rect, const QString& kind, const QColor& fill, const QColor& stroke) {
    const auto side = std::min(rect.width(), rect.height());
    if (side <= 0) return;
    const auto scale = side * .84;
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    painter->translate(rect.x() + (rect.width() - side) / 2 + side * .08, rect.y() + (rect.height() - side) / 2 + side * .08);
    painter->scale(scale, scale);
    const QPen pen(stroke, std::max(1., side * .07) / scale, Qt::SolidLine, Qt::FlatCap, Qt::MiterJoin);
    const auto& glyphs = catalog();
    const auto found = glyphs.constFind(kind);
    const auto& parts = found == glyphs.cend() ? glyphs.constFind(QString{}).value() : found.value();
    for (const auto& part : parts) {
        painter->setPen(part.outline ? pen : QPen(Qt::NoPen));
        painter->setBrush(part.fill == Fill::None ? QBrush(Qt::NoBrush) : QBrush(part.fill == Fill::Main ? fill : stroke));
        painter->drawPath(part.path);
    }
    painter->restore();
}
}
