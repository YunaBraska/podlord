#pragma once
#include "session_store.h"
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPointer>
#include <QLockFile>
#include <QTimer>
#include <QVariantMap>
#include <memory>

namespace podlord {
/** Anonymous release metadata only; never shares cluster credentials or installs software. */
class ReleaseUpdates final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantMap state READ state NOTIFY changed)
public:
    /** The alternate endpoint accepts only explicit loopback HTTP for external-boundary tests. */
    explicit ReleaseUpdates(QString profile, QObject* parent = nullptr,
        std::function<QDateTime()> now = QDateTime::currentDateTimeUtc, QUrl endpoint = officialEndpoint());
    ~ReleaseUpdates() override;
    static QUrl officialEndpoint();
    /** Exact native target name; empty for targets without a standalone installer. */
    static QString compatibleAssetName();
    QVariantMap state() const;
    /** Starts this owner's weekly schedule; repeated calls do not create another timer. */
    bool startAutomaticChecks();
    /** Resume/startup check, coalesced with any current request and persisted weekly attempt. */
    bool checkIfDue();
    Q_INVOKABLE bool checkNow();
    Q_INVOKABLE bool openDownload();
    Q_INVOKABLE bool openRelease();
signals:
    void changed();
private:
    struct Check final {
        QDateTime checkedAt;
        QString currentVersion, latestVersion, releaseUrl, downloadUrl, error;
    };
    const QString profile_, currentVersion_;
    const std::function<QDateTime()> now_;
    const QUrl endpoint_;
    QNetworkAccessManager network_;
    QPointer<QNetworkReply> reply_;
    QTimer weekly_, deadline_;
    std::unique_ptr<QLockFile> lock_;
    Check check_;
    QByteArray response_;
    QString localError_;
    bool automatic_ = false, enabled_ = true;
    Result<Check> load() const;
    Result<Check> save(const Check& value) const;
    bool check(bool force);
    bool due(const QDateTime& now) const;
    bool fail(const QString& error);
    bool complete(const QString& error = {});
    bool schedule();
};
} // namespace podlord
