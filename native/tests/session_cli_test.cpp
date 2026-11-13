#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QProcess>
#include <QSet>
#include <QSysInfo>
#include <QTemporaryDir>
#include <QUuid>
#include <cstdio>
#include <algorithm>
#include <functional>
#include <memory>
#include <map>
#include <stdexcept>
#include <vector>

namespace {
struct Output final { int status; QJsonObject json; };
struct TestProfile final { const QString executable; const QString path; const QString sessionExecutable{}; };
const QString context = "ctx:" + QString(64, 'a');

bool check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
    return true;
}
Output run(const TestProfile& profile, QStringList args, int expected = 0, bool attachProfile = true) {
    QProcess process;
    process.start(profile.executable, (attachProfile ? QStringList{"--profile", profile.path} : QStringList{}) + args);
    check(process.waitForFinished(30000), "CLI did not finish");
    check(process.exitStatus() == QProcess::NormalExit, "CLI crashed");
    const int status = process.exitCode();
    const QByteArray bytes = status == 0 ? process.readAllStandardOutput() : process.readAllStandardError();
    if (status != expected) {
        std::fprintf(stderr, "%s\n", bytes.constData());
        check(false, "Unexpected CLI status");
    }
    const QJsonDocument document = QJsonDocument::fromJson(bytes);
    check(document.isObject(), "CLI did not emit a JSON object");
    return {status, document.object()};
}
QJsonArray rows(const Output& output) { return output.json["sessions"].toArray(); }
QString id(const Output& output, qsizetype index = 0) {
    return rows(output)[index].toObject()["id"].toString();
}
Output create(const TestProfile& profile) { return run(profile, {"create", "--context", context}); }
QString code(const Output& output) { return output.json["error"].toObject()["code"].toString(); }
QByteArray readStore(const TestProfile& profile) {
    QFile file(QDir(profile.path).filePath("tabs.json"));
    check(file.open(QIODevice::ReadOnly), "Cannot read test store");
    return file.readAll();
}
bool writeStore(const TestProfile& profile, const QByteArray& bytes) {
    QFile file(QDir(profile.path).filePath("tabs.json"));
    check(file.open(QIODevice::WriteOnly | QIODevice::Truncate), "Cannot alter ingress data");
    return check(file.write(bytes) == bytes.size(), "Incomplete ingress write");
}
bool closedOutput(const TestProfile& p) {
    QProcess process;
    process.start("/bin/sh", {"-c", "exec \"$1\" --profile \"$2\" list >&-", "sh", p.executable, p.path});
    return check(process.waitForFinished(30000) && process.exitStatus() == QProcess::NormalExit
        && process.exitCode() == 1, "Closed output stream reported successful delivery");
}
bool directoryReadFailure(const TestProfile& p, const QString& path, QFileDevice::Permissions permissions) {
    check(QFile::setPermissions(path, permissions), "Cannot restrict directory access");
    Output result;
    const auto restore = [&] { return QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner); };
    try { result = run(p, {"list"}, 1); }
    catch (...) { restore(); throw; }
    check(restore(), "Cannot restore directory access");
    return check(code(result) == "ReadFailed", "Inaccessible directory reported as empty");
}

const QString kubeconfig = R"(apiVersion: v1
kind: Config
current-context: local
clusters:
- name: local-cluster
  cluster:
    server: https://127.0.0.1:6443
users:
- name: operator
  user:
    token: keep-this-token-private
contexts:
- name: local
  context:
    cluster: local-cluster
    user: operator
    namespace: default
)";
QString writeSource(const TestProfile& p, QString yaml = kubeconfig, QString name = "source.yaml") {
    const QString path = QDir(p.path).filePath(name);
    QFile file(path);
    check(file.open(QIODevice::WriteOnly | QIODevice::Truncate), "Cannot write external kubeconfig");
    const auto bytes = yaml.toUtf8(); check(file.write(bytes) == bytes.size(), "Kubeconfig write incomplete");
    return path;
}
Output importSource(const TestProfile& p) { return run(p, {"import", writeSource(p)}); }
QString sourceIssue(const TestProfile& p) {
    const auto listed = run(p, {"list"}).json;
    check(listed["sources"].toArray().isEmpty() && listed["errors"].toArray().size() == 1, "Expected one explicit owned-file failure");
    return listed["errors"].toArray()[0].toObject()["code"].toString();
}
bool alterOwned(QString path, const std::function<QJsonObject(QJsonObject)>& change) {
    QFile file(path); check(file.open(QIODevice::ReadOnly), "Cannot read owned source");
    const auto root = QJsonDocument::fromJson(file.readAll()).object(); file.close();
    const auto bytes = QJsonDocument(change(root)).toJson();
    check(file.open(QIODevice::WriteOnly | QIODevice::Truncate), "Cannot change owned ingress");
    return check(file.write(bytes) == bytes.size(), "Incomplete owned ingress write");
}
const std::map<QString, std::function<bool(const TestProfile&)>> sourceCases{
    {"source.profile_no_traverse", [](const TestProfile& p) {
        importSource(p); return directoryReadFailure(p, p.path, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    }},
    {"source.owned_no_traverse", [](const TestProfile& p) {
        importSource(p); return directoryReadFailure(p, QDir(p.path).filePath("kubeconfigs"), QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    }},
    {"source.output_write_failure_large", [](const TestProfile& p) {
        const auto yaml = QString(kubeconfig).replace("- name: local\n", "- name: " + QString(30000, 'x') + "\n");
        run(p, {"import", writeSource(p, yaml)}); return closedOutput(p);
    }},
    {"source.help", [](const TestProfile& p) {
        QProcess process; process.start(p.executable, {"--help"});
        return check(process.waitForFinished(30000) && process.exitCode() == 0
            && process.readAllStandardOutput().contains("No authentication"), "Source help unavailable without profile");
    }},
    {"source.isolated_corruption", [](const TestProfile& p) {
        const auto damaged = importSource(p).json;
        const auto healthy = run(p, {"import", writeSource(p, kubeconfig, "healthy.yaml")}).json;
        alterOwned(damaged["ownedPath"].toString(), [](QJsonObject root) { root["contentHash"] = "broken"; return root; });
        const auto listed = run(p, {"list"}).json;
        return check(listed["sources"].toArray().size() == 1
            && listed["sources"].toArray()[0].toObject()["id"] == healthy["id"]
            && listed["errors"].toArray().size() == 1, "Damaged snapshot suppressed healthy sources or its own error");
    }},
    {"source.session_binding", [](const TestProfile& p) {
        check(!p.sessionExecutable.isEmpty(), "Session CLI path was not supplied");
        const auto first = importSource(p).json;
        const auto context = first["contexts"].toArray()[0].toObject();
        const TestProfile sessions{p.sessionExecutable, p.path};
        const auto created = run(sessions, {"create", "--context", context["id"].toString()});
        const QString selected = id(created); run(sessions, {"open", selected});
        const auto before = readStore(sessions);
        const auto changed = run(p, {"import", writeSource(p, QString(kubeconfig).replace("6443", "7443"))}).json;
        check(changed["contexts"].toArray()[0].toObject()["id"] != context["id"], "Changed endpoint reused old context identity");
        const auto catalog = run(sessions, {"list"});
        return check(catalog.json["activeSessionId"] == selected && rows(catalog).size() == 1
            && rows(catalog)[0].toObject()["contextId"] == context["id"] && readStore(sessions) == before,
            "Import changed active session binding or history");
    }},
    {"source.missing_file", [](const TestProfile& p) {
        return check(code(run(p, {"import", QDir(p.path).filePath("missing.yaml")}, 1)) == "ReadFailed", "Missing file accepted");
    }},
    {"source.empty_path", [](const TestProfile& p) {
        return check(code(run(p, {"import", ""}, 2)) == "InvalidInput", "Empty source path accepted");
    }},
    {"source.directory_input", [](const TestProfile& p) {
        return check(code(run(p, {"import", p.path}, 1)) == "ReadFailed", "Directory treated as YAML file");
    }},
    {"source.invalid_utf8", [](const TestProfile& p) {
        QFile file(QDir(p.path).filePath("source.yaml"));
        check(file.open(QIODevice::WriteOnly) && file.write("\xFF") == 1, "Cannot write external encoding"); file.close();
        return check(code(run(p, {"import", file.fileName()}, 1)) == "InvalidData", "Invalid UTF-8 accepted");
    }},
    {"source.owned_filename", [](const TestProfile& p) {
        const auto source = importSource(p).json;
        check(QFile::rename(source["ownedPath"].toString(), QDir(p.path).filePath("kubeconfigs/wrong.json")), "Cannot alter snapshot identity");
        return check(sourceIssue(p) == "ReadFailed", "Missing catalogued source accepted");
    }},
    {"source.owned_root", [](const TestProfile& p) {
        const auto source = importSource(p).json; QFile file(source["ownedPath"].toString());
        check(file.open(QIODevice::WriteOnly | QIODevice::Truncate) && file.write("[]") == 2, "Cannot alter owned document root"); file.close();
        return check(sourceIssue(p) == "InvalidData", "Invalid owned root accepted");
    }},
    {"source.directory_is_file", [](const TestProfile& p) {
        QFile file(QDir(p.path).filePath("kubeconfigs")); check(file.open(QIODevice::WriteOnly), "Cannot block owned directory");
        return check(code(run(p, {"list"}, 1)) == "InvalidData", "Owned file treated as directory");
    }},
    {"source.import_relative_profile", [](const TestProfile& p) {
        return check(code(run({p.executable, "relative-profile"}, {"import", writeSource(p)}, 2)) == "InvalidInput", "Relative import profile accepted");
    }},
    {"source.directory_read_denied", [](const TestProfile& p) {
        importSource(p); return directoryReadFailure(p, QDir(p.path).filePath("kubeconfigs"), {});
    }},
    {"source.bom", [](const TestProfile& p) {
        const QString path = writeSource(p);
        QFile file(path); check(file.open(QIODevice::WriteOnly | QIODevice::Truncate), "Cannot write BOM source");
        const auto bytes = QByteArray("\xEF\xBB\xBF") + kubeconfig.toUtf8();
        check(file.write(bytes) == bytes.size(), "BOM source write incomplete"); file.close();
        const auto source = run(p, {"import", path}).json;
        return check(source["contexts"].toArray().size() == 1
            && run(p, {"list"}).json["sources"].toArray()[0].toObject()["contentHash"] == source["contentHash"], "Valid UTF-8 BOM rejected or changed");
    }},
    {"source.empty", [](const TestProfile& p) {
        return check(run(p, {"list"}).json["sources"].toArray().isEmpty()
            && !QFileInfo(QDir(p.path).filePath("kubeconfigs")).exists(), "Empty listing mutated profile");
    }},
    {"source.success", [](const TestProfile& p) {
        const auto source = importSource(p).json;
        const auto context = source["contexts"].toArray()[0].toObject();
        check(context["authType"] == "token" && context["namespace"] == "default", "Context metadata incorrect");
        check(!QJsonDocument(source).toJson().contains("keep-this-token-private"), "Credential leaked to metadata");
        return check(context["brokenReferences"].toArray().isEmpty() && QFileInfo(source["ownedPath"].toString()).isFile(), "Valid import failed");
    }},
    {"source.deduplicate", [](const TestProfile& p) {
        const auto first = importSource(p).json; const auto second = importSource(p).json;
        return check(first["id"] == second["id"] && run(p, {"list"}).json["sources"].toArray().size() == 1, "Identical source duplicated");
    }},
    {"source.changed", [](const TestProfile& p) {
        const auto first = importSource(p).json;
        QFile owned(first["ownedPath"].toString()); check(owned.open(QIODevice::ReadOnly), "Cannot read preserved snapshot");
        const auto before = owned.readAll(); owned.close();
        const auto second = run(p, {"import", writeSource(p, QString(kubeconfig).replace("6443", "7443"))}).json;
        check(first["id"] != second["id"] && run(p, {"list"}).json["sources"].toArray().size() == 2, "Changed source overwritten");
        check(owned.open(QIODevice::ReadOnly), "Cannot read old snapshot");
        return check(owned.readAll() == before, "Old snapshot content changed");
    }},
    {"source.different_path", [](const TestProfile& p) {
        const auto first = importSource(p).json;
        return check(run(p, {"import", writeSource(p, kubeconfig, "other.yaml")}).json["id"] != first["id"], "Source paths conflated");
    }},
    {"source.normalized_path", [](const TestProfile& p) {
        const auto first = importSource(p).json;
        const auto other = run(p, {"import", p.path + "/unused/../source.yaml"}).json;
        return check(first["id"] == other["id"], "Equivalent source paths duplicated");
    }},
    {"source.deleted_original", [](const TestProfile& p) {
        importSource(p); check(QFile::remove(QDir(p.path).filePath("source.yaml")), "Cannot remove original");
        return check(run(p, {"list"}).json["sources"].toArray().size() == 1, "Owned snapshot depends on original file");
    }},
    {"source.no_exec", [](const TestProfile& p) {
        const QString marker = QDir(p.path).filePath("plugin-ran");
        const QString yaml = QString(kubeconfig).replace("    token: keep-this-token-private", "    exec:\n      command: /bin/sh\n      args: ['-c', 'touch " + marker + "']");
        const auto result = run(p, {"import", writeSource(p, yaml)}).json;
        return check(result["contexts"].toArray()[0].toObject()["authType"] == "exec" && !QFileInfo(marker).exists(), "Import executed credential plugin");
    }},
    {"source.current_warning", [](const TestProfile& p) {
        return check(!run(p, {"import", writeSource(p, QString(kubeconfig).replace("current-context: local", "current-context: absent"))}).json["warnings"].toArray().isEmpty(), "Missing current context not reported");
    }},
    {"source.recency", [](const TestProfile& p) {
        const auto first = importSource(p).json;
        alterOwned(first["ownedPath"].toString(), [](QJsonObject root) { root["importedAt"] = "2000-01-01T00:00:00.000Z"; return root; });
        const auto second = run(p, {"import", writeSource(p, kubeconfig, "other.yaml")}).json;
        check(run(p, {"list"}).json["sources"].toArray()[0].toObject()["id"] == second["id"], "Newest import not first");
        alterOwned(second["ownedPath"].toString(), [](QJsonObject root) { root["importedAt"] = "2001-01-01T00:00:00.000Z"; return root; });
        importSource(p);
        return check(run(p, {"list"}).json["sources"].toArray()[0].toObject()["id"] == first["id"], "Reimport did not refresh recency");
    }},
    {"source.recency_tie", [](const TestProfile& p) {
        const auto first = importSource(p).json;
        const auto second = run(p, {"import", writeSource(p, kubeconfig, "other.yaml")}).json;
        for (const auto& source : {first, second}) alterOwned(source["ownedPath"].toString(), [](QJsonObject root) { root["importedAt"] = "2000-01-01T00:00:00.000Z"; return root; });
        return check(run(p, {"list"}).json["sources"].toArray()[0].toObject()["id"].toString()
            == std::min(first["id"].toString(), second["id"].toString()), "Recency tie unstable");
    }},
    {"source.busy", [](const TestProfile& p) {
        const auto first = importSource(p).json;
        QFile file(first["ownedPath"].toString()); check(file.open(QIODevice::ReadOnly), "Cannot read locked source");
        const auto before = file.readAll(); file.close();
        QLockFile lock(QDir(p.path).filePath("kubeconfigs/sources.lock")); check(lock.tryLock(0), "Cannot hold real import lock");
        check(code(run(p, {"import", QDir(p.path).filePath("source.yaml")}, 1)) == "Busy", "Locked import applied");
        check(file.open(QIODevice::ReadOnly), "Cannot read retained source"); return check(file.readAll() == before, "Busy import changed source");
    }},
    {"source.failed_directory", [](const TestProfile& p) {
        const QString source = writeSource(p); QFile file(QDir(p.path).filePath("blocker"));
        check(file.open(QIODevice::WriteOnly), "Cannot create directory blocker");
        return check(code(run({p.executable, file.fileName() + "/child"}, {"import", source}, 1)) == "WriteFailed", "Failed profile creation ignored");
    }},
    {"source.private", [](const TestProfile& p) {
#ifdef Q_OS_UNIX
        const auto source = run({p.executable, p.path + "/fresh"}, {"import", writeSource(p)}).json;
        for (const auto& path : {source["ownedPath"].toString(), p.path + "/fresh", p.path + "/fresh/kubeconfigs"})
            check(!(QFileInfo(path).permissions() & (QFileDevice::ReadGroup | QFileDevice::WriteGroup | QFileDevice::ReadOther | QFileDevice::WriteOther)), "Owned config is not private");
        return true;
#else
        return check(false, "Unix-only scenario registered");
#endif
    }},
    {"source.owned_symlink", [](const TestProfile& p) {
        const auto source = importSource(p).json; const QString path = source["ownedPath"].toString();
        check(QFile::remove(path) && QFile::link(QDir(p.path).filePath("source.yaml"), path), "Cannot create owned alias");
        return check(sourceIssue(p) == "InvalidData"
            && code(run(p, {"import", QDir(p.path).filePath("source.yaml")}, 1)) == "InvalidData", "Owned alias accepted");
    }},
    {"source.directory_symlink", [](const TestProfile& p) {
        check(QFile::link(p.path, QDir(p.path).filePath("kubeconfigs")), "Cannot create directory alias");
        return check(code(run(p, {"list"}, 1)) == "InvalidData", "Owned directory alias accepted");
    }},
    {"source.read_denied", [](const TestProfile& p) {
        const auto source = importSource(p).json; const QString path = source["ownedPath"].toString();
        check(QFile::setPermissions(path, {}), "Cannot deny source read");
        const auto result = sourceIssue(p);
        check(QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner), "Cannot restore source read");
        return check(result == "ReadFailed", "Unreadable source ignored");
    }},
    {"source.corrupted", [](const TestProfile& p) {
        const auto source = importSource(p).json; QFile file(source["ownedPath"].toString());
        check(file.open(QIODevice::WriteOnly | QIODevice::Truncate) && file.write("not-json") == 8, "Cannot corrupt external snapshot"); file.close();
        check(sourceIssue(p) == "InvalidData", "Corrupt snapshot accepted");
        return check(code(run(p, {"import", QDir(p.path).filePath("source.yaml")}, 1)) == "InvalidData", "Corrupt snapshot overwritten");
    }}
};
const std::map<QString, QString> invalidSources{
    {"source.empty_yaml", ""}, {"source.invalid_yaml", "secret: [unterminated"},
    {"source.root_scalar", "hello"}, {"source.multiple_documents", "---\n{}\n---\n{}"},
    {"source.no_contexts", "apiVersion: v1\nkind: Config"},
    {"source.bad_version", QString(kubeconfig).replace("apiVersion: v1", "apiVersion: v999")},
    {"source.bad_kind", QString(kubeconfig).replace("kind: Config", "kind: Pod")},
    {"source.bad_collection", "contexts: {}"},
    {"source.bad_entry", "contexts: [true]"},
    {"source.bad_name", "contexts: [{name: '', context: {}}]"},
    {"source.bad_payload", "contexts: [{name: local, context: 'bad'}]"},
    {"source.bad_field", "contexts: [{name: local, context: {cluster: {}}}]"},
    {"source.bad_exec", QString(kubeconfig).replace("token: keep-this-token-private", "exec: string")},
    {"source.bad_auth_provider", QString(kubeconfig).replace("token: keep-this-token-private", "auth-provider: string")},
    {"source.bad_token", QString(kubeconfig).replace("token: keep-this-token-private", "token: {}")}
};
struct RowChange final { QString key; QJsonValue value; };
const std::map<QString, RowChange> invalidOwnedSources{
    {"source.owned_version", {"version", 999}},
    {"source.owned_version_type", {"version", "1"}},
    {"source.owned_extra", {"extra", 1}},
    {"source.owned_path_type", {"sourcePath", 1}},
    {"source.owned_path_relative", {"sourcePath", "relative"}},
    {"source.owned_path_unclean", {"sourcePath", "/some/../source.yaml"}},
    {"source.owned_hash", {"contentHash", "wrong"}},
    {"source.owned_date", {"importedAt", "invalid"}},
    {"source.owned_yaml", {"yamlBase64", QString::fromLatin1(QByteArray("contexts: []").toBase64())}},
    {"source.owned_encoding", {"yamlBase64", "invalid!!"}}
};
const std::map<QString, QString> sourceAuth{
    {"source.auth_provider", "auth-provider:\n      name: oidc"},
    {"source.auth_certificate", "client-certificate: client.crt"},
    {"source.auth_embedded_certificate", "client-certificate-data: Y2VydA=="},
    {"source.auth_basic", "username: local-user"},
    {"source.auth_tokenfile", "tokenFile: token.txt"},
    {"source.auth_unknown", "custom: preserved"}
};
const std::map<QString, QString> sourceAuthExpected{
    {"source.auth_provider", "auth-provider:oidc"}, {"source.auth_certificate", "client-certificate"},
    {"source.auth_embedded_certificate", "client-certificate"}, {"source.auth_basic", "basic"},
    {"source.auth_tokenfile", "token"}, {"source.auth_unknown", "unknown"}
};
const std::map<QString, QString> sourceReferenceInputs{
    {"source.missing_cluster", QString(kubeconfig).replace("    cluster: local-cluster", "    cluster: absent")},
    {"source.missing_user", QString(kubeconfig).replace("    user: operator", "    user: absent")},
    {"source.anonymous", QString(kubeconfig).replace("    user: operator\n", "")},
    {"source.minimal", "contexts: [{name: local, context: {}}]"},
    {"source.null_lists", "clusters: null\nusers: null\ncontexts: [{name: local, context: {cluster: absent}}]"},
    {"source.bad_server", QString(kubeconfig).replace("https://127.0.0.1:6443", "ftp://127.0.0.1:6443")},
    {"source.empty_server", QString(kubeconfig).replace("https://127.0.0.1:6443", "''")},
    {"source.missing_server", QString(kubeconfig).replace("server: https://127.0.0.1:6443", "custom: preserved")},
    {"source.server_credentials", QString(kubeconfig).replace("https://127.0.0.1:6443", "https://local:secret-password@127.0.0.1:6443")},
    {"source.server_query", QString(kubeconfig).replace("https://127.0.0.1:6443", "https://127.0.0.1:6443?access_token=secret-query")},
    {"source.server_fragment", QString(kubeconfig).replace("https://127.0.0.1:6443", "https://127.0.0.1:6443#secret-fragment")},
    {"source.http", QString(kubeconfig).replace("https://", "http://")}
};
const std::map<QString, QStringList> sourceReferenceExpected{
    {"source.missing_cluster", {"cluster"}}, {"source.missing_user", {"user"}}, {"source.anonymous", {}},
    {"source.minimal", {"cluster"}}, {"source.null_lists", {"cluster"}},
    {"source.bad_server", {"server"}}, {"source.empty_server", {"server"}}, {"source.missing_server", {"server"}},
    {"source.server_credentials", {"server"}}, {"source.server_query", {"server"}}, {"source.server_fragment", {"server"}}, {"source.http", {}}
};
const std::map<QString, QString> sourceDuplicateInputs{
    {"source.duplicate_context", kubeconfig + "- name: local\n  context: {cluster: local-cluster, user: operator}\n"},
    {"source.duplicate_cluster", QString(kubeconfig).replace("users:", "- name: local-cluster\n  cluster: {server: https://127.0.0.1:7443}\nusers:")},
    {"source.duplicate_user", QString(kubeconfig).replace("contexts:", "- name: operator\n  user: {token: different-private-token}\ncontexts:")}
};

int benchmark(const TestProfile& profile, bool sources = false) {
    constexpr int sessions = 16;
    constexpr int samples = 50;
    for (int n = 0; n < sessions; ++n) {
        if (sources) run(profile, {"import", writeSource(profile, kubeconfig, QString("source-%1.yaml").arg(n))});
        else create(profile);
    }
    run(profile, {"list"}); // Warm the executable and filesystem cache before timed samples.
    QList<double> timings;
    for (int n = 0; n < samples; ++n) {
        QElapsedTimer timer; timer.start();
        const auto output = run(profile, {"list"});
        const double elapsed = static_cast<double>(timer.nsecsElapsed()) / 1000000.0;
        check(output.json[sources ? "sources" : "sessions"].toArray().size() == sessions, "Benchmark lost records");
        timings.append(elapsed);
    }
    std::sort(timings.begin(), timings.end());
    const QJsonObject report{{"boundary", "CLI list process wall time including startup and JSON validation"},
        {"catalog", sources ? "owned kubeconfig snapshots" : "open context tabs"},
        {"records", sessions}, {"samples", samples}, {"p50_ms", timings[24]},
        {"p95_ms", timings[47]}, {"max_ms", timings.back()},
        {"qt_version", QString(qVersion())}, {"platform", QSysInfo::prettyProductName()}};
    const QByteArray bytes = QJsonDocument(report).toJson();
    return std::fwrite(bytes.constData(), 1, static_cast<size_t>(bytes.size()), stdout)
        == static_cast<size_t>(bytes.size()) ? 0 : 1;
}
} // namespace
int main(int argc, char* argv[]) {
    const QCoreApplication app(argc, argv);
    if (argc != 3 && argc != 4) return 2;
    const QString executable = QString::fromLocal8Bit(argv[1]);
    QTemporaryDir directory;
    if (!directory.isValid()) return 1;
    const QString name = QString::fromLocal8Bit(argv[2]);
    const TestProfile profile{executable, directory.path(), argc == 4 ? QString::fromLocal8Bit(argv[3]) : QString{}};
    try {
        if (name == "benchmark") return benchmark(profile);
        if (name == "source.benchmark") return benchmark(profile, true);
        if (const auto scenario = sourceCases.find(name); scenario != sourceCases.end()) return scenario->second(profile) ? 0 : 1;
        if (const auto input = sourceReferenceInputs.find(name); input != sourceReferenceInputs.end()) {
            const auto source = run(profile, {"import", writeSource(profile, input->second)}).json;
            check(!QJsonDocument(source).toJson().contains("secret-"), "Server credentials leaked");
            return check(source["contexts"].toArray()[0].toObject()["brokenReferences"].toArray()
                == QJsonArray::fromStringList(sourceReferenceExpected.at(name)), "Broken-reference metadata differs") ? 0 : 1;
        }
        if (const auto input = sourceDuplicateInputs.find(name); input != sourceDuplicateInputs.end()) {
            const auto source = run(profile, {"import", writeSource(profile, input->second)}).json;
            return check(!source["warnings"].toArray().isEmpty() && source["contexts"].toArray().size() == 1
                && source["contexts"].toArray()[0].toObject()["brokenReferences"].toArray()
                    .contains("duplicate-" + name.section('_', -1)), "Ambiguous duplicate reported as usable context") ? 0 : 1;
        }
        if (const auto input = invalidSources.find(name); input != invalidSources.end()) {
            const auto result = run(profile, {"import", writeSource(profile, input->second)}, 1);
            check(!QJsonDocument(result.json).toJson().contains("unterminated"), "Parser leaked source payload");
            return check(code(result) == "InvalidData" && !QFileInfo(QDir(profile.path).filePath("kubeconfigs")).exists(), "Invalid source persisted") ? 0 : 1;
        }
        if (const auto input = invalidOwnedSources.find(name); input != invalidOwnedSources.end()) {
            const auto source = importSource(profile).json;
            alterOwned(source["ownedPath"].toString(), [&](QJsonObject root) { root[input->second.key] = input->second.value; return root; });
            return check(sourceIssue(profile) == (name == "source.owned_version" ? "UnsupportedVersion" : "InvalidData"), "Invalid owned snapshot accepted") ? 0 : 1;
        }
        if (const auto auth = sourceAuth.find(name); auth != sourceAuth.end()) {
            const auto source = run(profile, {"import", writeSource(profile, QString(kubeconfig).replace("token: keep-this-token-private", auth->second))}).json;
            return check(source["contexts"].toArray()[0].toObject()["authType"] == sourceAuthExpected.at(name), "Authentication label incorrect") ? 0 : 1;
        }
        const std::map<QString, QStringList> invalid{{"empty_profile", {"--profile", "", "list"}}, {"relative_profile", {"--profile", "relative", "list"}}, {"missing_command", {}}, {"unknown_command", {"unknown"}}, {"extra_arguments", {"list", "extra"}}, {"repeat_profile", {"--profile", profile.path, "list"}}, {"missing_profile", {"list"}}, {"parse_failure", {"--unknown"}}};
        if (name == "output_write_failure") return closedOutput(profile) ? 0 : 1;
        if (const auto entry = invalid.find(name); entry != invalid.end()) {
            const bool attach = name != "empty_profile" && name != "relative_profile" && name != "missing_profile";
            const auto result = run(profile, entry->second, 2, attach);
            return code(result) == "InvalidInput" ? 0 : 1;
        }
        return 2;
    }
    catch (const std::exception& error) { std::fprintf(stderr, "%s\n", error.what()); return 1; }
}
