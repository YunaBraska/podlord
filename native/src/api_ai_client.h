#pragma once
#include <QNetworkAccessManager>
#include <QPointer>
#include <QVariantList>
#include <functional>

namespace podlord {
/** An API-key-backed Claude or OpenRouter transport with its own model catalog. */
class ApiAiClient final : public QObject {
    Q_OBJECT
public:
    enum class Provider { Claude, OpenRouter };
    explicit ApiAiClient(QString profile, Provider provider, QUrl base = {}, QObject* parent = nullptr);
    bool configured() const { return writable_ && !key_.isEmpty(); }
    QString status() const { return status_; }
    QString identity() const;
    QVariantList models() const { return models_; }
    bool setKey(const QString& key);
    void clearKey();
    void refreshModels();
    void analyze(const QString& model, const QString& instructions, const QString& input,
        std::function<void(QString answer, QString error)> done);
    void cancelAnalysis();
signals:
    void changed();
private:
    bool save();
    void loadModels(const QString& after, QVariantList collected);
    const QString profile_;
    const Provider provider_;
    const QUrl base_;
    QNetworkAccessManager network_;
    QString key_, status_;
    QVariantList models_;
    QPointer<QNetworkReply> modelsReply_, analysisReply_;
    QByteArray savedBytes_;
    bool writable_ = true;
};
} // namespace podlord
