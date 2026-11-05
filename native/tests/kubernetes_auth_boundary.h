#pragma once
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QUrl>
#include <memory>

namespace podlord::test {
class Kubernetes final : public QTcpServer {
public:
    int calls = 0;
    bool reject = false;
    QByteArray authorization;
    Kubernetes() {
        connect(this, &QTcpServer::newConnection, this, [this] {
            while (hasPendingConnections()) {
                auto* socket = nextPendingConnection();
                auto bytes = std::make_shared<QByteArray>();
                connect(socket, &QTcpSocket::readyRead, this, [this, socket, bytes] {
                    bytes->append(socket->readAll());
                    if (!bytes->contains("\r\n\r\n")) return;
                    socket->disconnect(this); ++calls;
                    for (const auto& line : bytes->split('\n')) if (line.startsWith("Authorization:")) authorization = line.mid(14).trimmed();
                    const auto path = QUrl::fromEncoded(bytes->split(' ')[1]).path();
                    QJsonObject document;
                    if (path == "/api") document = {{"versions", QJsonArray{"v1"}}};
                    else if (path == "/apis") document = {{"groups", QJsonArray{}}};
                    else if (path == "/api/v1") document = {{"resources", QJsonArray{QJsonObject{{"name", "pods"}, {"kind", "Pod"}, {"namespaced", true}, {"verbs", QJsonArray{"get", "list"}}}}}};
                    else {
                        const QJsonObject pod{{"kind", "Pod"}, {"metadata", QJsonObject{{"name", "real-pod"}, {"namespace", "default"}, {"uid", "pod-id"}}}};
                        document = path == "/api/v1/pods" ? QJsonObject{{"items", QJsonArray{pod}}, {"metadata", QJsonObject{}}} : pod;
                    }
                    const auto body = QJsonDocument(document).toJson(QJsonDocument::Compact);
                    socket->write("HTTP/1.1 " + QByteArray(reject ? "401" : "200") + " Result\r\nConnection: close\r\nContent-Type: application/json\r\nContent-Length: " + QByteArray::number(body.size()) + "\r\n\r\n" + body);
                    socket->disconnectFromHost();
                });
                connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            }
        });
    }
};
} // namespace podlord::test
