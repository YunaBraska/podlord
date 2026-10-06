#include "credential_process.h"
#include <QJsonDocument>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <algorithm>

namespace podlord {
namespace {
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
} // namespace
CredentialProcess::CredentialProcess(QObject* parent) : QObject(parent) {}
CredentialProcess::~CredentialProcess() {
    for (const auto& job : jobs_) { disconnect(job.process, nullptr, this, nullptr); job.process->kill(); job.process->waitForFinished(1000); }
}
bool CredentialProcess::running(const QString& credential) const { return jobs_.contains(credential); }
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
bool CredentialProcess::cancel(const QString& credential) {
    auto job = jobs_.find(credential);
    if (job == jobs_.end()) return false;
    job->failure = "Authentication cancelled. Confirm another attempt to retry.";
    job->process->kill(); return true;
}
bool CredentialProcess::start(ClusterConnection connection, const QString& session) {
    if (!connection.exec) return false;
    const auto id = connection.credentialId;
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
}
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
} // namespace podlord
