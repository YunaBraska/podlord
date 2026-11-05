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

Result<QString> awsProfileName(const ExecCommand& command) {
    QString selected;
    for (int i = 0; i < command.arguments.size(); ++i) {
        const auto& arg = command.arguments[i];
        if (arg != "--profile" && !arg.startsWith("--profile=")) continue;
        if (!selected.isEmpty()) return Failure{StoreError::InvalidData, "AWS profile is selected more than once."};
        selected = arg == "--profile" ? (++i < command.arguments.size() ? command.arguments[i] : QString{}) : arg.mid(10);
        if (selected.isEmpty() || selected.startsWith("--")) return Failure{StoreError::InvalidData, "AWS profile name is missing."};
    }
    if (selected.isEmpty()) selected = command.environment.value("AWS_PROFILE", "default");
    if (selected.isEmpty() || selected.size() > 128 || selected != selected.trimmed()
        || std::any_of(selected.cbegin(), selected.cend(), [](QChar c) { return c.category() == QChar::Other_Control || c == '[' || c == ']'; }))
        return Failure{StoreError::InvalidData, "AWS profile name is invalid."};
    return selected;
}

Result<QMap<QString, QString>> readAwsProfile(const QByteArray& config, const QByteArray& credentials, const QString& name) {
    if ((config.isEmpty() && credentials.isEmpty()) || config.size() > 1024 * 1024 || credentials.size() > 1024 * 1024)
        return Failure{StoreError::InvalidData, "Select AWS config or credentials files up to 1 MiB each."};
    const auto sections = [](const QByteArray& bytes) -> Result<QMap<QString, QMap<QString, QString>>> {
        const auto content = bytes.startsWith("\xEF\xBB\xBF") ? bytes.mid(3) : bytes;
        const auto decoded = QString::fromUtf8(content);
        if (decoded.toUtf8() != content || decoded.contains(QChar::Null))
            return Failure{StoreError::InvalidData, "AWS profile files must contain UTF-8 text."};
        QMap<QString, QMap<QString, QString>> result;
        QString section;
        for (const auto& raw : decoded.split('\n')) {
            const auto line = raw.trimmed();
            if (line.isEmpty() || line.startsWith('#') || line.startsWith(';')) continue;
            if (line.startsWith('[')) {
                if (!line.endsWith(']')) return Failure{StoreError::InvalidData, "AWS profile section is malformed."};
                section = line.mid(1, line.size() - 2).trimmed();
                if (section.isEmpty() || result.contains(section)) return Failure{StoreError::InvalidData, "AWS profile sections must be named and unique."};
                result.insert(section, {});
                continue;
            }
            const auto equal = line.indexOf('=');
            if (section.isEmpty() || equal < 1) return Failure{StoreError::InvalidData, "AWS profile field is malformed."};
            const auto key = line.left(equal).trimmed().toLower();
            if (result[section].contains(key)) return Failure{StoreError::InvalidData, "AWS profile contains duplicate fields."};
            result[section].insert(key, line.mid(equal + 1).trimmed());
        }
        return result;
    };
    const auto configSections = sections(config), keySections = sections(credentials);
    if (const auto* failure = std::get_if<Failure>(&configSections)) return *failure;
    if (const auto* failure = std::get_if<Failure>(&keySections)) return *failure;
    const auto& configurations = std::get<QMap<QString, QMap<QString, QString>>>(configSections);
    const auto& keys = std::get<QMap<QString, QMap<QString, QString>>>(keySections);
    const auto configSection = name == "default" ? name : "profile " + name;
    if (!configurations.contains(configSection) && !keys.contains(name))
        return Failure{StoreError::NotFound, "The selected AWS profile is not present in these files."};
    auto fields = configurations.value(configSection);
    const auto credentialFields = keys.value(name);
    for (auto it = credentialFields.cbegin(); it != credentialFields.cend(); ++it) fields.insert(it.key(), it.value());
    for (const auto* unsupported : {"role_arn", "source_profile", "credential_source", "credential_process", "web_identity_token_file", "endpoint_url"})
        if (fields.contains(unsupported)) return Failure{StoreError::UnsupportedVersion,
            "This AWS profile requires an environment-bound, external or assumed-role credential path not supported by this import. No command was run."};
    if (fields.contains("sso_session")) {
        const auto section = "sso-session " + fields.value("sso_session");
        if (!configurations.contains(section)) return Failure{StoreError::InvalidData, "The selected AWS SSO session is missing."};
        const auto session = configurations.value(section);
        for (const auto* key : {"sso_start_url", "sso_region", "sso_registration_scopes"}) {
            if (fields.contains(key) && fields.value(key) != session.value(key))
                return Failure{StoreError::InvalidData, "AWS profile and SSO session settings conflict."};
            if (session.contains(key)) fields.insert(key, session.value(key));
        }
    }
    if (fields.contains("sso_registration_scopes") && fields.value("sso_registration_scopes") != "sso:account:access")
        return Failure{StoreError::UnsupportedVersion, "Native AWS sign-in requires the sso:account:access scope."};
    QMap<QString, QString> selected;
    for (const auto* key : {"region", "aws_access_key_id", "aws_secret_access_key", "aws_session_token",
                            "sso_start_url", "sso_region", "sso_account_id", "sso_role_name"})
        if (fields.contains(key)) selected.insert(key, fields.value(key));
    return selected;
}

Result<std::optional<EksLogin>> nativeEksConfig(const ExecCommand& command, const std::optional<QMap<QString, QString>>& importedProfile) {
    const auto program = QFileInfo(QString(command.program).replace('\\', '/')).fileName().toLower();
    if (program != "aws" && program != "aws.exe")
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
        if (key != "--region" && key != "--cluster-name" && key != "--cluster-id" && key != "--output" && key != "--profile")
            return Failure{StoreError::UnsupportedVersion, "This EKS credential option is not supported by native sign-in."};
        const auto value = equals >= 0 ? argument.mid(equals + 1) : (++i < command.arguments.size() ? command.arguments[i] : QString{});
        if (value.isEmpty() || value.startsWith("--") || value.contains(QChar::Null) || options.contains(key))
            return Failure{StoreError::InvalidData, "An EKS credential option is missing, duplicated or invalid."};
        options.insert(key, value);
    }
    if (!action) return std::optional<EksLogin>{};
    const auto profileName = awsProfileName(command);
    if (const auto* failure = std::get_if<Failure>(&profileName)) return *failure;
    if (!importedProfile && (options.contains("--profile") || command.environment.contains("AWS_PROFILE")))
        return Failure{StoreError::InvalidData, "Explicitly import this AWS profile through Sources before native sign-in. No CLI was run."};
    if (options.contains("--output") && options.value("--output") != "json")
        return Failure{StoreError::UnsupportedVersion, "Native EKS only supports JSON credential output."};
    const auto cluster = options.contains("--cluster-id") ? options.value("--cluster-id") : options.value("--cluster-name");
    const auto region = options.value("--region", command.environment.value("AWS_REGION",
        command.environment.value("AWS_DEFAULT_REGION", importedProfile ? importedProfile->value("region") : QString{})));
    static const QRegularExpression clusterName("^[A-Za-z0-9][A-Za-z0-9_.-]{0,127}$");
    static const QRegularExpression regionName("^[a-z0-9]+(?:-[a-z0-9]+)+-[0-9]+$");
    if (options.contains("--cluster-id") && options.contains("--cluster-name"))
        return Failure{StoreError::InvalidData, "Choose one EKS cluster name or cluster ID."};
    if (!clusterName.match(cluster).hasMatch() || region.size() > 64 || !regionName.match(region).hasMatch())
        return Failure{StoreError::InvalidData, "Native EKS requires a valid cluster and region."};
    const bool imported = command.environment.contains("AWS_ACCESS_KEY_ID") || command.environment.contains("AWS_SECRET_ACCESS_KEY")
        || command.environment.contains("AWS_SESSION_TOKEN");
    const auto credential = [&](const char* key) {
        if (importedProfile) return importedProfile->value(QString::fromLatin1(key).toLower()).toUtf8();
        return imported ? command.environment.value(QString::fromLatin1(key)).toUtf8() : qgetenv(key);
    };
    EksLogin login{cluster, region, credential("AWS_ACCESS_KEY_ID"), credential("AWS_SECRET_ACCESS_KEY"), credential("AWS_SESSION_TOKEN"), {}, {}};
    if (importedProfile && (importedProfile->contains("sso_start_url") || importedProfile->contains("sso_region")
        || importedProfile->contains("sso_account_id") || importedProfile->contains("sso_role_name"))) {
        AwsSso sso{QUrl(importedProfile->value("sso_start_url"), QUrl::StrictMode), importedProfile->value("sso_region"),
            importedProfile->value("sso_account_id"), importedProfile->value("sso_role_name")};
        static const QRegularExpression account("\\A[0-9]{12}\\z"), role("\\A[\\w+=,.@-]{1,128}\\z");
        if (!login.accessKey.isEmpty() || !login.secretKey.isEmpty() || !login.sessionToken.isEmpty()
            || sso.startUrl.toString().size() > 4096 || !sso.startUrl.isValid() || sso.startUrl.scheme() != "https" || sso.startUrl.host().isEmpty()
            || !sso.startUrl.userInfo().isEmpty() || sso.startUrl.port() != -1 || sso.startUrl.hasQuery() || sso.startUrl.hasFragment()
            || sso.region.size() > 64 || !regionName.match(sso.region).hasMatch() || !account.match(sso.accountId).hasMatch() || !role.match(sso.roleName).hasMatch())
            return Failure{StoreError::InvalidData, "AWS SSO requires a valid start URL, region, account and role, without mixed access keys."};
        login.sso = std::move(sso);
        return std::optional<EksLogin>{std::move(login)};
    }
    if (!printable(login.accessKey) || login.accessKey.size() > 128 || !printable(login.secretKey) || login.secretKey.size() > 128
        || (!login.sessionToken.isEmpty() && (!printable(login.sessionToken) || login.sessionToken.size() > 8192)))
        return Failure{StoreError::InvalidData, "Native EKS requires valid AWS access keys from one imported or local environment; no CLI was run."};
    return std::optional<EksLogin>{std::move(login)};
}

Result<ClusterConnection> nativeEksToken(ClusterConnection connection, QDateTime now) {
    if (!connection.exec || !connection.exec->eks || !now.isValid())
        return Failure{StoreError::InvalidInput, "No valid EKS credential was confirmed."};
    const auto& eks = *connection.exec->eks;
    if (!printable(eks.accessKey) || eks.accessKey.size() > 128 || !printable(eks.secretKey) || eks.secretKey.size() > 128
        || (!eks.sessionToken.isEmpty() && (!printable(eks.sessionToken) || eks.sessionToken.size() > 8192))
        || (eks.credentialExpiresAt.isValid() && eks.credentialExpiresAt <= now.addSecs(30)))
        return Failure{StoreError::InvalidData, "AWS credentials are missing, invalid or expired. Confirm another sign-in attempt."};
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
    if (eks.credentialExpiresAt.isValid()) connection.expiresAt = std::min(connection.expiresAt, eks.credentialExpiresAt);
    connection.credentialReady = true;
    connection.exec->eks.reset();
    for (const auto* name : {"AWS_ACCESS_KEY_ID", "AWS_SECRET_ACCESS_KEY", "AWS_SESSION_TOKEN"}) connection.exec->environment.remove(name);
    return connection;
}
} // namespace podlord
