#include "workspace.h"
#include "ui_input.h"
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <cstdio>

namespace {
bool write(const QString& path, const QByteArray& bytes) {
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}
int visibleNotices(QQuickItem* root, const QString& text) {
    int count = root->isVisible() && root->property("text").toString() == text ? 1 : 0;
    for (auto* child : root->childItems()) count += visibleNotices(child, text);
    return count;
}
bool run(const QString& scenario) {
    QTemporaryDir temporary;
    if (!temporary.isValid() || !QDir().mkpath(temporary.filePath("inputs"))) return false;
    if (!write(temporary.filePath("inputs/valid.yaml"),
        "apiVersion: v1\nkind: Config\nclusters:\n- name: local\n  cluster: {server: 'http://127.0.0.1:9'}\n"
        "users:\n- name: anonymous\n  user: {}\ncontexts:\n- name: local\n  context: {cluster: local, user: anonymous}\ncurrent-context: local\n")) return false;
    if (scenario == "failures" && !write(temporary.filePath("inputs/invalid.yaml"), "apiVersion: v1\nkind: Config\ncontexts: [\n")) return false;
    podlord::Workspace workspace(temporary.filePath("profile"));
    QSignalSpy requests(&workspace, &podlord::Workspace::requestStarted);
    if (!QTest::qWaitFor([&] { return !workspace.busy(); }) || !workspace.importFile(temporary.filePath("inputs"))
        || !QTest::qWaitFor([&] { return !workspace.busy(); }) || workspace.sourceImportNotice().isEmpty()) return false;
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("workspace", &workspace);
    engine.load(QUrl("qrc:/podlord/Main.qml"));
    if (engine.rootObjects().isEmpty()) return false;
    auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
    if (!window || !podlord::test::clickVisible(window, "settingsWorkspaceButton")) return false;
    if (!podlord::test::selectSettingsSection(window, scenario == "unrelated" ? "about" : "sources")) return false;
    if (scenario == "once" || scenario == "unrelated") {
        const int expected = scenario == "once" ? 1 : 0;
        if (!QTest::qWaitFor([&] { return visibleNotices(window->contentItem(), workspace.sourceImportNotice()) == expected; })) {
            std::fprintf(stderr, "Expected %d visible import messages, observed %d.\n", expected, visibleNotices(window->contentItem(), workspace.sourceImportNotice()));
            return false;
        }
        return requests.isEmpty();
    }
    if (scenario != "failures" || workspace.sourceImportIssues().isEmpty()
        || !podlord::test::clickVisible(window, "settingsSourceImportDetails")) return false;
    auto* dialog = window->findChild<QObject*>("sourceImportDetails");
    return dialog && QTest::qWaitFor([&] { return dialog->property("opened").toBool(); })
        && visibleNotices(window->contentItem(), workspace.sourceImportNotice()) == 1 && requests.isEmpty();
}
}
int main(int argc, char** argv) {
    QGuiApplication app(argc, argv);
    Q_INIT_RESOURCE(workspace_ui);
    const bool passed = argc == 2 && run(QString::fromLocal8Bit(argv[1]));
    if (!passed) std::fprintf(stderr, "Settings import-notice scenario failed: %s\n", argc == 2 ? argv[1] : "missing");
    return passed ? 0 : 1;
}
