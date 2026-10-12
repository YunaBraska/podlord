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

int main(int argc, char** argv) {
    QGuiApplication application(argc, argv);
    QTemporaryDir directory;
    if (!directory.isValid()) return 1;
    podlord::Workspace workspace(directory.filePath("profile"));
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("workspace", &workspace);
    engine.load(QUrl("qrc:/podlord/Main.qml"));
    auto* window = engine.rootObjects().isEmpty() ? nullptr : qobject_cast<QQuickWindow*>(engine.rootObjects().first());
    if (!window || !QTest::qWaitFor([&] { return !workspace.busy(); }, 5000)) return 1;
    auto* sidebar = window->findChild<QQuickItem*>("workspaceSidebarHost");
    auto* content = window->findChild<QQuickItem*>("workspaceContent");
    if (!sidebar || !content || !sidebar->isVisible()) return 1;
    const qreal resourceWidth = content->width();
    const auto columns = workspace.sourceColumns();
    QStringList order;
    for (const auto& value : columns) order.append(value.toMap().value("id").toString());
    if (order.indexOf("name") < 0 || order.indexOf("imported") != order.indexOf("name") + 1
        || order.indexOf("cluster") != order.indexOf("imported") + 1) return 1;
    if (!podlord::test::clickVisible(window, "settingsWorkspaceButton")
        || !QTest::qWaitFor([&] { return sidebar->isVisible() && content->width() <= resourceWidth + 1; }, 3000)) {
        std::fprintf(stderr, "Settings did not retain the resource sidebar.\n");
        return 1;
    }
    QQuickItem* editor = nullptr;
    QQuickItem* soundSource = nullptr;
    if (!QTest::qWaitFor([&] {
            editor = window->findChild<QQuickItem*>("alertEditor");
            soundSource = window->findChild<QQuickItem*>("openAlertSoundSource");
            return editor && soundSource && soundSource->isVisible();
        }, 3000)
        || soundSource->mapToItem(editor, QPointF(soundSource->width(), 0)).x() > editor->width() + 1) {
        std::fprintf(stderr, "The alert sound source action is clipped by the Settings editor.\n");
        return 1;
    }
    if (!podlord::test::clickVisible(window, "resourcesWorkspaceButton")
        || !QTest::qWaitFor([&] { return sidebar->isVisible() && content->width() <= resourceWidth + 1; }, 3000)) {
        std::fprintf(stderr, "Resources did not restore the previously open sidebar.\n");
        return 1;
    }
    return 0;
}
