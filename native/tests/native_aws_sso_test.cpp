#include "aws_boundary.h"
#include "credential_process.h"
#include "native_aws_login.h"
#include "browser_boundary.h"
#include <QGuiApplication>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkProxy>
#include <QSignalSpy>
#include <QSslKey>
#include <QSslSocket>
#include <QTcpServer>
#include <QTest>
#include <QUrlQuery>
#include <cstdio>
#include <memory>
#include <stdexcept>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rsa.h>
#include <openssl/x509v3.h>

using namespace podlord;
using podlord::test::AwsBoundary;
namespace {
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }

ClusterConnection connection() {
    ClusterConnection value;
    value.contextId = "test-context"; value.credentialId = "imported:test-context"; value.credentialReady = false;
    value.exec.emplace(); value.exec->program = "aws"; value.exec->arguments = {"eks", "get-token", "--cluster-name", "example", "--profile", "prod"};
    const QMap<QString, QString> fields{{"region", "eu-central-1"}, {"sso_start_url", "https://company.awsapps.com/start"},
        {"sso_region", "eu-west-1"}, {"sso_account_id", "123456789012"}, {"sso_role_name", "Developer"}};
    value.exec->eks = std::get<std::optional<EksLogin>>(nativeEksConfig(*value.exec, AwsProfile{fields, {}}));
    return value;
}
void run(const QString& scenario) {
    BrowserBoundary browser;
    AwsBoundary boundary(scenario);
    auto initial = connection();
    if (scenario == "role_cancel_dispatch" || scenario == "role_expired_source" || scenario == "role_shutdown") {
        initial.exec->eks->sso.reset(); initial.exec->eks->accessKey = "ROOTKEY"; initial.exec->eks->secretKey = "private-signing-secret";
        initial.exec->eks->roles.append(AwsRole{"arn:aws:iam::123456789012:role/Target", {}, {}, {}, 900});
        if (scenario == "role_expired_source") initial.exec->eks->credentialExpiresAt = QDateTime::currentDateTimeUtc().addSecs(-1);
        auto login = std::make_unique<NativeAwsLogin>(initial);
        int completions = 0;
        QObject::connect(login.get(), &NativeAwsLogin::completed, &browser, [&] { ++completions; });
        if (scenario == "role_cancel_dispatch") QObject::connect(login.get(), &NativeAwsLogin::instructionChanged, &browser, [&] { login->cancel(); });
        if (scenario == "role_shutdown") boundary.mode = "role_pending";
        login->start();
        if (scenario == "role_shutdown") {
            require(QTest::qWaitFor([&] { return boundary.roles == 1; }, 5000), "Role request did not arrive before shutdown.");
            login.reset(); QTest::qWait(30);
            require(completions == 0, "Destroyed role owner published a result.");
        } else require(completions == 1 && boundary.connections == 0, "Cancelled or expired source sent an STS request.");
        return;
    }
    if (scenario.startsWith("role_") || scenario.startsWith("mfa_")) {
        const bool sso = scenario == "role_sso";
        auto& eks = *initial.exec->eks;
        if (!sso) { eks.sso.reset(); eks.accessKey = "ROOTKEY"; eks.secretKey = "private-signing-secret"; eks.sessionToken = "root-session-token"; }
        eks.roles.append(AwsRole{"arn:aws:iam::123456789012:role/Target", "test-session", "external/value", {}, 900});
        if (scenario.startsWith("mfa_")) eks.roles.first().mfaSerial = "arn:aws:iam::123456789012:mfa/user";
        if (scenario == "role_chain" || scenario == "mfa_chain") {
            auto next = eks.roles.first(); next.arn = "arn:aws:iam::123456789012:role/Final"; next.sessionName.clear(); eks.roles.append(next);
        }
        if (scenario == "role_china") eks.region = "cn-north-1";
        CredentialProcess owner;
        QSignalSpy completed(&owner, &CredentialProcess::completed);
        require(owner.start(initial, "session"), "Native AWS role owner did not start.");
        const auto first = owner.challenge(initial.credentialId);
        if (scenario.startsWith("mfa_")) {
            require(first && first->device == eks.roles.first().mfaSerial && boundary.connections == 0, "MFA challenge contacted AWS before code submission.");
            require(!owner.submitMfa(initial.credentialId, first->id + "-stale", "123456")
                && !owner.submitMfa(initial.credentialId, first->id, "12345")
                && !owner.submitMfa(initial.credentialId, first->id, QString(6, QChar(0xFF11)))
                && !owner.submitMfa("another-context", first->id, "123456") && boundary.connections == 0, "Invalid or foreign MFA code sent a request.");
            if (scenario == "mfa_cancel" || scenario == "mfa_revoke") {
                require(scenario == "mfa_cancel" ? owner.cancel(initial.credentialId) : owner.revoke(initial.credentialId), "MFA cannot cancel or revoke.");
                require(!owner.challenge(initial.credentialId) && !owner.submitMfa(initial.credentialId, first->id, "123456")
                    && boundary.connections == 0 && !owner.cached(initial.credentialId), "Cancelled MFA retained authority."); return;
            }
            require(owner.submitMfa(initial.credentialId, first->id, "123456") && !owner.submitMfa(initial.credentialId, first->id, "123456"), "MFA code was lost or submitted twice.");
            if (scenario == "mfa_chain") {
                require(QTest::qWaitFor([&] { const auto next = owner.challenge(initial.credentialId); return next && next->id != first->id; }, 5000), "Chained MFA did not produce a fresh challenge.");
                const auto next = owner.challenge(initial.credentialId);
                require(!owner.submitMfa(initial.credentialId, first->id, "123456") && owner.submitMfa(initial.credentialId, next->id, "654321"), "Chained MFA accepted a stale challenge.");
            }
        }
        require(QTest::qWaitFor([&] { return completed.count() == 1; }, scenario.endsWith("timeout") ? 25000 : 15000), "Native role owner did not finish.");
        const bool failure = QStringList{"role_denied", "role_redirect", "role_server", "role_invalid_xml", "role_oversized", "role_timeout", "mfa_denied"}.contains(scenario);
        if (failure) require(!owner.cached(initial.credentialId) && std::holds_alternative<Failure>(owner.result(initial.credentialId))
            && !std::get<Failure>(owner.result(initial.credentialId)).message.contains("private-"), "Failed role authentication retained authority or leaked an AWS response.");
        else {
            const auto ready = owner.cached(initial.credentialId);
            const int count = scenario == "role_chain" || scenario == "mfa_chain" ? 2 : 1;
            require(ready && ready->credentialReady && ready->exec && !ready->exec->eks && boundary.roles == count && boundary.valid, "Native role chain did not yield a sanitized EKS token.");
            const auto url = QUrl::fromEncoded(QByteArray::fromBase64(ready->authorization.mid(18), QByteArray::Base64UrlEncoding));
            require(QUrlQuery(url).queryItemValue("X-Amz-Security-Token") == "private-role-token-" + QString::number(count), "EKS token used the source instead of final role identity.");
            require(boundary.roleForms.first().value("ExternalId") == "external/value" && boundary.roleForms.first().value("DurationSeconds") == "900", "Role options were not preserved.");
            if (count == 2) require(boundary.roleHeaders.last().contains("Credential=ROLEKEY1/"), "Chained role did not use intermediate credentials.");
            if (first) require(boundary.roleForms.first().value("TokenCode") == "123456" && !owner.challenge(initial.credentialId), "MFA code or challenge lifecycle was incorrect.");
        }
        require(browser.urls.size() == (sso ? 1 : 0) && boundary.valid, "Native role unexpectedly opened a browser or violated AWS transport.");
        const int calls = boundary.roles;
        QTest::qWait(50);
        require(calls == boundary.roles && completed.count() == 1, "Native role automatically retried.");
        return;
    }
    if (scenario == "china") initial.exec->eks->sso->region = "cn-north-1";
    if (scenario == "shutdown") {
        boundary.mode = "register_pending";
        auto* login = new NativeAwsLogin(initial);
        int completions = 0;
        QObject::connect(login, &NativeAwsLogin::completed, &browser, [&] { ++completions; });
        login->start();
        require(QTest::qWaitFor([&] { return boundary.registrations == 1; }, 5000), "AWS request did not arrive before shutdown.");
        delete login;
        QTest::qWait(50);
        require(completions == 0 && browser.urls.isEmpty() && boundary.devices == 0, "Destroyed AWS login published authority or opened a browser."); return;
    }
    if (scenario.startsWith("owner_")) {
        boundary.mode = scenario == "owner_success" ? "success" : "device_pending";
        CredentialProcess owner;
        QSignalSpy completed(&owner, &CredentialProcess::completed);
        require(owner.start(initial, "session"), "Cannot start confirmed AWS SSO owner.");
        require(owner.start(initial, "session"), "Duplicate running AWS SSO attempt failed.");
        require(owner.running(initial.credentialId), "AWS SSO is missing from the credential owner.");
        if (scenario == "owner_success") {
            require(QTest::qWaitFor([&] { return completed.count() == 1; }, 15000), "AWS SSO did not finish through its owner.");
            require(owner.cached(initial.credentialId).has_value() && !owner.running(initial.credentialId), "Owner lost completed SSO credentials.");
        } else {
            require(QTest::qWaitFor([&] { return boundary.devices == 1; }, 5000), "AWS SSO did not reach device request.");
            if (scenario == "owner_revoke") {
                require(owner.revoke(initial.credentialId) && !owner.running(initial.credentialId) && completed.isEmpty(), "Revoked SSO attempt published a result.");
                require(!owner.cached(initial.credentialId) && std::holds_alternative<Failure>(owner.result(initial.credentialId)), "Revoked SSO result survived.");
            } else require(owner.cancel(initial.credentialId) && completed.count() == 1 && !owner.running(initial.credentialId), "AWS SSO cancel did not finish exactly once.");
        }
        require(boundary.registrations == 1 && boundary.valid, "Duplicate AWS attempt or incorrect request contract."); return;
    }
    if (scenario == "missing_exec") initial.exec.reset();
    if (scenario == "missing_eks") initial.exec->eks.reset();
    if (scenario == "missing_sso") initial.exec->eks->sso.reset();
    if (scenario == "invalid_sso") initial.exec->eks->sso->region = "bad region";
    NativeAwsLogin login(initial);
    QSignalSpy completed(&login, &NativeAwsLogin::completed);
    std::optional<Result<ClusterConnection>> result;
    QObject::connect(&login, &NativeAwsLogin::completed, &login, [&](Result<ClusterConnection> value) { result = std::move(value); });
    bool sawInstruction = false;
    QObject::connect(&login, &NativeAwsLogin::instructionChanged, &login, [&] {
        sawInstruction = login.instruction().contains("ABCD-EFGH") && !login.instruction().contains("private-");
        if (scenario == "cancel_instruction") login.cancel();
    });
    if (scenario == "cancel_before") login.cancel();
    login.start(); login.start();
    if (scenario == "cancel_pending") {
        boundary.mode = "register_pending";
        require(QTest::qWaitFor([&] { return boundary.registrations == 1; }, 5000), "AWS registration did not arrive before cancellation.");
        login.cancel();
    }
    require(QTest::qWaitFor([&] { return result.has_value(); }, scenario.endsWith("timeout") ? 25000 : 22000), "AWS SSO did not finish before watchdog.");
    if (completed.count() != 1 || !boundary.valid) std::fprintf(stderr, "AWS completion count %d, register %d, device %d, token %d, credentials %d.\n", int(completed.count()), boundary.registrations, boundary.devices, boundary.tokens, boundary.credentials);
    require(completed.count() == 1 && boundary.valid, "AWS SSO completed twice or violated the HTTP contract.");
    const bool success = QStringList{"success", "pending", "slow_down", "pending_type", "slow_type", "device_no_complete", "short_role", "china"}.contains(scenario);
    if (scenario.startsWith("missing_") || scenario == "invalid_sso" || scenario == "cancel_before")
        require(boundary.connections == 0 && browser.urls.isEmpty(), "Invalid or cancelled SSO configuration contacted AWS.");
    else if (scenario == "tls_untrusted")
        require(boundary.connections > 0 && boundary.registrations == 0 && browser.urls.isEmpty(), "Untrusted TLS reached the AWS application boundary.");
    else if (scenario.startsWith("register_") || scenario == "cancel_pending")
        require(boundary.registrations == 1 && boundary.devices == 0 && browser.urls.isEmpty(), "Registration failure was not tested at the registration boundary.");
    else if (!success && (scenario.startsWith("device_") || scenario == "cancel_instruction"))
        require(boundary.registrations == 1 && boundary.devices == 1 && boundary.tokens == 0, "Device failure was not tested at the device boundary.");
    else if (scenario.startsWith("token_") || scenario == "denied")
        require(boundary.tokens == 1 && boundary.credentials == 0 && browser.urls.size() == 1, "Token failure was not tested at the token boundary.");
    else if (scenario.startsWith("credentials_"))
        require(boundary.credentials == 1 && browser.urls.size() == 1, "Credential failure was not tested at the role boundary.");
    if (success) {
        if (const auto* failure = std::get_if<Failure>(&*result)) std::fprintf(stderr, "AWS SSO rejected response: %s. Requests: %d/%d/%d/%d, connections %d, socket error %d, TLS backend %s.\n", qPrintable(failure->message), boundary.registrations, boundary.devices, boundary.tokens, boundary.credentials, boundary.connections, boundary.socketError, qPrintable(QSslSocket::activeBackend()));
        require(std::holds_alternative<ClusterConnection>(*result), "Valid AWS SSO flow was rejected.");
        require(!boundary.pollTimes.isEmpty() && boundary.pollTimes.first() - boundary.deviceAt + 2 >= 5000,
            "AWS device polling started before its minimum interval.");
        if (scenario == "pending" || scenario == "pending_type" || scenario == "slow_down" || scenario == "slow_type") {
            const auto interval = scenario == "slow_down" || scenario == "slow_type" ? 10000 : 5000;
            require(boundary.pollTimes.size() == 2 && boundary.pollTimes.last() - boundary.pollTimes.first() + 2 >= interval,
                "AWS pending or slow-down polling did not respect the provider interval.");
        }
        const auto ready = std::get<ClusterConnection>(*result);
        require(ready.credentialReady && ready.exec && !ready.exec->eks && ready.authorization.startsWith("Bearer k8s-aws-v1.")
            && ready.expiresAt > QDateTime::currentDateTimeUtc().addSecs(30) && sawInstruction && browser.urls.size() == 1, "AWS SSO returned an unusable or unsanitized EKS credential.");
        const auto url = QUrl::fromEncoded(QByteArray::fromBase64(ready.authorization.mid(18), QByteArray::Base64UrlEncoding));
        require(QUrlQuery(url).queryItemValue("X-Amz-Security-Token") == "private-session-token" && !url.toString().contains("private-signing-secret"), "EKS signing did not use SSO role credentials.");
        if (scenario == "short_role") require(ready.expiresAt < QDateTime::currentDateTimeUtc().addSecs(125), "EKS token outlived its role credentials.");
    } else {
        require(std::holds_alternative<Failure>(*result), "Invalid AWS SSO flow was accepted.");
        require(!std::get<Failure>(*result).message.contains("private-"), "AWS SSO leaked secret response data.");
    }
    const auto count = boundary.registrations + boundary.devices + boundary.tokens + boundary.credentials;
    login.start(); login.cancel(); QTest::qWait(20);
    require(completed.count() == 1 && count == boundary.registrations + boundary.devices + boundary.tokens + boundary.credentials, "Finished AWS SSO restarted or retried automatically.");
    require(login.instruction().isEmpty(), "Completed SSO kept the device code instruction.");
}
} // namespace
int main(int argc, char** argv) {
    QGuiApplication app(argc, argv);
    if (argc != 2) return 2;
    try {
        require(QSslSocket::setActiveBackend("openssl") || QSslSocket::activeBackend() == "openssl", "The packaged OpenSSL TLS backend is required for this AWS boundary test.");
        run(QString::fromUtf8(argv[1]));
        std::printf("AWS SSO scenario passed: %s\n", argv[1]);
        return 0;
    }
    catch (const std::exception& error) { std::fprintf(stderr, "%s\n", error.what()); return 1; }
}
