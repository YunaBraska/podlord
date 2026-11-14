#include "workspace.h"
#include "browser_boundary.h"
#include "ui_input.h"
#include <QFile>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QTemporaryDir>
#include <QTest>
#include <cstdio>

#define REQUIRE(value) do { if (!(value)) { std::fprintf(stderr, "Failed line %d: %s\n", __LINE__, #value); return false; } } while (false)

namespace {
bool run(const QString& scenario) {
    QTemporaryDir profile, input;
    REQUIRE(profile.isValid() && input.isValid());
    const podlord::KubeconfigStore store(profile.path());
    if (scenario == "unknown_exec_start") {
        const auto marker = input.filePath("executed");
        const auto program = input.filePath("credential-program");
        QFile file(program); REQUIRE(file.open(QIODevice::WriteOnly));
        const auto script = "#!/bin/sh\nprintf executed > '" + marker.toUtf8() + "'\n";
        REQUIRE(file.write(script) == script.size()); file.close();
        REQUIRE(file.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
        podlord::ClusterConnection connection;
        connection.credentialId = "store-external";
        connection.exec.emplace(); connection.exec->program = program;
        podlord::CredentialProcess credentials;
        QSignalSpy completed(&credentials, &podlord::CredentialProcess::completed);
        REQUIRE(!credentials.start(connection, "store-session"));
        REQUIRE(completed.size() == 1 && !credentials.running(connection.credentialId));
        return std::holds_alternative<podlord::Failure>(credentials.result(connection.credentialId)) && !QFile::exists(marker);
    }
    if (scenario == "folder") {
        REQUIRE(std::holds_alternative<podlord::Failure>(store.importPath(input.path())));
        const auto catalog = store.list();
        REQUIRE(std::holds_alternative<podlord::SourceCatalog>(catalog));
        return std::get<podlord::SourceCatalog>(catalog).sources.isEmpty();
    }
    if (scenario == "updates") {
        QFile cache(profile.filePath("release-check.json"));
        REQUIRE(cache.open(QIODevice::WriteOnly));
        const QByteArray invalid = "old private direct-download metadata";
        REQUIRE(cache.write(invalid) == invalid.size()); cache.close();
        podlord::ReleaseUpdates updates(profile.path());
        REQUIRE(!updates.startAutomaticChecks() && !updates.checkNow() && !updates.checkIfDue());
        const auto state = updates.state();
        REQUIRE(!state["enabled"].toBool() && !state["available"].toBool() && !state["newer"].toBool());
        REQUIRE(state["downloadUrl"].toString().isEmpty() && state["releaseUrl"].toString().isEmpty() && state["error"].toString().isEmpty());
        BrowserBoundary browser;
        REQUIRE(!updates.openDownload() && !updates.openRelease() && browser.urls.isEmpty());
        REQUIRE(cache.open(QIODevice::ReadOnly));
        return cache.readAll() == invalid;
    }
    if (scenario == "sources" || scenario == "ui") {
        podlord::Workspace workspace(profile.path());
        REQUIRE(workspace.appStore());
        REQUIRE(!workspace.importHome() && !workspace.refreshSources());
        REQUIRE(workspace.defaultSourceOrigin().startsWith(profile.path() + '/'));
        if (scenario == "sources") return true;
        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty("workspace", &workspace);
        engine.load(QUrl("qrc:/podlord/Main.qml"));
        REQUIRE(!engine.rootObjects().isEmpty());
        auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first()); REQUIRE(window);
        REQUIRE(QTest::qWaitFor([&] { return !workspace.busy(); }, 5000));
        REQUIRE(!podlord::test::visibleItem(window->contentItem(), "sourcePath"));
        REQUIRE(!podlord::test::visibleItem(window->contentItem(), "browseSourceFolder"));
        REQUIRE(workspace.setWorkspacePage("settings"));
        QTest::qWait(20);
        REQUIRE(podlord::test::clickVisible(window, "settingsSourcesSection"));
        REQUIRE(!podlord::test::visibleItem(window->contentItem(), "settingsSourcePath"));
        REQUIRE(podlord::test::clickVisible(window, "sourceActionsMenuButton"));
        REQUIRE(!podlord::test::visibleItem(window->contentItem(), "browseSourceFolder"));
        REQUIRE(!podlord::test::visibleItem(window->contentItem(), "importHomeButton"));
        REQUIRE(!podlord::test::visibleItem(window->contentItem(), "refreshSourceFilesButton"));
        REQUIRE(podlord::test::visibleItem(window->contentItem(), "browseSourceFile"));
        REQUIRE(podlord::test::visibleItem(window->contentItem(), "pasteSourceButton"));
        REQUIRE(podlord::test::visibleItem(window->contentItem(), "importGkeKeyFromSettings"));
        window->close();
        return true;
    }
    QString user = "    token: store-test-token\n";
    QString cluster;
    if (scenario == "reference_ca") cluster = "    certificate-authority: " + input.filePath("missing-ca.pem") + '\n';
    if (scenario == "reference_token") user = "    tokenFile: " + input.filePath("missing-token") + '\n';
    if (scenario == "unknown_exec") user = "    exec:\n      apiVersion: client.authentication.k8s.io/v1\n      command: unapproved-credential-program\n      interactiveMode: Never\n";
    if (scenario == "native_oidc") user = "    exec:\n      apiVersion: client.authentication.k8s.io/v1\n      command: kubectl\n      interactiveMode: Never\n      args: [oidc-login, get-token, --oidc-issuer-url=https://issuer.example.test, --oidc-client-id=podlord-store]\n";
    if (scenario == "native_aks") user = "    exec:\n      apiVersion: client.authentication.k8s.io/v1\n      command: kubelogin\n      interactiveMode: Never\n      args: [get-token, --tenant-id=11111111-1111-1111-1111-111111111111, --server-id=22222222-2222-2222-2222-222222222222, --client-id=33333333-3333-3333-3333-333333333333, --login=devicecode]\n";
    if (scenario == "native_eks") user = "    exec:\n      apiVersion: client.authentication.k8s.io/v1\n      command: aws\n      interactiveMode: Never\n      args: [eks, get-token, --cluster-name=store-example, --region=eu-central-1]\n      env:\n      - name: AWS_ACCESS_KEY_ID\n        value: STOREEXAMPLEACCESS\n      - name: AWS_SECRET_ACCESS_KEY\n        value: store-example-secret\n";
    if (scenario == "native_gke") user = "    exec:\n      apiVersion: client.authentication.k8s.io/v1\n      command: gke-gcloud-auth-plugin\n      interactiveMode: Never\n";
    const auto yaml = "apiVersion: v1\nkind: Config\nclusters:\n- name: cluster\n  cluster:\n    server: https://cluster.example.test\n" + cluster
        + "users:\n- name: user\n  user:\n" + user + "contexts:\n- name: context\n  context:\n    cluster: cluster\n    user: user\n";
    const auto original = input.filePath("config.yaml");
    QFile file(original); REQUIRE(file.open(QIODevice::WriteOnly));
    REQUIRE(file.write(yaml.toUtf8()) == yaml.toUtf8().size()); file.close();
    const auto imported = store.importFile(original);
    const auto* snapshot = std::get_if<podlord::SourceSnapshot>(&imported); REQUIRE(snapshot && snapshot->contexts.size() == 1);
    REQUIRE(file.remove());
    const auto result = store.connection(snapshot->contexts.first().id);
    if (scenario.startsWith("reference_") || scenario == "unknown_exec") return std::holds_alternative<podlord::Failure>(result);
    const auto* connection = std::get_if<podlord::ClusterConnection>(&result); REQUIRE(connection);
    if (scenario == "native_oidc" || scenario == "native_aks") return connection->exec && connection->exec->oidc && !connection->credentialReady;
    if (scenario == "native_gke") return connection->exec && connection->exec->gke && !connection->credentialReady;
    if (scenario == "native_eks") {
        REQUIRE(connection->exec && connection->exec->eks && !connection->credentialReady);
        podlord::CredentialProcess credentials;
        REQUIRE(credentials.start(*connection, "store-session"));
        const auto signedConnection = credentials.result(connection->credentialId);
        const auto* token = std::get_if<podlord::ClusterConnection>(&signedConnection);
        return token && token->credentialReady && token->authorization.startsWith("Bearer k8s-aws-v1.");
    }
    REQUIRE(scenario == "file_copy");
    return connection->authorization == "Bearer store-test-token";
}
}

int main(int argc, char** argv) {
    QGuiApplication app(argc, argv);
    QCoreApplication::setApplicationVersion("1.0.0");
    return argc == 2 && run(QString::fromUtf8(argv[1])) ? 0 : 1;
}
