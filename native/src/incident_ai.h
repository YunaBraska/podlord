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
#include <optional>
#include "chatgpt_client.h"
#include "api_ai_client.h"

namespace podlord {
/** Cached Kubernetes analysis through one selected provider. */
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
            QUrl("https://api.anthropic.com"), QUrl("https://openrouter.ai")}, CredentialStore* credentials = nullptr);
    bool supported() const;
    bool enabled(const QString& context) const;
    bool setEnabled(const QString& context, bool enabled);
    QStringList models() const;
    QVariantList modelChoices() const;
    QVariantList allModelChoices() const;
    QVariantList accounts() const;
    QString model() const { return provider_ == "chatgpt" ? chatgptModel_ : provider_ == "claude" ? claudeModel_
        : provider_ == "openrouter" ? openrouterModel_ : model_; }
    bool ready() const { return modelReady_; }
    bool setModel(const QString& model);
    bool selectModel(const QString& provider, const QString& model);
    QStringList reasoningOptions() const { return provider_ == "ollama" ? reasoningOptions_ : QStringList{}; }
    QString reasoning() const { return provider_ == "ollama" ? reasoning_ : QString{}; }
    bool setReasoning(const QString& reasoning);
    QString status(const QString& context = {}) const {
        return !status_.isEmpty() ? status_ : !contextStatus_.value(context).isEmpty() ? contextStatus_.value(context) :
            provider_ == "chatgpt" ? (!chatgpt_.status().isEmpty() ? chatgpt_.status()
                : !chatgpt_.connected() ? "Connect a ChatGPT account with plan permission first."
                : chatgptModel_.isEmpty() ? "Choose a ChatGPT model in AI settings." : QString{})
            : provider_ == "claude" ? (!claude_.status().isEmpty() ? claude_.status()
                : !claude_.configured() ? "Enter a Claude API key in AI settings."
                : claudeModel_.isEmpty() ? "Choose a Claude model in AI settings." : QString{})
            : provider_ == "openrouter" ? (!openrouter_.status().isEmpty() ? openrouter_.status()
                : !openrouter_.configured() ? "Enter an OpenRouter API key in AI settings."
                : openrouterModel_.isEmpty() ? "Choose an OpenRouter model in AI settings." : QString{}) : QString{};
    }
    QString provider() const { return provider_; }
    bool setProvider(const QString& provider);
    QString chatgptAccount() const { return chatgpt_.activeAccount(); }
    bool chatgptConnected() const { return chatgpt_.connected(); }
    QString chatgptClientId() const { return chatgpt_.activeClientId(); }
    bool chatgptSignIn(const QString& clientId = {}, const QString& language = "en") { return supported() && chatgpt_.signIn(clientId, language); }
    bool openRouterSignIn(const QString& language = "en") { return supported() && openrouter_.signIn(language); }
    void chatgptSignOut();
    bool setClaudeKey(const QString& key);
    void clearClaudeKey();
    void signOutOpenRouter();
    void refreshUsage() { emit changed(); }
    void refreshModels();
    void ensureModelsFresh();
    void observe(const QString& context, const QJsonArray& rows, const QString& language);
    void prepareManual(const QString& context, const QString& path);
    void observeDiagnostic(const QString& error, const QString& language);
    QVariantMap diagnosticResult() const;
    bool refresh(const QString& context, const QString& path);
    void retainContexts(const QSet<QString>& contexts);
    QVariantMap result(const QString& context, const QString& path) const;
signals:
    void changed();
private:
    struct Incident final {
        QString key, context, code, kind, path, language, provider, model, reasoning, thinkSignature;
        QStringList related;
        bool manual = false;
        QJsonArray evidence;
    };
    struct Record final { QJsonObject answer; QDateTime created; };
    bool save();
    qsizetype evidenceBudgetBytes(const QString& language, const QString& kind, const QString& code, const QStringList& related) const;
    void dispatch();
    void complete(Incident incident, const QString& answer, const QString& error);
    void providerChanged();
    void refreshReasoning();
    void recordRequest(const QString& provider, const QString& identity);
    void stopObserving(const QString& context);
    std::optional<Incident> manualIncident(const QString& context, const QString& path) const;
    const QString profile_;
    const QUrl endpoint_;
    ChatGptClient chatgpt_;
    ApiAiClient claude_, openrouter_;
    QNetworkAccessManager network_;
    QTimer dispatchTimer_;
    QMap<QString, QSet<QString>> enabledByProvider_;
    QStringList models_;
    qint64 ollamaContextTokens_ = 0;
    QStringList reasoningOptions_{"auto"};
    QString provider_ = "ollama", model_, chatgptModel_, claudeModel_, openrouterModel_, reasoning_ = "auto", status_;
    QMap<QString, QString> contextStatus_;
    QJsonValue thinkValue_{QJsonValue::Undefined};
    bool modelReady_ = false;
    QMap<QString, Record> records_;
    QMap<QString, QList<QDateTime>> requests_;
    QMap<QString, Incident> visible_;
    QMap<QString, Incident> manualVisible_;
    QMap<QString, QSet<QString>> activeKeys_;
    QMap<QString, QPair<QJsonArray, QString>> snapshots_;
    std::optional<Incident> diagnostic_;
    QMap<QString, QDateTime> retryAfter_;
    QQueue<Incident> queue_;
    QSet<QString> queued_;
    QMap<QString, QList<QDateTime>> recent_;
    QString inFlight_;
    QString inFlightContext_;
    QNetworkReply* activeReply_ = nullptr;
    QDateTime lastRequest_;
    QDateTime lastModelCatalogRefresh_;
    QMap<QString, QDateTime> serviceRetryAfterByProvider_;
    bool modelRefreshRunning_ = false;
    bool writable_ = true;
    QByteArray savedBytes_;
};
} // namespace podlord
