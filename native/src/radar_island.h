#pragma once
#include <QAbstractListModel>
#include <QPersistentModelIndex>
#include <QPointer>
#include <QQuickPaintedItem>
#include <QSet>
#include <QTimer>
#include <QElapsedTimer>
#include <QQueue>
#include <QCache>

namespace podlord {
class RadarTiles final : public QAbstractListModel {
    Q_OBJECT
public:
    struct Entry final { QString path; QPersistentModelIndex source; QPointF world; };
    explicit RadarTiles(QObject* parent) : QAbstractListModel(parent) {}
    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    bool publish(const QList<Entry>& entries, bool refresh);
private:
    QList<Entry> entries_;
};

/** Deterministic cache geometry and viewport projection; never owns transport. */
class RadarIsland : public QQuickPaintedItem {
    Q_OBJECT
    Q_PROPERTY(QAbstractItemModel* source READ source WRITE setSource NOTIFY sourceChanged)
    Q_PROPERTY(QString identityScope READ identityScope WRITE setIdentityScope NOTIFY sourceChanged)
    Q_PROPERTY(QAbstractItemModel* tiles READ tiles CONSTANT)
    Q_PROPERTY(QVariantMap viewPose READ viewPose WRITE setViewPose NOTIFY viewPoseChanged)
    Q_PROPERTY(int currentIndex READ currentIndex NOTIFY currentResourceChanged)
public:
    explicit RadarIsland(QQuickItem* parent = nullptr);
    QAbstractItemModel* source() const { return source_; }
    QString identityScope() const { return identityScope_; }
    QAbstractItemModel* tiles() { return &tiles_; }
    QVariantMap viewPose() const;
    static bool validPose(const QVariantMap& pose);
    int currentIndex() const;
    void setSource(QAbstractItemModel* source);
    void setViewPose(const QVariantMap& pose);
    Q_INVOKABLE bool pan(double dx, double dy);
    Q_INVOKABLE bool zoomAt(double factor, double x, double y);
    Q_INVOKABLE bool focusResource(int index, double zoom = 0);
    Q_INVOKABLE bool selectResource(int index);
    Q_INVOKABLE bool resetView();
    void paint(QPainter* painter) override;
signals:
    void sourceChanged();
    void viewPoseChanged();
    void currentResourceChanged();
private:
    struct Row final {
        QString path, kind, name, scope, cluster, owner, identity;
        bool operator==(const Row&) const = default;
    };
    struct Marker final { QPointF world; QString kind, group; };
    struct Terrain final {
        QList<Row> signature;
        QHash<QString, QPointF> positions;
        QHash<QPoint, QList<QString>> buckets;
        QList<Marker> markers;
    };
    QCache<QString, Terrain> terrains_{3};
    RadarTiles tiles_{this};
    QPointer<QAbstractItemModel> source_;
    QList<QMetaObject::Connection> connections_;
    QList<Row> signature_;
    QHash<QString, QPointF> positions_;
    QHash<QPoint, QList<QString>> resourceBuckets_;
    QHash<QString, QPersistentModelIndex> filtered_;
    QSet<QString> groups_;
    QList<Marker> markers_;
    QPointF pan_;
    QString currentPath_;
    QString identityScope_;
    double zoom_ = 1;
    bool pending_ = false, dirty_ = true;
    void setIdentityScope(const QString& scope);
    void schedule();
    void synchronize();
    void layout(const QList<Row>& rows);
    void project(bool refresh = false);
    QRectF worldViewport() const;
};

/** One viewport-sized background water surface with a bounded animation clock. */
class RadarWater : public QQuickPaintedItem {
    Q_OBJECT
    Q_PROPERTY(QVariantMap viewPose READ viewPose WRITE setViewPose NOTIFY viewPoseChanged)
    Q_PROPERTY(QColor color READ color WRITE setColor NOTIFY colorChanged)
    Q_PROPERTY(bool playing READ playing WRITE setPlaying NOTIFY playingChanged)
    Q_PROPERTY(int speedPercent READ speedPercent WRITE setSpeedPercent NOTIFY speedPercentChanged)
public:
    explicit RadarWater(QQuickItem* parent = nullptr);
    QVariantMap viewPose() const { return pose_; }
    QColor color() const { return color_; }
    bool playing() const { return playing_; }
    int speedPercent() const { return speed_; }
    void setViewPose(const QVariantMap& pose);
    void setColor(const QColor& color);
    void setPlaying(bool playing);
    void setSpeedPercent(int speed);
    Q_INVOKABLE bool noteRequest();
    void paint(QPainter* painter) override;
signals:
    void viewPoseChanged();
    void colorChanged();
    void playingChanged();
    void speedPercentChanged();
private:
    QVariantMap pose_{{"x",0},{"y",0},{"zoom",1}};
    QColor color_{QStringLiteral("#071619")};
    QTimer timer_;
    QElapsedTimer clock_;
    QQueue<qint64> requests_;
    qint64 interactionUntil_ = 0;
    int speed_ = 45, phase_ = 0;
    bool playing_ = false;
    void syncTimer();
    int interval();
};
}
