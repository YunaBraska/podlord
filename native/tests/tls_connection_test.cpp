#include <algorithm>
#include "workspace.h"
#include <QGuiApplication>
#include <QSslSocket>
#include <QDir>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QEventLoop>
#include <QTimer>
#include <functional>
#include <cstdio>
#include <stdexcept>
#ifdef Q_OS_WIN
#include <QtCore/qt_windows.h>
#endif

namespace {
bool require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
    return true;
}
bool waitFor(const std::function<bool()>& ready, int timeout) {
    if (ready()) return true;
    QEventLoop loop;
    QTimer poll;
    QObject::connect(&poll, &QTimer::timeout, &loop, [&] { if (ready()) loop.quit(); });
    QTimer::singleShot(timeout, &loop, &QEventLoop::quit);
    poll.start(10);
    loop.exec();
    return ready();
}
}
int main(int argc, char** argv) {
    QGuiApplication application(argc, argv);
    try {
        require(argc == 3, "Expected private kubeconfig and TLS scenario.");
        const QString scenario = QString::fromLocal8Bit(argv[2]);
        require(QStringList{"trusted", "untrusted_ca", "untrusted_client", "untrusted_host", "system_trusted", "system_untrusted"}.contains(scenario), "Unknown TLS scenario.");
        QTemporaryDir directory;
        require(directory.isValid(), "Cannot create private TLS profile.");
        podlord::Workspace workspace(directory.filePath("profile"));
        require(waitFor([&] { return !workspace.busy(); }, 5000), "Initial profile did not settle.");
        require(workspace.importFile(QString::fromLocal8Bit(argv[1])), "Cannot start actual kubeconfig import.");
        require(waitFor([&] { return !workspace.busy() && workspace.contexts().size() == 1; }, 7000), "TLS input was not imported.");
        const auto context = workspace.contexts().first().toMap();
        const auto connection = podlord::KubeconfigStore(directory.filePath("profile")).connection(context["id"].toString());
        require(std::holds_alternative<podlord::ClusterConnection>(connection), "Cannot resolve imported TLS configuration.");
        const auto& server = std::get<podlord::ClusterConnection>(connection).server;
        require(server.scheme() == "https" && server.host() == "127.0.0.1", "TLS tests must use their local external boundary.");
        require(workspace.openContext(context["id"].toString()), "Cannot open actual TLS context.");
        require(waitFor([&] {
            return !workspace.currentSession().isEmpty() && !workspace.busy() && !workspace.loading();
        }, 15000), "TLS discovery did not complete.");
        if (scenario == "trusted" || scenario == "system_trusted") require(workspace.resourceCount() == 1, "Trusted mutual TLS did not populate the real session cache.");
        else {
            require(workspace.resourceCount() == 0 && !workspace.syncProblem().isEmpty(), "Untrusted TLS populated the cache or did not report failure.");
            const auto audit = workspace.settingsDiagnostics().value("requests").toList();
            require(std::any_of(audit.cbegin(), audit.cend(), [](const QVariant& row) { return row.toMap().value("outcome").toString().contains("Qt network error"); }), "TLS diagnostics discarded the network failure details.");
        }
#if defined(Q_OS_MACOS) || defined(Q_OS_WIN)
        require(QSslSocket::activeBackend() == "openssl", "The app did not use its required OpenSSL runtime.");
#endif
#ifdef Q_OS_WIN
        const QDir runtime(QCoreApplication::applicationDirPath());
        QStringList runtimeFiles;
        for (const auto& pattern : {"libcrypto-3*.dll", "libssl-3*.dll"}) {
            const auto files = runtime.entryList({QString::fromLatin1(pattern)}, QDir::Files);
            require(files.size() == 1, "Expected one packaged OpenSSL library.");
            runtimeFiles << runtime.filePath(files.first());
        }
        runtimeFiles << runtime.filePath("tls/qopensslbackend.dll");
        for (const auto& file : runtimeFiles) {
            const QFileInfo expected(file);
            const auto name = expected.fileName();
            const auto module = GetModuleHandleW(reinterpret_cast<LPCWSTR>(name.utf16()));
            require(module != nullptr, "Required packaged TLS module was not loaded.");
            wchar_t loaded[32768];
            const auto length = GetModuleFileNameW(module, loaded, 32768);
            require(length > 0 && length < 32768, "Cannot inspect loaded TLS module.");
            const auto actual = QFileInfo(QString::fromWCharArray(loaded, static_cast<int>(length))).canonicalFilePath();
            require(!actual.isEmpty() && actual.compare(expected.canonicalFilePath(), Qt::CaseInsensitive) == 0,
                "TLS module resolved outside the extracted runtime.");
        }
#endif
        std::printf("TLS scenario passed: %s; backend: %s\n", argv[2], qPrintable(QSslSocket::activeBackend()));
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}
