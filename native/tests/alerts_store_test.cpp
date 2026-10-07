#include "alerts.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QFile>
#include <QJsonDocument>
#include <QLockFile>
#include <QDir>
#include <future>
#include <cstdio>

namespace {
bool run(const QString& scenario) {
    QTemporaryDir temporary; if (!temporary.isValid()) return false;
    const auto profile=temporary.filePath("profile"); podlord::AlertStore store(profile);
    const auto loaded=store.load();
    if (const auto* failure = std::get_if<podlord::Failure>(&loaded)) {
        std::fprintf(stderr, "Alert catalog load failed: %s\n", qPrintable(failure->message));
        return false;
    }
    const auto original=std::get<podlord::AlertCatalog>(loaded);
    if (scenario=="missing") return original.rules.size()==3 && !QFile::exists(profile);
    if (scenario.startsWith("description_")) {
        const QStringList descriptions{
            "Paint resources yellow or red while they have an active problem.",
            "Highlight recently changed resources green for the freshness window.",
            "Pulse active resources briefly when they enter the view."};
        const auto index=scenario=="description_problem" ? 0 : scenario=="description_recent" ? 1 : 2;
        return original.rules[index].value["description"].toString()==descriptions[index] && !QFile::exists(profile);
    }
    auto desired=original;
    auto custom=original.rules[0].value; custom["id"]="custom"; custom["builtIn"]=false; custom["name"]="Custom";
    const auto parsed=podlord::parseAlertRule(custom); if (!std::holds_alternative<podlord::AlertRule>(parsed)) return false;
    desired.rules.append(std::get<podlord::AlertRule>(parsed));
    if (scenario=="invalid_save") { desired.rules.last().value["name"]=""; return std::holds_alternative<podlord::Failure>(store.save(desired, original)) && !QFile::exists(profile); }
    if (scenario.startsWith("invalid_")) {
        const auto field=scenario.mid(8);
        if (field=="name") custom["name"]=" ";
        else if (field=="groups") custom["groups"]=QJsonArray{};
        else if (field=="group") custom["groups"]=QJsonArray{QJsonValue(QJsonArray{})};
        else if (field=="scope") custom["groups"]=QJsonArray{QJsonValue(QJsonArray{QJsonObject{{"field", "unknown"}, {"expression", "x"}}})};
        else if (field=="expression") custom["groups"]=QJsonArray{QJsonValue(QJsonArray{QJsonObject{{"field", "name"}, {"expression", ""}}})};
        else if (field=="regex") custom["groups"]=QJsonArray{QJsonValue(QJsonArray{QJsonObject{{"field", "name"}, {"expression", "/[/"}}})};
        else if (field=="number") custom["groups"]=QJsonArray{QJsonValue(QJsonArray{QJsonObject{{"field", "restarts"}, {"expression", "=>5"}}})};
        else if (field=="opposed_comparison") custom["groups"]=QJsonArray{QJsonValue(QJsonArray{QJsonObject{{"field", "restarts"}, {"expression", "<>5"}}})};
        else if (field=="duration") custom["groups"]=QJsonArray{QJsonValue(QJsonArray{QJsonObject{{"field", "age"}, {"expression", ">5z"}}})};
        else if (field=="boolean") custom["groups"]=QJsonArray{QJsonValue(QJsonArray{QJsonObject{{"field", "problems"}, {"expression", "maybe"}}})};
        else if (field=="extra") custom["extra"]=1;
        else if (field=="missing") custom.remove("name");
        else if (field=="color") custom["color"]="bad";
        else if (field=="mode") custom["colorMode"]="bad";
        else if (field=="animation") custom["animation"]="bad";
        else if (field=="seconds") custom["colorSeconds"]=0;
        else if (field=="fraction") custom["zoom"]=1.5;
        else if (field=="sound") custom["sound"]="remote-sound";
        else if (field=="minimum") custom["soundMinimumMatches"]=0;
        else return false;
        return std::holds_alternative<podlord::Failure>(podlord::parseAlertRule(custom)) && !QFile::exists(profile);
    }
    if (scenario=="relative") return std::holds_alternative<podlord::Failure>(podlord::AlertStore("relative").load());
    if (scenario=="symlink") {
        const auto target=temporary.filePath("target"); QFile file(target); if (!file.open(QIODevice::WriteOnly) || file.write("private")<0) return false; file.close();
        if (!QDir().mkpath(profile) || !QFile::link(target, profile+"/alert-rules.json")) return false;
        return std::holds_alternative<podlord::Failure>(store.load()) && std::holds_alternative<podlord::Failure>(store.save(desired, original));
    }
    if (scenario=="busy") {
        if (!QDir().mkpath(profile)) return false;
        QLockFile lock(profile+"/alert-rules.json.lock"); if (!lock.tryLock(0)) return false;
        const auto result=store.save(desired, original);
        return std::holds_alternative<podlord::Failure>(result) && std::get<podlord::Failure>(result).code==podlord::StoreError::Busy;
    }
    if (scenario=="concurrent") {
        const auto save=[&] { return store.save(desired, original); };
        auto first=std::async(std::launch::async, save), second=std::async(std::launch::async, save);
        const auto a=first.get(), b=second.get();
        return std::holds_alternative<podlord::AlertCatalog>(a)!=std::holds_alternative<podlord::AlertCatalog>(b);
    }
    const auto saved=store.save(desired, original); if (!std::holds_alternative<podlord::AlertCatalog>(saved)) return false;
    const auto path=profile+"/alert-rules.json";
    if (scenario=="private") {
        const auto permissions=QFileInfo(path).permissions();
        return permissions.testFlag(QFile::ReadOwner) && permissions.testFlag(QFile::WriteOwner)
            && !(permissions & (QFile::ReadGroup|QFile::WriteGroup|QFile::ExeGroup|QFile::ReadOther|QFile::WriteOther|QFile::ExeOther));
    }
    if (scenario=="restore") { const auto result=store.load(); return std::holds_alternative<podlord::AlertCatalog>(result) && std::get<podlord::AlertCatalog>(result).rules.size()==4; }
    if (scenario=="conflict") { const auto result=store.save(original, original); return std::holds_alternative<podlord::Failure>(result) && std::get<podlord::Failure>(result).code==podlord::StoreError::Conflict; }
    if (scenario=="repeat") return std::holds_alternative<podlord::AlertCatalog>(store.save(desired, desired));
    QFile file(path); if (!file.open(QIODevice::ReadOnly)) return false;
    const auto previous=file.readAll(); file.close(); auto root=QJsonDocument::fromJson(previous).object();
    if (scenario.startsWith("legacy_descriptions_")) {
        auto rules=root["rules"].toArray();
        for (int index=0; index<3; ++index) {
            auto rule=rules[index].toObject(); rule["description"]="Built-in desktop alert";
            if (index==0) rule["enabled"]=false;
            if (scenario=="legacy_descriptions_locked" && index==0) rule["name"]="Changed built-in";
            rules[index]=rule;
        }
        root["rules"]=rules;
        const auto legacy=QJsonDocument(root).toJson();
        if (!file.open(QIODevice::WriteOnly|QIODevice::Truncate) || file.write(legacy)!=legacy.size()) return false;
        file.close();
        const auto restored=store.load();
        if (scenario=="legacy_descriptions_locked") return std::holds_alternative<podlord::Failure>(restored)
            && file.open(QIODevice::ReadOnly) && file.readAll()==legacy;
        if (!std::holds_alternative<podlord::AlertCatalog>(restored)) return false;
        const auto catalog=std::get<podlord::AlertCatalog>(restored);
        if (catalog.rules.size()!=4 || catalog.rules[0].value["enabled"].toBool()
            || catalog.rules[3].value!=desired.rules[3].value) return false;
        for (int index=0; index<3; ++index)
            if (catalog.rules[index].value["description"]!=original.rules[index].value["description"]
                || catalog.rules[index].value["description"]=="Built-in desktop alert") return false;
        if (scenario=="legacy_descriptions_read") return file.open(QIODevice::ReadOnly) && file.readAll()==legacy;
        return scenario=="legacy_descriptions_save" && std::holds_alternative<podlord::AlertCatalog>(store.save(catalog,catalog))
            && file.open(QIODevice::ReadOnly) && !file.readAll().contains("Built-in desktop alert");
    }
    if (scenario=="future") root["version"]=2;
    else if (scenario=="extra_root") root["unknown"]=true;
    else if (scenario=="duplicate") { auto rules=root["rules"].toArray(); rules.append(rules.last()); root["rules"]=rules; }
    else if (scenario=="locked") { auto rules=root["rules"].toArray(); auto rule=rules[0].toObject(); rule["name"]="Changed built-in"; rules[0]=rule; root["rules"]=rules; }
    else if (scenario=="locked_description") { auto rules=root["rules"].toArray(); auto rule=rules[0].toObject(); rule["description"]="Changed built-in"; rules[0]=rule; root["rules"]=rules; }
    else if (scenario=="malformed") root={};
    else return false;
    const auto invalid=QJsonDocument(root).toJson(); if (!file.open(QIODevice::WriteOnly|QIODevice::Truncate) || file.write(invalid)!=invalid.size()) return false; file.close();
    if (!std::holds_alternative<podlord::Failure>(store.load()) || !std::holds_alternative<podlord::Failure>(store.save(desired, desired))) return false;
    if (!file.open(QIODevice::ReadOnly)) return false; return file.readAll()==invalid;
}
}
int main(int argc, char** argv) {
    QCoreApplication app(argc, argv); if (argc!=2) return 2;
    const bool passed=run(QString::fromLocal8Bit(argv[1]));
    if (!passed) std::fprintf(stderr, "Alert store scenario failed: %s\n", argv[1]); return passed ? 0 : 1;
}
