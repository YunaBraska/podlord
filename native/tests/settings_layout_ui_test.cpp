#include "workspace.h"
#include "ui_input.h"
#include <QAccessible>
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
    const auto scenario = argc == 2 ? QString::fromLocal8Bit(argv[1]) : QString{};
    if (scenario.startsWith("review_")) {
        const auto failed = [&](const char* stage) {
            std::fprintf(stderr, "Alert review %s failed at %s\n", qPrintable(scenario), stage);
            const auto directory = qEnvironmentVariable("PODLORD_REVIEW_FRAME_DIR");
            if (!directory.isEmpty()) window->grabWindow().save(directory + "/" + scenario + "-" + qEnvironmentVariable("QT_QUICK_CONTROLS_STYLE") + ".png");
            return 1;
        };
        window->resize(scenario == "review_touch_targets" ? QSize(844, 390) : QSize(1200, 900));
        if (!podlord::test::clickVisible(window, "settingsWorkspaceButton")
            || !podlord::test::selectSettingsSection(window, "alerts")
            || !podlord::test::clickVisible(window, "addAlert")) return failed("open new alert");
        auto* name = window->findChild<QQuickItem*>("alertName");
        if (!name || !podlord::test::scrollIntoView(window, name)) return failed("focus name");
        name->forceActiveFocus();
        QTest::keySequence(window, QKeySequence::SelectAll);
        for (const auto character : QByteArray("Keep this draft")) QTest::keyClick(window, character);
        if (scenario == "review_dirty_selection") {
            if (!podlord::test::clickVisible(window, "alertRule_0")) return failed("select other row");
            auto* decision = window->findChild<QObject*>("discardAlertChanges");
            if (!decision || !QTest::qWaitFor([&] { return decision->property("visible").toBool(); }, 2000)
                || name->property("text").toString() != "Keep this draft"
                || !podlord::test::clickVisible(window, "keepAlertChanges")) return failed("keep unsaved draft");
            return !decision->property("visible").toBool() && name->property("text").toString() == "Keep this draft" ? 0 : 1;
        }
        if (scenario == "review_toggle_keeps_draft") {
            const auto rule = workspace.alerts()->rules().first().toMap();
            if (!podlord::test::clickVisible(window, "toggleAlert_0")
                || !QTest::qWaitFor([&] { return !workspace.alerts()->busy()
                    && workspace.alerts()->rules().first().toMap().value("enabled").toBool() != rule.value("enabled").toBool(); }, 3000)) return failed("toggle stored rule");
            return name->property("text").toString() == "Keep this draft" ? 0 : 1;
        }
        if (scenario == "review_touch_targets") {
            for (const auto* id : {"saveAlert", "closeAlertEditor", "alertChooseColor", "addAlertAnd", "addAlertOr"}) {
                auto* control = window->findChild<QQuickItem*>(id);
                if (!control || !QTest::qWaitFor([&] { return control->width() >= 44 && control->height() >= 44; }, 2000)) {
                    std::fprintf(stderr, "Touch action is smaller than 44 points: %s\n", id); return 1;
                }
            }
            return 0;
        }
        return 1;
    }
    if (argc == 2 && QString::fromLocal8Bit(argv[1]) == "mobile_alert_controls") {
        window->resize(390, 844);
        if (!podlord::test::clickVisible(window, "settingsWorkspaceButton")) return 1;
        if (!podlord::test::clickVisible(window, "addAlert")) return 1;
        auto* editor = window->findChild<QQuickItem*>("alertEditor");
        auto* toggle = window->findChild<QQuickItem*>("alertSoundEnabled");
        auto* sound = window->findChild<QQuickItem*>("alertSound");
        auto* preview = window->findChild<QQuickItem*>("previewAlertSound");
        if (!editor || !toggle || !sound || !preview || sound->isVisible() || preview->isVisible()
            || !podlord::test::scrollIntoView(window, toggle)) return 1;
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
                          toggle->mapToScene(QPointF(toggle->width()/2, toggle->height()/2)).toPoint());
        if (!QTest::qWaitFor([&] { return sound->isVisible() && preview->isVisible(); }, 3000)) return 1;
        for (const auto* control : {toggle, sound, preview}) {
            const auto bounds = control->mapRectToItem(editor, control->boundingRect());
            if (bounds.left() < -1 || bounds.right() > editor->width() + 1) {
                std::fprintf(stderr, "Mobile Alert editor clips %s.\n", qPrintable(control->objectName()));
                return 1;
            }
        }
        return 0;
    }
    if (argc == 2 && QString::fromLocal8Bit(argv[1]) == "compact_scroll") {
        window->resize(640, 320);
        auto* header = window->findChild<QQuickItem*>("workspaceToolbar");
        if (!header || !QTest::qWaitFor([&] { return header->isVisible(); }, 3000)
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
    const auto credential = workspace.appStore() ? QString("token: local-layout-fixture")
        : QString("exec: {apiVersion: client.authentication.k8s.io/v1beta1, command: podlord-test-never-run}");
    const auto source = QString("apiVersion: v1\nkind: Config\nclusters:\n- name: local\n"
        "  cluster: {server: https://127.0.0.1:9}\nusers:\n- name: local\n  user:\n    ")
        + credential + "\ncontexts:\n- name: local\n  context: {cluster: local, user: local}\n";
    if (!workspace.importText(workspace.defaultSourceOrigin(), source)
        || !QTest::qWaitFor([&] { return !workspace.busy() && workspace.contexts().size()==1; },5000)
        || !workspace.openContext(workspace.contexts().first().toMap()["id"].toString())
        || !QTest::qWaitFor([&] { return !workspace.busy() && !workspace.currentSession().isEmpty()
            && (workspace.appStore() || workspace.authenticationRequired()); },5000)) return 1;
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
    auto* rules = window->findChild<QQuickItem*>("alertRulesTable");
    auto* add = window->findChild<QQuickItem*>("addAlert");
    if (!rules || !add || rules->height() > 221
        || add->mapToItem(rules, QPointF(0, 0)).y() > rules->height() + 16) {
        std::fprintf(stderr, "The alert rules table pushes its actions below the bounded desktop layout.\n");
        return 1;
    }
    auto* header = window->findChild<QQuickItem*>("alertHeaderView");
    auto* table = window->findChild<QQuickItem*>("alertTable");
    const auto firstColumnWidth = [table] {
        qreal width = -1;
        if (table) QMetaObject::invokeMethod(table, "columnWidth", Q_RETURN_ARG(qreal, width), Q_ARG(int, 0));
        return width;
    };
    if (!header || !QTest::qWaitFor([&] { return firstColumnWidth() > 0; }, 3000)) return 1;
    const qreal previousWidth = firstColumnWidth();
    const auto divider = header->mapToScene(QPointF(previousWidth - 1, header->height()/2)).toPoint();
    QTest::mouseMove(window, divider);
    QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, divider);
    QTest::mouseMove(window, divider + QPoint(64, 0));
    QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, divider + QPoint(64, 0));
    if (!QTest::qWaitFor([&] { return firstColumnWidth() > previousWidth + 20; }, 2000)) {
        std::fprintf(stderr, "Alert column did not resize through its header divider.\n");
        return 1;
    }
    if (!podlord::test::clickVisible(window, "mapWorkspaceButton")
        || !QTest::qWaitFor([&] { return sidebar->isVisible() && content->width() <= resourceWidth + 1; }, 3000)) {
        std::fprintf(stderr, "Resources did not restore the previously open sidebar.\n");
        return 1;
    }
    return 0;
}
