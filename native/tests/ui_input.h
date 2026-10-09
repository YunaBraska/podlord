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
/** Open the actual workspace menu before operating its non-primary actions. */
inline bool revealWorkspaceAction(QQuickWindow* window, const QString& name) {
    if (name != "refreshButton" && name != "commandPaletteButton" && name != "resourceFindButton"
        && name != "eventFindButton" && name != "resourceColumnsButton" && name != "eventColumnsButton") return true;
    if (visibleItem(window->contentItem(), name)) return true;
    auto* trigger = visibleItem(window->contentItem(), "workspaceActionsButton");
    if (!trigger || !trigger->isEnabled()) return false;
    trigger->ensurePolished();
    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
        trigger->mapToScene({trigger->width()/2, trigger->height()/2}).toPoint());
    return QTest::qWaitFor([&] { return visibleItem(window->contentItem(), name) != nullptr; }, 2000);
}
/** Reveal a control by real wheel input before sending a public pointer action. */
inline bool scrollIntoView(QQuickWindow* window, QQuickItem* target) {
    const auto pointerBounds = [](const QRectF& bounds, const QRectF& viewport) {
        return QRectF(bounds.width() > viewport.width() ? bounds.center().x() : bounds.x(),
                      bounds.height() > viewport.height() ? bounds.center().y() : bounds.y(),
                      bounds.width() > viewport.width() ? 0 : bounds.width(),
                      bounds.height() > viewport.height() ? 0 : bounds.height());
    };
    const auto clippedBy = [target, pointerBounds]() -> QQuickItem* {
        const auto bounds = QRectF(target->mapToScene({0,0}), target->size());
        for (auto* parent = target->parentItem(); parent; parent = parent->parentItem()) {
            if (!parent->clip()) continue;
            const auto viewport = QRectF(parent->mapToScene({0,0}), parent->size());
            const auto required = pointerBounds(bounds, viewport);
            const auto visible = viewport.adjusted(-1,-1,1,1);
            if (!visible.contains(required.topLeft()) || !visible.contains(required.bottomRight())) return parent;
        }
        return nullptr;
    };
    if (QGuiApplication::platformName()=="cocoa") {
        window->raise();
        window->requestActivate();
    }
    const QPointer<QQuickItem> retained(target);
    QSignalSpy initialFrame(window,&QQuickWindow::frameSwapped);
    window->update();
    (void)initialFrame.wait(1000);
    if (!retained) return false;
    QElapsedTimer clock; clock.start();
    bool wheeled = false;
    QPointF wheelPosition;
    for (int attempt=0; attempt<48; ++attempt) {
        if (!retained) return false;
        const auto bounds=QRectF(target->mapToScene({0,0}),target->size());
        auto* clipped = clippedBy();
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
            (void)frames.wait(1000);
            if (!retained) return false;
            if (clippedBy()) continue;
            const bool fits=QRectF(0,0,window->width(),window->height()).contains(target->mapToScene({target->width()/2,target->height()/2}));
            if (!fits) std::fprintf(stderr,"Pointer target %s is outside the window=%dx%d at=(%g,%g) size=%gx%g\n",qPrintable(target->objectName()),window->width(),window->height(),bounds.x(),bounds.y(),bounds.width(),bounds.height());
            return fits;
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
/** Reacquire a named delegate when wheel/layout work reuses its old item for another row. */
inline bool clickVisible(QQuickWindow* window, const QString& name) {
    for (int attempt=0;attempt<4;++attempt) {
        const QPointer<QQuickItem> target(visibleItem(window->contentItem(),name));
        if (!target || !target->isEnabled() || !scrollIntoView(window,target)) return false;
        if (!target || target->objectName()!=name) continue;
        QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,target->mapToScene({target->width()/2,target->height()/2}).toPoint());
        return true;
    }
    return false;
}
/** Select the visible desktop tab or the compact section picker through input. */
inline bool selectSettingsSection(QQuickWindow* window, const QString& section) {
    const QString name = section == "alerts" ? "alertsWorkspaceButton" : "settings" + section.left(1).toUpper() + section.mid(1) + "Section";
    auto* control = visibleItem(window->contentItem(), name);
    if (control) {
        if (!control->isEnabled() || !scrollIntoView(window, control)) return false;
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, control->mapToScene({control->width()/2, control->height()/2}).toPoint());
        return true;
    }
    auto* picker = visibleItem(window->contentItem(), "settingsSectionPicker");
    const QStringList order{"alerts", "appearance", "diagnostics", "graphics", "privacy", "sources", "sync", "workspace", "about"};
    const int row = order.indexOf(section);
    if (!picker || !picker->isEnabled() || row < 0 || !scrollIntoView(window, picker)) return false;
    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, picker->mapToScene({picker->width()/2, picker->height()/2}).toPoint());
    QTest::keyClick(window, Qt::Key_Home);
    for (int i = 0; i < row; ++i) QTest::keyClick(window, Qt::Key_Down);
    QTest::keyClick(window, Qt::Key_Return);
    return QTest::qWaitFor([&] { return picker->property("currentValue").toString() == section; });
}
}
