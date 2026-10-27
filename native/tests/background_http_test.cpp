#include "background_http.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QNetworkReply>
#include <QPointer>
#include <QTimer>
#include <QTest>
#include <QUrlQuery>
#include <cstdio>
#include <functional>

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
    QTimer reader;
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
        if (!waitFor([&] { return reply->bytesAvailable() == 4096; })) return 1;
        QTest::qWait(100);
        if (reply->bytesAvailable() != 4096 || reply->isFinished()) return 1;
        // Exercise partial reads without turning platform timer granularity into a speed gate.
        reader.setInterval(0);
        QObject::connect(&reader, &QTimer::timeout, &app, [&] { received += reply->read(512); });
        reader.start();
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
    if (scenario == "cancel_before" || scenario == "cancel_active" || scenario == "cancel_unread" || scenario == "cancel_metadata") {
        QTest::qWait(30);
        return reply->error() == QNetworkReply::OperationCanceledError && finished == 1 ? 0 : 1;
    }
    if (scenario == "disconnect") return reply->error() != QNetworkReply::NoError ? 0 : 1;
    if (scenario == "not_found") return reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 404
        && reply->error() == QNetworkReply::ContentNotFoundError && reply->readAll() == "missing" ? 0 : 1;
    if (scenario == "redirect") return reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 302
        && reply->attribute(QNetworkRequest::RedirectionTargetAttribute).toUrl() == QUrl("/body") ? 0 : 1;
    reader.stop();
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
