#include "workspace.h"
#include "ui_input.h"
#include <QFile>
#include <QDir>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QTemporaryDir>
#include <stdexcept>

using namespace podlord;
namespace {
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
void run(const QString& scenario) {
    QTemporaryDir root;
    require(root.isValid(), "Cannot create isolated UI profile.");
    Workspace workspace(root.filePath("profile"));
    require(QTest::qWaitFor([&] { return !workspace.busy(); }, 5000), "Workspace did not settle.");
    const auto source = KubeconfigStore(root.filePath("profile")).importText(root.filePath("cluster.yaml"),
        "apiVersion: v1\nclusters:\n- name: example\n  cluster: {server: 'https://127.0.0.1:9'}\nusers:\n- name: example\n  user:\n    exec:\n      command: aws\n      apiVersion: client.authentication.k8s.io/v1beta1\n      args: [eks, get-token, --cluster-name, example]\ncontexts:\n- name: Example\n  context: {cluster: example, user: example}\n");
    require(std::holds_alternative<SourceSnapshot>(source) && workspace.reload(), "Cannot load EKS source.");
    require(QTest::qWaitFor([&] { return !workspace.busy() && workspace.contexts().size() == 1; }, 5000), "Source not visible.");
    const auto contextId = workspace.contexts().first().toMap().value("id").toString();
    const auto credentialsPath = root.filePath("credentials");
    QFile credentials(credentialsPath);
    require(credentials.open(QIODevice::WriteOnly), "Cannot create selected credentials.");
    credentials.write(scenario == "invalid" ? QByteArray("invalid file") : QByteArray("[default]\nregion=eu-central-1\naws_access_key_id=IMPORTEDKEY\naws_secret_access_key=private-key\n"));
    credentials.close();
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("workspace", &workspace);
    engine.load(QUrl("qrc:/podlord/Main.qml"));
    require(!engine.rootObjects().isEmpty(), "Cannot load production UI.");
    auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
    require(window, "Production window is missing.");
    require(test::clickVisible(window, "settingsWorkspaceButton"), "Cannot open Settings.");
    require(QTest::qWaitFor([&] { return test::visibleItem(window->contentItem(), "settingsSourcesSection"); }, 2000)
        && test::clickVisible(window, "settingsSourcesSection"), "Cannot open Sources Settings.");
    if (scenario == "narrow") { window->resize(320, 640); QTest::qWait(100); }
    require(test::revealWorkspaceAction(window, "sourceActionsMenuButton") && test::clickVisible(window, "sourceActionsMenuButton"), "Cannot open source actions.");
    require(test::clickVisible(window, "importAwsProfileFromSettings"), "Cannot open AWS import through its real button.");
    auto* dialog = window->findChild<QObject*>("awsProfileDialog");
    require(dialog && dialog->property("visible").toBool(), "AWS dialog was not opened.");
    const auto evidence = qEnvironmentVariable("PODLORD_UI_EVIDENCE_DIR");
    const auto capture = [&](const QString& state) {
        if (evidence.isEmpty()) return;
        QTest::qWait(50);
        require(QDir().mkpath(evidence) && window->grabWindow().save(QDir(evidence).filePath("aws-import-" + scenario + "-" + state + ".png")), "Cannot retain the actual AWS import UI frame.");
    };
    capture("dialog");
    auto* input = test::visibleItem(window->contentItem(), "awsProfileCredentials");
    auto* context = test::visibleItem(window->contentItem(), "awsProfileContext");
    require(input && context && context->property("currentValue").toString() == contextId
        && context->property("displayText").toString() == "Example", "AWS dialog did not visibly select the owned context.");
    if (scenario == "cancel") {
        require(test::clickVisible(window, "cancelAwsProfile") && !dialog->property("visible").toBool(), "AWS dialog cannot cancel.");
    } else {
        input->setProperty("text", credentialsPath);
        require(test::clickVisible(window, "confirmAwsProfile"), "AWS import button is inaccessible.");
        require(QTest::qWaitFor([&] { return !workspace.busy(); }, 5000), "AWS UI import did not settle.");
        if (scenario == "invalid") { require(dialog->property("visible").toBool() && !workspace.sourceImportError().isEmpty(), "Invalid AWS import was silently dismissed."); capture("error"); }
        else {
            require(QTest::qWaitFor([&] { return !dialog->property("visible").toBool(); }, 1000), "Successful AWS import did not close.");
            const auto resolved = KubeconfigStore(root.filePath("profile")).connection(contextId);
            require(std::holds_alternative<ClusterConnection>(resolved) && std::get<ClusterConnection>(resolved).exec->eks.has_value(), "UI did not import a native EKS profile.");
            require(workspace.currentSession().isEmpty(), "Profile import automatically opened a cluster.");
        }
    }
    window->close();
}
} // namespace
int main(int argc, char** argv) {
    QGuiApplication app(argc, argv);
    if (argc != 2) return 2;
    try { run(QString::fromUtf8(argv[1])); return 0; }
    catch (const std::exception& error) { std::fprintf(stderr, "%s\n", error.what()); return 1; }
}
