#include "alerts.h"
#include <QFile>
#include <QGuiApplication>
#include <QRegularExpression>
#include <QSet>
#include <QTemporaryDir>
#include <QTest>
#include <cstdio>

namespace {
QVariantList reference(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    const QString source = QString::fromUtf8(file.readAll());
    QMap<QString, QString> constants;
    auto definitions = QRegularExpression("private const string (\\w+) = \"([^\"]+)\";").globalMatch(source);
    while (definitions.hasNext()) {
        const auto match = definitions.next();
        constants.insert(match.captured(1), match.captured(2));
    }
    const auto resolve = [&](QString value) {
        value = value.trimmed();
        return value.startsWith('"') ? value.mid(1, value.size() - 2) : constants.value(value);
    };
    QVariantList result;
    auto curated = QRegularExpression(R"rx(new\(\s*"([^"]+)"\s*,\s*"([^"]+)"\s*,\s*"([^"]+)"\s*,\s*([^,]+),\s*([^,]+),\s*([^,]+),\s*"([^"]+)"\s*(,\s*IsMusic:\s*true)?\s*\))rx").globalMatch(source);
    while (curated.hasNext()) {
        const auto match = curated.next();
        const auto asset = match.captured(7);
        result.append(QVariantMap{{"id", match.captured(1)}, {"name", match.captured(2)},
            {"purpose", match.captured(3)}, {"author", resolve(match.captured(4))},
            {"license", resolve(match.captured(5))}, {"source", resolve(match.captured(6))},
            {"asset", asset == "none" ? "" : "qrc:/podlord/audio/" + asset.mid(13)},
            {"isMusic", !match.captured(8).isEmpty()}});
    }
    const auto start = source.indexOf("private static readonly string[] KenneyInterfaceFiles");
    const auto end = source.indexOf("];", start);
    if (start < 0 || end < 0 || result.size() != 17) return {};
    auto files = QRegularExpression("\"([^\"]+\\.ogg)\"").globalMatch(source.mid(start, end - start));
    while (files.hasNext()) {
        const auto fileName = files.next().captured(1);
        QString base = fileName.chopped(4);
        auto parts = base.split('_');
        for (auto& part : parts) if (!part.isEmpty()) part[0] = part[0].toUpper();
        result.append(QVariantMap{{"id", "kenney-interface-" + base.replace('_', '-')},
            {"name", parts.join(' ')}, {"purpose", "Interface command sound from Kenney Interface Sounds."},
            {"author", constants.value("KenneyAuthor")}, {"license", constants.value("KenneyLicense")},
            {"source", constants.value("KenneyInterfaceUrl")},
            {"asset", "qrc:/podlord/audio/interface/" + fileName}, {"isMusic", false}});
    }
    return result.size() == 117 ? result : QVariantList{};
}

bool run(const QString& scenario, const QString& input) {
    QTemporaryDir temporary;
    if (!temporary.isValid()) return false;
    podlord::ResourceClient client;
    podlord::Alerts alerts(temporary.path(), &client, nullptr);
    for (int attempt = 0; alerts.busy() && attempt < 500; ++attempt) QTest::qWait(10);
    if (alerts.busy() || !alerts.error().isEmpty()) return false;
    const auto sounds = alerts.sounds();
    if (scenario == "reference") {
        const auto expected = reference(input);
        return !expected.isEmpty() && sounds == expected && alerts.sounds() == sounds;
    }
    if (scenario == "assets") {
        QSet<QString> identities;
        for (const auto& entry : sounds) {
            const auto sound = entry.toMap();
            const auto id = sound["id"].toString();
            if (identities.contains(id)) return false;
            identities.insert(id);
            if (id == "none") continue;
            const QUrl asset(sound["asset"].toString());
            QFile file(":" + asset.path());
            if (asset.scheme() != "qrc" || !file.open(QIODevice::ReadOnly) || file.read(4) != "OggS"
                || sound["author"] != "Kenney" || sound["license"] != "CC0-1.0") return false;
        }
        return identities.size() == 117;
    }
    if (scenario != "roundtrip") return false;
    auto draft = alerts.rules().first().toMap();
    draft["id"] = "sound-roundtrip";
    draft["name"] = "Sound roundtrip";
    draft["builtIn"] = false;
    draft["sound"] = input;
    if (!alerts.saveRule(draft)) return false;
    for (int attempt = 0; alerts.busy() && attempt < 500; ++attempt) QTest::qWait(10);
    if (alerts.busy() || !alerts.error().isEmpty()) return false;
    const auto restored = podlord::AlertStore(temporary.path()).load();
    const auto* catalog = std::get_if<podlord::AlertCatalog>(&restored);
    return catalog && catalog->rules.last().value["sound"].toString() == input
        && alerts.rules().last().toMap()["sound"].toString() == input;
}
}

int main(int argc, char** argv) {
    QGuiApplication app(argc, argv);
    Q_INIT_RESOURCE(alert_audio);
    if (argc != 3) return 2;
    const bool passed = run(QString::fromLocal8Bit(argv[1]), QString::fromLocal8Bit(argv[2]));
    if (!passed) std::fprintf(stderr, "Sound catalog scenario failed: %s %s\n", argv[1], argv[2]);
    return passed ? 0 : 1;
}
