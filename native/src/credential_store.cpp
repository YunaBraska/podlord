#include "credential_store.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>

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
const QString service = QStringLiteral("dev.podlord.ai");
#ifdef Q_OS_MACOS
CFStringRef string(const QString& value) {
    const auto bytes = value.toUtf8();
    return CFStringCreateWithBytes(kCFAllocatorDefault, reinterpret_cast<const UInt8*>(bytes.constData()),
        bytes.size(), kCFStringEncodingUTF8, false);
}
CFMutableDictionaryRef query(const QString& account) {
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
const SecretSchema schema = [] {
    SecretSchema value{};
    value.name = "dev.podlord.ai";
    value.flags = SECRET_SCHEMA_NONE;
    value.attributes[0] = {"account", SECRET_SCHEMA_ATTRIBUTE_STRING};
    return value;
}();
#endif
} // namespace

CredentialStore::ReadResult SystemCredentialStore::read(const QString& profile, const QString& name) {
    const auto account = accountName(profile, name);
#ifdef Q_OS_MACOS
    auto* attributes = query(account);
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
#ifdef Q_OS_MACOS
    auto* attributes = query(account);
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
    const bool saved = secret_password_store_sync(&schema, SECRET_COLLECTION_DEFAULT, "PodLord AI", utf8.c_str(),
        nullptr, &failure, "account", key.constData(), nullptr);
    if (saved) return true;
    error = "System password store is unavailable or locked.";
    if (failure) g_error_free(failure);
    return false;
#else
    error = "System password store is unavailable or locked.";
    return false;
#endif
}

bool SystemCredentialStore::remove(const QString& profile, const QString& name, QString& error) {
    const auto account = accountName(profile, name);
#ifdef Q_OS_MACOS
    auto* attributes = query(account);
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
} // namespace podlord
