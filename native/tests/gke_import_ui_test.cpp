#include "kubeconfig_store.h"
#include "workspace.h"
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QTest>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rsa.h>
#include <memory>

using namespace podlord;

namespace {
QByteArray privateKey() {
    std::unique_ptr<EVP_PKEY_CTX, decltype(&EVP_PKEY_CTX_free)> context(EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, nullptr), EVP_PKEY_CTX_free);
    EVP_PKEY* raw = nullptr;
    if (!context || EVP_PKEY_keygen_init(context.get()) != 1 || EVP_PKEY_CTX_set_rsa_keygen_bits(context.get(), 2048) != 1
        || EVP_PKEY_keygen(context.get(), &raw) != 1) return {};
    std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)> key(raw, EVP_PKEY_free);
    std::unique_ptr<BIO, decltype(&BIO_free)> output(BIO_new(BIO_s_mem()), BIO_free);
    if (!output || PEM_write_bio_PrivateKey(output.get(), key.get(), nullptr, nullptr, 0, nullptr, nullptr) != 1) return {};
    BUF_MEM* memory = nullptr;
    BIO_get_mem_ptr(output.get(), &memory);
    return memory ? QByteArray(memory->data, memory->length) : QByteArray{};
}

bool run(const QString& scenario) {
    QTemporaryDir root;
    if (!root.isValid()) return false;
    const auto profile = root.filePath("profile");
    KubeconfigStore store(profile);
    const auto source = store.importText(root.filePath("gke.yaml"),
        "apiVersion: v1\nkind: Config\nclusters:\n- name: google\n  cluster:\n    server: https://127.0.0.1:9\n"
        "users:\n- name: google\n  user:\n    exec:\n      apiVersion: client.authentication.k8s.io/v1beta1\n"
        "      command: gke-gcloud-auth-plugin\ncontexts:\n- name: google\n  context:\n    cluster: google\n    user: google\n");
    if (!std::holds_alternative<SourceSnapshot>(source)) return false;
    const auto contextId = std::get<SourceSnapshot>(source).contexts.first().id;
    const auto key = privateKey();
    if (key.isEmpty()) return false;
    const auto keyPath = root.filePath("selected-key.json");
    const auto json = scenario == "invalid" ? QByteArray("not a service-account key") : QJsonDocument(QJsonObject{
        {"type", "service_account"}, {"client_email", "podlord@example.iam.gserviceaccount.com"},
        {"private_key_id", "0123456789abcdef"}, {"private_key", QString::fromUtf8(key)},
        {"token_uri", "https://oauth2.googleapis.com/token"}}).toJson();
    QFile selected(keyPath);
    if (!selected.open(QIODevice::WriteOnly) || selected.write(json) != json.size()) return false;
    selected.close();

    Workspace workspace(profile);
    if (!QTest::qWaitFor([&] { return !workspace.busy() && workspace.contexts().size() == 1; }, 5000)) return false;
    const auto target = scenario == "wrong_context" ? QString("ctx:") + QString(64, '0') : contextId;
    if (!workspace.importGkeServiceAccount(target, keyPath)
        || !QTest::qWaitFor([&] { return !workspace.busy(); }, 5000)) return false;
    const auto stored = store.hasGkeServiceAccount(contextId);
    if (!std::holds_alternative<bool>(stored)) return false;
    if (scenario == "invalid" || scenario == "wrong_context")
        return !workspace.sourceImportError().isEmpty() && !workspace.sourceImportError().contains(keyPath)
            && !std::get<bool>(stored);

    const QFileInfo privateCopy(profile + "/kubeconfigs/gke-" + contextId.mid(4) + ".cred");
    if (!std::get<bool>(stored) || !privateCopy.isFile()
        || privateCopy.permissions() & (QFileDevice::ReadGroup | QFileDevice::WriteGroup | QFileDevice::ReadOther | QFileDevice::WriteOther)
        || !workspace.sourceImportError().isEmpty() || !workspace.sourceImportNotice().contains("private app storage")
        || workspace.sourceImportNotice().contains(keyPath) || QString::fromUtf8(json).contains(workspace.sourceImportNotice())) return false;
    if (scenario == "success") return true;
    if (scenario == "removal_failure") {
        if (!QFile::remove(privateCopy.filePath()) || !QDir().mkdir(privateCopy.filePath())) return false;
        const auto removed = store.removeContext(contextId, {});
        const auto* failure = std::get_if<Failure>(&removed);
        const auto catalog = SessionStore(profile).list();
        return failure && failure->code == StoreError::CleanupFailed && QFileInfo(privateCopy.filePath()).isDir()
            && std::holds_alternative<SessionCatalog>(catalog) && std::get<SessionCatalog>(catalog).removedContexts.contains(contextId);
    }
    const auto removed = store.removeContext(contextId, {});
    const auto after = store.hasGkeServiceAccount(contextId);
    return std::holds_alternative<SessionCatalog>(removed) && std::holds_alternative<bool>(after) && !std::get<bool>(after);
}
} // namespace

int main(int argc, char** argv) {
    QGuiApplication app(argc, argv);
    if (argc != 2) return 2;
    const auto scenario = QString::fromUtf8(argv[1]);
    if (scenario != "success" && scenario != "invalid" && scenario != "wrong_context" && scenario != "removal" && scenario != "removal_failure") return 2;
    return run(scenario) ? 0 : 1;
}
