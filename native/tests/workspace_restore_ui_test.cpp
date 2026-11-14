#include "workspace.h"
#include "ui_input.h"
#include <QFile>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QLockFile>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <cstdio>
#include <stdexcept>

using namespace podlord;
namespace {
bool require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
    return true;
}
QByteArray contents(const QString& path) {
    QFile file(path); require(file.open(QIODevice::ReadOnly), "Cannot read private evidence.");
    return file.readAll();
}
bool write(const QString& path, const QByteArray& bytes) {
    QFile file(path); return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}
void settle(Workspace& workspace) {
    require(QTest::qWaitFor([&] { return !workspace.busy(); }, 5000), "Workspace did not settle.");
}
void run(const QString& scenario) {
    QTemporaryDir temporary; require(temporary.isValid(), "Cannot create private profile.");
    const auto profile = temporary.filePath("profile"), settingsPath = profile + "/read-settings.json";
    require(std::holds_alternative<ReadSettings>(ReadSettingsStore(profile).save({}, {})), "Cannot save initial real settings.");
    const auto imported = KubeconfigStore(profile).importText(temporary.filePath("config"),
        "apiVersion: v1\nkind: Config\nclusters:\n- name: local\n  cluster: {server: http://127.0.0.1:9}\n"
        "contexts:\n- name: first\n  context: {cluster: local}\n- name: second\n  context: {cluster: local}\n");
    require(std::holds_alternative<SourceSnapshot>(imported), "Cannot import real context identities.");
    const auto contexts = std::get<SourceSnapshot>(imported).contexts;
    const SessionStore sessions(profile);
    auto created = sessions.openTab(contexts.first().id);
    require(std::holds_alternative<SessionCatalog>(created), "Cannot create owned session.");
    const auto first = std::get<SessionCatalog>(created).sessions.first().id;
    require(std::holds_alternative<SessionCatalog>(sessions.activate(first)), "Cannot activate owned session.");
    created = sessions.openTab(contexts.last().id);
    require(std::holds_alternative<SessionCatalog>(created), "Cannot create second owned session.");
    const auto second = std::get<SessionCatalog>(created).sessions.last().id;
    require(std::holds_alternative<SessionCatalog>(sessions.activate(second)), "Cannot activate second owned session.");
    const auto sessionBytes = contents(profile + "/tabs.json");
    if (scenario == "disabled" || scenario == "reload" || scenario == "reactivate" || scenario == "reenable" || scenario == "locked_start") {
        auto root = QJsonDocument::fromJson(contents(settingsPath)).object();
        root["workspaceRestore"] = false;
        require(write(settingsPath, QJsonDocument(root).toJson()), "Cannot select disabled startup preference.");
    }
    if (scenario == "invalid_flag" || scenario == "missing_flag") {
        auto root = QJsonDocument::fromJson(contents(settingsPath)).object();
        if (scenario == "invalid_flag") root["workspaceRestore"] = "false";
        else root.remove("workspaceRestore");
        require(write(settingsPath, QJsonDocument(root).toJson()), "Cannot exercise invalid external preference.");
    }
    QLockFile sessionLock(profile + "/tabs.lock");
    if (scenario == "locked_start") require(sessionLock.tryLock(0), "Cannot hold external session lock.");
    Workspace workspace(profile); settle(workspace);
    if (scenario == "invalid_flag" || scenario == "missing_flag" || scenario == "locked_start") {
        require(!workspace.error().isEmpty(), "Startup failure is not visible.");
        require(contents(profile + "/tabs.json") == sessionBytes, "Failed startup changed session ownership.");
        require(workspace.currentSession().isEmpty() && workspace.tabs().isEmpty(), "Failed startup adopted an unsafe placement.");
        return;
    }
    require(workspace.property("workspaceRestoreEnabled").isValid(), "Restoration preference is missing.");
    if (scenario == "disabled" || scenario == "reload" || scenario == "reactivate" || scenario == "reenable") {
        require(!workspace.property("workspaceRestoreEnabled").toBool() && workspace.tabs().isEmpty() && workspace.currentSession().isEmpty(), "Disabled restart reopened tabs.");
        const auto after = std::get<SessionCatalog>(sessions.list());
        require(after.sessions.isEmpty() && !after.activeSession,
            "Disabled restart retained a closed-tab catalog or activation.");
        if (scenario == "disabled") return;
        require(!workspace.activate(first.toString(QUuid::WithoutBraces)) && workspace.openContext(contexts.first().id),
            "Explicit context opening did not replace the closed tab."); settle(workspace);
        require(workspace.tabs().size() == 1 && workspace.currentSession() != first.toString(QUuid::WithoutBraces),
            "Explicit context opening restored unrelated or closed tabs.");
        if (scenario == "reload") { require(workspace.reload(), "Explicit reload rejected."); settle(workspace); require(workspace.tabs().size() == 1, "Reload reapplied startup suppression."); }
        if (scenario == "reenable") {
            require(workspace.saveWorkspaceRestore(true), "Cannot reenable restoration."); settle(workspace);
            Workspace restarted(profile); settle(restarted);
            require(restarted.tabs().size() == 1 && restarted.currentSession() == workspace.currentSession(), "Reenabled restart lost explicit placement.");
        }
        return;
    }
    require(workspace.property("workspaceRestoreEnabled").toBool() && workspace.tabs().size() == 2
        && workspace.currentSession() == second.toString(QUuid::WithoutBraces), "Default startup lost saved tab order or activation.");
    const auto adoptedBytes = contents(profile + "/tabs.json");
    if (scenario == "default") return;
    if (scenario == "conflict") {
        auto changed = std::get<ReadSettings>(ReadSettingsStore(profile).load()); changed.logLimitMb = 9;
        require(std::holds_alternative<ReadSettings>(ReadSettingsStore(profile).save(changed, {})), "Cannot exercise concurrent settings writer.");
        require(workspace.saveWorkspaceRestore(false), "Asynchronous save admission rejected."); settle(workspace);
        require(workspace.property("workspaceRestoreEnabled").toBool() && !workspace.error().isEmpty()
            && std::get<ReadSettings>(ReadSettingsStore(profile).load()).logLimitMb == 9, "Stale toggle overwrote unrelated settings.");
        return;
    }
    QLockFile settingsLock(settingsPath + ".lock");
    if (scenario == "locked_save") {
        require(settingsLock.tryLock(0) && workspace.saveWorkspaceRestore(false), "Cannot exercise locked save."); settle(workspace);
        require(workspace.property("workspaceRestoreEnabled").toBool() && !workspace.error().isEmpty(), "Failed save adopted the preference."); return;
    }
    QQmlApplicationEngine engine; engine.rootContext()->setContextProperty("workspace", &workspace);
    engine.load(QUrl("qrc:/podlord/Main.qml")); require(!engine.rootObjects().isEmpty(), "Cannot load actual application.");
    auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first()); require(window, "Application window is missing.");
    if (scenario == "narrow") { window->setWidth(390); window->setHeight(720); }
    require(QTest::qWaitForWindowExposed(window, 5000), "Application window did not become visible.");
    const auto click = [&](const char* name, bool keyboard = false) {
        QSignalSpy frames(window, &QQuickWindow::frameSwapped); window->update();
        require(frames.wait(1000), "Application did not render its controls.");
        auto* control = window->findChild<QQuickItem*>(QString::fromLatin1(name));
        require(control && control->isVisible() && control->isEnabled(), "Restoration control is not reachable.");
        if (!keyboard) require(podlord::test::scrollIntoView(window, control), "Cannot scroll to the restoration control.");
        QPoint point;
        const auto reachable = QTest::qWaitFor([&] {
            window->contentItem()->ensurePolished(); control->ensurePolished();
            point = control->mapToScene(QPointF(control->width()/2, control->height()/2)).toPoint();
            return control->width() > 0 && control->height() > 0 && window->contentItem()->contains(point);
        }, 5000);
        if (!reachable) throw std::runtime_error(QString("Control %1 lies outside scene %2x%3 at %4,%5 (%6x%7).")
            .arg(QString::fromLatin1(name)).arg(window->contentItem()->width()).arg(window->contentItem()->height())
            .arg(point.x()).arg(point.y()).arg(control->width()).arg(control->height()).toStdString());
        if (keyboard) { control->forceActiveFocus(); QTest::keyClick(window, Qt::Key_Space); }
        else QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, point);
        QCoreApplication::processEvents();
    };
    click("settingsWorkspaceButton"); require(podlord::test::selectSettingsSection(window, "appearance"), "Appearance section is not reachable.");
    click("inlineWorkspaceRestore", scenario == "keyboard"); settle(workspace);
    require(!workspace.property("workspaceRestoreEnabled").toBool(), "Real control did not persist restoration preference.");
    require(workspace.tabs().size() == 2 && contents(profile + "/tabs.json") == adoptedBytes, "Preference change closed current tabs or changed their catalog.");
    require(workspace.portForwards().isEmpty(), "Preference change recreated forwarding.");
    if (scenario == "repeat") { require(workspace.saveWorkspaceRestore(false), "Repeat preference save rejected."); settle(workspace); }
    Workspace restarted(profile); settle(restarted);
    require(!restarted.property("workspaceRestoreEnabled").toBool() && restarted.tabs().isEmpty(), "Saved control did not suppress reopening on restart.");
}
}
int main(int argc, char** argv) {
    QGuiApplication application(argc, argv);
    try {
        require(argc == 2, "Expected one restoration scenario.");
        const auto scenario = QString::fromLocal8Bit(argv[1]);
        require(QStringList{"default", "disabled", "reload", "reactivate", "reenable", "locked_start", "invalid_flag", "missing_flag", "conflict", "locked_save", "ui", "keyboard", "narrow", "repeat"}.contains(scenario), "Unknown restoration scenario.");
        run(scenario); return 0;
    } catch (const std::exception& error) { std::fprintf(stderr, "%s\n", error.what()); return 1; }
}
