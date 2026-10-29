#include "kubeconfig_store.h"
#include "native_eks.h"
#include "native_gke.h"
#include "native_oci.h"
#include "native_oidc.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLockFile>
#include <QMap>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QSslKey>
#include <QSslSocket>
#include <QUrl>
#include <yaml-cpp/yaml.h>
#include <algorithm>

namespace podlord {
namespace {
constexpr qint64 maximumKubeconfigBytes = 16 * 1024 * 1024;
bool generatedSource(const QString& path) {
    // Historical virtual origins are stored identities, not executable providers.
    static const QRegularExpression pattern("\\Apodlord-generated://[A-Za-z0-9][A-Za-z0-9_.-]{0,127}/[A-Za-z0-9][A-Za-z0-9_.-]{0,127}\\z");
    return pattern.match(path).hasMatch();
}
bool contentSource(const QString& path) {
    return path.startsWith("content://", Qt::CaseInsensitive);
}
QString hash(const QByteArray& bytes) {
    return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}
QString snapshotId(const QString& path, const QString& contentHash) {
    return hash(path.toUtf8()) + "-" + contentHash;
}
Result<QString> sourcePath(QString path) {
    if (path.trimmed().isEmpty() || path.contains(QChar::Null))
        return Failure{StoreError::InvalidInput, "A source path is required."};
    if (contentSource(path)) {
        const QUrl url(path, QUrl::StrictMode);
        if (!url.isValid() || url.scheme().compare("content", Qt::CaseInsensitive) != 0
            || url.host().isEmpty() || url.path().isEmpty() || url.path().contains(QChar::Null)
            || !url.userInfo().isEmpty() || url.port() != -1 || url.hasFragment())
            return Failure{StoreError::InvalidInput, "A valid content URI with a provider and document path is required."};
        return path;
    }
    if (path.startsWith("file://", Qt::CaseInsensitive)) {
        const QUrl url(QString(path).replace(' ', "%20"), QUrl::StrictMode);
        path = url.toLocalFile();
        if (!url.isValid() || url.hasQuery() || url.hasFragment()
            || !url.userInfo().isEmpty() || url.port() != -1
            || !QDir::isAbsolutePath(path) || path.contains(QChar::Null))
            return Failure{StoreError::InvalidInput, "A valid absolute file URL without credentials, query or fragment is required."};
    }
    if (path == "~") path = QDir::homePath();
    else if (path.startsWith("~/")) path = QDir(QDir::homePath()).filePath(path.mid(2));
    else if (path.startsWith('~'))
        return Failure{StoreError::InvalidInput, "Only ~ and ~/ home paths are supported."};
    return QDir::cleanPath(QFileInfo(path).absoluteFilePath());
}
QString text(const YAML::Node& node, const char* key) {
    const auto value = node[key];
    if (!value || value.IsNull()) return {};
    if (!value.IsScalar()) throw YAML::BadConversion(value.Mark());
    return QString::fromStdString(value.as<std::string>());
}
struct NamedEntries final {
    QMap<QString, YAML::Node> values;
    QSet<QString> duplicates;
};
NamedEntries entries(const YAML::Node& root, const char* key, const char* payload, QStringList& warnings) {
    NamedEntries result;
    const auto list = root[key];
    if (!list || list.IsNull()) return result;
    if (!list.IsSequence()) throw YAML::BadConversion(list.Mark());
    for (const auto& entry : list) {
        if (!entry.IsMap()) throw YAML::BadConversion(entry.Mark());
        const QString name = text(entry, "name");
        const auto value = entry[payload];
        if (name.trimmed().isEmpty() || !value || !value.IsMap()) throw YAML::BadConversion(entry.Mark());
        if (result.values.contains(name)) {
            warnings.append(QString("Duplicate %1 name: %2").arg(key, name));
            result.duplicates.insert(name);
            continue;
        }
        result.values.insert(name, value);
    }
    return result;
}
Result<SourceSnapshot> parse(QString path, const QByteArray& bytes, QDateTime importedAt) {
    if (bytes.size() > maximumKubeconfigBytes)
        return Failure{StoreError::InvalidData, "Kubeconfig exceeds the 16 MiB import limit."};
    const auto content = bytes.startsWith("\xEF\xBB\xBF") ? bytes.mid(3) : bytes;
    if (bytes.isEmpty() || QString::fromUtf8(content).toUtf8() != content)
        return Failure{StoreError::InvalidData, "Kubeconfig must contain UTF-8 YAML."};
    SourceSnapshot result;
    result.sourcePath = std::move(path);
    result.contentHash = hash(bytes);
    result.id = snapshotId(result.sourcePath, result.contentHash);
    result.importedAt = importedAt;
    try {
        const auto documents = YAML::LoadAll(bytes.toStdString());
        if (documents.size() != 1 || !documents[0].IsMap())
            return Failure{StoreError::InvalidData, "Kubeconfig must contain one mapping document."};
        const auto& root = documents[0];
        const QString version = text(root, "apiVersion");
        const QString kind = text(root, "kind");
        if ((!version.isEmpty() && version != "v1") || (!kind.isEmpty() && kind != "Config"))
            return Failure{StoreError::InvalidData, "Unsupported kubeconfig kind or API version."};
        const auto clusters = entries(root, "clusters", "cluster", result.warnings);
        const auto users = entries(root, "users", "user", result.warnings);
        const auto contexts = entries(root, "contexts", "context", result.warnings);
        if (contexts.values.isEmpty()) return Failure{StoreError::InvalidData, "Kubeconfig contains no contexts."};
        const auto selected = text(root, "current-context");
        if (!selected.isEmpty() && !contexts.values.contains(selected)) result.warnings.append("Current context does not exist.");
        for (auto it = contexts.values.cbegin(); it != contexts.values.cend(); ++it) {
            SourceContext context;
            context.id = "ctx:" + hash(result.id.toUtf8() + '\0' + it.key().toUtf8());
            context.name = it.key();
            context.displayName = context.name;
            context.cluster = text(it.value(), "cluster");
            context.user = text(it.value(), "user");
            context.nameSpace = text(it.value(), "namespace");
            context.authType = "unknown";
            if (contexts.duplicates.contains(it.key())) context.brokenReferences.append("duplicate-context");
            if (clusters.duplicates.contains(context.cluster)) context.brokenReferences.append("duplicate-cluster");
            if (users.duplicates.contains(context.user)) context.brokenReferences.append("duplicate-user");
            if (context.cluster.isEmpty() || !clusters.values.contains(context.cluster)) context.brokenReferences.append("cluster");
            else {
                const auto cluster = clusters.values.value(context.cluster);
                QUrl url(text(cluster, "server"));
                if (generatedSource(result.sourcePath) && (url.host() == "0.0.0.0" || url.host() == "localhost"))
                    url.setHost("127.0.0.1");
                if (!url.isValid() || url.host().isEmpty() || (url.scheme() != "http" && url.scheme() != "https")
                    || !url.userInfo().isEmpty() || url.hasQuery() || url.hasFragment()) context.brokenReferences.append("server");
                context.server = url.toString(QUrl::RemoveUserInfo | QUrl::RemoveQuery | QUrl::RemoveFragment);
            }
            if (!context.user.isEmpty() && !users.values.contains(context.user)) context.brokenReferences.append("user");
            else if (!context.user.isEmpty()) {
                const auto user = users.values.value(context.user);
                if (user["exec"]) {
                    if (!user["exec"].IsMap()) throw YAML::BadConversion(user["exec"].Mark());
                    context.authType = "exec";
                } else if (user["auth-provider"]) {
                    if (!user["auth-provider"].IsMap()) throw YAML::BadConversion(user["auth-provider"].Mark());
                    context.authType = "auth-provider:" + text(user["auth-provider"], "name");
                } else if (!text(user, "token").isEmpty() || !text(user, "tokenFile").isEmpty()) context.authType = "token";
                else if (!text(user, "client-certificate-data").isEmpty() || !text(user, "client-certificate").isEmpty()) context.authType = "client-certificate";
                else if (!text(user, "username").isEmpty()) context.authType = "basic";
            }
            result.contexts.append(std::move(context));
        }
        return result;
    } catch (const YAML::Exception&) {
        return Failure{StoreError::InvalidData, "Invalid kubeconfig YAML or field types."};
    }
}
Result<SourceSnapshot> readOwned(const QString& filePath, QByteArray* originalOut = nullptr) {
    if (QFileInfo(filePath).isSymLink()) return Failure{StoreError::InvalidData, "Owned snapshot cannot be a symlink."};
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) return Failure{StoreError::ReadFailed, "Cannot read owned snapshot."};
    const auto bytes = file.read(32 * 1024 * 1024 + 1);
    if (bytes.size() > 32 * 1024 * 1024)
        return Failure{StoreError::InvalidData, "Owned kubeconfig snapshot exceeds the storage limit."};
    if (file.error() != QFileDevice::NoError) return Failure{StoreError::ReadFailed, "Cannot read owned snapshot."};
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(bytes, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject())
        return Failure{StoreError::InvalidData, "Invalid owned snapshot; original data retained."};
    const auto row = document.object();
    const int version = row["version"].toInt();
    if (row["version"].isDouble() && version != 1 && version != 2)
        return Failure{StoreError::UnsupportedVersion, "Unsupported owned snapshot version."};
    QSet<QString> expected{"version", "sourcePath", "contentHash", "yamlBase64", "importedAt"};
    if (version == 2) expected.insert("contextAliases");
    const auto keys = row.keys();
    if (QSet<QString>(keys.cbegin(), keys.cend()) != expected || !row["version"].isDouble())
        return Failure{StoreError::InvalidData, "Invalid owned snapshot fields."};
    if (row["version"].toDouble() != version) return Failure{StoreError::InvalidData, "Invalid owned snapshot version."};
    for (const auto& key : QStringList{"sourcePath", "contentHash", "yamlBase64", "importedAt"})
        if (!row[key].isString()) return Failure{StoreError::InvalidData, "Invalid owned snapshot field type."};
    const QString path = row["sourcePath"].toString();
    const QDateTime time = QDateTime::fromString(row["importedAt"].toString(), Qt::ISODateWithMs);
    const auto canonical = sourcePath(path);
    if ((!generatedSource(path) && (!std::holds_alternative<QString>(canonical) || std::get<QString>(canonical) != path)) || !time.isValid())
        return Failure{StoreError::InvalidData, "Invalid owned source identity or import time."};
    const auto encoded = row["yamlBase64"].toString();
    const auto original = QByteArray::fromBase64(encoded.toLatin1());
    if (QString::fromLatin1(original.toBase64()) != encoded)
        return Failure{StoreError::InvalidData, "Invalid owned snapshot encoding."};
    auto result = parse(path, original, time);
    if (std::holds_alternative<Failure>(result)) return result;
    auto& source = std::get<SourceSnapshot>(result);
    if (source.contentHash != row["contentHash"].toString() || QFileInfo(filePath).fileName() != source.id + ".json")
        return Failure{StoreError::InvalidData, "Owned snapshot identity or content hash mismatch."};
    if (version == 2) {
        if (!row["contextAliases"].isObject()) return Failure{StoreError::InvalidData, "Invalid source display aliases."};
        const auto aliases = row["contextAliases"].toObject();
        for (auto alias = aliases.begin(); alias != aliases.end(); ++alias) {
            auto context = std::find_if(source.contexts.begin(), source.contexts.end(), [&](const auto& value) { return value.id == alias.key(); });
            const auto name = alias.value().toString();
            if (context == source.contexts.end() || !alias.value().isString() || name.isEmpty() || name.size() > 512
                || name != name.trimmed() || std::any_of(name.cbegin(), name.cend(), [](QChar c) { return c.category() == QChar::Other_Control; }))
                return Failure{StoreError::InvalidData, "Invalid source display alias."};
            context->displayName = name;
        }
    }
    source.ownedPath = filePath;
    if (originalOut) *originalOut = original;
    return result;
}
Result<SourceSnapshot> publishSource(const SourceSnapshot& source, const QByteArray& bytes) {
    QJsonObject aliases;
    for (const auto& context : source.contexts)
        if (context.displayName != context.name) aliases.insert(context.id, context.displayName);
    QJsonObject record{{"version", aliases.isEmpty() ? 1 : 2}, {"sourcePath", source.sourcePath}, {"contentHash", source.contentHash},
        {"yamlBase64", QString::fromLatin1(bytes.toBase64())}, {"importedAt", source.importedAt.toUTC().toString(Qt::ISODateWithMs)}};
    if (!aliases.isEmpty()) record.insert("contextAliases", aliases);
    QSaveFile owned(source.ownedPath);
    if (!owned.open(QIODevice::WriteOnly) || !owned.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner))
        return Failure{StoreError::WriteFailed, "Cannot create protected owned snapshot."};
    const auto json = QJsonDocument(record).toJson();
    if (owned.write(json) != json.size() || !owned.commit())
        return Failure{StoreError::WriteFailed, "Cannot publish owned snapshot; previous data retained."};
    return source;
}
Result<QString> sourceDirectory(const QString& profile, bool create) {
    if (const auto failure = profileFailure(profile)) return *failure;
    const QString directory = QDir(profile).filePath("kubeconfigs");
    if (QFileInfo(directory).isSymLink() || (QFileInfo(directory).exists() && !QFileInfo(directory).isDir()))
        return Failure{StoreError::InvalidData, "Owned kubeconfig directory is invalid."};
    if (!create) return directory;
    const bool exists = QFileInfo(profile).exists();
    if (!QDir().mkpath(directory) || (!exists && !QFile::setPermissions(profile,
        QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner))
        || !QFile::setPermissions(directory, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner))
        return Failure{StoreError::WriteFailed, "Cannot create protected kubeconfig directory."};
    return QFileInfo(directory).canonicalFilePath();
}
QString ociCredentialPath(const QString& directory, const QString& contextId) {
    return QDir(directory).filePath("oci-" + contextId.mid(4) + ".cred");
}
QString gkeCredentialPath(const QString& directory, const QString& contextId) {
    return QDir(directory).filePath("gke-" + contextId.mid(4) + ".cred");
}
Result<QByteArray> readGkeImport(QString path) {
    const auto normalized = sourcePath(std::move(path));
    if (const auto* failure = std::get_if<Failure>(&normalized)) return *failure;
    path = std::get<QString>(normalized);
    if (!contentSource(path) && !QFileInfo(path).isFile())
        return Failure{StoreError::ReadFailed, "Select a readable regular Google service-account JSON key."};
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return Failure{StoreError::ReadFailed, "Cannot read the selected Google service-account key."};
    const auto bytes = file.read(65537);
    if (file.error() != QFileDevice::NoError || bytes.isEmpty() || bytes.size() > 65536)
        return Failure{StoreError::InvalidData, "The selected Google service-account key is empty or exceeds 64 KiB."};
    return bytes;
}
Result<QByteArray> readOciImport(QString path, qint64 limit) {
    const auto normalized = sourcePath(std::move(path));
    if (const auto* failure = std::get_if<Failure>(&normalized)) return *failure;
    path = std::get<QString>(normalized);
    if (!contentSource(path) && !QFileInfo(path).isFile())
        return Failure{StoreError::ReadFailed, "Select a readable regular OCI config or PEM file."};
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return Failure{StoreError::ReadFailed, "Cannot read the selected OCI file."};
    const auto bytes = file.read(limit + 1);
    if (file.error() != QFileDevice::NoError) return Failure{StoreError::ReadFailed, "Cannot read the selected OCI file."};
    if (bytes.isEmpty() || bytes.size() > limit) return Failure{StoreError::InvalidData, "The selected OCI file is empty or exceeds its size limit."};
    return bytes;
}
Result<QString> prepareImportedOci(ExecCommand& command) {
    QString profile = command.environment.take("OCI_CLI_PROFILE");
    QStringList arguments;
    for (int i = 0; i < command.arguments.size(); ++i) {
        const auto argument = command.arguments[i];
        if (argument == "--profile" || argument.startsWith("--profile=")) {
            if (!profile.isEmpty()) return Failure{StoreError::InvalidData, "OCI profile is selected more than once."};
            profile = argument == "--profile" ? (i + 1 < command.arguments.size() ? command.arguments[++i] : QString{}) : argument.mid(10);
            if (profile.isEmpty() || profile.startsWith("--")) return Failure{StoreError::InvalidData, "OCI profile name is missing."};
        } else arguments.append(argument);
    }
    if (profile.isEmpty()) profile = "DEFAULT";
    static const QRegularExpression profileName("\\A[A-Za-z0-9._-]{1,128}\\z");
    if (!profileName.match(profile).hasMatch()) return Failure{StoreError::InvalidData, "OCI profile name is invalid."};
    if (!command.environment.value("OCI_CLI_PASSPHRASE").isEmpty()
        || (!command.environment.value("OCI_CLI_AUTH").isEmpty() && command.environment.value("OCI_CLI_AUTH") != "api_key"))
        return Failure{StoreError::UnsupportedVersion, "Imported OCI keys require unencrypted API-key authentication."};
    command.arguments = arguments;
    command.environment.remove("OCI_CLI_KEY_FILE");
    command.environment.remove("OCI_CLI_CONFIG_FILE");
    command.environment.remove("OCI_CLI_PASSPHRASE");
    return profile;
}
Result<QMap<QString, QString>> parseOciConfig(const QByteArray& bytes, const QString& profile) {
    const auto content = QString::fromUtf8(bytes);
    if (content.toUtf8() != bytes) return Failure{StoreError::InvalidData, "OCI config must be UTF-8."};
    QMap<QString, QString> fields;
    QString section;
    for (const auto& raw : content.split('\n')) {
        const auto line = raw.trimmed();
        if (line.isEmpty() || line.startsWith('#') || line.startsWith(';')) continue;
        if (line.startsWith('[') && line.endsWith(']')) { section = line.mid(1, line.size() - 2).trimmed(); continue; }
        if (section != profile) continue;
        const auto equal = line.indexOf('=');
        if (equal < 1) return Failure{StoreError::InvalidData, "Selected OCI config profile is malformed."};
        const auto name = line.left(equal).trimmed().toLower();
        const auto value = line.mid(equal + 1).trimmed();
        if (fields.contains(name)) return Failure{StoreError::InvalidData, "Selected OCI config profile has duplicate fields."};
        fields.insert(name, value);
    }
    if (!fields.value("pass_phrase").isEmpty() || (!fields.value("auth").isEmpty() && fields.value("auth") != "api_key"))
        return Failure{StoreError::UnsupportedVersion, "Imported OCI keys require an unencrypted API key."};
    QMap<QString, QString> environment;
    for (const auto& pair : {qMakePair("user", "OCI_CLI_USER"), qMakePair("tenancy", "OCI_CLI_TENANCY"),
                             qMakePair("fingerprint", "OCI_CLI_FINGERPRINT"), qMakePair("region", "OCI_CLI_REGION")}) {
        if (fields.value(pair.first).isEmpty()) return Failure{StoreError::InvalidData, "Selected OCI config profile is missing required API-key fields."};
        environment.insert(QString::fromLatin1(pair.second), fields.value(pair.first));
    }
    return environment;
}
Result<std::optional<QMap<QString, QString>>> importedOciEnvironment(const QString& profile, const QString& contextId) {
    const auto directory = sourceDirectory(profile, false);
    if (const auto* failure = std::get_if<Failure>(&directory)) return *failure;
    const auto path = ociCredentialPath(std::get<QString>(directory), contextId);
    if (QFileInfo(path).isSymLink()) return Failure{StoreError::InvalidData, "Protected OCI credential cannot be a symlink."};
    if (!QFileInfo(path).exists()) return std::optional<QMap<QString, QString>>{};
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return Failure{StoreError::ReadFailed, "Cannot read protected OCI credential."};
    const auto bytes = file.read(65537);
    if (bytes.size() > 65536 || file.error() != QFileDevice::NoError)
        return Failure{StoreError::InvalidData, "Protected OCI credential is invalid."};
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(bytes, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject())
        return Failure{StoreError::InvalidData, "Protected OCI credential is invalid."};
    const auto record = document.object();
    const QSet<QString> expected{"version", "OCI_CLI_USER", "OCI_CLI_TENANCY", "OCI_CLI_FINGERPRINT", "OCI_CLI_REGION", "OCI_CLI_KEY_CONTENT"};
    const auto keys = record.keys();
    if (record["version"].toInt() != 1 || QSet<QString>(keys.cbegin(), keys.cend()) != expected)
        return Failure{StoreError::InvalidData, "Protected OCI credential is invalid."};
    QMap<QString, QString> environment;
    for (const auto& name : expected) {
        if (name == "version") continue;
        if (!record[name].isString()) return Failure{StoreError::InvalidData, "Protected OCI credential is invalid."};
        environment.insert(name, record[name].toString());
    }
    return std::optional<QMap<QString, QString>>{environment};
}
Result<std::optional<GkeServiceAccount>> importedGkeAccount(const QString& profile, const QString& contextId) {
    const auto directory = sourceDirectory(profile, false);
    if (const auto* failure = std::get_if<Failure>(&directory)) return *failure;
    const auto path = gkeCredentialPath(std::get<QString>(directory), contextId);
    if (QFileInfo(path).isSymLink()) return Failure{StoreError::InvalidData, "Protected Google service-account key cannot be a symlink."};
    if (!QFileInfo(path).exists()) return std::optional<GkeServiceAccount>{};
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return Failure{StoreError::ReadFailed, "Cannot read protected Google service-account key."};
    const auto bytes = file.read(65537);
    QJsonParseError error;
    auto document = QJsonDocument::fromJson(bytes, &error);
    if (bytes.size() > 65536 || file.error() != QFileDevice::NoError || error.error != QJsonParseError::NoError || !document.isObject())
        return Failure{StoreError::InvalidData, "Protected Google service-account key is invalid."};
    auto record = document.object();
    const QSet<QString> expected{"version", "type", "client_email", "private_key_id", "private_key", "token_uri"};
    const auto keys = record.keys();
    if (record.take("version").toInt() != 1 || QSet<QString>(keys.cbegin(), keys.cend()) != expected)
        return Failure{StoreError::InvalidData, "Protected Google service-account key is invalid."};
    const auto parsed = parseGkeServiceAccount(QJsonDocument(record).toJson(QJsonDocument::Compact));
    if (const auto* failure = std::get_if<Failure>(&parsed)) return *failure;
    return std::optional<GkeServiceAccount>{std::get<GkeServiceAccount>(parsed)};
}
} // namespace

KubeconfigStore::KubeconfigStore(QString profile) : profile_(std::move(profile)) {}
Result<SourceSnapshot> KubeconfigStore::importFile(QString path) const {
    const auto normalized = sourcePath(std::move(path));
    if (const auto* failure = std::get_if<Failure>(&normalized)) return Failure{failure->code, failure->message, true};
    path = std::get<QString>(normalized);
    if (!contentSource(path) && !QFileInfo(path).isFile()) return Failure{StoreError::ReadFailed, "Source must be a readable regular kubeconfig file.", true};
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return Failure{StoreError::ReadFailed, "Cannot read kubeconfig source.", true};
    const auto bytes = file.read(maximumKubeconfigBytes + 1);
    if (file.error() != QFileDevice::NoError) return Failure{StoreError::ReadFailed, "Cannot read kubeconfig source.", true};
    return importContent(std::move(path), bytes);
}
Result<SourceSnapshot> KubeconfigStore::importText(QString originPath, const QString& yaml) const {
    if (!QDir::isAbsolutePath(originPath) && !originPath.startsWith("~/")
        && QUrl(originPath).scheme().compare("file", Qt::CaseInsensitive) != 0)
        return Failure{StoreError::InvalidInput, "Pasted kubeconfig origin must be an absolute local path or file URL."};
    if (yaml.size() > maximumKubeconfigBytes)
        return Failure{StoreError::InvalidData, "Kubeconfig exceeds the 16 MiB import limit."};
    const auto normalized = sourcePath(std::move(originPath));
    if (const auto* failure = std::get_if<Failure>(&normalized)) return *failure;
    return importContent(std::get<QString>(normalized), yaml.toUtf8());
}
Result<SourceSnapshot> KubeconfigStore::importOciCredentials(const QString& contextId, QString configPath, QString keyPath) const {
    static const QRegularExpression identifier("\\Actx:[0-9a-f]{64}\\z");
    if (!identifier.match(contextId).hasMatch()) return Failure{StoreError::InvalidInput, "Select a valid imported context."};
    const auto config = readOciImport(std::move(configPath), 65536);
    if (const auto* failure = std::get_if<Failure>(&config)) return *failure;
    const auto key = readOciImport(std::move(keyPath), 32768);
    if (const auto* failure = std::get_if<Failure>(&key)) return *failure;
    if (QSslKey(std::get<QByteArray>(key), QSsl::Rsa).length() < 2048)
        return Failure{StoreError::InvalidData, "Select an unencrypted OCI RSA PEM key of at least 2048 bits."};
    const auto directory = sourceDirectory(profile_, true);
    if (const auto* failure = std::get_if<Failure>(&directory)) return *failure;
    QLockFile lock(QDir(std::get<QString>(directory)).filePath("sources.lock"));
    lock.setStaleLockTime(0);
    if (!lock.tryLock(0)) return Failure{lock.error() == QLockFile::LockFailedError ? StoreError::Busy : StoreError::WriteFailed,
        "Cannot lock owned kubeconfigs; OCI credential was not imported."};
    const auto catalog = list();
    if (const auto* failure = std::get_if<Failure>(&catalog)) return *failure;
    for (const auto& source : std::get<SourceCatalog>(catalog).sources) for (const auto& context : source.contexts) {
        if (context.id != contextId) continue;
        QByteArray original;
        const auto owned = readOwned(source.ownedPath, &original);
        if (const auto* failure = std::get_if<Failure>(&owned)) return *failure;
        try {
            const auto root = YAML::Load(original.toStdString());
            QStringList warnings;
            const auto users = entries(root, "users", "user", warnings);
            const auto user = users.values.value(context.user);
            const auto exec = user["exec"];
            if (!exec || !exec.IsMap()) return Failure{StoreError::InvalidInput, "Select an OCI exec context."};
            ExecCommand command;
            command.program = text(exec, "command");
            const auto args = exec["args"];
            if (!args || !args.IsSequence()) return Failure{StoreError::InvalidData, "OCI exec arguments are invalid."};
            for (const auto& arg : args) {
                if (!arg.IsScalar()) return Failure{StoreError::InvalidData, "OCI exec arguments are invalid."};
                command.arguments.append(QString::fromStdString(arg.as<std::string>()));
            }
            const auto env = exec["env"];
            if (env && !env.IsSequence()) return Failure{StoreError::InvalidData, "OCI exec environment is invalid."};
            if (env) for (const auto& item : env) command.environment.insert(text(item, "name"), text(item, "value"));
            const auto selected = prepareImportedOci(command);
            if (const auto* failure = std::get_if<Failure>(&selected)) return *failure;
            const auto fields = parseOciConfig(std::get<QByteArray>(config), std::get<QString>(selected));
            if (const auto* failure = std::get_if<Failure>(&fields)) return *failure;
            for (auto setting = std::get<QMap<QString, QString>>(fields).cbegin(); setting != std::get<QMap<QString, QString>>(fields).cend(); ++setting)
                command.environment.insert(setting.key(), setting.value());
            command.environment.insert("OCI_CLI_KEY_CONTENT", QString::fromUtf8(std::get<QByteArray>(key)));
            const auto parsed = nativeOciConfig(command);
            if (const auto* failure = std::get_if<Failure>(&parsed)) return *failure;
            if (!std::get<std::optional<OciLogin>>(parsed)) return Failure{StoreError::InvalidInput, "Select an OCI OKE exec context."};
            const auto path = ociCredentialPath(std::get<QString>(directory), contextId);
            if (QFileInfo(path).isSymLink()) return Failure{StoreError::InvalidData, "Protected OCI credential cannot be a symlink."};
            QJsonObject record{{"version", 1}, {"OCI_CLI_KEY_CONTENT", command.environment.value("OCI_CLI_KEY_CONTENT")}};
            for (const auto& name : {"OCI_CLI_USER", "OCI_CLI_TENANCY", "OCI_CLI_FINGERPRINT", "OCI_CLI_REGION"})
                record.insert(QString::fromLatin1(name), command.environment.value(QString::fromLatin1(name)));
            QSaveFile saved(path);
            if (!saved.open(QIODevice::WriteOnly) || !saved.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner))
                return Failure{StoreError::WriteFailed, "Cannot create protected OCI credential."};
            const auto bytes = QJsonDocument(record).toJson();
            if (saved.write(bytes) != bytes.size() || !saved.commit())
                return Failure{StoreError::WriteFailed, "Cannot publish protected OCI credential; previous data retained."};
            return source;
        } catch (const YAML::Exception&) {
            return Failure{StoreError::InvalidData, "OCI exec configuration is invalid."};
        }
    }
    return Failure{StoreError::NotFound, "Imported context is unavailable."};
}
Result<bool> KubeconfigStore::hasOciCredential(const QString& contextId) const {
    static const QRegularExpression identifier("\\Actx:[0-9a-f]{64}\\z");
    if (!identifier.match(contextId).hasMatch()) return Failure{StoreError::InvalidInput, "Select a valid imported context."};
    const auto directory = sourceDirectory(profile_, false);
    if (const auto* failure = std::get_if<Failure>(&directory)) return *failure;
    const QFileInfo credential(ociCredentialPath(std::get<QString>(directory), contextId));
    if (credential.isSymLink()) return Failure{StoreError::InvalidData, "Protected OCI credential cannot be a symlink."};
    return credential.exists();
}
Result<SourceSnapshot> KubeconfigStore::importGkeServiceAccount(const QString& contextId, QString keyPath) const {
    static const QRegularExpression identifier("\\Actx:[0-9a-f]{64}\\z");
    if (!identifier.match(contextId).hasMatch()) return Failure{StoreError::InvalidInput, "Select a valid imported context."};
    const auto bytes = readGkeImport(std::move(keyPath));
    if (const auto* failure = std::get_if<Failure>(&bytes)) return *failure;
    const auto account = parseGkeServiceAccount(std::get<QByteArray>(bytes));
    if (const auto* failure = std::get_if<Failure>(&account)) return *failure;
    const auto directory = sourceDirectory(profile_, true);
    if (const auto* failure = std::get_if<Failure>(&directory)) return *failure;
    QLockFile lock(QDir(std::get<QString>(directory)).filePath("sources.lock"));
    lock.setStaleLockTime(0);
    if (!lock.tryLock(0)) return Failure{lock.error() == QLockFile::LockFailedError ? StoreError::Busy : StoreError::WriteFailed,
        "Cannot lock owned kubeconfigs; Google service-account key was not imported."};
    const auto catalog = list();
    if (const auto* failure = std::get_if<Failure>(&catalog)) return *failure;
    for (const auto& source : std::get<SourceCatalog>(catalog).sources) for (const auto& context : source.contexts) {
        if (context.id != contextId) continue;
        QByteArray original;
        const auto owned = readOwned(source.ownedPath, &original);
        if (const auto* failure = std::get_if<Failure>(&owned)) return *failure;
        try {
            const auto root = YAML::Load(original.toStdString());
            QStringList warnings;
            const auto users = entries(root, "users", "user", warnings);
            const auto exec = users.values.value(context.user)["exec"];
            if (!exec || !exec.IsMap()) return Failure{StoreError::InvalidInput, "Select a GKE exec context."};
            ExecCommand command;
            command.program = text(exec, "command");
            const auto args = exec["args"];
            if (args && !args.IsSequence()) return Failure{StoreError::InvalidData, "GKE exec arguments are invalid."};
            if (args) for (const auto& arg : args) {
                if (!arg.IsScalar()) return Failure{StoreError::InvalidData, "GKE exec arguments are invalid."};
                command.arguments.append(QString::fromStdString(arg.as<std::string>()));
            }
            const auto parsed = nativeGkeConfig(command);
            if (const auto* failure = std::get_if<Failure>(&parsed)) return *failure;
            if (!std::get<std::optional<GkeLogin>>(parsed)) return Failure{StoreError::InvalidInput, "Select a GKE exec context."};
            const auto& value = std::get<GkeServiceAccount>(account);
            const auto path = gkeCredentialPath(std::get<QString>(directory), contextId);
            if (QFileInfo(path).isSymLink()) return Failure{StoreError::InvalidData, "Protected Google service-account key cannot be a symlink."};
            QJsonObject record{{"version", 1}, {"type", "service_account"}, {"client_email", value.clientEmail},
                {"private_key_id", value.privateKeyId}, {"private_key", QString::fromUtf8(value.privateKey)},
                {"token_uri", "https://oauth2.googleapis.com/token"}};
            QSaveFile saved(path);
            if (!saved.open(QIODevice::WriteOnly) || !saved.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner))
                return Failure{StoreError::WriteFailed, "Cannot create protected Google service-account key."};
            const auto stored = QJsonDocument(record).toJson();
            if (saved.write(stored) != stored.size() || !saved.commit())
                return Failure{StoreError::WriteFailed, "Cannot publish protected Google service-account key; previous data retained."};
            return source;
        } catch (const YAML::Exception&) {
            return Failure{StoreError::InvalidData, "GKE exec configuration is invalid."};
        }
    }
    return Failure{StoreError::NotFound, "Imported context is unavailable."};
}
Result<bool> KubeconfigStore::hasGkeServiceAccount(const QString& contextId) const {
    static const QRegularExpression identifier("\\Actx:[0-9a-f]{64}\\z");
    if (!identifier.match(contextId).hasMatch()) return Failure{StoreError::InvalidInput, "Select a valid imported context."};
    const auto directory = sourceDirectory(profile_, false);
    if (const auto* failure = std::get_if<Failure>(&directory)) return *failure;
    const QFileInfo credential(gkeCredentialPath(std::get<QString>(directory), contextId));
    if (credential.isSymLink()) return Failure{StoreError::InvalidData, "Protected Google service-account key cannot be a symlink."};
    return credential.exists();
}
Result<SourceSnapshot> KubeconfigStore::importContent(QString path, const QByteArray& bytes) const {
    auto parsed = parse(path, bytes, QDateTime::currentDateTimeUtc());
    if (auto* failure = std::get_if<Failure>(&parsed)) { failure->sourceInput = true; return parsed; }
    const auto directory = sourceDirectory(profile_, true);
    if (std::holds_alternative<Failure>(directory)) return std::get<Failure>(directory);
    QLockFile lock(QDir(std::get<QString>(directory)).filePath("sources.lock"));
    lock.setStaleLockTime(0);
    if (!lock.tryLock(0)) return Failure{lock.error() == QLockFile::LockFailedError ? StoreError::Busy : StoreError::WriteFailed,
        "Cannot lock owned kubeconfigs; import was not applied."};
    auto& source = std::get<SourceSnapshot>(parsed);
    source.ownedPath = QDir(std::get<QString>(directory)).filePath(source.id + ".json");
    const auto sessions = SessionStore(profile_).list();
    if (const auto* failure = std::get_if<Failure>(&sessions)) return *failure;
    if (QFileInfo(source.ownedPath).exists() || QFileInfo(source.ownedPath).isSymLink()) {
        const auto previous = readOwned(source.ownedPath);
        if (std::holds_alternative<Failure>(previous)) return std::get<Failure>(previous);
        source.contexts = std::get<SourceSnapshot>(previous).contexts;
        QStringList restored;
        for (const auto& context : source.contexts)
            if (std::get<SessionCatalog>(sessions).removedContexts.contains(context.id)) restored.append(context.id);
        if (!restored.isEmpty()) {
            const auto result = SessionStore(profile_).restoreContexts(restored);
            if (const auto* failure = std::get_if<Failure>(&result)) return *failure;
            // Reavailability and deleted-session retention need only the catalog commit.
            return previous;
        }
    }
    return publishSource(source, bytes);
}
Result<SourceSnapshot> KubeconfigStore::renameContext(const QString& contextId, QString displayName) const {
    displayName = displayName.trimmed();
    if (contextId.isEmpty() || displayName.size() > 512
        || std::any_of(displayName.cbegin(), displayName.cend(), [](QChar c) { return c.category() == QChar::Other_Control; }))
        return Failure{StoreError::InvalidInput, "Select a context and a display name of at most 512 characters without control characters."};
    const auto directory = sourceDirectory(profile_, false);
    if (const auto* failure = std::get_if<Failure>(&directory)) return *failure;
    if (!QDir(std::get<QString>(directory)).exists()) return Failure{StoreError::NotFound, "Source context is unavailable."};
    QLockFile lock(QDir(std::get<QString>(directory)).filePath("sources.lock"));
    lock.setStaleLockTime(0);
    if (!lock.tryLock(0)) return Failure{lock.error() == QLockFile::LockFailedError ? StoreError::Busy : StoreError::WriteFailed,
        "Cannot lock owned kubeconfigs; display name was not changed."};
    const auto catalog = list();
    if (const auto* failure = std::get_if<Failure>(&catalog)) return *failure;
    for (const auto& snapshot : std::get<SourceCatalog>(catalog).sources) {
        if (std::none_of(snapshot.contexts.cbegin(), snapshot.contexts.cend(), [&](const auto& context) { return context.id == contextId; })) continue;
        QByteArray bytes;
        auto loaded = readOwned(snapshot.ownedPath, &bytes);
        if (const auto* failure = std::get_if<Failure>(&loaded)) return *failure;
        auto& source = std::get<SourceSnapshot>(loaded);
        auto context = std::find_if(source.contexts.begin(), source.contexts.end(), [&](const auto& value) { return value.id == contextId; });
        const auto name = displayName.isEmpty() ? context->name : displayName;
        if (context->displayName == name) return snapshot;
        context->displayName = name;
        const auto saved = publishSource(source, bytes);
        if (const auto* failure = std::get_if<Failure>(&saved)) return *failure;
        auto visible = snapshot;
        for (auto& value : visible.contexts) if (value.id == contextId) value.displayName = name;
        return visible;
    }
    return Failure{StoreError::NotFound, "Source context is unavailable; existing data was retained."};
}
Result<SessionCatalog> KubeconfigStore::removeContext(const QString& contextId, QStringList expectedSessions) const {
    static const QRegularExpression identifier("\\Actx:[0-9a-f]{64}\\z");
    if (!identifier.match(contextId).hasMatch()) return Failure{StoreError::InvalidInput, "Select a valid imported context."};
    for (const auto& id : expectedSessions)
        if (QUuid(id).isNull() || QUuid(id).toString(QUuid::WithoutBraces) != id)
            return Failure{StoreError::InvalidInput, "Confirmation must contain canonical session identifiers."};
    std::sort(expectedSessions.begin(), expectedSessions.end());
    if (std::adjacent_find(expectedSessions.cbegin(), expectedSessions.cend()) != expectedSessions.cend())
        return Failure{StoreError::InvalidInput, "Confirmation cannot contain duplicate sessions."};
    const auto directory = sourceDirectory(profile_, false);
    if (const auto* failure = std::get_if<Failure>(&directory)) return *failure;
    if (!QDir(std::get<QString>(directory)).exists()) return Failure{StoreError::NotFound, "Source context is unavailable."};
    QLockFile lock(QDir(std::get<QString>(directory)).filePath("sources.lock"));
    lock.setStaleLockTime(0);
    if (!lock.tryLock(0)) return Failure{lock.error() == QLockFile::LockFailedError ? StoreError::Busy : StoreError::WriteFailed,
        "Cannot lock owned kubeconfigs; context and sessions were retained."};
    const auto catalog = list();
    if (const auto* failure = std::get_if<Failure>(&catalog)) return *failure;
    for (const auto& source : std::get<SourceCatalog>(catalog).sources)
        if (std::any_of(source.contexts.cbegin(), source.contexts.cend(), [&](const auto& context) { return context.id == contextId; })) {
            const auto removed = SessionStore(profile_).removeContext(contextId, expectedSessions);
            if (std::holds_alternative<SessionCatalog>(removed)) {
                bool deleted = true;
                for (const auto& path : {ociCredentialPath(std::get<QString>(directory), contextId),
                                         gkeCredentialPath(std::get<QString>(directory), contextId)}) {
                    const QFileInfo credential(path);
                    if ((credential.exists() || credential.isSymLink()) && !QFile::remove(path)) deleted = false;
                }
                if (!deleted) return Failure{StoreError::CleanupFailed,
                    "The context and sessions were removed, but a private credential could not be deleted. Check the protected profile before discarding backups."};
            }
            return removed;
        }
    return Failure{StoreError::NotFound, "Source context is unavailable; existing data was retained."};
}
Result<SourceImportReport> KubeconfigStore::importPath(QString path) const {
    const auto normalized = sourcePath(std::move(path));
    if (const auto* failure = std::get_if<Failure>(&normalized)) return *failure;
    if (const auto failure = profileFailure(profile_)) return *failure;
    path = std::get<QString>(normalized);
    const QFileInfo root(path);
    if (!contentSource(path) && !root.exists()) return Failure{StoreError::ReadFailed, "Kubeconfig source does not exist."};
    if (root.isDir() && root == QFileInfo(profile_))
        return Failure{StoreError::InvalidInput, "The private application profile cannot be scanned as a source folder."};
    if (contentSource(path) || !root.isDir()) {
        const auto imported = importFile(path);
        if (const auto* failure = std::get_if<Failure>(&imported)) return *failure;
        return SourceImportReport{{std::get<SourceSnapshot>(imported)}, {}};
    }
    if (!readableDirectory(path)) return Failure{StoreError::ReadFailed, "Cannot read source folder."};
    SourceImportReport report;
    QStringList directories{path};
    while (!directories.isEmpty()) {
        const QString directory = directories.takeLast();
        if (!readableDirectory(directory)) {
            report.errors.append({directory, {StoreError::ReadFailed, "Cannot read source folder."}});
            continue;
        }
        const auto entries = QDir(directory).entryInfoList(QDir::Files | QDir::Dirs | QDir::Hidden
            | QDir::NoDotAndDotDot | QDir::NoSymLinks, QDir::Name);
        for (const auto& entry : entries) {
            if (entry == QFileInfo(profile_)) continue;
            if (entry.isDir()) { directories.append(entry.absoluteFilePath()); continue; }
            const auto imported = importFile(entry.absoluteFilePath());
            if (const auto* failure = std::get_if<Failure>(&imported)) report.errors.append({entry.absoluteFilePath(), *failure});
            else report.sources.append(std::get<SourceSnapshot>(imported));
        }
    }
    std::sort(report.sources.begin(), report.sources.end(), [](const auto& a, const auto& b) { return a.sourcePath < b.sourcePath; });
    std::sort(report.errors.begin(), report.errors.end(), [](const auto& a, const auto& b) { return a.sourcePath < b.sourcePath; });
    return report;
}
Result<SourceCatalog> KubeconfigStore::list() const {
    const auto directory = sourceDirectory(profile_, false);
    if (std::holds_alternative<Failure>(directory)) return std::get<Failure>(directory);
    const auto sessions = SessionStore(profile_).list();
    if (const auto* failure = std::get_if<Failure>(&sessions)) return *failure;
    const auto& removed = std::get<SessionCatalog>(sessions).removedContexts;
    SourceCatalog catalog;
    QList<SourceSnapshot> snapshots;
    const QDir owned(std::get<QString>(directory));
    if (owned.exists() && !readableDirectory(owned.absolutePath()))
        return Failure{StoreError::ReadFailed, "Cannot read owned kubeconfig directory."};
    for (const auto& path : owned.entryInfoList({"*.json"}, QDir::Files | QDir::System, QDir::Name)) {
        const auto source = readOwned(path.absoluteFilePath());
        if (std::holds_alternative<Failure>(source)) {
            catalog.errors.append({path.absoluteFilePath(), std::get<Failure>(source)});
            continue;
        }
        snapshots.append(std::get<SourceSnapshot>(source));
    }
    std::sort(snapshots.begin(), snapshots.end(), [](const auto& a, const auto& b) {
        return a.importedAt != b.importedAt ? a.importedAt > b.importedAt : a.id < b.id;
    });
    QSet<QString> currentPaths;
    for (auto& snapshot : snapshots) {
        snapshot.current = !currentPaths.contains(snapshot.sourcePath);
        currentPaths.insert(snapshot.sourcePath);
        snapshot.contexts.erase(std::remove_if(snapshot.contexts.begin(), snapshot.contexts.end(),
            [&](const auto& context) { return removed.contains(context.id); }), snapshot.contexts.end());
        if (!snapshot.contexts.isEmpty()) catalog.sources.append(std::move(snapshot));
    }
    return catalog;
}
Result<ClusterConnection> KubeconfigStore::connection(const QString& contextId) const {
    if (contextId.isEmpty()) return Failure{StoreError::InvalidInput, "A context is required."};
    const auto catalog = list();
    if (const auto* failure = std::get_if<Failure>(&catalog)) return *failure;
    for (const auto& source : std::get<SourceCatalog>(catalog).sources) {
        for (const auto& context : source.contexts) {
            if (context.id != contextId) continue;
            if (!context.brokenReferences.isEmpty())
                return Failure{StoreError::InvalidData, "Context has missing or ambiguous references."};
            QByteArray original;
            const auto owned = readOwned(source.ownedPath, &original);
            if (const auto* failure = std::get_if<Failure>(&owned)) return *failure;
            try {
                const auto root = YAML::Load(original.toStdString());
                QStringList warnings;
                const auto clusters = entries(root, "clusters", "cluster", warnings);
                const auto users = entries(root, "users", "user", warnings);
                const auto cluster = clusters.values.value(context.cluster);
                const auto user = context.user.isEmpty() ? YAML::Node(YAML::NodeType::Map) : users.values.value(context.user);
                if (user["auth-provider"] && user["exec"])
                    return Failure{StoreError::InvalidData, "Exec and legacy auth-provider authentication cannot be combined."};
                if (!text(cluster, "proxy-url").isEmpty() || !text(user, "as").isEmpty() || !text(user, "as-uid").isEmpty() || user["as-groups"] || user["as-user-extra"])
                    return Failure{StoreError::UnsupportedVersion, "Proxy and impersonation configuration is not implemented in the native connection lane yet."};
                const auto material = [&](const YAML::Node& node, const char* dataKey, const char* pathKey) -> Result<QByteArray> {
                    const QString encoded = text(node, dataKey);
                    if (!encoded.isEmpty()) {
                        const auto decoded = QByteArray::fromBase64Encoding(encoded.toLatin1(), QByteArray::AbortOnBase64DecodingErrors);
                        if (!decoded || QString::fromLatin1(encoded.toLatin1()) != encoded)
                            return Failure{StoreError::InvalidData, "Invalid encoded credential or certificate."};
                        return decoded.decoded;
                    }
                    const QString path = text(node, pathKey);
                    if (path.isEmpty()) return QByteArray{};
                    if (generatedSource(source.sourcePath) && QDir::isRelativePath(path))
                        return Failure{StoreError::InvalidInput, "Generated kubeconfigs require embedded credentials/certificates or absolute file references."};
                    if (contentSource(source.sourcePath) && QDir::isRelativePath(path))
                        return Failure{StoreError::InvalidInput, "Document kubeconfigs require embedded credentials/certificates or absolute file references."};
                    QFile file(QDir(QFileInfo(source.sourcePath).absolutePath()).absoluteFilePath(path));
                    const QFileInfo info(file);
                    if (!info.isFile())
                        return Failure{StoreError::ReadFailed, "Referenced credential or certificate must be a readable regular file."};
                    if (info.size() > maximumKubeconfigBytes)
                        return Failure{StoreError::InvalidData, "Referenced credential or certificate exceeds the 16 MiB limit."};
                    if (!file.open(QIODevice::ReadOnly)) return Failure{StoreError::ReadFailed, "Cannot read referenced credential or certificate file."};
                    const auto bytes = file.read(maximumKubeconfigBytes + 1);
                    if (bytes.size() > maximumKubeconfigBytes)
                        return Failure{StoreError::InvalidData, "Referenced credential or certificate exceeds the 16 MiB limit."};
                    if (file.error() != QFileDevice::NoError || bytes.isEmpty())
                        return Failure{StoreError::ReadFailed, "Cannot read referenced credential or certificate file."};
                    return bytes;
                };
                ClusterConnection result;
                result.contextId = contextId;
                result.credentialId = hash((source.id + '\0' + context.cluster + '\0' + context.user).toUtf8());
                result.server = QUrl(context.server);
                result.tls = QSslConfiguration::defaultConfiguration();
                result.peerName = text(cluster, "tls-server-name");
                if (cluster["insecure-skip-tls-verify"]) result.insecure = cluster["insecure-skip-tls-verify"].as<bool>();
                result.tls.setPeerVerifyMode(result.insecure ? QSslSocket::VerifyNone : QSslSocket::VerifyPeer);
                const auto ca = material(cluster, "certificate-authority-data", "certificate-authority");
                if (const auto* failure = std::get_if<Failure>(&ca)) return *failure;
                if (!std::get<QByteArray>(ca).isEmpty()) {
                    const auto certificates = QSslCertificate::fromData(std::get<QByteArray>(ca));
                    if (certificates.isEmpty()) return Failure{StoreError::InvalidData, "Invalid certificate authority."};
                    result.tls.setCaCertificates(certificates);
                }
                const auto certificate = material(user, "client-certificate-data", "client-certificate");
                const auto key = material(user, "client-key-data", "client-key");
                if (const auto* failure = std::get_if<Failure>(&certificate)) return *failure;
                if (const auto* failure = std::get_if<Failure>(&key)) return *failure;
                if (!std::get<QByteArray>(certificate).isEmpty() || !std::get<QByteArray>(key).isEmpty()) {
                    const auto installed = clientCertificate(result.tls, std::get<QByteArray>(certificate), std::get<QByteArray>(key));
                    if (const auto* failure = std::get_if<Failure>(&installed)) return *failure;
                    result.tls = std::get<QSslConfiguration>(installed);
                }
                QString token = text(user, "token");
                if (!text(user, "tokenFile").isEmpty()) {
                    const auto file = material(user, "", "tokenFile");
                    if (const auto* failure = std::get_if<Failure>(&file)) return *failure;
                    const auto bytes = std::get<QByteArray>(file);
                    token = QString::fromUtf8(bytes).trimmed();
                    if (token.isEmpty()) return Failure{StoreError::InvalidData, "Token file contains no token."};
                    if (QString::fromUtf8(bytes).toUtf8() != bytes)
                        return Failure{StoreError::InvalidData, "Token file must contain UTF-8 text."};
                }
                if (token.isEmpty() && user["auth-provider"]) {
                    const auto config = user["auth-provider"]["config"];
                    if (config && !config.IsNull() && !config.IsMap())
                        return Failure{StoreError::InvalidData, "Auth-provider config must be a mapping."};
                    if (config && config.IsMap()) {
                        token = text(config, "access-token");
                        if (token.isEmpty()) token = text(config, "id-token");
                    }
                    if (token.isEmpty())
                        return Failure{StoreError::UnsupportedVersion, "No cached auth-provider token is available. Refresh it with your provider and explicitly reimport the configuration."};
                }
                if (!token.isEmpty()) {
                    if (std::any_of(token.cbegin(), token.cend(), [](QChar c) { return c.isSpace() || c.category() == QChar::Other_Control; }))
                        return Failure{StoreError::InvalidData, "Invalid token characters."};
                    result.authorization = "Bearer " + token.toUtf8();
                } else if (!text(user, "username").isEmpty()) {
                    const QString name = text(user, "username");
                    if (name.contains(':')) return Failure{StoreError::InvalidData, "Basic authentication username cannot contain a colon."};
                    result.authorization = "Basic " + (name + ':' + text(user, "password")).toUtf8().toBase64();
                }
                if (const auto node = user["exec"]; node) {
                    if (!result.authorization.isEmpty() || !result.tls.localCertificate().isNull())
                        return Failure{StoreError::InvalidData, "Exec and static credentials cannot be combined."};
                    ExecCommand command;
                    command.apiVersion = text(node, "apiVersion");
                    const bool v1 = command.apiVersion == "client.authentication.k8s.io/v1";
                    if (!v1 && command.apiVersion != "client.authentication.k8s.io/v1beta1")
                        return Failure{StoreError::UnsupportedVersion, "Unsupported exec credential API version."};
                    const auto mode = text(node, "interactiveMode");
                    if ((v1 && mode.isEmpty()) || (!mode.isEmpty() && mode != "Never" && mode != "IfAvailable" && mode != "Always"))
                        return Failure{StoreError::InvalidData, "Invalid or missing exec interactiveMode."};
                    command.program = text(node, "command");
                    if (command.program.isEmpty() || std::any_of(command.program.cbegin(), command.program.cend(), [](QChar c) { return c.category() == QChar::Other_Control; }))
                        return Failure{StoreError::InvalidData, "Invalid exec command."};
                    const bool generated = generatedSource(source.sourcePath);
                    command.workingDirectory = QFileInfo(generated ? source.ownedPath : source.sourcePath).absolutePath();
                    if (generated && QDir::isRelativePath(command.program) && (command.program.contains('/') || command.program.contains('\\')))
                        return Failure{StoreError::InvalidInput, "Generated kubeconfigs require an absolute or PATH-resolved credential command."};
                    if (QDir::isRelativePath(command.program) && (command.program.contains('/') || command.program.contains('\\')))
                        command.program = QDir::cleanPath(QDir(command.workingDirectory).absoluteFilePath(command.program));
                    const auto args = node["args"];
                    if (args && !args.IsSequence()) return Failure{StoreError::InvalidData, "Exec args must be a list of strings."};
                    if (args) for (const auto& arg : args) {
                        if (!arg.IsScalar()) return Failure{StoreError::InvalidData, "Exec args must be a list of strings."};
                        const auto value = QString::fromStdString(arg.as<std::string>());
                        if (value.contains(QChar::Null)) return Failure{StoreError::InvalidData, "Invalid exec argument."};
                        command.arguments.append(value);
                    }
                    const auto env = node["env"];
                    if (env && !env.IsSequence()) return Failure{StoreError::InvalidData, "Exec env must be a list."};
                    QSet<QString> names;
                    if (env) for (const auto& entry : env) {
                        const auto name = text(entry, "name");
                        const auto value = text(entry, "value");
#ifdef Q_OS_WIN
                        const auto canonical = name.toUpper();
#else
                        const auto canonical = name;
#endif
                        if (name.isEmpty() || name.contains('=') || name.contains(QChar::Null) || value.contains(QChar::Null)
                            || canonical == "KUBERNETES_EXEC_INFO" || names.contains(canonical))
                            return Failure{StoreError::InvalidData, "Invalid, reserved or duplicate exec environment name."};
                        names.insert(canonical); command.environment.insert(name, value);
                    }
                    QJsonObject spec{{"interactive", false}};
                    if (node["provideClusterInfo"] && node["provideClusterInfo"].as<bool>()) {
                        QJsonObject info{{"server", result.server.toString()}};
                        if (!result.peerName.isEmpty()) info.insert("tls-server-name", result.peerName);
                        if (result.insecure) info.insert("insecure-skip-tls-verify", true);
                        if (!std::get<QByteArray>(ca).isEmpty()) info.insert("certificate-authority-data", QString::fromLatin1(std::get<QByteArray>(ca).toBase64()));
                        if (const auto extensions = cluster["extensions"]; extensions) {
                            if (!extensions.IsSequence()) return Failure{StoreError::InvalidData, "Invalid cluster extensions."};
                            for (const auto& extension : extensions)
                                if (text(extension, "name") == "client.authentication.k8s.io/exec")
                                    return Failure{StoreError::UnsupportedVersion, "Exec cluster configuration extensions are not supported yet."};
                        }
                        spec.insert("cluster", info);
                    }
                    command.info = {{"apiVersion", command.apiVersion}, {"kind", "ExecCredential"}, {"spec", spec}};
                    const auto importedOci = importedOciEnvironment(profile_, contextId);
                    if (const auto* failure = std::get_if<Failure>(&importedOci)) return *failure;
                    const bool hasImportedOci = std::get<std::optional<QMap<QString, QString>>>(importedOci).has_value();
                    if (hasImportedOci) {
                        const auto prepared = prepareImportedOci(command);
                        if (const auto* failure = std::get_if<Failure>(&prepared)) return *failure;
                        for (auto setting = std::get<std::optional<QMap<QString, QString>>>(importedOci)->cbegin();
                             setting != std::get<std::optional<QMap<QString, QString>>>(importedOci)->cend(); ++setting)
                            command.environment.insert(setting.key(), setting.value());
                    }
                    const auto parsed = nativeOidc(command);
                    if (const auto* failure = std::get_if<Failure>(&parsed)) return *failure;
                    command.oidc = std::get<std::optional<OidcLogin>>(parsed);
#if defined(Q_OS_IOS) || defined(Q_OS_ANDROID)
                    if (!command.oidc) {
                        const auto aks = nativeAks(command);
                        if (const auto* failure = std::get_if<Failure>(&aks)) return *failure;
                        command.oidc = std::get<std::optional<OidcLogin>>(aks);
                    }
#endif
                    const auto importedGke = importedGkeAccount(profile_, contextId);
                    if (const auto* failure = std::get_if<Failure>(&importedGke)) return *failure;
                    const auto& account = std::get<std::optional<GkeServiceAccount>>(importedGke);
#if defined(Q_OS_IOS) || defined(Q_OS_ANDROID)
                    const bool useNativeGke = true;
#else
                    const bool useNativeGke = account.has_value();
#endif
                    if (!command.oidc && useNativeGke) {
                        const auto gke = nativeGkeConfig(command);
                        if (const auto* failure = std::get_if<Failure>(&gke)) return *failure;
                        command.gke = std::get<std::optional<GkeLogin>>(gke);
                        if (command.gke) {
                            command.gke->serviceAccount = account;
                            if (command.gke->serviceAccountOnly && !command.gke->serviceAccount)
                                return Failure{StoreError::UnsupportedVersion, "This GKE context requires an explicitly imported Google service-account key on mobile."};
                        }
                    }
#if defined(Q_OS_IOS) || defined(Q_OS_ANDROID)
                    if (!command.oidc && !command.gke) {
                        const auto eks = nativeEksConfig(command);
                        if (const auto* failure = std::get_if<Failure>(&eks)) return *failure;
                        command.eks = std::get<std::optional<EksLogin>>(eks);
                    }
                    if (!command.oidc && !command.gke && !command.eks) {
                        const auto oci = nativeOciConfig(command);
                        if (const auto* failure = std::get_if<Failure>(&oci)) return *failure;
                        command.oci = std::get<std::optional<OciLogin>>(oci);
                    }
#endif
#if !defined(Q_OS_IOS) && !defined(Q_OS_ANDROID)
                    if (hasImportedOci && !command.oidc) {
                        const auto oci = nativeOciConfig(command);
                        if (const auto* failure = std::get_if<Failure>(&oci)) return *failure;
                        command.oci = std::get<std::optional<OciLogin>>(oci);
                    }
#endif
                    if (mode == "Always" && !command.oidc && !command.gke && !command.eks && !command.oci)
                        return Failure{StoreError::UnsupportedVersion, "This credential command requires terminal input; the desktop login has no terminal stdin."};
#if defined(Q_OS_IOS) || defined(Q_OS_ANDROID)
                    if (!command.oidc && !command.gke && !command.eks && !command.oci)
                        return Failure{StoreError::UnsupportedVersion, "This kubeconfig uses an environment-bound or external credential command. On mobile, sign in with a supported browser identity or import a personal token or certificate; VM and CI identities cannot be moved to this device."};
#endif
                    if (hasImportedOci || account.has_value()) result.credentialId = importedCredentialId(contextId);
                    result.exec = std::move(command); result.credentialReady = false;
                }
                return result;
            } catch (const YAML::Exception&) {
                return Failure{StoreError::InvalidData, "Invalid connection field types."};
            }
        }
    }
    return Failure{StoreError::NotFound, "Owned context is unavailable; existing session binding was retained."};
}
QString importedCredentialId(const QString& contextId) { return "imported:" + contextId; }
Result<QSslConfiguration> clientCertificate(QSslConfiguration tls, const QByteArray& certificate, const QByteArray& key) {
    const auto certificates = QSslCertificate::fromData(certificate);
    QSslKey privateKey;
    for (const auto algorithm : {QSsl::Rsa, QSsl::Ec, QSsl::Dsa}) {
        privateKey = QSslKey(key, algorithm);
        if (!privateKey.isNull()) break;
    }
    if (certificates.isEmpty() || privateKey.isNull())
        return Failure{StoreError::InvalidData, "Invalid or incomplete client certificate/key pair."};
    tls.setLocalCertificateChain(certificates); tls.setPrivateKey(privateKey);
    return tls;
}
QJsonObject sourceJson(const SourceSnapshot& source) {
    QJsonArray contexts;
    for (const auto& context : source.contexts) contexts.append(QJsonObject{{"id", context.id}, {"name", context.name}, {"displayName", context.displayName},
        {"cluster", context.cluster}, {"user", context.user}, {"namespace", context.nameSpace},
        {"server", context.server}, {"authType", context.authType},
        {"brokenReferences", QJsonArray::fromStringList(context.brokenReferences)}});
    return {{"id", source.id}, {"sourcePath", source.sourcePath}, {"contentHash", source.contentHash},
        {"ownedPath", source.ownedPath}, {"importedAt", source.importedAt.toUTC().toString(Qt::ISODateWithMs)},
        {"contexts", contexts}, {"warnings", QJsonArray::fromStringList(source.warnings)}};
}
QJsonObject sourceImportJson(const SourceImportReport& report) {
    QJsonArray sources, errors;
    for (const auto& source : report.sources) sources.append(sourceJson(source));
    for (const auto& issue : report.errors) errors.append(QJsonObject{{"sourcePath", issue.sourcePath},
        {"code", errorName(issue.failure.code)}, {"message", issue.failure.message}});
    return {{"sources", sources}, {"errors", errors}};
}
} // namespace podlord
