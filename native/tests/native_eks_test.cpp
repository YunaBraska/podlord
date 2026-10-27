#include "native_eks.h"
#include <QCoreApplication>
#include <QUrlQuery>

using namespace podlord;

namespace {
ExecCommand command() {
    ExecCommand value;
    value.program = "/usr/bin/aws";
    value.arguments = {"--region", "eu-central-1", "eks", "get-token", "--cluster-name", "podlord-e2e", "--output", "json"};
    value.environment = {{"AWS_ACCESS_KEY_ID", "AKIDEXAMPLE"},
                         {"AWS_SECRET_ACCESS_KEY", "wJalrXUtnFEMI/K7MDENG+bPxRfiCYEXAMPLEKEY"}};
    return value;
}

std::optional<EksLogin> parsed(const ExecCommand& value) {
    const auto result = nativeEksConfig(value);
    return std::holds_alternative<Failure>(result) ? std::optional<EksLogin>{} : std::get<std::optional<EksLogin>>(result);
}

ClusterConnection connection(const ExecCommand& value) {
    ClusterConnection result;
    result.exec = value;
    result.exec->eks = parsed(value);
    return result;
}

bool signedUrl(const Result<ClusterConnection>& result, QUrl* url = nullptr) {
    const auto* signedConnection = std::get_if<ClusterConnection>(&result);
    if (!signedConnection || !signedConnection->credentialReady || !signedConnection->authorization.startsWith("Bearer k8s-aws-v1.")) return false;
    const auto token = signedConnection->authorization.mid(sizeof("Bearer k8s-aws-v1.") - 1);
    const auto decoded = QByteArray::fromBase64(token, QByteArray::Base64UrlEncoding);
    const auto parsedUrl = QUrl::fromEncoded(decoded);
    if (!parsedUrl.isValid() || parsedUrl.host() != "sts.eu-central-1.amazonaws.com" || parsedUrl.scheme() != "https") return false;
    if (url) *url = parsedUrl;
    return true;
}
} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    if (argc != 2) return 2;
    const QString scenario = QString::fromUtf8(argv[1]);
    auto value = command();
    const auto fixedTime = QDateTime::fromString("2026-10-15T19:00:00Z", Qt::ISODate);
    bool passed = false;
    if (scenario == "parse_standard") {
        const auto login = parsed(value);
        passed = login && login->cluster == "podlord-e2e" && login->region == "eu-central-1"
            && login->accessKey == "AKIDEXAMPLE" && !login->secretKey.isEmpty();
    } else if (scenario == "parse_equals") {
        value.arguments = {"eks", "get-token", "--region=eu-central-1", "--cluster-name=podlord-e2e", "--output=json"};
        passed = parsed(value).has_value();
    } else if (scenario == "parse_cluster_id") {
        value.arguments = {"eks", "get-token", "--region", "eu-central-1", "--cluster-id", "podlord-e2e"};
        passed = parsed(value).has_value();
    } else if (scenario == "parse_no_action") {
        value.arguments = {"--region", "eu-central-1"};
        passed = !parsed(value).has_value();
    } else if (scenario == "parse_bad_output") {
        value.arguments.append("--output=yaml");
        passed = std::holds_alternative<Failure>(nativeEksConfig(value));
    } else if (scenario == "parse_missing_value") {
        value.arguments.append("--region");
        passed = std::holds_alternative<Failure>(nativeEksConfig(value));
    } else if (scenario == "parse_option_as_value") {
        value.arguments.append({"--region", "--output=json"});
        passed = std::holds_alternative<Failure>(nativeEksConfig(value));
    } else if (scenario == "parse_nul_value") {
        value.arguments.append("--region=" + QString::fromUtf8(QByteArray("eu\0-west", 8)));
        passed = std::holds_alternative<Failure>(nativeEksConfig(value));
    } else if (scenario == "parse_duplicate") {
        value.arguments.append("--region=eu-west-1");
        passed = std::holds_alternative<Failure>(nativeEksConfig(value));
    } else if (scenario == "parse_bad_cluster") {
        value.arguments[5] = "../other";
        passed = std::holds_alternative<Failure>(nativeEksConfig(value));
    } else if (scenario == "parse_bad_region") {
        value.arguments[1] = "../other";
        passed = std::holds_alternative<Failure>(nativeEksConfig(value));
    } else if (scenario == "parse_missing_access") {
        value.environment.remove("AWS_ACCESS_KEY_ID");
        passed = std::holds_alternative<Failure>(nativeEksConfig(value));
    } else if (scenario == "parse_missing_secret") {
        value.environment.remove("AWS_SECRET_ACCESS_KEY");
        passed = std::holds_alternative<Failure>(nativeEksConfig(value));
    } else if (scenario == "parse_control_access") {
        value.environment["AWS_ACCESS_KEY_ID"] = "key with space";
        passed = std::holds_alternative<Failure>(nativeEksConfig(value));
    } else if (scenario == "parse_long_access") {
        value.environment["AWS_ACCESS_KEY_ID"] = QString(129, 'a');
        passed = std::holds_alternative<Failure>(nativeEksConfig(value));
    } else if (scenario == "parse_control_secret") {
        value.environment["AWS_SECRET_ACCESS_KEY"] = "secret\nvalue";
        passed = std::holds_alternative<Failure>(nativeEksConfig(value));
    } else if (scenario == "parse_long_secret") {
        value.environment["AWS_SECRET_ACCESS_KEY"] = QString(129, 's');
        passed = std::holds_alternative<Failure>(nativeEksConfig(value));
    } else if (scenario == "parse_control_session") {
        value.environment["AWS_SESSION_TOKEN"] = "bad\nvalue";
        passed = std::holds_alternative<Failure>(nativeEksConfig(value));
    } else if (scenario == "parse_long_session") {
        value.environment["AWS_SESSION_TOKEN"] = QString(8193, 's');
        passed = std::holds_alternative<Failure>(nativeEksConfig(value));
    } else if (scenario == "parse_local_environment") {
        value.environment.clear();
        qputenv("AWS_ACCESS_KEY_ID", "LOCALKEY"); qputenv("AWS_SECRET_ACCESS_KEY", "LOCALSECRET");
        const auto login = parsed(value);
        passed = login && login->accessKey == "LOCALKEY" && login->secretKey == "LOCALSECRET";
    } else if (scenario == "parse_unknown_option") {
        value.arguments += {"--profile", "other"};
        passed = std::holds_alternative<Failure>(nativeEksConfig(value));
    } else if (scenario == "parse_conflicting_cluster") {
        value.arguments += {"--cluster-id", "other"};
        passed = std::holds_alternative<Failure>(nativeEksConfig(value));
    } else if (scenario == "parse_other_program") {
        value.program = "gcloud";
        const auto result = nativeEksConfig(value);
        passed = !std::holds_alternative<Failure>(result) && !std::get<std::optional<EksLogin>>(result);
    } else if (scenario == "sign_reference") {
        const auto result = nativeEksToken(connection(value), fixedTime);
        QUrl url;
        passed = signedUrl(result, &url)
            && QUrlQuery(url).queryItemValue("X-Amz-Signature") == "9f3171402e3f91813fda398d6ebc2e509d5487771a604890999fa37a0ad466f4"
            && std::get<ClusterConnection>(result).expiresAt == fixedTime.addSecs(840)
            && !url.toString().contains("EXAMPLEKEY");
    } else if (scenario == "sign_session") {
        value.environment.insert("AWS_SESSION_TOKEN", "temporary+token/=");
        const auto result = nativeEksToken(connection(value), fixedTime);
        QUrl url;
        passed = signedUrl(result, &url)
            && QUrlQuery(url).queryItemValue("X-Amz-Security-Token", QUrl::FullyDecoded) == "temporary+token/="
            && QUrlQuery(url).queryItemValue("X-Amz-Signature") != "9f3171402e3f91813fda398d6ebc2e509d5487771a604890999fa37a0ad466f4";
    } else if (scenario == "sign_missing") {
        passed = std::holds_alternative<Failure>(nativeEksToken(ClusterConnection{}, fixedTime));
    } else if (scenario == "sign_missing_eks") {
        ClusterConnection missing;
        missing.exec.emplace();
        passed = std::holds_alternative<Failure>(nativeEksToken(missing, fixedTime));
    } else if (scenario == "sign_invalid_time") {
        passed = std::holds_alternative<Failure>(nativeEksToken(connection(value), QDateTime{}));
    } else if (scenario == "sign_china_region") {
        value.arguments[1] = "cn-north-1";
        const auto result = nativeEksToken(connection(value), fixedTime);
        const auto* ready = std::get_if<ClusterConnection>(&result);
        const auto token = ready ? ready->authorization.mid(sizeof("Bearer k8s-aws-v1.") - 1) : QByteArray{};
        passed = ready && ready->credentialReady && QByteArray::fromBase64(token, QByteArray::Base64UrlEncoding)
            .startsWith("https://sts.cn-north-1.amazonaws.com.cn/?");
    } else {
        return 2;
    }
    return passed ? 0 : 1;
}
