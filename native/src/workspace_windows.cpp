#include "workspace.h"
#include "workspace_runtime.h"
#include "window_host.h"
#include <QFutureWatcher>
#include <QGuiApplication>
#include <QQuickWindow>
#include <QtConcurrent/QtConcurrentRun>

namespace podlord {
WorkspaceRuntime::WorkspaceRuntime(QString selectedProfile, std::function<QDateTime()> clock, QUrl releaseEndpoint)
    : profile(std::move(selectedProfile)), now(std::move(clock)), client(nullptr, now),
      releases(profile, nullptr, now, std::move(releaseEndpoint)) {
    QObject::connect(&client, &ResourceClient::authenticationRejected, &credentials, &CredentialProcess::invalidate);
    QObject::connect(&credentials, &CredentialProcess::completed, &client, [this](const QString& credential, const QString& session) {
        const auto result = credentials.result(credential);
        if (const auto* connection = std::get_if<ClusterConnection>(&result)) {
            QString foreground = session;
            for (const auto* window : windows) {
                if (window->windowClosed_ || !window->windowVisible_) continue;
                const auto active = client.connection(window->active_);
                if (!active || active->credentialId != credential) continue;
                foreground = window->active_;
                if (window->windowFocused_) break;
            }
            client.authenticate(*connection, foreground);
        }
    });
}

bool WorkspaceRuntime::publishCatalog(const SessionCatalog& catalog, Workspace* source) {
    QSet<QString> open;
    for (const auto& session : catalog.sessions) if (session.open) open.insert(session.id.toString(QUuid::WithoutBraces));
    for (auto it = owners.begin(); it != owners.end();) {
        if (open.contains(it.key())) { ++it; continue; }
        it.value()->alerts_.closeSession(it.key());
        client.close(it.key()); it = owners.erase(it);
    }
    for (const auto& id : open) if (!owners.contains(id)) owners.insert(id, source);
    for (auto* window : windows) {
        if (window == source || window->windowClosed_) continue;
        window->sources_ = source->sources_;
        window->selection_ = source->selection_;
        window->select(catalog, false);
    }
    return true;
}

bool Workspace::windowActionsAvailable() const {
#if defined(Q_OS_IOS) || defined(Q_OS_ANDROID)
    return false;
#else
    return runtime_->host != nullptr;
#endif
}

bool Workspace::windowActionsReady() const {
    return windowActionsAvailable() && !windowClosed_ && !busy_ && !pendingLeave_
        && !viewSaving_ && pendingViews_.isEmpty() && !viewStateFailed() && !presetsBusy_ && !tableLayoutSaving_;
}

bool Workspace::attachWindow(QObject* window) {
    if (!qobject_cast<QQuickWindow*>(window) || windowClosed_) return false;
    if (window_) window_->removeEventFilter(this);
    window_ = window; window_->installEventFilter(this); return true;
}

bool Workspace::setWindowFocused(bool focused) {
    windowFocused_ = focused;
    return !windowClosed_ && client_.setFocused(focused && windowVisible_
        && QGuiApplication::applicationState() == Qt::ApplicationActive, viewId_);
}

bool Workspace::detachSession(const QString& id) {
    if (!windowActionsAvailable() || windowClosed_ || busy_ || id.isEmpty()) return false;
    if (auto* owner = runtime_->owners.value(id); owner && owner != this)
        return owner->activate(id) && runtime_->host->focus(*owner);
    if (detached_ && tabs().size() == 1) return activate(id) && runtime_->host->focus(*this);
    if (runtime_->owners.value(id) != this) return false;
    if (id == active_ && !allowLeave(Leave::Detach, id)) return false;
    if (viewSaving_ || !pendingViews_.isEmpty() || viewStateFailed() || presetsBusy_ || tableLayoutSaving_) {
        error_ = "Wait for the saved view to finish, or resolve its save error before moving this tab.";
        emit changed(); return false;
    }
    return runtime_->host->detach(*this, id);
}
bool Workspace::openSessionWindow(const QString& id) {
    if (!windowActionsAvailable() || windowClosed_ || busy_ || pendingLeave_ || id.isEmpty()) return false;
    if (auto* owner = runtime_->owners.value(id)) {
        if (owner == this) return detachSession(id);
        return owner->activate(id) && runtime_->host->focus(*owner);
    }
    const auto found = std::find_if(catalog_.sessions.cbegin(), catalog_.sessions.cend(), [&](const auto& session) {
        return session.id.toString(QUuid::WithoutBraces) == id;
    });
    if (found == catalog_.sessions.cend() || viewSaving_ || !pendingViews_.isEmpty() || viewStateFailed()) return false;
    return mutate([id](const SessionStore& store) { return store.activate(QUuid(id)); }, {}, {}, {}, true);
}

bool Workspace::prepareWindowClose() {
    if (windowCloseReady_ || windowClosed_) return true;
    if (!runtime_->host || (!detached_ && runtime_->windows.size() == 1)) return true;
    if (busy_) return false;
    QList<QUuid> ids;
    for (const auto& row : tabs()) ids.append(QUuid(row.toMap()["id"].toString()));
    if (ids.isEmpty()) return true;
    busy_ = true; emit changed();
    auto* watcher = new QFutureWatcher<Result<SessionCatalog>>(this);
    connect(watcher, &QFutureWatcher<Result<SessionCatalog>>::finished, this, [this, watcher] {
        const auto result = watcher->result(); watcher->deleteLater(); busy_ = false;
        if (const auto* failure = std::get_if<Failure>(&result)) {
            error_ = failure->message; emit changed(); return;
        }
        select(std::get<SessionCatalog>(result));
        windowCloseReady_ = true; emit changed(); emit windowCloseApproved();
    });
    sessionMutationFuture_ = QtConcurrent::run([profile = profile_, ids] { return SessionStore(profile).closeSessions(ids); });
    watcher->setFuture(sessionMutationFuture_);
    return false;
}

bool Workspace::finishWindowClose() {
    if (windowClosed_) return true;
    windowClosed_ = true;
    if (window_) window_->removeEventFilter(this);
    client_.hideLogs(viewId_);
    client_.showSession({}, viewId_);
    for (auto it = runtime_->owners.begin(); it != runtime_->owners.end();) {
        if (it.value() != this) { ++it; continue; }
        client_.close(it.key()); alerts_.closeSession(it.key()); it = runtime_->owners.erase(it);
    }
    runtime_->windows.removeAll(this);
    active_.clear(); navigation_.clear(); logPositions_.clear(); revealedValues_.clear();
    rows_.publish({}); eventRows_.publish({}); portRows_.publish({});
    inspectorEventRows_.publish({}); inspectorLinkRows_.publish({}); valueRows_.publish({}); logs_.publish({});
    sourceRows_.publish({}); diagnosticRows_.publish({}); requestAuditRows_.publish({});
    inspectorDocument_ = {}; inspectorSummary_ = {}; overviewFields_.clear(); inspectorEvents_.clear(); inspectorLinks_.clear();
    overview_.clear(); yaml_.clear(); inspectorScope_.clear(); logScope_.clear();
    emit windowClosed(); return true;
}
}
