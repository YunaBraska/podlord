#pragma once
#include "background_http.h"
#include "kubeconfig_store.h"
#include <QPointer>
#include <QTimer>
#include <functional>

namespace podlord {
struct AwsMfaChallenge final { QString id, role, device; };
/** Owns one confirmed native AWS sign-in, including SSO, role chains and ephemeral MFA. */
class NativeAwsLogin final : public QObject {
    Q_OBJECT
public:
    explicit NativeAwsLogin(ClusterConnection connection, QObject* parent = nullptr);
    ~NativeAwsLogin() override;
    void start();
    void cancel();
    QString instruction() const;
    std::optional<AwsMfaChallenge> challenge() const;
    bool submitMfa(const QString& challengeId, const QString& code);
signals:
    void instructionChanged();
    void completed(Result<ClusterConnection> result);
private:
    void sendJson(const QUrl& url, const QJsonObject& body, bool get, std::function<void(int, const QJsonObject&)> done);
    void send(QNetworkRequest request, const QByteArray& body, bool get, std::function<void(int, const QByteArray&)> done);
    void assumeRole();
    bool requestRole(const QString& code);
    void poll();
    void finish(Result<ClusterConnection> result);
    ClusterConnection connection_;
    PlatformNetworkAccessManager network_;
    QPointer<QNetworkReply> pending_;
    QTimer timer_;
    QUrl oidc_, portal_;
    QString clientId_, clientSecret_, deviceCode_, accessToken_, instruction_;
    QString mfaChallenge_;
    QDateTime deadline_;
    int interval_ = 5;
    bool started_ = false, done_ = false;
};
} // namespace podlord
