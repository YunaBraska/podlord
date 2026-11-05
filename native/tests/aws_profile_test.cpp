#include "native_eks.h"
#include "credential_process.h"
#include <QGuiApplication>
#include <QFile>
#include <QDir>
#include <QJsonDocument>
#include <QJsonArray>
#include <QLockFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QUrlQuery>
#include <cstdio>
#include <stdexcept>

using namespace podlord;
namespace {
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
void write(const QString& path, const QByteArray& bytes) {
    QFile file(path);
    require(file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size(), "Cannot write selected test file.");
}
QByteArray read(const QString& path) { QFile file(path); return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{}; }
ExecCommand command() {
    ExecCommand value;
    value.program = "aws";
    value.arguments = {"eks", "get-token", "--cluster-name", "example", "--profile", "prod"};
    return value;
}
const QByteArray keys = "[prod]\naws_access_key_id=IMPORTEDKEY\naws_secret_access_key=private-key-value\naws_session_token=private-session\n";
const QByteArray config = "[profile prod]\nregion=eu-central-1\noutput=json\n[profile unrelated]\nregion=us-west-2\n";
const QByteArray ssoConfig = "[profile prod]\nregion=eu-central-1\nsso_session=company\nsso_account_id=123456789012\nsso_role_name=Developer\n"
    "[sso-session company]\nsso_start_url=https://company.awsapps.com/start\nsso_region=eu-west-1\nsso_registration_scopes=sso:account:access\n";
void run(const QString& scenario) {
    auto exec = command();
    QByteArray cfg = config, creds = keys;
    if (scenario == "resolve_default_env_without_import") {
        exec.arguments = {"eks", "get-token", "--cluster-name", "example", "--region", "eu-central-1"};
        exec.environment = {{"AWS_DEFAULT_PROFILE", "personal"}, {"AWS_ACCESS_KEY_ID", "OTHERKEY"}, {"AWS_SECRET_ACCESS_KEY", "OTHERSECRET"}};
        const auto result = nativeEksConfig(exec);
        require(std::holds_alternative<Failure>(result) && std::get<Failure>(result).message.contains("import"),
            "AWS_DEFAULT_PROFILE silently fell back to a different identity without an explicit import.");
        return;
    }
    if (scenario.startsWith("name_")) {
        QString expected = "prod";
        bool invalid = false;
        if (scenario == "name_default") { exec.arguments = {}; expected = "default"; }
        if (scenario == "name_env") { exec.arguments = {}; exec.environment.insert("AWS_PROFILE", "personal"); expected = "personal"; }
        if (scenario == "name_precedence") exec.environment.insert("AWS_PROFILE", "ignored");
        if (scenario == "name_default_env") { exec.arguments = {}; exec.environment.insert("AWS_DEFAULT_PROFILE", "personal"); expected = "personal"; }
        if (scenario == "name_default_env_precedence") { exec.arguments = {}; exec.environment = {{"AWS_DEFAULT_PROFILE", "personal"}, {"AWS_PROFILE", "ignored"}}; expected = "personal"; }
        if (scenario == "name_default_env_argument") exec.environment = {{"AWS_DEFAULT_PROFILE", "ignored"}, {"AWS_PROFILE", "also-ignored"}};
        if (scenario == "name_default_env_blank") { exec.arguments = {}; exec.environment.insert("AWS_DEFAULT_PROFILE", ""); invalid = true; }
        if (scenario == "name_equals") exec.arguments = {"--profile=prod"};
        if (scenario == "name_duplicate") { exec.arguments.append("--profile=other"); invalid = true; }
        if (scenario == "name_missing") { exec.arguments = {"--profile"}; invalid = true; }
        if (scenario == "name_option") { exec.arguments = {"--profile", "--region"}; invalid = true; }
        if (scenario == "name_blank") { exec.environment.insert("AWS_PROFILE", ""); exec.arguments = {}; invalid = true; }
        if (scenario == "name_invalid") { exec.arguments = {"--profile=bad[profile"}; invalid = true; }
        if (scenario == "name_control") { exec.arguments = {"--profile=bad\nprofile"}; invalid = true; }
        if (scenario == "name_long") { exec.arguments = {"--profile=" + QString(129, 'a')}; invalid = true; }
        if (scenario == "name_trim") { exec.arguments = {"--profile= prod"}; invalid = true; }
        const auto result = awsProfileName(exec);
        require(invalid ? std::holds_alternative<Failure>(result) : std::holds_alternative<QString>(result) && std::get<QString>(result) == expected, "Wrong AWS profile selection.");
        return;
    }
    if (scenario.startsWith("parse_")) {
        bool invalid = false;
        QString name = "prod";
        if (scenario == "parse_credentials_only") cfg = {};
        if (scenario == "parse_config_only") { cfg += "aws_access_key_id=UNRELATED\n"; cfg = "[profile prod]\nregion=eu-central-1\naws_access_key_id=CONFIGKEY\naws_secret_access_key=CONFIGSECRET\n"; creds = {}; }
        if (scenario == "parse_default") { cfg = "[default]\nregion=eu-central-1\n"; creds = "[default]\naws_access_key_id=DEFAULT\naws_secret_access_key=SECRET\n"; name = "default"; }
        if (scenario == "parse_bom") cfg.prepend("\xEF\xBB\xBF");
        if (scenario == "parse_comments") cfg.prepend("# comment\n; comment\n\n");
        if (scenario == "parse_precedence") cfg = "[profile prod]\nregion=eu-central-1\naws_access_key_id=CONFIGKEY\naws_secret_access_key=CONFIGSECRET\n";
        if (scenario == "parse_sso" || scenario == "parse_legacy_sso") { cfg = ssoConfig; creds = {}; }
        if (scenario == "parse_legacy_sso") cfg = "[profile prod]\nregion=eu-central-1\nsso_start_url=https://company.awsapps.com/start\nsso_region=eu-west-1\nsso_account_id=123456789012\nsso_role_name=Developer\n";
        if (scenario == "parse_empty") { cfg = {}; creds = {}; invalid = true; }
        if (scenario == "parse_large_config") { cfg = QByteArray(1024 * 1024 + 1, 'a'); invalid = true; }
        if (scenario == "parse_large_credentials") { creds = QByteArray(1024 * 1024 + 1, 'a'); invalid = true; }
        if (scenario == "parse_utf8") { cfg = QByteArray("\xff"); invalid = true; }
        if (scenario == "parse_null") { cfg.append('\0'); invalid = true; }
        if (scenario == "parse_section") { cfg = "[broken\n"; invalid = true; }
        if (scenario == "parse_empty_section") { cfg = "[]\n"; invalid = true; }
        if (scenario == "parse_duplicate_section") { cfg += "[profile prod]\n"; invalid = true; }
        if (scenario == "parse_no_section") { cfg = "region=eu-central-1\n"; invalid = true; }
        if (scenario == "parse_no_equal") { cfg += "broken\n"; invalid = true; }
        if (scenario == "parse_duplicate_field") { creds += "aws_access_key_id=OTHER\n"; invalid = true; }
        if (scenario == "parse_missing") { name = "missing"; invalid = true; }
        if (scenario == "parse_no_inherit") { cfg = "[default]\nregion=eu-west-1\n[profile prod]\noutput=json\n"; creds = {}; }
        for (const auto* field : {"role_arn", "source_profile", "credential_source", "credential_process", "web_identity_token_file", "endpoint_url"})
            if (scenario == "parse_unsupported_" + QString(field)) { creds += QByteArray(field) + "=secret-must-not-leak\n"; invalid = true; }
        if (scenario == "parse_session_missing") { cfg = ssoConfig.left(ssoConfig.indexOf("[sso-session")); creds = {}; invalid = true; }
        if (scenario == "parse_session_conflict") { cfg = ssoConfig; cfg.insert(cfg.indexOf("sso_session"), "sso_region=us-east-1\n"); creds = {}; invalid = true; }
        if (scenario == "parse_scope") { cfg = ssoConfig; cfg.replace("sso:account:access", "extra:scope"); creds = {}; invalid = true; }
        if (scenario == "parse_bad_credentials") { creds = "bad"; invalid = true; }
        const auto result = readAwsProfile(cfg, creds, name);
        if (invalid) {
            require(std::holds_alternative<Failure>(result), "Invalid AWS profile was accepted.");
            require(!std::get<Failure>(result).message.contains("secret-must-not-leak"), "AWS parser exposed input contents.");
        } else {
            require(std::holds_alternative<QMap<QString, QString>>(result), "Valid AWS profile was rejected.");
            const auto& fields = std::get<QMap<QString, QString>>(result);
            require(!fields.contains("output") && !fields.contains("sso_session"), "Unneeded profile fields were retained.");
            if (scenario == "parse_precedence") require(fields.value("aws_access_key_id") == "IMPORTEDKEY", "Credentials file did not override config.");
            if (scenario == "parse_no_inherit") require(!fields.contains("region"), "Default leaked into a named profile.");
        }
        return;
    }
    if (scenario.startsWith("resolve_")) {
        const auto selected = readAwsProfile(ssoConfig, {}, "prod");
        auto fields = std::get<QMap<QString, QString>>(selected);
        const auto key = scenario.mid(8);
        bool invalid = true;
        if (key == "sso") invalid = false;
        else if (key == "windows") { exec.program = "C:\\Program Files\\Amazon\\AWSCLIV2\\aws.exe"; invalid = false; }
        else if (key == "windows_upper") { exec.program = "C:\\AWS\\AWS.EXE"; invalid = false; }
        else if (key == "exe") { exec.program = "aws.exe"; invalid = false; }
        else if (key == "region_flag") { exec.arguments.append({"--region", "us-east-1"}); invalid = false; }
        else if (key == "region_env") { exec.environment.insert("AWS_REGION", "us-east-1"); invalid = false; }
        else if (key == "region_default_env") { exec.environment.insert("AWS_DEFAULT_REGION", "us-east-1"); invalid = false; }
        else if (key == "mixed") fields.insert("aws_access_key_id", "UNEXPECTED");
        else if (key == "http") fields["sso_start_url"] = "http://company.awsapps.com/start";
        else if (key == "userinfo") fields["sso_start_url"] = "https://user:secret@company.awsapps.com/start";
        else if (key == "port") fields["sso_start_url"] = "https://company.awsapps.com:443/start";
        else if (key == "query") fields["sso_start_url"] += "?secret=hidden";
        else if (key == "fragment") fields["sso_start_url"] += "#fragment";
        else if (key == "empty_url") fields["sso_start_url"] = "";
        else if (key == "long_url") fields["sso_start_url"] = "https://company.awsapps.com/" + QString(4097, 'a');
        else if (key == "long_eks_region") fields["region"] = QString(65, 'a') + "-1";
        else if (key == "long_sso_region") fields["sso_region"] = QString(65, 'a') + "-1";
        else if (key == "bad_region") fields["sso_region"] = "not-region";
        else if (key == "bad_account") fields["sso_account_id"] = "123";
        else if (key == "bad_role") fields["sso_role_name"] = "bad role";
        else if (key == "missing_sso") fields.remove("sso_account_id");
        else if (key == "no_ambient_fallback") { fields.clear(); fields.insert("region", "eu-central-1"); qputenv("AWS_ACCESS_KEY_ID", "AMBIENT"); qputenv("AWS_SECRET_ACCESS_KEY", "SECRET"); }
        else if (key == "missing_profile") { require(std::holds_alternative<Failure>(nativeEksConfig(exec)), "Native resolver used an unimported profile."); return; }
        const auto result = nativeEksConfig(exec, fields);
        if (invalid) require(std::holds_alternative<Failure>(result), "Invalid native AWS profile configuration was accepted.");
        else {
            require(std::holds_alternative<std::optional<EksLogin>>(result) && std::get<std::optional<EksLogin>>(result).has_value(), "SSO configuration was not resolved.");
            const auto& login = *std::get<std::optional<EksLogin>>(result);
            require(login.sso.has_value() && login.accessKey.isEmpty(), "SSO import unexpectedly resolved credentials.");
            if (key.startsWith("region_")) require(login.region == "us-east-1", "EKS region precedence changed.");
        }
        return;
    }
    QTemporaryDir root;
    require(root.isValid(), "Cannot create isolated profile.");
    const auto profile = root.filePath("profile");
    KubeconfigStore store(profile);
    QString yaml =
        "apiVersion: v1\nkind: Config\nclusters:\n- name: shared\n  cluster:\n    server: https://127.0.0.1:9\nusers:\n- name: shared\n  user:\n    exec:\n      apiVersion: client.authentication.k8s.io/v1beta1\n      command: aws\n      args: [eks, get-token, --cluster-name, example, --profile, prod]\ncontexts:\n- name: one\n  context: {cluster: shared, user: shared}\n- name: two\n  context: {cluster: shared, user: shared}\n";
    if (scenario == "store_other_program") yaml.replace("command: aws", "command: other");
    if (scenario == "store_no_exec") yaml.replace("    exec:", "    ignored:");
    if (scenario == "store_no_args") yaml.replace("args: [eks, get-token, --cluster-name, example, --profile, prod]", "args: null");
    if (scenario == "store_scalar_args") yaml.replace("args: [eks, get-token, --cluster-name, example, --profile, prod]", "args: bad");
    if (scenario == "store_mapping_arg") yaml.replace("args: [eks, get-token, --cluster-name, example, --profile, prod]", "args: [{bad: value}]");
    if (scenario == "store_scalar_env") yaml.replace("command: aws", "command: aws\n      env: bad");
    if (scenario == "store_empty_env") yaml.replace("command: aws", "command: aws\n      env: [{name: '', value: bad}]");
    if (scenario == "store_duplicate_env") yaml.replace("command: aws", "command: aws\n      env: [{name: AWS_PROFILE, value: prod}, {name: AWS_PROFILE, value: prod}]");
    if (scenario == "store_env") yaml.replace("command: aws", "command: aws\n      env: [{name: AWS_REGION, value: eu-west-1}]");
    if (scenario == "store_bad_profile") yaml.replace("--profile, prod", "--profile, 'bad[profile'");
    if (scenario == "store_bad_action") yaml.replace("eks, get-token", "eks, not-token");
    if (scenario == "store_bad_cluster") yaml.replace("--cluster-name, example", "--cluster-name, bad/name");
    if (scenario == "store_bad_field_type") yaml.replace("command: aws", "command: {bad: field}");
    const auto source = store.importText(root.filePath("cluster.yaml"), yaml);
    require(std::holds_alternative<SourceSnapshot>(source), "Cannot import real EKS kubeconfig.");
    const auto snapshot = std::get<SourceSnapshot>(source);
    const auto id = snapshot.contexts.first().id, second = snapshot.contexts.last().id;
    const auto cfgPath = root.filePath("config"), keyPath = root.filePath("credentials");
    write(cfgPath, scenario == "store_sso" ? ssoConfig : cfg); write(keyPath, creds);
    const auto privatePath = profile + "/kubeconfigs/aws-" + id.mid(4) + ".cred";
    std::unique_ptr<QLockFile> lock;
    if (scenario == "store_locked") { lock = std::make_unique<QLockFile>(profile + "/kubeconfigs/sources.lock"); require(lock->tryLock(0), "Cannot own source lock."); }
    if (scenario == "store_bad_file") write(cfgPath, "bad profile");
    if (scenario == "store_directory") require(QDir().mkdir(privatePath), "Cannot create blocked credential target.");
    if (scenario == "store_symlink") require(QFile::link(cfgPath, privatePath), "Cannot create protected-record symlink.");
    if (scenario == "store_bad_storage") {
        require(QDir().rename(profile + "/kubeconfigs", profile + "/retained"), "Cannot retain isolated source directory.");
        write(profile + "/kubeconfigs", "not a directory");
    }
    if (scenario == "store_lock_directory") require(QDir().mkdir(profile + "/kubeconfigs/sources.lock"), "Cannot block source lock.");
    const auto target = scenario == "store_bad_id" ? "invalid" : scenario == "store_missing_context" ? "ctx:" + QString(64, '0') : id;
    const auto imported = store.importAwsProfile(target, scenario == "store_missing_file" ? root.filePath("missing") : cfgPath,
        scenario == "store_sso" ? QString{} : scenario == "store_missing_credentials" ? root.filePath("missing") : keyPath);
    if (scenario == "store_locked" || scenario == "store_bad_file" || scenario == "store_directory" || scenario == "store_bad_id"
        || scenario == "store_missing_context" || scenario == "store_missing_file" || scenario == "store_missing_credentials" || scenario == "store_symlink"
        || scenario == "store_other_program" || scenario == "store_no_exec" || scenario == "store_no_args" || scenario == "store_scalar_args"
        || scenario == "store_mapping_arg" || scenario == "store_scalar_env" || scenario == "store_empty_env" || scenario == "store_duplicate_env"
        || scenario == "store_bad_profile" || scenario == "store_bad_action" || scenario == "store_bad_cluster" || scenario == "store_bad_field_type"
        || scenario == "store_bad_storage" || scenario == "store_lock_directory") {
        require(std::holds_alternative<Failure>(imported), "Invalid private AWS import succeeded."); return;
    }
    require(std::holds_alternative<SourceSnapshot>(imported), "AWS import failed.");
    require(!read(privatePath).contains("unrelated") && !read(privatePath).contains("output"), "Unrelated AWS profile data was persisted.");
    require(!(QFileInfo(privatePath).permissions() & (QFile::ReadGroup | QFile::WriteGroup | QFile::ReadOther | QFile::WriteOther)), "Private AWS credentials have public permissions.");
    const auto resolved = store.connection(id);
    require(std::holds_alternative<ClusterConnection>(resolved), "Imported AWS profile cannot resolve.");
    const auto& connection = std::get<ClusterConnection>(resolved);
    require(!connection.credentialReady && connection.exec && connection.exec->eks && connection.credentialId == importedCredentialId(id), "AWS import authorized or shared the wrong context.");
    require(!QJsonDocument(sourceJson(snapshot)).toJson().contains("private-key-value"), "Source metadata exposed AWS keys.");
    if (scenario == "store_sso") { require(connection.exec->eks->sso.has_value(), "Stored SSO profile was lost."); return; }
    if (scenario == "store_catalog_corrupt") {
        const auto retained = read(privatePath);
        write(keyPath, QByteArray(keys).replace("IMPORTEDKEY", "REPLACEMENT"));
        write(profile + "/sessions.json", "{invalid session catalog");
        require(std::holds_alternative<Failure>(store.importAwsProfile(id, cfgPath, keyPath))
            && !retained.isEmpty() && read(privatePath) == retained,
            "A corrupt session catalog did not preserve the previously imported AWS identity.");
        return;
    }
    if (scenario == "store_env") { require(connection.exec->eks->region == "eu-west-1", "Exec environment did not retain region precedence."); return; }
    if (scenario.startsWith("record_")) {
        auto json = QJsonDocument::fromJson(read(privatePath)).object();
        const auto mode = scenario.mid(7);
        if (mode == "version") json["version"] = 2;
        if (mode == "profile") json["profile"] = "other";
        if (mode == "missing") json.remove("profile");
        if (mode == "fields") json["fields"] = QJsonArray{};
        if (mode == "extra") json["fields"] = QJsonObject{{"unexpected", "private-content"}};
        if (mode == "type") json["fields"] = QJsonObject{{"region", 7}};
        write(privatePath, mode == "large" ? QByteArray(65537, 'x') : mode == "array" ? QByteArray("[]") : QJsonDocument(json).toJson());
        if (mode == "directory") { require(QFile::remove(privatePath) && QDir().mkdir(privatePath), "Cannot obstruct private credential read."); }
        if (mode == "symlink") { require(QFile::remove(privatePath) && QFile::link(cfgPath, privatePath), "Cannot link private credential record."); }
        require(std::holds_alternative<Failure>(store.connection(id)), "Malformed protected AWS record was ignored or authorized."); return;
    }
    if (scenario == "store_corrupt") { write(privatePath, "broken"); require(std::holds_alternative<Failure>(store.connection(id)), "Corrupt AWS private record was ignored."); return; }
    if (scenario == "store_replace") { write(keyPath, QByteArray(keys).replace("IMPORTEDKEY", "REPLACEMENT")); require(std::holds_alternative<SourceSnapshot>(store.importAwsProfile(id, cfgPath, keyPath)), "Cannot replace AWS credentials."); require(std::get<ClusterConnection>(store.connection(id)).exec->eks->accessKey == "REPLACEMENT", "Old AWS key survived replacement."); return; }
    if (scenario == "store_removal" || scenario == "store_removal_failure") {
        require(std::holds_alternative<SourceSnapshot>(store.importAwsProfile(second, cfgPath, keyPath)), "Cannot import second context.");
        if (scenario == "store_removal_failure") { require(QFile::remove(privatePath) && QDir().mkdir(privatePath), "Cannot block private file removal."); }
        const auto removed = store.removeContext(id, {});
        require(scenario == "store_removal" ? std::holds_alternative<SessionCatalog>(removed) && !QFileInfo::exists(privatePath)
            : std::holds_alternative<Failure>(removed) && std::get<Failure>(removed).code == StoreError::CleanupFailed, "Wrong AWS removal result.");
        require(std::holds_alternative<ClusterConnection>(store.connection(second)), "Removing one AWS profile removed another context's key."); return;
    }
    CredentialProcess owner;
    QSignalSpy completed(&owner, &CredentialProcess::completed);
    require(owner.start(connection, "session") && completed.count() == 1, "Native imported EKS did not complete without a CLI.");
    const auto ready = owner.cached(connection.credentialId);
    require(ready && ready->credentialReady && ready->exec && !ready->exec->eks && !ready->authorization.contains("private-key-value"), "Ready connection retained AWS signing material.");
    require(owner.revoke(connection.credentialId) && !owner.cached(connection.credentialId), "Revoked AWS credentials remained available.");
    if (scenario == "store_success") return;
    throw std::runtime_error("Unknown AWS profile scenario.");
}
} // namespace
int main(int argc, char** argv) {
    QGuiApplication app(argc, argv);
    if (argc != 2) return 2;
    try { run(QString::fromUtf8(argv[1])); return 0; }
    catch (const std::exception& error) { std::fprintf(stderr, "%s\n", error.what()); return 1; }
}
