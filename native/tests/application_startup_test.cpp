#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QScopeGuard>
#include <QStandardPaths>
#include <QSslSocket>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QUrl>
#include <cstdio>
#include <stdexcept>

namespace {
bool require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
    return true;
}
bool write(const QString& path, const QByteArray& bytes) {
    require(QDir().mkpath(QFileInfo(path).absolutePath()), "Cannot create isolated input directory");
    QFile file(path);
    return require(file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size(), "Cannot write isolated input");
}
bool execute(const QString& binary, const QString& scenario, const QString& expectedVersion, const QString& sourceBinary) {
    require(QStringList{"default", "ambient_kubeconfig", "ambient_exec", "profile_file", "profile_symlink", "explicit_empty", "explicit_override",
        "help", "version", "help_invalid_profile", "version_invalid_profile", "unknown_option", "source_file", "source_url", "source_folder",
        "source_partial", "source_missing", "source_invalid", "source_empty", "source_repeated", "source_exec", "source_repeat_import",
        "source_changed", "help_with_source", "version_with_source"}.contains(scenario), "Unknown application startup scenario");
    const bool explicitSource = scenario.startsWith("source_");
    const bool metadata = scenario.startsWith("help") || scenario.startsWith("version");
    QTemporaryDir temporary;
    require(temporary.isValid(), "Cannot create isolated startup home");
    const auto home = temporary.filePath("home");
    require(QDir().mkpath(home), "Cannot create startup home");
    auto environment = QProcessEnvironment::systemEnvironment();
    for (const auto& name : {"HOME", "USERPROFILE", "CFFIXED_USER_HOME"}) environment.insert(name, home);
    environment.insert("XDG_DATA_HOME", home + "/data");
    environment.insert("APPDATA", home + "/roaming");
    environment.insert("LOCALAPPDATA", home + "/local");
    environment.insert("QT_QPA_PLATFORM", qEnvironmentVariable("PODLORD_TEST_QPA_PLATFORM", "offscreen"));
    environment.insert("QT_QUICK_BACKEND", "software");
    environment.insert("QT_QUICK_CONTROLS_STYLE", "Fusion");
    if (!environment.contains("LLVM_PROFILE_FILE")) environment.insert("LLVM_PROFILE_FILE", temporary.filePath("startup-%p.profraw"));
    // Resolve the real platform path in this environment before starting the app.
    // If platform isolation is unavailable, never launch against the user's data.
    QProcess probe;
    probe.setProcessEnvironment(environment);
    probe.start(QCoreApplication::applicationFilePath(), {"profile-path"});
    require(probe.waitForFinished(5000) && probe.exitCode() == 0, "Cannot resolve isolated platform profile");
    const auto profile = QString::fromUtf8(probe.readAllStandardOutput()).trimmed();
    require(profile.startsWith(home + "/") && QDir::isAbsolutePath(profile), "Platform profile escaped isolated home; app was not launched");

    QTcpServer boundary;
    require(boundary.listen(QHostAddress::LocalHost, 0), "Cannot listen at external Kubernetes boundary");
    int calls = 0;
    QObject::connect(&boundary, &QTcpServer::newConnection, &boundary, [&] {
        while (auto* socket = boundary.nextPendingConnection()) {
            ++calls; socket->disconnectFromHost(); socket->deleteLater();
        }
    });
    const auto ambient = home + "/.kube/config";
    const auto marker = temporary.filePath("provider-started");
    const auto kubeconfig = "apiVersion: v1\nkind: Config\ncurrent-context: ambient\nclusters:\n- name: ambient\n  cluster: {server: 'http://127.0.0.1:"
        + QByteArray::number(boundary.serverPort()) + "'}\nusers:\n- name: ambient\n  user:\n";
    const auto credentials = scenario == "ambient_exec" || scenario == "source_exec" || metadata || scenario == "unknown_option"
        ? "    exec:\n      command: " + QCoreApplication::applicationFilePath().toUtf8()
            + "\n      apiVersion: client.authentication.k8s.io/v1\n      interactiveMode: Never\n      args: [provider, '" + marker.toUtf8() + "']\n"
        : QByteArray("    token: ambient-startup-secret\n");
    const auto original = kubeconfig + credentials + "contexts:\n- name: ambient\n  context: {cluster: ambient, user: ambient}\n";
    write(ambient, original);
    environment.insert("KUBECONFIG", ambient);
    environment.insert("PODLORD_HOME", home + "/legacy");
    QStringList arguments;
    const bool rejected = scenario == "profile_file" || scenario == "profile_symlink" || scenario == "explicit_empty"
        || scenario == "source_empty" || scenario == "source_repeated";
    if (scenario == "profile_file" || scenario == "explicit_override") write(profile, "not-a-profile-directory\n");
    if (scenario == "profile_symlink") {
        require(QDir().mkpath(QFileInfo(profile).absolutePath()) && QDir().mkpath(temporary.filePath("linked-profile")), "Cannot prepare profile symlink");
        require(QFile::link(temporary.filePath("linked-profile"), profile), "Cannot create profile symlink");
    }
    if (scenario == "explicit_override") arguments = {"--profile", temporary.filePath("explicit-profile")};
    if (scenario == "explicit_empty") arguments = {"--profile", ""};
    const auto explicitPath = temporary.filePath("explicit inputs/config # %.yaml");
    QByteArray sourceContents = original;
    if (explicitSource || scenario.endsWith("with_source")) {
        sourceContents.replace("ambient", "explicit-source");
        if (scenario == "source_invalid") sourceContents = "apiVersion: [\n";
        write(explicitPath, sourceContents);
        QString input = explicitPath;
        if (scenario == "source_url") input = QUrl::fromLocalFile(input).toString(QUrl::FullyEncoded);
        if (scenario == "source_folder" || scenario == "source_partial") input = QFileInfo(explicitPath).absolutePath();
        if (scenario == "source_partial") write(QFileInfo(explicitPath).absolutePath() + "/invalid.yaml", "apiVersion: [\n");
        if (scenario == "source_missing") input = temporary.filePath("missing-config");
        if (scenario == "source_empty") input.clear();
        arguments = {"--kubeconfig", input};
        if (scenario == "source_repeated") arguments.append({"--kubeconfig", explicitPath});
    }
    if (metadata) {
        if (scenario.endsWith("invalid_profile")) {
            write(profile, "not-a-profile-directory\n");
            arguments = {"--profile", profile};
        }
        arguments.append(scenario.startsWith("version") ? "--version" : "--help");
    }
    if (scenario == "unknown_option") arguments = {"--not-a-podlord-option"};
    QProcess process;
    process.setProcessEnvironment(environment);
    process.setWorkingDirectory(temporary.path());
    const auto cleanup = qScopeGuard([&] {
        if (process.state() != QProcess::NotRunning) {
            process.terminate();
            if (!process.waitForFinished(3000)) { process.kill(); process.waitForFinished(3000); }
        }
    });
    process.start(binary, arguments);
    require(process.waitForStarted(5000), "Native application failed to start");
    if (metadata || scenario == "unknown_option") {
        require(process.waitForFinished(5000) && process.exitStatus() == QProcess::NormalExit, "Command-line metadata did not exit normally");
        QCoreApplication::processEvents();
        const auto output = process.readAllStandardOutput();
        const auto errors = process.readAllStandardError();
        require(process.exitCode() == (metadata ? 0 : 1), "Unexpected command-line exit status");
        require(calls == 0 && !QFileInfo(marker).exists() && !QFileInfo(profile + "/kubeconfigs").exists(), "Command-line metadata contacted Kubernetes or imported ambient data");
        require(!errors.contains("ambient-startup-secret") && !output.contains("ambient-startup-secret"), "Command-line output exposed an ambient credential");
        if (scenario.startsWith("version")) require(!expectedVersion.isEmpty() && output.trimmed() == "Podlord Native " + expectedVersion.toUtf8(), "Application version differs from the build version");
        else if (scenario.startsWith("help")) require(output.contains("--profile") && output.contains("--version") && output.contains("--help")
            && output.contains("--kubeconfig"), "Application help omitted supported command-line actions");
        else require(errors.contains("not-a-podlord-option"), "Unknown option lacks explicit rejection feedback");
        if (scenario.endsWith("invalid_profile")) {
            QFile retained(profile);
            require(retained.open(QIODevice::ReadOnly) && retained.readAll() == "not-a-profile-directory\n", "Metadata command modified a rejected profile");
        } else require(!QFileInfo::exists(profile), "Metadata command created an application profile");
        return true;
    }
    if (rejected) {
        require(process.waitForFinished(5000) && process.exitStatus() == QProcess::NormalExit && process.exitCode() == 2,
            "Invalid default or explicit profile was not rejected");
        require(process.readAllStandardError().contains(explicitSource ? "kubeconfig" : "application profile"), "Argument rejection lacks understandable feedback");
        require(calls == 0 && !QFileInfo(marker).exists() && !QFileInfo(profile + "/kubeconfigs").exists(), "Rejected arguments imported a source or invoked authentication");
        return true;
    }
    const auto sourceRows = [&] {
        require(!sourceBinary.isEmpty(), "Startup source checks require the real source CLI");
        QProcess listing;
        listing.setProcessEnvironment(environment);
        listing.start(sourceBinary, {"--profile", profile, "list"});
        require(listing.waitForFinished(5000) && listing.exitCode() == 0, "Cannot inspect startup sources through the public CLI");
        QJsonParseError error;
        const auto document = QJsonDocument::fromJson(listing.readAllStandardOutput(), &error);
        require(error.error == QJsonParseError::NoError && document.isObject(), "Source CLI returned malformed JSON");
        const auto rows = document.object().value("sources").toArray();
        for (const auto& row : rows) require(row.toObject().value("sourcePath").toString() == explicitPath,
            "Explicit launch imported an ambient or unrelated kubeconfig");
        return rows;
    };
    const auto awaitSources = [&](int count) {
        QElapsedTimer deadline; deadline.start();
        while (deadline.elapsed() < 5000 && process.state() != QProcess::NotRunning) {
            QCoreApplication::processEvents();
            if (sourceRows().size() == count) return true;
            process.waitForFinished(20);
        }
        return false;
    };
    const bool sourceAccepted = explicitSource && scenario != "source_missing" && scenario != "source_invalid";
    if (sourceAccepted) require(awaitSources(1), "Explicit startup did not import its source asynchronously");
    QElapsedTimer timer; timer.start();
    while (timer.elapsed() < 1500 && process.state() != QProcess::NotRunning) {
        QCoreApplication::processEvents(); process.waitForFinished(20);
    }
    const auto errors = process.readAllStandardError();
    if (process.state() == QProcess::NotRunning) std::fprintf(stderr, "%s", errors.constData());
    require(process.state() != QProcess::NotRunning, "Normal native launch exited before a usable event loop");
    if (scenario == "ambient_kubeconfig") {
        require(calls == 0 && !QFileInfo(profile + "/kubeconfigs").exists(), "Normal launch imported or contacted ambient Kubernetes");
    }
    if (scenario == "ambient_exec") require(!QFileInfo(marker).exists(), "Normal launch executed an ambient authentication provider");
    require(!errors.contains("ambient-startup-secret"), "Startup diagnostics exposed an ambient credential");
    if (explicitSource) {
        require(calls == 0 && !QFileInfo(marker).exists(), "Startup import contacted Kubernetes or executed authentication");
        require(sourceRows().size() == (sourceAccepted ? 1 : 0), "Startup source outcome differs from the requested import");
        QFile retained(explicitPath);
        require(retained.open(QIODevice::ReadOnly) && retained.readAll() == sourceContents, "Startup import modified the external kubeconfig");
        if (scenario == "source_repeat_import" || scenario == "source_changed") {
            process.terminate();
            require(process.waitForFinished(3000), "Cannot close the first startup process");
            if (scenario == "source_changed") write(explicitPath, sourceContents + "# changed configuration\n");
            process.start(binary, arguments);
            require(process.waitForStarted(5000) && awaitSources(scenario == "source_changed" ? 2 : 1),
                "Restart did not preserve immutable import deduplication or changed snapshots");
            QCoreApplication::processEvents();
            require(calls == 0 && !QFileInfo(marker).exists(), "Repeated import unexpectedly contacted Kubernetes or authenticated");
        }
    }
    return true;
}
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    if (argc == 2 && QByteArray(argv[1]) == "tls-runtime") {
        const QJsonObject runtime{{"backend", QSslSocket::activeBackend()}, {"available", QJsonArray::fromStringList(QSslSocket::availableBackends())},
            {"supportsTls", QSslSocket::supportsSsl()}, {"build", QSslSocket::sslLibraryBuildVersionString()}, {"loaded", QSslSocket::sslLibraryVersionString()}};
        std::fputs(QJsonDocument(runtime).toJson(QJsonDocument::Compact).constData(), stdout);
        return QSslSocket::supportsSsl() ? 0 : 1;
    }
    if (argc == 2 && QByteArray(argv[1]) == "profile-path") {
        QCoreApplication::setApplicationName("Podlord Native");
        std::fputs(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation).toUtf8().constData(), stdout);
        return 0;
    }
    if (argc == 3 && QByteArray(argv[1]) == "provider") return write(QString::fromLocal8Bit(argv[2]), "started\n") ? 0 : 1;
    if (argc < 3 || argc > 5) return 2;
    try { return execute(QString::fromLocal8Bit(argv[1]), QString::fromLocal8Bit(argv[2]), argc >= 4 ? QString::fromLocal8Bit(argv[3]) : QString{},
        argc == 5 ? QString::fromLocal8Bit(argv[4]) : QString{}) ? 0 : 1; }
    catch (const std::exception& exception) { std::fprintf(stderr, "%s\n", exception.what()); return 1; }
}
