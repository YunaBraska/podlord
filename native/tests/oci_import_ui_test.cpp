#include "kubeconfig_store.h"
#include "native_oci.h"
#include "resource_client.h"
#include "workspace.h"
#include "ui_input.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickWindow>
#include <QPointer>
#include <QTemporaryDir>
#include <QTest>
#include <QSslKey>
#include <QTcpServer>
#include <QTcpSocket>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rsa.h>
#include <cstdio>
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
bool writeFile(const QString& path, const QByteArray& bytes) {
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}
bool run(const QString& scenario) {
    QTemporaryDir root;
    if (!root.isValid()) return false;
    const auto profile = root.filePath("profile");
    KubeconfigStore store(profile);
    QTcpServer api;
    int requests = 0;
    const bool freshness = scenario.startsWith("freshness_");
    const bool recovery = scenario == "freshness_recovery";
    const auto detailPath = QStringLiteral("/api/v1/namespaces/default/secrets/kept");
    QJsonObject secret{{"apiVersion", "v1"}, {"kind", "Secret"},
        {"metadata", QJsonObject{{"name", "kept"}, {"namespace", "default"}, {"uid", "kept-uid"}, {"resourceVersion", "1"}}},
        {"data", QJsonObject{{"password", QString::fromLatin1(QByteArray("private-freshness-value").toBase64())}}}};
    if (recovery) {
        auto metadata = secret["metadata"].toObject();
        metadata["annotations"] = QJsonObject{{"recovery-note", "before"}};
        secret["metadata"] = metadata;
    }
    QByteArray authorizationA, authorizationB, patchAuthorization;
    QPointer<QTcpSocket> heldA, heldB;
    bool holdA = false, holdB = false, rejectBDetail = false;
    int patches = 0;
    int recoveryReadbacksA = 0, recoveryReadsB = 0;
    QList<QByteArray> recoveryWrites;
    const auto respond = [](QTcpSocket* socket, const QJsonObject& document, int status = 200) {
        const auto body = QJsonDocument(document).toJson(QJsonDocument::Compact);
        socket->write("HTTP/1.1 " + QByteArray::number(status) + " Result\r\nContent-Type: application/json\r\nContent-Length: "
            + QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body);
        socket->disconnectFromHost();
    };
    if (!api.listen(QHostAddress::LocalHost)) return false;
    QObject::connect(&api, &QTcpServer::newConnection, &api, [&] {
        while (auto* socket = api.nextPendingConnection()) {
            QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            QObject::connect(socket, &QTcpSocket::readyRead, socket, [&, socket] {
                const auto request = socket->property("request").toByteArray() + socket->readAll();
                socket->setProperty("request", request);
                const auto headerEnd = request.indexOf("\r\n\r\n");
                if (headerEnd < 0 || socket->property("answered").toBool()) return;
                if (recovery && request.startsWith("PATCH ")) {
                    int length = 0;
                    for (const auto& line : request.left(headerEnd).split('\n'))
                        if (line.toLower().startsWith("content-length:")) length = line.mid(15).trimmed().toInt();
                    if (request.size() - headerEnd - 4 < length) return;
                }
                socket->setProperty("answered", true);
                ++requests;
                if (freshness) {
                    QByteArray authorization;
                    for (const auto& line : request.split('\n'))
                        if (line.startsWith("Authorization:")) authorization = line.mid(14).trimmed();
                    if (authorizationA.isEmpty()) authorizationA = authorization;
                    if (authorization != authorizationA) authorizationB = authorization;
                    const auto path = QUrl::fromEncoded(request.split(' ')[1]).path();
                    if (path == detailPath) {
                        if (request.startsWith("PATCH ")) {
                            ++patches; patchAuthorization = authorization;
                            if (recovery) {
                                recoveryWrites.append(authorization);
                                if (authorization == authorizationA) {
                                    // The write receipt is lost; a concurrent update must be observed explicitly.
                                    auto metadata = secret["metadata"].toObject(); metadata["resourceVersion"] = "external-2";
                                    metadata["annotations"] = QJsonObject{{"recovery-note", "external"}}; secret["metadata"] = metadata;
                                    auto data = secret["data"].toObject(); data["retained"] = QString::fromLatin1(QByteArray("external-value").toBase64()); secret["data"] = data;
                                    socket->abort();
                                    return;
                                }
                                const auto patch = QJsonDocument::fromJson(request.mid(headerEnd + 4)).array();
                                const auto change = patch.size() == 3 ? patch[2].toObject() : QJsonObject{};
                                if (patch.size() != 3 || patch[0].toObject() != QJsonObject{{"op", "test"}, {"path", "/metadata/uid"}, {"value", "kept-uid"}}
                                    || patch[1].toObject() != QJsonObject{{"op", "test"}, {"path", "/metadata/resourceVersion"}, {"value", "external-2"}}
                                    || (change["op"] != "replace" && change["op"] != "add")
                                    || change["path"] != "/metadata/annotations/recovery-note" || change["value"] != "after") {
                                    respond(socket, secret, 409);
                                    return;
                                }
                                auto metadata = secret["metadata"].toObject();
                                auto annotations = metadata["annotations"].toObject(); annotations["recovery-note"] = change["value"];
                                metadata["annotations"] = annotations; secret["metadata"] = metadata;
                            }
                            if (!recovery) secret["immutable"] = false;
                            auto metadata = secret["metadata"].toObject(); metadata["resourceVersion"] = recovery ? "3" : "2"; secret["metadata"] = metadata;
                        } else if (recovery && patches > 0 && authorization == authorizationA) {
                            ++recoveryReadbacksA;
                            respond(socket, {}, 503);
                            return;
                        } else if (recovery && authorization != authorizationA && ++recoveryReadsB == 1) {
                            respond(socket, {}, 503);
                            return;
                        } else if (holdA && authorization == authorizationA) { heldA = socket; return; }
                        else if (holdB && authorization != authorizationA) { heldB = socket; return; }
                        else if (rejectBDetail && authorization != authorizationA) { respond(socket, {}, 503); return; }
                        respond(socket, secret);
                    } else if (authorization != authorizationA) {
                        // New credentials cannot refresh the retained collection in place of a detail GET.
                        respond(socket, {}, 403);
                    } else if (path == "/api") respond(socket, {{"versions", QJsonArray{"v1"}}});
                    else if (path == "/apis") respond(socket, {{"groups", QJsonArray{}}});
                    else if (path == "/api/v1") respond(socket, {{"resources", QJsonArray{QJsonObject{
                        {"name", "secrets"}, {"kind", "Secret"}, {"namespaced", true}, {"verbs", QJsonArray{"get", "list", "patch"}}}}}});
                    else respond(socket, {{"items", QJsonArray{secret}}, {"metadata", QJsonObject{}}});
                    return;
                }
                socket->write("HTTP/1.1 503 Service Unavailable\r\nContent-Length: 2\r\nConnection: close\r\n\r\n{}");
                socket->disconnectFromHost();
            });
        }
    });
    const auto sourcePath = root.filePath("oracle.yaml");
    auto yaml = QByteArray("apiVersion: v1\nkind: Config\nclusters:\n- name: oracle\n  cluster:\n    server: http://127.0.0.1:")
        + QByteArray::number(api.serverPort()) + "\n"
        "users:\n- name: oracle\n  user:\n    exec:\n      apiVersion: client.authentication.k8s.io/v1beta1\n"
        "      command: oci\n      args:\n      - ce\n      - cluster\n      - generate-token\n"
        "      - --cluster-id\n      - ocid1.cluster.oc1..example\n"
        "contexts:\n- name: oracle\n  context:\n    cluster: oracle\n    user: oracle\n";
    if (scenario == "identity") yaml += "- name: oracle-second\n  context:\n    cluster: oracle\n    user: oracle\n";
    if (!writeFile(sourcePath, yaml)) return false;
    const auto source = store.importFile(sourcePath);
    if (!std::holds_alternative<SourceSnapshot>(source)) return false;
    const auto contextId = std::get<SourceSnapshot>(source).contexts.first().id;
    const auto configPath = root.filePath("oci-config");
    const auto keyPath = root.filePath("oci-key.pem");
    const auto key = privateKey();
    if (key.isEmpty() || !writeFile(configPath,
        "[DEFAULT]\nuser=ocid1.user.oc1..example\ntenancy=ocid1.tenancy.oc1..example\n"
        "fingerprint=00:11:22:33:44:55:66:77:88:99:aa:bb:cc:dd:ee:ff\nregion=eu-frankfurt-1\n")
        || !writeFile(keyPath, scenario == "bad_key" ? QByteArray("not a PEM key") : key)) return false;
    if (scenario == "closed_credentials") {
        if (!std::holds_alternative<SourceSnapshot>(store.importOciCredentials(contextId, configPath, keyPath))) return false;
        const auto pending = store.connection(contextId);
        const auto* connection = std::get_if<ClusterConnection>(&pending);
        if (!connection) return false;
        ResourceClient client;
        client.enableRequests(false);
        for (const auto ready : {false, true}) {
            const auto authorized = ready ? nativeOciToken(*connection) : pending;
            const auto* value = std::get_if<ClusterConnection>(&authorized);
            if (!value) return false;
            client.open("retained", *value, {});
            if (!client.close("retained") || !client.forgetCredentials(value->credentialId)) return false;
            const auto forgotten = client.connection("retained");
            if (!forgotten || forgotten->credentialReady || !forgotten->authorization.isEmpty() || forgotten->exec
                || !forgotten->tls.privateKey().isNull()) return false;
        }
        return requests == 0;
    }
    Workspace workspace(profile);
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("workspace", &workspace);
    engine.load(QUrl("qrc:/podlord/Main.qml"));
    if (engine.rootObjects().isEmpty()) return false;
    auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
    if (!window || !QTest::qWaitFor([&] { return !workspace.busy() && workspace.contexts().size() == (scenario == "identity" ? 2 : 1); }, 5000)) return false;
    if (!test::clickVisible(window,"mapSources") || !test::clickVisible(window, "importOciKey")) return false;
    auto* dialog = window->findChild<QObject*>("ociImportDialog");
    auto* context = window->findChild<QQuickItem*>("ociImportContext");
    if (!dialog || !dialog->property("visible").toBool() || !context || context->property("currentValue").toString() != contextId)
        return false;
    if (!workspace.importOciCredentials(contextId, configPath, keyPath)
        || !QTest::qWaitFor([&] { return !workspace.busy(); }, 5000)) return false;
    const auto credential = store.hasOciCredential(contextId);
    if (scenario == "bad_key")
        return !workspace.sourceImportError().isEmpty() && dialog->property("visible").toBool()
            && std::holds_alternative<bool>(credential) && !std::get<bool>(credential);
    const auto connection = store.connection(contextId);
    const auto* ready = std::get_if<ClusterConnection>(&connection);
    if (!(workspace.sourceImportError().isEmpty() && workspace.sourceImportNotice().contains("OCI API key")
        && std::holds_alternative<bool>(credential) && std::get<bool>(credential)
        && ready && ready->exec && ready->exec->oci && ready->exec->oci->privateKey == key
        && QTest::qWaitFor([&] { return !dialog->property("visible").toBool(); }, 2000))) return false;
    if (scenario == "success") return true;
    if (scenario == "identity") {
        const auto secondId = std::get<SourceSnapshot>(source).contexts.last().id;
        if (!workspace.importOciCredentials(secondId, configPath, keyPath)
            || !QTest::qWaitFor([&] { return !workspace.busy(); }, 5000)) return false;
        const auto second = store.connection(secondId);
        const auto* other = std::get_if<ClusterConnection>(&second);
        return other && other->exec && other->exec->oci && ready->credentialId != other->credentialId
            && other->exec->oci->privateKey == key;
    }
    if (!workspace.openContext(contextId)
        || !QTest::qWaitFor([&] { return !workspace.busy() && workspace.authenticationRequired(); }, 5000)
        || requests != 0) { qWarning("OCI open: busy=%d auth=%d requests=%d",workspace.busy(),workspace.authenticationRequired(),requests); return false; }
    if (scenario == "removal_failure") {
        const auto privateCopy = profile + "/kubeconfigs/oci-" + contextId.mid(4) + ".cred";
        if (!QFile::remove(privateCopy) || !QDir().mkdir(privateCopy)
            || !workspace.requestSourceRemoval(contextId) || !workspace.confirmSourceRemoval()
            || !QTest::qWaitFor([&] { return !workspace.busy() && workspace.sourceRemoval().isEmpty() && workspace.contexts().isEmpty(); }, 5000)) return false;
        return workspace.sessions().isEmpty() && workspace.sourceImportError().contains("private credential")
            && QFileInfo(privateCopy).isDir() && QFileInfo::exists(keyPath) && requests == 0;
    }
    if (!workspace.refresh(true) || !QTest::qWaitFor([&] { return requests > 0 && !workspace.authenticationRunning(); }, 5000)
        || workspace.authenticationRequired()) { qWarning("OCI login: required=%d running=%d requests=%d",workspace.authenticationRequired(),workspace.authenticationRunning(),requests); return false; }
    if (freshness) {
        if (!QTest::qWaitFor([&] { return workspace.table()->rowCount() == 1 && !workspace.loading(); }, 7000)
            || !workspace.inspectPath(detailPath) || !QTest::qWaitFor([&] { return workspace.canEditYaml(); }, 5000)
            || !workspace.setInspectorPage("yaml")) { qWarning("OCI detail: rows=%d loading=%d editable=%d",workspace.table()->rowCount(),workspace.loading(),workspace.canEditYaml()); return false; }
        const auto cachedYaml = workspace.yamlText();
        if (recovery) {
            const auto failed = [&](const char* stage) {
                std::fprintf(stderr, "Freshness recovery failed at %s: patches=%d A-readbacks=%d B-reads=%d editing=%d reconcile=%d status=%s\n",
                    stage, patches, recoveryReadbacksA, recoveryReadsB, workspace.yamlEditing(), workspace.canReconcileYaml(), qPrintable(workspace.yamlApplyStatus()));
                return false;
            };
            auto draftText = cachedYaml; draftText.replace("before", "after");
            if (draftText == cachedYaml) return failed("mutable annotation fixture");
            if (!workspace.beginYamlEdit() || !workspace.setYamlDraft(draftText) || !workspace.previewYamlApply()
                || !QTest::qWaitFor([&] { return workspace.yamlApplyPreview(); }, 5000) || !workspace.confirmYamlApply(true)) return failed("initial A write");
            if (!QTest::qWaitFor([&] { return patches == 1 && recoveryReadbacksA == 1 && workspace.canReadBackYaml()
                    && workspace.yamlApplyStatus().contains("unresolved"); }, 7000)
                || !workspace.yamlEditing() || !workspace.yamlDirty() || workspace.canReconcileYaml()) return failed("unavailable A read-back");
            const auto replacement = privateKey();
            if (replacement.isEmpty() || replacement == key || !writeFile(keyPath, replacement)
                || !workspace.importOciCredentials(contextId, configPath, keyPath)
                || !QTest::qWaitFor([&] { return !workspace.busy() && workspace.authenticationRequired(); }, 5000)
                || !workspace.refresh(true) || !QTest::qWaitFor([&] {
                    return !workspace.busy() && !workspace.authenticationRunning() && !workspace.authenticationRequired() && !authorizationB.isEmpty();
                }, 7000) || authorizationB == authorizationA || recoveryReadsB != 0) return failed("authenticate B without detail");
            if (!workspace.yamlEditing() || workspace.yamlText() != draftText || workspace.canReconcileYaml()
                || workspace.previewYamlApply() || workspace.confirmYamlApply(true) || patches != 1) return failed("withheld B authority");
            if (!workspace.readBackYaml() || !QTest::qWaitFor([&] {
                    return recoveryReadsB == 1 && workspace.canReadBackYaml() && workspace.yamlApplyStatus().contains("unresolved");
                }, 5000) || !workspace.yamlEditing() || !workspace.setYamlDraft(draftText + "\n# editable after failed read-back\n")
                || !workspace.setYamlDraft(draftText) || workspace.canReconcileYaml() || workspace.previewYamlApply()
                || workspace.confirmYamlApply(true) || patches != 1) return failed("failed B read-back preserves draft only");
            if (!workspace.readBackYaml() || !QTest::qWaitFor([&] { return recoveryReadsB == 2 && workspace.canReconcileYaml(); }, 5000)
                || patches != 1 || workspace.confirmYamlApply(true)) return failed("reconcile current B read-back");
            if (!workspace.reconcileYaml() || !workspace.yamlReconcilePending() || workspace.yamlConflicts().isEmpty()) return failed("explicit reconciliation");
            const auto conflicts = workspace.yamlConflicts().size();
            for (int index = 0; index < conflicts; ++index) if (!workspace.chooseYamlConflict(index, true)) return failed("conflict choice");
            if (!workspace.yamlReconcileReady() || !workspace.finishYamlReconcile(true)
                || !QTest::qWaitFor([&] { return workspace.yamlApplyPreview(); }, 5000)
                || !workspace.yamlApplyTarget().contains("Original version: external-2") || patches != 1) return failed("new B preview without write");
            if (!workspace.confirmYamlApply(true) || !QTest::qWaitFor([&] { return patches == 2 && !workspace.yamlEditing(); }, 5000)) return failed("explicit B write");
            return recoveryWrites == QList<QByteArray>{authorizationA, authorizationB} && patchAuthorization == authorizationB
                && secret["metadata"].toObject()["annotations"].toObject()["recovery-note"] == "after" && !secret.contains("immutable")
                && secret["data"].toObject().contains("retained") && workspace.table()->rowCount() == 1;
        }
        const bool draft = scenario == "freshness_draft" || scenario == "freshness_preview";
        const auto draftText = cachedYaml + "\nimmutable: false\n";
        if (draft && (!workspace.beginYamlEdit() || !workspace.setYamlDraft(draftText))) return false;
        if (scenario == "freshness_preview" && (!workspace.previewYamlApply()
            || !QTest::qWaitFor([&] { return workspace.yamlApplyPreview(); }, 5000))) return false;
        const auto secretVisible = [&] {
            auto* values = workspace.valuesTable();
            for (int row = 0; row < values->rowCount(); ++row)
                for (int column = 0; column < values->columnCount(); ++column)
                    if (values->index(row, column).data().toString().contains("private-freshness-value")) return true;
            return false;
        };
        if (!workspace.revealValue("data/password", true) || !secretVisible()) return false;
        if (scenario == "freshness_late" || scenario == "freshness_after_b") {
            holdA = true;
            if (!workspace.refreshInspector() || !QTest::qWaitFor([&] { return !heldA.isNull(); }, 5000)) return false;
        }
        const auto replacement = privateKey();
        if (replacement.isEmpty() || replacement == key || !writeFile(keyPath, replacement)
            || !workspace.importOciCredentials(contextId, configPath, keyPath)
            || !QTest::qWaitFor([&] { return !workspace.busy() && workspace.authenticationRequired(); }, 5000)) return false;
        if (workspace.canEditYaml() || workspace.yamlEditing() != draft || workspace.beginYamlEdit()
            || workspace.revealValue("data/password", true) || secretVisible() || workspace.table()->rowCount() != 1
            || workspace.yamlText() != (draft ? draftText : cachedYaml)) return false;
        holdB = true;
        if (!workspace.refresh(true) || !QTest::qWaitFor([&] {
                return !workspace.busy() && !workspace.authenticationRunning() && !workspace.authenticationRequired()
                    && !authorizationB.isEmpty();
            }, 7000) || authorizationB == authorizationA) return false;
        if (workspace.canEditYaml() || workspace.yamlEditing() != draft || workspace.beginYamlEdit()
            || workspace.revealValue("data/password", true) || secretVisible() || patches != 0) return false;
        if (scenario == "freshness_draft") {
            auto* preview = window->findChild<QQuickItem*>("previewYaml");
            if (!preview || !QTest::qWaitFor([&] { return !preview->isEnabled(); }, 2000)) {
                qWarning("Apply preview must remain disabled before a fresh current-credential detail read.");
                return false;
            }
        }
        if (scenario == "freshness_preview") {
            if (workspace.confirmYamlApply(true) || patches != 0) return false;
        } else if (draft && workspace.previewYamlApply()) return false;
        if (draft && (!workspace.setYamlDraft(draftText + "\n# local edit while awaiting fresh authority\n")
            || !workspace.setYamlDraft(draftText) || !workspace.yamlDirty() || patches != 0)) return false;
        if (!workspace.refreshInspector() || !QTest::qWaitFor([&] { return !heldB.isNull(); }, 5000)) return false;
        if (scenario == "freshness_late") {
            auto old = secret;
            auto metadata = old["metadata"].toObject(); metadata["resourceVersion"] = "late-A"; old["metadata"] = metadata;
            respond(heldA, old);
            if (!QTest::qWaitFor([&] { return workspace.yamlText().contains("late-A"); }, 5000)
                || workspace.canEditYaml() || workspace.beginYamlEdit() || workspace.revealValue("data/password", true)
                || secretVisible() || workspace.table()->rowCount() != 1) return false;
        }
        holdB = false;
        respond(heldB, secret);
        if (!QTest::qWaitFor([&] { return workspace.revealValue("data/password", true); }, 5000)
            || workspace.yamlEditing() != draft || (!draft && !workspace.canEditYaml()) || !secretVisible()
            || (draft && workspace.yamlText() != draftText)) return false;
        if (scenario == "freshness_after_b") {
            const auto failed = [&](const char* stage) {
                std::fprintf(stderr, "Freshness after B failed at %s: editable=%d patches=%d status=%s\n",
                    stage, workspace.canEditYaml(), patches, qPrintable(workspace.inspectorStatus()));
                return false;
            };
            auto old = secret;
            auto metadata = old["metadata"].toObject(); metadata["resourceVersion"] = "late-A"; old["metadata"] = metadata;
            respond(heldA, old);
            if (!QTest::qWaitFor([&] { return workspace.inspectorStatus().contains("A newer resource read is already cached."); }, 5000)
                || !workspace.canEditYaml() || workspace.yamlText() != cachedYaml || !secretVisible()
                || !workspace.revealValue("data/password", true) || patches != 0) return failed("late A preserves accepted B freshness");
            rejectBDetail = true;
            if (!workspace.refreshInspector() || workspace.canEditYaml()
                || !QTest::qWaitFor([&] { return workspace.inspectorStatus().contains("HTTP 503"); }, 5000)
                || workspace.canEditYaml() || workspace.beginYamlEdit() || workspace.yamlText() != cachedYaml || patches != 0)
                return failed("failed current refresh remains read-only");
            rejectBDetail = false;
            if (!workspace.refreshInspector() || !QTest::qWaitFor([&] { return workspace.canEditYaml(); }, 5000))
                return failed("successful current refresh restores freshness");
        }
        if ((!draft && !workspace.beginYamlEdit()) || !workspace.setYamlDraft(draftText)
            || !workspace.previewYamlApply() || !QTest::qWaitFor([&] { return workspace.yamlApplyPreview(); }, 5000)
            || !workspace.confirmYamlApply(true)) return false;
        return QTest::qWaitFor([&] { return patches == 1 && !workspace.yamlEditing(); }, 5000)
            && patchAuthorization == authorizationB && workspace.table()->rowCount() == 1;
    }
    if (!workspace.requestSourceRemoval(contextId) || !workspace.confirmSourceRemoval()
        || !QTest::qWaitFor([&] { return !workspace.busy() && workspace.contexts().isEmpty(); }, 5000)) return false;
    QTest::qWait(200);
    const auto previousRequests = requests;
    if (!workspace.importFile(sourcePath) || !QTest::qWaitFor([&] { return !workspace.busy() && workspace.contexts().size() == 1; }, 5000)
        || !workspace.openContext(contextId) || !QTest::qWaitFor([&] { return !workspace.busy(); }, 5000)) return false;
    QTest::qWait(200);
    if (requests != previousRequests || !workspace.importOciCredentials(contextId, configPath, keyPath)
        || !QTest::qWaitFor([&] { return !workspace.busy() && workspace.authenticationRequired(); }, 5000)) return false;
    QTest::qWait(200);
    if (requests != previousRequests || !workspace.refresh(true)
        || !QTest::qWaitFor([&] { return requests > previousRequests && !workspace.authenticationRunning(); }, 5000)) return false;
    const auto beforeReplacement = requests;
    if (!workspace.importOciCredentials(contextId, configPath, keyPath)
        || !QTest::qWaitFor([&] { return !workspace.busy() && workspace.authenticationRequired(); }, 5000)) return false;
    QTest::qWait(200);
    return requests == beforeReplacement;
}
} // namespace

int main(int argc, char** argv) {
    QGuiApplication app(argc, argv);
    if (argc != 2 || !QStringList{"success", "bad_key", "cache_removal", "removal_failure", "identity", "closed_credentials",
        "freshness_reauth", "freshness_draft", "freshness_preview", "freshness_late", "freshness_recovery", "freshness_after_b"}.contains(QString::fromUtf8(argv[1]))) return 2;
    return run(QString::fromUtf8(argv[1])) ? 0 : 1;
}
