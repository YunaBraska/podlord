#pragma once
#include "credential_store.h"
#include <QNetworkAccessManager>
#include <QPointer>
#include <QTcpServer>
#include <QTimer>
#include <QVariantList>
#include <functional>

namespace podlord {
/** An API-key-backed Claude or OpenRouter transport with its own model catalog. */
class ApiAiClient final : public QObject {
    Q_OBJECT
public:
    enum class Provider { Claude, OpenRouter };
    explicit ApiAiClient(QString profile, Provider provider, QUrl base = {}, QObject* parent = nullptr,
        CredentialStore* credentials = nullptr);
    bool configured() const { return writable_ && !key_.isEmpty(); }
    QString status() const { return status_; }
    QString identity() const;
    QVariantList models() const { return models_; }
    bool setKey(const QString& key);
    bool signIn(const QString& language = "en");
    void clearKey();
    void refreshModels();
    void analyze(const QString& model, const QString& instructions, const QString& input,
        std::function<void(QString answer, QString error)> done);
    void cancelAnalysis();
signals:
    void changed();
    void analysisSent(const QString& identity);
private:
    void restore(const CredentialStore::ReadResult& loaded);
    bool save();
    void loadModels(const QString& after, QVariantList collected);
    void finishLogin(const QString& status);
    const QString profile_;
    const Provider provider_;
    const QUrl base_;
    QNetworkAccessManager network_;
    QTcpServer callbackServer_;
    QTimer loginTimer_;
    QString key_, status_;
    QString loginState_, loginVerifier_;
    QString loginLanguage_ = "en";
    QVariantList models_;
    QPointer<QNetworkReply> modelsReply_, analysisReply_, loginReply_;
    QByteArray savedBytes_;
    bool writable_ = true;
    ProfileCredentialStore systemCredentials_;
    CredentialStore* credentials_;
};
} // namespace podlord
