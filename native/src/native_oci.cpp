#include "native_oci.h"
#include <QFileInfo>
#include <QLocale>
#include <QMap>
#include <QRegularExpression>
#include <QSslKey>
#include <QUrlQuery>
#if defined(Q_OS_DARWIN)
#include <Security/Security.h>
#else
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <memory>
#endif

namespace podlord {
namespace {
Failure invalidKey() { return {StoreError::InvalidData, "The imported OCI RSA private key is invalid or unsupported; no Kubernetes request was sent."}; }

Result<QByteArray> signature(const QByteArray& pem, const QByteArray& message) {
    const QSslKey parsed(pem, QSsl::Rsa);
    if (parsed.isNull() || parsed.length() < 2048 || parsed.length() > 8192) return invalidKey();
#if defined(Q_OS_DARWIN)
    const auto der = parsed.toDer();
    const auto* bytes = reinterpret_cast<const UInt8*>(der.constData());
    CFDataRef data = CFDataCreate(kCFAllocatorDefault, bytes, der.size());
    if (!data) return invalidKey();
    const void* keys[] = {kSecAttrKeyType, kSecAttrKeyClass};
    const void* values[] = {kSecAttrKeyTypeRSA, kSecAttrKeyClassPrivate};
    CFDictionaryRef attributes = CFDictionaryCreate(kCFAllocatorDefault, keys, values, 2,
        &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    SecKeyRef key = attributes ? SecKeyCreateWithData(data, attributes, nullptr) : nullptr;
    if (attributes) CFRelease(attributes);
    CFRelease(data);
    if (!key) return invalidKey();
    CFDataRef input = CFDataCreate(kCFAllocatorDefault, reinterpret_cast<const UInt8*>(message.constData()), message.size());
    CFDataRef signedData = input ? SecKeyCreateSignature(key, kSecKeyAlgorithmRSASignatureMessagePKCS1v15SHA256, input, nullptr) : nullptr;
    if (input) CFRelease(input);
    CFRelease(key);
    if (!signedData) return invalidKey();
    const QByteArray result(reinterpret_cast<const char*>(CFDataGetBytePtr(signedData)), CFDataGetLength(signedData));
    CFRelease(signedData);
    return result;
#else
    std::unique_ptr<BIO, decltype(&BIO_free)> input(BIO_new_mem_buf(pem.constData(), pem.size()), BIO_free);
    std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)> key(input ? PEM_read_bio_PrivateKey(input.get(), nullptr, nullptr, nullptr) : nullptr, EVP_PKEY_free);
    std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> context(EVP_MD_CTX_new(), EVP_MD_CTX_free);
    if (!key || EVP_PKEY_base_id(key.get()) != EVP_PKEY_RSA || EVP_PKEY_bits(key.get()) < 2048 || !context
        || EVP_DigestSignInit(context.get(), nullptr, EVP_sha256(), nullptr, key.get()) != 1
        || EVP_DigestSignUpdate(context.get(), message.constData(), message.size()) != 1) return invalidKey();
    size_t size = 0;
    if (EVP_DigestSignFinal(context.get(), nullptr, &size) != 1 || size > 1024) return invalidKey();
    QByteArray result(static_cast<qsizetype>(size), '\0');
    if (EVP_DigestSignFinal(context.get(), reinterpret_cast<unsigned char*>(result.data()), &size) != 1) return invalidKey();
    result.resize(static_cast<qsizetype>(size));
    return result;
#endif
}

bool identifier(const QString& value, const QString& type) {
    return value.startsWith("ocid1." + type + '.') && value.size() <= 256
        && !value.contains(QRegularExpression("[^A-Za-z0-9._-]"));
}
} // namespace

Result<std::optional<OciLogin>> nativeOciConfig(const ExecCommand& command) {
    const auto executable = QFileInfo(command.program).fileName();
    if (executable.compare("oci", Qt::CaseInsensitive) != 0 && executable.compare("oci.exe", Qt::CaseInsensitive) != 0)
        return std::optional<OciLogin>{};
    if (command.arguments.size() < 3 || command.arguments.mid(0, 3) != QStringList{"ce", "cluster", "generate-token"})
        return std::optional<OciLogin>{};
    QMap<QString, QString> options;
    for (int i = 3; i < command.arguments.size(); ++i) {
        const auto argument = command.arguments[i];
        const auto equal = argument.indexOf('=');
        const auto name = equal < 0 ? argument : argument.left(equal);
        if (name != "--cluster-id" && name != "--region" && name != "--output")
            return Failure{StoreError::UnsupportedVersion, "This OCI credential option requires the external CLI and is unavailable in native sign-in."};
        const auto value = equal < 0 ? (++i < command.arguments.size() ? command.arguments[i] : QString{}) : argument.mid(equal + 1);
        if (value.isEmpty() || value.startsWith("--") || value.contains(QChar::Null) || options.contains(name))
            return Failure{StoreError::InvalidData, "An OCI credential option is missing, duplicated or invalid."};
        options.insert(name, value);
    }
    if (options.contains("--output") && options.value("--output") != "json")
        return Failure{StoreError::UnsupportedVersion, "Native OCI only supports JSON credential output."};
    const bool imported = command.environment.keys().contains("OCI_CLI_KEY_CONTENT") || command.environment.keys().contains("OCI_CLI_USER")
        || command.environment.keys().contains("OCI_CLI_TENANCY") || command.environment.keys().contains("OCI_CLI_FINGERPRINT")
        || command.environment.keys().contains("OCI_CLI_REGION");
    const auto setting = [&](const char* name) {
        return imported ? command.environment.value(QString::fromLatin1(name)) : QString::fromUtf8(qgetenv(name));
    };
    if ((!setting("OCI_CLI_AUTH").isEmpty() && setting("OCI_CLI_AUTH") != "api_key")
        || !setting("OCI_CLI_KEY_FILE").isEmpty() || !setting("OCI_CLI_CONFIG_FILE").isEmpty()
        || !setting("OCI_CLI_PROFILE").isEmpty() || !setting("OCI_CLI_PASSPHRASE").isEmpty())
        return Failure{StoreError::UnsupportedVersion, "Native OCI requires an unencrypted API key supplied as OCI_CLI_KEY_CONTENT; profiles, key files and environment-bound identities are not silently reused."};
    OciLogin login{options.value("--cluster-id"), options.value("--region", setting("OCI_CLI_REGION")),
        setting("OCI_CLI_USER"), setting("OCI_CLI_TENANCY"), setting("OCI_CLI_FINGERPRINT"), setting("OCI_CLI_KEY_CONTENT").toUtf8()};
    static const QRegularExpression region("^[a-z0-9]+(?:-[a-z0-9]+)+-[0-9]+$");
    static const QRegularExpression fingerprint("^(?:[0-9a-fA-F]{2}:){15}[0-9a-fA-F]{2}$");
    if (!identifier(login.cluster, "cluster") || !identifier(login.user, "user") || !identifier(login.tenancy, "tenancy")
        || !region.match(login.region).hasMatch() || !fingerprint.match(login.fingerprint).hasMatch())
        return Failure{StoreError::InvalidData, "Native OCI requires a valid cluster, region, user, tenancy and API-key fingerprint."};
    if (login.privateKey.isEmpty() || login.privateKey.size() > 32768 || QSslKey(login.privateKey, QSsl::Rsa).length() < 2048)
        return invalidKey();
    return std::optional<OciLogin>{std::move(login)};
}

Result<ClusterConnection> nativeOciToken(ClusterConnection connection, QDateTime now) {
    if (!connection.exec || !connection.exec->oci || !now.isValid())
        return Failure{StoreError::InvalidInput, "No valid OCI credential was confirmed."};
    const auto& oci = *connection.exec->oci;
    const auto host = "containerengine." + oci.region + ".oraclecloud.com";
    const auto path = "/cluster_request/" + oci.cluster;
    const auto date = QLocale::c().toString(now.toUTC(), "ddd, dd MMM yyyy HH:mm:ss 'GMT'").toUtf8();
    const auto signing = "(request-target): get " + path.toUtf8() + "\nhost: " + host.toUtf8() + "\ndate: " + date;
    const auto signedBytes = signature(oci.privateKey, signing);
    if (const auto* failure = std::get_if<Failure>(&signedBytes)) return *failure;
    const auto authorization = "Signature version=\"1\",keyId=\"" + oci.tenancy.toUtf8() + '/' + oci.user.toUtf8() + '/'
        + oci.fingerprint.toUtf8() + "\",algorithm=\"rsa-sha256\",headers=\"(request-target) host date\",signature=\""
        + std::get<QByteArray>(signedBytes).toBase64() + '"';
    QUrl url("https://" + host + path);
    QUrlQuery query;
    query.addQueryItem("authorization", QString::fromLatin1(authorization));
    query.addQueryItem("date", QString::fromLatin1(date));
    url.setQuery(query);
    connection.authorization = "Bearer " + url.toEncoded().toBase64(QByteArray::Base64UrlEncoding);
    connection.expiresAt = now.toUTC().addSecs(240);
    connection.credentialReady = true;
    connection.exec->oci->privateKey.clear();
    connection.exec->environment.remove("OCI_CLI_KEY_CONTENT");
    return connection;
}
} // namespace podlord
