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
    if (argc == 2 && QString::fromLocal8Bit(argv[1]) == "mobile_alert_controls") {
        window->resize(390, 844);
        if (!podlord::test::clickVisible(window, "settingsWorkspaceButton")) return 1;
        QQuickItem* editor = nullptr;
        QQuickItem* attribution = nullptr;
        QQuickItem* sound = nullptr;
        QQuickItem* preview = nullptr;
        QQuickItem* source = nullptr;
        if (!QTest::qWaitFor([&] {
                editor = window->findChild<QQuickItem*>("alertEditor");
                attribution = window->findChild<QQuickItem*>("alertSoundAttribution");
                sound = window->findChild<QQuickItem*>("alertSound");
                preview = window->findChild<QQuickItem*>("previewAlertSound");
                source = window->findChild<QQuickItem*>("openAlertSoundSource");
                return editor && attribution && sound && preview && source && source->isVisible() && editor->width() > 0;
            }, 3000)) return 1;
        const auto contained = [editor](QQuickItem* item) {
            const qreal left = item->mapToItem(editor, QPointF{}).x();
            const qreal right = item->mapToItem(editor, QPointF(item->width(), 0)).x();
            return left >= -1 && right <= editor->width() + 1;
        };
        if (!contained(sound) || !contained(preview) || !contained(source) || !contained(attribution)) {
            std::fprintf(stderr, "Mobile Alert editor clips a sound control or source attribution.\n");
            return 1;
        }
        return 0;
    }
    if (argc == 2 && QString::fromLocal8Bit(argv[1]) == "compact_scroll") {
        window->resize(640, 320);
        auto* rail = window->findChild<QQuickItem*>("landscapeNavigation");
        if (!rail || !QTest::qWaitFor([&] { return rail->isVisible(); }, 3000)
            || !podlord::test::clickVisible(window, "settingsWorkspaceButton")) {
            std::fprintf(stderr, "Compact Settings navigation is unavailable.\n");
            return 1;
        }
        if (!podlord::test::selectSettingsSection(window, "appearance")) {
            std::fprintf(stderr, "Compact Settings could not select Appearance.\n");
            return 1;
        }
        auto* scroll = window->findChild<QQuickItem*>("settingsContent");
        auto* language = window->findChild<QQuickItem*>("inlineUiLanguage");
    if (!scroll || !language || !QTest::qWaitFor([&] {
            return scroll->isVisible() && scroll->height() > 40 && language->isVisible()
                && scroll->property("contentHeight").toDouble() > scroll->height() + 40;
        }, 3000)) {
            std::fprintf(stderr, "Compact Settings content is not scrollable: viewport=%g content=%g language=%d parent=%g grandparent=%g window=%dx%d languageHeight=%g\n",
                         scroll ? scroll->height() : -1, scroll ? scroll->property("contentHeight").toDouble() : -1,
                         language && language->isVisible(), scroll && scroll->parentItem() ? scroll->parentItem()->height() : -1,
                         scroll && scroll->parentItem() && scroll->parentItem()->parentItem() ? scroll->parentItem()->parentItem()->height() : -1,
                         window->width(), window->height(), language ? language->height() : -1);
            const auto frame = qEnvironmentVariable("PODLORD_SETTINGS_FAILURE_FRAME");
            if (!frame.isEmpty()) window->grabWindow().save(frame);
            return 1;
        }
        const qreal initialY = language->mapToScene(QPointF{}).y();
        if (!podlord::test::scrollIntoView(window, language)) return 1;
        const qreal scrollTop = scroll->mapToScene(QPointF{}).y();
        const qreal scrollBottom = scroll->mapToScene({0, scroll->height()}).y();
        if (!QTest::qWaitFor([&] {
                const qreal y = language->mapToScene(QPointF{}).y();
                return y < initialY - 20 && y >= scrollTop - 1
                    && y + language->height() <= scrollBottom + 1;
            }, 2000)) {
            std::fprintf(stderr, "Compact Settings did not scroll to Language: initial=%g current=%g height=%g viewport=%g..%g content=%g\n",
                         initialY, language->mapToScene(QPointF{}).y(), language->height(), scrollTop, scrollBottom,
                         scroll->property("contentHeight").toDouble());
            return 1;
        }
        return 0;
    }
    if (!podlord::test::clickVisible(window, "resourcesWorkspaceButton")) return 1;
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
    auto* rules = window->findChild<QQuickItem*>("alertRulesTable");
    auto* add = window->findChild<QQuickItem*>("addAlert");
    if (!rules || !add || rules->height() > 221
        || add->mapToItem(rules, QPointF(0, 0)).y() > rules->height() + 16) {
        std::fprintf(stderr, "The alert rules table pushes its actions below the bounded desktop layout.\n");
        return 1;
    }
    if (!podlord::test::clickVisible(window, "resourcesWorkspaceButton")
        || !QTest::qWaitFor([&] { return sidebar->isVisible() && content->width() <= resourceWidth + 1; }, 3000)) {
        std::fprintf(stderr, "Resources did not restore the previously open sidebar.\n");
        return 1;
    }
    return 0;
}
