#include "credential_process.h"
#include "native_oci.h"
#include <QJsonDocument>
#include <QPointer>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <algorithm>

namespace podlord {
namespace {
#if !defined(Q_OS_IOS) && !defined(Q_OS_ANDROID)
Result<ClusterConnection> credentials(ClusterConnection connection, const QByteArray& bytes) {
    const auto rejected = [] { return Failure{StoreError::InvalidData, "Credential command returned an invalid or expired ExecCredential; no request was sent."}; };
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(bytes, &error);
    if (QString::fromUtf8(bytes).toUtf8() != bytes || error.error != QJsonParseError::NoError || !document.isObject()) return rejected();
    const auto root = document.object();
    if (root["kind"] != "ExecCredential" || root["apiVersion"] != connection.exec->apiVersion || !root["status"].isObject()) return rejected();
    const auto status = root["status"].toObject();
    for (const auto& name : {"token", "clientCertificateData", "clientKeyData", "expirationTimestamp"})
        if (status.contains(name) && !status[name].isString()) return rejected();
    const auto token = status["token"].toString();
    const auto certificate = status["clientCertificateData"].toString().toUtf8();
    const auto key = status["clientKeyData"].toString().toUtf8();
    if (token.isEmpty() && certificate.isEmpty() && key.isEmpty()) return rejected();
    if (std::any_of(token.cbegin(), token.cend(), [](QChar c) { return c.isSpace() || c.category() == QChar::Other_Control; })) return rejected();
    if (!certificate.isEmpty() || !key.isEmpty()) {
        const auto installed = clientCertificate(connection.tls, certificate, key);
        if (const auto* failure = std::get_if<Failure>(&installed)) return *failure;
        connection.tls = std::get<QSslConfiguration>(installed);
    }
    if (status.contains("expirationTimestamp")) {
        static const QRegularExpression rfc3339("^\\d{4}-\\d{2}-\\d{2}T\\d{2}:\\d{2}:\\d{2}(?:\\.\\d{1,9})?(?:Z|[+-]\\d{2}:\\d{2})$");
        const auto text = status["expirationTimestamp"].toString();
        connection.expiresAt = QDateTime::fromString(text, Qt::ISODateWithMs).toUTC();
        if (!rfc3339.match(text).hasMatch() || !connection.expiresAt.isValid() || connection.expiresAt <= QDateTime::currentDateTimeUtc()) return rejected();
    }
    if (!token.isEmpty()) connection.authorization = "Bearer " + token.toUtf8();
    connection.credentialReady = true;
    return connection;
}
#endif
} // namespace
CredentialProcess::CredentialProcess(QObject* parent) : QObject(parent) {}
CredentialProcess::~CredentialProcess() {
#if !defined(Q_OS_IOS) && !defined(Q_OS_ANDROID)
    for (const auto& job : jobs_) { disconnect(job.process, nullptr, this, nullptr); job.process->kill(); job.process->waitForFinished(1000); }
#endif
}
bool CredentialProcess::running(const QString& credential) const { return jobs_.contains(credential) || nativeJobs_.contains(credential) || gkeJobs_.contains(credential); }
QString CredentialProcess::instruction(const QString& credential) const {
    const auto* job = nativeJobs_.value(credential, nullptr);
    return job ? job->instruction() : QString{};
}
std::optional<ClusterConnection> CredentialProcess::cached(const QString& credential) const {
    const auto found = results_.constFind(credential);
    if (found == results_.cend()) return {};
    const auto* connection = std::get_if<ClusterConnection>(&found.value());
    if (!connection || (connection->expiresAt.isValid() && connection->expiresAt <= QDateTime::currentDateTimeUtc())) return {};
    return *connection;
}
Result<ClusterConnection> CredentialProcess::result(const QString& credential) const {
    const auto found = results_.constFind(credential);
    return found == results_.cend() ? Result<ClusterConnection>{Failure{StoreError::NotFound, "No confirmed credential result."}} : found.value();
}
bool CredentialProcess::invalidate(const QString& credential) { return results_.remove(credential) > 0; }
bool CredentialProcess::revoke(const QString& credential) {
    bool removed = results_.remove(credential) > 0;
    const auto discard = [this, &removed](auto* login) {
        if (!login) return;
        removed = true;
        disconnect(login, nullptr, this, nullptr);
        login->cancel();
        login->deleteLater();
    };
    discard(nativeJobs_.take(credential));
    discard(gkeJobs_.take(credential));
#if !defined(Q_OS_IOS) && !defined(Q_OS_ANDROID)
    auto job = jobs_.find(credential);
    if (job != jobs_.end()) {
        auto* process = job->process;
        jobs_.erase(job); removed = true;
        disconnect(process, nullptr, this, nullptr);
        connect(process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), process, &QObject::deleteLater);
        connect(process, &QProcess::errorOccurred, process, &QObject::deleteLater);
        process->kill();
    }
#endif
    emit revoked(credential);
    if (removed) emit changed();
    return removed;
}
bool CredentialProcess::cancel(const QString& credential) {
    if (auto* native = nativeJobs_.value(credential, nullptr)) { native->cancel(); return true; }
    if (auto* gke = gkeJobs_.value(credential, nullptr)) { gke->cancel(); return true; }
#if defined(Q_OS_IOS) || defined(Q_OS_ANDROID)
    Q_UNUSED(credential);
    return false;
#else
    auto job = jobs_.find(credential);
    if (job == jobs_.end()) return false;
    job->failure = "Authentication cancelled. Confirm another attempt to retry.";
    job->process->kill(); return true;
#endif
}
bool CredentialProcess::start(ClusterConnection connection, const QString& session) {
    if (!connection.exec) return false;
    const auto id = connection.credentialId;
    if (connection.exec->gke) {
        if (gkeJobs_.contains(id)) return true;
        results_.remove(id);
        auto* login = new NativeGkeLogin(std::move(connection), this);
        gkeJobs_.insert(id, login);
        connect(login, &NativeGkeLogin::completed, this, [this, id, session, guard = QPointer<NativeGkeLogin>(login)](Result<ClusterConnection> result) {
            auto* login = guard.data();
            if (!login || gkeJobs_.value(id) != login) return;
            gkeJobs_.remove(id); results_.insert(id, std::move(result)); login->deleteLater();
            emit changed(); emit completed(id, session);
        });
        login->start(); emit changed(); return true;
    }
    if (connection.exec->eks) {
        results_.insert(id, nativeEksToken(std::move(connection)));
        emit changed(); emit completed(id, session);
        return true;
    }
    if (connection.exec->oci) {
        results_.insert(id, nativeOciToken(std::move(connection)));
        emit changed(); emit completed(id, session);
        return true;
    }
    if (connection.exec->oidc) {
        if (nativeJobs_.contains(id)) return true;
        results_.remove(id);
#if defined(Q_OS_IOS) || defined(Q_OS_ANDROID)
        const auto flow = connection.exec->oidc->deviceCodeGrant ? NativeOidcLogin::Flow::Device : NativeOidcLogin::Flow::Browser;
        auto* login = new NativeOidcLogin(std::move(connection), flow, this);
#else
        auto* login = new NativeOidcLogin(std::move(connection), NativeOidcLogin::Flow::Device, this);
#endif
        nativeJobs_.insert(id, login);
        connect(login, &NativeOidcLogin::instructionChanged, this, &CredentialProcess::changed);
        connect(login, &NativeOidcLogin::completed, this, [this, id, session, guard = QPointer<NativeOidcLogin>(login)](Result<ClusterConnection> result) {
            auto* login = guard.data();
            if (!login || nativeJobs_.value(id) != login) return;
            nativeJobs_.remove(id); results_.insert(id, std::move(result)); login->deleteLater();
            emit changed(); emit completed(id, session);
        });
        login->start(); emit changed(); return true;
    }
#if defined(Q_OS_IOS) || defined(Q_OS_ANDROID) || defined(PODLORD_MAC_APP_STORE)
    results_.insert(id, Failure{StoreError::UnsupportedVersion, "External credential commands are unavailable in this build. Use native sign-in or explicitly import a personal credential."});
    emit changed(); emit completed(id, session);
    return false;
#else
    if (jobs_.contains(id)) return true;
    results_.remove(id);
    auto* process = new QProcess(this);
    jobs_.insert(id, {process, {}, {}});
    auto environment = QProcessEnvironment::systemEnvironment();
    for (auto it = connection.exec->environment.cbegin(); it != connection.exec->environment.cend(); ++it) environment.insert(it.key(), it.value());
    environment.insert("KUBERNETES_EXEC_INFO", QString::fromUtf8(QJsonDocument(connection.exec->info).toJson(QJsonDocument::Compact)));
    process->setProcessEnvironment(environment);
    process->setWorkingDirectory(connection.exec->workingDirectory);
    process->setProcessChannelMode(QProcess::SeparateChannels);
    process->setStandardInputFile(QProcess::nullDevice());
    connect(process, &QProcess::readyReadStandardOutput, this, [this, id, process] {
        auto job = jobs_.find(id);
        if (job == jobs_.end() || job->process != process) return;
        job->output.append(process->readAllStandardOutput());
        if (job->output.size() > 1024 * 1024) { job->output.clear(); job->failure = "Credential command output exceeded the 1 MiB safety limit."; process->kill(); }
    });
    connect(process, &QProcess::readyReadStandardError, this, [process] { process->readAllStandardError(); });
    connect(process, &QProcess::errorOccurred, this, [this, connection, session, process](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) finish(connection, session, process);
    });
    connect(process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this, [this, connection, session, process] { finish(connection, session, process); });
    process->start(connection.exec->program, connection.exec->arguments);
    emit changed(); return true;
#endif
}
#if !defined(Q_OS_IOS) && !defined(Q_OS_ANDROID)
void CredentialProcess::finish(const ClusterConnection& connection, const QString& session, QProcess* process) {
    auto job = jobs_.find(connection.credentialId);
    if (job == jobs_.end() || job->process != process) return;
    const auto output = job->output + process->readAllStandardOutput();
    Result<ClusterConnection> result = Failure{StoreError::ReadFailed, "Credential command could not complete. Check its installation or login, then confirm another attempt."};
    if (!job->failure.isEmpty()) result = Failure{StoreError::ReadFailed, job->failure};
    else if (output.size() > 1024 * 1024) result = Failure{StoreError::InvalidData, "Credential command output exceeded the 1 MiB safety limit."};
    else if (process->error() != QProcess::FailedToStart && process->exitStatus() == QProcess::NormalExit && process->exitCode() == 0)
        result = credentials(connection, output);
    results_.insert(connection.credentialId, std::move(result));
    jobs_.erase(job); process->deleteLater();
    emit changed(); emit completed(connection.credentialId, session);
}
#endif
} // namespace podlord
