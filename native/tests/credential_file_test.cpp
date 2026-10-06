#include "kubeconfig_store.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QTemporaryDir>
#include <cstdio>
#include <stdexcept>
#ifdef Q_OS_UNIX
#include <sys/stat.h>
#endif

namespace {
bool require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
    return true;
}
bool write(const QString& path, const QByteArray& bytes) {
    QFile file(path);
    return require(file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size(), "Cannot write test input.");
}
}

int main(int argc, char** argv) {
    QCoreApplication application(argc, argv);
    try {
        const auto args = application.arguments();
        if (args.size() == 6 && args[1] == "--child") {
            const podlord::KubeconfigStore store(args[2]);
            const auto imported = store.importFile(args[3]);
            require(std::holds_alternative<podlord::SourceSnapshot>(imported), "Import failed before credential resolution.");
            const auto& source = std::get<podlord::SourceSnapshot>(imported);
            require(source.contexts.size() == 1, "Expected one context.");
            const auto result = store.connection(source.contexts.first().id);
            if (args[4] == "success") {
                require(std::holds_alternative<podlord::ClusterConnection>(result), "Regular credential file was rejected.");
                QFile expectedToken(args[5]);
                require(expectedToken.open(QIODevice::ReadOnly), "Cannot read expected token.");
                require(std::get<podlord::ClusterConnection>(result).authorization == "Bearer " + expectedToken.readAll(), "Token was changed.");
            } else {
                const auto* failure = std::get_if<podlord::Failure>(&result);
                require(failure != nullptr, "Invalid credential file was accepted.");
                require(podlord::errorName(failure->code) == args[4], "Incorrect credential error category.");
                if (!args[5].isEmpty()) require(failure->message.contains(args[5]), "Credential file limit was not enforced.");
                require(!failure->message.contains("private-test-token"), "Credential error exposed token content.");
            }
            return 0;
        }
        require(args.size() == 3, "Expected field and scenario.");
        const auto field = args[1];
        const auto scenario = args[2];
        require(QStringList{"tokenFile", "certificate-authority", "client-certificate", "client-key"}.contains(field), "Unknown field.");
        require(QStringList{"missing", "directory", "empty", "oversized", "fifo", "broken_link", "relative", "absolute", "symlink", "limit", "invalid_utf8", "whitespace"}.contains(scenario), "Unknown scenario.");
        QTemporaryDir directory;
        require(directory.isValid(), "Cannot create private test directory.");
        const auto materialPath = directory.filePath("credential");
        QString path = "credential";
        QString expected = "ReadFailed";
        QString detail;
        if (scenario == "directory") require(QDir().mkdir(materialPath), "Cannot create directory input.");
        else if (scenario == "fifo") {
#ifdef Q_OS_UNIX
            require(::mkfifo(QFile::encodeName(materialPath).constData(), 0600) == 0, "Cannot create FIFO input.");
#else
            require(false, "FIFO test is unsupported on this platform.");
#endif
        } else if (scenario == "broken_link") {
            require(QFile::link(directory.filePath("missing-target"), materialPath), "Cannot create broken symlink.");
        } else if (scenario != "missing") {
            if (scenario == "oversized") {
                QFile file(materialPath);
                require(file.open(QIODevice::WriteOnly) && file.resize(16 * 1024 * 1024 + 1), "Cannot create oversized input.");
                expected = "InvalidData";
                detail = "16 MiB";
            } else if (scenario == "empty") write(materialPath, {});
            else {
                require(field == "tokenFile", "Successful decoding scenarios apply to token files.");
                QByteArray token = "private-test-token";
                if (scenario == "limit") token = QByteArray(16 * 1024 * 1024, 't');
                if (scenario == "invalid_utf8") token = QByteArray(1, static_cast<char>(0xff));
                if (scenario == "whitespace") token = " \n\t";
                write(materialPath, token);
                expected = scenario == "invalid_utf8" || scenario == "whitespace" ? "InvalidData" : "success";
                // The child reads the expected token from a file, never a command line.
                if (expected == "success") detail = QString::fromUtf8(token);
                if (scenario == "absolute") path = materialPath;
                if (scenario == "symlink") {
                    require(QFile::link(materialPath, directory.filePath("linked")), "Cannot create credential symlink.");
                    path = "linked";
                }
            }
        }
        QByteArray yaml = "apiVersion: v1\nkind: Config\nclusters:\n- name: local\n  cluster:\n    server: https://127.0.0.1:9\n";
        if (field == "certificate-authority") yaml += "    certificate-authority: " + path.toUtf8() + "\n";
        yaml += "users:\n- name: local\n  user:\n";
        if (field != "certificate-authority") yaml += "    " + field.toUtf8() + ": " + path.toUtf8() + "\n";
        else yaml += "    token: private-test-token\n";
        yaml += "contexts:\n- name: local\n  context:\n    cluster: local\n    user: local\n";
        const auto configPath = directory.filePath("config.yaml");
        write(configPath, yaml);
        const auto expectedPath = directory.filePath("expected-token");
        if (expected == "success") write(expectedPath, detail.toUtf8());
        QProcess child;
        child.start(application.applicationFilePath(), {"--child", directory.filePath("profile"), configPath, expected, expected == "success" ? expectedPath : detail});
        require(child.waitForStarted(3000), "Credential test child did not start.");
        if (!child.waitForFinished(5000)) {
            child.kill();
            require(child.waitForFinished(3000), "Credential test child did not stop.");
            require(false, "Credential resolution blocked on a special file.");
        }
        if (child.exitCode() != 0) std::fputs(child.readAllStandardError().constData(), stderr);
        require(child.exitStatus() == QProcess::NormalExit && child.exitCode() == 0, "Credential boundary assertion failed.");
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}
