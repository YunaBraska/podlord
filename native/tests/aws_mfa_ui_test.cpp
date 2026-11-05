#include "workspace.h"
#include "ui_input.h"
#include "aws_boundary.h"
#include "browser_boundary.h"
#include "kubernetes_auth_boundary.h"
#include <QDir>
#include <QFile>
#include <QPointer>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QTemporaryDir>
#include <stdexcept>

using namespace podlord;
namespace {
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
void write(const QString& path, const QByteArray& data) {
    QFile file(path); require(file.open(QIODevice::WriteOnly) && file.write(data) == data.size(), "Cannot write isolated selected AWS file.");
}
void run(const QString& scenario) {
    BrowserBoundary browser;
    test::AwsBoundary boundary(scenario == "denied" ? "mfa_denied" : "success");
    test::Kubernetes api;
    require(api.listen(QHostAddress::LocalHost, 0), "Cannot listen for the external Kubernetes boundary.");
    QTemporaryDir temporary;
    require(temporary.isValid(), "Cannot create isolated MFA profile.");
    const auto profile = temporary.filePath("profile");
    KubeconfigStore store(profile);
    const auto imported = store.importText(temporary.filePath("cluster.yaml"),
        QString("apiVersion: v1\nclusters:\n- name: example\n  cluster: {server: 'http://127.0.0.1:%1'}\nusers:\n- name: example\n  user:\n    exec:\n      command: aws\n      apiVersion: client.authentication.k8s.io/v1beta1\n      args: [eks, get-token, --cluster-name, example, --profile, target]\ncontexts:\n"
        "- name: One\n  context: {cluster: example, user: example}\n- name: Two\n  context: {cluster: example, user: example}\n").arg(api.serverPort()));
    require(std::holds_alternative<SourceSnapshot>(imported), "Cannot import EKS source.");
    const auto contexts = std::get<SourceSnapshot>(imported).contexts;
    const auto config = temporary.filePath("config");
    const auto keys = temporary.filePath("credentials");
    write(config, "[profile target]\nregion=eu-central-1\nrole_arn=arn:aws:iam::123456789012:role/Target\nsource_profile=root\nmfa_serial=arn:aws:iam::123456789012:mfa/user\n");
    write(keys, "[root]\naws_access_key_id=ROOTKEY\naws_secret_access_key=private-signing-secret\n");
    require(std::holds_alternative<SourceSnapshot>(store.importAwsProfile(contexts.first().id, config, keys)), "Cannot import role/MFA profile.");
    if (scenario == "second_challenge") require(std::holds_alternative<SourceSnapshot>(store.importAwsProfile(contexts.last().id, config, keys)), "Cannot import second context role.");
    Workspace workspace(profile);
    require(QTest::qWaitFor([&] { return !workspace.busy(); }, 5000), "Workspace did not settle.");
    require(workspace.authenticationChallenge().isEmpty() && !workspace.submitAuthenticationCode("missing", "123456"), "No-context MFA acquired authority.");
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("workspace", &workspace);
    engine.load(QUrl("qrc:/podlord/Main.qml"));
    require(!engine.rootObjects().isEmpty(), "Production MFA window cannot load.");
    auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
    require(window, "Production window is missing.");
    if (scenario == "narrow") window->resize(320, 640);
    if (scenario == "landscape") window->resize(640, 320);
    require(workspace.openContext(contexts.first().id)
        && QTest::qWaitFor([&] { return !workspace.busy() && workspace.authenticationRequired(); }, 5000), "Opening context did not await explicit login.");
    require(test::clickVisible(window, "resourcesWorkspaceButton"), "Cannot select the resource view before authentication.");
    require(test::revealWorkspaceAction(window, "authenticationButton") && test::clickVisible(window, "authenticationButton")
        && QTest::qWaitFor([&] { return test::visibleItem(window->contentItem(), "authenticationAccept"); }, 2000)
        && test::clickVisible(window, "authenticationAccept"), "Cannot confirm native login through production controls.");
    require(QTest::qWaitFor([&] { return test::visibleItem(window->contentItem(), "authenticationMfaCode"); }, 5000), "MFA form did not appear.");
    const auto challenge = workspace.authenticationChallenge().value("id").toString();
    require(!challenge.isEmpty() && boundary.connections == 0 && browser.urls.isEmpty(), "MFA contacted an external system before code entry.");
    QPointer<QQuickItem> input = test::visibleItem(window->contentItem(), "authenticationMfaCode");
    input->setProperty("text", "123456");
    require(input->property("displayText").toString() != "123456", "MFA code is not masked.");
    const auto evidence = qEnvironmentVariable("PODLORD_UI_EVIDENCE_DIR");
    if (!evidence.isEmpty()) {
        window->update(); QTest::qWait(80);
        require(QDir().mkpath(evidence) && window->grabWindow().save(QDir(evidence).filePath("aws-mfa-" + scenario + "-" + qEnvironmentVariable("QT_QUICK_CONTROLS_STYLE") + ".png")), "Cannot retain MFA UI frame.");
    }
    if (scenario == "second_challenge") {
        require(workspace.openContext(contexts.last().id) && QTest::qWaitFor([&] { return !workspace.busy() && workspace.authenticationRequired(); }, 5000), "Cannot select second MFA context.");
        require(test::revealWorkspaceAction(window, "authenticationButton") && test::clickVisible(window, "authenticationButton")
            && QTest::qWaitFor([&] { return test::visibleItem(window->contentItem(), "authenticationAccept"); }, 2000)
            && test::clickVisible(window, "authenticationAccept")
            && QTest::qWaitFor([&] { return test::visibleItem(window->contentItem(), "authenticationMfaCode"); }, 2000), "Cannot start second MFA challenge.");
        const auto other = workspace.authenticationChallenge().value("id").toString();
        require(!other.isEmpty() && other != challenge, "Contexts shared an MFA challenge.");
        test::visibleItem(window->contentItem(), "authenticationMfaCode")->setProperty("text", "654321");
        require(workspace.openContext(contexts.first().id) && QTest::qWaitFor([&] { return workspace.authenticationChallenge().value("id") == challenge; }, 3000), "Cannot return to first pending MFA.");
        auto* restored = test::visibleItem(window->contentItem(), "authenticationMfaCode");
        require(restored && restored->property("text").toString().isEmpty() && boundary.connections == 0, "MFA code crossed context/challenge boundaries.");
        workspace.cancelAuthentication();
        require(workspace.openContext(contexts.last().id) && QTest::qWaitFor([&] { return workspace.authenticationChallenge().value("id") == other; }, 3000), "Second challenge was affected by first cancellation.");
        workspace.cancelAuthentication();
    } else if (scenario == "context_switch" || scenario == "replacement" || scenario == "cancel") {
        if (scenario == "context_switch") require(workspace.openContext(contexts.last().id), "Cannot switch MFA context.");
        else if (scenario == "replacement") require(workspace.importAwsProfile(contexts.first().id, config, keys), "Cannot replace imported MFA identity.");
        else require(test::clickVisible(window, "cancelAuthenticationMfa"), "Cannot cancel MFA through its form.");
        require(QTest::qWaitFor([&] { return !test::visibleItem(window->contentItem(), "authenticationMfaCode"); }, 3000)
            && !workspace.submitAuthenticationCode(challenge, "123456") && boundary.roles == 0, "Old MFA input remained usable after invalidation.");
        if (input) require(input->property("text").toString().isEmpty(), "Inactive MFA input retained a code.");
    } else {
        require(test::clickVisible(window, "submitAuthenticationMfa"), "MFA Continue is inaccessible.");
        require(!workspace.submitAuthenticationCode(challenge, "123456"), "Repeated UI submission reused an MFA code.");
        require(QTest::qWaitFor([&] { return !workspace.authenticationRunning() && !workspace.busy(); }, 10000), "MFA authentication never completed.");
        require(!test::visibleItem(window->contentItem(), "authenticationMfaCode") && workspace.authenticationChallenge().isEmpty()
            && boundary.roles == 1 && boundary.valid, "MFA completion did not clear its form or broke the transport contract.");
        if (scenario == "denied") require(workspace.authenticationRequired() && api.calls == 0, "Denied MFA authorized Kubernetes.");
        else {
            const bool recovered = QTest::qWaitFor([&] { return !workspace.authenticationRequired() && api.calls > 0 && !workspace.busy(); }, 10000);
            if (!recovered) std::fprintf(stderr, "MFA recovery: Kubernetes requests=%d, connections=%d, required=%d, busy=%d, error=%s\n", api.calls, boundary.connections,
                workspace.authenticationRequired(), workspace.busy(), qPrintable(workspace.error()));
            require(recovered, "Role credentials never reached the real Kubernetes HTTP boundary.");
            require(api.authorization.startsWith("Bearer k8s-aws-v1."), "Kubernetes did not receive the final EKS token.");
        }
    }
    require(browser.urls.isEmpty(), "Static role/MFA authentication opened a browser.");
    window->close();
}
}
int main(int argc, char** argv) {
    QGuiApplication app(argc, argv);
    if (argc != 2) return 2;
    try {
        require(QSslSocket::setActiveBackend("openssl") || QSslSocket::activeBackend() == "openssl", "Packaged OpenSSL is required.");
        run(QString::fromUtf8(argv[1]));
        std::printf("AWS MFA UI scenario passed: %s\n", argv[1]); return 0;
    } catch (const std::exception& error) { std::fprintf(stderr, "%s\n", error.what()); return 1; }
}
