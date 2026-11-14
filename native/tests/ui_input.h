#pragma once
#include "alerts.h"
#include <QAbstractItemModel>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QColor>
#include <QImage>
#include <QQmlEngine>
#include <QQmlExpression>
#include <QQuickItem>
#include <QQuickWindow>
#include <QPointer>
#include <QSignalSpy>
#include <QWheelEvent>
#include <QTest>
#include <cstdio>
#include <optional>

namespace podlord::test {
/** Configure an isolated UI profile through the real preference API before loading resources. */
inline bool muteAlerts(Alerts* alerts) {
    if (!alerts || !QTest::qWaitFor([&] { return !alerts->busy(); }, 8000)) return false;
    if (alerts->muted()) return true;
    return alerts->setPreferences(true,alerts->reducedMotion())
        && QTest::qWaitFor([&] { return !alerts->busy() && alerts->muted(); }, 8000);
}
#ifdef Q_OS_IOS
bool orientIosTestWindow(QQuickWindow* window);
#endif
inline QRectF visibleBounds(QQuickWindow* window, QQuickItem* control) {
    auto bounds = control->mapRectToScene(QRectF(QPointF{}, control->size()));
    for (auto* parent = control->parentItem(); parent; parent = parent->parentItem())
        if (parent->clip()) bounds = bounds.intersected(parent->mapRectToScene(QRectF(QPointF{}, parent->size())));
    return bounds.intersected(QRectF(0, 0, window->width(), window->height()));
}
/** Sample the actual window, including native parent painting rather than only child graphics. */
inline QColor centerPixel(QQuickWindow* window, QQuickItem* control) {
    if (!window || !control || !window->isVisible() || !control->isVisible() || window->width()<=0 || control->width()<=0 || control->height()<=0) return {};
    const auto center=control->mapToScene(QPointF(control->width()/2,control->height()/2));
    if (!visibleBounds(window,control).contains(center)) return {};
    const auto frame=window->grabWindow();
    const double ratio=double(frame.width())/window->width();
    const QPoint pixel(qRound(center.x()*ratio),qRound(center.y()*ratio));
    return frame.rect().contains(pixel) ? frame.pixelColor(pixel) : QColor{};
}
/** Locate rendered delegates, whose QObject owner need not be the window. */
inline QQuickItem* visibleItem(QQuickItem* root, const QString& name) {
    if (!root || !root->isVisible()) return nullptr;
    if (root->objectName() == name) return root;
    for (auto* child : root->childItems())
        if (auto* found = visibleItem(child, name)) return found;
    return nullptr;
}
struct RadarPoint final { QRectF bounds; QString path; };
/** Read the published projection without manufacturing widgets or changing selection. */
inline std::optional<RadarPoint> radarPoint(QQuickItem* radar,int resourceIndex) {
    if (!radar || !radar->isVisible()) return {};
    auto* model=radar->property("tiles").value<QAbstractItemModel*>();
    if (!model) return {};
    int indexRole=-1,xRole=-1,yRole=-1;
    const auto roles=model->roleNames();
    for (auto role=roles.cbegin();role!=roles.cend();++role) {
        if (role.value()=="resourceIndex") indexRole=role.key();
        if (role.value()=="worldX") xRole=role.key();
        if (role.value()=="worldY") yRole=role.key();
    }
    if (indexRole<0 || xRole<0 || yRole<0) return {};
    const auto pose=radar->property("viewPose").toMap();
    const double zoom=pose.value("zoom").toDouble(),side=5.5*zoom;
    for (int row=0;row<model->rowCount();++row) {
        const auto index=model->index(row,0);
        if (index.data(indexRole).toInt()!=resourceIndex) continue;
        const QPointF center(radar->width()/2+(index.data(xRole).toDouble()+pose.value("x").toDouble())*zoom,
            radar->height()/2+(index.data(yRole).toDouble()+pose.value("y").toDouble())*zoom);
        return RadarPoint{QRectF(center-QPointF(side/2,side/2),QSizeF(side,side)),index.data(Qt::UserRole).toString()};
    }
    return {};
}
inline bool radarPointVisible(QQuickItem* radar,int resourceIndex) {
    const auto point=radarPoint(radar,resourceIndex);
    return point && QRectF(QPointF{},radar->size()).adjusted(-.5,-.5,.5,.5).contains(point->bounds);
}
/** Return only an actual shared input target, never a proxy for an absent point. */
inline QQuickItem* radarTarget(QQuickWindow* window,int resourceIndex) {
    if (!window) return nullptr;
    for (const auto* name:{"radarFocusTarget","radarHoverTarget"}) {
        auto* target=visibleItem(window->contentItem(),name);
        if (target && target->property("resourceIndex").toInt()==resourceIndex) return target;
    }
    return nullptr;
}
inline QColor radarPixel(QQuickWindow* window,int resourceIndex) {
    if (!window || !window->isVisible() || window->width()<=0) return {};
    auto* radar=visibleItem(window->contentItem(),"resourceRadar");
    const auto point=radarPoint(radar,resourceIndex);
    if (!point || !radarPointVisible(radar,resourceIndex)) return {};
    const auto center=radar->mapToScene(point->bounds.center());
    const auto image=window->grabWindow();
    const double ratio=double(image.width())/window->width();
    const QPoint pixel(qRound(center.x()*ratio),qRound(center.y()*ratio));
    return image.rect().contains(pixel) ? image.pixelColor(pixel) : QColor{};
}
/** Wait for the public TableView column mapping before retaining a delegate. */
inline bool tableColumnsReady(QQuickItem* view, const QVariantList& columns) {
    if (!view || !view->isVisible() || columns.isEmpty()) return false;
    QStringList checks;
    checks.append(QString("columns === %1").arg(columns.size()));
    int position = 0;
    for (const bool pinned : {true, false}) {
        for (const auto& value : columns) {
            const auto column = value.toMap();
            if ((column["visible"].toBool() && column["pinned"].toBool()) != pinned) continue;
            checks.append(QString("columnAtIndex(model.index(0, %1)) === %2")
                .arg(column["column"].toInt()).arg(position++));
        }
    }
    QQmlExpression expression(qmlContext(view), view, checks.join(" && "));
    const auto ready = expression.evaluate();
    if (expression.hasError()) std::fprintf(stderr, "Table column lookup failed: %s\n", qPrintable(expression.error().toString()));
    return !expression.hasError() && ready.toBool();
}
/** Resolve the live TableView delegate, not an old pooled item with the same name. */
inline QQuickItem* tableCell(QQuickItem* view, int row, int column) {
    if (!view || !view->isVisible() || view->width() <= 0 || row < 0 || column < 0) return nullptr;
    QQmlExpression expression(qmlContext(view), view, QString("itemAtIndex(model.index(%1, %2))").arg(row).arg(column));
    auto* cell = expression.evaluate().value<QQuickItem*>();
    if (expression.hasError()) std::fprintf(stderr, "Table cell lookup failed: %s\n", qPrintable(expression.error().toString()));
    return cell;
}
/** Open the actual workspace menu before operating its non-primary actions. */
inline bool revealWorkspaceAction(QQuickWindow* window, const QString& name) {
    if (name != "refreshButton" && name != "commandPaletteButton" && name != "resourceFindButton"
        && name != "eventFindButton" && name != "resourceColumnsButton" && name != "eventColumnsButton"
        && name != "workspaceSearchButton" && name != "quickOpenDropdown" && name != "mapWorkspaceButton"
        && name != "resourcesWorkspaceButton" && name != "eventsWorkspaceButton" && name != "portForwardTasksButton"
        && name != "settingsWorkspaceButton" && name != "toggleSidebar") return true;
    if (visibleItem(window->contentItem(), name)) return true;
    auto* trigger = visibleItem(window->contentItem(), "workspaceActionsButton");
    if (!trigger || !trigger->isEnabled()) return false;
    trigger->ensurePolished();
    QSignalSpy frame(window,&QQuickWindow::frameSwapped);
    window->update();
    if (!QTest::qWaitFor([&] { return !frame.isEmpty(); },2000)) return false;
    if (visibleItem(window->contentItem(),name)) return true;
    trigger = visibleItem(window->contentItem(),"workspaceActionsButton");
    if (!trigger || !trigger->isEnabled()) return false;
    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
        trigger->mapToScene({trigger->width()/2, trigger->height()/2}).toPoint());
    return QTest::qWaitFor([&] { return visibleItem(window->contentItem(), name) != nullptr; }, 2000);
}
/** Reveal a control by real wheel input before sending a public pointer action. */
inline bool scrollIntoView(QQuickWindow* window, QQuickItem* target, bool pointerAction = false) {
    const auto pointerBounds = [](const QRectF& bounds, const QRectF& viewport) {
        return QRectF(bounds.width() > viewport.width() ? bounds.center().x() : bounds.x(),
                      bounds.height() > viewport.height() ? bounds.center().y() : bounds.y(),
                      bounds.width() > viewport.width() ? 0 : bounds.width(),
                      bounds.height() > viewport.height() ? 0 : bounds.height());
    };
    const auto clippedBy = [target, pointerBounds, pointerAction]() -> QQuickItem* {
        const auto bounds = target->mapRectToScene(QRectF(QPointF{}, target->size()));
        for (auto* parent = target->parentItem(); parent; parent = parent->parentItem()) {
            if (!parent->clip()) continue;
            const auto viewport = parent->mapRectToScene(QRectF(QPointF{}, parent->size()));
            const auto required = pointerBounds(bounds, viewport);
            // Clicks tolerate native tab-strip subpixels; layout assertions keep their stricter bound.
            const qreal border = pointerAction ? 2 : 0;
            const auto visible = viewport.adjusted(-border,-border,border,border);
            if (!visible.contains(required.topLeft()) || !visible.contains(required.bottomRight())) return parent;
        }
        return nullptr;
    };
    if (QGuiApplication::platformName()=="cocoa") {
        window->raise();
        window->requestActivate();
    }
    const QPointer<QQuickItem> retained(target);
    for (auto* parent = target; parent; parent = parent->parentItem()) parent->ensurePolished();
    QSignalSpy frame(window, &QQuickWindow::afterAnimating);
    window->update();
    if (frame.isEmpty() && !frame.wait(2000)) return false;
    if (!QTest::qWaitFor([&] { return retained && target->width() > 0 && target->height() > 0; }, 2000)) return false;
    QElapsedTimer clock; clock.start();
    bool wheeled = false;
    QPointF wheelPosition;
    QPoint wheelDelta;
    const auto ancestorGeometry = [&] {
        const auto* focus = window->activeFocusItem();
        QStringList details{QString("  focus=\"%1\" class=%2 windowFocused=%3")
            .arg(focus ? focus->objectName() : QString("<none>"),
                 focus ? QString::fromLatin1(focus->metaObject()->className()) : QString("<none>"))
            .arg(QGuiApplication::focusWindow() == window)};
        if (!retained) { details.append("  <target destroyed>"); return details.join('\n'); }
        for (auto* parent = retained->parentItem(); parent; parent = parent->parentItem()) {
            const auto contentY = parent->property("contentY");
            if (!parent->clip() && !contentY.isValid()) continue;
            const auto area = parent->mapRectToScene(QRectF(QPointF{}, parent->size()));
            details.append(QString("  %1 name=\"%2\" clip=%3 viewport=(%4,%5,%6,%7) contentY=%8 contentHeight=%9 interactive=%10 moving=%11")
                .arg(QString::fromLatin1(parent->metaObject()->className()), parent->objectName()).arg(parent->clip())
                .arg(area.x()).arg(area.y()).arg(area.width()).arg(area.height())
                .arg(contentY.toString(), parent->property("contentHeight").toString(),
                     parent->property("interactive").toString(), parent->property("moving").toString()));
        }
        return details.join('\n');
    };
    QString firstAncestors;
    for (int attempt=0; attempt<48; ++attempt) {
        if (!QTest::qWaitFor([&] {
            if (!retained) return true;
            for (auto* parent=target->parentItem(); parent; parent=parent->parentItem())
                if (parent->property("moving").toBool()) return false;
            return true;
        },2000) || !retained) return false;
        const auto bounds=target->mapRectToScene(QRectF(QPointF{}, target->size()));
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
                    const auto current=target->mapRectToScene(QRectF(QPointF{}, target->size()));
                    std::fprintf(stderr,"Pointer target %s did not settle: initial=(%g,%g,%g,%g) current=(%g,%g,%g,%g) window=%dx%d\n",qPrintable(target->objectName()),bounds.x(),bounds.y(),bounds.width(),bounds.height(),current.x(),current.y(),current.width(),current.height(),window->width(),window->height());
                    for (auto* parent=target->parentItem(); parent; parent=parent->parentItem()) if (parent->clip() || parent->property("moving").toBool()) {
                        const auto area=parent->mapRectToScene(QRectF(QPointF{}, parent->size()));
                        std::fprintf(stderr,"  parent=%s clip=%d moving=%d viewport=(%g,%g,%g,%g)\n",qPrintable(parent->objectName()),parent->clip(),parent->property("moving").toBool(),area.x(),area.y(),area.width(),area.height());
                    }
                }
                return false;
            }
            if (clippedBy()) continue;
            const bool fits=retained && QRectF(0,0,window->width(),window->height()).contains(target->mapToScene({target->width()/2,target->height()/2}));
            if (!fits) std::fprintf(stderr,"Pointer target %s is outside the window=%dx%d at=(%g,%g) size=%gx%g\n",qPrintable(target->objectName()),window->width(),window->height(),bounds.x(),bounds.y(),bounds.width(),bounds.height());
            return fits;
        }
        const auto viewport=clipped->mapRectToScene(QRectF(QPointF{}, clipped->size()));
        if (attempt==47) {
            std::fprintf(stderr,"Cannot reveal pointer target %s at=(%g,%g) size=%gx%g; clip %s at=(%g,%g) size=%gx%g.\n",qPrintable(target->objectName()),bounds.x(),bounds.y(),bounds.width(),bounds.height(),qPrintable(clipped->objectName()),viewport.x(),viewport.y(),viewport.width(),viewport.height());
            const auto capture=qEnvironmentVariable("PODLORD_INPUT_FAILURE_FRAME");
            if (!capture.isEmpty()) window->grabWindow().save(capture+"-"+target->objectName()+"-"+qEnvironmentVariable("QT_QUICK_CONTROLS_STYLE")+".png");
        }
        const auto required = pointerBounds(bounds, viewport);
        // Avoid sending parent scrolling into a nested editor in the viewport's center.
        const QPointF point(required.top()<viewport.top() || required.bottom()>viewport.bottom()
            ? viewport.left()+qMin(8.0,viewport.width()/2) : viewport.center().x(),viewport.center().y());
        const QPoint delta(required.left()<viewport.left() ? 120 : required.right()>viewport.right() ? -120 : 0,
                           required.top()<viewport.top() ? 120 : required.bottom()>viewport.bottom() ? -120 : 0);
        QWheelEvent wheel(point,window->mapToGlobal(point.toPoint()),{},delta,Qt::NoButton,Qt::NoModifier,Qt::NoScrollPhase,false);
        wheel.setTimestamp(static_cast<ulong>(clock.msecsSinceReference()+clock.elapsed()));
        if (firstAncestors.isEmpty()) firstAncestors = ancestorGeometry();
        frame.clear();
        QCoreApplication::sendEvent(window,&wheel);
        wheeled=true; wheelPosition=point;
        wheelDelta=delta;
        window->update();
        if (!QTest::qWaitFor([&] { return !retained || !frame.isEmpty(); },2000) || !retained) return false;
    }
    std::fprintf(stderr,"Exhausted pointer reveal %s: lastWheel=(%g,%g) delta=(%d,%d)\nFirst ancestors:\n%s\nFinal ancestors:\n%s\n",
        retained ? qPrintable(retained->objectName()) : "<destroyed>",wheelPosition.x(),wheelPosition.y(),wheelDelta.x(),wheelDelta.y(),
        qPrintable(firstAncestors),qPrintable(ancestorGeometry()));
    return false;
}
/** Reacquire a named delegate when wheel/layout work reuses its old item for another row. */
inline bool clickVisible(QQuickWindow* window, const QString& name, Qt::MouseButton button = Qt::LeftButton) {
    for (int attempt=0;attempt<4;++attempt) {
        QSignalSpy frame(window,&QQuickWindow::frameSwapped);
        window->update();
        if (!QTest::qWaitFor([&] { return !frame.isEmpty(); }, 2000)) {
            qWarning() << "Pointer frame unavailable:" << name << "visible" << window->isVisible()
                << "exposed" << window->isExposed() << "active" << window->isActive();
            return false;
        }
        if (!visibleItem(window->contentItem(),name) && !revealWorkspaceAction(window,name)) return false;
        const QPointer<QQuickItem> target(visibleItem(window->contentItem(),name));
        if (!target || !target->isEnabled() || !scrollIntoView(window,target,true)) return false;
        if (!target || target->objectName()!=name) continue;
        const auto bounds = visibleBounds(window, target);
        if (bounds.isEmpty()) continue;
        frame.clear(); window->update();
        if (!QTest::qWaitFor([&] { return !frame.isEmpty(); }, 2000)) return false;
        if (!target || target->objectName()!=name || visibleBounds(window, target)!=bounds) continue;
        std::optional<QSignalSpy> activated;
        if (button == Qt::LeftButton) {
            const auto* meta = target->metaObject();
            const int triggered = meta->indexOfSignal("triggered()");
            const int clicked = meta->indexOfSignal("clicked()");
            const int signal = triggered >= 0 ? triggered : clicked;
            if (signal >= 0) activated.emplace(target, meta->method(signal));
        }
        QTest::mouseClick(window,button,Qt::NoModifier,bounds.center().toPoint());
        QCoreApplication::processEvents();
        if (activated && activated->isEmpty() && !activated->wait(2000)) {
            qWarning() << "Pointer action was not activated:" << name << "visible bounds" << bounds;
            auto* hit = window->contentItem();
            while (auto* child = hit->childAt(hit->mapFromScene(bounds.center()).x(), hit->mapFromScene(bounds.center()).y())) hit = child;
            qWarning() << "Pointer hit:" << hit->objectName() << hit->metaObject()->className();
            const auto capture = qEnvironmentVariable("PODLORD_INPUT_FAILURE_FRAME");
            if (!capture.isEmpty()) window->grabWindow().save(capture + "-click-" + name + "-" + qEnvironmentVariable("QT_QUICK_CONTROLS_STYLE") + ".png");
            return false;
        }
        return true;
    }
    return false;
}
/** Dismiss the actual modal Drawer through its public outside-tap gesture. */
inline bool closeSidebarDrawer(QQuickWindow* window) {
    auto* drawer=window ? window->findChild<QObject*>("sidebarDrawer") : nullptr;
    if (!drawer || !drawer->property("visible").toBool()) return true;
    if (!QTest::qWaitFor([&] { return drawer->property("opened").toBool(); },2000)) return false;
    const auto left=drawer->property("x").toReal();
    const auto right=left+drawer->property("width").toReal();
    const auto outside=left>1 ? left/2 : right+(window->width()-right)/2;
    QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,QPoint(qRound(outside),window->height()/2));
    return QTest::qWaitFor([&] { return !drawer->property("visible").toBool(); },2000);
}
/** Open the retained Radar through the actual workspace controls, using Map when space is compact. */
inline bool focusResourceRadar(QQuickWindow* window, bool expanded = false) {
    if (!window) return false;
    const auto* sidebar=window->findChild<QQuickItem*>("workspaceSidebar");
    const bool map = expanded || window->property("compactLandscape").toBool()
        || (sidebar && sidebar->property("suppressSmallRadar").toBool());
    const auto navigation = map ? "mapWorkspaceButton" : "resourcesWorkspaceButton";
    auto* button = visibleItem(window->contentItem(),navigation);
    if (!button || !button->property("checked").toBool()) {
        auto* drawer = window->findChild<QObject*>("sidebarDrawer");
        if (drawer && drawer->property("visible").toBool()) {
            if (!closeSidebarDrawer(window)) return false;
        }
        if (!clickVisible(window,navigation)) return false;
    }
    if (!map && !visibleItem(window->contentItem(),"resourceRadar") && !clickVisible(window,"toggleSidebar")) return false;
    auto* drawer=window->findChild<QObject*>("sidebarDrawer");
    if (drawer && drawer->property("visible").toBool()
        && !QTest::qWaitFor([&] { return drawer->property("opened").toBool(); },2000)) return false;
    if (!QTest::qWaitFor([&] { auto* radar=visibleItem(window->contentItem(),"resourceRadar");
        if (!radar || radar->width()<=0 || radar->height()<=0) return false;
        const auto bounds=radar->mapRectToScene(radar->boundingRect());
        return QRectF(0,0,window->width(),window->height()).contains(bounds)
            && visibleBounds(window,radar).contains(bounds); },2000)) return false;
    visibleItem(window->contentItem(),"resourceRadar")->forceActiveFocus(Qt::TabFocusReason);
    return true;
}
/** Select the visible desktop tab or the compact section picker through input. */
inline bool selectSettingsSection(QQuickWindow* window, const QString& section) {
#ifdef Q_OS_IOS
    QSignalSpy settled(window,&QQuickWindow::frameSwapped);
    window->update();
    if (!settled.wait(2000)) return false;
#endif
    const QString name = section == "alerts" ? "alertsWorkspaceButton" : "settings" + section.left(1).toUpper() + section.mid(1) + "Section";
    auto* control = visibleItem(window->contentItem(), name);
    if (control) {
        if (!control->isEnabled() || !scrollIntoView(window, control, true)) return false;
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, control->mapToScene({control->width()/2, control->height()/2}).toPoint());
        return true;
    }
    auto* picker = visibleItem(window->contentItem(), "settingsSectionPicker");
    const QStringList order{"alerts", "appearance", "diagnostics", "sources", "sync", "about"};
    const int row = order.indexOf(section);
    if (!picker || !picker->isEnabled() || row < 0 || !scrollIntoView(window, picker, true)) return false;
    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, picker->mapToScene({picker->width()/2, picker->height()/2}).toPoint());
#ifdef Q_OS_IOS
    if (!QTest::qWaitFor([&] {
        QQmlExpression expression(qmlContext(picker),picker,"popup.visible");
        return expression.evaluate().toBool() && !expression.hasError();
    },2000)) return false;
#endif
    QTest::keyClick(window, Qt::Key_Home);
    for (int i = 0; i < row; ++i) QTest::keyClick(window, Qt::Key_Down);
#ifdef Q_OS_IOS
    QQuickItem* option=nullptr;
    if (!QTest::qWaitFor([&] {
        QQmlExpression expression(qmlContext(picker),picker,QString("popup.contentItem.itemAtIndex(%1)").arg(row));
        option=expression.evaluate().value<QQuickItem*>();
        return !expression.hasError() && option && !visibleBounds(window,option).isEmpty();
    },2000)) return false;
    QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,visibleBounds(window,option).center().toPoint());
#else
    QTest::keyClick(window, Qt::Key_Return);
#endif
    return QTest::qWaitFor([&] { return picker->property("currentValue").toString() == section; });
}
}
