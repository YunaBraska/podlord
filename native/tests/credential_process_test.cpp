#include "credential_process.h"
#include <QCoreApplication>
#include <QDateTime>
#include <QJsonDocument>
#include <QTest>
#include <QThread>
#include <cstdio>

using namespace podlord;

namespace {
ClusterConnection connection() {
    ClusterConnection value;
    value.credentialId = "credential-test";
    value.exec.emplace();
    value.exec->program = QCoreApplication::applicationFilePath();
    value.exec->apiVersion = "client.authentication.k8s.io/v1";
    return value;
}

QByteArray output(const QString& scenario) {
    QJsonObject status{{"token", "credential-token"}};
    QJsonObject root{{"kind", "ExecCredential"}, {"apiVersion", "client.authentication.k8s.io/v1"}, {"status", status}};
    if (scenario == "wrong_kind") root.insert("kind", "Other");
    else if (scenario == "wrong_version") root.insert("apiVersion", "client.authentication.k8s.io/v1beta1");
    else if (scenario == "missing_status") root.remove("status");
    else if (scenario == "token_number") { status.insert("token", 3); root.insert("status", status); }
    else if (scenario == "empty_status") { status.remove("token"); root.insert("status", status); }
    else if (scenario == "token_space") { status.insert("token", "bad token"); root.insert("status", status); }
    else if (scenario == "token_control") { status.insert("token", QString("bad") + QChar(1)); root.insert("status", status); }
    else if (scenario == "bad_certificate") {
        status.remove("token"); status.insert("clientCertificateData", "invalid"); status.insert("clientKeyData", "invalid");
        root.insert("status", status);
    } else if (scenario == "expired" || scenario == "future" || scenario == "near_expiry"
        || scenario == "bad_expiry" || scenario == "invalid_date") {
        const auto expiry = scenario == "bad_expiry" ? QString("tomorrow")
            : scenario == "invalid_date" ? QString("2026-02-30T12:00:00Z")
            : QDateTime::currentDateTimeUtc().addSecs(scenario == "near_expiry" ? 3 : scenario == "future" ? 3600 : -3600)
                .toString(Qt::ISODateWithMs);
        status.insert("expirationTimestamp", expiry); root.insert("status", status);
    }
    if (scenario == "invalid_json") return "{";
    if (scenario == "array") return "[]";
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

bool scenario(const QString& name) {
    CredentialProcess process;
    const auto id = QString("credential-test");
    if (name == "before_start")
        return !process.running(id) && !process.cached(id) && !process.invalidate(id) && !process.cancel(id) && !process.revoke(id)
            && process.instruction(id).isEmpty() && std::holds_alternative<Failure>(process.result(id));
    auto value = connection();
    if (name == "missing_exec") {
        value.exec.reset();
        return !process.start(value, "session") && std::holds_alternative<Failure>(process.result(id));
    }
    if (name == "gke_unavailable" || name == "gke_service_missing") {
        value.exec->gke.emplace();
        value.exec->gke->serviceAccountOnly = name == "gke_service_missing";
    } else if (name == "eks" || name == "revoke_ready") value.exec->eks = EksLogin{"podlord-test", "eu-central-1", "test-access", "test-secret", {}};
    else if (name == "oci_bad_key") value.exec->oci = OciLogin{"ocid1.cluster.test", "eu-central-1", "user", "tenancy", "fingerprint", "invalid"};
    else if (name == "oidc_invalid" || name == "oidc_cancel" || name == "revoke_oidc") {
        value.exec->oidc.emplace();
        value.exec->oidc->deviceCodeGrant = true;
        value.exec->oidc->useAccessToken = true;
        if (name != "oidc_invalid") value.exec->oidc->issuer = QUrl("http://127.0.0.1:1");
    } else if (name == "missing_program") value.exec->program = "/podlord-test/no-such-executable";
    else if (name == "cancel" || name == "repeat" || name == "revoke_running") value.exec->arguments = {"sleep"};
    else value.exec->arguments = {name == "nonzero" ? "emit-error" : name == "oversized" ? "emit-oversized"
        : name == "invalid_utf8" ? "emit-binary" : name == "stderr" ? "emit-stderr" : "emit", QString::fromUtf8(output(name))};

    int completions = 0;
    QObject::connect(&process, &CredentialProcess::completed, &process, [&](const QString& credential, const QString& session) {
        if (credential == id && session == "session") ++completions;
    });
    if (!process.start(value, "session")) return false;
    if (name == "revoke_ready")
        return process.cached(id).has_value() && process.revoke(id) && !process.cached(id)
            && std::holds_alternative<Failure>(process.result(id)) && !process.revoke(id);
    if (name == "revoke_running" || name == "revoke_oidc") {
        if (!process.running(id) || !process.revoke(id) || process.running(id) || process.revoke(id)) return false;
        QTest::qWait(100);
        const auto removed = process.result(id);
        if (completions != 0 || !std::holds_alternative<Failure>(removed) || std::get<Failure>(removed).code != StoreError::NotFound) return false;
        value.exec->oidc.reset(); value.exec->arguments = {"emit", QString::fromUtf8(output("valid"))};
        if (!process.start(value, "session") || !QTest::qWaitFor([&] { return completions == 1; }, 5000)) return false;
        QTest::qWait(100);
        const auto ready = process.cached(id);
        return completions == 1 && ready && ready->authorization == "Bearer credential-token" && !process.running(id);
    }
    if (name == "oidc_cancel" && (!process.running(id) || !process.cancel(id))) return false;
    if (name == "repeat" && (!process.running(id) || !process.start(value, "session"))) return false;
    if ((name == "cancel" || name == "repeat") && !process.cancel(id)) return false;
    if (!QTest::qWaitFor([&] { return completions == 1; }, 5000)) return false;
    if (process.running(id) || completions != 1) return false;
    const auto result = process.result(id);
    if (name == "eks") {
        const auto* ready = std::get_if<ClusterConnection>(&result);
        return ready && ready->authorization.startsWith("Bearer k8s-aws-v1.") && ready->credentialReady;
    }
    if (name == "valid" || name == "future" || name == "near_expiry" || name == "stderr") {
        const auto* ready = std::get_if<ClusterConnection>(&result);
        const auto cached = process.cached(id);
        const bool expired = name == "near_expiry" && (QTest::qWait(3200), !process.cached(id));
        return ready && ready->authorization == "Bearer credential-token" && ready->credentialReady
            && cached && cached->authorization == ready->authorization && process.invalidate(id)
            && !process.cached(id) && std::holds_alternative<Failure>(process.result(id))
            && (name != "near_expiry" || expired);
    }
    const auto* failure = std::get_if<Failure>(&result);
    return failure && !process.cached(id) && (name != "oversized" || failure->message.contains("1 MiB"));
}
} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    const auto args = app.arguments();
    if (args.size() >= 2 && (args[1] == "emit" || args[1] == "emit-error" || args[1] == "emit-oversized"
        || args[1] == "emit-binary" || args[1] == "emit-stderr" || args[1] == "sleep")) {
        if (args[1] == "sleep") { QThread::sleep(3); return 0; }
        const auto bytes = args[1] == "emit-oversized" ? QByteArray(1024 * 1024 + 1, 'x')
            : args[1] == "emit-binary" ? QByteArray(1, char(0xff)) : args.value(2).toUtf8();
        if (args[1] == "emit-stderr") std::fputs("credential program diagnostic\n", stderr);
        std::fwrite(bytes.constData(), 1, size_t(bytes.size()), stdout);
        return args[1] == "emit-error" ? 7 : 0;
    }
    if (args.size() != 2) return 2;
    return scenario(args[1]) ? 0 : 1;
}
