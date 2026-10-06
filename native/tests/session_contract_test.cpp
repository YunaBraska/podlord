#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QTemporaryDir>
#include <QTimeZone>
#include <cstdio>
#include <stdexcept>
#include "session_store.h"

namespace {
bool require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
    return true;
}
struct Cli final {
    const QString executable;
    const QString profile;
    QJsonObject run(QStringList args) const {
        QProcess process;
        process.start(executable, QStringList{"--profile", profile} + args);
        require(process.waitForFinished(30000), "CLI did not finish");
        const QByteArray error = process.readAllStandardError();
        if (process.exitCode() != 0) std::fprintf(stderr, "%s\n", error.constData());
        require(process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0, "CLI command failed");
        const auto document = QJsonDocument::fromJson(process.readAllStandardOutput());
        require(document.isObject(), "CLI did not return JSON");
        return document.object();
    }
    QJsonObject create(QString name) const {
        const auto rows = run({"create", "--context", "local-context", "--name", name})["sessions"].toArray();
        return rows.last().toObject();
    }
    QJsonObject snapshot(QString id, QString context) const {
        return run({"snapshot", id, "--context", context})["sessions"].toArray().last().toObject();
    }
    bool edit(const std::function<QJsonObject(QJsonObject)>& change) const {
        QFile file(QDir(profile).filePath("sessions.json"));
        require(file.open(QIODevice::ReadOnly), "Cannot read external session data");
        auto root = QJsonDocument::fromJson(file.readAll()).object(); file.close();
        const auto bytes = QJsonDocument(change(root)).toJson();
        require(file.open(QIODevice::WriteOnly | QIODevice::Truncate), "Cannot alter external session data");
        return require(file.write(bytes) == bytes.size(), "External session write incomplete");
    }
    bool row(qsizetype index, QString key, QJsonValue value) const {
        return edit([&](QJsonObject root) {
            auto rows = root["sessions"].toArray(); auto row = rows[index].toObject();
            row[key] = value; rows[index] = row; root["sessions"] = rows; return root;
        });
    }
    bool fails(QStringList args, QString expected) const {
        QFile file(QDir(profile).filePath("sessions.json"));
        require(file.open(QIODevice::ReadOnly), "Cannot read retained data");
        const auto before = file.readAll(); file.close();
        QProcess process; process.start(executable, QStringList{"--profile", profile} + args);
        require(process.waitForFinished(30000), "Failure command did not finish");
        require(process.exitStatus() == QProcess::NormalExit && process.exitCode() == 1, "Expected explicit failure");
        require(QJsonDocument::fromJson(process.readAllStandardError()).object()["error"].toObject()["code"] == expected, "Wrong failure code");
        require(file.open(QIODevice::ReadOnly), "Cannot reread retained data");
        return require(file.readAll() == before, "Rejected operation changed data");
    }
};
} // namespace

int main(int argc, char* argv[]) {
    const QCoreApplication app(argc, argv);
    if (argc != 3) return 2;
    QTemporaryDir directory;
    if (!directory.isValid()) return 1;
    const Cli cli{QString::fromLocal8Bit(argv[1]), directory.path()};
    const QString scenario = QString::fromLocal8Bit(argv[2]);
    try {
        const auto first = cli.create("Production");
        const QString firstId = first["id"].toString();
        if (scenario == "named_suffix")
            return require(cli.snapshot(firstId, "changed")["displayName"] == "Production 2", "SES-005: named replacement must receive suffix 2") ? 0 : 1;
        if (scenario == "successive_suffix") {
            const auto second = cli.snapshot(firstId, "changed");
            return require(cli.snapshot(second["id"].toString(), "changed-again")["displayName"] == "Production 3", "SES-007: suffix must not accumulate") ? 0 : 1;
        }
        if (scenario == "independent_sequence") {
            const auto other = cli.create("Testing"); cli.snapshot(firstId, "changed");
            return require(cli.snapshot(other["id"].toString(), "changed-other")["displayName"] == "Testing 2", "SES-017: sequences must number independently") ? 0 : 1;
        }
        if (scenario == "renamed_sequence") {
            const auto second = cli.snapshot(firstId, "changed"); const QString secondId = second["id"].toString();
            cli.run({"rename", secondId, "Payments"});
            return require(cli.snapshot(secondId, "changed-again")["displayName"] == "Payments 2", "SES-019: rename must start a new base sequence") ? 0 : 1;
        }
        if (scenario == "occupied_suffix") {
            cli.create("Production 2");
            return require(cli.snapshot(firstId, "changed")["displayName"] == "Production 3", "SES-018: occupied names must be skipped") ? 0 : 1;
        }
        if (scenario == "repeat_changed_snapshot") {
            cli.run({"open", firstId});
            cli.run({"snapshot", firstId, "--context", "changed", "--namespace", "alpha", "--namespace", "zeta"});
            QFile file(QDir(cli.profile).filePath("sessions.json"));
            require(file.open(QIODevice::ReadOnly), "Cannot read snapshot bytes");
            const auto before = file.readAll(); file.close();
            const auto repeated = cli.run({"snapshot", firstId, "--context", " changed ", "--namespace", "zeta", "--namespace", "alpha", "--namespace", "alpha"});
            require(repeated["sessions"].toArray().size() == 2 && repeated["activeSessionId"] == firstId,
                "SES-003: repeated changed configuration duplicated or retargeted sessions");
            require(file.open(QIODevice::ReadOnly), "Cannot read retained snapshot bytes");
            return require(file.readAll() == before, "Repeated equivalent replacement rewrote history") ? 0 : 1;
        }
        if (scenario == "unrelated_equal_config") {
            const auto other = cli.create("Other");
            cli.snapshot(other["id"].toString(), "changed");
            return require(cli.snapshot(firstId, "changed")["displayName"] == "Production 2", "Unrelated sequence suppressed required replacement") ? 0 : 1;
        }
        if (scenario == "usage_window") {
            cli.run({"open", firstId}); cli.run({"close", firstId});
            QJsonArray history;
            for (int n = 0; n < 100; ++n) history.append(QDateTime::currentDateTimeUtc().addDays(-31).toString(Qt::ISODateWithMs));
            cli.row(0, "usageAt", history);
            const auto recent = cli.create("Recent"); cli.run({"open", recent["id"].toString()});
            return require(cli.run({"selection"})["sessions"].toArray()[0].toObject()["id"] == recent["id"], "SES-009: old usage must not outrank recent use") ? 0 : 1;
        }
        const auto now = QDateTime::currentDateTimeUtc();
        if (scenario == "old_schema") {
            cli.edit([](QJsonObject root) { root["version"] = 1; return root; });
            return cli.fails({"create", "--context", "new"}, "UnsupportedVersion") ? 0 : 1;
        }
        if (scenario == "selection_bad_store") {
            cli.row(0, "sequenceId", "invalid");
            return cli.fails({"selection"}, "InvalidData") ? 0 : 1;
        }
        if (scenario == "rename_noop") {
            const auto second = cli.snapshot(firstId, "changed");
            cli.run({"rename", second["id"].toString(), "Production 2"});
            return require(cli.snapshot(second["id"].toString(), "changed-again")["displayName"] == "Production 3", "Unchanged title reset sequence") ? 0 : 1;
        }
        if (scenario == "history_copy") {
            cli.run({"open", firstId});
            const auto second = cli.snapshot(firstId, "changed");
            const auto rows = cli.run({"list"})["sessions"].toArray();
            require(rows[0].toObject()["useCount"] == "1", "Original history lost");
            require(second["usageAt"].toArray().isEmpty() && second["useCount"] == "0", "Replacement inherited usage");
            return require(cli.run({"list"})["activeSessionId"] == firstId, "Snapshot retargeted active tab") ? 0 : 1;
        }
        if (scenario == "independent_overflow") {
            cli.row(0, "ordinal", "18446744073709551615");
            return require(cli.create("Independent")["displayName"] == "Independent", "Unrelated sequence exhausted new root") ? 0 : 1;
        }
        if (scenario == "occupied_max") {
            cli.row(0, "ordinal", "18446744073709551614");
            cli.create("Production 18446744073709551615");
            return cli.fails({"snapshot", firstId, "--context", "changed"}, "Conflict") ? 0 : 1;
        }
        if (scenario == "invalid_reference") {
            const auto result = podlord::SessionStore(cli.profile).selection(QDateTime{});
            return require(std::holds_alternative<podlord::Failure>(result)
                && std::get<podlord::Failure>(result).code == podlord::StoreError::InvalidInput, "Invalid reference clock accepted") ? 0 : 1;
        }
        if (scenario == "cutoff_timezone") {
            const QTimeZone zone("Europe/Berlin");
            require(zone.isValid(), "Reference timezone unavailable");
            const auto reference = QDateTime::fromString("2026-04-01T10:00:00.000Z", Qt::ISODateWithMs);
            const auto second = cli.create("Recent");
            const auto inside = reference.addDays(-30).addMSecs(1).toString(Qt::ISODateWithMs);
            cli.row(0, "usageAt", QJsonArray{inside, inside});
            cli.row(1, "usageAt", QJsonArray{reference.addDays(-29).toString(Qt::ISODateWithMs)});
            for (int index = 0; index < 2; ++index) cli.row(index, "createdAt", reference.addDays(-60).toString(Qt::ISODateWithMs));
            const auto result = podlord::SessionStore(cli.profile).selection(reference.toTimeZone(zone));
            require(std::holds_alternative<podlord::SessionCatalog>(result), "Timezone selection failed");
            return require(std::get<podlord::SessionCatalog>(result).sessions[0].id.toString(QUuid::WithoutBraces) == firstId,
                "Timezone/DST changed the 30-day UTC window") ? 0 : 1;
        }
        const auto second = cli.create("Recent"); const auto secondId = second["id"].toString();
        if (scenario == "rename_conflict") return cli.fails({"rename", firstId, "Recent"}, "Conflict") ? 0 : 1;
        if (scenario == "sequence_mismatch") {
            cli.row(1, "sequenceId", first["sequenceId"]);
            return cli.fails({"create", "--context", "new"}, "InvalidData") ? 0 : 1;
        }
        const auto timestamp = [&](int days) { return now.addDays(days).toString(Qt::ISODateWithMs); };
        if (scenario == "recent_tie") {
            cli.row(0, "usageAt", QJsonArray{timestamp(-2)});
            cli.row(1, "usageAt", QJsonArray{timestamp(-1)});
        } else if (scenario == "used_before_unused") {
            cli.row(1, "usageAt", QJsonArray{timestamp(-31)});
        } else if (scenario == "never_used_newest") {
            cli.row(0, "createdAt", timestamp(-2)); cli.row(1, "createdAt", timestamp(-1));
        } else if (scenario == "id_tie" || scenario == "unused_id_tie") {
            if (scenario == "id_tie") {
                cli.row(0, "usageAt", QJsonArray{timestamp(-1)});
                cli.row(1, "usageAt", QJsonArray{timestamp(-1)});
            } else {
                cli.row(0, "createdAt", timestamp(-1)); cli.row(1, "createdAt", timestamp(-1));
            }
            const auto selected = cli.run({"selection"})["sessions"].toArray();
            return require(selected[0].toObject()["id"].toString() == std::min(firstId, secondId), "SES-014: ID tie ordering differs") ? 0 : 1;
        } else if (scenario == "stable_order") {
            cli.run({"open", secondId}); cli.run({"selection"}); cli.run({"open", firstId});
            const auto rows = cli.run({"list"})["sessions"].toArray();
            return require(rows[0].toObject()["id"] == firstId && rows[1].toObject()["id"] == secondId, "SES-015/016: catalog/tab order changed") ? 0 : 1;
        } else if (scenario == "cutoff" || scenario == "cutoff_excluded") {
            cli.row(0, "usageAt", scenario == "cutoff" ? QJsonArray{timestamp(-30), timestamp(-30)}
                : QJsonArray{now.addDays(-30).addMSecs(-1).toString(Qt::ISODateWithMs), now.addMSecs(1).toString(Qt::ISODateWithMs)});
            cli.row(1, "usageAt", QJsonArray{timestamp(-29)});
            const auto result = podlord::SessionStore(cli.profile).selection(now);
            require(std::holds_alternative<podlord::SessionCatalog>(result), "Clock-boundary selection failed");
            return require(std::get<podlord::SessionCatalog>(result).sessions[0].id.toString(QUuid::WithoutBraces)
                == (scenario == "cutoff" ? firstId : secondId), "30-day cutoff or future exclusion differs") ? 0 : 1;
        } else return 2;
        return require(cli.run({"selection"})["sessions"].toArray()[0].toObject()["id"] == secondId, "Selection ranking differs from SES-009/010/013") ? 0 : 1;
        return 2;
    } catch (const std::exception& error) { std::fprintf(stderr, "%s\n", error.what()); return 1; }
}
