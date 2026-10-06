#include "yaml_apply.h"
#include <QCoreApplication>
#include <QJsonDocument>
#include <cstdio>
#include <limits>
using namespace podlord;
namespace {
const QJsonObject base{{"apiVersion", "v1"}, {"kind", "ConfigMap"}, {"metadata", QJsonObject{
    {"name", "checked"}, {"namespace", "default"}, {"uid", "check-uid"}, {"resourceVersion", "opaque/version:2"}}},
    {"data", QJsonObject{{"setting", "before"}, {"retained", "same"}}}};
bool run(const QString& scenario) {
    auto original = base, desired = base;
    desired["data"] = QJsonObject{{"setting", "after"}, {"retained", "same"}};
    qint64 limit = 3 * 1048576;
    QString text;
    bool rejected = false;
    if (scenario.startsWith("merge_")) {
        auto current = base;
        auto metadata = current["metadata"].toObject(); metadata["resourceVersion"] = "new/version"; current["metadata"] = metadata;
        if (scenario == "merge_disjoint") current["data"] = QJsonObject{{"setting", "before"}, {"retained", "server"}};
        else if (scenario == "merge_equal") current["data"] = desired["data"];
        else if (scenario == "merge_array") { original["items"] = QJsonArray{1}; desired["items"] = QJsonArray{2}; current["items"] = QJsonArray{3}; desired["data"] = base["data"]; }
        else if (scenario == "merge_delete") { auto data = desired["data"].toObject(); data.remove("retained"); desired["data"] = data; current["data"] = QJsonObject{{"setting", "before"}, {"retained", "server"}}; }
        else current["data"] = QJsonObject{{"setting", "server"}, {"retained", "same"}};
        const bool choices = scenario == "merge_mine" || scenario == "merge_server";
        const auto result = mergeYaml(original, desired, current, choices ? QMap<QString,bool>{{"/data/setting", scenario == "merge_mine"}} : QMap<QString,bool>{});
        if (scenario == "merge_disjoint") return result.conflicts.isEmpty() && result.document["data"].toObject()["setting"] == "after" && result.document["data"].toObject()["retained"] == "server" && result.document["metadata"].toObject()["resourceVersion"] == "new/version";
        if (scenario == "merge_equal") return result.conflicts.isEmpty() && result.document == current;
        if (choices) return result.conflicts.isEmpty() && result.document["data"].toObject()["setting"] == (scenario == "merge_mine" ? "after" : "server");
        return result.conflicts == QStringList{scenario == "merge_array" ? "/items" : scenario == "merge_delete" ? "/data/retained" : "/data/setting"};
    }
    if (scenario.startsWith("observe_")) {
        auto current = desired;
        if (scenario == "observe_defaults") { current["server"] = "extra"; auto metadata = current["metadata"].toObject(); metadata["resourceVersion"] = "new"; current["metadata"] = metadata; }
        else if (scenario == "observe_wrong_uid") { auto metadata = current["metadata"].toObject(); metadata["uid"] = "recreated"; current["metadata"] = metadata; }
        else if (scenario == "observe_missing") current.remove("data");
        else if (scenario == "observe_delete") { auto data = desired["data"].toObject(); data.remove("retained"); desired["data"] = data; current = desired; }
        return yamlChangesObserved(original, desired, current) == (scenario == "observe_defaults" || scenario == "observe_delete");
    }
    if (scenario.startsWith("secret_")) {
        original["kind"] = "Secret";
        original["data"] = QJsonObject{{"private", "c2VjcmV0LXZhbHVl"}};
        auto metadata = original["metadata"].toObject(); metadata["annotations"] = QJsonObject{{"kubectl.kubernetes.io/last-applied-configuration", "secret-value-embedded"}}; original["metadata"] = metadata;
        desired = original; metadata["labels"] = QJsonObject{{"changed", "yes"}}; desired["metadata"] = metadata;
        if (scenario == "secret_changed") desired["data"] = QJsonObject{{"private", "bmV3LXNlY3JldA=="}};
        text = resourceYaml(desired);
        if (scenario == "secret_new_placeholder") { text += "stringData:\n  new: '[hidden]'\n"; rejected = true; }
        if (scenario == "secret_changed") text.replace("\"[hidden]\"", "\"bmV3LXNlY3JldA==\"", Qt::CaseSensitive); // restore annotation separately below
        if (scenario == "secret_changed") text.replace("\"kubectl.kubernetes.io/last-applied-configuration\": \"bmV3LXNlY3JldA==\"", "\"kubectl.kubernetes.io/last-applied-configuration\": \"[hidden]\"");
    } else if (scenario == "no_changes") { desired = original; rejected = true; }
    else if (scenario == "zero_limit") { limit = 0; rejected = true; }
    else if (scenario == "text_limit") { limit = 10; rejected = true; }
    else if (scenario == "patch_limit") { auto data = original["data"].toObject(); for (int i=0; i<50; ++i) data["old" + QString::number(i)] = "before"; original["data"] = data; limit = 500; rejected = true; }
    else if (scenario == "escaped_key") desired["data"] = QJsonObject{{"setting", "before"}, {"retained", "same"}, {"a~/b", "after"}};
    else if (scenario == "array") desired["list"] = QJsonArray{1, false, QJsonValue::Null, "text"};
    else if (scenario == "integer") desired["number"] = QJsonValue(std::numeric_limits<qint64>::max());
    if (text.isEmpty()) text = resourceYaml(desired);
    if (scenario == "typed_bool") text += "number: !!bool true\n";
    if (scenario == "bad_tag") { text += "number: !custom true\n"; rejected = true; }
    if (scenario == "bad_int") { text += "number: !!int true\n"; rejected = true; }
    if (scenario == "bad_bool") { text += "number: !!bool 123\n"; rejected = true; }
    if (scenario == "integer_overflow") { text += "number: 1844674407370955161600\n"; rejected = true; }
    if (scenario == "nonfinite") { text += "number: .nan\n"; rejected = true; }
    if (scenario == "alias_expansion") { text += "a0: &a0 [one, two]\n"; for (int i=1; i<30; ++i) text += QString("a%1: &a%1 [*a%2, *a%2]\n").arg(i).arg(i-1); limit = 65536; rejected = true; }
    const auto result = prepareYaml(text, original, limit);
    if (rejected) {
        const auto* error = std::get_if<Failure>(&result);
        return error && !error->message.isEmpty() && !error->message.contains("secret-value") && !error->message.contains("c2VjcmV0");
    }
    const auto* prepared = std::get_if<PreparedYaml>(&result);
    if (!prepared) { std::fprintf(stderr, "%s\n", qPrintable(std::get<Failure>(result).message)); return false; }
    if (prepared->patch[0].toObject()["path"] != "/metadata/uid" || prepared->patch[1].toObject()["path"] != "/metadata/resourceVersion") return false;
    if (scenario == "secret_changed") return prepared->document["data"].toObject()["private"] == "bmV3LXNlY3JldA==" && !prepared->preview.contains("bmV3") && !prepared->preview.contains("secret-value");
    if (scenario == "secret_retained") return prepared->document["data"] == original["data"] && prepared->document["metadata"].toObject()["annotations"] == original["metadata"].toObject()["annotations"] && prepared->patch.size() == 3 && !prepared->preview.contains("c2Vj");
    if (scenario == "escaped_key") return prepared->patch[2].toObject()["path"] == "/data/a~0~1b";
    if (scenario == "integer") return prepared->document["number"].toInteger() == std::numeric_limits<qint64>::max();
    if (scenario == "typed_bool") return prepared->document["number"] == true;
    return scenario == "array" ? prepared->document["list"] == desired["list"] : prepared->document == desired;
}
}
static int stringDataCase(const QString &scenario) {
    const QJsonObject baseline{
        {"apiVersion", "v1"}, {"kind", scenario == "string_data_non_secret" ? "ConfigMap" : "Secret"},
        {"metadata", QJsonObject{{"name", "entry"}, {"namespace", "test"},
                                 {"uid", "secret-uid"}, {"resourceVersion", "7"}}},
        {"data", QJsonObject{{"alpha", "b2xk"}}}};
    const QString key = scenario == "string_data_override" || scenario == "string_data_same" ? "alpha" : "own";
    const QString entered = scenario == "string_data_unicode" ? QString::fromUtf8("new-\xc3\xbc")
        : scenario == "string_data_empty" ? QString{}
        : scenario == "string_data_same" ? QStringLiteral("old")
        : scenario == "string_data_limit" ? QString(1500, QChar('x')) : QStringLiteral("new-local-value");
    const QString text = resourceYaml(baseline) + "\nstringData:\n  " + key + ": \"" + entered + "\"\n";
    const auto result = prepareYaml(text, baseline, scenario == "string_data_limit" ? 1900 : 1024 * 1024);
    const auto *prepared = std::get_if<PreparedYaml>(&result);
    if (scenario == "string_data_limit" || scenario == "string_data_same") {
        if (prepared) { QTextStream(stderr) << "Expected local rejection without a patch\n"; return 1; }
        return 0;
    }
    if (!prepared) { QTextStream(stderr) << "Expected a prepared Secret change\n"; return 1; }
    if (scenario == "string_data_non_secret")
        return prepared->document.value("stringData").toObject().value(key).toString() == entered ? 0 : 1;
    const QString encoded = QString::fromLatin1(entered.toUtf8().toBase64());
    if (prepared->document.contains("stringData") ||
        prepared->document.value("data").toObject().value(key).toString() != encoded ||
        (key != "alpha" && prepared->document.value("data").toObject().value("alpha") != baseline.value("data").toObject().value("alpha"))) {
        QTextStream(stderr) << "Write-only Secret input was not normalized into data\n"; return 1;
    }
    auto observed = prepared->document;
    auto metadata = observed.value("metadata").toObject();
    metadata.insert("resourceVersion", "8");
    observed.insert("metadata", metadata);
    if (!yamlChangesObserved(baseline, prepared->document, observed)) {
        QTextStream(stderr) << "Read-back did not recognize the persisted Secret value\n"; return 1;
    }
    if (!entered.isEmpty() && (prepared->preview.contains(entered) || prepared->preview.contains(encoded) ||
                              resourceYaml(observed).contains(entered) || resourceYaml(observed).contains(encoded))) {
        QTextStream(stderr) << "Secret value escaped into a comparison or loaded YAML\n"; return 1;
    }
    return 0;
}

int main(int argc, char** argv) {
    if (argc == 2 && QString::fromLocal8Bit(argv[1]).startsWith("string_data_"))
        return stringDataCase(QString::fromLocal8Bit(argv[1])); QCoreApplication app(argc, argv); return app.arguments().size() == 2 && run(app.arguments()[1]) ? 0 : 1; }
