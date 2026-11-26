#pragma once
#include "credential_store.h"
#include <QDateTime>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QPointer>
#include <QTcpServer>
#include <QTimer>
#include <QVariantList>
#include <QLockFile>
#include <functional>
#include <memory>

namespace podlord {
/** ChatGPT-plan OAuth session and account-specific Responses transport. */
class ChatGptClient final : public QObject {
    Q_OBJECT
public:
    explicit ChatGptClient(QString profile, QUrl authBase = QUrl("https://auth.openai.com"),
        QUrl apiBase = QUrl("https://api.openai.com"), QObject* parent = nullptr, CredentialStore* credentials = nullptr);
    QString activeAccount() const;
    QString activeSubject() const;
    QString activeClientId() const { return activeClientId_; }
    QVariantList models() const { return models_; }
    QString status() const { return status_; }
    bool connected() const;
    bool signIn(const QString& clientId = {}, const QString& language = "en");
    void signOut();
    void refreshModels();
    void analyze(const QString& model, const QString& instructions, const QString& input,
        std::function<void(QString answer, QString error)> done);
    void cancelAnalysis();
signals:
    void changed();
    void analysisSent(const QString& subject);
private:
    void restore(const CredentialStore::ReadResult& loaded);
    struct Account {
        QString clientId, subject, email, idToken, accessToken, refreshToken, scopes;
        QDateTime expiresAt;
    };
    bool save();
    void finishLogin(const QString& error);
    void exchangeCode(const QString& code, const QString& issuedClientId);
    void acceptTokens(const QJsonObject& tokens, const QString& issuedClientId);
    void ensureAccess(std::function<void(bool)> done);
    Account* active();
    const Account* active() const;
    const QString profile_;
    const QUrl authBase_, apiBase_;
    QNetworkAccessManager network_;
    QTcpServer callbackServer_;
    QTimer loginTimer_;
    QList<Account> accounts_;
    QVariantList models_;
    QString activeClientId_, hostId_, status_;
    QString pendingClientId_, issuedClientId_, retryClientId_, state_, nonce_, verifier_, redirectUri_, loginLanguage_ = "en";
    QPointer<QNetworkReply> loginReply_, refreshReply_, modelsReply_, analysisReply_, revokeReply_;
    std::unique_ptr<QLockFile> refreshLock_;
    QList<std::function<void(bool)>> accessWaiters_;
    std::function<void(QString, QString)> analysisDone_;
    quint64 analysisGeneration_ = 0;
    QByteArray savedBytes_;
    bool writable_ = true;
    ProfileCredentialStore systemCredentials_;
    CredentialStore* credentials_;
};
} // namespace podlord
