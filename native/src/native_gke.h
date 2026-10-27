#pragma once
#include "kubeconfig_store.h"
#include "background_http.h"
#include <QPointer>
#include <functional>

namespace podlord {
/** Parses one Google service-account JSON key without retaining unrelated fields. */
Result<GkeServiceAccount> parseGkeServiceAccount(const QByteArray& json);
/** Recognizes the supported GKE exec forms without invoking gcloud or kubectl. */
Result<std::optional<GkeLogin>> nativeGkeConfig(const ExecCommand& command);
/** Builds personal iOS authorization from the registered public Google client ID. */
Result<OidcLogin> nativeGkeBrowserConfig(const QString& clientId);

class NativeOidcLogin;

/** Owns one explicitly confirmed GKE authorization attempt and its transient token. */
class NativeGkeLogin final : public QObject {
    Q_OBJECT
public:
    enum class AuthorizationStatus { Success, Cancelled, Unavailable, Failure };
    struct AuthorizationResult final { AuthorizationStatus status; QByteArray accessToken; QDateTime expiresAt{}; };
    using Authorize = std::function<void(std::function<void(AuthorizationResult)>)>;
    explicit NativeGkeLogin(ClusterConnection connection, QObject* parent = nullptr);
    NativeGkeLogin(ClusterConnection connection, Authorize authorize, std::function<void()> cancel,
        QUrl serviceAccountEndpoint, QObject* parent = nullptr);
    ~NativeGkeLogin() override;
    void start();
    void cancel();
signals:
    void completed(Result<ClusterConnection> result);
private:
    struct Android;
    void acceptAuthorization(AuthorizationResult result);
    void requestServiceAccount();
    void finish(Result<ClusterConnection> result);
    ClusterConnection connection_;
    Authorize authorize_;
    std::function<void()> cancelAuthorization_;
    QUrl serviceAccountEndpoint_;
    PlatformNetworkAccessManager network_;
    QPointer<QNetworkReply> pending_;
    QPointer<NativeOidcLogin> browser_;
    std::unique_ptr<Android> android_;
    bool started_ = false;
    bool done_ = false;
};
} // namespace podlord
