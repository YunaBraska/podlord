#pragma once
#include <QDateTime>
#include <QJsonArray>
#include <QJsonObject>
#include <QMap>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QObject>
#include <QQueue>
#include <QSet>
#include <QTimer>
#include <QVariantMap>
#include <QUrl>

namespace podlord {
/** Local, opt-in analysis of recognized cached Kubernetes problem codes. */
class IncidentAi final : public QObject {
    Q_OBJECT
public:
    explicit IncidentAi(QString profile, QUrl endpoint = QUrl("http://127.0.0.1:11434"), QObject* parent = nullptr);
    bool supported() const;
    bool enabled(const QString& context) const;
    bool setEnabled(const QString& context, bool enabled);
    QStringList models() const { return models_; }
    QString model() const { return model_; }
    bool setModel(const QString& model);
    QStringList reasoningOptions() const { return reasoningOptions_; }
    QString reasoning() const { return reasoning_; }
    bool setReasoning(const QString& reasoning);
    QString status(const QString& context = {}) const { return !status_.isEmpty() ? status_ : contextStatus_.value(context); }
    void refreshModels();
    void observe(const QString& context, const QJsonArray& rows, const QString& language);
    void retainContexts(const QSet<QString>& contexts);
    QVariantMap result(const QString& context, const QString& path) const;
signals:
    void changed();
private:
    struct Incident final {
        QString key, context, code, kind, name, nameSpace, path, language, model, reasoning, thinkSignature, documentation;
    };
    struct Record final { QJsonObject answer; QDateTime created; };
    bool save();
    void dispatch();
    void refreshReasoning();
    void stopObserving(const QString& context);
    const QString profile_;
    const QUrl endpoint_;
    QNetworkAccessManager network_;
    QTimer dispatchTimer_;
    QSet<QString> enabled_;
    QStringList models_;
    QStringList reasoningOptions_{"auto"};
    QString model_, reasoning_ = "auto", status_;
    QMap<QString, QString> contextStatus_;
    QJsonValue thinkValue_{QJsonValue::Undefined};
    bool modelReady_ = false;
    QMap<QString, Record> records_;
    QMap<QString, Incident> visible_;
    QMap<QString, QSet<QString>> activeKeys_;
    QMap<QString, QPair<QJsonArray, QString>> snapshots_;
    QMap<QString, QDateTime> retryAfter_;
    QQueue<Incident> queue_;
    QSet<QString> queued_;
    QMap<QString, QList<QDateTime>> recent_;
    QString inFlight_;
    QString inFlightContext_;
    QNetworkReply* activeReply_ = nullptr;
    QDateTime lastRequest_;
    QDateTime serviceRetryAfter_;
    bool modelRefreshRunning_ = false;
    bool writable_ = true;
    QByteArray savedBytes_;
};
} // namespace podlord
