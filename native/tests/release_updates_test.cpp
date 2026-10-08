#include "release_updates.h"
#include "workspace.h"
#include "browser_boundary.h"
#include "ui_input.h"
#include <QFile>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <cstdio>

namespace {
// Only GitHub HTTP and the browser handoff are external test boundaries.
class ReleaseServer final : public QTcpServer {
public:
    QList<QByteArray> requests;
    QList<QPointer<QTcpSocket>> waiting;
    QByteArray body, extraHeaders;
    int status = 200;
    bool hold = false;
    ReleaseServer() {
        connect(this, &QTcpServer::newConnection, this, [this] {
            while (hasPendingConnections()) {
                auto* socket = nextPendingConnection();
                connect(socket, &QTcpSocket::readyRead, this, [this, socket] {
                    auto bytes = socket->property("requestBytes").toByteArray() + socket->readAll();
                    socket->setProperty("requestBytes", bytes);
                    if (socket->property("received").toBool() || !bytes.contains("\r\n\r\n")) return;
                    socket->setProperty("received", true); requests.append(bytes);
                    if (hold) waiting.append(socket); else send(socket);
                });
            }
        });
        listen(QHostAddress::LocalHost);
    }
    QUrl endpoint() const { return QUrl(QString("http://127.0.0.1:%1/latest").arg(serverPort())); }
    void send(QTcpSocket* socket) {
        socket->write("HTTP/1.1 " + QByteArray::number(status) + " Test\r\nContent-Type: application/json\r\nConnection: close\r\n"
            + extraHeaders + "Content-Length: " + QByteArray::number(body.size()) + "\r\n\r\n" + body);
        socket->disconnectFromHost();
    }
    void finish() { for (const auto& socket : waiting) if (socket) send(socket); waiting.clear(); }
};
QJsonObject release(const QString& tag = "v0.2.0") {
    const auto name = podlord::ReleaseUpdates::compatibleAssetName();
    return {{"tag_name", tag}, {"draft", false}, {"prerelease", false},
        {"html_url", "https://github.com/YunaBraska/podlord/releases/tag/" + tag},
        {"assets", QJsonArray{QJsonObject{{"name", name}, {"state", "uploaded"}, {"size", 32000000},
            {"browser_download_url", "https://github.com/YunaBraska/podlord/releases/download/" + tag + '/' + name}}}}};
}
bool write(const QString& path, const QByteArray& bytes) {
    QFile file(path); return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}
QByteArray read(const QString& path) {
    QFile file(path); return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
}
bool run(const QString& scenario) {
    QTemporaryDir temporary;
    ReleaseServer server;
    if (!temporary.isValid() || !server.isListening()) return false;
    auto now = QDateTime::fromString("2026-10-08T20:00:00.000Z", Qt::ISODateWithMs);
    const std::function<QDateTime()> clock = scenario == "empty_clock" ? std::function<QDateTime()>{} : std::function<QDateTime()>{[&now] { return now; }};
    if (scenario == "invalid_clock") now = {};
    if (scenario == "invalid_current_version") QCoreApplication::setApplicationVersion("unversioned");
    const auto profile = temporary.filePath("profile");
    const auto cache = profile + "/release-check.json";
    auto metadata = release();
    if (scenario == "legacy_asset") {
        auto asset = metadata["assets"].toArray().first().toObject();
        asset["name"] = "podlord-macos-arm64.zip";
        asset["browser_download_url"] = "https://github.com/YunaBraska/podlord/releases/download/v0.2.0/podlord-macos-arm64.zip";
        metadata["assets"] = QJsonArray{asset};
    } else if (scenario == "no_asset") metadata["assets"] = QJsonArray{};
    else if (scenario == "older") metadata = release("v0.0.9");
    else if (scenario == "same") metadata = release("v0.1.0");
    else if (scenario == "draft") metadata["draft"] = true;
    else if (scenario == "prerelease") metadata["prerelease"] = true;
    else if (scenario == "missing_draft") metadata.remove("draft");
    else if (scenario == "bad_version") metadata["tag_name"] = "v0.2.0/../evil";
    else if (scenario == "overflow_version") metadata["tag_name"] = "v999999999999999999999.2.0";
    else if (scenario == "bad_release_url") metadata["html_url"] = "https://example.org/credentials";
    else if (scenario.startsWith("asset_")) {
        auto asset = metadata["assets"].toArray().first().toObject();
        if (scenario == "asset_url") asset["browser_download_url"] = "https://example.org/package.zip";
        if (scenario == "asset_state") asset["state"] = "new";
        if (scenario == "asset_size_zero") asset["size"] = 0;
        if (scenario == "asset_size_fraction") asset["size"] = 1.5;
        if (scenario == "asset_size_large") asset["size"] = 50000001;
        metadata["assets"] = scenario == "asset_duplicate" ? QJsonArray{asset, asset} : QJsonArray{asset};
    }
    server.body = QJsonDocument(metadata).toJson(QJsonDocument::Compact);
    if (scenario == "malformed") server.body = "{broken";
    if (scenario == "non_object") server.body = "[]";
    if (scenario == "oversized") server.body = QByteArray(1024*1024+1, ' ');
    if (scenario.startsWith("http_")) server.status = scenario.mid(5).toInt();
    if (scenario == "redirect") { server.status = 302; server.extraHeaders = "Location: " + server.endpoint().toEncoded() + "\r\n"; }
    if (scenario == "disabled") qputenv("PODLORD_DISABLE_UPDATE_CHECK", "1");
    else qunsetenv("PODLORD_DISABLE_UPDATE_CHECK");
    if (scenario == "invalid_cache" || scenario == "cache_symlink" || scenario == "lock_directory") {
        if (!QDir().mkpath(profile)) return false;
        if (scenario == "invalid_cache" && !write(cache, "{\"version\":99,\"data\":\"retained\"}")) return false;
        if (scenario == "cache_symlink" && (!write(temporary.filePath("target"), "retained") || !QFile::link(temporary.filePath("target"), cache))) return false;
        if (scenario == "lock_directory" && !QDir().mkpath(cache + ".lock")) return false;
    }
    if (scenario.startsWith("ui_")) {
        BrowserBoundary browser;
        podlord::Workspace workspace(profile, nullptr, clock, server.endpoint());
        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty("workspace", &workspace);
        engine.load(QUrl("qrc:/podlord/Main.qml"));
        if (engine.rootObjects().isEmpty()) return false;
        auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
        if (!window || !podlord::test::clickVisible(window, "settingsWorkspaceButton")
            || !podlord::test::selectSettingsSection(window, "about")) return false;
        if (scenario == "ui_narrow") window->resize(640, 700);
        if (scenario == "ui_idle") return server.requests.isEmpty()
            && podlord::test::visibleItem(window->contentItem(), "aboutCheckUpdates")
            && !podlord::test::visibleItem(window->contentItem(), "aboutDownloadUpdate");
        server.hold = scenario == "ui_busy";
        if (!podlord::test::clickVisible(window, "aboutCheckUpdates")
            || !QTest::qWaitFor([&] { return server.requests.size() == 1; })) return false;
        if (scenario == "ui_busy") {
            auto* button = podlord::test::visibleItem(window->contentItem(), "aboutCheckUpdates");
            if (!button || button->isEnabled() || !workspace.releaseUpdates()->state()["busy"].toBool()) return false;
            server.finish();
        }
        if (!QTest::qWaitFor([&] { return !workspace.releaseUpdates()->state()["busy"].toBool(); })
            || !workspace.releaseUpdates()->state()["available"].toBool()
            || !podlord::test::clickVisible(window, "aboutDownloadUpdate")) return false;
        if (browser.urls != QList<QUrl>{QUrl(workspace.releaseUpdates()->state()["downloadUrl"].toString())}) return false;
        if (!podlord::test::clickVisible(window, "aboutReleasePage")) return false;
        return browser.urls.size() == 2 && server.requests.size() == 1
            && browser.urls.last() == QUrl(workspace.releaseUpdates()->state()["releaseUrl"].toString());
    }
    const auto endpoint = scenario == "bad_endpoint" ? QUrl("https://example.org/latest") : server.endpoint();
    podlord::ReleaseUpdates updates(profile, nullptr, clock, endpoint);
    const auto state = [&] { return updates.state(); };
    const auto finish = [&] { return QTest::qWaitFor([&] { return !state()["busy"].toBool(); }, 15000); };
    if (scenario == "idle") return server.requests.isEmpty() && !QFileInfo::exists(profile)
        && !state()["available"].toBool() && state()["error"].toString().isEmpty();
    if (scenario == "disabled") return !updates.startAutomaticChecks() && !updates.checkNow()
        && server.requests.isEmpty() && !QFileInfo::exists(profile) && !state()["enabled"].toBool();
    if (scenario == "invalid_cache" || scenario == "cache_symlink" || scenario == "lock_directory" || scenario == "bad_endpoint"
        || scenario == "empty_clock" || scenario == "invalid_clock" || scenario == "invalid_current_version")
        return !updates.checkNow() && server.requests.isEmpty() && !state()["error"].toString().isEmpty()
            && (scenario != "invalid_cache" || read(cache) == "{\"version\":99,\"data\":\"retained\"}")
            && (scenario != "cache_symlink" || read(temporary.filePath("target")) == "retained");
    if (scenario == "timeout") server.hold = true;
    if (scenario == "coalesce" || scenario == "owners" || scenario == "process_owners" || scenario == "handoff_busy") server.hold = true;
    if (!updates.startAutomaticChecks() || !QTest::qWaitFor([&] { return server.requests.size() == 1; })) return false;
    const auto headers = server.requests.first().toLower();
    if (headers.contains("authorization:") || headers.contains("cookie:")
        || !headers.contains("x-github-api-version: 2026-03-10")) return false;
    if (scenario == "coalesce" || scenario == "owners" || scenario == "process_owners" || scenario == "handoff_busy") {
        if (updates.checkNow() || updates.checkIfDue() || updates.startAutomaticChecks()) return false;
        if (scenario == "handoff_busy" && updates.openDownload()) return false;
        if (scenario != "coalesce") {
            podlord::ReleaseUpdates second(profile, nullptr, clock, server.endpoint());
            if (second.checkNow() || second.state()["error"].toString().isEmpty()) return false;
        }
        if (scenario == "process_owners") {
            QProcess child;
            child.start(QCoreApplication::applicationFilePath(), {"locked", profile, server.endpoint().toString()});
            if (!child.waitForFinished(5000) || child.exitStatus() != QProcess::NormalExit || child.exitCode() != 0) return false;
        }
        server.finish();
    }
    if (!finish() || state()["lastCheckedAt"].toString().isEmpty() || server.requests.size() != 1) return false;
    const bool rejected = scenario == "draft" || scenario == "prerelease" || scenario == "missing_draft"
        || scenario == "bad_version" || scenario == "overflow_version" || scenario == "bad_release_url"
        || scenario.startsWith("asset_") || scenario == "malformed" || scenario == "non_object" || scenario == "oversized"
        || scenario == "redirect" || scenario == "timeout" || (scenario.startsWith("http_") && server.status != 404);
    if (rejected) return !state()["available"].toBool() && !state()["error"].toString().isEmpty()
        && !updates.checkIfDue() && server.requests.size() == 1;
    if (scenario == "http_404") return !state()["available"].toBool() && state()["error"].toString().isEmpty() && !updates.checkIfDue();
    if (scenario == "legacy_asset" || scenario == "no_asset") return state()["newer"].toBool()
        && !state()["available"].toBool() && state()["downloadUrl"].toString().isEmpty() && !updates.openDownload();
    if (scenario == "older" || scenario == "same") return !state()["newer"].toBool() && !state()["available"].toBool() && !updates.openDownload();
    if (!state()["available"].toBool() || !state()["error"].toString().isEmpty() || !QFileInfo::exists(cache)) return false;
    if (scenario == "handoff") {
        BrowserBoundary browser;
        return updates.openDownload() && updates.openRelease() && browser.urls.size() == 2
            && browser.urls.first() == QUrl(state()["downloadUrl"].toString()) && server.requests.size() == 1;
    }
    if (scenario == "weekly") {
        now = now.addDays(6);
        if (updates.checkIfDue() || server.requests.size() != 1) return false;
        now = now.addDays(1);
        return updates.checkIfDue() && finish() && server.requests.size() == 2;
    }
    if (scenario == "scheduled_weekly") {
        now = now.addDays(7).addMSecs(-100);
        if (updates.checkIfDue() || server.requests.size() != 1) return false;
        QTimer::singleShot(0, &updates, [&now] { now = now.addMSecs(100); });
        return QTest::qWaitFor([&] { return server.requests.size() == 2; }) && finish() && !updates.checkIfDue();
    }
    if (scenario == "manual") return updates.checkNow() && finish() && server.requests.size() == 2;
    if (scenario == "future") { now = now.addDays(-1); return updates.checkIfDue() && finish() && server.requests.size() == 2; }
    if (scenario == "upgrade") {
        QCoreApplication::setApplicationVersion("0.1.1");
        podlord::ReleaseUpdates newerApp(profile, nullptr, clock, server.endpoint());
        return newerApp.checkIfDue() && QTest::qWaitFor([&] { return !newerApp.state()["busy"].toBool(); })
            && server.requests.size() == 2;
    }
    if (scenario == "preserve_failure") {
        const auto url = state()["downloadUrl"];
        server.status = 429; server.body = "private upstream details";
        if (!updates.checkNow() || !finish() || !state()["available"].toBool() || state()["downloadUrl"] != url
            || state()["error"].toString().contains("private") || updates.checkIfDue()) return false;
        podlord::ReleaseUpdates restarted(profile, nullptr, clock, server.endpoint());
        return restarted.state()["available"].toBool() && !restarted.checkIfDue() && server.requests.size() == 2;
    }
    if (scenario == "permissions") return !(QFile::permissions(cache)
        & (QFile::ReadGroup | QFile::WriteGroup | QFile::ReadOther | QFile::WriteOther));
    if (scenario == "restart") {
        podlord::ReleaseUpdates restarted(profile, nullptr, clock, server.endpoint());
        return restarted.state()["available"].toBool() && !restarted.startAutomaticChecks() && server.requests.size() == 1;
    }
    return scenario == "success" || scenario == "coalesce" || scenario == "owners" || scenario == "process_owners" || scenario == "handoff_busy";
}
}
int main(int argc, char** argv) {
    QGuiApplication app(argc, argv);
    QCoreApplication::setApplicationVersion("0.1.0");
    Q_INIT_RESOURCE(workspace_ui);
    if (argc == 4 && QString::fromLocal8Bit(argv[1]) == "locked") {
        podlord::ReleaseUpdates updates(QString::fromLocal8Bit(argv[2]), nullptr, QDateTime::currentDateTimeUtc, QUrl(QString::fromLocal8Bit(argv[3])));
        return !updates.checkNow() && !updates.state()["error"].toString().isEmpty() ? 0 : 1;
    }
    const bool passed = argc == 2 && run(QString::fromLocal8Bit(argv[1]));
    if (!passed) std::fprintf(stderr, "Release-update public-boundary scenario failed: %s\n", argc == 2 ? argv[1] : "missing");
    return passed ? 0 : 1;
}
