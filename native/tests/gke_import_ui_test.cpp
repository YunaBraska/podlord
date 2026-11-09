#include "kubeconfig_store.h"
#include "workspace.h"
#include "ui_input.h"
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QUrl>
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
#if !defined(Q_OS_IOS) && !defined(Q_OS_ANDROID) && !defined(PODLORD_MAC_APP_STORE)
    const auto original = store.connection(contextId);
    const auto* originalConnection = std::get_if<ClusterConnection>(&original);
    if (!originalConnection || !originalConnection->exec || originalConnection->exec->gke) return false;
#endif
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
    const bool desktopUi = scenario == "desktop_settings";
    std::unique_ptr<QQmlApplicationEngine> engine;
    QQuickWindow* window = nullptr;
    const auto target = scenario == "wrong_context" ? QString("ctx:") + QString(64, '0') : contextId;
    if (desktopUi) {
        engine = std::make_unique<QQmlApplicationEngine>();
        engine->rootContext()->setContextProperty("workspace", &workspace);
        engine->load(QUrl("qrc:/podlord/Main.qml"));
        if (engine->rootObjects().isEmpty()) return false;
        window = qobject_cast<QQuickWindow*>(engine->rootObjects().first());
        if (!window || !QTest::qWaitFor([&] { return window->isVisible(); }, 5000)) return false;
        if (!podlord::test::clickVisible(window, "settingsWorkspaceButton")
            || !podlord::test::selectSettingsSection(window, "sources")
            || !podlord::test::clickVisible(window, "sourceActionsMenuButton")
            || !podlord::test::clickVisible(window, "importGkeKeyFromSettings")) return false;
        auto* dialog = engine->rootObjects().first()->findChild<QObject*>("gkeImportDialog");
        auto* picker = engine->rootObjects().first()->findChild<QObject*>("gkeImportContext");
        if (!dialog || !picker || !QTest::qWaitFor([&] { return dialog->property("opened").toBool(); }, 2000)
            || picker->property("currentValue").toString() != contextId) return false;
        const auto frames = qEnvironmentVariable("PODLORD_GKE_IMPORT_FRAMES");
        if (!frames.isEmpty() && !window->grabWindow().save(QDir(frames).filePath(
            scenario + "-" + qEnvironmentVariable("QT_QUICK_CONTROLS_STYLE") + ".png"))) return false;
        if (!podlord::test::clickVisible(window, "chooseGkeKey")) return false;
        auto* chooser = engine->rootObjects().first()->findChild<QObject*>("gkeKeyChooser");
        // Only the external system chooser is simulated. Its accepted URL enters the real QML import handler.
        if (!chooser || !QTest::qWaitFor([&] { return chooser->property("visible").toBool(); }, 2000)
            || !chooser->setProperty("selectedFile", QUrl::fromLocalFile(keyPath))
            || !QMetaObject::invokeMethod(chooser, "accepted")
            || !QMetaObject::invokeMethod(chooser, "close")) return false;
        if (!QTest::qWaitFor([&] { return !workspace.busy() && !dialog->property("visible").toBool(); }, 5000)) {
            qWarning() << "GKE chooser completion failed:" << "busy" << workspace.busy()
                << "dialog visible" << dialog->property("visible") << "importing" << dialog->property("importing")
                << "error" << workspace.sourceImportError() << "notice" << workspace.sourceImportNotice();
            return false;
        }
        window->close();
    } else if (!workspace.importGkeServiceAccount(target, keyPath)
        || !QTest::qWaitFor([&] { return !workspace.busy(); }, 5000)) return false;
    const auto stored = store.hasGkeServiceAccount(contextId);
    if (!std::holds_alternative<bool>(stored)) return false;
    if (scenario == "invalid" || scenario == "wrong_context")
        return !workspace.sourceImportError().isEmpty() && !workspace.sourceImportError().contains(keyPath)
            && !std::get<bool>(stored);

    const QFileInfo privateCopy(profile + "/kubeconfigs/gke-" + contextId.mid(4) + ".cred");
    if (!std::get<bool>(stored) || !privateCopy.isFile()
        || privateCopy.permissions() & (QFileDevice::ReadGroup | QFileDevice::WriteGroup | QFileDevice::ReadOther | QFileDevice::WriteOther)
        || !workspace.sourceImportError().isEmpty() || !workspace.sourceImportNotice().isEmpty()) return false;
    const auto resolved = store.connection(contextId);
    const auto* connection = std::get_if<ClusterConnection>(&resolved);
    if (!connection || !connection->exec || !connection->exec->gke || !connection->exec->gke->serviceAccount
        || connection->credentialId != importedCredentialId(contextId) || connection->credentialReady) {
        std::fputs("Imported GKE key did not select context-isolated native authentication.\n", stderr);
        return false;
    }
    if (scenario == "success" || desktopUi) return true;
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
    if (scenario != "success" && scenario != "invalid" && scenario != "wrong_context" && scenario != "removal" && scenario != "removal_failure"
        && scenario != "desktop_main" && scenario != "desktop_settings") return 2;
    return run(scenario) ? 0 : 1;
}
