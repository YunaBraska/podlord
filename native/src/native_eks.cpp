#include "native_eks.h"
#include <QCryptographicHash>
#include <QFileInfo>
#include <QMap>
#include <QMessageAuthenticationCode>
#include <QRegularExpression>
#include <QUrl>
#include <QSet>
#include <QXmlStreamReader>
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
QByteArray signingKey(const EksLogin& login, const QByteArray& day) {
    return hmac(hmac(hmac(hmac("AWS4" + login.secretKey, day), login.region.toUtf8()), "sts"), "aws4_request");
}
bool validRole(const AwsRole& role) {
    static const QRegularExpression arn("\\Aarn:(?:aws|aws-cn|aws-us-gov):iam::[0-9]{12}:role/(?:[!-~]+/)?[A-Za-z0-9_+=,.@-]{1,64}\\z");
    static const QRegularExpression session("\\A[A-Za-z0-9_+=,.@-]{2,64}\\z");
    static const QRegularExpression external("\\A[A-Za-z0-9_+=,.@:/-]{2,1224}\\z");
    static const QRegularExpression serial("\\A[A-Za-z0-9_+=/:,.@-]{9,256}\\z");
    return role.arn.size() <= 2048 && arn.match(role.arn).hasMatch()
        && (role.sessionName.isEmpty() || session.match(role.sessionName).hasMatch())
        && (role.externalId.isEmpty() || external.match(role.externalId).hasMatch())
        && (role.mfaSerial.isEmpty() || serial.match(role.mfaSerial).hasMatch())
        && role.durationSeconds >= 900 && role.durationSeconds <= 43200;
}
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
    if (selected.isEmpty()) selected = command.environment.value("AWS_DEFAULT_PROFILE", command.environment.value("AWS_PROFILE", "default"));
    if (selected.isEmpty() || selected.size() > 128 || selected != selected.trimmed()
        || std::any_of(selected.cbegin(), selected.cend(), [](QChar c) { return c.category() == QChar::Other_Control || c == '[' || c == ']'; }))
        return Failure{StoreError::InvalidData, "AWS profile name is invalid."};
    return selected;
}

Result<AwsProfile> readAwsProfile(const QByteArray& config, const QByteArray& credentials, const QString& name) {
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
    AwsProfile selected;
    QSet<QString> visited;
    QString current = name;
    QMap<QString, QString> fields;
    for (;;) {
        ExecCommand selection; selection.arguments = {"--profile", current};
        if (std::holds_alternative<Failure>(awsProfileName(selection)))
            return Failure{StoreError::InvalidData, "AWS source profile name is invalid."};
        if (visited.contains(current)) return Failure{StoreError::InvalidData, "AWS source profiles contain a cycle."};
        visited.insert(current);
        const auto section = current == "default" ? current : "profile " + current;
        if (!configurations.contains(section) && !keys.contains(current))
            return Failure{StoreError::NotFound, "The selected AWS profile or source profile is missing from these files."};
        fields = configurations.value(section);
        const auto credentialFields = keys.value(current);
        for (auto it = credentialFields.cbegin(); it != credentialFields.cend(); ++it) fields.insert(it.key(), it.value());
        if (current == name && fields.contains("region")) selected.fields.insert("region", fields.value("region"));
        for (const auto* unsupported : {"credential_source", "credential_process", "web_identity_token_file", "endpoint_url"})
            if (fields.contains(unsupported)) return Failure{StoreError::UnsupportedVersion,
                "This AWS profile requires an environment-bound or external credential path not supported by this import. No command was run."};
        if (!fields.contains("role_arn")) {
            if (fields.contains("source_profile") || fields.contains("mfa_serial") || fields.contains("external_id")
                || fields.contains("role_session_name") || fields.contains("duration_seconds"))
                return Failure{StoreError::InvalidData, "AWS role settings require a role ARN and source profile."};
            break;
        }
        if (fields.value("source_profile").isEmpty()) return Failure{StoreError::InvalidData, "AWS role requires a source profile."};
        AwsRole role{fields.value("role_arn"), fields.value("role_session_name"), fields.value("external_id"), fields.value("mfa_serial")};
        if (fields.contains("duration_seconds")) {
            bool valid = false;
            role.durationSeconds = fields.value("duration_seconds").toInt(&valid);
            if (!valid) return Failure{StoreError::InvalidData, "AWS role duration is invalid."};
        }
        if (!validRole(role)) return Failure{StoreError::InvalidData, "AWS role ARN, session name, external ID, MFA device or duration is invalid."};
        selected.roles.prepend(std::move(role));
        const auto source = fields.value("source_profile");
        const bool staticKeys = fields.contains("aws_access_key_id") || fields.contains("aws_secret_access_key") || fields.contains("aws_session_token");
        const bool sso = fields.contains("sso_session") || fields.contains("sso_start_url") || fields.contains("sso_account_id");
        if (sso || (staticKeys && source != current))
            return Failure{StoreError::InvalidData, "AWS role and source credentials must not be mixed in one profile."};
        if (source == current && staticKeys) break;
        current = source;
    }
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
    for (const auto* key : {"aws_access_key_id", "aws_secret_access_key", "aws_session_token",
                            "sso_start_url", "sso_region", "sso_account_id", "sso_role_name"})
        if (fields.contains(key)) selected.fields.insert(key, fields.value(key));
    return selected;
}

Result<std::optional<EksLogin>> nativeEksConfig(const ExecCommand& command, const std::optional<AwsProfile>& importedProfile) {
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
        if (key != "--region" && key != "--cluster-name" && key != "--cluster-id" && key != "--output" && key != "--profile" && key != "--role-arn")
            return Failure{StoreError::UnsupportedVersion, "This EKS credential option is not supported by native sign-in."};
        const auto value = equals >= 0 ? argument.mid(equals + 1) : (++i < command.arguments.size() ? command.arguments[i] : QString{});
        if (value.isEmpty() || value.startsWith("--") || value.contains(QChar::Null) || options.contains(key))
            return Failure{StoreError::InvalidData, "An EKS credential option is missing, duplicated or invalid."};
        options.insert(key, value);
    }
    if (!action) return std::optional<EksLogin>{};
    const auto profileName = awsProfileName(command);
    if (const auto* failure = std::get_if<Failure>(&profileName)) return *failure;
    if (!importedProfile && (options.contains("--profile") || command.environment.contains("AWS_DEFAULT_PROFILE") || command.environment.contains("AWS_PROFILE")))
        return Failure{StoreError::InvalidData, "Explicitly import this AWS profile through Sources before native sign-in. No CLI was run."};
    if (options.contains("--output") && options.value("--output") != "json")
        return Failure{StoreError::UnsupportedVersion, "Native EKS only supports JSON credential output."};
    const auto cluster = options.contains("--cluster-id") ? options.value("--cluster-id") : options.value("--cluster-name");
    const auto region = options.value("--region", command.environment.value("AWS_REGION",
        command.environment.value("AWS_DEFAULT_REGION", importedProfile ? importedProfile->fields.value("region") : QString{})));
    static const QRegularExpression clusterName("^[A-Za-z0-9][A-Za-z0-9_.-]{0,127}$");
    static const QRegularExpression regionName("^[a-z0-9]+(?:-[a-z0-9]+)+-[0-9]+$");
    if (options.contains("--cluster-id") && options.contains("--cluster-name"))
        return Failure{StoreError::InvalidData, "Choose one EKS cluster name or cluster ID."};
    if (!clusterName.match(cluster).hasMatch() || region.size() > 64 || !regionName.match(region).hasMatch())
        return Failure{StoreError::InvalidData, "Native EKS requires a valid cluster and region."};
    const bool imported = command.environment.contains("AWS_ACCESS_KEY_ID") || command.environment.contains("AWS_SECRET_ACCESS_KEY")
        || command.environment.contains("AWS_SESSION_TOKEN");
    const auto credential = [&](const char* key) {
        if (importedProfile) return importedProfile->fields.value(QString::fromLatin1(key).toLower()).toUtf8();
        return imported ? command.environment.value(QString::fromLatin1(key)).toUtf8() : qgetenv(key);
    };
    EksLogin login{cluster, region, credential("AWS_ACCESS_KEY_ID"), credential("AWS_SECRET_ACCESS_KEY"), credential("AWS_SESSION_TOKEN"), {}, {}, importedProfile ? importedProfile->roles : QList<AwsRole>{}};
    if (options.contains("--role-arn")) login.roles.append(AwsRole{options.value("--role-arn"), {}, {}, {}});
    for (const auto& role : login.roles) if (!validRole(role)) return Failure{StoreError::InvalidData, "AWS role settings are invalid."};
    if (importedProfile && (importedProfile->fields.contains("sso_start_url") || importedProfile->fields.contains("sso_region")
        || importedProfile->fields.contains("sso_account_id") || importedProfile->fields.contains("sso_role_name"))) {
        const auto& fields = importedProfile->fields;
        AwsSso sso{QUrl(fields.value("sso_start_url"), QUrl::StrictMode), fields.value("sso_region"), fields.value("sso_account_id"), fields.value("sso_role_name")};
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
    if (!eks.roles.isEmpty()) return Failure{StoreError::InvalidInput, "Complete the configured AWS role sign-in before requesting an EKS token."};
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
    const auto signature = hmac(signingKey(eks, day), toSign).toHex();
    const auto url = "https://" + host + "/?" + query + "&X-Amz-Signature=" + signature;
    connection.authorization = "Bearer k8s-aws-v1." + url.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
    connection.expiresAt = utc.addSecs(840);
    if (eks.credentialExpiresAt.isValid()) connection.expiresAt = std::min(connection.expiresAt, eks.credentialExpiresAt);
    connection.credentialReady = true;
    connection.exec->eks.reset();
    for (const auto* name : {"AWS_ACCESS_KEY_ID", "AWS_SECRET_ACCESS_KEY", "AWS_SESSION_TOKEN"}) connection.exec->environment.remove(name);
    return connection;
}

Result<AwsRoleRequest> nativeAwsRoleRequest(const EksLogin& source, const AwsRole& role, const QString& code, QDateTime now) {
    static const QRegularExpression region("\\A[a-z0-9]+(?:-[a-z0-9]+)+-[0-9]+\\z"), otp("\\A[0-9]{6}\\z");
    if (!now.isValid() || !validRole(role) || role.sessionName.isEmpty() || source.region.size() > 64 || !region.match(source.region).hasMatch()
        || !printable(source.accessKey) || source.accessKey.size() > 128 || !printable(source.secretKey) || source.secretKey.size() > 128
        || (!source.sessionToken.isEmpty() && (!printable(source.sessionToken) || source.sessionToken.size() > 8192))
        || (source.credentialExpiresAt.isValid() && source.credentialExpiresAt <= now.addSecs(30))
        || (role.mfaSerial.isEmpty() ? !code.isEmpty() : !otp.match(code).hasMatch()))
        return Failure{StoreError::InvalidInput, "AWS role credentials, settings or MFA code are invalid or expired."};
    const auto utc = now.toUTC();
    const auto day = utc.toString("yyyyMMdd").toUtf8(), stamp = utc.toString("yyyyMMdd'T'HHmmss'Z'").toUtf8();
    const auto host = "sts." + source.region.toUtf8() + (source.region.startsWith("cn-") ? ".amazonaws.com.cn" : ".amazonaws.com");
    AwsRoleRequest result{QUrl("https://" + QString::fromLatin1(host) + "/"), {}, {}};
    QMap<QString, QString> fields{{"Action", "AssumeRole"}, {"Version", "2011-06-15"}, {"RoleArn", role.arn},
        {"RoleSessionName", role.sessionName}, {"DurationSeconds", QString::number(role.durationSeconds)}};
    if (!role.externalId.isEmpty()) fields.insert("ExternalId", role.externalId);
    if (!role.mfaSerial.isEmpty()) { fields.insert("SerialNumber", role.mfaSerial); fields.insert("TokenCode", code); }
    for (auto it = fields.cbegin(); it != fields.cend(); ++it) {
        if (!result.body.isEmpty()) result.body += '&';
        result.body += encoded(it.key()) + '=' + encoded(it.value());
    }
    result.headers = {{"content-type", "application/x-www-form-urlencoded; charset=utf-8"}, {"host", host}, {"x-amz-date", stamp}};
    if (!source.sessionToken.isEmpty()) result.headers.insert("x-amz-security-token", source.sessionToken);
    QByteArray canonicalHeaders, signedHeaders;
    for (auto it = result.headers.cbegin(); it != result.headers.cend(); ++it) {
        canonicalHeaders += it.key() + ':' + it.value() + '\n';
        if (!signedHeaders.isEmpty()) signedHeaders += ';';
        signedHeaders += it.key();
    }
    const auto canonical = "POST\n/\n\n" + canonicalHeaders + '\n' + signedHeaders + '\n'
        + QCryptographicHash::hash(result.body, QCryptographicHash::Sha256).toHex();
    const auto scope = day + '/' + source.region.toUtf8() + "/sts/aws4_request";
    const auto toSign = "AWS4-HMAC-SHA256\n" + stamp + '\n' + scope + '\n'
        + QCryptographicHash::hash(canonical, QCryptographicHash::Sha256).toHex();
    result.headers.insert("authorization", "AWS4-HMAC-SHA256 Credential=" + source.accessKey + '/' + scope
        + ", SignedHeaders=" + signedHeaders + ", Signature=" + hmac(signingKey(source, day), toSign).toHex());
    return result;
}

Result<AwsTemporaryCredentials> nativeAwsRoleCredentials(const QByteArray& bytes, QDateTime now) {
    const auto rejected = []() -> Result<AwsTemporaryCredentials> { return Failure{StoreError::InvalidData, "AWS returned invalid role credentials. Confirm another sign-in attempt."}; };
    if (bytes.isEmpty() || bytes.size() > 65536 || !now.isValid()) return rejected();
    QXmlStreamReader xml(bytes);
    xml.setEntityExpansionLimit(0);
    QStringList path;
    QMap<QString, QString> values;
    int roots = 0, results = 0, credentials = 0;
    while (!xml.atEnd()) {
        const auto token = xml.readNext();
        if (token == QXmlStreamReader::DTD || token == QXmlStreamReader::EntityReference) return rejected();
        if (token == QXmlStreamReader::StartElement) {
            path.append(xml.name().toString());
            if (path.size() > 8 || xml.namespaceUri() != "https://sts.amazonaws.com/doc/2011-06-15/") return rejected();
            if (path.size() == 1 && (path.first() != "AssumeRoleResponse" || ++roots != 1)) return rejected();
            if (path == QStringList{"AssumeRoleResponse", "AssumeRoleResult"} && ++results != 1) return rejected();
            if (path == QStringList{"AssumeRoleResponse", "AssumeRoleResult", "Credentials"} && ++credentials != 1) return rejected();
            if (path.size() == 4 && path[1] == "AssumeRoleResult" && path[2] == "Credentials") {
                const auto name = path.last();
                if (values.contains(name) || !QStringList{"AccessKeyId", "SecretAccessKey", "SessionToken", "Expiration"}.contains(name)) return rejected();
                values.insert(name, xml.readElementText(QXmlStreamReader::ErrorOnUnexpectedElement));
                path.removeLast();
            }
        } else if (token == QXmlStreamReader::EndElement && !path.isEmpty()) path.removeLast();
    }
    AwsTemporaryCredentials result{values.value("AccessKeyId").toLatin1(), values.value("SecretAccessKey").toLatin1(), values.value("SessionToken").toLatin1(),
        QDateTime::fromString(values.value("Expiration"), Qt::ISODateWithMs)};
    if (xml.hasError() || roots != 1 || results != 1 || credentials != 1 || values.size() != 4 || !path.isEmpty()
        || QString::fromLatin1(result.accessKey) != values.value("AccessKeyId") || QString::fromLatin1(result.secretKey) != values.value("SecretAccessKey")
        || QString::fromLatin1(result.sessionToken) != values.value("SessionToken")
        || !printable(result.accessKey) || result.accessKey.size() > 128 || !printable(result.secretKey) || result.secretKey.size() > 128
        || !printable(result.sessionToken) || result.sessionToken.size() > 8192
        || !values.value("Expiration").endsWith('Z') || !result.expiresAt.isValid() || result.expiresAt <= now.addSecs(30) || result.expiresAt > now.addDays(1)) return rejected();
    return result;
}
} // namespace podlord
