#pragma once
#include <QMetaObject>
#include <QSslCertificate>
#include <QSslError>
#ifndef Q_OS_ANDROID
#include <condition_variable>
#include <memory>
#include <mutex>
#endif

class QNetworkReply;
class QWebSocket;

namespace podlord {
#ifndef Q_OS_ANDROID
struct TrustCancellation final {
    std::mutex mutex;
    std::condition_variable ready;
    bool canceled = false;
    void cancel() {
        { std::lock_guard lock(mutex); canceled = true; }
        ready.notify_all();
    }
    bool isCanceled() {
        std::lock_guard lock(mutex);
        return canceled;
    }
};
#endif
#ifdef Q_OS_DARWIN
/** Wait for Apple's SSL policy outside the UI thread. Cancellation never approves a peer. */
bool platformTrustsPeer(const QList<QSslCertificate>& chain, const QString& peerName,
    const QList<QSslCertificate>& authorities, const QList<QSslError>& errors,
    std::shared_ptr<TrustCancellation> stop = std::make_shared<TrustCancellation>());
#endif
/** iOS OpenSSL lacks a system CA loader. Other platforms retain Qt's existing verification. */
#ifndef Q_OS_ANDROID
QMetaObject::Connection configurePlatformTrust(QNetworkReply& reply, std::shared_ptr<TrustCancellation> stop);
#endif
QMetaObject::Connection configurePlatformTrust(QWebSocket& socket);
}
