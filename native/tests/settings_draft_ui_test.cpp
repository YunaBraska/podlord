#include "workspace.h"
#include "ui_input.h"
#include <QDir>
#include <QLockFile>
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
bool require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
    return true;
}
}

int main(int argc, char** argv) {
    QGuiApplication application(argc, argv);
    try {
        require(argc == 2, "Expected a settings scenario.");
        const QString scenario = QString::fromLocal8Bit(argv[1]);
        require(QStringList{"sync", "yaml", "invalid", "sync_save", "sync_lock", "sync_conflict", "sync_leave", "sync_narrow", "yaml_save", "yaml_invalid", "yaml_lock", "yaml_conflict", "yaml_leave", "yaml_narrow"}.contains(scenario), "Unknown settings scenario.");
        const bool yaml = scenario.startsWith("yaml");
        QTemporaryDir directory;
        require(directory.isValid(), "Cannot create private settings profile.");
        podlord::Workspace workspace(directory.filePath("profile"));
        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty("workspace", &workspace);
        engine.load(QUrl("qrc:/podlord/Main.qml"));
        require(!engine.rootObjects().isEmpty(), "Cannot create real settings UI.");
        auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
        require(window && QTest::qWaitFor([&] { return !workspace.busy(); }, 5000), "Settings did not settle.");
        if (scenario.endsWith("_narrow")) window->setWidth(360);
        const auto item = [&](const char* name) {
            auto* target = window->findChild<QQuickItem*>(QString::fromLatin1(name));
            require(target && target->isVisible() && target->isEnabled(), "Settings control is not available.");
            return target;
        };
        const auto click = [&](const char* name) {
            auto* target = item(name);
            require(podlord::test::scrollIntoView(window, target), "Settings action cannot be reached in its viewport.");
            const auto point = target->mapToScene(QPointF(target->width() / 2, target->height() / 2)).toPoint();
            require(window->contentItem()->contains(point), "Settings action lies outside the scene.");
            QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, point);
            QCoreApplication::processEvents();
        };
        const auto fill = [&](const char* name, const QByteArray& value) {
            auto* target = item(name);
            require(podlord::test::scrollIntoView(window, target), "Settings input cannot be reached in its viewport.");
            target->forceActiveFocus();
            QTest::keySequence(window, QKeySequence::SelectAll);
            for (const auto character : value) QTest::keyClick(window, character);
            QTest::keyClick(window, Qt::Key_Tab);
        };
        click("settingsWorkspaceButton");
        click(yaml ? "settingsPrivacySection" : "settingsSyncSection");
        if (yaml) fill("inlineYamlLimit", scenario == "yaml_invalid" ? "0" : "6");
        else {
            fill("inlineInactiveMinutes", "12");
            fill("inlineRequestLimit", "60");
            fill("inlineLogLimit", "9");
            require(item("inlineInactiveMinutes")->property("value").toInt() == 12
                && item("inlineRequestLimit")->property("value").toInt() == 60
                && item("inlineLogLimit")->property("text").toString() == "9", "Could not enter real sync draft.");
        }
        if (scenario.contains('_')) {
            if (scenario.endsWith("_leave")) {
                click("resourcesWorkspaceButton");
                click("settingsWorkspaceButton");
                click(yaml ? "settingsPrivacySection" : "settingsSyncSection");
                require(yaml ? item("inlineYamlLimit")->property("text").toString() == "3"
                    : item("inlineLogLimit")->property("text").toString() == "5", "Leaving settings implicitly saved an unconfirmed draft.");
                require(workspace.requestLimit() == 0 && workspace.logLimitMb() == 5 && workspace.yamlLimitMiB() == 3, "Leaving settings changed the active configuration.");
                return 0;
            }
            QLockFile lock(directory.filePath("profile/read-settings.json.lock"));
            if (scenario.endsWith("_lock")) require(QDir().mkpath(directory.filePath("profile")) && lock.tryLock(0), "Could not acquire the external profile lock.");
            if (scenario.endsWith("_conflict")) {
                podlord::Workspace other(directory.filePath("profile"));
                require(QTest::qWaitFor([&] { return !other.busy(); }, 5000) && other.saveReadSettings(121, 0, "5"), "Could not save settings through the other workspace.");
                require(QTest::qWaitFor([&] { return !other.busy() && other.requestLimit() == 121; }, 5000), "The other workspace did not complete its save.");
            }
            click(yaml ? "inlineSaveYamlLimit" : "inlineSaveSync");
            require(QTest::qWaitFor([&] { return !workspace.busy(); }, 5000), "Settings save did not settle.");
            const bool rejected = scenario.endsWith("_lock") || scenario.endsWith("_conflict") || scenario == "yaml_invalid";
            if (rejected) {
                require(!workspace.error().isEmpty(), "Rejected settings save has no visible error.");
                require(workspace.requestLimit() == 0 && workspace.logLimitMb() == 5 && workspace.yamlLimitMiB() == 3, "Rejected save changed the active configuration.");
                require(yaml ? item("inlineYamlLimit")->property("text").toString() == (scenario == "yaml_invalid" ? "0" : "6")
                    : item("inlineLogLimit")->property("text").toString() == "9", "Rejected save discarded the editable draft.");
                return 0;
            }
            podlord::Workspace restored(directory.filePath("profile"));
            require(QTest::qWaitFor([&] { return !restored.busy(); }, 5000), "Saved settings could not be reopened.");
            require(yaml ? restored.yamlLimitMiB() == 6 && restored.logLimitMb() == 5 && restored.requestLimit() == 0
                : restored.requestLimit() == 60 && restored.inactiveSyncMinutes() == 12 && restored.logLimitMb() == 9 && restored.yamlLimitMiB() == 3, "Saved settings or unrelated policy did not survive reopen.");
            return 0;
        }
        const auto name = workspace.themeName();
        const auto variant = workspace.themeVariant() == "dark" ? QString("light") : QString("dark");
        if (scenario == "invalid") require(!workspace.saveAppearance("missing-theme", variant), "Unknown theme was accepted.");
        else {
            // A real public preference update, not a fabricated notification or store replacement.
            require(workspace.saveAppearance(name, variant), "Appearance change was rejected.");
            require(QTest::qWaitFor([&] { return !workspace.busy() && workspace.themeVariant() == variant; }, 5000), "Appearance change did not complete.");
        }
        QCoreApplication::processEvents();
        if (yaml) {
            require(item("inlineYamlLimit")->property("text").toString() == "6", "Appearance update discarded unrelated YAML input.");
            require(workspace.yamlLimitMiB() == 3, "Appearance update saved the YAML draft implicitly.");
        } else {
            require(item("inlineInactiveMinutes")->property("value").toInt() == 12
                && item("inlineRequestLimit")->property("value").toInt() == 60
                && item("inlineLogLimit")->property("text").toString() == "9", "Appearance update discarded unrelated sync input.");
            require(workspace.logLimitMb() == 5, "Appearance update saved the sync draft implicitly.");
        }
        if (scenario != "invalid") {
            click("settingsAppearanceSection");
            require(item("inlineAppearanceTheme")->property("currentText").toString() == name
                && item("inlineAppearanceVariant")->property("currentText").toString() == variant, "Appearance controls do not reflect the saved theme.");
        }
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}
