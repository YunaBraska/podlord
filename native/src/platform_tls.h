#pragma once
#include <QMetaObject>
#include <QSslCertificate>
#include <QSslError>
#ifndef Q_OS_ANDROID
#include <stop_token>
#endif

class QNetworkReply;
class QWebSocket;

namespace podlord {
#ifdef Q_OS_DARWIN
/** Wait for Apple's SSL policy outside the UI thread. Cancellation never approves a peer. */
bool platformTrustsPeer(const QList<QSslCertificate>& chain, const QString& peerName,
    const QList<QSslCertificate>& authorities, const QList<QSslError>& errors, std::stop_token stop = {});
#endif
/** iOS OpenSSL lacks a system CA loader. Other platforms retain Qt's existing verification. */
#ifndef Q_OS_ANDROID
QMetaObject::Connection configurePlatformTrust(QNetworkReply& reply, std::stop_token stop);
#endif
QMetaObject::Connection configurePlatformTrust(QWebSocket& socket);
}
