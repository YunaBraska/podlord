#include "workspace.h"
#include "ui_input.h"
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QTest>
#include <cstdio>
#include <stdexcept>

namespace {
bool require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
    return true;
}
QQuickItem* findItem(QQuickItem* root, const QString& name) {
    if (root->objectName() == name) return root;
    for (auto* child : root->childItems()) if (auto* found = findItem(child, name)) return found;
    return nullptr;
}
}

int main(int argc, char** argv) {
    QGuiApplication application(argc, argv);
    try {
        require(argc == 2, "Expected viewport width.");
        bool valid = false;
        const int width = QString::fromLocal8Bit(argv[1]).toInt(&valid);
        require(valid && (width == 320 || width == 390 || width == 1440), "Unsupported viewport width.");
        QTemporaryDir directory;
        require(directory.isValid(), "Cannot create private profile.");
        podlord::Workspace workspace(directory.filePath("profile"));
        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty("workspace", &workspace);
        engine.load(QUrl("qrc:/podlord/Main.qml"));
        require(!engine.rootObjects().isEmpty(), "Cannot load real application UI.");
        auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
        require(window, "Application window is unavailable.");
        window->resize(width, 720);
        require(QTest::qWaitFor([&] { return workspace.pulseMetrics().size() == 5; }, 5000), "Public HUD metrics did not settle.");
        const auto metrics = workspace.pulseMetrics();
        require(metrics.size() == 5, "Expected public CPU, memory, storage, pod and node metrics.");
        const auto fits = [&] {
            auto* strip=findItem(window->contentItem(),"headerMetrics");
            if (!strip || strip->property("visibleCount").toInt()!=0 || !strip->property("overflowMetrics").toList().isEmpty()) return false;
            for (const auto& value:metrics) {
                const auto metric=value.toMap();
                if (metric.value("available").toBool() || metric.value("usage").toDouble()>0) return false;
                const auto* card=findItem(window->contentItem(),"pulse_"+metric.value("id").toString());
                if (card && card->isVisible()) return false;
            }
            return true;
        };
        require(QTest::qWaitFor(fits, 3000), "Empty sessions must not display unavailable or zero-count HUD cards.");
        for (const auto* name : {"resourcesWorkspaceButton", "eventsWorkspaceButton", "portForwardTasksButton"})
            require(!findItem(window->contentItem(),name)->isVisible(),"Context-only navigation appeared without a context.");
        QList<QRectF> navigationBounds;
        for (const auto* name : {"resourcesWorkspaceButton", "eventsWorkspaceButton", "portForwardTasksButton", "settingsWorkspaceButton"}) {
            auto* button = findItem(window->contentItem(), name);
            require(button && !button->property("showText").toBool(),
                "Navigation must retain compact icons with accessible labels.");
            if (!button->isVisible()) continue;
            const auto rectangle = button->mapRectToScene(button->boundingRect());
            require(rectangle.width() >= 32 && rectangle.left() >= 0 && rectangle.right() <= window->width() + 0.5,
                "Desktop navigation label is clipped.");
            for (const auto& previous : navigationBounds)
                require(!previous.intersects(rectangle), "Desktop navigation labels overlap.");
            navigationBounds.append(rectangle);
        }
        if (width == 390) {
            workspace.setWorkspacePage("resources");
            window->resize(844, 390);
            require(QTest::qWaitFor([&] {
                auto* radar = findItem(window->contentItem(), "resourceRadar");
                return radar && !radar->isVisible();
            }, 3000), "Compact landscape must leave space for content instead of the small Radar.");
            auto* emptyRadar = findItem(window->contentItem(), "emptyRadar");
            require(emptyRadar && QTest::qWaitFor([&] {
                if (!emptyRadar->isVisible()) return true;
                const auto bounds = emptyRadar->mapRectToScene(emptyRadar->boundingRect());
                const auto container = emptyRadar->parentItem()->mapRectToScene(emptyRadar->parentItem()->boundingRect());
                if (!container.adjusted(-0.5, -0.5, 0.5, 0.5).contains(bounds)) return false;
                for (const auto* name : {"radarZoomOut", "radarZoom"}) {
                    auto* control = findItem(window->contentItem(), name);
                    if (!control || !control->isVisible() || bounds.intersects(control->mapRectToScene(control->boundingRect()))) return false;
                }
                return true;
            }, 3000), "Landscape radar empty-state text overlaps its controls or is clipped.");
            window->resize(width, 720);
            require(QTest::qWaitFor(fits, 3000), "HUD metrics did not recover after landscape resize.");
        }
        const auto navigate = [&](const char* buttonName, const char* page) {
            return podlord::test::clickVisible(window,buttonName)
                && QTest::qWaitFor([&] { return workspace.workspacePage()==page; },3000);
        };
        require(navigate("settingsWorkspaceButton", "settings") && navigate("mapWorkspaceButton", "map"),
            "Visible navigation did not open its workspace.");
        const auto evidence = qEnvironmentVariable("PODLORD_UI_EVIDENCE_DIR");
        if (!evidence.isEmpty()) {
            workspace.setWorkspacePage("settings");
            QCoreApplication::processEvents();
            const auto image = window->grabWindow();
            require(!image.isNull() && image.save(evidence + "/hud-" + QString::number(width) + "-" + qEnvironmentVariable("QT_QUICK_CONTROLS_STYLE") + ".png"), "Cannot save real HUD evidence.");
        }
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}
