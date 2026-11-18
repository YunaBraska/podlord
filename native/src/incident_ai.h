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
#include "chatgpt_client.h"
#include "api_ai_client.h"

namespace podlord {
/** Local, opt-in analysis of recognized cached Kubernetes problem codes. */
class IncidentAi final : public QObject {
    Q_OBJECT
public:
    struct RemoteEndpoints {
        QUrl chatgptAuth;
        QUrl chatgptApi;
        QUrl claude;
        QUrl openrouter;
    };
    explicit IncidentAi(QString profile, QUrl endpoint = QUrl("http://127.0.0.1:11434"), QObject* parent = nullptr,
        RemoteEndpoints remote = {QUrl("https://auth.openai.com"), QUrl("https://api.openai.com"),
            QUrl("https://api.anthropic.com"), QUrl("https://openrouter.ai")});
    bool supported() const;
    bool enabled(const QString& context) const;
    bool setEnabled(const QString& context, bool enabled);
    QStringList models() const;
    QVariantList modelChoices() const;
    QString model() const { return provider_ == "chatgpt" ? chatgptModel_ : provider_ == "claude" ? claudeModel_
        : provider_ == "openrouter" ? openrouterModel_ : model_; }
    bool setModel(const QString& model);
    QStringList reasoningOptions() const { return provider_ == "ollama" ? reasoningOptions_ : QStringList{}; }
    QString reasoning() const { return provider_ == "ollama" ? reasoning_ : QString{}; }
    bool setReasoning(const QString& reasoning);
    QString status(const QString& context = {}) const {
        return !status_.isEmpty() ? status_ : !contextStatus_.value(context).isEmpty() ? contextStatus_.value(context) :
            provider_ == "chatgpt" ? (!chatgpt_.status().isEmpty() ? chatgpt_.status()
                : chatgptModel_.isEmpty() ? "Choose a ChatGPT model in AI settings." : QString{})
            : provider_ == "claude" ? (!claude_.configured() ? "Enter a Claude API key in AI settings."
                : !claude_.status().isEmpty() ? claude_.status()
                : claudeModel_.isEmpty() ? "Choose a Claude model in AI settings." : QString{})
            : provider_ == "openrouter" ? (!openrouter_.configured() ? "Enter an OpenRouter API key in AI settings."
                : !openrouter_.status().isEmpty() ? openrouter_.status()
                : openrouterModel_.isEmpty() ? "Choose an OpenRouter model in AI settings." : QString{}) : QString{};
    }
    QString provider() const { return provider_; }
    bool setProvider(const QString& provider);
    QVariantList chatgptAccounts() const { return chatgpt_.accounts(); }
    QString chatgptAccount() const { return chatgpt_.activeAccount(); }
    QString chatgptClientId() const { return chatgpt_.activeClientId(); }
    bool chatgptSignIn(const QString& clientId = {}) { return supported() && chatgpt_.signIn(clientId); }
    bool chatgptSelectAccount(const QString& clientId) { return supported() && chatgpt_.selectAccount(clientId); }
    void chatgptSignOut() { if (supported()) chatgpt_.signOut(); }
    bool setApiKey(const QString& key);
    void clearApiKey();
    bool apiKeyConfigured() const;
    void refreshModels();
    void observe(const QString& context, const QJsonArray& rows, const QString& language);
    bool refresh(const QString& context, const QString& path);
    void retainContexts(const QSet<QString>& contexts);
    QVariantMap result(const QString& context, const QString& path) const;
signals:
    void changed();
private:
    struct Incident final {
        QString key, context, code, kind, name, nameSpace, path, language, provider, model, reasoning, thinkSignature, documentation;
        QStringList related;
        bool manual = false;
    };
    struct Record final { QJsonObject answer; QDateTime created; };
    bool save();
    void dispatch();
    void complete(Incident incident, const QString& answer, const QString& error);
    void providerChanged();
    void refreshReasoning();
    void stopObserving(const QString& context);
    const QString profile_;
    const QUrl endpoint_;
    ChatGptClient chatgpt_;
    ApiAiClient claude_, openrouter_;
    QNetworkAccessManager network_;
    QTimer dispatchTimer_;
    QMap<QString, QSet<QString>> enabledByProvider_;
    QStringList models_;
    QStringList reasoningOptions_{"auto"};
    QString provider_ = "ollama", model_, chatgptModel_, claudeModel_, openrouterModel_, reasoning_ = "auto", status_;
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
    QMap<QString, QDateTime> serviceRetryAfterByProvider_;
    bool modelRefreshRunning_ = false;
    bool writable_ = true;
    QByteArray savedBytes_;
};
} // namespace podlord
