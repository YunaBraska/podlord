#pragma once
#include "session_store.h"
#include <QNetworkRequest>
#include <QTcpServer>

namespace podlord {
/** Session-owned loopback TCP transport. Kubernetes request admission remains external. */
class PortForwardTransport final : public QObject {
    Q_OBJECT
public:
    explicit PortForwardTransport(QObject* parent = nullptr);
    ~PortForwardTransport() override;
    Result<int> listen(int localPort);
    bool targetReady();
    bool connectStream(const QString& token, QNetworkRequest request, int remotePort);
    bool rejectStream(const QString& token);
    bool stop();
signals:
    void connectionRequested(const QString& token);
    void connectionClosed(const QString& token);
    void handshakeFinished(const QString& token, bool accepted, bool authenticationRequired);
    void streamFailed(const QString& message);
private:
    struct Stream;
    QTcpServer listener_;
    QMap<QString, Stream*> streams_;
    bool ready_ = false, stopped_ = false;
    bool removeStream(const QString& token);
    bool failStream(const QString& token, const QString& message, bool authenticationRequired = false);
    bool drain(const QString& token);
};
}
