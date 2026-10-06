#include "workspace.h"
#include <QGuiApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSslSocket>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>
#include <cstdio>
#include <stdexcept>

namespace {
bool require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
    return true;
}
bool write(const QString& path, const QByteArray& bytes) {
    QFile file(path);
    return require(file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size(), "Cannot prepare private external CLI input");
}
QByteArray config(const QByteArray& server = "https://127.0.0.1:9", const QByteArray& credentials = "    token: k3d-private-token\n") {
    return "apiVersion: v1\nkind: Config\nclusters:\n- name: local\n  cluster:\n    server: " + server
        + "\nusers:\n- name: local\n  user:\n" + credentials
        + "contexts:\n- name: local\n  context:\n    cluster: local\n    user: local\n";
}
// Only the external k3d executable is simulated. Stores, UI and parsing are real.
int externalCli(int argc, char** argv) {
    const QCoreApplication app(argc, argv);
    const auto args = app.arguments().mid(1);
        const auto mode = qEnvironmentVariable("PODLORD_K3D_TEST_MODE");
    QFile calls(qEnvironmentVariable("PODLORD_K3D_TEST_CALLS"));
    require(calls.open(QIODevice::WriteOnly | QIODevice::Append), "Cannot record external CLI calls");
    calls.write(QJsonDocument(QJsonArray::fromStringList(args)).toJson(QJsonDocument::Compact) + '\n');
    QByteArray output;
    if (args == QStringList{"cluster", "list", "--no-headers"} && mode == "text_fallback") output = "local 1/1 0/0 false\n";
    else if (args == QStringList{"cluster", "list", "-o", "json"}) {
        if (mode == "busy") QThread::msleep(300);
        if (mode == "text_fallback") return 2;
        if (mode == "list_timeout") QThread::msleep(35000);
        if (mode == "list_fail") { std::fputs("k3d-private-token\n", stderr); return 3; }
        if (mode == "invalid_json") output = "[invalid k3d-private-token";
        else if (mode == "not_array") output = "{}";
        else if (mode == "invalid_entry") output = "[17]";
        else if (mode == "invalid_name") output = "[{\"name\":\"../../escape\"}]";
        else if (mode == "invalid_name_control") output = "[{\"name\":\"local\\n\"}]";
        else if (mode == "empty") output = "[]";
        else if (mode == "list_large") output = QByteArray(16 * 1024 * 1024 + 1, 'x');
        else if (mode == "duplicate") output = "[{\"name\":\"local\"},{\"name\":\"local\"}]";
        else if (mode == "partial") output = "[{\"name\":\"local\"},{\"name\":\"broken\"}]";
        else {
            const auto key = mode.startsWith("alias_") ? mode.mid(6) : QString("name");
            output = QJsonDocument(QJsonArray{QJsonObject{{key, "local"}}}).toJson(QJsonDocument::Compact);
        }
    } else if (args.size() == 3 && args.first() == "kubeconfig" && args[1] == "get") {
        require(args.last() == "local" || args.last() == "broken", "Unexpected external CLI cluster argument");
        if (mode == "get_timeout") QThread::msleep(35000);
        if (mode == "get_fail" || args.last() == "broken") { std::fputs("k3d-private-token\n", stderr); return 4; }
        if (mode == "yaml_invalid") output = "invalid: [k3d-private-token";
        else if (mode == "utf8_invalid") output = QByteArray(1, static_cast<char>(0xff));
        else if (mode == "get_large") output = QByteArray(16 * 1024 * 1024 + 1, 'x');
        else {
            QFile file(qEnvironmentVariable("PODLORD_K3D_TEST_YAML"));
            require(file.open(QIODevice::ReadOnly), "External CLI configuration is unavailable");
            output = file.readAll();
        }
    } else return 5;
    require(std::fwrite(output.constData(), 1, static_cast<size_t>(output.size()), stdout) == static_cast<size_t>(output.size()), "Cannot write external CLI response");
    return 0;
}
bool click(QQuickWindow* window, QQuickItem* target) {
    if (!target || !target->isVisible() || !target->isEnabled()) return false;
    for (auto* parent = target; parent; parent = parent->parentItem()) parent->ensurePolished();
    QCoreApplication::processEvents();
    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
        target->mapToScene(QPointF(target->width() / 2, target->height() / 2)).toPoint());
    return true;
}
}

int main(int argc, char** argv) {
    try {
        if (argc > 1 && (QByteArray(argv[1]) == "cluster" || QByteArray(argv[1]) == "kubeconfig")) return externalCli(argc, argv);
        const QGuiApplication app(argc, argv);
        require(argc >= 2 && argc <= 3, "Expected K3D import scenario and optional real cluster name");
        const QString mode = QString::fromLocal8Bit(argv[1]);
        const bool real = mode == "real";
        require(!real || argc == 3, "Real test requires its own cluster name");
        QTemporaryDir temporary;
        require(temporary.isValid(), "Cannot create private import profile");
        const QString root = temporary.path();
        const QString profile = root + "/profile";
        const QString fixture = root + "/external.yaml";
        const QString callsPath = root + "/calls.jsonl";
        if (QStringList{"missing_executable", "relative_executable", "directory_executable", "non_executable"}.contains(mode)) {
            const QString path = mode == "relative_executable" ? QString("k3d") : mode == "directory_executable" ? root : root + "/absent";
            if (mode == "non_executable") {
                write(path, "not executable");
                require(QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner), "Cannot establish executable permission boundary");
            }
            require(std::holds_alternative<podlord::Failure>(podlord::KubeconfigStore(profile).importK3d(path))
                && !QFileInfo::exists(profile), "Invalid executable created a source or started a process");
            return 0;
        }
        if (!real) {
            const QString bin = root + "/bin";
            require(QDir().mkpath(bin), "Cannot create private CLI directory");
            const QString executable = bin + "/k3d"
#ifdef Q_OS_WIN
                ".exe"
#endif
                ;
            require(QFile::copy(QCoreApplication::applicationFilePath(), executable)
                && QFile::setPermissions(executable, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner), "Cannot prepare external CLI boundary");
            if (mode == "start_fail") write(executable, "not an executable image");
            qputenv("PATH", (bin + QDir::listSeparator() + qEnvironmentVariable("PATH")).toUtf8());
            qputenv("PODLORD_K3D_TEST_MODE", mode.toUtf8());
            qputenv("PODLORD_K3D_TEST_CALLS", callsPath.toUtf8());
            qputenv("PODLORD_K3D_TEST_YAML", fixture.toUtf8());
            QByteArray yaml = config();
            if (mode == "zero") yaml = config("https://0.0.0.0:9");
            if (mode == "localhost") yaml = config("https://localhost:9");
            if (mode == "secret_endpoint") yaml = config("https://0.0.0.0:9", "    token: https://0.0.0.0:9\n");
            if (mode == "relative_credential") yaml = config("https://127.0.0.1:9", "    tokenFile: relative-token\n");
            if (mode == "relative_exec") yaml = config("https://127.0.0.1:9", "    exec:\n      apiVersion: client.authentication.k8s.io/v1\n      interactiveMode: Never\n      command: ./login\n");
            write(fixture, yaml);
            if (mode == "invalid_profile") write(profile, "not a directory");
        }
        podlord::Workspace workspace(profile);
        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty("workspace", &workspace);
        engine.load(QUrl("qrc:/podlord/Main.qml"));
        require(!engine.rootObjects().isEmpty(), "Cannot load the real source workspace");
        auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
        require(window && QTest::qWaitFor([&] { return !workspace.busy(); }, 5000), "Initial catalogs did not settle");
        if (mode == "ui_narrow") { window->setWidth(320); window->setHeight(720); }
        auto* button = window->findChild<QQuickItem*>("importK3dButton");
        int requests = 0;
        QObject::connect(&workspace, &podlord::Workspace::requestStarted, &workspace, [&] { ++requests; });
        bool finished = false;
        QObject::connect(&workspace, &podlord::Workspace::sourceImportFinished, &workspace, [&](bool) { finished = true; });
        if (mode == "palette") {
            QTest::keyClick(window, Qt::Key_K, Qt::ControlModifier);
            QCoreApplication::processEvents();
            QTest::keyClick(window, Qt::Key_K);
            QTest::keyClick(window, Qt::Key_3);
            QTest::keyClick(window, Qt::Key_D);
            QTest::keyClick(window, Qt::Key_Return);
        } else require(click(window, button), "Public Import K3D action is missing or unusable");
        if (mode == "busy") require(workspace.busy() && !button->isEnabled() && !workspace.importK3d(), "An active import admitted another CLI operation");
        require(QTest::qWaitFor([&] { return finished && !workspace.busy(); }, 45000), "K3D import did not settle");
        require(requests == 0 && workspace.currentSession().isEmpty() && workspace.sessions().isEmpty(), "Import contacted Kubernetes or opened a session");
        require(!workspace.sourceImportNotice().contains("k3d-private-token") && !workspace.sourceImportError().contains("k3d-private-token"), "CLI failure exposed credential data");
        for (const auto& issue : workspace.sourceImportIssues()) require(!issue.toMap().value("message").toString().contains("k3d-private-token"), "Per-cluster feedback exposed credential data");
        const QStringList rejected{"invalid_json", "not_array", "invalid_entry", "invalid_name", "invalid_name_control", "list_fail", "get_fail", "yaml_invalid", "utf8_invalid", "list_large", "get_large", "list_timeout", "get_timeout", "invalid_profile", "start_fail"};
        if (rejected.contains(mode)) {
            require(workspace.contexts().isEmpty() && (!workspace.sourceImportError().isEmpty() || !workspace.sourceImportIssues().isEmpty()), "Invalid CLI input did not fail explicitly");
            require(mode != "invalid_profile" || !QFileInfo::exists(callsPath), "Invalid profile executed an external tool");
            return 0;
        }
        if (mode == "empty") {
            require(workspace.contexts().isEmpty() && workspace.sourceImportError().isEmpty() && !workspace.sourceImportNotice().isEmpty(), "Empty K3D catalog has no visible outcome");
            return 0;
        }
        const podlord::KubeconfigStore store(profile);
        auto loaded = store.list();
        require(std::holds_alternative<podlord::SourceCatalog>(loaded), "Generated source cannot be read back");
        const QString origin = "podlord-generated://k3d/" + (real ? QString::fromLocal8Bit(argv[2]) : QString("local"));
        const auto sources = std::get<podlord::SourceCatalog>(loaded).sources;
        const podlord::SourceSnapshot* imported = nullptr;
        for (const auto& source : sources) if (source.sourcePath == origin) imported = &source;
        require(imported && !imported->contexts.isEmpty(), "Generated source lost its virtual identity or contexts");
        const QString context = imported->contexts.first().id;
        const auto connection = store.connection(context);
        if (mode == "relative_credential" || mode == "relative_exec") {
            require(std::holds_alternative<podlord::Failure>(connection), "Generated source silently resolved a relative credential or executable");
            return 0;
        }
        require(std::holds_alternative<podlord::ClusterConnection>(connection)
            && std::get<podlord::ClusterConnection>(connection).server.host() == "127.0.0.1", "Generated local endpoint was not normalized safely");
        if (mode == "secret_endpoint") require(std::get<podlord::ClusterConnection>(connection).authorization == "Bearer https://0.0.0.0:9", "Endpoint normalization modified credentials");
        require(mode != "duplicate" || sources.size() == 1, "Duplicate cluster list created duplicate sources");
        require(mode != "partial" || (sources.size() == 1 && workspace.sourceImportIssues().size() == 1), "Partial failure discarded healthy imports");
        if (mode == "refresh_generated") {
            require(!workspace.refreshSources() && !workspace.sourceImportError().isEmpty(), "File refresh tried to read a virtual source");
        }
        if (mode == "repeat" || mode == "changed") {
            if (mode == "changed") write(fixture, config("https://127.0.0.1:10"));
            finished = false;
            require(click(window, button) && QTest::qWaitFor([&] { return finished && !workspace.busy(); }, 10000), "Repeated import did not settle");
            loaded = store.list();
            require(std::holds_alternative<podlord::SourceCatalog>(loaded)
                && std::get<podlord::SourceCatalog>(loaded).sources.size() == (mode == "changed" ? 2 : 1), "Reimport broke immutable snapshot identity");
            const auto original = store.connection(context);
            require(std::holds_alternative<podlord::ClusterConnection>(original)
                && std::get<podlord::ClusterConnection>(original).server.port() == 9, "Reimport retargeted the original context");
        }
        if (real) {
            const bool loaded = workspace.openContext(context) && QTest::qWaitFor([&] { return !workspace.busy() && workspace.totalResourceCount() > 0; }, 90000);
            if (!loaded) std::fprintf(stderr, "Local generated import: backend=%s requests=%d resources=%d status=%s error=%s\n",
                QSslSocket::activeBackend().toUtf8().constData(), requests, workspace.totalResourceCount(), workspace.status().toUtf8().constData(), workspace.error().toUtf8().constData());
            require(loaded, "Generated kubeconfig cannot load its real local Kubernetes resources");
            require(QTest::qWaitFor([&] { return !workspace.loading(); }, 180000), "Real local discovery did not finish within the integration-test budget");
            const auto evidence = qEnvironmentVariable("PODLORD_TEST_EVIDENCE_DIR");
            if (!evidence.isEmpty()) require(window->grabWindow().save(evidence + "/k3d-generated-source-resources.png"), "Cannot save actual local Kubernetes frame");
        }
        std::printf("K3D public import scenario passed: %s\n", mode.toUtf8().constData());
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}
