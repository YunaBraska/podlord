#pragma once
#include <QNetworkAccessManager>
#include <QNetworkProxy>
#include <memory>

namespace podlord {
/** The HTTP operations used by Podlord, with transport and trust evaluation on one owned worker. */
class BackgroundHttpClient final : public QObject {
public:
    explicit BackgroundHttpClient(QObject* parent = nullptr);
    ~BackgroundHttpClient() override;
    QNetworkReply* get(const QNetworkRequest& request);
    QNetworkReply* post(const QNetworkRequest& request, const QByteArray& body);
    QNetworkReply* sendCustomRequest(const QNetworkRequest& request, const QByteArray& method, const QByteArray& body);
    BackgroundHttpClient& setProxy(const QNetworkProxy& proxy);
    BackgroundHttpClient& clearConnectionCache();
private:
    struct State;
    std::unique_ptr<State> state_;
    QNetworkReply* send(QNetworkAccessManager::Operation operation, const QNetworkRequest& request,
        const QByteArray& method, const QByteArray& body);
};

#ifdef Q_OS_IOS
using PlatformNetworkAccessManager = BackgroundHttpClient;
#else
using PlatformNetworkAccessManager = QNetworkAccessManager;
#endif
}
