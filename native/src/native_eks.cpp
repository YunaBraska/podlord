#include "native_eks.h"
#include <QCryptographicHash>
#include <QFileInfo>
#include <QMap>
#include <QMessageAuthenticationCode>
#include <QRegularExpression>
#include <QUrl>
#include <algorithm>

namespace podlord {
namespace {
bool printable(const QByteArray& value) {
    return !value.isEmpty() && std::all_of(value.cbegin(), value.cend(), [](char ch) {
        return static_cast<unsigned char>(ch) >= 33 && static_cast<unsigned char>(ch) <= 126;
    });
}
QByteArray hmac(const QByteArray& key, const QByteArray& value) {
    return QMessageAuthenticationCode::hash(value, key, QCryptographicHash::Sha256);
}
QByteArray encoded(const QString& value) { return QUrl::toPercentEncoding(value); }
} // namespace

Result<std::optional<EksLogin>> nativeEksConfig(const ExecCommand& command) {
    if (QFileInfo(command.program).fileName().compare("aws", Qt::CaseInsensitive) != 0)
        return std::optional<EksLogin>{};
    QMap<QString, QString> options;
    bool action = false;
    for (int i = 0; i < command.arguments.size(); ++i) {
        const auto argument = command.arguments[i];
        if (argument == "eks" && i + 1 < command.arguments.size() && command.arguments[i + 1] == "get-token" && !action) {
            action = true; ++i; continue;
        }
        const auto equals = argument.indexOf('=');
        const auto key = equals < 0 ? argument : argument.left(equals);
        if (key != "--region" && key != "--cluster-name" && key != "--cluster-id" && key != "--output")
            return Failure{StoreError::UnsupportedVersion, "This EKS credential option is not supported by native sign-in."};
        const auto value = equals >= 0 ? argument.mid(equals + 1) : (++i < command.arguments.size() ? command.arguments[i] : QString{});
        if (value.isEmpty() || value.startsWith("--") || value.contains(QChar::Null) || options.contains(key))
            return Failure{StoreError::InvalidData, "An EKS credential option is missing, duplicated or invalid."};
        options.insert(key, value);
    }
    if (!action) return std::optional<EksLogin>{};
    if (options.contains("--output") && options.value("--output") != "json")
        return Failure{StoreError::UnsupportedVersion, "Native EKS only supports JSON credential output."};
    const auto cluster = options.contains("--cluster-id") ? options.value("--cluster-id") : options.value("--cluster-name");
    const auto region = options.value("--region");
    static const QRegularExpression clusterName("^[A-Za-z0-9][A-Za-z0-9_.-]{0,127}$");
    static const QRegularExpression regionName("^[a-z0-9]+(?:-[a-z0-9]+)+-[0-9]+$");
    if (options.contains("--cluster-id") && options.contains("--cluster-name"))
        return Failure{StoreError::InvalidData, "Choose one EKS cluster name or cluster ID."};
    if (!clusterName.match(cluster).hasMatch() || !regionName.match(region).hasMatch())
        return Failure{StoreError::InvalidData, "Native EKS requires a valid cluster and region."};
    const bool imported = command.environment.contains("AWS_ACCESS_KEY_ID") || command.environment.contains("AWS_SECRET_ACCESS_KEY")
        || command.environment.contains("AWS_SESSION_TOKEN");
    const auto credential = [&](const char* key) {
        return imported ? command.environment.value(QString::fromLatin1(key)).toUtf8() : qgetenv(key);
    };
    EksLogin login{cluster, region, credential("AWS_ACCESS_KEY_ID"), credential("AWS_SECRET_ACCESS_KEY"), credential("AWS_SESSION_TOKEN")};
    if (!printable(login.accessKey) || login.accessKey.size() > 128 || !printable(login.secretKey) || login.secretKey.size() > 128
        || (!login.sessionToken.isEmpty() && (!printable(login.sessionToken) || login.sessionToken.size() > 8192)))
        return Failure{StoreError::InvalidData, "Native EKS requires valid AWS access keys from one imported or local environment; no CLI was run."};
    return std::optional<EksLogin>{std::move(login)};
}

Result<ClusterConnection> nativeEksToken(ClusterConnection connection, QDateTime now) {
    if (!connection.exec || !connection.exec->eks || !now.isValid())
        return Failure{StoreError::InvalidInput, "No valid EKS credential was confirmed."};
    const auto& eks = *connection.exec->eks;
    const auto utc = now.toUTC();
    const auto day = utc.toString("yyyyMMdd").toUtf8();
    const auto stamp = utc.toString("yyyyMMdd'T'HHmmss'Z'").toUtf8();
    const auto scope = day + '/' + eks.region.toUtf8() + "/sts/aws4_request";
    const auto host = "sts." + eks.region.toUtf8() + (eks.region.startsWith("cn-") ? ".amazonaws.com.cn" : ".amazonaws.com");
    QMap<QByteArray, QByteArray> parameters{{"Action", "GetCallerIdentity"}, {"Version", "2011-06-15"},
        {"X-Amz-Algorithm", "AWS4-HMAC-SHA256"}, {"X-Amz-Credential", eks.accessKey + '/' + scope},
        {"X-Amz-Date", stamp}, {"X-Amz-Expires", "60"}, {"X-Amz-SignedHeaders", "host;x-k8s-aws-id"}};
    if (!eks.sessionToken.isEmpty()) parameters.insert("X-Amz-Security-Token", eks.sessionToken);
    QByteArray query;
    for (auto it = parameters.cbegin(); it != parameters.cend(); ++it) {
        if (!query.isEmpty()) query += '&';
        query += encoded(QString::fromLatin1(it.key())) + '=' + encoded(QString::fromLatin1(it.value()));
    }
    const auto headers = "host:" + host + "\nx-k8s-aws-id:" + eks.cluster.toUtf8() + '\n';
    const auto canonical = "GET\n/\n" + query + "\n" + headers + "\nhost;x-k8s-aws-id\n"
        + QCryptographicHash::hash({}, QCryptographicHash::Sha256).toHex();
    const auto toSign = "AWS4-HMAC-SHA256\n" + stamp + '\n' + scope + '\n'
        + QCryptographicHash::hash(canonical, QCryptographicHash::Sha256).toHex();
    auto key = hmac("AWS4" + eks.secretKey, day);
    key = hmac(key, eks.region.toUtf8());
    key = hmac(key, "sts");
    key = hmac(key, "aws4_request");
    const auto signature = hmac(key, toSign).toHex();
    const auto url = "https://" + host + "/?" + query + "&X-Amz-Signature=" + signature;
    connection.authorization = "Bearer k8s-aws-v1." + url.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
    connection.expiresAt = utc.addSecs(840);
    connection.credentialReady = true;
    return connection;
}
} // namespace podlord
