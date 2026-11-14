#include <algorithm>
#include <QFileOpenEvent>
#include <QFileInfo>
#include <functional>
#include <QSslSocket>
#include "workspace.h"
#include "window_host.h"
#include "graphics_configuration.h"
#include <QCommandLineParser>
#include <QGuiApplication>
#include <QQmlContext>
#include <QQuickStyle>
#include <QStandardPaths>
#include <QStyleHints>
#include <QTimer>
#include <cstdio>

namespace {
class PodlordApplication final : public QGuiApplication {
public:
    using QGuiApplication::QGuiApplication;
    QStringList pendingFiles;
    std::function<void()> filesAvailable;
    bool event(QEvent* event) override {
        if (event->type() == QEvent::FileOpen) {
            const auto* opened = static_cast<QFileOpenEvent*>(event);
            const auto url = opened->url();
            if (url.isLocalFile() || url.scheme() == "content") {
                pendingFiles.append(url.isLocalFile() ? url.toLocalFile() : url.toString());
                if (filesAvailable) filesAvailable();
                return true;
            }
        }
        return QGuiApplication::event(event);
    }
};
}
int main(int argc, char** argv) {
#ifdef Q_OS_ANDROID
    qputenv("ANDROID_OPENSSL_SUFFIX", "_3");
    qputenv("QT_ANDROID_NO_FULLSCREEN_KEYBOARD", "1");
#endif
#ifdef Q_OS_MACOS
    // Bundled QML is AOT-compiled; runtime cache writes delay first launch from a fresh path.
    if (!qEnvironmentVariableIsSet("QML_DISK_CACHE")) qputenv("QML_DISK_CACHE", "aot");
#endif
    if (!podlord::configureGraphics()) { std::fputs("Cannot configure native graphics.\n", stderr); return 1; }
    PodlordApplication app(argc, argv);
    if (!qEnvironmentVariableIsSet("QT_QUICK_CONTROLS_STYLE")) QQuickStyle::setStyle("Fusion");
    QCoreApplication::setApplicationName("Podlord Native");
    QCoreApplication::setApplicationVersion(QString::fromLatin1(PODLORD_APP_VERSION));
    QCommandLineParser parser;
    parser.addHelpOption(); parser.addVersionOption();
    parser.addOption({"profile", "Absolute private application profile; defaults to this app's local data directory.", "path"});
    parser.addOption({"kubeconfig", "Explicitly import one kubeconfig file, file URL or folder after opening the window; does not open a context or authenticate.", "path"});
    parser.addPositionalArgument("files", "Kubeconfig files to open.", "[files...]");
    parser.process(app);
    const auto startupSource = parser.value("kubeconfig");
    if (parser.values("kubeconfig").size() > 1 || (parser.isSet("kubeconfig") && startupSource.trimmed().isEmpty())) {
        std::fputs("Use --kubeconfig once with a non-empty file or folder path. No source was imported.\n", stderr);
        return 2;
    }
    const auto profile = parser.isSet("profile") ? parser.value("profile")
        : QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    const auto failure = podlord::profileFailure(profile);
    const auto files = parser.positionalArguments();
    const bool invalidFile = std::any_of(files.cbegin(), files.cend(), [](const QString& path) {
        const QUrl url(path); return !(url.isLocalFile() ? QFileInfo(url.toLocalFile()).isFile() : QFileInfo(path).isFile());
    });
    if (invalidFile) {
        std::fputs("Select an existing kubeconfig file. No source was imported.\n", stderr);
        return 2;
    }
    if (parser.values("profile").size() > 1 || failure) {
        std::fputs("The application profile must be an absolute, accessible directory. Use --profile to select another private directory; no ambient kubeconfig is loaded.\n", stderr);
        return 2;
    }
    if (!QSslSocket::setActiveBackend("openssl")) {
        std::fputs("The TLS runtime is unavailable. Start the complete Podlord application package.\n", stderr);
        return 1;
    }
    podlord::Workspace workspace(profile);
    app.styleHints()->setColorScheme(workspace.themeVariant() == "dark" ? Qt::ColorScheme::Dark : Qt::ColorScheme::Light);
    app.setPalette(workspace.appearancePalette());
    podlord::WindowHost windows(workspace);
    QObject::connect(&windows, &podlord::WindowHost::windowOpened, &app, [&app](QQuickWindow* window) {
        auto* projection = qobject_cast<podlord::Workspace*>(QQmlEngine::contextForObject(window)->contextProperty("workspace").value<QObject*>());
        if (projection) QObject::connect(projection, &podlord::Workspace::appearanceChanged, &app, [&app, projection] {
            app.styleHints()->setColorScheme(projection->themeVariant() == "dark" ? Qt::ColorScheme::Dark : Qt::ColorScheme::Light);
            app.setPalette(projection->appearancePalette());
        });
    });
    if (!windows.open()) { std::fputs("Cannot create the native application window.\n", stderr); return 1; }
    {
        QTimer::singleShot(0, workspace.releaseUpdates(), &podlord::ReleaseUpdates::startAutomaticChecks);
        QObject::connect(&app, &QGuiApplication::applicationStateChanged, workspace.releaseUpdates(), [&workspace](Qt::ApplicationState state) {
            if (state == Qt::ApplicationActive) workspace.releaseUpdates()->checkIfDue();
        });
    }
    QTimer startupImport;
    startupImport.setSingleShot(true);
    app.pendingFiles.append(files);
    if (parser.isSet("kubeconfig")) app.pendingFiles.append(startupSource);
    app.filesAvailable = [&] { if (!workspace.busy() && !app.pendingFiles.isEmpty()) startupImport.start(0); };
    QObject::connect(&workspace, &podlord::Workspace::changed, &startupImport, [&] { app.filesAvailable(); });
    QObject::connect(&startupImport, &QTimer::timeout, &workspace, [&] {
        if (workspace.busy()) return;
        auto pending = std::move(app.pendingFiles); app.pendingFiles.clear();
        if (!pending.isEmpty()) workspace.importFiles(pending);
    });
    app.filesAvailable();
    const auto result = app.exec();
    app.filesAvailable = {};
    return result;
}
