#include "platform_tls.h"
#include <QWebSocket>
#include <QFutureWatcher>
#include <QtConcurrent/QtConcurrentRun>
#include <memory>

namespace podlord {
QMetaObject::Connection configurePlatformTrust(QWebSocket& socket) {
#ifdef Q_OS_IOS
    socket.setPauseMode(QAbstractSocket::PauseOnSslErrors);
    return QObject::connect(&socket, &QWebSocket::sslErrors, &socket, [&socket](const QList<QSslError>& errors) {
        const auto tls = socket.sslConfiguration();
        const auto configuredName = socket.request().peerVerifyName();
        const auto name = configuredName.isEmpty() ? socket.requestUrl().host() : configuredName;
        auto* watcher = new QFutureWatcher<bool>(&socket);
        const auto stop = std::make_shared<TrustCancellation>();
        QObject::connect(&socket, &QWebSocket::stateChanged, watcher, [stop](QAbstractSocket::SocketState state) {
            if (state == QAbstractSocket::UnconnectedState || state == QAbstractSocket::ClosingState) stop->cancel();
        });
        QObject::connect(&socket, &QObject::destroyed, watcher, [stop] { stop->cancel(); });
        QObject::connect(watcher, &QFutureWatcher<bool>::finished, &socket, [&socket, watcher, stop, errors] {
            if (!stop->isCanceled() && (socket.state() == QAbstractSocket::ConnectingState || socket.state() == QAbstractSocket::ConnectedState)) {
                if (watcher->result()) socket.ignoreSslErrors(errors);
                socket.resume();
            }
            watcher->deleteLater();
        });
        watcher->setFuture(QtConcurrent::run([tls, name, errors, token = stop] {
            return platformTrustsPeer(tls.peerCertificateChain(), name, tls.caCertificates(), errors, token);
        }));
    });
#else
    Q_UNUSED(socket);
    return {};
#endif
}
}
