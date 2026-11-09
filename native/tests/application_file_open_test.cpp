#include "workspace.h"
#include <QCoreApplication>
#include <QFileOpenEvent>
#include <QGuiApplication>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickWindow>
#include <QTimer>
#include <QUrl>
#include <cstdio>

namespace {
void exerciseFileOpen() {
    if (qEnvironmentVariableIsSet("PODLORD_FILE_OPEN_TEST_NO_TLS")) {
        QCoreApplication::setLibraryPaths({qEnvironmentVariable("PODLORD_FILE_OPEN_TEST_PLUGIN_ROOT")});
        return;
    }
    QTimer::singleShot(0,QCoreApplication::instance(),[] {
        const auto document=qEnvironmentVariable("PODLORD_FILE_OPEN_TEST_DOCUMENT");
        podlord::Workspace* workspace=nullptr;
        for (auto* window:QGuiApplication::topLevelWindows()) {
            if (auto* quick=qobject_cast<QQuickWindow*>(window)) {
                if (auto* context=QQmlEngine::contextForObject(quick))
                    workspace=qobject_cast<podlord::Workspace*>(context->contextProperty("workspace").value<QObject*>());
            }
            if (workspace) break;
        }
        if (document.isEmpty() || !workspace) {
            std::fputs("File-open test workspace is unavailable.\n",stderr);
            QCoreApplication::exit(2); return;
        }
        QObject::connect(workspace,&podlord::Workspace::changed,QCoreApplication::instance(),[workspace] {
            if (!workspace->busy() && workspace->contexts().size()==1)
                QCoreApplication::exit(workspace->currentSession().isEmpty() ? 0 : 3);
        });
        QTimer::singleShot(12000,QCoreApplication::instance(),[] { QCoreApplication::exit(4); });
        // Drive the same public Qt ingress used by native file/document events.
        // Only the host's external document-provider URL is unavailable here.
        const QList<QUrl> documents{QUrl::fromLocalFile(document),QUrl::fromLocalFile(document),
            QUrl("content://podlord.invalid/missing"),QUrl("https://podlord.invalid/not-a-document"),QUrl{}};
        for (const auto& url:documents) {
            QFileOpenEvent opened(url);
            QCoreApplication::sendEvent(QCoreApplication::instance(),&opened);
        }
    });
}
Q_COREAPP_STARTUP_FUNCTION(exerciseFileOpen)
}
