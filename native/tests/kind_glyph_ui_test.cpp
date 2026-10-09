#include "workspace.h"
#include <QGuiApplication>
#include <QImage>
#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <cstdio>

namespace {
int coloredPixels(const QImage& image, bool red) {
    int count = 0;
    for (int y = 0; y < image.height(); ++y) for (int x = 0; x < image.width(); ++x) {
        const auto color = image.pixelColor(x, y);
        if (color.alpha() > 200 && (red ? color.red() > 200 && color.blue() < 30 : color.blue() > 200 && color.red() < 30)) ++count;
    }
    return count;
}
QImage frame(QQuickWindow* window) {
    window->update(); QTest::qWait(60);
    return window->grabWindow();
}
bool exercise(const QStringList& args) {
    if (args.size() < 2 || args.size() > 4) return false;
    QTemporaryDir temporary;
    if (!temporary.isValid()) return false;
    podlord::Workspace workspace(temporary.filePath("profile"));
    QQmlApplicationEngine engine;
    QSignalSpy warnings(&engine, &QQmlApplicationEngine::warnings);
    engine.loadData(R"qml(import QtQuick
import QtQuick.Window
import Podlord.Graphics 1.0
Window {
    width: 96; height: 96; visible: true; color: "transparent"
    KindGlyph { objectName: "glyph"; width: parent.width; height: parent.height; kind: "Pod"; fill: "#ff0000"; stroke: "#000000" }
})qml", QUrl("qrc:/podlord/glyph-render-test.qml"));
    if (engine.rootObjects().size() != 1) return false;
    auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
    auto* glyph = window ? window->findChild<QQuickItem*>("glyph") : nullptr;
    if (!glyph || !QTest::qWaitFor([&] { return window->isExposed(); }, 3000)) return false;
    const auto scenario = args[1];
    if (scenario == "render" || scenario == "alias") {
        if (args.size() < 3 || !glyph->setProperty("kind", args[2])) return false;
        const auto image = frame(window);
        if (image.isNull() || coloredPixels(image, true) < 30 || image.pixelColor(0, 0).alpha() != 0) return false;
        if (scenario == "alias") {
            if (args.size() != 4 || !glyph->setProperty("kind", args[3])) return false;
            if (image != frame(window)) return false;
        }
    } else {
        const auto original = frame(window);
        if (original.isNull() || coloredPixels(original, true) < 30) return false;
        if (scenario == "visibility") {
            glyph->setProperty("kind", "Visible");
            const auto visible = frame(window);
            if (visible.isNull() || visible == original) return false;
            glyph->setProperty("kind", "Hidden");
            const auto hidden = frame(window);
            if (hidden.isNull() || visible == hidden) return false;
        } else if (scenario == "kind") {
            glyph->setProperty("kind", "Secret");
            if (original == frame(window)) return false;
        } else if (scenario == "fill") {
            glyph->setProperty("fill", QColor(Qt::blue));
            const auto image = frame(window);
            if (coloredPixels(image, true) != 0 || coloredPixels(image, false) < 30) return false;
        } else if (scenario == "stroke") {
            glyph->setProperty("stroke", QColor(Qt::blue));
            if (coloredPixels(frame(window), false) < 30) return false;
        } else if (scenario == "repeat") {
            QSignalSpy kind(glyph, SIGNAL(kindChanged())), fill(glyph, SIGNAL(fillChanged())), stroke(glyph, SIGNAL(strokeChanged()));
            for (int i = 0; i < 20; ++i) {
                glyph->setProperty("kind", "Pod"); glyph->setProperty("fill", QColor(Qt::red)); glyph->setProperty("stroke", QColor(Qt::black));
            }
            if (!kind.isValid() || !fill.isValid() || !stroke.isValid() || kind.count() || fill.count() || stroke.count() || original != frame(window)) return false;
        } else if (scenario == "invalid_color") {
            glyph->setProperty("fill", QColor{}); glyph->setProperty("stroke", QColor{});
            if (original != frame(window)) return false;
        } else if (scenario == "hidden") {
            glyph->setVisible(false); glyph->setProperty("kind", "Secret"); glyph->setProperty("fill", QColor(Qt::blue));
            const auto hidden = frame(window);
            if (coloredPixels(hidden, true) || coloredPixels(hidden, false)) return false;
            glyph->setVisible(true);
            if (coloredPixels(frame(window), false) < 30) return false;
        } else if (scenario == "zero") {
            glyph->setWidth(0); glyph->setHeight(0);
            const auto image = frame(window);
            if (coloredPixels(image, true) || coloredPixels(image, false)) return false;
            glyph->setWidth(96); glyph->setHeight(96);
            if (original != frame(window)) return false;
        } else if (scenario == "wide" || scenario == "tall" || scenario == "resize") {
            window->resize(scenario == "tall" ? 64 : 128, scenario == "wide" ? 64 : 128);
            const auto image = frame(window);
            if (image == original || coloredPixels(image, true) < 30 || image.pixelColor(0, 0).alpha()) return false;
            window->resize(96, 96);
            if (original != frame(window)) return false;
        } else return false;
    }
    return warnings.isEmpty();
}
}
int main(int argc, char** argv) {
    QGuiApplication app(argc, argv);
    const bool passed = exercise(app.arguments());
    if (!passed) std::fprintf(stderr, "Glyph UI scenario failed: %s\n", qPrintable(app.arguments().join(' ')));
    return passed ? 0 : 1;
}
