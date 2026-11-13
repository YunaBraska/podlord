#include "window_host.h"
#include "workspace.h"
#include "workspace_runtime.h"
#include <QEvent>
#include <QGuiApplication>
#include <QFutureWatcher>
#include <QQmlContext>
#include <QScreen>
#include <QTimer>
#include <QtConcurrent/QtConcurrentRun>
#include <algorithm>

namespace podlord {
WindowHost::WindowHost(Workspace& primary, QObject* parent)
    : QObject(parent), primary_(primary), component_(&engine_) {
    primary_.runtime_->host = this;
    qGuiApp->installEventFilter(this);
    retryQuit_.setSingleShot(true);
    connect(&retryQuit_, &QTimer::timeout, this, [this] {
        if (closePhase_ != ClosePhase::Waiting) return;
        QEvent quit(QEvent::Quit);
        QCoreApplication::sendEvent(qGuiApp, &quit);
    });
    restoreReady_ = connect(&primary_, &Workspace::changed, this, [this] { restore(); });
    auto* load = new QFutureWatcher<Result<WindowLayout>>(this);
    connect(load, &QFutureWatcher<Result<WindowLayout>>::finished, this, [this, load] {
        storedLayout_ = load->result(); load->deleteLater();
        restore();
        if (closePhase_ == ClosePhase::Waiting) retryQuit_.start(0);
    });
    const auto profile = primary_.profile_;
    layoutFuture_ = QtConcurrent::run([profile] { return WindowLayoutStore(profile).load(); });
    load->setFuture(layoutFuture_);
    component_.loadUrl(QUrl("qrc:/podlord/Main.qml"));
}

WindowHost::~WindowHost() {
    qGuiApp->removeEventFilter(this);
    layoutFuture_.waitForFinished();
    primary_.runtime_->host = nullptr;
    for (const auto& entry : std::as_const(windows_)) {
        delete entry.window.data(); delete entry.context;
        if (entry.owned) delete entry.workspace;
    }
}

bool WindowHost::create(Workspace& workspace, bool owned, const Workspace* source, const WindowPlacement* placement) {
    if (!component_.isReady()) return false;
    if (source) {
        workspace.sources_ = source->sources_; workspace.catalog_ = source->catalog_;
        workspace.settings_ = source->settings_; workspace.settingsReady_ = source->settingsReady_; workspace.viewsLoaded_ = true;
        workspace.tableLayouts_ = source->tableLayouts_;
        workspace.publishAppearance();
        QJsonArray sources;
        for (int row = 0; row < source->sourceRows_.rowCount(); ++row) sources.append(source->sourceRows_.row(row));
        workspace.sourceRows_.publish(sources);
    }
    auto* context = new QQmlContext(engine_.rootContext(), this);
    context->setContextProperty("workspace", &workspace);
    auto* object = component_.create(context);
    auto* window = qobject_cast<QQuickWindow*>(object);
    if (!window) {
        delete object; delete context;
        workspace.error_ = "The native window could not be created; the original tab remains open.";
        emit workspace.changed(); return false;
    }
    QQmlEngine::setObjectOwnership(window, QQmlEngine::CppOwnership);
    windows_.append({&workspace, window, context, owned,
        placement ? placement->id : workspace.detached_ ? QUuid::createUuid().toString(QUuid::WithoutBraces) : QString("main"),
        window->geometry(), window->windowState() == Qt::WindowMaximized});
    const bool desktopGeometry = workspace.windowActionsAvailable();
    const auto geometryChanged = [this, window, desktopGeometry] {
        if (window->windowState() != Qt::WindowNoState && desktopGeometry) return;
        for (auto& entry : windows_) if (entry.window == window) { entry.normalGeometry = window->geometry(); break; }
    };
    // Platform geometry can change before QWindow publishes its new state.
    // Deliver after that transition; the window owns and cancels queued updates.
    connect(window, &QWindow::xChanged, window, geometryChanged, Qt::QueuedConnection);
    connect(window, &QWindow::yChanged, window, geometryChanged, Qt::QueuedConnection);
    connect(window, &QWindow::widthChanged, window, geometryChanged, Qt::QueuedConnection);
    connect(window, &QWindow::heightChanged, window, geometryChanged, Qt::QueuedConnection);
    connect(window, &QWindow::windowStateChanged, this, [this, window](Qt::WindowState state) {
        if (state == Qt::WindowMinimized) return;
        for (auto& entry : windows_) if (entry.window == window) { entry.maximized = state == Qt::WindowMaximized; break; }
    });
    connect(&workspace, &Workspace::windowClosed, this, [this, &workspace] {
        QTimer::singleShot(0, this, [this, &workspace] { retire(workspace); });
    });
    connect(&workspace, &Workspace::windowCloseCancelled, this, &WindowHost::cancelClose);
    emit windowOpened(window); return true;
}

bool WindowHost::open() {
    if (!windows_.isEmpty()) return focus(primary_);
    const bool opened = create(primary_, false);
    if (opened) restore();
    return opened;
}

QList<QQuickWindow*> WindowHost::windows() const {
    QList<QQuickWindow*> result;
    for (const auto& entry : windows_) if (entry.window && !entry.workspace->windowClosed_) result.append(entry.window);
    return result;
}

bool WindowHost::applicationClosing() const { return closePhase_ != ClosePhase::Idle; }

WindowLayout WindowHost::layout() const {
    WindowLayout result;
    for (const auto& entry : windows_) {
        if (!entry.window || entry.workspace->windowClosed_) continue;
        const auto geometry = entry.window->windowState() == Qt::WindowNoState ? entry.window->geometry() : entry.normalGeometry;
        WindowPlacement placement{entry.id, {}, {}, geometry, entry.maximized};
        for (const auto& tab : entry.workspace->tabs()) placement.sessions.insert(QUuid(tab.toMap()["id"].toString()));
        const QUuid active(entry.workspace->active_);
        if (placement.sessions.contains(active)) placement.activeSession = active;
        result.append(std::move(placement));
    }
    return result;
}

bool WindowHost::restore() {
    if (layoutRestored_ || !storedLayout_ || windows_.isEmpty() || primary_.busy()) return false;
    layoutRestored_ = true; disconnect(restoreReady_);
    if (primary_.settingsReady_ && primary_.settings_.workspaceRestore && primary_.viewsLoaded_) {
        if (const auto* failure = std::get_if<Failure>(&*storedLayout_)) {
            primary_.error_ += (primary_.error_.isEmpty() ? QString{} : QString("\n")) + failure->message;
            emit primary_.changed();
        } else if (primary_.windowActionsAvailable()) {
            QSet<QUuid> open;
            for (const auto& session : primary_.catalog_.sessions) open.insert(session.id);
            WindowLayout desired;
            QSet<QUuid> assigned;
            for (auto placement : std::get<WindowLayout>(*storedLayout_)) {
                placement.sessions.intersect(open);
                if (placement.activeSession && !placement.sessions.contains(*placement.activeSession)) placement.activeSession.reset();
                if (placement.id != "main" && placement.sessions.isEmpty()) continue;
                assigned.unite(placement.sessions); desired.append(std::move(placement));
            }
            if (!desired.isEmpty()) {
                int root = 0;
                for (int i = 0; i < desired.size(); ++i) if (desired[i].id == "main") { root = i; break; }
                desired[root].sessions.unite(open - assigned);
                for (auto& placement : desired) if (!placement.activeSession) {
                    if (primary_.catalog_.activeSession && placement.sessions.contains(*primary_.catalog_.activeSession))
                        placement.activeSession = primary_.catalog_.activeSession;
                    else for (const auto& session : primary_.catalog_.sessions) if (placement.sessions.contains(session.id)) {
                        placement.activeSession = session.id; break;
                    }
                }
                windows_.first().id = desired[root].id;
                primary_.detached_ = desired[root].id != "main";
                primary_.active_ = desired[root].activeSession ? desired[root].activeSession->toString(QUuid::WithoutBraces) : QString{};
                for (int i = 0; i < desired.size(); ++i) {
                    if (i == root) continue;
                    const auto& placement = desired[i];
                    auto* target = new Workspace(primary_.runtime_, this, false);
                    target->detached_ = true;
                    target->active_ = placement.activeSession ? placement.activeSession->toString(QUuid::WithoutBraces) : QString{};
                    for (const auto& session : placement.sessions) {
                        const auto id = session.toString(QUuid::WithoutBraces);
                        target->navigation_.insert(id, primary_.navigation_.value(id));
                        primary_.runtime_->owners[id] = target;
                    }
                    target->navigation_[target->active_].workspace = "map";
                    if (!create(*target, true, &primary_, &placement)) {
                        for (const auto& session : placement.sessions) primary_.runtime_->owners[session.toString(QUuid::WithoutBraces)] = &primary_;
                        delete target;
                        primary_.error_ += (primary_.error_.isEmpty() ? QString{} : QString("\n"))
                            + "A saved window could not be restored; its sessions remain in the main window.";
                        continue;
                    }
                    for (const auto& session : placement.sessions) {
                        const auto id = session.toString(QUuid::WithoutBraces);
                        primary_.navigation_.remove(id);
                    }
                    target->mapNavigating_ = true; target->select(primary_.catalog_, false); target->mapNavigating_ = false;
                }
                primary_.navigation_[primary_.active_].workspace = "map";
                primary_.mapNavigating_ = true; primary_.select(primary_.catalog_, false); primary_.mapNavigating_ = false;
                for (auto& entry : windows_) {
                    const auto saved = std::find_if(desired.cbegin(), desired.cend(), [&](const auto& value) { return value.id == entry.id; });
                    if (saved == desired.cend()) continue;
                    auto* screen = entry.window->screen() ? entry.window->screen() : QGuiApplication::primaryScreen();
                    qint64 largest = 0;
                    for (auto* candidate : QGuiApplication::screens()) {
                        const auto intersection = candidate->availableGeometry().intersected(saved->geometry);
                        const qint64 area = qint64(qMax(0, intersection.width())) * qMax(0, intersection.height());
                        if (area > largest) { screen = candidate; largest = area; }
                    }
                    QRect geometry = saved->geometry;
                    if (screen) {
                        auto available = screen->availableGeometry().marginsRemoved(entry.window->frameMargins());
                        if (available.isEmpty()) available = screen->availableGeometry();
                        const int width = qBound(qMin(entry.window->minimumWidth(), available.width()), geometry.width(), available.width());
                        const int height = qBound(qMin(entry.window->minimumHeight(), available.height()), geometry.height(), available.height());
                        geometry = QRect(qBound(available.left(), geometry.x(), available.right() - width + 1),
                            qBound(available.top(), geometry.y(), available.bottom() - height + 1), width, height);
                    }
                    entry.window->setWindowState(Qt::WindowNoState); entry.window->setGeometry(geometry);
                    entry.normalGeometry = geometry;
                    // Keep the public QML visibility and native window state consistent.
                    entry.window->setProperty("visibility", QVariant::fromValue(saved->maximized ? QWindow::Maximized : QWindow::Windowed));
                }
                if (primary_.catalog_.activeSession && QGuiApplication::applicationState() == Qt::ApplicationActive) {
                    auto* selected = primary_.runtime_->owners.value(primary_.catalog_.activeSession->toString(QUuid::WithoutBraces));
                    if (selected) focus(*selected);
                }
            }
        }
    }
    emit primary_.sessionWindowActionsReadyChanged();
    if (closePhase_ == ClosePhase::Waiting) retryQuit_.start(0);
    return true;
}

bool WindowHost::layoutCloseNeedsDecision(const Workspace& workspace) const {
    return closePhase_ == ClosePhase::Waiting && layoutDecision_ == &workspace;
}

bool WindowHost::cancelClose() {
    if (closePhase_ == ClosePhase::Idle) return false;
    closePhase_ = ClosePhase::Idle; retryQuit_.stop(); skipLayoutSave_ = false;
    layoutDecision_.clear(); layoutCloseError_.clear();
    for (const auto& entry : windows_) {
        entry.workspace->viewClosePending_ = false; entry.workspace->viewCloseDiscardApproved_ = false;
        emit entry.workspace->changed();
    }
    return true;
}

bool WindowHost::confirmLayoutClose(Workspace& workspace, bool discard) {
    if (!layoutCloseNeedsDecision(workspace)) return false;
    if (!discard) return cancelClose();
    layoutDecision_.clear(); layoutCloseError_.clear(); skipLayoutSave_ = true;
    emit workspace.changed(); retryQuit_.start(0); return true;
}

bool WindowHost::eventFilter(QObject* watched, QEvent* event) {
    if (event->type() == QEvent::Close && closePhase_ == ClosePhase::Idle && windows().size() == 1
        && !primary_.detached_ && watched == primary_.window_) {
        closePhase_ = ClosePhase::Waiting; retryQuit_.start(0); event->ignore(); return true;
    }
    if (watched == qGuiApp && event->type() == QEvent::Quit && !windows().isEmpty()) {
        // Qt closes individual windows while handling Quit. Keep that operation
        // distinct from a user closing one window so open tabs survive restart.
        closePhase_ = ClosePhase::Waiting;
        Workspace* placementOwner = nullptr;
        for (const auto& entry : std::as_const(windows_)) {
            if (!entry.window || entry.workspace->windowClosed_) continue;
            if (!placementOwner) placementOwner = entry.workspace;
            if (entry.workspace->requestWindowClose()) continue;
            const auto* workspace = entry.workspace;
            const bool decisionPending = workspace->pendingLeave_ && workspace->pendingLeave_->action == Workspace::Leave::Window;
            if (!decisionPending && !workspace->viewClosePending_ && layoutRestored_) cancelClose();
            event->ignore();
            return true;
        }
        if (layoutDecision_ || layoutSaving_ || !storedLayout_) { event->ignore(); return true; }
        if (!skipLayoutSave_ && layoutRestored_ && placementOwner->viewsLoaded_ && placementOwner->settings_.workspaceRestore) {
            const auto desired = layout();
            const auto* previous = std::get_if<WindowLayout>(&*storedLayout_);
            if (!previous || *previous != desired) {
                layoutSaving_ = true;
                auto* save = new QFutureWatcher<Result<WindowLayout>>(this);
                connect(save, &QFutureWatcher<Result<WindowLayout>>::finished, this, [this, save] {
                    const auto result = save->result(); save->deleteLater(); layoutSaving_ = false;
                    if (const auto* failure = std::get_if<Failure>(&result)) {
                        if (closePhase_ != ClosePhase::Waiting) return;
                        for (const auto& entry : windows_) if (entry.window && !entry.workspace->windowClosed_) {
                            layoutDecision_ = entry.workspace; layoutCloseError_ = failure->message;
                            emit entry.workspace->changed(); break;
                        }
                    } else {
                        storedLayout_ = result;
                        if (closePhase_ == ClosePhase::Waiting) retryQuit_.start(0);
                    }
                });
                const auto profile = primary_.profile_;
                const auto expected = previous ? *previous : WindowLayout{};
                layoutFuture_ = QtConcurrent::run([profile, desired, expected] { return WindowLayoutStore(profile).save(desired, expected); });
                save->setFuture(layoutFuture_); event->ignore(); return true;
            }
        }
        closePhase_ = ClosePhase::Closing;
        QTimer::singleShot(0, this, [this] {
            if (closePhase_ == ClosePhase::Closing) closePhase_ = ClosePhase::Idle;
        });
    } else if (event->type() == QEvent::Close && closePhase_ == ClosePhase::Waiting
        && std::any_of(windows_.cbegin(), windows_.cend(), [watched](const auto& entry) { return entry.window == watched; })) {
        // A completed save or accepted draft decision retries the whole quit,
        // rather than closing just the window that finished first.
        event->ignore();
        if (!layoutSaving_ && !layoutDecision_) retryQuit_.start(0);
        return true;
    }
    return QObject::eventFilter(watched, event);
}

bool WindowHost::focus(Workspace& workspace) {
    for (const auto& entry : windows_) {
        if (entry.workspace != &workspace || !entry.window || workspace.windowClosed_) continue;
        if (!entry.window->isVisible() || entry.window->visibility() == QWindow::Minimized)
            entry.window->setProperty("visibility", QVariant::fromValue(entry.maximized ? QWindow::Maximized : QWindow::Windowed));
        entry.window->raise(); entry.window->requestActivate(); return true;
    }
    return false;
}

bool WindowHost::retire(Workspace& workspace) {
    for (auto it = windows_.begin(); it != windows_.end(); ++it) {
        if (it->workspace != &workspace) continue;
        const auto entry = *it; windows_.erase(it);
        delete entry.window.data(); delete entry.context;
        if (entry.owned) entry.workspace->deleteLater();
        return true;
    }
    return false;
}

bool WindowHost::detach(Workspace& source, const QString& session) {
    if (!layoutRestored_ || source.windowClosed_ || source.runtime_->owners.value(session) != &source) return false;
    emit source.logViewChanging();
    const bool fresh = session == source.active_ && source.yamlFresh_;
    auto* target = new Workspace(source.runtime_, this, false);
    target->detached_ = true;
    target->navigation_.insert(session, source.navigation_.value(session));
    target->active_ = session;
    target->mapHistory_.append(session);
    target->mapHistoryIndex_ = 1;
    for (auto it = source.logPositions_.cbegin(); it != source.logPositions_.cend(); ++it)
        if (it.key().startsWith(session + '\n')) target->logPositions_.insert(it.key(), it.value());
    if (!create(*target, true, &source)) { delete target; return false; }
    const bool selected = session == source.active_;
    const bool overview = source.workspacePage() == "map" && source.mapOverview();
    const bool moved = selected && !overview ? source.client_.moveSession(source.viewId_, target->viewId_)
        : source.client_.showSession(session, target->viewId_);
    if (!moved) {
        target->windowClosed_ = true; retire(*target);
        source.error_ = "This session is already shown elsewhere; its original tab and connections are unchanged.";
        emit source.changed(); return false;
    }
    source.runtime_->owners[session] = target;
    if (source.preparedSnapshots_.contains(session)) target->preparedSnapshots_.insert(session, source.preparedSnapshots_.take(session));
    if (source.snapshotPreparation_ && source.snapshotPreparation_->session == session) source.snapshotPreparation_->watcher->cancel();
    source.alerts_.transferSession(session, target->alerts_);
    source.navigation_.remove(session);
    for (auto it = source.logPositions_.begin(); it != source.logPositions_.end();)
        if (it.key().startsWith(session + '\n')) it = source.logPositions_.erase(it); else ++it;
    if (selected) {
        source.revealedValues_.clear(); source.select(source.catalog_, false);
        if (!source.active_.isEmpty() && !source.client_.connection(source.active_)) source.resolve(source.active_);
    } else { emit source.catalogsChanged(); emit source.changed(); }
    target->publish();
    target->yamlFresh_ = fresh; emit target->yamlEditChanged();
    if (!selected && !target->inspectorPath().isEmpty()) target->refreshInspector();
    if (target->logsVisible()) target->client_.showLogs(session, target->navigation_.value(session).inspected, target->viewId_);
    target->publishLogs(); emit target->catalogsChanged();
    return focus(*target);
}
}
