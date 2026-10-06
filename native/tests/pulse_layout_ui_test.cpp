#include "workspace.h"
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
            QList<QRectF> bounds;
            for (const auto& value : metrics) {
                const auto metric = value.toMap();
                auto* card = findItem(window->contentItem(), "pulse_" + metric["id"].toString());
                if (!card || !card->isVisible() || !card->isEnabled() || card->width() < 62 || card->height() < 36) return false;
                const auto rectangle = card->mapRectToScene(card->boundingRect());
                if (rectangle.left() < 0 || rectangle.right() > window->width() + 0.5
                    || rectangle.top() < 0 || rectangle.bottom() > window->height()) return false;
                // A control outside its clipping parent is not an accessible HUD card.
                for (auto* parent = card->parentItem(); parent; parent = parent->parentItem()) {
                    if (parent->clip() && !parent->mapRectToScene(parent->boundingRect()).adjusted(-0.5, -0.5, 0.5, 0.5).contains(rectangle)) return false;
                }
                for (const auto& previous : bounds) if (previous.intersects(rectangle)) return false;
                bounds.append(rectangle);
            }
            if (width==1440) {
                for (const auto& rectangle:bounds)
                    if (qAbs(rectangle.top()-bounds.first().top())>0.5) return false;
            }
            return true;
        };
        require(QTest::qWaitFor(fits, 3000), "HUD metrics are clipped, overlapping or unreachable.");
        auto* cpu = findItem(window->contentItem(), "pulse_cpu");
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
            cpu->mapToScene(QPointF(cpu->width() / 2, cpu->height() / 2)).toPoint());
        require(QTest::qWaitFor([&] { return workspace.workspacePage() == "dashboard"; }, 3000), "HUD no longer opens the real dashboard.");
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
