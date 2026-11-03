#include "window_host.h"
#include "workspace.h"
#include "workspace_runtime.h"
#include <QEvent>
#include <QGuiApplication>
#include <QQmlContext>
#include <QTimer>

namespace podlord {
WindowHost::WindowHost(Workspace& primary, QObject* parent)
    : QObject(parent), primary_(primary), component_(&engine_) {
    primary_.runtime_->host = this;
    qGuiApp->installEventFilter(this);
    component_.loadUrl(QUrl("qrc:/podlord/Main.qml"));
}

WindowHost::~WindowHost() {
    qGuiApp->removeEventFilter(this);
    primary_.runtime_->host = nullptr;
    for (const auto& entry : std::as_const(windows_)) {
        delete entry.window.data(); delete entry.context;
        if (entry.owned) delete entry.workspace;
    }
}

bool WindowHost::create(Workspace& workspace, bool owned) {
    if (!component_.isReady()) return false;
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
    windows_.append({&workspace, window, context, owned});
    connect(&workspace, &Workspace::windowClosed, this, [this, &workspace] {
        QTimer::singleShot(0, this, [this, &workspace] { retire(workspace); });
    });
    connect(&workspace, &Workspace::windowCloseCancelled, this, [this] { closePhase_ = ClosePhase::Idle; });
    emit windowOpened(window); return true;
}

bool WindowHost::open() {
    if (!windows_.isEmpty()) return focus(primary_);
    return create(primary_, false);
}

QList<QQuickWindow*> WindowHost::windows() const {
    QList<QQuickWindow*> result;
    for (const auto& entry : windows_) if (entry.window && !entry.workspace->windowClosed_) result.append(entry.window);
    return result;
}

bool WindowHost::applicationClosing() const { return closePhase_ != ClosePhase::Idle; }

bool WindowHost::eventFilter(QObject* watched, QEvent* event) {
    if (watched == qGuiApp && event->type() == QEvent::Quit && !windows().isEmpty()) {
        // Qt closes individual windows while handling Quit. Keep that operation
        // distinct from a user closing one window so open tabs survive restart.
        closePhase_ = ClosePhase::Waiting;
        for (const auto& entry : std::as_const(windows_)) {
            if (!entry.window || entry.workspace->windowClosed_) continue;
            if (entry.workspace->requestWindowClose()) continue;
            const auto* workspace = entry.workspace;
            const bool decisionPending = workspace->pendingLeave_ && workspace->pendingLeave_->action == Workspace::Leave::Window;
            if (!decisionPending && !workspace->viewClosePending_) closePhase_ = ClosePhase::Idle;
            event->ignore();
            return true;
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
        QTimer::singleShot(0, this, [this] {
            if (closePhase_ != ClosePhase::Waiting) return;
            QEvent quit(QEvent::Quit);
            QCoreApplication::sendEvent(qGuiApp, &quit);
        });
        return true;
    }
    return QObject::eventFilter(watched, event);
}

bool WindowHost::focus(Workspace& workspace) {
    for (const auto& entry : windows_) {
        if (entry.workspace != &workspace || !entry.window || workspace.windowClosed_) continue;
        if (entry.window->visibility() == QWindow::Minimized) entry.window->showNormal();
        else entry.window->show();
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
    if (source.windowClosed_ || source.runtime_->owners.value(session) != &source) return false;
    emit source.logViewChanging();
    const bool fresh = session == source.active_ && source.yamlFresh_;
    auto* target = new Workspace(source.runtime_, this, false);
    target->detached_ = true;
    target->sources_ = source.sources_; target->catalog_ = source.catalog_; target->selection_ = source.selection_;
    target->settings_ = source.settings_; target->settingsReady_ = source.settingsReady_; target->viewsLoaded_ = true;
    target->tableLayouts_ = source.tableLayouts_;
    target->navigation_.insert(session, source.navigation_.value(session));
    target->savedViews_.insert(session, source.savedViews_.value(session));
    target->active_ = session;
    for (auto it = source.logPositions_.cbegin(); it != source.logPositions_.cend(); ++it)
        if (it.key().startsWith(session + '\n')) target->logPositions_.insert(it.key(), it.value());
    target->publishAppearance();
    QJsonArray sources;
    for (int row = 0; row < source.sourceRows_.rowCount(); ++row) sources.append(source.sourceRows_.row(row));
    target->sourceRows_.publish(sources);
    if (!create(*target, true)) { delete target; return false; }
    const bool selected = session == source.active_;
    const bool moved = selected ? source.client_.moveSession(source.viewId_, target->viewId_)
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
    source.navigation_.remove(session); source.savedViews_.remove(session);
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
