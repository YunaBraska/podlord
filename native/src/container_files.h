#pragma once
#include <QWebSocket>
#include <QNetworkRequest>
#include <QTimer>
#include <QVariantList>

namespace podlord {
class ResourceClient;
/** One read-only, user-initiated filesystem view of a verified running Pod container. */
class ContainerFiles final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList entries READ entries NOTIFY changed)
    Q_PROPERTY(QString directory READ directory NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(QString downloadName READ downloadName NOTIFY changed)
    Q_PROPERTY(QUrl suggestedSaveUrl READ suggestedSaveUrl NOTIFY changed)
public:
    ContainerFiles(ResourceClient& client, QString session, QString podPath, QString container, QObject* parent = nullptr);
    ~ContainerFiles() override;
    QVariantList entries() const { return entries_; }
    QString directory() const { return directory_; }
    QString status() const { return status_; }
    bool busy() const { return busy_; }
    QString downloadName() const { return downloadName_; }
    QUrl suggestedSaveUrl() const;
    QString podPath() const { return podPath_; }
    QString podUid() const { return podUid_; }
    QString container() const { return container_; }
    Q_INVOKABLE bool browse(const QString& name = {});
    Q_INVOKABLE bool up();
    Q_INVOKABLE bool refresh();
    Q_INVOKABLE bool download(const QString& name);
    Q_INVOKABLE bool save(const QUrl& destination);
    Q_INVOKABLE void discardDownload();
    bool open(QNetworkRequest request);
    void reject(const QString& message);
signals:
    void changed();
    void downloadReady();
    void handshakeFinished(bool accepted, bool authenticationRequired);
private:
    bool request(bool file, const QString& target);
    void fail(const QString& message, bool authenticationRequired = false);
    void finish();
    ResourceClient& client_;
    const QString session_, podPath_, container_, podUid_;
    QWebSocket socket_;
    QTimer timeout_;
    QString directory_ = "/", pendingPath_, downloadName_, status_;
    QByteArray output_, remoteStatus_;
    QVariantList entries_;
    bool busy_ = false, file_ = false, negotiated_ = false, stdoutClosed_ = false, statusClosed_ = false, reported_ = false;
};
} // namespace podlord
