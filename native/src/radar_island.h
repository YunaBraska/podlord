#pragma once
#include "alerts.h"
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
    Q_PROPERTY(int animationRole READ animationRole CONSTANT)
public:
    static constexpr int animationScaleRole = Qt::UserRole + 24;
    int animationRole() const { return animationScaleRole; }
    enum class Animation { None, Blink, Pulse, Outline, Sweep };
    struct Entry final {
        QString path;
        QPersistentModelIndex source;
        int resourceIndex = -1;
        QPointF world;
        QString kind;
        QColor color, announce;
        Animation animation = Animation::None;
        bool glow = false;
        qint64 started = 0;
    };
    explicit RadarTiles(QObject* parent) : QAbstractListModel(parent) {}
    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    bool publish(const QList<Entry>& entries, bool refresh);
    void refreshSourceIndices();
    const QList<Entry>& entries() const { return entries_; }
    qint64 frameTime() const { return frameTime_; }
    bool advanceFrame(qint64 at);
private:
    QList<Entry> entries_;
    qint64 frameTime_ = 0;
};

/** Deterministic cache geometry and viewport projection; never owns transport. */
class RadarIsland : public QQuickPaintedItem {
    Q_OBJECT
    Q_PROPERTY(QAbstractItemModel* source READ source WRITE setSource NOTIFY sourceChanged)
    Q_PROPERTY(QString identityScope READ identityScope WRITE setIdentityScope NOTIFY sourceChanged)
    Q_PROPERTY(QAbstractItemModel* tiles READ tiles CONSTANT)
    Q_PROPERTY(QVariantMap viewPose READ viewPose WRITE setViewPose NOTIFY viewPoseChanged)
    Q_PROPERTY(int currentIndex READ currentIndex NOTIFY currentResourceChanged)
    Q_PROPERTY(QColor textColor READ textColor WRITE setTextColor NOTIFY textColorChanged)
    Q_PROPERTY(Alerts* alerts READ alerts WRITE setAlerts NOTIFY alertsChanged)
    Q_PROPERTY(bool effectsEnabled READ effectsEnabled WRITE setEffectsEnabled NOTIFY effectsEnabledChanged)
    Q_PROPERTY(QColor glyphFill READ glyphFill WRITE setGlyphFill NOTIFY glyphFillChanged)
    Q_PROPERTY(QString inspectedPath READ inspectedPath WRITE setInspectedPath NOTIFY inspectedPathChanged)
    Q_PROPERTY(QString hoveredPath READ hoveredPath WRITE setHoveredPath NOTIFY hoveredPathChanged)
    Q_PROPERTY(QString focusedPath READ focusedPath WRITE setFocusedPath NOTIFY focusedPathChanged)
public:
    explicit RadarIsland(QQuickItem* parent = nullptr);
    ~RadarIsland() override;
    QAbstractItemModel* source() const { return source_; }
    QString identityScope() const { return identityScope_; }
    QAbstractItemModel* tiles() { return &tiles_; }
    QVariantMap viewPose() const;
    QColor textColor() const { return textColor_; }
    Alerts* alerts() const { return alerts_; }
    bool effectsEnabled() const { return effectsEnabled_; }
    QColor glyphFill() const { return glyphFill_; }
    QString inspectedPath() const { return inspectedPath_; }
    QString hoveredPath() const { return hoveredPath_; }
    QString focusedPath() const { return focusedPath_; }
    static bool validPose(const QVariantMap& pose);
    int currentIndex() const;
    void setSource(QAbstractItemModel* source);
    void setViewPose(const QVariantMap& pose);
    void setTextColor(const QColor& color);
    void setAlerts(Alerts* alerts);
    void setEffectsEnabled(bool enabled);
    void setGlyphFill(const QColor& color);
    void setInspectedPath(const QString& path);
    void setHoveredPath(const QString& path);
    void setFocusedPath(const QString& path);
    Q_INVOKABLE bool pan(double dx, double dy);
    Q_INVOKABLE bool zoomAt(double factor, double x, double y);
    Q_INVOKABLE bool focusResource(int index, double zoom = 0);
    Q_INVOKABLE bool selectResource(int index);
    Q_INVOKABLE bool resetView();
    Q_INVOKABLE bool fitView();
    Q_INVOKABLE QString resourceAt(double x, double y);
    Q_INVOKABLE QVariantMap resourceInfo(const QString& path, bool details) const;
    Q_INVOKABLE bool traverseVisible(int direction);
    Q_INVOKABLE QImage previewImage();
    void paint(QPainter* painter) override;
signals:
    void sourceChanged();
    void viewPoseChanged();
    void currentResourceChanged();
    void textColorChanged();
    void alertsChanged();
    void effectsEnabledChanged();
    void glyphFillChanged();
    void inspectedPathChanged();
    void hoveredPathChanged();
    void focusedPathChanged();
private:
    struct Row final {
        QString path, kind, name, scope, cluster, owner, identity;
        bool operator==(const Row&) const = default;
    };
    struct Marker final { QPointF world; QRectF labelBounds; QString kind, group, label; bool overview = false; QRectF area; };
    struct Terrain final {
        QList<Row> signature;
        QHash<QString, QPointF> positions;
        QHash<QPoint, QList<QString>> buckets;
        QList<Marker> markers;
        QHash<QPoint, QList<int>> markerBuckets;
        QRectF bounds;
    };
    QCache<QString, Terrain> terrains_{3};
    RadarTiles tiles_{this};
    QPointer<QAbstractItemModel> source_;
    QList<QMetaObject::Connection> connections_;
    QPointer<Alerts> alerts_;
    QList<QMetaObject::Connection> alertConnections_;
    QMetaObject::Connection frameConnection_;
    QElapsedTimer animationClock_;
    QTimer animationTimer_;
    QColor glyphFill_{QStringLiteral("#050806")};
    QString inspectedPath_, hoveredPath_, focusedPath_;
    bool effectsEnabled_ = true;
    QList<Row> signature_;
    QHash<QString, QPointF> positions_;
    QHash<QPoint, QList<QString>> resourceBuckets_;
    QHash<QString, QPersistentModelIndex> filtered_;
    QSet<QString> groups_;
    QList<Marker> markers_;
    QHash<QPoint, QList<int>> markerBuckets_;
    QRectF terrainBounds_;
    QPointF pan_;
    QColor textColor_{QStringLiteral("#DDE7EC")};
    QString currentPath_;
    QString identityScope_;
    double zoom_ = 1;
    bool pending_ = false, dirty_ = true, geometryDirty_ = true, filteredDirty_ = true;
    void setIdentityScope(const QString& scope);
    void observeSource();
    void schedule();
    void synchronize();
    void layout(const QList<Row>& rows);
    void project(bool refresh = false);
    void syncAnimationClock();
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
    ~RadarWater() override;
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
    QMetaObject::Connection exposureConnection_;
    qint64 interactionUntil_ = 0;
    int speed_ = 45, phase_ = 0;
    bool playing_ = false;
    void syncTimer();
    int interval();
};
}
