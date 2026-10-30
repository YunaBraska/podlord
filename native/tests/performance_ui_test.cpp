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
#include <QThread>
#include <QSGRendererInterface>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <cstdio>
#include <ctime>
#include <optional>

namespace {
struct FrameTiming final { double renderedMs, actionMs; quint64 token; };
struct CompletedFrame final { qint64 at; QList<double> processingMs; };
struct FrameProbeState final {
    std::atomic<quint64> requested{0};
    std::atomic<quint64> ready{0};
    std::atomic<qint64> guiFrameCpu{-1};
    qint64 lastGuiCpu = -1;
    qint64 renderStart = -1, synchronizedGuiCpu = -1;
    quint64 renderToken = 0;
    bool synchronized = false;
    bool readyFrame = false;
    std::mutex mutex;
    QMap<quint64, CompletedFrame> completed;
};
qint64 monotonicNs() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
bool waitFor(const std::function<bool()>& ready, int timeout = 15000) { return QTest::qWaitFor(ready, timeout); }
bool report(const QJsonObject& value) {
    auto record = value;
    if (record.value("type")=="failure") {
        const auto* focus=QGuiApplication::focusWindow();
        record["applicationState"]=int(QGuiApplication::applicationState());
        record["ownedFocusWindowType"]=focus ? int(focus->type()) : -1;
        record["ownedFocusWindowHasTransientParent"]=focus && focus->transientParent();
    }
    record["at"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    const bool diagnostic = QCoreApplication::arguments().contains("--background-metal-diagnostic");
    record["diagnosticOnly"] = diagnostic;
    record["renderingBoundary"] = diagnostic ? "background-cocoa-metal-diagnostic"
        : QGuiApplication::platformName() == "offscreen" ? "headless-software" : "foreground-cocoa";
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
bool run(const QString& config, podlord::Workspace& workspace, QQuickWindow* window, QElapsedTimer& startup, bool backgroundMetal) {
    const bool desktop = QGuiApplication::platformName() == "cocoa";
    if (!window) return false;
    const auto threadCpuNs = []() -> qint64 {
#ifdef CLOCK_THREAD_CPUTIME_ID
        timespec value{};
        if (clock_gettime(CLOCK_THREAD_CPUTIME_ID, &value) == 0)
            return qint64(value.tv_sec) * 1000000000 + value.tv_nsec;
#endif
        return -1;
    };
    const auto probe = std::make_shared<FrameProbeState>();
    probe->lastGuiCpu = threadCpuNs();
    QObject frameProbe;
    bool activationObserved = backgroundMetal && (window->isActive() || QGuiApplication::applicationState() == Qt::ApplicationActive);
    QObject::connect(qGuiApp, &QGuiApplication::applicationStateChanged, &frameProbe, [&](Qt::ApplicationState state) {
        if (backgroundMetal && state == Qt::ApplicationActive) activationObserved = true;
    });
    QObject::connect(window, &QWindow::activeChanged, &frameProbe, [&] {
        if (backgroundMetal && window->isActive()) activationObserved = true;
    });
    const auto validDesktopState = [&] {
        return window->isExposed() && (backgroundMetal ? !activationObserved && !window->isActive() : window->isActive());
    };
    auto* guiThread = QThread::currentThread();
    QObject::connect(window, &QQuickWindow::afterAnimating, &frameProbe, [probe, threadCpuNs] {
        const auto current = threadCpuNs();
        probe->guiFrameCpu.store(current >= 0 && probe->lastGuiCpu >= 0 ? current - probe->lastGuiCpu : -1);
        probe->lastGuiCpu = current;
    });
    QObject::connect(window, &QQuickWindow::beforeFrameBegin, &frameProbe, [probe, threadCpuNs] {
        probe->renderStart = threadCpuNs();
        probe->renderToken = probe->requested.load();
        probe->synchronizedGuiCpu = 0;
        probe->synchronized = false;
        probe->readyFrame = false;
    }, Qt::DirectConnection);
    QObject::connect(window, &QQuickWindow::beforeSynchronizing, &frameProbe, [probe] {
        probe->synchronizedGuiCpu = probe->guiFrameCpu.load();
        probe->renderToken = probe->requested.load();
        probe->readyFrame = probe->ready.load() == probe->renderToken;
        probe->synchronized = true;
    }, Qt::DirectConnection);
    QObject::connect(window, &QQuickWindow::afterFrameEnd, &frameProbe, [probe, threadCpuNs, guiThread] {
        const auto current = threadCpuNs();
        if (!probe->renderToken) return;
        const bool valid = current >= probe->renderStart && probe->renderStart >= 0 && probe->synchronizedGuiCpu >= 0
            && QThread::currentThread() != guiThread;
        const double milliseconds = valid ? (probe->synchronizedGuiCpu + current - probe->renderStart) / 1000000.0 : -1;
        const std::lock_guard lock(probe->mutex);
        if (!probe->completed.contains(probe->renderToken)) {
            if (!probe->synchronized) return;
            probe->completed.insert(probe->renderToken, {0, {}});
        }
        auto& completed = probe->completed[probe->renderToken];
        if (probe->readyFrame && !completed.at) completed.at = monotonicNs();
        completed.processingMs.append(milliseconds);
    }, Qt::DirectConnection);
    quint64 actionToken = 0;
    if (!window || !waitFor([&] { return window->isVisible() && !workspace.busy(); })) return false;
    if (!backgroundMetal) { window->raise(); window->requestActivate(); }
    if (desktop && (!waitFor([&] { return validDesktopState() || activationObserved; }) || !validDesktopState())) {
        report({{"type", "failure"}, {"reason", backgroundMetal ? "background Metal window unavailable or activated" : "foreground desktop unavailable"}, {"visible", window->isVisible()},
                {"exposed", window->isExposed()}, {"active", window->isActive()}});
        return false;
    }
    if (backgroundMetal && window->rendererInterface()->graphicsApi() != QSGRendererInterface::Metal) {
        report({{"type", "failure"}, {"reason", "background diagnosis requires Metal; no renderer fallback"}});
        return false;
    }
    if (backgroundMetal && !report({{"type", "window"}, {"visible", window->isVisible()}, {"exposed", window->isExposed()},
            {"active", window->isActive()}, {"activationObserved", activationObserved}, {"audioMuted", workspace.alerts()->muted()},
            {"scope", "Nonactivating Metal diagnosis only; never foreground release evidence"}})) return false;
    auto* settings = podlord::test::visibleItem(window->contentItem(), "settingsWorkspaceButton");
    if (!settings || !podlord::test::scrollIntoView(window, settings)) return false;
    if (!report({{"type", "construction"}, {"milliseconds", startup.nsecsElapsed() / 1000000.0},
                 {"windowWidth", window->width()}, {"windowHeight", window->height()}, {"devicePixelRatio", window->devicePixelRatio()},
                 {"scope", "Workspace/QML construction, not installed-process startup"}})) return false;
    const auto frame = [&](const std::function<bool()>& action, const std::function<bool()>& visible) -> std::optional<FrameTiming> {
        if (!waitFor([&] { return !workspace.busy(); })) return {};
        if (desktop && !validDesktopState()) {
            report({{"type", "failure"}, {"reason", backgroundMetal ? "background Metal window unavailable or activated" : "foreground desktop unavailable"}, {"visible", window->isVisible()},
                    {"exposed", window->isExposed()}, {"active", window->isActive()},
                    {"visibility", int(window->visibility())}, {"applicationState", int(QGuiApplication::applicationState())}});
            return {};
        }
        std::optional<double> rendered;
        QElapsedTimer elapsed;
        QObject frameObserver;
        QObject::connect(window, &QQuickWindow::frameSwapped, &frameObserver,
            [&] { if (!rendered && visible()) rendered = elapsed.nsecsElapsed() / 1000000.0; }, Qt::QueuedConnection);
        elapsed.start();
        const auto actionStart = monotonicNs();
        probe->requested.store(0);
        probe->ready.store(0);
        const bool acted = action();
        const double actionMs = elapsed.nsecsElapsed() / 1000000.0;
        if (!acted) {
            report({{"type", "failure"}, {"reason", "public action rejected"}, {"error", workspace.error()},
                    {"busy", workspace.busy()}, {"loading", workspace.loading()}});
            return {};
        }
        const auto token = ++actionToken;
        probe->requested.store(token);
        probe->ready.store(visible() ? token : 0);
        QObject::connect(window, &QQuickWindow::afterAnimating, &frameObserver,
            [&, token] { probe->ready.store(visible() ? token : 0); });
        const auto completedFrame = [&]() -> std::optional<CompletedFrame> {
            const std::lock_guard lock(probe->mutex);
            const auto found = probe->completed.constFind(token);
            return found == probe->completed.cend() ? std::optional<CompletedFrame>{} : *found;
        };
        window->update();
        if (!waitFor([&] { const auto completed = completedFrame();
                return desktop ? completed && completed->at > 0 && visible() : rendered.has_value(); }, 2000)) {
            report({{"type", "failure"}, {"reason", "rendered frame unavailable"}, {"visible", window->isVisible()},
                    {"exposed", window->isExposed()}, {"active", window->isActive()}});
            return {};
        }
        if (desktop && !validDesktopState()) {
            report({{"type", "failure"}, {"reason", backgroundMetal ? "background Metal window lost or activated during measured frame" : "foreground lost during measured frame"},
                    {"exposed", window->isExposed()}, {"active", window->isActive()}});
            return {};
        }
        if (desktop) {
            const auto at = completedFrame()->at;
            for (int drain = 0; drain < 2; ++drain) {
                const auto count = completedFrame()->processingMs.size();
                window->update();
                if (!waitFor([&] { const auto sample = completedFrame(); return sample && sample->processingMs.size() > count && visible(); }, 2000))
                    return {};
            }
            probe->requested.store(0);
            return FrameTiming{(at - actionStart) / 1000000.0, actionMs, token};
        }
        return FrameTiming{*rendered, actionMs, 0};
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
                 {"displayLimit", workspace.resourceLimit()}, {"displayedTableRows", workspace.displayedResourceCount()},
                 {"boundary", "external local HTTP API; real Workspace, caches, QML and render loop"},
                 {"protocol", "5 warmups, 30 retained action-to-frame samples per class; 10 steady batches; RSS tolerance 5 MB; 60 seconds nonsync idle"}})) return false;
    bool accepted = true;
    if (desktop) {
        QObject calibration;
        bool injected = false;
        bool calibrationReady = false;
        const auto token = actionToken + 1;
        QObject::connect(window, &QQuickWindow::afterFrameEnd, &calibration, [&] {
            if (injected) return;
            {
                const std::lock_guard lock(probe->mutex);
                if (!probe->completed.contains(token)) return;
            }
            injected = true;
            const auto start = threadCpuNs();
            if (start < 0) return;
            QElapsedTimer deadline; deadline.start();
            auto current = start;
            while (current >= 0 && current - start < 20000000 && deadline.elapsed() < 1000) current = threadCpuNs();
            calibrationReady = true;
            window->update();
        }, Qt::QueuedConnection);
        const auto sample = frame([] { return true; }, [&] { return calibrationReady; });
        QObject::disconnect(window, nullptr, &calibration, nullptr);
        if (!sample) return false;
        QList<double> observed;
        {
            const std::lock_guard lock(probe->mutex);
            observed = probe->completed.value(sample->token).processingMs;
        }
        const double maximum = observed.isEmpty() ? -1 : *std::max_element(observed.cbegin(), observed.cend());
        const bool detected = injected && observed.size() >= 3 && observed.first() >= 0
            && maximum >= 18 && maximum > observed.first() + 10;
        if (!report({{"type", "frameClockCalibration"}, {"knownFollowupCpuMs", 20}, {"frames", observed.size()},
                     {"firstFrameMs", observed.isEmpty() ? -1 : observed.first()}, {"maximumFrameMs", maximum},
                     {"passed", detected}, {"scope", "Calibration excluded from retained performance samples"}}) || !detected) return false;
    }
    const auto reportFrameWork = [&](const QString& name, const QList<quint64>& tokens) {
        if (!desktop) return true;
        QList<double> samples, maxima;
        {
            const std::lock_guard lock(probe->mutex);
            for (const auto token : tokens) {
                const auto found = probe->completed.constFind(token);
                if (found == probe->completed.cend() || found->processingMs.size() < 3) return false;
                samples.append(found->processingMs);
                maxima.append(*std::max_element(found->processingMs.cbegin(), found->processingMs.cend()));
            }
        }
        std::sort(samples.begin(), samples.end());
        std::sort(maxima.begin(), maxima.end());
        const int p95 = static_cast<int>((samples.size() * 95 + 99) / 100) - 1;
        const bool valid = tokens.size() == 30 && !samples.isEmpty() && samples.first() >= 0
            && window->rendererInterface()->graphicsApi() == QSGRendererInterface::Metal;
        const bool passed = valid && samples[p95] <= 8 && maxima[28] <= 8;
        return report({{"type", "frameWork"}, {"name", name}, {"samples", samples.size()},
                       {"distinctActions", tokens.size()}, {"p95Ms", valid ? samples[p95] : -1},
                       {"actionMaximumP95Ms", valid ? maxima[28] : -1}, {"maxMs", valid ? samples.last() : -1},
                       {"p95LimitMs", 8}, {"passed", passed},
                       {"method", "30 distinct post-action synchronization tokens; all frames through expected public state plus two synchronized drain frames; both frame and per-action maximum p95; GUI and render thread CPU excludes sleeping/GPU/display waiting"}}) && passed;
    };
    const auto benchmark = [&](const QString& name, double limit, const std::function<bool(int)>& action, const std::function<bool(int)>& visible) {
        if (!report({{"type", "interactionStart"}, {"name", name}})) return false;
        QList<double> samples, actions, afterAction;
        QList<quint64> processing;
        for (int i = 0; i < 35; ++i) {
            const auto elapsed = frame([&] { return action(i); }, [&] { return visible(i); });
            if (!elapsed) { report({{"type", "failure"}, {"interaction", name}, {"sample", i}, {"error", workspace.error()}}); return false; }
            if (i >= 5) {
                samples.append(elapsed->renderedMs); actions.append(elapsed->actionMs);
                afterAction.append(elapsed->renderedMs - elapsed->actionMs);
                processing.append(elapsed->token);
            }
        }
        std::sort(samples.begin(), samples.end());
        std::sort(actions.begin(), actions.end()); std::sort(afterAction.begin(), afterAction.end());
        const bool passed = samples[28] <= limit && samples.last() <= 100;
        return report({{"type", "interaction"}, {"name", name}, {"samples", samples.size()},
                       {"p50Ms", samples[14]}, {"p95Ms", samples[28]}, {"maxMs", samples.last()},
                       {"publicActionP95Ms", actions[28]}, {"postActionP95Ms", afterAction[28]},
                       {"p95LimitMs", limit}, {"passed", passed}}) && reportFrameWork(name, processing) && passed;
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
                && model->data(model->index(0, 6)).toString() == clusters[i % 3]
                && (i % 3 == 0 ? workspace.sortColumnIndex() == 0 && workspace.sortDirection() == "DESC"
                    && model->data(model->index(0, 0)).toString() == "load-99" : workspace.sortColumnIndex() == -1);
        }) && accepted;
    if (!frame([&] { return workspace.activate(sessions.first()); }, [&] { return !workspace.busy() && workspace.currentSession() == sessions.first(); })) return false;
    accepted = benchmark("cached-inspector", 50, [&](int) { return workspace.closeInspector() && workspace.inspectPath(path); },
        [&](int) { return workspace.inspectorPath() == path && workspace.inspectorName() == "load-0" && !workspace.overviewFields().isEmpty(); }) && accepted;
    if (!workspace.closeInspector()) return false;
    auto* scrollingTable = podlord::test::visibleItem(window->contentItem(), "resourceTable");
    if (!scrollingTable || !scrollingTable->isVisible()) return false;
    QList<quint64> scrollProcessing;
    for (int i = 0; i < 35; ++i) {
        const auto before = scrollingTable->property("contentY").toDouble();
        const int direction = before > 1 ? 120 : -120;
        const auto elapsed = frame([&] {
                const auto point = scrollingTable->mapToScene({scrollingTable->width()/2, scrollingTable->height()/2});
                QWheelEvent wheel(point, window->mapToGlobal(point.toPoint()), {}, {0, direction},
                                  Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
                wheel.setTimestamp(static_cast<ulong>(startup.msecsSinceReference() + startup.elapsed()));
                QCoreApplication::sendEvent(window, &wheel);
                return true;
            }, [&] { return qAbs(scrollingTable->property("contentY").toDouble() - before) > 0.5
                && !scrollingTable->property("moving").toBool(); });
        if (!elapsed) {
            report({{"type", "failure"}, {"interaction", "scroll"}, {"sample", i},
                    {"beforeContentY", before}, {"afterContentY", scrollingTable->property("contentY").toDouble()},
                    {"originY", scrollingTable->property("originY").toDouble()},
                    {"contentHeight", scrollingTable->property("contentHeight").toDouble()},
                    {"viewportHeight", scrollingTable->height()},
                    {"moving", scrollingTable->property("moving").toBool()}});
            return false;
        }
        if (i >= 5) scrollProcessing.append(elapsed->token);
    }
    accepted = reportFrameWork("scroll", scrollProcessing) && accepted;
    if (!workspace.inspectPath(path)) return false;
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
#ifdef Q_OS_MACOS
    constexpr qint64 residentLimit = 310000000;
#else
    constexpr qint64 residentLimit = 250000000;
#endif
    constexpr qint64 residentOptimizationTarget = 250000000;
    constexpr qint64 warmGrowthLimit = 5000000;
    const bool memoryPassed = maximum <= residentLimit && final <= baseline + warmGrowthLimit;
    if (!report({{"type", "memory"}, {"maxResidentBytes", double(maximum)}, {"residentLimitBytes", double(residentLimit)},
                 {"optimizationTargetBytes", double(residentOptimizationTarget)},
                 {"warmGrowthBytes", double(final - baseline)}, {"warmGrowthLimitBytes", double(warmGrowthLimit)}, {"passed", memoryPassed}})) return false;
    accepted = memoryPassed && accepted;
    if (!workspace.filter("") || !workspace.pauseLogs(true)) return false;
    QEventLoop idle;
    QTimer quiet, timeout, foregroundGuard;
    quiet.setSingleShot(true); timeout.setSingleShot(true);
    quiet.setTimerType(Qt::PreciseTimer);
    bool timedOut = false;
    bool foregroundLost = desktop && !validDesktopState();
    QObject::connect(&foregroundGuard, &QTimer::timeout, &idle, [&] {
        if (validDesktopState()) return;
        foregroundLost = true; idle.quit();
    });
    QObject::connect(&timeout, &QTimer::timeout, &idle, [&] { timedOut = true; idle.quit(); });
    QElapsedTimer period;
    double cpuSeconds = 0;
    qint64 idleMilliseconds = 0;
    bool synchronizing = workspace.loading() || workspace.busy();
    auto cpu = std::clock(); period.start();
    if (cpu == std::clock_t(-1)) return false;
    const auto accumulate = [&] {
        if (!synchronizing) {
            cpuSeconds += double(std::clock() - cpu) / CLOCKS_PER_SEC;
            idleMilliseconds += period.elapsed();
        }
    };
    QObject::connect(&quiet, &QTimer::timeout, &idle, [&] {
        const auto remaining = 60000 - idleMilliseconds - period.elapsed();
        if (remaining > 0) quiet.start(static_cast<int>(remaining));
        else idle.quit();
    });
    const auto sync = QObject::connect(&workspace, &podlord::Workspace::changed, &idle, [&] {
        const bool next = workspace.loading() || workspace.busy();
        if (next == synchronizing) return;
        accumulate();
        if (next) quiet.stop();
        else quiet.start(static_cast<int>(std::max<qint64>(1, 60000 - idleMilliseconds)));
        synchronizing = next; cpu = std::clock(); period.restart();
    });
    if (!synchronizing) quiet.start(60000);
    if (!report({{"type", "phase"}, {"name", "idle"}, {"logPaused", true}, {"requiredNonsyncMilliseconds", 60000}})) return false;
    timeout.start(120000); if (desktop) foregroundGuard.start(1000); idle.exec(); accumulate(); QObject::disconnect(sync);
    const double percent = idleMilliseconds > 0 ? cpuSeconds / (idleMilliseconds / 1000.0) * 100 : 100;
    const bool idlePassed = !timedOut && !foregroundLost && idleMilliseconds >= 60000 && percent <= 2;
    if (!report({{"type", "idle"}, {"milliseconds", double(idleMilliseconds)}, {"cpuPercentOneCore", percent}, {"foregroundLost", foregroundLost},
                 {"activationObserved", activationObserved}, {"passed", idlePassed}})) return false;
    accepted = idlePassed && accepted;
    probe->requested.store(0);
    QObject::disconnect(window, nullptr, &frameProbe, nullptr);
    if (qEnvironmentVariableIsSet("PODLORD_PERFORMANCE_INSPECT")) {
        if (!report({{"type", "phase"}, {"name", "diagnostic-hold"}, {"milliseconds", 30000}, {"scope", "After measured actions and idle; attach process diagnostics here"}})) return false;
        QTest::qWait(30000);
    }
    return report({{"type", "result"}, {"passed", accepted},
                   {"scope", backgroundMetal ? "background Metal diagnosis only; not foreground, startup or release evidence"
                        : desktop ? "foreground runtime and conservative per-frame thread CPU, not installed startup, license or signing clearance"
                                      : "headless runtime regression, not foreground frames, installed startup, license or signing clearance"}}) && accepted;
}
}
int main(int argc, char** argv) {
    const bool backgroundMetal = argc == 3 && QByteArrayView(argv[2]) == "--background-metal-diagnostic";
    // Qt's Cocoa launch delegate otherwise activates the process even after open -g.
    if (backgroundMetal && !qputenv("QT_MAC_DISABLE_FOREGROUND_APPLICATION_TRANSFORM", "1")) return 1;
    if (!podlord::configureGraphics()) { std::fputs("Cannot configure native graphics.\n", stderr); return 1; }
    QGuiApplication app(argc, argv);
    if (argc != 2 && !backgroundMetal) return 2;
    if (backgroundMetal && QGuiApplication::platformName() != "cocoa") {
        std::fputs("Background Metal diagnostics require the macOS Cocoa renderer.\n", stderr);
        return 2;
    }
    if (QGuiApplication::platformName() != "cocoa" && !(QGuiApplication::platformName() == "offscreen" && qEnvironmentVariable("QT_QUICK_BACKEND") == "software")) {
        std::fprintf(stderr, "Use the foreground macOS Cocoa renderer or explicit offscreen/software regression measurement.\n");
        return 2;
    }
    const auto config=QString::fromLocal8Bit(argv[1]);
    QElapsedTimer startup; startup.start();
    QTemporaryDir temporary;
    if (!temporary.isValid()) return 1;
    podlord::Workspace workspace(temporary.filePath("profile"));
    if (backgroundMetal && !podlord::test::muteAlerts(workspace.alerts())) return 1;
    QQmlApplicationEngine engine;
    if (backgroundMetal) engine.setInitialProperties({{"visible", false}, {"flags", int(Qt::Window | Qt::WindowDoesNotAcceptFocus)}});
    engine.rootContext()->setContextProperty("workspace", &workspace);
    engine.load(QUrl("qrc:/podlord/Main.qml"));
    if (engine.rootObjects().isEmpty()) return 1;
    auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
    if (backgroundMetal && window) window->show();
    QTimer::singleShot(0,&app,[&] {
        const bool passed=run(config,workspace,window,startup,backgroundMetal);
        if (!passed) std::fprintf(stderr,"Native performance protocol failed.\n");
        if (window) window->close();
        app.exit(passed ? 0 : 1);
    });
    return app.exec();
}
