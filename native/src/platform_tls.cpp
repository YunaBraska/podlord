#include "platform_tls.h"
#include <QNetworkReply>

namespace podlord {
QMetaObject::Connection configurePlatformTrust(QNetworkReply& reply, std::shared_ptr<TrustCancellation> stop) {
#ifdef Q_OS_IOS
    return QObject::connect(&reply, &QNetworkReply::sslErrors, &reply,
        [&reply, stop](const QList<QSslError>& errors) {
            const auto tls = reply.sslConfiguration();
            const auto configuredName = reply.request().peerVerifyName();
            const auto name = configuredName.isEmpty() ? reply.url().host() : configuredName;
            if (platformTrustsPeer(tls.peerCertificateChain(), name, tls.caCertificates(), errors, stop)) reply.ignoreSslErrors(errors);
        });
#else
    Q_UNUSED(reply); Q_UNUSED(stop);
    return {};
#endif
}
}
