#include "workspace.h"
#include "ui_input.h"
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QProcessEnvironment>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSslKey>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>
#include <cstdio>
#include <functional>
#ifdef Q_OS_UNIX
#include <cerrno>
#include <csignal>
#endif

namespace {
bool waitFor(const std::function<bool()>& ready, int timeout = 7000) {
    QElapsedTimer timer; timer.start();
    while (!ready() && timer.elapsed() < timeout) QTest::qWait(10);
    return ready();
}
QQuickItem* find(QQuickItem* root, const QString& name) {
    if (root->objectName() == name) return root;
    for (auto* child : root->childItems()) if (auto* item = find(child, name)) return item;
    return nullptr;
}
QQuickItem* item(QQuickWindow* window, const QString& name) { return find(window->contentItem(), name); }
bool click(QQuickWindow* window, const QString& name) {
    if (!podlord::test::revealWorkspaceAction(window, name)) return false;
    auto* target = item(window, name);
    if (!target || !target->isVisible() || !target->isEnabled()) return false;
    QList<QQuickItem*> ancestors;
    for (auto* parent = target; parent; parent = parent->parentItem()) ancestors.prepend(parent);
    for (auto* parent : ancestors) parent->ensurePolished();
    QCoreApplication::processEvents();
    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, target->mapToScene(QPointF(target->width() / 2, target->height() / 2)).toPoint());
    return true;
}
QString text(QQuickWindow* window, const QString& name) { const auto* target = item(window, name); return target ? target->property("text").toString() : QString{}; }
QString quote(const QString& value) {
    const auto array = QJsonDocument(QJsonArray{value}).toJson(QJsonDocument::Compact);
    return QString::fromUtf8(array.mid(1, array.size() - 2));
}
bool write(const QString& path, const QByteArray& bytes) {
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.setPermissions(QFile::ReadOwner | QFile::WriteOwner) && file.write(bytes) == bytes.size();
}
QByteArray read(const QString& path) { QFile file(path); return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{}; }

// This process is the external identity-provider boundary, not an internal auth replacement.
int provider(const QStringList& args) {
    if (args.size() != 6 || args[5] != "literal;$(must-not-run)") return 3;
    const auto scenario = args[2];
    const auto counter = args[3];
    const auto info = QJsonDocument::fromJson(qgetenv("KUBERNETES_EXEC_INFO")).object();
    if (info["kind"] != "ExecCredential" || info["spec"].toObject()["interactive"] != false
        || info["spec"].toObject()["cluster"].toObject()["server"].toString().isEmpty()
        || qgetenv("PODLORD_AUTH_TEST") != "external-only" || QFileInfo(QDir::currentPath()).canonicalFilePath() != QFileInfo(args[4]).canonicalFilePath()) return 4;
    if (!write(counter, read(counter) + "call\n")) return 5;
    if (scenario == "shutdown_running" && !write(counter + ".pid", QByteArray::number(QCoreApplication::applicationPid()))) return 5;
    if (scenario == "cancel_running") QTest::qSleep(10000);
    if (scenario == "shutdown_running") QTest::qSleep(10000);
    if (scenario == "shared_running") QTest::qSleep(1000);
    if (scenario == "failed_exit") { std::fputs("secret-provider-diagnostic", stderr); return 7; }
    QByteArray output;
    QJsonObject status{{"token", "exec-test-token"}};
    if (scenario == "no_credentials") status = {};
    if (scenario == "bad_token") status["token"] = "token\r\nInjected: secret";
    if (scenario == "expired") status["expirationTimestamp"] = "2000-01-01T00:00:00Z";
    if (scenario == "bad_date") status["expirationTimestamp"] = "2099-01-01T00:00:00";
    if (scenario == "expiry") status["expirationTimestamp"] = QDateTime::currentDateTimeUtc().addMSecs(3000).toString(Qt::ISODateWithMs);
    QJsonObject credential{{"kind", "ExecCredential"}, {"apiVersion", info["apiVersion"]}, {"status", status}};
    if (scenario == "wrong_version") credential["apiVersion"] = "client.authentication.k8s.io/v0";
    if (scenario == "missing_status") credential.remove("status");
    output = QJsonDocument(credential).toJson(QJsonDocument::Compact);
    if (scenario == "invalid_json") output = "{secret-invalid-json";
    if (scenario == "large_output") output = QByteArray(2 * 1024 * 1024, 'x');
    return std::fwrite(output.constData(), 1, static_cast<size_t>(output.size()), stdout) == static_cast<size_t>(output.size()) ? 0 : 8;
}

class Kubernetes final : public QTcpServer {
public:
    int calls = 0;
    bool reject = false;
    QByteArray authorization;
    Kubernetes() {
        connect(this, &QTcpServer::newConnection, this, [this] {
            while (hasPendingConnections()) {
                auto* socket = nextPendingConnection();
                auto bytes = std::make_shared<QByteArray>();
                connect(socket, &QTcpSocket::readyRead, this, [this, socket, bytes] {
                    bytes->append(socket->readAll());
                    if (!bytes->contains("\r\n\r\n")) return;
                    socket->disconnect(this); ++calls;
                    for (const auto& line : bytes->split('\n')) if (line.startsWith("Authorization:")) authorization = line.mid(14).trimmed();
                    const auto path = QUrl::fromEncoded(bytes->split(' ')[1]).path();
                    QJsonObject document;
                    if (path == "/api") document = {{"versions", QJsonArray{"v1"}}};
                    else if (path == "/apis") document = {{"groups", QJsonArray{}}};
                    else if (path == "/api/v1") document = {{"resources", QJsonArray{QJsonObject{{"name", "pods"}, {"kind", "Pod"}, {"namespaced", true}, {"verbs", QJsonArray{"get", "list"}}}}}};
                    else {
                        const QJsonObject pod{{"kind", "Pod"}, {"metadata", QJsonObject{{"name", "real-pod"}, {"namespace", "default"}, {"uid", "pod-id"}}}};
                        document = path == "/api/v1/pods" ? QJsonObject{{"items", QJsonArray{pod}}, {"metadata", QJsonObject{}}} : pod;
                    }
                    const auto body = QJsonDocument(document).toJson(QJsonDocument::Compact);
                    socket->write("HTTP/1.1 " + QByteArray(reject ? "401" : "200") + " Result\r\nConnection: close\r\nContent-Type: application/json\r\nContent-Length: " + QByteArray::number(body.size()) + "\r\n\r\n" + body);
                    socket->disconnectFromHost();
                });
                connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            }
        });
    }
};

bool execute(const QString& scenario) {
    QTemporaryDir temporary;
    if (!temporary.isValid()) return false;
    Kubernetes server;
    if (!server.listen(QHostAddress::LocalHost, 0)) return false;
    const auto source = temporary.filePath("source.config");
    const auto counter = temporary.filePath("provider.calls");
    const auto profile = temporary.filePath("profile");
    const bool shared = scenario == "shared_running" || scenario == "shared_401";
    QString program = QCoreApplication::applicationFilePath();
    if (scenario == "relative_command") {
        if (!QFile::link(program, temporary.filePath("provider"))) return false;
        program = "./provider";
    }
    if (scenario == "failed_start") program = temporary.filePath("missing-provider");
    const auto version = scenario == "v1beta1" ? "client.authentication.k8s.io/v1beta1" : "client.authentication.k8s.io/v1";
    QString mode = "\n      interactiveMode: Never";
    if (scenario == "v1beta1" || scenario == "invalid_mode") mode.clear();
    if (scenario == "requires_stdin") mode = "\n      interactiveMode: Always";
    const auto arguments = QJsonDocument(QJsonArray{"provider", scenario, counter, temporary.path(), "literal;$(must-not-run)"}).toJson(QJsonDocument::Compact);
    const auto yaml = "apiVersion: v1\nkind: Config\nclusters:\n- name: local\n  cluster:\n    server: http://127.0.0.1:" + QByteArray::number(server.serverPort())
        + "\nusers:\n- name: local\n  user:\n" + (scenario == "mixed_credentials" ? "    token: must-not-send\n" : "")
        + "    exec:\n      command: " + quote(program).toUtf8() + "\n      apiVersion: " + version + mode.toUtf8()
        + "\n      provideClusterInfo: true\n      args: " + (scenario == "invalid_args" ? QByteArray("{}") : arguments)
        + "\n      env: " + (scenario == "invalid_env" ? QByteArray("[{name: KUBERNETES_EXEC_INFO, value: forbidden}]") : QByteArray("[{name: PODLORD_AUTH_TEST, value: external-only}]"))
        + "\ncontexts:\n- name: local\n  context: {cluster: local, user: local}\n"
        + (shared ? "- name: second\n  context: {cluster: local, user: local}\n" : "");
    if (!write(source, yaml)) return false;
    auto workspace = std::make_unique<podlord::Workspace>(profile);
    auto engine = std::make_unique<QQmlApplicationEngine>();
    engine->rootContext()->setContextProperty("workspace", workspace.get()); engine->load(QUrl("qrc:/podlord/Main.qml"));
    if (engine->rootObjects().isEmpty()) return false;
    auto* window = qobject_cast<QQuickWindow*>(engine->rootObjects().first());
    if (!window || !waitFor([&] { return !workspace->busy(); })) return false;
    const auto failed = [&](const char* stage) {
        auto* selector = item(window, "contexts");
        std::fprintf(stderr, "Authentication stage failed: %s; busy=%d running=%d required=%d rows=%d API-calls=%d provider-calls=%lld error-present=%d context-ready=%d\n", stage, workspace->busy(), workspace->authenticationRunning(), workspace->authenticationRequired(), workspace->table()->rowCount(), server.calls, static_cast<long long>(read(counter).count('\n')), !workspace->error().isEmpty(), selector && !selector->property("currentValue").toString().isEmpty());
        const auto evidence = qEnvironmentVariable("PODLORD_AUTH_FAILURE_IMAGE");
        if (!evidence.isEmpty()) { QTest::qWait(30); window->grabWindow().save(evidence, "PNG"); }
        return false;
    };
    // Wait for the scheduled startup scan before issuing the import action.
    QTest::qWait(20);
    if (!waitFor([&] { return !workspace->busy(); })) return false;
    auto* field = item(window, "sourcePath");
    if (!field) return false;
    field->forceActiveFocus(); QTest::keySequence(window, QKeySequence::SelectAll);
    for (const auto c : source) QTest::keyClick(window, c.toLatin1());
    if (!click(window, "importButton") || !waitFor([&] { return workspace->contexts().size() == (shared ? 2 : 1) && !workspace->busy(); }) || !click(window, "openContext")) return failed("import/open context");
    const bool badConfig = scenario == "invalid_mode" || scenario == "requires_stdin" || scenario == "invalid_args" || scenario == "invalid_env" || scenario == "mixed_credentials";
    if (badConfig) return waitFor([&] { return !workspace->error().isEmpty(); }) && server.calls == 0 && read(counter).isEmpty();
    if (!waitFor([&] { return workspace->authenticationRequired() && !workspace->busy(); }) || server.calls != 0 || !read(counter).isEmpty()) return failed("await login confirmation");
    if (!workspace->authenticationPrompt().contains(program == "./provider" ? temporary.filePath("provider") : program)) return failed("login target");
    if (!click(window, "authenticationButton") || !waitFor([&] { return item(window, "authenticationAccept") && item(window, "authenticationAccept")->isVisible(); })) return failed("login prompt");
    if (scenario == "cancel_confirmation") {
        if (!click(window, "authenticationCancel")) return false;
        QTest::qWait(100); return server.calls == 0 && read(counter).isEmpty() && workspace->authenticationRequired();
    }
    if (!click(window, "authenticationAccept")) return failed("confirm login");
    const auto first = workspace->currentSession();
    const auto openSecond = [&] {
        if (!click(window, "sourcesButton")) return false;
        if (!click(window, "contexts")) return false;
        QTest::keyClick(window, Qt::Key_Down); QTest::keyClick(window, Qt::Key_Return);
        return click(window, "openContext") && waitFor([&] { return !workspace->busy() && workspace->currentSession() != first; });
    };
    if (scenario == "shared_running") {
        if (!waitFor([&] { return workspace->authenticationRunning() && read(counter) == "call\n"; }) || !openSecond()) return false;
        if (!workspace->authenticationRunning() || item(window, "authenticationButton")->isEnabled()) return false;
    }
    if (scenario == "shutdown_running") {
        if (!waitFor([&] { return workspace->authenticationRunning() && !read(counter + ".pid").isEmpty(); })) return false;
        const auto pid = read(counter + ".pid").toLongLong();
        QElapsedTimer timer; timer.start(); engine.reset(); workspace.reset();
#ifdef Q_OS_UNIX
        return timer.elapsed() < 1500 && ::kill(static_cast<pid_t>(pid), 0) == -1 && errno == ESRCH && server.calls == 0;
#else
        Q_UNUSED(pid);
        return timer.elapsed() < 1500 && server.calls == 0;
#endif
    }
    if (scenario == "cancel_running") {
        if (!waitFor([&] { return workspace->authenticationRunning() && !read(counter).isEmpty(); })) return failed("provider start");
        if (!click(window, "cancelAuthentication")) return false;
        return waitFor([&] { return !workspace->authenticationRunning() && workspace->error().contains("cancelled"); }) && server.calls == 0 && read(counter) == "call\n";
    }
    const bool invalid = scenario == "invalid_json" || scenario == "wrong_version" || scenario == "missing_status" || scenario == "no_credentials" || scenario == "bad_token"
        || scenario == "expired" || scenario == "bad_date" || scenario == "failed_exit" || scenario == "failed_start" || scenario == "large_output";
    if (invalid) {
        if (!waitFor([&] { return !workspace->busy() && !workspace->authenticationRunning() && !workspace->error().isEmpty(); })) return false;
        QTest::qWait(100);
        return server.calls == 0 && workspace->authenticationRequired() && !workspace->error().contains("secret")
            && read(counter) == (scenario == "failed_start" ? QByteArray{} : QByteArray("call\n"));
    }
    if (!waitFor([&] { return workspace->table()->rowCount() == 1 && !workspace->loading(); }) || server.authorization != "Bearer exec-test-token" || read(counter) != "call\n") return failed("authenticated resources");
    const auto session = workspace->currentSession();
    if (scenario == "shared_401") {
        if (!openSecond()) return failed("open shared session");
        if (!waitFor([&] { return workspace->table()->rowCount() == 1 && !workspace->loading(); })) return failed("shared cached resources");
        server.reject = true;
        if (!click(window, "refreshButton") || !waitFor([&] { return workspace->authenticationRequired(); })) return failed("shared rejection");
        const auto calls = server.calls;
        if (!workspace->activate(first) || !waitFor([&] { return !workspace->busy() && workspace->authenticationRequired(); })) return failed("shared rejected activation");
        QTest::qWait(100);
        return server.calls == calls && read(counter) == "call\n" && workspace->table()->rowCount() == 1;
    }
    if (scenario == "expiry") {
        if (!waitFor([&] { return workspace->authenticationRequired(); })) return false;
        const auto calls = server.calls;
        QTest::qWait(100);
        return workspace->table()->rowCount() == 1 && server.calls == calls && read(counter) == "call\n" && item(window, "authenticationButton")->isVisible();
    }
    if (scenario == "retry_401") {
        server.reject = true;
        if (!click(window, "refreshButton") || !waitFor([&] { return workspace->authenticationRequired(); })) return false;
        const auto calls = server.calls; QTest::qWait(100);
        if (server.calls != calls || read(counter) != "call\n" || workspace->table()->rowCount() != 1) return false;
        if (!click(window, "authenticationButton") || !waitFor([&] { return item(window, "authenticationAccept")->isVisible(); }) || !click(window, "authenticationAccept")) return false;
        if (!waitFor([&] { return read(counter) == "call\ncall\n" && !workspace->authenticationRunning() && workspace->authenticationRequired() && !workspace->busy(); })) return false;
        QTest::qWait(100); return read(counter) == "call\ncall\n" && workspace->table()->rowCount() == 1;
    }
    if (scenario == "cache_reopen") {
        if (!workspace->close(session) || !waitFor([&] { return workspace->currentSession().isEmpty() && !workspace->busy(); })) return false;
        if (!workspace->activate(session) || !waitFor([&] { return workspace->table()->rowCount() == 1 && !workspace->busy(); })) return false;
        return !workspace->authenticationRequired() && read(counter) == "call\n";
    }
    if (scenario == "restore") {
        engine.reset(); workspace.reset();
        podlord::Workspace restored(profile);
        return waitFor([&] { return restored.authenticationRequired() && !restored.busy(); }) && read(counter) == "call\n";
    }
    return !workspace->authenticationRequired() && workspace->error().isEmpty();
}
bool realCluster(const QString& original) {
    QTemporaryDir temporary;
    if (!temporary.isValid()) return false;
    const auto profile = temporary.filePath("profile");
    const podlord::KubeconfigStore store(profile);
    const auto imported = store.importFile(original);
    const auto* source = std::get_if<podlord::SourceSnapshot>(&imported);
    if (!source || source->contexts.isEmpty()) return false;
    const auto resolved = store.connection(source->contexts.first().id);
    const auto* connection = std::get_if<podlord::ClusterConnection>(&resolved);
    if (!connection || connection->tls.localCertificate().isNull() || connection->tls.privateKey().isNull()) return false;
    QByteArray certificates, authorities;
    for (const auto& certificate : connection->tls.localCertificateChain()) certificates += certificate.toPem();
    for (const auto& certificate : connection->tls.caCertificates()) authorities += certificate.toPem();
    const auto credentialFile = temporary.filePath("provider-credential.json");
    const auto counter = temporary.filePath("provider.calls");
    const QJsonObject credential{{"kind", "ExecCredential"}, {"apiVersion", "client.authentication.k8s.io/v1"},
        {"status", QJsonObject{{"clientCertificateData", QString::fromUtf8(certificates)}, {"clientKeyData", QString::fromUtf8(connection->tls.privateKey().toPem())}}}};
    if (!write(credentialFile, QJsonDocument(credential).toJson(QJsonDocument::Compact))) return false;
    const auto input = temporary.filePath("exec.config");
    const auto args = QJsonDocument(QJsonArray{"provider-certificate", credentialFile, counter}).toJson(QJsonDocument::Compact);
    const auto yaml = "apiVersion: v1\nkind: Config\nclusters:\n- name: exec-cluster\n  cluster:\n    server: " + quote(connection->server.toString()).toUtf8()
        + "\n    certificate-authority-data: " + authorities.toBase64()
        + "\nusers:\n- name: exec-user\n  user:\n    exec:\n      command: " + quote(QCoreApplication::applicationFilePath()).toUtf8()
        + "\n      apiVersion: client.authentication.k8s.io/v1\n      interactiveMode: Never\n      provideClusterInfo: true\n      args: " + args
        + "\ncontexts:\n- name: exec-local\n  context: {cluster: exec-cluster, user: exec-user}\n";
    if (!write(input, yaml)) return false;
    podlord::Workspace workspace(profile);
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("workspace", &workspace); engine.load(QUrl("qrc:/podlord/Main.qml"));
    if (engine.rootObjects().isEmpty()) return false;
    auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
    if (!window) return false;
    QTest::qWait(20);
    if (!waitFor([&] { return !workspace.busy(); })) return false;
    auto* field = item(window, "sourcePath");
    if (!field) return false;
    field->forceActiveFocus(); for (const auto c : input) QTest::keyClick(window, c.toLatin1());
    if (!click(window, "importButton") || !waitFor([&] { return workspace.contexts().size() == 2 && !workspace.busy(); })) return false;
    // The newly imported source is first in the authoritative recency ordering.
    if (!click(window, "openContext") || !waitFor([&] { return workspace.authenticationRequired() && !workspace.busy(); }) || !read(counter).isEmpty()) return false;
    if (!click(window, "authenticationButton") || !waitFor([&] { return item(window, "authenticationAccept") && item(window, "authenticationAccept")->isVisible(); }) || !click(window, "authenticationAccept")) return false;
    if (!waitFor([&] { return !workspace.authenticationRunning() && !workspace.busy() && !workspace.authenticationRequired(); })) return false;
    auto* filter = item(window, "resourceFilter");
    if (!filter || !filter->isEnabled()) return false;
    filter->forceActiveFocus(); for (const auto c : QByteArray("podlord-native-e2e")) QTest::keyClick(window, c);
    if (!waitFor([&] { return workspace.table()->rowCount() == 1 && item(window, "cell_0_0"); }, 60000)) {
        std::fprintf(stderr, "Exec certificate Kubernetes read failed: %s %s\n", qPrintable(workspace.error()), qPrintable(workspace.status())); return false;
    }
    return click(window, "cell_0_0") && waitFor([&] { return workspace.inspected().contains("fetchedAt"); })
        && workspace.inspected().contains("ConfigMap") && read(counter) == "call\n";
}
} // namespace
int main(int argc, char** argv) {
    if (argc > 1 && QByteArray(argv[1]) == "provider") { QCoreApplication app(argc, argv); return provider(app.arguments()); }
    if (argc == 4 && QByteArray(argv[1]) == "provider-certificate") {
        QCoreApplication app(argc, argv);
        const auto info = QJsonDocument::fromJson(qgetenv("KUBERNETES_EXEC_INFO")).object();
        if (info["spec"].toObject()["cluster"].toObject()["certificate-authority-data"].toString().isEmpty()) return 4;
        const auto output = read(QString::fromLocal8Bit(argv[2]));
        if (!write(QString::fromLocal8Bit(argv[3]), read(QString::fromLocal8Bit(argv[3])) + "call\n")) return 5;
        return std::fwrite(output.constData(), 1, static_cast<size_t>(output.size()), stdout) == static_cast<size_t>(output.size()) ? 0 : 6;
    }
    QGuiApplication app(argc, argv);
    if (argc != 2 && argc != 3) return 2;
    const bool passed = argc == 3 ? QByteArray(argv[1]) == "real" && realCluster(QString::fromLocal8Bit(argv[2])) : execute(QString::fromLocal8Bit(argv[1]));
    if (!passed) std::fprintf(stderr, "Authentication UI scenario failed: %s\n", argv[1]);
    return passed ? 0 : 1;
}
