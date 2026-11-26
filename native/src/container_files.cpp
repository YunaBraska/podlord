#include "container_files.h"
#include "resource_client.h"
#include "platform_tls.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkRequest>
#include <QSaveFile>
#include <QStandardPaths>
#include <QDir>
#include <QGuiApplication>
#include <QUrlQuery>
#include <QWebSocketHandshakeOptions>
#include <algorithm>
#ifdef Q_OS_ANDROID
#include <QJniEnvironment>
#include <QJniObject>
#endif

namespace podlord {
namespace {
constexpr qsizetype listingLimit = 1024 * 1024, downloadLimit = 32 * 1024 * 1024;
bool validName(const QString& name) {
    return !name.isEmpty() && name != "." && name != ".." && name.size() <= 255
        && !name.contains('/') && !name.contains(QChar::Null);
}
}
ContainerFiles::ContainerFiles(ResourceClient& client, QString session, QString podPath, QString container, QObject* parent)
    : QObject(parent), client_(client), session_(std::move(session)), podPath_(std::move(podPath)), container_(std::move(container)),
      podUid_(client.resource(session_, podPath_)["uid"].toString()) {
    socket_.setProxy(QNetworkProxy::NoProxy);
    configurePlatformTrust(socket_);
    socket_.setReadBufferSize(downloadLimit + 64);
    socket_.setMaxAllowedIncomingFrameSize(downloadLimit + 1);
    socket_.setMaxAllowedIncomingMessageSize(downloadLimit + 1);
    timeout_.setSingleShot(true);
    connect(&client_, &ResourceClient::changed, this, [this](const QString& id) {
        if (id != session_ || (client_.sessionOpen(session_) && !client_.authenticationRequired(session_)
            && (!client_.containsResource(session_, podPath_) || client_.resource(session_, podPath_)["uid"] == podUid_))) return;
        if (busy_) fail(client_.authenticationRequired(session_) ? "files.execFailed" : "files.targetChanged");
        else if (!downloadName_.isEmpty()) discardDownload();
    });
    connect(&timeout_, &QTimer::timeout, this, [this] { fail("files.execFailed"); });
    connect(&socket_, &QWebSocket::connected, this, [this] {
        if (socket_.subprotocol() != "v5.channel.k8s.io") { fail("files.execFailed"); return; }
        negotiated_ = true;
        if (!reported_) { reported_ = true; emit handshakeFinished(true, false); }
    });
    connect(&socket_, &QWebSocket::authenticationRequired, this, [this](QAuthenticator*) { fail("files.execFailed", true); });
    connect(&socket_, &QWebSocket::errorOccurred, this, [this](QAbstractSocket::SocketError error) {
        if (busy_ && (error != QAbstractSocket::RemoteHostClosedError || (remoteStatus_.isEmpty() && !statusClosed_))) fail("files.execFailed");
    });
    connect(&socket_, &QWebSocket::textMessageReceived, this, [this](const QString&) { fail("files.execFailed"); });
    connect(&socket_, &QWebSocket::binaryMessageReceived, this, [this](const QByteArray& frame) {
        if (!busy_ || frame.isEmpty()) { if (busy_) fail("files.execFailed"); return; }
        const auto channel = static_cast<quint8>(frame[0]);
        const auto bytes = frame.sliced(1);
        if (channel == 1) {
            if (output_.size() + bytes.size() > (file_ ? downloadLimit : listingLimit)) { fail("files.tooLarge"); return; }
            output_ += bytes;
        } else if (channel == 2) {
            // Stderr can contain file data or credentials. It is never shown or logged.
            if (bytes.size() > 4096) { fail("files.execFailed"); return; }
        } else if (channel == 3) {
            if (remoteStatus_.size() + bytes.size() > 65536) { fail("files.execFailed"); return; }
            remoteStatus_ += bytes;
        } else if (channel == 255 && bytes.size() == 1 && static_cast<quint8>(bytes[0]) == 1) {
            stdoutClosed_ = true;
            if (statusClosed_) finish();
        } else if (channel == 255 && bytes.size() == 1 && static_cast<quint8>(bytes[0]) == 3) {
            statusClosed_ = true;
            if (stdoutClosed_) finish();
        } else if (channel != 255 || bytes.size() != 1 || static_cast<quint8>(bytes[0]) > 4) fail("files.execFailed");
    });
    connect(&socket_, &QWebSocket::disconnected, this, [this] {
        if (busy_ && (statusClosed_ || !remoteStatus_.isEmpty())) finish();
        else if (busy_) fail("files.execFailed");
    });
}
ContainerFiles::~ContainerFiles() { socket_.abort(); }
QUrl ContainerFiles::suggestedSaveUrl() const {
    return QUrl::fromLocalFile(QDir(QStandardPaths::writableLocation(QStandardPaths::DownloadLocation)).filePath(downloadName_));
}
bool ContainerFiles::request(bool file, const QString& target) {
    if (busy_ || target.isEmpty() || !target.startsWith('/')) return false;
    pendingPath_ = target; file_ = file; busy_ = true; negotiated_ = false; stdoutClosed_ = false; statusClosed_ = false; reported_ = false;
    output_.clear(); remoteStatus_.clear(); downloadName_.clear(); status_ = "files.loading";
    emit changed();
    if (client_.startFileRead(session_, podPath_, container_, this)) return true;
    fail("files.targetChanged"); return false;
}
bool ContainerFiles::open(QNetworkRequest request) {
    if (!busy_ || socket_.state() != QAbstractSocket::UnconnectedState) return false;
    timeout_.start(file_ ? 120000 : 20000);
    auto url = request.url(); url.setScheme(url.scheme() == "https" ? "wss" : "ws");
    QUrlQuery query;
    const auto add = [&query](const QString& key, const QString& value) {
        query.addQueryItem(key, QString::fromLatin1(QUrl::toPercentEncoding(value)));
    };
    add("container", container_);
    add("stdin", "false"); add("stdout", "true");
    add("stderr", "true"); add("tty", "false");
    add("command", "/bin/sh"); add("command", "-c");
    add("command", file_
        ? "[ -f \"$1\" ] && [ ! -L \"$1\" ] && exec cat \"$1\""
        : "for f in \"$1\"/* \"$1\"/.[!.]* \"$1\"/..?*; do [ -e \"$f\" ] || [ -L \"$f\" ] || continue; "
          "if [ -L \"$f\" ]; then t=l; elif [ -d \"$f\" ]; then t=d; elif [ -f \"$f\" ]; then t=f; else t=o; fi; "
          "printf '%s\\000%s\\000' \"$t\" \"${f##*/}\"; done");
    add("command", "sh"); add("command", pendingPath_);
    url.setQuery(query); request.setUrl(url); request.setRawHeader("Accept", "*/*");
    auto tls = request.sslConfiguration(); tls.setAllowedNextProtocols({QSslConfiguration::NextProtocolHttp1_1});
    socket_.setSslConfiguration(tls);
    QWebSocketHandshakeOptions options; options.setSubprotocols({"v5.channel.k8s.io"});
    socket_.open(request, options); return true;
}
bool ContainerFiles::browse(const QString& name) {
    if (!name.isEmpty() && !validName(name)) return false;
    if (!name.isEmpty()) {
        bool listedDirectory = false;
        for (const auto& value : entries_) if (value.toMap().value("name") == name && value.toMap().value("type") == "directory") listedDirectory = true;
        if (!listedDirectory) return false;
    }
    return request(false, name.isEmpty() ? directory_ : directory_ == "/" ? "/" + name : directory_ + '/' + name);
}
bool ContainerFiles::up() {
    if (directory_ == "/") return false;
    return request(false, directory_.section('/', 0, -2).isEmpty() ? "/" : directory_.section('/', 0, -2));
}
bool ContainerFiles::refresh() { return request(false, directory_); }
bool ContainerFiles::download(const QString& name) {
    if (!validName(name)) return false;
    bool listedFile = false;
    for (const auto& value : entries_) if (value.toMap().value("name") == name && value.toMap().value("type") == "file") listedFile = true;
    return listedFile && request(true, directory_ == "/" ? "/" + name : directory_ + '/' + name);
}
bool ContainerFiles::save(const QUrl& destination) {
    if (downloadName_.isEmpty()) { status_ = "files.saveFailed"; emit changed(); return false; }
#ifdef Q_OS_ANDROID
    if (destination.scheme() == "content") {
        QJniEnvironment environment;
        const auto context = QNativeInterface::QAndroidApplication::context();
        const auto url = QJniObject::fromString(destination.toString(QUrl::FullyEncoded));
        auto* bytes = environment->NewByteArray(static_cast<jsize>(output_.size()));
        if (!bytes) { status_ = "files.saveFailed"; emit changed(); return false; }
        environment->SetByteArrayRegion(bytes, 0, static_cast<jsize>(output_.size()), reinterpret_cast<const jbyte*>(output_.constData()));
        const auto saved = QJniObject::callStaticMethod<jboolean>("berlin/yuna/podlord/DocumentAccess", "write",
            "(Landroid/content/Context;Ljava/lang/String;[B)Z", context.object<jobject>(), url.object<jstring>(), bytes);
        environment->DeleteLocalRef(bytes);
        if (!saved) { status_ = "files.saveFailed"; emit changed(); return false; }
        output_.clear(); downloadName_.clear(); status_ = "files.saved"; emit changed(); return true;
    }
#endif
    if (!destination.isLocalFile()) { status_ = "files.saveFailed"; emit changed(); return false; }
    QSaveFile file(destination.toLocalFile());
    if (!file.open(QIODevice::WriteOnly) || file.write(output_) != output_.size() || !file.commit()) {
        status_ = "files.saveFailed"; emit changed(); return false;
    }
    output_.clear(); downloadName_.clear(); status_ = "files.saved"; emit changed(); return true;
}
void ContainerFiles::discardDownload() { output_.clear(); downloadName_.clear(); status_.clear(); emit changed(); }
void ContainerFiles::reject(const QString& message) { fail(message); }
void ContainerFiles::fail(const QString& message, bool authenticationRequired) {
    timeout_.stop();
    busy_ = false; output_.clear(); remoteStatus_.clear(); downloadName_.clear(); status_ = message;
    if (!reported_) { reported_ = true; emit handshakeFinished(false, authenticationRequired); }
    socket_.abort(); emit changed();
}
void ContainerFiles::finish() {
    if (!busy_) return;
    if (!negotiated_ || (!statusClosed_ && remoteStatus_.isEmpty())) { fail("files.execFailed"); return; }
    timeout_.stop();
    if (!remoteStatus_.isEmpty()) {
        QJsonParseError parse;
        const auto status = QJsonDocument::fromJson(remoteStatus_, &parse).object();
        if (parse.error != QJsonParseError::NoError || status["status"] != "Success") { fail("files.execFailed"); return; }
    }
    if (file_) {
        downloadName_ = pendingPath_.section('/', -1);
        status_.clear();
    } else {
        const auto parts = output_.split('\0');
        if (!output_.isEmpty() && (parts.size() < 3 || !parts.last().isEmpty() || (parts.size() - 1) % 2 != 0)) { fail("files.execFailed"); return; }
        QVariantList entries;
        for (int i = 0; i + 1 < parts.size() - 1; i += 2) {
            const auto name = QString::fromUtf8(parts[i + 1]);
            if (!validName(name) || !QByteArrayList{"d", "f", "l", "o"}.contains(parts[i]) || entries.size() >= 1000) { fail("files.execFailed"); return; }
            entries.append(QVariantMap{{"name", name}, {"type", parts[i] == "d" ? "directory" : parts[i] == "f" ? "file" : parts[i] == "l" ? "link" : "other"}});
        }
        std::sort(entries.begin(), entries.end(), [](const QVariant& a, const QVariant& b) {
            const auto left = a.toMap(), right = b.toMap();
            if ((left.value("type") == "directory") != (right.value("type") == "directory")) return left.value("type") == "directory";
            return left.value("name").toString().compare(right.value("name").toString(), Qt::CaseInsensitive) < 0;
        });
        entries_ = std::move(entries); directory_ = pendingPath_; output_.clear(); status_.clear();
    }
    busy_ = false; remoteStatus_.clear(); socket_.abort(); emit changed();
    if (file_) emit downloadReady();
}
} // namespace podlord
