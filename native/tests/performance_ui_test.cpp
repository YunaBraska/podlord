#include "workspace.h"
#include "graphics_configuration.h"
#include "ui_input.h"
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QTimer>
#include <QProcess>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDateTime>
#include <QTest>
#include <algorithm>
#include <cstdio>
#include <ctime>
#include <optional>

namespace {
bool waitFor(const std::function<bool()>& ready, int timeout = 15000) { return QTest::qWaitFor(ready, timeout); }
bool report(const QJsonObject& value) {
    auto record = value;
    record["at"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    const auto bytes = QJsonDocument(record).toJson(QJsonDocument::Compact);
    const bool written = std::fwrite(bytes.constData(), 1, bytes.size(), stdout) == size_t(bytes.size());
    return std::fputc('\n', stdout) != EOF && std::fflush(stdout) == 0 && written;
}
std::optional<qint64> residentBytes() {
    QProcess process;
    process.start("/bin/ps", {"-p", QString::number(QCoreApplication::applicationPid()), "-o", "rss="});
    if (!process.waitForFinished(2000) || process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) return {};
    bool valid = false;
    const auto kib = process.readAllStandardOutput().trimmed().toLongLong(&valid);
    return valid && kib > 0 ? std::optional<qint64>{kib * 1024} : std::nullopt;
}
bool run(const QString& config) {
    QElapsedTimer startup; startup.start();
    QTemporaryDir temporary;
    if (!temporary.isValid()) return false;
    podlord::Workspace workspace(temporary.filePath("profile"));
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("workspace", &workspace);
    engine.load(QUrl("qrc:/podlord/Main.qml"));
    if (engine.rootObjects().isEmpty()) return false;
    auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
    if (!window || !waitFor([&] { return window->isVisible() && !workspace.busy(); })) return false;
    auto* settings = podlord::test::visibleItem(window->contentItem(), "settingsWorkspaceButton");
    if (!settings || !podlord::test::scrollIntoView(window, settings)) return false;
    if (!report({{"type", "construction"}, {"milliseconds", startup.nsecsElapsed() / 1000000.0},
                 {"scope", "Workspace/QML construction, not installed-process startup"}})) return false;
    const auto frame = [&](const std::function<bool()>& action, const std::function<bool()>& visible) -> std::optional<double> {
        if (!waitFor([&] { return !workspace.busy(); })) return {};
        window->raise(); window->requestActivate();
        if (!waitFor([&] { return window->isExposed() && window->isActive(); })) {
            report({{"type", "failure"}, {"reason", "foreground desktop unavailable"}, {"visible", window->isVisible()},
                    {"exposed", window->isExposed()}, {"active", window->isActive()}});
            return {};
        }
        std::optional<double> rendered;
        QElapsedTimer elapsed;
        QObject frameObserver;
        QObject::connect(window, &QQuickWindow::frameSwapped, &frameObserver,
            [&] { if (!rendered && visible()) rendered = elapsed.nsecsElapsed() / 1000000.0; }, Qt::QueuedConnection);
        elapsed.start();
        if (!action()) {
            report({{"type", "failure"}, {"reason", "public action rejected"}, {"error", workspace.error()},
                    {"busy", workspace.busy()}, {"loading", workspace.loading()}});
            return {};
        }
        window->update();
        if (!waitFor([&] { return rendered.has_value(); }, 2000)) {
            report({{"type", "failure"}, {"reason", "rendered frame unavailable"}, {"visible", window->isVisible()},
                    {"exposed", window->isExposed()}, {"active", window->isActive()}});
            return {};
        }
        if (!window->isExposed() || !window->isActive()) {
            report({{"type", "failure"}, {"reason", "foreground lost during measured frame"},
                    {"exposed", window->isExposed()}, {"active", window->isActive()}});
            return {};
        }
        return rendered;
    };
    if (!workspace.importFile(config) || !waitFor([&] { return !workspace.busy() && workspace.contexts().size() == 3; })) return false;
    QStringList sessions, clusters;
    QList<int> counts;
    int total = 0;
    const auto path = QStringLiteral("/api/v1/namespaces/benchmark/pods/load-0");
    for (const auto& entry : workspace.contexts()) {
        if (!workspace.openContext(entry.toMap()["id"].toString()) || !waitFor([&] { return !workspace.busy() && !workspace.loading() && workspace.totalResourceCount() >= 1666; })) return false;
        sessions.append(workspace.currentSession());
        clusters.append(entry.toMap()["cluster"].toString());
        counts.append(workspace.totalResourceCount());
        total += counts.last();
        if (!workspace.setWorkspacePage("resources") || !workspace.inspectPath(path) || !workspace.setInspectorPage("logs")
            || !waitFor([&] { return workspace.logRows()->rowCount() > 0; })) return false;
        if (!frame([] { return true; }, [&] { return workspace.logsVisible(); })) return false;
    }
    if (total != 5000 || sessions.size() != 3 || workspace.logLimitMb() != 5) return false;
    if (!frame([&] { return workspace.activate(sessions.first()); }, [&] { return !workspace.busy() && workspace.currentSession() == sessions.first(); })) return false;
    if (!report({{"type", "profile"}, {"sessions", 3}, {"resources", total}, {"pods", 1500},
                 {"configMaps", 2334}, {"secrets", 1166}, {"logLimitMb", workspace.logLimitMb()},
                 {"logRows", workspace.logRows()->rowCount()}, {"qt", qVersion()},
                 {"boundary", "external local HTTP API; real Workspace, caches, QML and render loop"},
                 {"protocol", "5 warmups, 30 retained action-to-frame samples per class; 10 steady batches; RSS tolerance 5 MB; 60 seconds nonsync idle"}})) return false;
    bool accepted = true;
    const auto benchmark = [&](const QString& name, double limit, const std::function<bool(int)>& action, const std::function<bool(int)>& visible) {
        if (!report({{"type", "interactionStart"}, {"name", name}})) return false;
        QList<double> samples;
        for (int i = 0; i < 35; ++i) {
            const auto elapsed = frame([&] { return action(i); }, [&] { return visible(i); });
            if (!elapsed) { report({{"type", "failure"}, {"interaction", name}, {"sample", i}, {"error", workspace.error()}}); return false; }
            if (i >= 5) samples.append(*elapsed);
        }
        std::sort(samples.begin(), samples.end());
        const bool passed = samples[28] <= limit && samples.last() <= 100;
        return report({{"type", "interaction"}, {"name", name}, {"samples", samples.size()},
                       {"p50Ms", samples[14]}, {"p95Ms", samples[28]}, {"maxMs", samples.last()},
                       {"p95LimitMs", limit}, {"passed", passed}}) && passed;
    };
    accepted = benchmark("filter", 100, [&](int i) { return workspace.filter(i % 2 ? "load-1" : ""); },
        [&](int i) { return workspace.filterText() == (i % 2 ? "load-1" : "") && workspace.resourceCount() > 0; }) && accepted;
    if (!workspace.filter("")) return false;
    accepted = benchmark("sort", 100, [&](int) { return workspace.sortColumn(0); }, [&] (int) { return workspace.resourceCount() == counts.first(); }) && accepted;
    accepted = benchmark("cached-session-tab", 50, [&](int i) { return workspace.activate(sessions[i % 3]); },
        [&](int i) {
            const auto* model = workspace.table();
            return workspace.currentSession() == sessions[i % 3] && workspace.totalResourceCount() == counts[i % 3]
                && model->rowCount() > 0
                && model->data(model->index(0, 6)).toString() == clusters[i % 3];
        }) && accepted;
    if (!frame([&] { return workspace.activate(sessions.first()); }, [&] { return !workspace.busy() && workspace.currentSession() == sessions.first(); })) return false;
    accepted = benchmark("cached-inspector", 50, [&](int) { return workspace.closeInspector() && workspace.inspectPath(path); },
        [&](int) { return workspace.inspectorPath() == path && workspace.inspectorName() == "load-0" && !workspace.overviewFields().isEmpty(); }) && accepted;
    if (!workspace.setInspectorPage("logs") || !waitFor([&] { return workspace.logsVisible() && workspace.logRows()->rowCount() > 0; })) return false;
    QList<qint64> resident;
    for (int batch = 0; batch < 12; ++batch) {
        for (int i = 0; i < 6; ++i) {
            const auto index = i % 3;
            if (!frame([&] { return workspace.activate(sessions[index]); }, [&] { return !workspace.busy() && workspace.currentSession() == sessions[index]; })
                || !frame([&] { return workspace.filter(i % 2 ? "load-2" : "") && workspace.sortColumn(0); }, [] { return true; })) return false;
        }
        const auto rss = residentBytes();
        if (!rss) return false;
        if (!report({{"type", "resident"}, {"batch", batch}, {"bytes", double(*rss)}, {"warmup", batch < 2}})) return false;
        if (batch >= 2) resident.append(*rss);
    }
    const auto maximum = *std::max_element(resident.begin(), resident.end());
    const auto baseline = *std::min_element(resident.begin(), resident.begin() + 5);
    const auto final = *std::max_element(resident.end() - 5, resident.end());
    const bool memoryPassed = maximum <= 250000000 && final <= baseline + 5000000;
    if (!report({{"type", "memory"}, {"maxResidentBytes", double(maximum)}, {"warmGrowthBytes", double(final - baseline)}, {"passed", memoryPassed}})) return false;
    accepted = memoryPassed && accepted;
    if (!workspace.filter("") || !workspace.pauseLogs(true)) return false;
    QEventLoop idle;
    QTimer quiet, timeout, foregroundGuard;
    quiet.setSingleShot(true); timeout.setSingleShot(true);
    QObject::connect(&quiet, &QTimer::timeout, &idle, &QEventLoop::quit);
    bool timedOut = false;
    bool foregroundLost = !window->isExposed() || !window->isActive();
    QObject::connect(&foregroundGuard, &QTimer::timeout, &idle, [&] {
        if (window->isExposed() && window->isActive()) return;
        foregroundLost = true; idle.quit();
    });
    QObject::connect(&timeout, &QTimer::timeout, &idle, [&] { timedOut = true; idle.quit(); });
    QElapsedTimer period;
    double cpuSeconds = 0;
    qint64 idleMilliseconds = 0;
    int remaining = 60000;
    bool synchronizing = workspace.loading() || workspace.busy();
    auto cpu = std::clock(); period.start();
    if (cpu == std::clock_t(-1)) return false;
    const auto accumulate = [&] {
        if (!synchronizing) {
            cpuSeconds += double(std::clock() - cpu) / CLOCKS_PER_SEC;
            idleMilliseconds += period.elapsed();
        }
    };
    const auto sync = QObject::connect(&workspace, &podlord::Workspace::changed, &idle, [&] {
        const bool next = workspace.loading() || workspace.busy();
        if (next == synchronizing) return;
        accumulate();
        if (next) { remaining = quiet.remainingTime(); quiet.stop(); }
        else quiet.start(std::max(1, remaining));
        synchronizing = next; cpu = std::clock(); period.restart();
    });
    if (!synchronizing) quiet.start(remaining);
    if (!report({{"type", "phase"}, {"name", "idle"}, {"logPaused", true}, {"requiredNonsyncMilliseconds", 60000}})) return false;
    timeout.start(120000); foregroundGuard.start(1000); idle.exec(); accumulate(); QObject::disconnect(sync);
    const double percent = idleMilliseconds > 0 ? cpuSeconds / (idleMilliseconds / 1000.0) * 100 : 100;
    const bool idlePassed = !timedOut && !foregroundLost && idleMilliseconds >= 59990 && percent <= 2;
    if (!report({{"type", "idle"}, {"milliseconds", double(idleMilliseconds)}, {"cpuPercentOneCore", percent}, {"foregroundLost", foregroundLost}, {"passed", idlePassed}})) return false;
    accepted = idlePassed && accepted;
    if (qEnvironmentVariableIsSet("PODLORD_PERFORMANCE_INSPECT")) {
        if (!report({{"type", "phase"}, {"name", "diagnostic-hold"}, {"milliseconds", 30000}, {"scope", "After measured actions and idle; attach process diagnostics here"}})) return false;
        QTest::qWait(30000);
    }
    return report({{"type", "result"}, {"passed", accepted}, {"scope", "runtime preflight, not installed-app startup, UI-work-per-frame, license or signing clearance"}}) && accepted;
}
}
int main(int argc, char** argv) {
    if (!podlord::configureGraphics()) { std::fputs("Cannot configure native graphics.\n", stderr); return 1; }
    QGuiApplication app(argc, argv);
    if (argc != 2) return 2;
    if (QGuiApplication::platformName() != "cocoa") {
        std::fprintf(stderr, "This performance protocol requires the macOS Cocoa renderer.\n");
        return 2;
    }
    const bool passed = run(QString::fromLocal8Bit(argv[1]));
    if (!passed) std::fprintf(stderr, "Native performance protocol failed.\n");
    return passed ? 0 : 1;
}
