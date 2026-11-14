#include "background_http.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QNetworkReply>
#include <QNetworkProxyFactory>
#include <QPointer>
#include <QScopeGuard>
#include <QSemaphore>
#include <QTest>
#include <QUrlQuery>
#include <cstdio>
#include <functional>
#include <utility>

namespace {
QByteArray payload(int size) { QByteArray result(size, Qt::Uninitialized); for (int i = 0; i < size; ++i) result[i] = char(i % 251); return result; }
bool waitFor(const std::function<bool()>& predicate) {
    return QTest::qWaitFor(predicate, 10000);
}
}
int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    if (argc != 3) return 2;
    const QUrl base(QString::fromLocal8Bit(argv[1]));
    const QString scenario = QString::fromLocal8Bit(argv[2]);
    auto client = std::make_unique<podlord::BackgroundHttpClient>();
    client->setProxy(QNetworkProxy::NoProxy);
    const auto requestFor = [&](const QString& route) { return QNetworkRequest(base.resolved(QUrl(route))); };
    if (scenario == "cancel_queued") {
        QSemaphore entered, proceed;
        class ProxyGate final : public QNetworkProxyFactory {
        public:
            ProxyGate(QSemaphore& entered, QSemaphore& proceed, QNetworkProxy proxy)
                : entered_(entered), proceed_(proceed), proxy_(std::move(proxy)) {}
            QList<QNetworkProxy> queryProxy(const QNetworkProxyQuery&) override {
                entered_.release();
                proceed_.tryAcquire(1, 10000);
                return {proxy_};
            }
        private:
            QSemaphore &entered_, &proceed_;
            const QNetworkProxy proxy_;
        };
        QNetworkProxyFactory::setApplicationProxyFactory(new ProxyGate(entered, proceed,
            QNetworkProxy(QNetworkProxy::HttpProxy, base.host(), quint16(base.port()))));
        const auto cleanup = qScopeGuard([&] {
            proceed.release();
            client.reset();
            QNetworkProxyFactory::setApplicationProxyFactory(nullptr);
        });
        client->setProxy(QNetworkProxy::DefaultProxy);
        // Qt bypasses proxy lookup for loopback targets; the proxy itself stays local.
        auto* first = client->get(QNetworkRequest(QUrl("http://podlord.invalid/body")));
        if (!waitFor([&] { return entered.tryAcquire(); })) return 1;
        auto* queued = client->get(QNetworkRequest(QUrl("http://podlord.invalid/canceled")));
        int finished = 0;
        QObject::connect(queued, &QNetworkReply::finished, &app, [&] { ++finished; });
        // The first proxy lookup holds dispatch while the second request is queued.
        QCoreApplication::processEvents();
        queued->abort();
        proceed.release();
        if (!waitFor([&] { return first->isFinished() && finished == 1; })) return 1;
        QTest::qWait(30);
        return first->error() == QNetworkReply::NoError && first->readAll() == payload(262175)
            && queued->error() == QNetworkReply::OperationCanceledError && finished == 1 ? 0 : 1;
    }
    if (scenario == "parallel") {
        QList<QNetworkReply*> replies;
        for (int i = 0; i < 8; ++i) replies.append(client->get(requestFor("/body")));
        if (!waitFor([&] { for (const auto* reply : replies) if (!reply->isFinished()) return false; return true; })) return 1;
        for (auto* reply : replies) if (reply->error() != QNetworkReply::NoError || reply->readAll() != payload(262175)) return 1;
        return 0;
    }
    if (scenario == "repeat") {
        for (int i = 0; i < 3; ++i) {
            auto* reply = client->get(requestFor("/body"));
            if (!waitFor([&] { return reply->isFinished(); }) || reply->error() != QNetworkReply::NoError || reply->readAll() != payload(262175)) return 1;
            reply->deleteLater(); client->clearConnectionCache();
        }
        return 0;
    }
    const bool streaming = scenario == "bounded" || scenario == "paused_reader" || scenario == "cancel_unread" || scenario == "cancel_active" || scenario == "destroy";
    auto request = requestFor(streaming ? "/chunks" : scenario == "not_found" ? "/missing"
        : scenario == "disconnect" ? "/disconnect" : scenario == "redirect" ? "/redirect" : scenario == "cancel_metadata" ? "/empty" : "/body");
    if (scenario == "redirect") request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    request.setRawHeader("X-Podlord-Test", "actual-request-header");
    const auto input = payload(131083);
    QPointer<QNetworkReply> reply = scenario == "post" ? client->post(requestFor("/echo"), input)
        : scenario == "patch" ? client->sendCustomRequest(requestFor("/echo"), "PATCH", input) : client->get(request);
    QByteArray received;
    int finished = 0;
    qint64 largestBuffer = 0;
    QObject::connect(reply, &QNetworkReply::finished, &app, [&] { ++finished; });
    if (streaming) {
        reply->setReadBufferSize(4096);
        QObject::connect(reply, &QNetworkReply::readyRead, &app, [&] {
            largestBuffer = std::max(largestBuffer, reply->bytesAvailable());
            if (scenario == "cancel_unread") { reply->abort(); return; }
            if (scenario == "paused_reader") return;
            received += reply->readAll();
            if (scenario == "cancel_active" && received.size() >= 8192) reply->abort();
        });
    }
    if (scenario == "paused_reader") {
        char byte{};
        if (reply->read(&byte, 1) != 0 || reply->isFinished()) return 1;
        if (!waitFor([&] { return reply->bytesAvailable() == 4096; })) return 1;
        QTest::qWait(100);
        if (reply->bytesAvailable() != 4096 || reply->isFinished()) return 1;
        received += reply->read(512);
        if (received.size() != 512 || !waitFor([&] { return reply->bytesAvailable() == 4096; }) || reply->isFinished()) return 1;
        // QTest polling must not determine the consumer's transfer rate.
        const auto drain = [&] { while (reply->bytesAvailable() > 0) received += reply->read(512); };
        QObject::connect(reply, &QNetworkReply::readyRead, &app, drain);
        drain();
    }
    if (scenario == "cancel_metadata") QObject::connect(reply, &QNetworkReply::metaDataChanged, &app, [reply] { reply->abort(); });
    if (scenario == "cancel_before") reply->abort();
    if (scenario == "destroy") {
        if (!waitFor([&] { return !received.isEmpty(); })) return 1;
        client.reset();
        if (!reply.isNull()) return 1;
        // Observe closure while this process is alive, rather than hiding a leaked transfer at exit.
        podlord::BackgroundHttpClient verifier;
        auto* probe = verifier.get(requestFor("/connections"));
        if (!waitFor([&] { return probe->isFinished(); })) return 1;
        return probe->error() == QNetworkReply::NoError && probe->readAll() == "0" ? 0 : 1;
    }
    if (!waitFor([&] { return finished == 1; })) { std::fprintf(stderr, "HTTP scenario timed out: %s; received=%lld buffered=%lld finished=%d\n", argv[2], static_cast<long long>(received.size()), static_cast<long long>(reply->bytesAvailable()), finished); return 1; }
    if (scenario == "paused_reader" && (!reply->read(1).isEmpty() || !reply->atEnd())) return 1;
    if (scenario == "cancel_before" || scenario == "cancel_active" || scenario == "cancel_unread" || scenario == "cancel_metadata") {
        QTest::qWait(30);
        return reply->error() == QNetworkReply::OperationCanceledError && finished == 1 ? 0 : 1;
    }
    if (scenario == "disconnect") return reply->error() != QNetworkReply::NoError ? 0 : 1;
    if (scenario == "not_found") return reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 404
        && reply->error() == QNetworkReply::ContentNotFoundError && reply->readAll() == "missing" ? 0 : 1;
    if (scenario == "redirect") return reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 302
        && reply->attribute(QNetworkRequest::RedirectionTargetAttribute).toUrl() == QUrl("/body") ? 0 : 1;
    received += reply->readAll();
    const auto tls = reply->sslConfiguration();
    const auto expected = scenario == "post" || scenario == "patch" ? input : payload(streaming ? 196619 : 262175);
    const bool valid = reply->error() == QNetworkReply::NoError && received == expected
        && tls.peerCertificateChain().isEmpty()
        && (!streaming || largestBuffer <= 4096)
        && reply->header(QNetworkRequest::ContentLengthHeader).toLongLong() == expected.size();
    std::printf("Background HTTP %s: bytes=%lld largest_buffer=%lld finished=%d\n", argv[2],
        static_cast<long long>(received.size()), static_cast<long long>(largestBuffer), finished);
    return valid ? 0 : 1;
}
