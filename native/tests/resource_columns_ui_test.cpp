#include "kubeconfig_store.h"
#include "workspace.h"
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QTest>
#include <algorithm>

using namespace podlord;

namespace {
QString firstHeader(QQuickWindow* window) {
    QQuickItem* first = nullptr;
    QList<QQuickItem*> pending{window->contentItem()};
    while (!pending.isEmpty()) {
        auto* item = pending.takeLast();
        pending.append(item->childItems());
        if (!item->objectName().startsWith("header_") || !item->isVisible() || item->width() <= 0) continue;
        const auto x = item->mapToScene(QPointF()).x();
        if (x < 0 || x >= window->width()) continue;
        if (!first || x < first->mapToScene(QPointF()).x()) first = item;
    }
    return first ? first->property("text").toString() : QString{};
}
bool run(bool customized) {
    QTemporaryDir root;
    if (!root.isValid()) return false;
    const auto profile = root.filePath("profile");
    const auto imported = KubeconfigStore(profile).importText(root.filePath("columns.yaml"),
        "apiVersion: v1\nkind: Config\nclusters:\n- name: local\n  cluster:\n    server: https://127.0.0.1:9\n"
        "users:\n- name: local\n  user:\n    exec:\n      apiVersion: client.authentication.k8s.io/v1beta1\n"
        "      command: podlord-test-never-run\ncontexts:\n- name: local\n  context:\n    cluster: local\n    user: local\n");
    const auto* source = std::get_if<SourceSnapshot>(&imported);
    if (!source) return false;
    if (customized) {
        Workspace writer(profile);
        if (!QTest::qWaitFor([&] { return !writer.busy(); }, 5000)) return false;
        auto columns = writer.resourceColumns();
        const auto kind = std::find_if(columns.begin(), columns.end(), [](const auto& entry) { return entry.toMap().value("id") == "kind"; });
        if (kind == columns.end()) return false;
        std::rotate(columns.begin(), kind, kind + 1);
        if (!writer.saveTableLayout("resource", columns)
            || !QTest::qWaitFor([&] { return !writer.property("tableLayoutSaving").toBool(); }, 5000)
            || !writer.tableLayoutError().isEmpty()) return false;
    }
    Workspace workspace(profile);
    if (!QTest::qWaitFor([&] { return !workspace.busy(); }, 5000)
        || !workspace.openContext(source->contexts.first().id)
        || !QTest::qWaitFor([&] { return !workspace.busy() && workspace.authenticationRequired(); }, 5000)) return false;
    const auto saved = workspace.resourceColumns();
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("workspace", &workspace);
    engine.load(QUrl("qrc:/podlord/Main.qml"));
    if (engine.rootObjects().isEmpty()) return false;
    auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
    if (!window) return false;
    for (const auto width : {320, 599, 600, 1440, 320}) {
        window->resize(width, 800);
        const auto expected = customized ? "Kind" : width < 600 ? "Name" : "Status";
        if (!QTest::qWaitFor([&] { return firstHeader(window) == expected; }, 3000)) {
            qWarning() << "First visible resource header at" << width << "was" << firstHeader(window) << "instead of" << expected;
            return false;
        }
        if (workspace.resourceColumns() != saved || !workspace.authenticationRequired()) return false;
    }
    return true;
}
} // namespace

int main(int argc, char** argv) {
    QGuiApplication app(argc, argv);
    if (argc != 2 || !QStringList{"default", "custom"}.contains(QString::fromUtf8(argv[1]))) return 2;
    return run(QString::fromUtf8(argv[1]) == "custom") ? 0 : 1;
}
