#pragma once
#include "background_http.h"
#include "kubeconfig_store.h"
#include <QPointer>
#include <QTimer>
#include <functional>

namespace podlord {
/** Owns one confirmed IAM Identity Center device authorization. Secrets never leave this attempt. */
class NativeAwsSso final : public QObject {
    Q_OBJECT
public:
    explicit NativeAwsSso(ClusterConnection connection, QObject* parent = nullptr);
    ~NativeAwsSso() override;
    void start();
    void cancel();
    QString instruction() const;
signals:
    void instructionChanged();
    void completed(Result<ClusterConnection> result);
private:
    void send(const QUrl& url, const QJsonObject& body, bool get, std::function<void(int, const QJsonObject&)> done);
    void poll();
    void finish(Result<ClusterConnection> result);
    ClusterConnection connection_;
    PlatformNetworkAccessManager network_;
    QPointer<QNetworkReply> pending_;
    QTimer timer_;
    QUrl oidc_, portal_;
    QString clientId_, clientSecret_, deviceCode_, accessToken_, instruction_;
    QDateTime deadline_;
    int interval_ = 5;
    bool started_ = false, done_ = false;
};
} // namespace podlord
