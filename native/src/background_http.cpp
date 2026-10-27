#include "background_http.h"
#include "platform_tls.h"
#include <QNetworkReply>
#include <QPointer>
#include <QSslConfiguration>
#include <QThread>
#include <algorithm>
#include <cstring>
#include <stop_token>

namespace podlord {
struct HttpMetadata {
    QUrl url;
    QList<QNetworkReply::RawHeaderPair> headers;
    QHash<QNetworkRequest::Attribute, QVariant> attributes;
    QSslConfiguration tls;
};
}
Q_DECLARE_METATYPE(podlord::HttpMetadata)

namespace podlord {
namespace {
constexpr qint64 chunkLimit = 65536;

class HttpTransfer final : public QObject {
    Q_OBJECT
public:
    HttpTransfer(QNetworkAccessManager& network, QNetworkAccessManager::Operation operation,
        QNetworkRequest request, QByteArray method, QByteArray body, QNetworkProxy proxy, std::stop_token stop)
        : network_(network), operation_(operation), request_(std::move(request)), method_(std::move(method)),
          body_(std::move(body)), proxy_(std::move(proxy)), stop_(std::move(stop)) {}
    void start(qint64 capacity) {
        if (done_) return;
        if (stop_.stop_requested()) { cancel(); return; }
        credit_ = capacity;
        setParent(&network_);
        network_.setProxy(proxy_);
        reply_ = operation_ == QNetworkAccessManager::GetOperation ? network_.get(request_)
            : operation_ == QNetworkAccessManager::PostOperation ? network_.post(request_, body_)
            : network_.sendCustomRequest(request_, method_, body_);
        reply_->setReadBufferSize(chunkLimit);
        configurePlatformTrust(*reply_, stop_);
        connect(reply_, &QNetworkReply::metaDataChanged, this, &HttpTransfer::publishMetadata);
        connect(reply_, &QNetworkReply::readyRead, this, &HttpTransfer::drain);
        connect(reply_, &QNetworkReply::finished, this, &HttpTransfer::drain);
        connect(reply_, &QNetworkReply::encrypted, this, [this] { publishMetadata(); emit encrypted(); });
        connect(reply_, &QNetworkReply::downloadProgress, this, &HttpTransfer::downloadProgress);
        connect(reply_, &QNetworkReply::uploadProgress, this, &HttpTransfer::uploadProgress);
        connect(reply_, &QNetworkReply::requestSent, this, &HttpTransfer::requestSent);
        connect(reply_, &QNetworkReply::socketStartedConnecting, this, &HttpTransfer::socketStartedConnecting);
        connect(reply_, &QNetworkReply::redirected, this, &HttpTransfer::redirected);
    }
    void cancel() {
        if (done_) return;
        done_ = true;
        if (reply_) { disconnect(reply_, nullptr, this, nullptr); reply_->abort(); reply_->deleteLater(); }
        emit completed(QNetworkReply::OperationCanceledError, "HTTP request canceled.");
        deleteLater();
    }
    void grant(qint64 capacity) { if (!done_) { credit_ = capacity; drain(); } }
signals:
    void metadataReady(const podlord::HttpMetadata& metadata);
    void chunkReady(const QByteArray& bytes);
    void completed(QNetworkReply::NetworkError error, const QString& message);
    void encrypted();
    void requestSent();
    void socketStartedConnecting();
    void redirected(const QUrl& url);
    void downloadProgress(qint64 received, qint64 total);
    void uploadProgress(qint64 sent, qint64 total);
private:
    void publishMetadata() {
        if (!reply_) return;
        HttpMetadata metadata{reply_->url(), reply_->rawHeaderPairs(), {}, reply_->sslConfiguration()};
        for (const auto key : {QNetworkRequest::HttpStatusCodeAttribute, QNetworkRequest::HttpReasonPhraseAttribute,
            QNetworkRequest::RedirectionTargetAttribute, QNetworkRequest::ConnectionEncryptedAttribute,
            QNetworkRequest::SourceIsFromCacheAttribute, QNetworkRequest::Http2WasUsedAttribute}) {
            const auto value = reply_->attribute(key);
            if (value.isValid()) metadata.attributes.insert(key, value);
        }
        emit metadataReady(metadata);
    }
    void drain() {
        if (done_ || !reply_) return;
        if (credit_ > 0 && reply_->bytesAvailable() > 0) {
            const auto bytes = reply_->read(std::min(credit_, chunkLimit));
            if (!bytes.isEmpty()) { credit_ = 0; emit chunkReady(bytes); }
        }
        if (reply_->isFinished() && reply_->bytesAvailable() == 0) {
            done_ = true;
            publishMetadata();
            emit completed(reply_->error(), reply_->errorString());
            reply_->deleteLater();
            deleteLater();
        }
    }
    QNetworkAccessManager& network_;
    const QNetworkAccessManager::Operation operation_;
    const QNetworkRequest request_;
    const QByteArray method_, body_;
    const QNetworkProxy proxy_;
    const std::stop_token stop_;
    QPointer<QNetworkReply> reply_;
    qint64 credit_ = 0;
    bool done_ = false;
};

class BufferedHttpReply final : public QNetworkReply {
    Q_OBJECT
public:
    BufferedHttpReply(QNetworkAccessManager::Operation operation, const QNetworkRequest& request, QObject* parent)
        : QNetworkReply(parent), tls_(request.sslConfiguration()) {
        setOperation(operation); setRequest(request); setUrl(request.url());
        open(QIODevice::ReadOnly | QIODevice::Unbuffered);
    }
    ~BufferedHttpReply() override { stop_.request_stop(); emit cancelRequested(); }
    std::stop_token stopToken() const { return stop_.get_token(); }
    void dispatch() { emit dispatchRequested(readBufferSize() > 0 ? std::min(readBufferSize(), chunkLimit) : chunkLimit); }
    bool isSequential() const override { return true; }
    qint64 bytesAvailable() const override { return buffer_.size() - offset_ + QNetworkReply::bytesAvailable(); }
    void setReadBufferSize(qint64 size) override { QNetworkReply::setReadBufferSize(size); grant(); }
    void abort() override {
        if (isFinished()) return;
        stop_.request_stop();
        setError(OperationCanceledError, "HTTP request canceled."); setFinished(true);
        emit cancelRequested();
        QMetaObject::invokeMethod(this, [this] { emit errorOccurred(error()); emit readChannelFinished(); emit finished(); }, Qt::QueuedConnection);
    }
    void receive(const QByteArray& bytes) {
        if (isFinished()) return;
        if (offset_ > 0) { buffer_.remove(0, offset_); offset_ = 0; }
        buffer_.append(bytes); awaitingCredit_ = true;
        emit readyRead();
        grant();
    }
    void applyMetadata(const HttpMetadata& metadata) {
        if (isFinished()) return;
        setUrl(metadata.url); tls_ = metadata.tls;
        for (const auto& header : metadata.headers) setRawHeader(header.first, header.second);
        for (auto it = metadata.attributes.cbegin(); it != metadata.attributes.cend(); ++it) setAttribute(it.key(), it.value());
        emit metaDataChanged();
    }
    void complete(QNetworkReply::NetworkError code, const QString& message) {
        if (isFinished()) return;
        if (code != NoError) setError(code, message);
        setFinished(true);
        if (code != NoError) emit errorOccurred(code);
        emit readChannelFinished(); emit finished();
    }
signals:
    void dispatchRequested(qint64 capacity);
    void cancelRequested();
    void capacityAvailable(qint64 capacity);
protected:
    qint64 readData(char* destination, qint64 maximum) override {
        const auto count = std::min(maximum, qint64(buffer_.size() - offset_));
        if (count <= 0) return isFinished() ? -1 : 0;
        std::memcpy(destination, buffer_.constData() + offset_, size_t(count));
        offset_ += count;
        if (offset_ == buffer_.size()) { buffer_.clear(); offset_ = 0; }
        grant();
        return count;
    }
    void sslConfigurationImplementation(QSslConfiguration& configuration) const override { configuration = tls_; }
private:
    void grant() {
        if (!awaitingCredit_ || isFinished()) return;
        const auto capacity = readBufferSize() > 0 ? std::max(qint64(0), readBufferSize() - (buffer_.size() - offset_)) : chunkLimit;
        if (capacity > 0) { awaitingCredit_ = false; emit capacityAvailable(std::min(capacity, chunkLimit)); }
    }
    std::stop_source stop_;
    QSslConfiguration tls_;
    QByteArray buffer_;
    qint64 offset_ = 0;
    bool awaitingCredit_ = false;
};
}

struct BackgroundHttpClient::State {
    QThread thread;
    QNetworkAccessManager* network = new QNetworkAccessManager;
    QNetworkProxy proxy;
    State() {
        thread.setObjectName("Podlord HTTP");
        network->moveToThread(&thread);
        QObject::connect(&thread, &QThread::finished, network, &QObject::deleteLater);
        thread.start();
    }
    ~State() {
        // Drain per-request cancellation before stopping its owner thread.
        QMetaObject::invokeMethod(network, [] {}, Qt::BlockingQueuedConnection);
        thread.quit(); thread.wait();
    }
};
BackgroundHttpClient::BackgroundHttpClient(QObject* parent) : QObject(parent), state_(std::make_unique<State>()) {
    qRegisterMetaType<HttpMetadata>();
}
BackgroundHttpClient::~BackgroundHttpClient() {
    for (auto* reply : findChildren<QNetworkReply*>()) reply->abort();
}
QNetworkReply* BackgroundHttpClient::get(const QNetworkRequest& request) { return send(QNetworkAccessManager::GetOperation, request, {}, {}); }
QNetworkReply* BackgroundHttpClient::post(const QNetworkRequest& request, const QByteArray& body) { return send(QNetworkAccessManager::PostOperation, request, {}, body); }
QNetworkReply* BackgroundHttpClient::sendCustomRequest(const QNetworkRequest& request, const QByteArray& method, const QByteArray& body) {
    return send(QNetworkAccessManager::CustomOperation, request, method, body);
}
BackgroundHttpClient& BackgroundHttpClient::setProxy(const QNetworkProxy& proxy) { state_->proxy = proxy; return *this; }
BackgroundHttpClient& BackgroundHttpClient::clearConnectionCache() {
    QMetaObject::invokeMethod(state_->network, &QNetworkAccessManager::clearConnectionCache, Qt::QueuedConnection);
    return *this;
}
QNetworkReply* BackgroundHttpClient::send(QNetworkAccessManager::Operation operation, const QNetworkRequest& request,
    const QByteArray& method, const QByteArray& body) {
    auto* reply = new BufferedHttpReply(operation, request, this);
    auto* transfer = new HttpTransfer(*state_->network, operation, request, method, body, state_->proxy, reply->stopToken());
    transfer->moveToThread(&state_->thread);
    connect(&state_->thread, &QThread::finished, transfer, &QObject::deleteLater);
    connect(reply, &BufferedHttpReply::dispatchRequested, transfer, &HttpTransfer::start, Qt::QueuedConnection);
    connect(reply, &BufferedHttpReply::cancelRequested, transfer, &HttpTransfer::cancel, Qt::QueuedConnection);
    connect(reply, &BufferedHttpReply::capacityAvailable, transfer, &HttpTransfer::grant, Qt::QueuedConnection);
    connect(transfer, &HttpTransfer::metadataReady, reply, &BufferedHttpReply::applyMetadata);
    connect(transfer, &HttpTransfer::chunkReady, reply, &BufferedHttpReply::receive);
    connect(transfer, &HttpTransfer::completed, reply, &BufferedHttpReply::complete);
    connect(transfer, &HttpTransfer::encrypted, reply, &QNetworkReply::encrypted);
    connect(transfer, &HttpTransfer::requestSent, reply, &QNetworkReply::requestSent);
    connect(transfer, &HttpTransfer::socketStartedConnecting, reply, &QNetworkReply::socketStartedConnecting);
    connect(transfer, &HttpTransfer::redirected, reply, &QNetworkReply::redirected);
    connect(transfer, &HttpTransfer::downloadProgress, reply, &QNetworkReply::downloadProgress);
    connect(transfer, &HttpTransfer::uploadProgress, reply, &QNetworkReply::uploadProgress);
    QMetaObject::invokeMethod(reply, [reply] { reply->dispatch(); }, Qt::QueuedConnection);
    return reply;
}
}
#include "background_http.moc"
