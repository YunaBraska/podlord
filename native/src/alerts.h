#pragma once
#include "resource_client.h"
#include <QColor>
#include <QRegularExpression>
#include <QVariantList>
#include <QMediaPlayer>
#include <QAudioOutput>
#include <QStandardItemModel>
#include <QSortFilterProxyModel>

namespace podlord {
struct AlertCriterion final {
    QString field, expression;
    QStringList tokens;
    QList<QRegularExpression> patterns;
    QList<QPair<QString, double>> comparisons;
};
struct AlertRule final {
    QJsonObject value;
    QList<QList<AlertCriterion>> groups;
};
struct AlertCatalog final {
    QList<AlertRule> rules;
    bool muted = false, reducedMotion = false;
};
/** Parses and compiles an editable rule at the input boundary; never accepts unknown fields. */
Result<AlertRule> parseAlertRule(QJsonObject value);
/** Canonical built-in rules, matching the existing desktop catalog. */
/** Private atomic persistence. Invalid or newer data is never replaced with defaults. */
class AlertStore final {
public:
    explicit AlertStore(QString profile) : profile_(std::move(profile)) {}
    Result<AlertCatalog> load() const;
    Result<AlertCatalog> save(const AlertCatalog& desired, const AlertCatalog& expected) const;
private:
    const QString profile_;
};
/** Owns cache-only evaluation, per-session deduplication, finite holds and local sound playback. */
class Alerts final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList rules READ rules NOTIFY rulesChanged)
    Q_PROPERTY(QVariantList matches READ matches NOTIFY presentationChanged)
    Q_PROPERTY(QString error READ error NOTIFY rulesChanged)
    Q_PROPERTY(QString evaluationError READ evaluationError NOTIFY presentationChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY rulesChanged)
    Q_PROPERTY(bool muted READ muted NOTIFY rulesChanged)
    Q_PROPERTY(bool reducedMotion READ reducedMotion NOTIFY rulesChanged)
    Q_PROPERTY(int revision READ revision NOTIFY presentationChanged)
    Q_PROPERTY(QStringList fields READ fields CONSTANT)
    Q_PROPERTY(QVariantList sounds READ sounds CONSTANT)
    Q_PROPERTY(QAbstractItemModel* tableModel READ tableModel CONSTANT)
    Q_PROPERTY(int tableSortColumn READ tableSortColumn NOTIFY tableSortChanged)
    Q_PROPERTY(QString tableSortDirection READ tableSortDirection NOTIFY tableSortChanged)
    Q_PROPERTY(bool zoomPreviewBusy READ zoomPreviewBusy NOTIFY zoomPreviewChanged)
    Q_PROPERTY(QString zoomPreviewError READ zoomPreviewError NOTIFY zoomPreviewChanged)
public:
    explicit Alerts(QString profile, ResourceClient* client, QObject* parent,
                    std::function<QDateTime()> now = QDateTime::currentDateTimeUtc);
    QVariantList rules() const;
    QVariantList matches() const;
    QString error() const { return error_; }
    QString evaluationError() const;
    bool busy() const { return busy_; }
    bool muted() const { return catalog_.muted; }
    bool reducedMotion() const { return catalog_.reducedMotion; }
    int revision() const { return revision_; }
    QStringList fields() const;
    QVariantList sounds() const;
    QAbstractItemModel* tableModel() { return &table_; }
    const QAbstractItemModel* tableModel() const { return &table_; }
    int tableSortColumn() const { return table_.sortColumn(); }
    QString tableSortDirection() const;
    Q_INVOKABLE bool sortTable(int column);
    Q_INVOKABLE bool copyCell(int row, int column);
    Q_INVOKABLE bool copyIdentity(const QString& identity, int column);
    Q_INVOKABLE bool saveRule(const QVariantMap& draft);
    Q_INVOKABLE bool duplicateRule(const QString& id);
    Q_INVOKABLE bool deleteRule(const QString& id);
    Q_INVOKABLE bool setPreferences(bool muted, bool reducedMotion);
    Q_INVOKABLE bool reload();
    Q_INVOKABLE bool previewSound(const QString& id);
    bool zoomPreviewBusy() const { return zoomPreviewBusy_; }
    QString zoomPreviewError() const { return zoomPreviewError_; }
    /** Evaluate one draft against the current cache, focusing an eligible visible path without saving or replaying effects. */
    bool previewZoom(const QVariantMap& draft, const QStringList& visiblePaths);
    /** Copies explicitly selected local text; rejects oversized clipboard input. */
    Q_INVOKABLE bool copyText(const QString& text);
    Q_INVOKABLE QVariantMap effect(const QString& path) const;
    bool showSession(const QString& session);
    bool closeSession(const QString& session);
signals:
    void rulesChanged();
    void presentationChanged();
    void tableSortChanged();
    void focusRequested(const QString& session, const QString& path, int percent);
    void soundRequested(const QString& session, const QString& rule, const QString& sound);
    void zoomPreviewChanged();
    void zoomPreviewReady(const QString& session, const QString& path, int percent);
private:
    struct State final {
        QMap<QString, QJsonObject> previous;
        QMap<QString, QDateTime> changedAt;
        QMap<QString, QJsonObject> triggered;
        QMap<QString, QDateTime> colorUntil, animationUntil;
        QVariantList matches;
        QString evaluationError;
        QMap<QString, QVariantMap> effects;
        quint64 generation = 0;
        bool initialized = false;
        bool loading = true;
    };
    const QString profile_;
    ResourceClient* const client_;
    const std::function<QDateTime()> now_;
    AlertCatalog catalog_;
    QStandardItemModel ruleRows_{this};
    QSortFilterProxyModel table_{this};
    QMap<QString, State> states_;
    QSet<QString> closed_, pending_;
    QString shown_, error_;
    bool busy_ = true, ready_ = false, evaluating_ = false;
    int revision_ = 0;
    bool zoomPreviewBusy_ = false;
    QString zoomPreviewError_;
    quint64 zoomPreviewGeneration_ = 0;
    QTimer expiry_;
    std::unique_ptr<QAudioOutput> output_;
    std::unique_ptr<QMediaPlayer> player_;
    bool persist(AlertCatalog desired);
    bool queue(const QString& session);
    bool dispatch();
    bool play(const QString& id);
    bool publishTable();
    bool publishTableMatches();
};
} // namespace podlord
