#pragma once
#include "kubeconfig_store.h"
#include "background_http.h"
#include <QPointer>
#include <QTimer>
#include <functional>

namespace podlord {
/** Recognizes the supported kubelogin OIDC arguments without executing the imported command. */
Result<std::optional<OidcLogin>> nativeOidc(const ExecCommand& command);
/** Interprets an AKS kubelogin configuration without invoking Azure tooling. */
Result<std::optional<OidcLogin>> nativeAks(const ExecCommand& command);

/** One explicitly confirmed, in-memory OIDC authorization attempt. */
class NativeOidcLogin final : public QObject {
    Q_OBJECT
public:
    enum class Flow { Device, Browser };
    explicit NativeOidcLogin(ClusterConnection connection, Flow flow = Flow::Device, QObject* parent = nullptr);
    void start();
    void cancel();
    QString instruction() const;
signals:
    void instructionChanged();
    void completed(Result<ClusterConnection> result);
private:
    void send(const QUrl& url, const QByteArray& form, std::function<void(int, const QJsonObject&)> done);
    void requestDevice(const QUrl& endpoint);
    void requestBrowser(const QUrl& endpoint);
    void handleRedirect(const QUrl& url);
    void acceptToken(int status, const QJsonObject& object);
    void poll();
    void finish(Result<ClusterConnection> result);
    ClusterConnection connection_;
    Flow flow_;
    PlatformNetworkAccessManager network_;
    QPointer<QNetworkReply> pending_;
    QTimer timer_;
    QUrl tokenEndpoint_;
    QUrl jwksEndpoint_;
    QString deviceCode_, instruction_, state_, nonce_, codeVerifier_;
    QDateTime deadline_;
    int interval_ = 5;
    bool done_ = false;
};
} // namespace podlord
