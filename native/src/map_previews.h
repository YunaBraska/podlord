#pragma once
#include <QFutureWatcher>
#include <QImage>
#include <QMap>
#include <QObject>
#include <QSet>

namespace podlord {
/** Private visual history only. No Kubernetes documents, credentials or transport. */
class MapPreviews final : public QObject {
    Q_OBJECT
public:
    explicit MapPreviews(QString profile);
    ~MapPreviews() override;
    void reconcile(QSet<QString> contexts, bool authoritative);
    void forget(const QString& context);
    bool save(const QString& context, const QImage& image);
    QString url(const QString& context) const { return contexts_.contains(context) ? urls_.value(context) : QString{}; }
    QString error() const { return error_; }
    quint64 revision() const { return revision_; }
signals:
    void changed();
private:
    struct Snapshot final { QMap<QString,QString> urls; QString error; bool readable = false; };
    const QString profile_;
    QSet<QString> contexts_;
    QMap<QString,QString> urls_;
    QMap<QString,QImage> pending_;
    QSet<QString> removed_;
    QFutureWatcher<Snapshot> task_{this};
    QString error_;
    quint64 revision_ = 0;
    bool dirty_ = false, authoritative_ = false, busy_ = false;
    void dispatch();
    static Snapshot persist(const QString& profile, const QSet<QString>& contexts, bool prune, const QMap<QString,QImage>& images, const QSet<QString>& removed);
};
}
