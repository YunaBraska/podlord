#pragma once
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QQuickItem>
#include <QQuickWindow>
#include <QPointer>
#include <QSignalSpy>
#include <QWheelEvent>
#include <QTest>
#include <cstdio>

namespace podlord::test {
/** Locate rendered delegates, whose QObject owner need not be the window. */
inline QQuickItem* visibleItem(QQuickItem* root, const QString& name) {
    if (!root || !root->isVisible()) return nullptr;
    if (root->objectName() == name) return root;
    for (auto* child : root->childItems())
        if (auto* found = visibleItem(child, name)) return found;
    return nullptr;
}
/** Reveal a control by real wheel input before sending a public pointer action. */
inline bool scrollIntoView(QQuickWindow* window, QQuickItem* target) {
    const auto pointerBounds = [](const QRectF& bounds, const QRectF& viewport) {
        return QRectF(bounds.width() > viewport.width() ? bounds.center().x() : bounds.x(),
                      bounds.height() > viewport.height() ? bounds.center().y() : bounds.y(),
                      bounds.width() > viewport.width() ? 0 : bounds.width(),
                      bounds.height() > viewport.height() ? 0 : bounds.height());
    };
    if (QGuiApplication::platformName()=="cocoa") {
        window->raise();
        window->requestActivate();
    }
    const QPointer<QQuickItem> retained(target);
    QSignalSpy initialFrame(window,&QQuickWindow::frameSwapped);
    window->update();
    if (!initialFrame.wait(1000) || !retained) return false;
    QElapsedTimer clock; clock.start();
    bool wheeled = false;
    QPointF wheelPosition;
    for (int attempt=0; attempt<48; ++attempt) {
        if (!retained) return false;
        const auto bounds=QRectF(target->mapToScene({0,0}),target->size());
        QQuickItem* clipped=nullptr;
        for (auto* parent=target->parentItem(); parent; parent=parent->parentItem()) {
            const auto viewport=QRectF(parent->mapToScene({0,0}),parent->size());
            const auto required = pointerBounds(bounds, viewport);
            const auto visible = viewport.adjusted(-1,-1,1,1);
            if (parent->clip() && (!visible.contains(required.topLeft()) || !visible.contains(required.bottomRight()))) { clipped=parent; break; }
        }
        if (!clipped) {
            if (wheeled) {
                QWheelEvent end(wheelPosition,window->mapToGlobal(wheelPosition.toPoint()),{},{},Qt::NoButton,Qt::NoModifier,Qt::ScrollEnd,false);
                QCoreApplication::sendEvent(window,&end); wheeled=false;
            }
            const bool settled=QTest::qWaitFor([&] {
                if (!retained) return true;
                for (auto* parent=target->parentItem(); parent; parent=parent->parentItem())
                    if (parent->property("moving").toBool()) return false;
                return QRectF(0,0,window->width(),window->height()).contains(target->mapToScene({target->width()/2,target->height()/2}));
            },2000);
            if (!settled || !retained) {
                if (retained) {
                    const auto current=QRectF(target->mapToScene({0,0}),target->size());
                    std::fprintf(stderr,"Pointer target %s did not settle: initial=(%g,%g,%g,%g) current=(%g,%g,%g,%g) window=%dx%d\n",qPrintable(target->objectName()),bounds.x(),bounds.y(),bounds.width(),bounds.height(),current.x(),current.y(),current.width(),current.height(),window->width(),window->height());
                    for (auto* parent=target->parentItem(); parent; parent=parent->parentItem()) if (parent->clip() || parent->property("moving").toBool()) {
                        const auto area=QRectF(parent->mapToScene({0,0}),parent->size());
                        std::fprintf(stderr,"  parent=%s clip=%d moving=%d viewport=(%g,%g,%g,%g)\n",qPrintable(parent->objectName()),parent->clip(),parent->property("moving").toBool(),area.x(),area.y(),area.width(),area.height());
                    }
                }
                return false;
            }
            QSignalSpy frames(window,&QQuickWindow::frameSwapped); window->update();
            const bool painted=frames.wait(1000);
            if (!retained) return false;
            const bool fits=retained && QRectF(0,0,window->width(),window->height()).contains(target->mapToScene({target->width()/2,target->height()/2}));
            if (!painted || !fits) std::fprintf(stderr,"Pointer target %s: frame=%d inside-window=%d window=%dx%d at=(%g,%g) size=%gx%g\n",qPrintable(target->objectName()),painted,fits,window->width(),window->height(),bounds.x(),bounds.y(),bounds.width(),bounds.height());
            return painted && fits;
        }
        const auto viewport=QRectF(clipped->mapToScene({0,0}),clipped->size());
        if (attempt==47) {
            std::fprintf(stderr,"Cannot reveal pointer target %s at=(%g,%g) size=%gx%g; clip %s at=(%g,%g) size=%gx%g.\n",qPrintable(target->objectName()),bounds.x(),bounds.y(),bounds.width(),bounds.height(),qPrintable(clipped->objectName()),viewport.x(),viewport.y(),viewport.width(),viewport.height());
            const auto capture=qEnvironmentVariable("PODLORD_INPUT_FAILURE_FRAME");
            if (!capture.isEmpty()) window->grabWindow().save(capture+"-"+target->objectName()+"-"+qEnvironmentVariable("QT_QUICK_CONTROLS_STYLE")+".png");
        }
        const auto point=viewport.center();
        const auto required = pointerBounds(bounds, viewport);
        const QPoint delta(required.left()<viewport.left() ? 120 : required.right()>viewport.right() ? -120 : 0,
                           required.top()<viewport.top() ? 120 : required.bottom()>viewport.bottom() ? -120 : 0);
        QWheelEvent wheel(point,window->mapToGlobal(point.toPoint()),{},delta,Qt::NoButton,Qt::NoModifier,Qt::NoScrollPhase,false);
        wheel.setTimestamp(static_cast<ulong>(clock.msecsSinceReference()+clock.elapsed()));
        QCoreApplication::sendEvent(window,&wheel);
        wheeled=true; wheelPosition=point;
        QTest::qWait(20);
    }
    return false;
}
}
