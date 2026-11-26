#include "credential_store.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QMutex>

#ifdef Q_OS_MACOS
#include <CoreFoundation/CoreFoundation.h>
#include <Security/Security.h>
#elif defined(Q_OS_WIN)
#include <windows.h>
#include <wincred.h>
#elif defined(Q_OS_LINUX)
#undef signals
#include <libsecret/secret.h>
#endif

namespace podlord {
CredentialStore::ReadResult CredentialStore::readMigrating(const QString& profile, const QString& name,
    const QString& oldFile, qsizetype maxBytes, std::function<QByteArray(const QByteArray&)> prepare) {
    auto stored = read(profile, name);
    if (!stored.ok) return stored;
    const auto path = QDir(profile).filePath(oldFile);
    QFile legacy(path);
    if (QFileInfo(path).isSymLink()) return {false, false, {}, "Legacy credentials could not be migrated. The old file was kept."};
    if (!legacy.exists()) return stored;
    if (!legacy.open(QIODevice::ReadOnly) || legacy.size() > maxBytes
        || (legacy.permissions() & (QFile::ReadGroup | QFile::WriteGroup | QFile::ReadOther | QFile::WriteOther)))
        return {false, false, {}, "Legacy credentials could not be migrated. The old file was kept."};
    const auto original = legacy.readAll();
    legacy.close();
    const auto bytes = prepare ? prepare(original) : original;
    if (bytes.isEmpty()) return {false, false, {}, "Legacy credentials could not be migrated. The old file was kept."};
    if (stored.found && stored.data != bytes)
        return {false, false, {}, "Legacy credentials could not be migrated. The old file was kept."};
    if (!stored.found) {
        QString error;
        if (!write(profile, name, bytes, error)) return {false, false, {}, "Legacy credentials could not be migrated. The old file was kept."};
    }
    if (!legacy.remove()) return {false, false, {}, "Legacy credentials could not be migrated. The old file was kept."};
    return {true, true, bytes, {}};
}
namespace {
QString accountName(const QString& profile, const QString& name) {
    const auto path = QDir::cleanPath(QDir(profile).absolutePath()).toUtf8();
    return name + "." + QString::fromLatin1(QCryptographicHash::hash(path, QCryptographicHash::Sha256).toHex());
}
#ifdef Q_OS_MACOS
CFStringRef string(const QString& value) {
    const auto bytes = value.toUtf8();
    return CFStringCreateWithBytes(kCFAllocatorDefault, reinterpret_cast<const UInt8*>(bytes.constData()),
        bytes.size(), kCFStringEncodingUTF8, false);
}
CFMutableDictionaryRef query(const QString& service, const QString& account) {
    auto* result = CFDictionaryCreateMutable(kCFAllocatorDefault, 4, &kCFTypeDictionaryKeyCallBacks,
        &kCFTypeDictionaryValueCallBacks);
    CFDictionarySetValue(result, kSecClass, kSecClassGenericPassword);
    auto* serviceValue = string(service);
    auto* accountValue = string(account);
    CFDictionarySetValue(result, kSecAttrService, serviceValue);
    CFDictionarySetValue(result, kSecAttrAccount, accountValue);
    CFRelease(serviceValue);
    CFRelease(accountValue);
    return result;
}
#elif defined(Q_OS_LINUX)
SecretSchema schemaFor(const char* name) {
    SecretSchema value{};
    value.name = name;
    value.flags = SECRET_SCHEMA_NONE;
    value.attributes[0] = {"account", SECRET_SCHEMA_ATTRIBUTE_STRING};
    return value;
}
const SecretSchema legacySchema = schemaFor("dev.podlord.ai");
const SecretSchema profileSchema = schemaFor("dev.podlord");
#endif
} // namespace

SystemCredentialStore::SystemCredentialStore(Service service) : service_(service) {}

CredentialStore::ReadResult SystemCredentialStore::read(const QString& profile, const QString& name) {
    const auto account = accountName(profile, name);
    const auto service = service_ == Service::Profile ? QStringLiteral("dev.podlord") : QStringLiteral("dev.podlord.ai");
#ifdef Q_OS_MACOS
    auto* attributes = query(service, account);
    CFDictionarySetValue(attributes, kSecReturnData, kCFBooleanTrue);
    CFDictionarySetValue(attributes, kSecMatchLimit, kSecMatchLimitOne);
    CFTypeRef result = nullptr;
    const auto status = SecItemCopyMatching(attributes, &result);
    CFRelease(attributes);
    if (status == errSecItemNotFound) return {true, false, {}, {}};
    if (status != errSecSuccess || !result) return {false, false, {}, "System password store is unavailable or locked."};
    const auto data = QByteArray(reinterpret_cast<const char*>(CFDataGetBytePtr(static_cast<CFDataRef>(result))),
        CFDataGetLength(static_cast<CFDataRef>(result)));
    CFRelease(result);
    return {true, true, data, {}};
#elif defined(Q_OS_WIN)
    PCREDENTIALW credential = nullptr;
    const auto target = (service + "." + account).toStdWString();
    if (!CredReadW(target.c_str(), CRED_TYPE_GENERIC, 0, &credential)) {
        const auto code = GetLastError();
        return code == ERROR_NOT_FOUND ? ReadResult{true, false, {}, {}}
            : ReadResult{false, false, {}, "System password store is unavailable or locked."};
    }
    const auto data = QByteArray(reinterpret_cast<const char*>(credential->CredentialBlob), credential->CredentialBlobSize);
    CredFree(credential);
    return {true, true, data, {}};
#elif defined(Q_OS_LINUX)
    GError* failure = nullptr;
    const auto& schema = service_ == Service::Profile ? profileSchema : legacySchema;
    auto* value = secret_password_lookup_sync(&schema, nullptr, &failure, "account", account.toUtf8().constData(), nullptr);
    if (failure) {
        g_error_free(failure);
        return {false, false, {}, "System password store is unavailable or locked."};
    }
    if (!value) return {true, false, {}, {}};
    const auto data = QByteArray(value);
    secret_password_free(value);
    return {true, true, data, {}};
#else
    return {false, false, {}, "System password store is unavailable or locked."};
#endif
}

bool SystemCredentialStore::write(const QString& profile, const QString& name, const QByteArray& data, QString& error) {
    const auto account = accountName(profile, name);
    const auto service = service_ == Service::Profile ? QStringLiteral("dev.podlord") : QStringLiteral("dev.podlord.ai");
#ifdef Q_OS_MACOS
    auto* attributes = query(service, account);
    auto* value = CFDataCreate(kCFAllocatorDefault, reinterpret_cast<const UInt8*>(data.constData()), data.size());
    auto* update = CFDictionaryCreateMutable(kCFAllocatorDefault, 1, &kCFTypeDictionaryKeyCallBacks,
        &kCFTypeDictionaryValueCallBacks);
    CFDictionarySetValue(update, kSecValueData, value);
    auto status = SecItemUpdate(attributes, update);
    if (status == errSecItemNotFound) {
        CFDictionarySetValue(attributes, kSecValueData, value);
        status = SecItemAdd(attributes, nullptr);
    }
    CFRelease(update); CFRelease(value); CFRelease(attributes);
    if (status == errSecSuccess) return true;
    error = "System password store is unavailable or locked.";
    return false;
#elif defined(Q_OS_WIN)
    if (data.size() > CRED_MAX_CREDENTIAL_BLOB_SIZE) {
        error = "Credentials exceed the Windows password store size limit.";
        return false;
    }
    const auto target = (service + "." + account).toStdWString();
    CREDENTIALW credential{};
    credential.Type = CRED_TYPE_GENERIC;
    credential.TargetName = const_cast<wchar_t*>(target.c_str());
    credential.CredentialBlobSize = static_cast<DWORD>(data.size());
    credential.CredentialBlob = reinterpret_cast<LPBYTE>(const_cast<char*>(data.constData()));
    credential.Persist = CRED_PERSIST_LOCAL_MACHINE;
    if (CredWriteW(&credential, 0)) return true;
    error = "System password store is unavailable or locked.";
    return false;
#elif defined(Q_OS_LINUX)
    GError* failure = nullptr;
    const auto utf8 = data.toStdString();
    const auto key = account.toUtf8();
    const auto& schema = service_ == Service::Profile ? profileSchema : legacySchema;
    const bool saved = secret_password_store_sync(&schema, SECRET_COLLECTION_DEFAULT, "PodLord", utf8.c_str(),
        nullptr, &failure, "account", key.constData(), nullptr);
    if (saved) return true;
    error = "System password store is unavailable or locked.";
    if (failure) g_error_free(failure);
    return false;
#else
    Q_UNUSED(data);
    error = "System password store is unavailable or locked.";
    return false;
#endif
}

bool SystemCredentialStore::remove(const QString& profile, const QString& name, QString& error) {
    const auto account = accountName(profile, name);
    const auto service = service_ == Service::Profile ? QStringLiteral("dev.podlord") : QStringLiteral("dev.podlord.ai");
#ifdef Q_OS_MACOS
    auto* attributes = query(service, account);
    const auto status = SecItemDelete(attributes);
    CFRelease(attributes);
    if (status == errSecSuccess || status == errSecItemNotFound) return true;
    error = "System password store is unavailable or locked.";
    return false;
#elif defined(Q_OS_WIN)
    const auto target = (service + "." + account).toStdWString();
    if (CredDeleteW(target.c_str(), CRED_TYPE_GENERIC, 0) || GetLastError() == ERROR_NOT_FOUND) return true;
    error = "System password store is unavailable or locked.";
    return false;
#elif defined(Q_OS_LINUX)
    GError* failure = nullptr;
    const auto key = account.toUtf8();
    const auto& schema = service_ == Service::Profile ? profileSchema : legacySchema;
    const bool removed = secret_password_clear_sync(&schema, nullptr, &failure, "account", key.constData(), nullptr);
    if (removed) return true;
    error = "System password store is unavailable or locked.";
    if (failure) g_error_free(failure);
    return false;
#else
    error = "System password store is unavailable or locked.";
    return false;
#endif
}

ProfileCredentialStore::ProfileCredentialStore(CredentialStore* vaultBackend, CredentialStore* legacyBackend)
    : vaultBackend_(vaultBackend ? vaultBackend : &system_),
      legacyBackend_(legacyBackend ? legacyBackend : (vaultBackend ? vaultBackend : &legacy_)) {}

#if defined(Q_OS_MACOS) || defined(Q_OS_LINUX)
namespace {
const QString vaultName = QStringLiteral("podlord-profile-vault-v1");
QMutex vaultMutex;
struct Vault {
    bool ok = false;
    QJsonObject entries;
    QString error;
};
Vault loadVault(CredentialStore& backend, const QString& profile) {
    const auto stored = backend.read(profile, vaultName);
    if (!stored.ok) return {false, {}, stored.error};
    if (!stored.found) return {true, {}, {}};
    const auto document = QJsonDocument::fromJson(stored.data);
    if (!document.isObject() || document.object().value("version") != 1
        || !document.object().value("entries").isObject())
        return {false, {}, "PodLord credential vault is invalid."};
    const auto entries = document.object().value("entries").toObject();
    for (auto it = entries.begin(); it != entries.end(); ++it) {
        if (it.key().isEmpty() || !it.value().isString()
            || !QByteArray::fromBase64Encoding(it.value().toString().toLatin1(),
                QByteArray::AbortOnBase64DecodingErrors))
            return {false, {}, "PodLord credential vault is invalid."};
    }
    return {true, entries, {}};
}
bool saveVault(CredentialStore& backend, const QString& profile, const QJsonObject& entries, QString& error) {
    const auto data = QJsonDocument(QJsonObject{{"version", 1}, {"entries", entries}}).toJson(QJsonDocument::Compact);
    if (!backend.write(profile, vaultName, data, error)) return false;
    const auto checked = backend.read(profile, vaultName);
    if (checked.ok && checked.found && checked.data == data) return true;
    error = checked.ok ? "PodLord credential vault could not be verified." : checked.error;
    return false;
}
bool validName(const QString& name) { return !name.isEmpty() && name != vaultName; }
}
#endif

CredentialStore::ReadResult ProfileCredentialStore::read(const QString& profile, const QString& name) {
#if defined(Q_OS_MACOS) || defined(Q_OS_LINUX)
    if (!validName(name)) return {false, false, {}, "Invalid credential name."};
    auto vault = loadVault(*vaultBackend_, profile);
    if (!vault.ok) return {false, false, {}, vault.error};
    if (vault.entries.contains(name))
        return {true, true, QByteArray::fromBase64(vault.entries.value(name).toString().toLatin1()), {}};
    auto legacy = legacyBackend_->read(profile, name);
    if (!legacy.ok || !legacy.found || !QDir(profile).exists()) return legacy;
    QMutexLocker guard(&vaultMutex);
    QLockFile lock(QDir(profile).filePath(".credential-vault.lock"));
    lock.setStaleLockTime(0);
    if (!lock.tryLock(30000)) return {false, false, {}, "PodLord credentials are in use by another process."};
    vault = loadVault(*vaultBackend_, profile);
    if (!vault.ok) return {false, false, {}, vault.error};
    if (vault.entries.contains(name))
        return {true, true, QByteArray::fromBase64(vault.entries.value(name).toString().toLatin1()), {}};
    legacy = legacyBackend_->read(profile, name);
    if (!legacy.ok || !legacy.found) return legacy;
    vault.entries.insert(name, QString::fromLatin1(legacy.data.toBase64()));
    QString error;
    if (!saveVault(*vaultBackend_, profile, vault.entries, error)) return {false, false, {}, error};
    legacyBackend_->remove(profile, name, error);
    return legacy;
#else
    return legacyBackend_->read(profile, name);
#endif
}

bool ProfileCredentialStore::write(const QString& profile, const QString& name, const QByteArray& data, QString& error) {
#if defined(Q_OS_MACOS) || defined(Q_OS_LINUX)
    if (!validName(name)) { error = "Invalid credential name."; return false; }
    QMutexLocker guard(&vaultMutex);
    QLockFile lock(QDir(profile).filePath(".credential-vault.lock"));
    lock.setStaleLockTime(0);
    if (!lock.tryLock(30000)) { error = "PodLord credentials are in use by another process."; return false; }
    auto vault = loadVault(*vaultBackend_, profile);
    if (!vault.ok) { error = vault.error; return false; }
    vault.entries.insert(name, QString::fromLatin1(data.toBase64()));
    if (!saveVault(*vaultBackend_, profile, vault.entries, error)) return false;
    legacyBackend_->remove(profile, name, error);
    return true;
#else
    return legacyBackend_->write(profile, name, data, error);
#endif
}

bool ProfileCredentialStore::remove(const QString& profile, const QString& name, QString& error) {
#if defined(Q_OS_MACOS) || defined(Q_OS_LINUX)
    if (!validName(name)) { error = "Invalid credential name."; return false; }
    QMutexLocker guard(&vaultMutex);
    QLockFile lock(QDir(profile).filePath(".credential-vault.lock"));
    lock.setStaleLockTime(0);
    if (!lock.tryLock(30000)) { error = "PodLord credentials are in use by another process."; return false; }
    auto vault = loadVault(*vaultBackend_, profile);
    if (!vault.ok) { error = vault.error; return false; }
    if (!legacyBackend_->remove(profile, name, error)) return false;
    if (!vault.entries.contains(name)) return true;
    vault.entries.remove(name);
    if (!vault.entries.isEmpty()) return saveVault(*vaultBackend_, profile, vault.entries, error);
    if (!vaultBackend_->remove(profile, vaultName, error)) return false;
    const auto checked = vaultBackend_->read(profile, vaultName);
    if (checked.ok && !checked.found) return true;
    error = checked.ok ? "PodLord credential vault could not be verified." : checked.error;
    return false;
#else
    return legacyBackend_->remove(profile, name, error);
#endif
}
} // namespace podlord
