#include "native_oci.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QLockFile>
#include <QTemporaryDir>
#include <QUrlQuery>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rsa.h>
#include <memory>

using namespace podlord;

namespace {
QByteArray privateKey(int bits = 2048, bool elliptic = false) {
    EVP_PKEY* raw = elliptic ? EVP_PKEY_Q_keygen(nullptr, nullptr, "EC", "prime256v1") : EVP_RSA_gen(bits);
    if (!raw) return {};
    std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)> key(raw, EVP_PKEY_free);
    std::unique_ptr<BIO, decltype(&BIO_free)> output(BIO_new(BIO_s_mem()), BIO_free);
    if (!output || PEM_write_bio_PrivateKey(output.get(), key.get(), nullptr, nullptr, 0, nullptr, nullptr) != 1) return {};
    BUF_MEM* memory = nullptr;
    BIO_get_mem_ptr(output.get(), &memory);
    return memory ? QByteArray(memory->data, memory->length) : QByteArray{};
}

ExecCommand command(const QByteArray& key) {
    ExecCommand value;
    value.program = "/usr/bin/oci";
    value.arguments = {"ce", "cluster", "generate-token", "--cluster-id", "ocid1.cluster.oc1..example"};
    value.environment = {{"OCI_CLI_REGION", "eu-frankfurt-1"}, {"OCI_CLI_USER", "ocid1.user.oc1..example"},
        {"OCI_CLI_TENANCY", "ocid1.tenancy.oc1..example"},
        {"OCI_CLI_FINGERPRINT", "00:11:22:33:44:55:66:77:88:99:aa:bb:cc:dd:ee:ff"},
        {"OCI_CLI_KEY_CONTENT", QString::fromUtf8(key)}};
    return value;
}

ClusterConnection connection(const ExecCommand& value) {
    ClusterConnection result;
    result.exec = value;
    const auto parsed = nativeOciConfig(value);
    if (const auto* login = std::get_if<std::optional<OciLogin>>(&parsed)) result.exec->oci = *login;
    return result;
}

bool importedCredential(const QString& scenario, const QByteArray& key) {
    QTemporaryDir root;
    if (!root.isValid()) return false;
    const auto profile = root.filePath("profile");
    KubeconfigStore store(profile);
    QByteArray args = QByteArray("      args:\n      - ce\n      - cluster\n      - ")
        + (scenario == "import_bad_action" ? "inspect" : "generate-token")
        + "\n      - --cluster-id\n      - "
        + (scenario == "import_bad_cluster" ? "invalid" : "ocid1.cluster.oc1..example") + "\n"
        + (scenario == "import_profile" ? QByteArray("      - --profile=WORK\n") : QByteArray{});
    if (scenario == "import_missing_args") args.clear();
    if (scenario == "import_scalar_args") args = "      args: wrong\n";
    if (scenario == "import_mapping_arg") args = "      args:\n      - {bad: value}\n";
    if (scenario == "import_unknown_option") args += "      - --unknown\n      - value\n";
    if (scenario == "import_profile_separate") args += "      - --profile\n      - WORK\n";
    if (scenario == "import_profile_duplicate") args += "      - --profile=WORK\n      - --profile=OTHER\n";
    if (scenario == "import_profile_missing") args += "      - --profile\n";
    if (scenario == "import_profile_invalid") args += "      - --profile=BAD/NAME\n";
    QByteArray env = scenario == "import_scalar_env" ? QByteArray("      env: wrong\n")
        : QByteArray("      env:\n      - name: OCI_CLI_EXTRA\n        value: ignored\n");
    if (scenario == "import_passphrase") env += "      - name: OCI_CLI_PASSPHRASE\n        value: secret\n";
    if (scenario == "import_other_auth") env += "      - name: OCI_CLI_AUTH\n        value: instance_principal\n";
    const QByteArray user = scenario == "import_missing_exec" ? QByteArray("    token: static\n")
        : QByteArray("    exec:\n      apiVersion: client.authentication.k8s.io/v1beta1\n      command: ")
            + (scenario == "import_non_oci" ? "kubectl" : "oci") + "\n" + args + env;
    const QByteArray kubeconfig = QByteArray("apiVersion: v1\nkind: Config\nclusters:\n- name: oracle\n  cluster:\n    server: https://127.0.0.1:9\n"
        "users:\n- name: oracle\n  user:\n") + user
        + "contexts:\n- name: oracle\n  context:\n    cluster: oracle\n    user: oracle\n";
    const auto source = store.importText(root.filePath("source.yaml"), QString::fromUtf8(kubeconfig));
    if (!std::holds_alternative<SourceSnapshot>(source) || std::get<SourceSnapshot>(source).contexts.size() != 1) return false;
    const auto contextId = std::get<SourceSnapshot>(source).contexts.first().id;
    const auto credentialPath = profile + "/kubeconfigs/oci-" + contextId.mid(4) + ".cred";
    const auto configPath = root.filePath("oci-config");
    const auto keyPath = root.filePath("oci-key.pem");
    const QByteArray config = QByteArray(scenario == "import_profile" || scenario == "import_profile_separate" ? "[WORK]\n" : "[DEFAULT]\n")
        + "user=ocid1.user.oc1..example\ntenancy=ocid1.tenancy.oc1..example\n"
          "fingerprint=00:11:22:33:44:55:66:77:88:99:aa:bb:cc:dd:ee:ff\nregion=eu-frankfurt-1\nkey_file=/unused/path\n";
    QFile configFile(configPath), keyFile(keyPath);
    if (!configFile.open(QIODevice::WriteOnly) || configFile.write(scenario == "import_bad_config" ? QByteArray("[DEFAULT]\nuser=missing\n") : config) != (scenario == "import_bad_config" ? QByteArray("[DEFAULT]\nuser=missing\n").size() : config.size())
        || !keyFile.open(QIODevice::WriteOnly)) return false;
    const auto selectedKey = scenario == "import_bad_key" ? QByteArray("not a PEM key") : key;
    if (keyFile.write(selectedKey) != selectedKey.size()) return false;
    configFile.close(); keyFile.close();
    if (scenario == "import_has_invalid_id") return std::holds_alternative<Failure>(store.hasOciCredential("invalid"));
    if (scenario == "import_has_missing") {
        const auto found = store.hasOciCredential(contextId);
        return std::holds_alternative<bool>(found) && !std::get<bool>(found);
    }
    if (scenario == "import_has_bad_profile") {
        const auto blocked = root.filePath("blocked-profile");
        QFile file(blocked);
        if (!file.open(QIODevice::WriteOnly)) return false;
        file.close();
        return std::holds_alternative<Failure>(KubeconfigStore(blocked).hasOciCredential(contextId));
    }
    if (scenario == "import_symlink" || scenario == "import_has_symlink") {
        if (!QFile(keyPath).link(credentialPath)) return false;
        if (scenario == "import_has_symlink")
            return std::holds_alternative<Failure>(store.hasOciCredential(contextId));
    }
    if (scenario == "import_target_directory" && !QDir().mkdir(credentialPath)) return false;
    if (scenario == "import_bad_profile") {
        if (!QDir().rename(profile, profile + "-saved")) return false;
        QFile blocked(profile);
        if (!blocked.open(QIODevice::WriteOnly)) return false;
        blocked.close();
    }
    std::unique_ptr<QLockFile> lock;
    if (scenario == "import_locked") {
        lock = std::make_unique<QLockFile>(profile + "/kubeconfigs/sources.lock");
        lock->setStaleLockTime(0);
        if (!lock->tryLock(0)) return false;
    }
    const auto imported = store.importOciCredentials(scenario == "import_wrong_context" ? "ctx:" + QString(64, '0')
            : scenario == "import_invalid_id" ? "invalid" : contextId,
        scenario == "import_missing_file" ? root.filePath("missing") : configPath,
        scenario == "import_missing_key_file" ? root.filePath("missing-key") : keyPath);
    if (scenario == "import_bad_config" || scenario == "import_bad_key" || scenario == "import_non_oci"
        || scenario == "import_wrong_context" || scenario == "import_missing_file"
        || scenario == "import_missing_key_file" || scenario == "import_invalid_id"
        || scenario == "import_missing_exec" || scenario == "import_missing_args" || scenario == "import_scalar_args"
        || scenario == "import_mapping_arg" || scenario == "import_scalar_env" || scenario == "import_bad_action"
        || scenario == "import_bad_cluster" || scenario == "import_unknown_option" || scenario == "import_symlink"
        || scenario == "import_target_directory" || scenario == "import_bad_profile" || scenario == "import_locked"
        || scenario == "import_profile_duplicate" || scenario == "import_profile_missing" || scenario == "import_profile_invalid"
        || scenario == "import_passphrase" || scenario == "import_other_auth")
        return std::holds_alternative<Failure>(imported);
    if (!std::holds_alternative<SourceSnapshot>(imported)) return false;
    const QFileInfo credential(credentialPath);
    if (!credential.isFile() || (credential.permissions() & (QFileDevice::ReadGroup | QFileDevice::WriteGroup
        | QFileDevice::ReadOther | QFileDevice::WriteOther)) != QFileDevice::Permissions{}) return false;
    if (scenario == "import_repeat" && !std::holds_alternative<SourceSnapshot>(store.importOciCredentials(contextId, configPath, keyPath))) return false;
    if (scenario == "import_remove")
        return std::holds_alternative<SessionCatalog>(store.removeContext(contextId, {})) && !QFileInfo::exists(credentialPath);
    if (scenario == "import_remove_failure") {
        if (!QFile::remove(credentialPath) || !QDir().mkdir(credentialPath)) return false;
        const auto removed = store.removeContext(contextId, {});
        const auto catalog = SessionStore(profile).list();
        const auto* failure = std::get_if<Failure>(&removed);
        return failure && failure->code == StoreError::CleanupFailed && errorName(failure->code) == "CleanupFailed"
            && std::holds_alternative<SessionCatalog>(catalog) && std::get<SessionCatalog>(catalog).removedContexts.contains(contextId)
            && QFileInfo(credentialPath).isDir() && QFileInfo(keyPath).isFile();
    }
    if (scenario == "import_corrupt") {
        QFile damaged(credentialPath);
        if (!damaged.open(QIODevice::WriteOnly | QIODevice::Truncate) || damaged.write("bad") != 3) return false;
        damaged.close();
        return std::holds_alternative<Failure>(store.connection(contextId));
    }
    const auto resolved = store.connection(contextId);
    const auto* ready = std::get_if<ClusterConnection>(&resolved);
    if (!ready || !ready->exec || !ready->exec->oci || ready->exec->oci->privateKey != key
        || ready->exec->oci->region != "eu-frankfurt-1") return false;
    const auto metadata = QJsonDocument(sourceJson(std::get<SourceSnapshot>(imported))).toJson();
    return !metadata.contains("PRIVATE KEY") && !metadata.contains("OCI_CLI_KEY_CONTENT");
}

bool verifies(const Result<ClusterConnection>& result, const QByteArray& pem, QDateTime time) {
    const auto* ready = std::get_if<ClusterConnection>(&result);
    if (!ready || !ready->credentialReady || !ready->authorization.startsWith("Bearer ")
        || ready->expiresAt != time.addSecs(240) || !ready->exec || !ready->exec->oci
        || !ready->exec->oci->privateKey.isEmpty() || ready->exec->environment.contains("OCI_CLI_KEY_CONTENT")) return false;
    const auto url = QUrl::fromEncoded(QByteArray::fromBase64(ready->authorization.mid(7), QByteArray::Base64UrlEncoding));
    if (url.scheme() != "https" || url.host() != "containerengine.eu-frankfurt-1.oraclecloud.com"
        || url.path() != "/cluster_request/ocid1.cluster.oc1..example") return false;
    const QUrlQuery query(url);
    const auto date = query.queryItemValue("date", QUrl::FullyDecoded).toUtf8();
    const auto authorization = query.queryItemValue("authorization", QUrl::FullyDecoded).toUtf8();
    const auto prefix = QByteArray("Signature version=\"1\",keyId=\"ocid1.tenancy.oc1..example/ocid1.user.oc1..example/")
        + "00:11:22:33:44:55:66:77:88:99:aa:bb:cc:dd:ee:ff\",algorithm=\"rsa-sha256\",headers=\"(request-target) host date\",signature=\"";
    if (date != "Thu, 15 Oct 2026 19:00:00 GMT" || !authorization.startsWith(prefix) || !authorization.endsWith('"')) return false;
    const auto signedBytes = QByteArray::fromBase64(authorization.mid(prefix.size(), authorization.size() - prefix.size() - 1));
    const auto message = QByteArray("(request-target): get /cluster_request/ocid1.cluster.oc1..example\n")
        + "host: containerengine.eu-frankfurt-1.oraclecloud.com\ndate: " + date;
    std::unique_ptr<BIO, decltype(&BIO_free)> input(BIO_new_mem_buf(pem.constData(), pem.size()), BIO_free);
    std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)> key(input ? PEM_read_bio_PrivateKey(input.get(), nullptr, nullptr, nullptr) : nullptr, EVP_PKEY_free);
    std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> context(EVP_MD_CTX_new(), EVP_MD_CTX_free);
    return key && context && EVP_DigestVerifyInit(context.get(), nullptr, EVP_sha256(), nullptr, key.get()) == 1
        && EVP_DigestVerifyUpdate(context.get(), message.constData(), message.size()) == 1
        && EVP_DigestVerifyFinal(context.get(), reinterpret_cast<const unsigned char*>(signedBytes.constData()), signedBytes.size()) == 1;
}
} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    if (argc != 2) return 2;
    const QString scenario = QString::fromUtf8(argv[1]);
    const auto key = privateKey(scenario == "parse_weak_key" || scenario == "sign_weak_key" ? 1024 : 2048,
        scenario == "parse_ec_key" || scenario == "sign_ec_key");
    if (key.isEmpty()) return 1;
    auto value = command(key);
    const auto time = QDateTime::fromString("2026-10-15T19:00:00Z", Qt::ISODate);
#ifdef PODLORD_OPENSSL_FAULT_TESTS
    if (scenario == "sign_bio_failure") {
        const auto imported = connection(value);
        qputenv("PODLORD_TEST_BIO_ALLOCATION_FAILURE", "1");
        const auto result = nativeOciToken(imported, time);
        qunsetenv("PODLORD_TEST_BIO_ALLOCATION_FAILURE");
        return std::holds_alternative<Failure>(result) ? 0 : 1;
    }
#endif
    bool passed = false;
    if (scenario.startsWith("import_")) {
        passed = importedCredential(scenario, key);
    } else if (scenario == "parse_standard") {
        const auto parsed = nativeOciConfig(value);
        const auto* login = std::get_if<std::optional<OciLogin>>(&parsed);
        passed = login && *login && (*login)->cluster == "ocid1.cluster.oc1..example"
            && (*login)->region == "eu-frankfurt-1" && (*login)->privateKey == key;
    } else if (scenario == "parse_equals") {
        value.arguments = {"ce", "cluster", "generate-token", "--cluster-id=ocid1.cluster.oc1..example",
            "--region=eu-frankfurt-1", "--output=json"};
        const auto result = nativeOciConfig(value);
        passed = std::holds_alternative<std::optional<OciLogin>>(result) && std::get<std::optional<OciLogin>>(result).has_value();
    } else if (scenario == "parse_other_program") {
        value.program = "kubectl";
        const auto result = nativeOciConfig(value);
        passed = std::holds_alternative<std::optional<OciLogin>>(result) && !std::get<std::optional<OciLogin>>(result);
    } else if (scenario == "parse_other_action") {
        value.arguments[2] = "get";
        const auto result = nativeOciConfig(value);
        passed = std::holds_alternative<std::optional<OciLogin>>(result) && !std::get<std::optional<OciLogin>>(result);
    } else if (scenario == "parse_oci_exe") {
        value.program = "/usr/bin/oci.exe";
        const auto result = nativeOciConfig(value);
        passed = std::holds_alternative<std::optional<OciLogin>>(result) && std::get<std::optional<OciLogin>>(result).has_value();
    } else if (scenario == "parse_short_args") {
        value.arguments = {"ce", "cluster"};
        const auto result = nativeOciConfig(value);
        passed = std::holds_alternative<std::optional<OciLogin>>(result) && !std::get<std::optional<OciLogin>>(result);
    } else if (scenario == "parse_missing_cluster") {
        value.arguments = {"ce", "cluster", "generate-token"};
        passed = std::holds_alternative<Failure>(nativeOciConfig(value));
    } else if (scenario == "parse_region_option") {
        value.arguments += {"--region", "us-ashburn-1"};
        const auto result = nativeOciConfig(value);
        const auto* login = std::get_if<std::optional<OciLogin>>(&result);
        passed = login && *login && (*login)->region == "us-ashburn-1";
    } else if (scenario == "parse_output_separate") {
        value.arguments += {"--output", "json"};
        const auto result = nativeOciConfig(value);
        passed = std::holds_alternative<std::optional<OciLogin>>(result) && std::get<std::optional<OciLogin>>(result).has_value();
    } else if (scenario == "parse_empty_value") {
        value.arguments.append("--output=");
        passed = std::holds_alternative<Failure>(nativeOciConfig(value));
    } else if (scenario == "parse_option_as_value") {
        value.arguments += {"--region", "--output=json"};
        passed = std::holds_alternative<Failure>(nativeOciConfig(value));
    } else if (scenario == "parse_nul_value") {
        value.arguments.append("--region=" + QString::fromUtf8(QByteArray("us\0-east-1", 9)));
        passed = std::holds_alternative<Failure>(nativeOciConfig(value));
    } else if (scenario == "parse_api_key_auth") {
        value.environment["OCI_CLI_AUTH"] = "api_key";
        const auto result = nativeOciConfig(value);
        passed = std::holds_alternative<std::optional<OciLogin>>(result) && std::get<std::optional<OciLogin>>(result).has_value();
    } else if (scenario == "parse_unknown_option") {
        value.arguments += {"--profile", "other"};
        passed = std::holds_alternative<Failure>(nativeOciConfig(value));
    } else if (scenario == "parse_missing_value") {
        value.arguments.append("--region");
        passed = std::holds_alternative<Failure>(nativeOciConfig(value));
    } else if (scenario == "parse_duplicate") {
        value.arguments += {"--cluster-id", "ocid1.cluster.oc1..other"};
        passed = std::holds_alternative<Failure>(nativeOciConfig(value));
    } else if (scenario == "parse_bad_output") {
        value.arguments.append("--output=yaml");
        passed = std::holds_alternative<Failure>(nativeOciConfig(value));
    } else if (scenario == "parse_bad_cluster") {
        value.arguments[4] = "../other";
        passed = std::holds_alternative<Failure>(nativeOciConfig(value));
    } else if (scenario == "parse_bad_region") {
        value.environment["OCI_CLI_REGION"] = "../other";
        passed = std::holds_alternative<Failure>(nativeOciConfig(value));
    } else if (scenario == "parse_bad_user") {
        value.environment["OCI_CLI_USER"] = "other";
        passed = std::holds_alternative<Failure>(nativeOciConfig(value));
    } else if (scenario == "parse_bad_tenancy") {
        value.environment["OCI_CLI_TENANCY"] = "other";
        passed = std::holds_alternative<Failure>(nativeOciConfig(value));
    } else if (scenario == "parse_bad_fingerprint") {
        value.environment["OCI_CLI_FINGERPRINT"] = "not-a-fingerprint";
        passed = std::holds_alternative<Failure>(nativeOciConfig(value));
    } else if (scenario == "parse_missing_key") {
        value.environment.remove("OCI_CLI_KEY_CONTENT");
        passed = std::holds_alternative<Failure>(nativeOciConfig(value));
    } else if (scenario == "parse_bad_key") {
        value.environment["OCI_CLI_KEY_CONTENT"] = "invalid";
        passed = std::holds_alternative<Failure>(nativeOciConfig(value));
    } else if (scenario == "parse_weak_key" || scenario == "parse_ec_key") {
        passed = std::holds_alternative<Failure>(nativeOciConfig(value));
    } else if (scenario == "parse_profile") {
        value.environment["OCI_CLI_PROFILE"] = "other";
        passed = std::holds_alternative<Failure>(nativeOciConfig(value));
    } else if (scenario == "parse_key_file") {
        value.environment["OCI_CLI_KEY_FILE"] = "/private/key.pem";
        passed = std::holds_alternative<Failure>(nativeOciConfig(value));
    } else if (scenario == "parse_config_file") {
        value.environment["OCI_CLI_CONFIG_FILE"] = "/private/config";
        passed = std::holds_alternative<Failure>(nativeOciConfig(value));
    } else if (scenario == "parse_passphrase") {
        value.environment["OCI_CLI_PASSPHRASE"] = "secret";
        passed = std::holds_alternative<Failure>(nativeOciConfig(value));
    } else if (scenario == "parse_empty_key") {
        value.environment["OCI_CLI_KEY_CONTENT"] = "";
        passed = std::holds_alternative<Failure>(nativeOciConfig(value));
    } else if (scenario == "parse_oversized_key") {
        value.environment["OCI_CLI_KEY_CONTENT"] = QString(32769, 'x');
        passed = std::holds_alternative<Failure>(nativeOciConfig(value));
    } else if (scenario == "parse_long_cluster") {
        value.arguments[4] = "ocid1.cluster." + QString(250, 'a');
        passed = std::holds_alternative<Failure>(nativeOciConfig(value));
    } else if (scenario == "parse_cluster_control") {
        value.arguments[4] = "ocid1.cluster.oc1..\ninvalid";
        passed = std::holds_alternative<Failure>(nativeOciConfig(value));
    } else if (scenario == "parse_other_auth") {
        value.environment["OCI_CLI_AUTH"] = "instance_principal";
        passed = std::holds_alternative<Failure>(nativeOciConfig(value));
    } else if (scenario == "parse_partial_environment") {
        value.environment.remove("OCI_CLI_USER");
        passed = std::holds_alternative<Failure>(nativeOciConfig(value));
    } else if (scenario == "parse_local_environment") {
        value.environment.clear();
        qputenv("OCI_CLI_REGION", "eu-frankfurt-1");
        qputenv("OCI_CLI_USER", "ocid1.user.oc1..example");
        qputenv("OCI_CLI_TENANCY", "ocid1.tenancy.oc1..example");
        qputenv("OCI_CLI_FINGERPRINT", "00:11:22:33:44:55:66:77:88:99:aa:bb:cc:dd:ee:ff");
        qputenv("OCI_CLI_KEY_CONTENT", key);
        qunsetenv("OCI_CLI_AUTH");
        qunsetenv("OCI_CLI_KEY_FILE");
        qunsetenv("OCI_CLI_CONFIG_FILE");
        qunsetenv("OCI_CLI_PROFILE");
        qunsetenv("OCI_CLI_PASSPHRASE");
        const auto result = nativeOciConfig(value);
        const auto* login = std::get_if<std::optional<OciLogin>>(&result);
        passed = login && *login && (*login)->privateKey == key;
    } else if (scenario == "parse_missing_region") {
        value.environment.remove("OCI_CLI_REGION");
        passed = std::holds_alternative<Failure>(nativeOciConfig(value));
    } else if (scenario == "parse_missing_tenancy") {
        value.environment.remove("OCI_CLI_TENANCY");
        passed = std::holds_alternative<Failure>(nativeOciConfig(value));
    } else if (scenario == "parse_missing_fingerprint") {
        value.environment.remove("OCI_CLI_FINGERPRINT");
        passed = std::holds_alternative<Failure>(nativeOciConfig(value));
    } else if (scenario == "sign_reference") {
        passed = verifies(nativeOciToken(connection(value), time), key, time);
    } else if (scenario == "sign_missing") {
        passed = std::holds_alternative<Failure>(nativeOciToken(ClusterConnection{}, time));
    } else if (scenario == "sign_missing_oci") {
        ClusterConnection missing;
        missing.exec.emplace();
        passed = std::holds_alternative<Failure>(nativeOciToken(missing, time));
    } else if (scenario == "sign_invalid_time") {
        passed = std::holds_alternative<Failure>(nativeOciToken(connection(value), QDateTime{}));
    } else if (scenario == "sign_invalid_key") {
        auto missing = connection(value);
        missing.exec->oci->privateKey = "invalid";
        passed = std::holds_alternative<Failure>(nativeOciToken(missing, time));
    } else if (scenario == "sign_weak_key" || scenario == "sign_ec_key") {
        auto weak = connection(command(privateKey()));
        weak.exec->oci->privateKey = key;
        passed = std::holds_alternative<Failure>(nativeOciToken(weak, time));
    } else if (scenario == "sign_utc_offset") {
        const auto offset = QDateTime::fromString("2026-10-15T21:00:00+02:00", Qt::ISODate);
        passed = verifies(nativeOciToken(connection(value), offset), key, time);
    } else {
        return 2;
    }
    return passed ? 0 : 1;
}
