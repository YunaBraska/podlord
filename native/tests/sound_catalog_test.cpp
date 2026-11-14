#include "alerts.h"
#include "music.h"
#include <QCryptographicHash>
#include <QFile>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QSet>
#include <QTemporaryDir>
#include <QTest>
#include <cstdio>

namespace {
bool run(const QString& scenario, const QString& input) {
    QTemporaryDir temporary;
    if (!temporary.isValid()) return false;
    podlord::ResourceClient client;
    podlord::Alerts alerts(temporary.path(), &client, nullptr);
    for (int attempt = 0; alerts.busy() && attempt < 500; ++attempt) QTest::qWait(10);
    if (alerts.busy() || !alerts.error().isEmpty()) return false;
    const auto sounds = alerts.sounds(), tracks = podlord::bundledTracks();
    if (scenario == "catalog") {
        if (sounds.size() != 21 || tracks.size() != 20 || sounds.first().toMap()["id"] != "none") return false;
        for (const auto& list : {sounds, tracks}) for (const auto& entry : list) {
            const auto item = entry.toMap();
            if (item["id"].toString().isEmpty() || item["name"].toString().isEmpty()
                || item.contains("author") || item.contains("source") || item.contains("license")) return false;
        }
        return alerts.sounds() == sounds;
    }
    if (scenario == "assets") {
        QSet<QString> identities;
        QSet<QByteArray> hashes;
        for (const auto& list : {sounds, tracks}) for (const auto& entry : list) {
            const auto item = entry.toMap();
            const auto id = item["id"].toString();
            if (identities.contains(id)) return false;
            identities.insert(id);
            if (id == "none") continue;
            const QUrl asset(item["asset"].toString());
            QFile file(":" + asset.path());
            if (asset.scheme() != "qrc" || !asset.host().isEmpty() || asset.hasQuery() || asset.hasFragment()
                || !asset.path().startsWith("/podlord/audio/") || !file.open(QIODevice::ReadOnly) || file.size() <= 0) return false;
            const auto hash = QCryptographicHash::hash(file.readAll(), QCryptographicHash::Sha256);
            if (hashes.contains(hash)) return false;
            hashes.insert(hash);
        }
        return identities.size() == 41 && hashes.size() == 40;
    }
    if (scenario == "legacy_migration") {
        QFile catalog(":/podlord/audio/catalog.json");
        if (!catalog.open(QIODevice::ReadOnly)) return false;
        const auto aliases = QJsonDocument::fromJson(catalog.readAll()).object()["legacySounds"].toObject();
        if (aliases.size() != 122) return false;
        auto rule = QJsonObject::fromVariantMap(alerts.rules().first().toMap());
        rule["id"] = "retained-rule"; rule["name"] = "Retained rule";
        rule["builtIn"] = false; rule["enabled"] = false;
        const auto path = temporary.filePath("alert-rules.json");
        for (auto alias = aliases.begin(); alias != aliases.end(); ++alias) {
            rule["sound"] = alias.key();
            const auto bytes = QJsonDocument(QJsonObject{{"version", 2}, {"rules", QJsonArray{rule}}, {"muted", true}, {"reducedMotion", true}}).toJson();
            QFile file(path);
            if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size()) return false;
            file.close();
            const auto restored = podlord::AlertStore(temporary.path()).load();
            const auto* result = std::get_if<podlord::AlertCatalog>(&restored);
            if (!result || !result->muted || !result->reducedMotion || result->rules.last().value["sound"] != alias.value()
                || result->rules.last().value["enabled"].toBool() || result->rules.last().value["id"] != "retained-rule") return false;
            if (!file.open(QIODevice::ReadOnly) || file.readAll() != bytes) return false;
            file.close();
            const auto saved = podlord::AlertStore(temporary.path()).save(*result, *result);
            if (!std::holds_alternative<podlord::AlertCatalog>(saved) || !file.open(QIODevice::ReadOnly)
                || !file.readAll().contains(alias.value().toString().toUtf8())) return false;
        }
        return true;
    }
    auto draft = alerts.rules().first().toMap();
    draft["id"] = "sound-roundtrip"; draft["name"] = "Sound roundtrip"; draft["builtIn"] = false;
    draft["sound"] = scenario == "unknown" ? "unknown-sound" : input;
    if (scenario == "unknown") return !alerts.saveRule(draft) && !alerts.error().isEmpty();
    if (scenario != "roundtrip" || !alerts.saveRule(draft)) return false;
    for (int attempt = 0; alerts.busy() && attempt < 500; ++attempt) QTest::qWait(10);
    if (alerts.busy() || !alerts.error().isEmpty()) return false;
    const auto restored = podlord::AlertStore(temporary.path()).load();
    const auto* result = std::get_if<podlord::AlertCatalog>(&restored);
    return result && result->rules.last().value["sound"].toString() == input;
}
}
int main(int argc, char** argv) {
    QGuiApplication app(argc, argv);
    if (argc != 3) return 2;
    const bool passed = run(QString::fromLocal8Bit(argv[1]), QString::fromLocal8Bit(argv[2]));
    if (!passed) std::fprintf(stderr, "Sound catalog scenario failed: %s %s\n", argv[1], argv[2]);
    return passed ? 0 : 1;
}
