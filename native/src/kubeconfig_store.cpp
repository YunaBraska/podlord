#include "kubeconfig_store.h"
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLockFile>
#include <QMap>
#include <QProcess>
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
bool k3dName(const QString& name) {
    static const QRegularExpression pattern("\\A[A-Za-z0-9][A-Za-z0-9_.-]{0,127}\\z");
    return pattern.match(name).hasMatch();
}
bool generatedSource(const QString& path) {
    const QString prefix = "podlord-generated://k3d/";
    return path.startsWith(prefix) && k3dName(path.mid(prefix.size()));
}
Result<QByteArray> k3dOutput(const QString& executable, const QStringList& arguments) {
    QProcess process;
    process.setStandardErrorFile(QProcess::nullDevice());
    process.start(executable, arguments, QIODevice::ReadOnly);
    if (!process.waitForStarted(5000))
        return Failure{StoreError::ReadFailed, "Cannot start k3d. Check its installation and local Docker/Colima connection."};
    QElapsedTimer elapsed;
    elapsed.start();
    QByteArray output;
    do {
        process.waitForReadyRead(50);
        output += process.readAllStandardOutput();
        const bool oversized = output.size() > maximumKubeconfigBytes;
        if (oversized || elapsed.elapsed() >= 30000) {
            process.kill();
            process.waitForFinished(5000);
            return Failure{oversized ? StoreError::InvalidData : StoreError::ReadFailed,
                oversized ? "k3d output exceeds the 16 MiB input limit." : "The local k3d command did not finish within 30 seconds."};
        }
    } while (process.state() != QProcess::NotRunning);
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0)
        return Failure{StoreError::ReadFailed, "k3d command failed. Check k3d and the local Docker/Colima connection."};
    return output;
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
    if ((!generatedSource(path) && (!QDir::isAbsolutePath(path) || QDir::cleanPath(path) != path)) || !time.isValid())
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
} // namespace

KubeconfigStore::KubeconfigStore(QString profile) : profile_(std::move(profile)) {}
Result<SourceImportReport> KubeconfigStore::importK3d(const QString& executable) const {
    if (const auto failure = profileFailure(profile_)) return *failure;
    const QFileInfo tool(executable);
    if (!QDir::isAbsolutePath(executable) || !tool.isFile() || !tool.isExecutable())
        return Failure{StoreError::ReadFailed, "k3d is not available. Install k3d and Docker/Colima to import local clusters."};
    QStringList names;
    const auto listing = k3dOutput(executable, {"cluster", "list", "-o", "json"});
    if (const auto* bytes = std::get_if<QByteArray>(&listing)) {
        QJsonParseError error;
        const auto document = QJsonDocument::fromJson(*bytes, &error);
        if (error.error != QJsonParseError::NoError || !document.isArray())
            return Failure{StoreError::InvalidData, "k3d cluster list must return a JSON array."};
        for (const auto& entry : document.array()) {
            if (!entry.isObject()) return Failure{StoreError::InvalidData, "Invalid k3d cluster entry."};
            QString name;
            for (const auto* key : {"name", "Name", "clusterName", "ClusterName"})
                if (name.isEmpty()) name = entry.toObject().value(key).toString();
            if (!k3dName(name)) return Failure{StoreError::InvalidData, "Invalid k3d cluster name."};
            names.append(name);
        }
    } else {
        // Existing reference compatibility: older k3d versions expose a text table.
        const auto legacy = k3dOutput(executable, {"cluster", "list", "--no-headers"});
        if (std::holds_alternative<Failure>(legacy)) return std::get<Failure>(listing);
        const auto legacyBytes = std::get<QByteArray>(legacy);
        const auto text = QString::fromUtf8(legacyBytes);
        if (text.toUtf8() != legacyBytes) return Failure{StoreError::InvalidData, "k3d cluster list must contain UTF-8 text."};
        for (const auto& row : text.split('\n', Qt::SkipEmptyParts)) {
            const auto name = row.simplified().section(' ', 0, 0);
            if (!k3dName(name)) return Failure{StoreError::InvalidData, "Invalid k3d cluster name."};
            names.append(name);
        }
    }
    names.removeDuplicates();
    names.sort();
    SourceImportReport report;
    for (const auto& name : names) {
        const auto origin = "podlord-generated://k3d/" + name;
        const auto exported = k3dOutput(executable, {"kubeconfig", "get", name});
        if (const auto* failure = std::get_if<Failure>(&exported)) {
            report.errors.append({origin, *failure});
            continue;
        }
        const auto imported = importContent(origin, std::get<QByteArray>(exported));
        if (const auto* failure = std::get_if<Failure>(&imported)) report.errors.append({origin, *failure});
        else report.sources.append(std::get<SourceSnapshot>(imported));
    }
    return report;
}
Result<SourceSnapshot> KubeconfigStore::importFile(QString path) const {
    const auto normalized = sourcePath(std::move(path));
    if (const auto* failure = std::get_if<Failure>(&normalized)) return *failure;
    path = std::get<QString>(normalized);
    if (!QFileInfo(path).isFile()) return Failure{StoreError::ReadFailed, "Source must be a readable regular kubeconfig file."};
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return Failure{StoreError::ReadFailed, "Cannot read kubeconfig source."};
    const auto bytes = file.read(maximumKubeconfigBytes + 1);
    if (file.error() != QFileDevice::NoError) return Failure{StoreError::ReadFailed, "Cannot read kubeconfig source."};
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
Result<SourceSnapshot> KubeconfigStore::importContent(QString path, const QByteArray& bytes) const {
    auto parsed = parse(path, bytes, QDateTime::currentDateTimeUtc());
    if (std::holds_alternative<Failure>(parsed)) return parsed;
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
        if (std::any_of(source.contexts.cbegin(), source.contexts.cend(), [&](const auto& context) { return context.id == contextId; }))
            return SessionStore(profile_).removeContext(contextId, expectedSessions);
    return Failure{StoreError::NotFound, "Source context is unavailable; existing data was retained."};
}
Result<SourceImportReport> KubeconfigStore::importPath(QString path) const {
    const auto normalized = sourcePath(std::move(path));
    if (const auto* failure = std::get_if<Failure>(&normalized)) return *failure;
    if (const auto failure = profileFailure(profile_)) return *failure;
    path = std::get<QString>(normalized);
    const QFileInfo root(path);
    if (!root.exists()) return Failure{StoreError::ReadFailed, "Kubeconfig source does not exist."};
    if (root.isDir() && root == QFileInfo(profile_))
        return Failure{StoreError::InvalidInput, "The private application profile cannot be scanned as a source folder."};
    if (!root.isDir()) {
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
    const QDir owned(std::get<QString>(directory));
    if (owned.exists() && !readableDirectory(owned.absolutePath()))
        return Failure{StoreError::ReadFailed, "Cannot read owned kubeconfig directory."};
    for (const auto& path : owned.entryInfoList({"*.json"}, QDir::Files | QDir::System, QDir::Name)) {
        const auto source = readOwned(path.absoluteFilePath());
        if (std::holds_alternative<Failure>(source)) {
            catalog.errors.append({path.absoluteFilePath(), std::get<Failure>(source)});
            continue;
        }
        auto snapshot = std::get<SourceSnapshot>(source);
        snapshot.contexts.erase(std::remove_if(snapshot.contexts.begin(), snapshot.contexts.end(),
            [&](const auto& context) { return removed.contains(context.id); }), snapshot.contexts.end());
        if (!snapshot.contexts.isEmpty()) catalog.sources.append(std::move(snapshot));
    }
    std::sort(catalog.sources.begin(), catalog.sources.end(), [](const auto& a, const auto& b) {
        return a.importedAt != b.importedAt ? a.importedAt > b.importedAt : a.id < b.id;
    });
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
                    if (mode == "Always") return Failure{StoreError::UnsupportedVersion, "This credential command requires terminal input; the desktop login has no terminal stdin."};
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
