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

int main(int argc, char** argv) {
    if (!podlord::configureGraphics()) { std::fputs("Cannot configure native graphics.\n", stderr); return 1; }
    QGuiApplication app(argc, argv);
    if (!qEnvironmentVariableIsSet("QT_QUICK_CONTROLS_STYLE")) QQuickStyle::setStyle("Fusion");
    QCoreApplication::setApplicationName("Podlord Native");
    QCoreApplication::setApplicationVersion(QString::fromLatin1(PODLORD_APP_VERSION));
    QCommandLineParser parser;
    parser.addHelpOption(); parser.addVersionOption();
    parser.addOption({"profile", "Absolute private application profile; defaults to this app's local data directory.", "path"});
    parser.addOption({"kubeconfig", "Explicitly import one kubeconfig file, file URL or folder after opening the window; does not open a session or authenticate.", "path"});
    parser.process(app);
    const auto startupSource = parser.value("kubeconfig");
    if (parser.values("kubeconfig").size() > 1 || (parser.isSet("kubeconfig") && startupSource.trimmed().isEmpty())) {
        std::fputs("Use --kubeconfig once with a non-empty file or folder path. No source was imported.\n", stderr);
        return 2;
    }
    const auto profile = parser.isSet("profile") ? parser.value("profile")
        : QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    const auto failure = podlord::profileFailure(profile);
    if (!parser.positionalArguments().isEmpty() || parser.values("profile").size() > 1 || failure) {
        std::fputs("The application profile must be an absolute, accessible directory. Use --profile to select another private directory; no ambient kubeconfig is loaded.\n", stderr);
        return 2;
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
    if (parser.isSet("kubeconfig")) {
        const auto ready = QObject::connect(&workspace, &podlord::Workspace::changed, &startupImport, [&] {
            if (!workspace.busy()) startupImport.start(0);
        });
        QObject::connect(&startupImport, &QTimer::timeout, &workspace, [&, ready] {
            if (workspace.busy()) return;
            QObject::disconnect(ready);
            workspace.importFile(startupSource);
        });
        startupImport.start(0);
    }
    return app.exec();
}
