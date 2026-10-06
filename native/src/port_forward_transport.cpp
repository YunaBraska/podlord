#include "port_forward_transport.h"
#include <QNetworkProxy>
#include <QTcpSocket>
#include <QWebSocket>
#include <QWebSocketHandshakeOptions>
#include <QUrlQuery>
#include <array>

namespace podlord {
namespace { constexpr qint64 bufferLimit = 1024 * 1024, chunkSize = 32768; }
struct PortForwardTransport::Stream final : QObject {
    QTcpSocket* local;
    QWebSocket remote;
    std::array<bool, 2> headers{false, false};
    int port = 0;
    bool requested = false, admitted = false, reported = false;
    explicit Stream(QTcpSocket* socket, QObject* parent) : QObject(parent), local(socket) { local->setParent(this); }
};
PortForwardTransport::PortForwardTransport(QObject* parent) : QObject(parent) {
    connect(&listener_, &QTcpServer::newConnection, this, [this] {
        while (auto* socket = listener_.nextPendingConnection()) {
            if (stopped_ || streams_.size() >= 64) {
                socket->abort(); socket->deleteLater(); emit streamFailed("This forward already has 64 active or queued connections. Close a connection before opening another."); continue;
            }
            const auto token = QUuid::createUuid().toString(QUuid::WithoutBraces);
            auto* stream = new Stream(socket, this);
            stream->local->setReadBufferSize(65536);
            streams_.insert(token, stream);
            connect(socket, &QTcpSocket::disconnected, this, [this, token] { removeStream(token); });
            connect(socket, &QTcpSocket::readyRead, this, [this, token] { drain(token); });
            connect(socket, &QTcpSocket::errorOccurred, this, [this, token](QAbstractSocket::SocketError) { removeStream(token); });
            if (ready_) { stream->requested = true; emit connectionRequested(token); }
        }
    });
}
PortForwardTransport::~PortForwardTransport() { stop(); }
Result<int> PortForwardTransport::listen(int localPort) {
    if (localPort < 1 || localPort > 65535 || stopped_ || listener_.isListening())
        return Failure{StoreError::InvalidInput, "Choose a local TCP port from 1 through 65535."};
    if (!listener_.listen(QHostAddress::LocalHost, static_cast<quint16>(localPort)))
        return Failure{StoreError::Busy, "The local loopback port is occupied or unavailable. Choose another port."};
    return localPort;
}
bool PortForwardTransport::targetReady() {
    if (stopped_ || !listener_.isListening()) return false;
    ready_ = true;
    for (const auto& token : streams_.keys()) {
        auto* stream = streams_.value(token);
        if (stream && !stream->requested) { stream->requested = true; emit connectionRequested(token); }
    }
    return true;
}
bool PortForwardTransport::connectStream(const QString& token, QNetworkRequest request, int remotePort) {
    auto* stream = streams_.value(token);
    if (stopped_ || !ready_ || !stream || stream->admitted || remotePort < 1 || remotePort > 65535) return false;
    stream->admitted = true; stream->port = remotePort;
    auto url = request.url();
    url.setScheme(url.scheme() == "https" ? "wss" : "ws");
    QUrlQuery query; query.addQueryItem("ports", QString::number(remotePort)); url.setQuery(query); request.setUrl(url);
    request.setRawHeader("Accept", "*/*");
    stream->remote.setProxy(QNetworkProxy::NoProxy);
    auto tls = request.sslConfiguration();
    tls.setAllowedNextProtocols({QSslConfiguration::NextProtocolHttp1_1});
    stream->remote.setSslConfiguration(tls);
    // Qt waits for an entire frame; a smaller socket buffer can deadlock reception.
    stream->remote.setReadBufferSize(bufferLimit + 64);
    stream->remote.setMaxAllowedIncomingFrameSize(bufferLimit + 1);
    stream->remote.setMaxAllowedIncomingMessageSize(bufferLimit + 1);
    stream->remote.setOutgoingFrameSize(chunkSize + 1);
    connect(&stream->remote, &QWebSocket::connected, this, [this, token] {
        auto* current = streams_.value(token); if (!current) return;
        if (current->remote.subprotocol() != "v4.channel.k8s.io") { failStream(token, "The API did not negotiate the required Kubernetes port-forward protocol."); return; }
        current->reported = true; emit handshakeFinished(token, true, false);
    });
    connect(&stream->remote, &QWebSocket::authenticationRequired, this, [this, token](QAuthenticator*) {
        failStream(token, "Port-forward authentication failed. Confirm authentication manually; no login was started.", true);
    });
    connect(&stream->remote, &QWebSocket::errorOccurred, this, [this, token](QAbstractSocket::SocketError) {
        failStream(token, "Port-forward connection, TLS or API upgrade failed. Verify access and retry explicitly.");
    });
    connect(&stream->remote, &QWebSocket::textMessageReceived, this, [this, token](const QString&) {
        failStream(token, "The API sent non-binary Kubernetes stream data.");
    });
    connect(&stream->remote, &QWebSocket::binaryMessageReceived, this, [this, token](const QByteArray& message) {
        auto* current = streams_.value(token); if (!current) return;
        if (message.isEmpty() || static_cast<unsigned char>(message[0]) > 1) { failStream(token, "Invalid Kubernetes port-forward channel."); return; }
        const auto channel = static_cast<unsigned char>(message[0]);
        auto payload = message.sliced(1);
        if (!current->headers[channel]) {
            if (payload.size() < 2 || (static_cast<unsigned char>(payload[0]) | (static_cast<unsigned char>(payload[1]) << 8)) != current->port) {
                failStream(token, "Invalid Kubernetes port-forward port header."); return;
            }
            current->headers[channel] = true; payload.remove(0, 2);
        }
        if (channel == 1 && !payload.isEmpty()) { failStream(token, "The remote port could not be forwarded. Verify the target port and permissions."); return; }
        if (channel == 0 && !payload.isEmpty()) {
            if (current->local->bytesToWrite() + payload.size() > bufferLimit || current->local->write(payload) != payload.size()) {
                failStream(token, "The local connection exceeded the bounded port-forward buffer or could not accept data."); return;
            }
        }
        drain(token);
    });
    connect(&stream->remote, &QWebSocket::bytesWritten, this, [this, token](qint64) { drain(token); });
    connect(&stream->remote, &QWebSocket::aboutToClose, this, [this, token] {
        auto* current = streams_.value(token); if (!current) return;
        if (current->remote.closeCode() != QWebSocketProtocol::CloseCodeNormal && current->remote.closeCode() != QWebSocketProtocol::CloseCodeGoingAway) {
            failStream(token, "The API closed the port-forward stream with a WebSocket protocol or size-limit error."); return;
        }
    });
    connect(&stream->remote, &QWebSocket::disconnected, this, [this, token] {
        auto* current = streams_.value(token); if (!current) return;
        if (!current->reported) { failStream(token, "The API closed before the port-forward handshake completed."); return; }
        // Flush already received TCP output before removing the stream.
        current->local->disconnectFromHost();
    });
    QWebSocketHandshakeOptions options; options.setSubprotocols({"v4.channel.k8s.io"});
    stream->remote.open(request, options);
    return true;
}
bool PortForwardTransport::drain(const QString& token) {
    auto* stream = streams_.value(token);
    if (!stream || !stream->headers[0] || !stream->headers[1] || stream->remote.state() != QAbstractSocket::ConnectedState) return false;
    while (stream->local->bytesAvailable() > 0 && stream->remote.bytesToWrite() < 65536) {
        const auto payload = stream->local->read(chunkSize);
        if (stream->remote.sendBinaryMessage(QByteArray(1, char(0)) + payload) != payload.size() + 1)
            return failStream(token, "The Kubernetes stream could not accept local data.");
    }
    return true;
}
bool PortForwardTransport::removeStream(const QString& token) {
    auto* stream = streams_.take(token); if (!stream) return false;
    disconnect(&stream->remote, nullptr, this, nullptr); disconnect(stream->local, nullptr, this, nullptr);
    stream->remote.abort(); stream->local->abort();
    emit connectionClosed(token);
    if (stream->admitted && !stream->reported) { stream->reported = true; emit handshakeFinished(token, false, false); }
    stream->deleteLater(); return true;
}
bool PortForwardTransport::failStream(const QString& token, const QString& message, bool authenticationRequired) {
    auto* stream = streams_.value(token); if (!stream) return false;
    if (stream->admitted && !stream->reported) { stream->reported = true; emit handshakeFinished(token, false, authenticationRequired); }
    emit streamFailed(message); return removeStream(token);
}
bool PortForwardTransport::rejectStream(const QString& token) { return removeStream(token); }
bool PortForwardTransport::stop() {
    stopped_ = true; listener_.close();
    for (const auto& token : streams_.keys()) removeStream(token);
    return true;
}
}
