#include "workspace.h"
#include "workspace_runtime.h"
#include "window_host.h"
#include <QDebug>
#include <QFutureWatcher>
#include <QtConcurrent/QtConcurrentRun>

namespace podlord {
Workspace::~Workspace() {
    if (snapshotPreparation_) snapshotPreparation_->watcher->cancel();
    runtime_->windows.removeAll(this);
    for (auto it = runtime_->owners.begin(); it != runtime_->owners.end();)
        if (it.value() == this) { client_.close(it.key()); it = runtime_->owners.erase(it); }
        else ++it;
    client_.showSession({}, viewId_);
    disconnect(&client_, nullptr, this, nullptr);
    sourceImportFuture_.waitForFinished();
    sourceEditFuture_.waitForFinished();
    sessionMutationFuture_.waitForFinished();
    if (presetsBusy_) presetsFuture_.waitForFinished();
}
WorkspaceRuntime::~WorkspaceRuntime() { flushViewSaves(); flushUsageSaves(); }
void WorkspaceRuntime::flushViewSaves() {
    while (viewSaving || !pendingViews.isEmpty()) {
        if (!viewSaving) dispatchViewSave();
        if (!viewSaving) break;
        viewSaveFuture.waitForFinished();
        const auto result = viewSaveFuture.result();
        if (const auto* saved = std::get_if<TableViewStates>(&result)) savedViews[viewSaveContext] = *saved;
        else { pendingViews.remove(viewSaveContext); qWarning("Context settings could not be saved during shutdown."); }
        viewSaving = false;
    }
}
bool Workspace::viewStateFailed() const {
    if (!presetsError_.isEmpty()) return true;
    for (const auto& tab : catalog_.sessions) {
        const auto id = tab.id.toString(QUuid::WithoutBraces);
        if ((runtime_->owners.value(id) == this || (!runtime_->owners.contains(id) && !detached_))
            && viewStateErrors_.contains(tab.config.contextId)) return true;
    }
    return false;
}
QString Workspace::viewStateError() const {
    if (!presetsError_.isEmpty()) return presetsError_;
    const auto context = sessionConfiguration(active_).value("contextId").toString();
    if (viewStateErrors_.contains(context)) return viewStateErrors_.value(context);
    return viewStateErrors_.isEmpty() ? QString{} : viewStateErrors_.first();
}
bool Workspace::viewCloseNeedsDecision() const {
    return (runtime_->host && runtime_->host->layoutCloseNeedsDecision(*this))
        || (viewClosePending_ && !viewSaving_ && viewStateFailed());
}
void Workspace::applyContextViews(const QString& context, const TableViewStates& views, bool refresh) {
    const auto schemas = tableSchemas(true);
    for (const auto& session : catalog_.sessions) {
        const auto id = session.id.toString(QUuid::WithoutBraces);
        if (session.config.contextId != context || (runtime_->owners.contains(id) && runtime_->owners.value(id) != this)) continue;
        auto& nav = navigation_[id];
        const auto resources = views.value("resource"), events = views.value("event");
        nav.filter = resources.filter; nav.fields = resources.fields;
        nav.column = static_cast<int>(schemas.value("resource").indexOf(resources.column));
        nav.mode = resources.mode; nav.limit = resources.limit;
        nav.order = resources.descending ? Qt::DescendingOrder : Qt::AscendingOrder;
        nav.eventFilter = events.filter; nav.eventColumn = static_cast<int>(schemas.value("event").indexOf(events.column));
        nav.eventOrder = events.descending ? Qt::DescendingOrder : Qt::AscendingOrder;
        const auto ports = views.value("port"), detailEvents = views.value("inspectorEvent"), links = views.value("inspectorLink");
        nav.portFilter = ports.filter; nav.portColumn = static_cast<int>(schemas.value("port").indexOf(ports.column));
        nav.portOrder = ports.descending ? Qt::DescendingOrder : Qt::AscendingOrder;
        nav.inspectorEventColumn = static_cast<int>(schemas.value("inspectorEvent").indexOf(detailEvents.column));
        nav.inspectorEventOrder = detailEvents.descending ? Qt::DescendingOrder : Qt::AscendingOrder;
        nav.inspectorLinkColumn = static_cast<int>(schemas.value("inspectorLink").indexOf(links.column));
        nav.inspectorLinkOrder = links.descending ? Qt::DescendingOrder : Qt::AscendingOrder;
        const auto values = views.value("value");
        nav.valueColumn = static_cast<int>(schemas.value("value").indexOf(values.column));
        nav.valueOrder = values.descending ? Qt::DescendingOrder : Qt::AscendingOrder;
    }
    if (!refresh || sessionConfiguration(active_).value("contextId").toString() != context) return;
    const auto& nav = currentNavigation();
    visibleTable_.setLimit(nav.limit); table_.filter(nav.filter, nav.fields, nav.mode); table_.sort(nav.column, nav.order);
    events_.filter(nav.eventFilter); events_.sort(nav.eventColumn, nav.eventOrder);
    ports_.setFilterFixedString(nav.portFilter.trimmed()); ports_.sort(nav.portColumn, nav.portOrder);
    inspectorEventsTable_.sort(nav.inspectorEventColumn, nav.inspectorEventOrder);
    inspectorLinksTable_.sort(nav.inspectorLinkColumn, nav.inspectorLinkOrder); valuesTable_.sort(nav.valueColumn, nav.valueOrder);
    dashboardDirty_ = true; publishDashboard();
    emit fieldFiltersChanged(); emit resourcePresentationChanged(); emit changed();
}
bool Workspace::restoreViewStates(const LoadedViewStates& states) {
    viewStateErrors_.clear();
    runtime_->viewSchemas = tableSchemas(true);
    for (auto context = states.cbegin(); context != states.cend(); ++context) {
        if (const auto* failure = std::get_if<Failure>(&context.value())) { viewStateErrors_[context.key()] = failure->message; continue; }
        const auto views = std::get<TableViewStates>(context.value());
        savedViews_[context.key()] = views; runtime_->contextViews[context.key()] = views;
        for (auto* window : runtime_->windows) if (!window->windowClosed_) window->applyContextViews(context.key(), views, window != this);
    }
    return !viewStateFailed();
}
bool Workspace::queueViewSave(const QString& table) {
    const auto context = sessionConfiguration(active_).value("contextId").toString();
    if (context.isEmpty() || viewStateErrors_.contains(context)) return false;
    const auto& nav = currentNavigation();
    const auto* model = table == "resource" ? &rows_ : table == "event" ? &eventRows_ : table == "port" ? &portRows_
        : table == "inspectorEvent" ? &inspectorEventRows_ : table == "inspectorLink" ? &inspectorLinkRows_ : table == "value" ? &valueRows_ : nullptr;
    if (!model) return false;
    const int column = table == "resource" ? nav.column : table == "event" ? nav.eventColumn : table == "port" ? nav.portColumn
        : table == "inspectorEvent" ? nav.inspectorEventColumn : table == "inspectorLink" ? nav.inspectorLinkColumn : nav.valueColumn;
    const auto order = table == "resource" ? nav.order : table == "event" ? nav.eventOrder : table == "port" ? nav.portOrder
        : table == "inspectorEvent" ? nav.inspectorEventOrder : table == "inspectorLink" ? nav.inspectorLinkOrder : nav.valueOrder;
    const TableViewState value{table == "resource" ? nav.filter : table == "event" ? nav.eventFilter : table == "port" ? nav.portFilter : QString{},
        column < 0 ? QString{} : model->headerData(column, Qt::Horizontal, Qt::UserRole).toString(), column >= 0 && order == Qt::DescendingOrder,
        table == "resource" ? nav.fields : QMap<QString, QString>{}, table == "resource" ? nav.mode : QString{}, table == "resource" ? nav.limit : 256,
        table == "resource" ? runtime_->contextViews.value(context).value("resource").namespaces : QStringList{}};
    pendingViews_[context][table] = value;
    auto& views = runtime_->contextViews[context];
    if (views.isEmpty()) views = savedViews_.value(context);
    views[table] = value;
    for (auto* window : runtime_->windows) if (!window->windowClosed_) {
        window->viewCloseDiscardApproved_ = false;
        window->applyContextViews(context, views, window != this);
    }
    return dispatchViewSave();
}
bool Workspace::dispatchViewSave() {
    runtime_->viewSchemas = tableSchemas(true);
    return runtime_->dispatchViewSave();
}
bool WorkspaceRuntime::dispatchViewSave() {
    if (viewSaving || pendingViews.isEmpty()) return false;
    const auto context = pendingViews.firstKey();
    const auto table = pendingViews[context].firstKey();
    const auto value = pendingViews[context].take(table);
    if (pendingViews[context].isEmpty()) pendingViews.remove(context);
    const auto expected = savedViews.value(context).value(table,
        TableViewState{.limit = table == "resource" ? DefaultResourceDisplayLimit : 256});
    viewSaving = true; viewSaveContext = context;
    for (auto* window : windows) if (!window->windowClosed_) emit window->sessionWindowActionsReadyChanged();
    auto* watcher = new QFutureWatcher<Result<TableViewStates>>(&client);
    QObject::connect(watcher, &QFutureWatcher<Result<TableViewStates>>::finished, &client, [this, watcher, context] {
        const auto result = watcher->result(); watcher->deleteLater(); viewSaving = false;
        if (const auto* failure = std::get_if<Failure>(&result)) { viewStateErrors[context] = failure->message; pendingViews.remove(context); }
        else savedViews[context] = std::get<TableViewStates>(result);
        dispatchViewSave();
        QList<QPointer<Workspace>> observers;
        for (auto* window : windows) observers.append(window);
        for (auto window : observers) if (window && !window->windowClosed_) {
            emit window->sessionWindowActionsReadyChanged();
            if (window && (std::holds_alternative<Failure>(result) || window->viewClosePending_)) emit window->changed();
            if (window && window->viewClosePending_ && !viewSaving && !window->presetsBusy_ && pendingViews.isEmpty() && !window->viewStateFailed())
                emit window->windowCloseApproved();
        }
    });
    viewSaveFuture = QtConcurrent::run([profile = profile, schemas = viewSchemas, context, table, value, expected] {
        return ViewStateStore(profile, schemas).save(context, table, value, expected);
    });
    watcher->setFuture(viewSaveFuture);
    return true;
}
bool Workspace::reloadSavedViews() {
    if (busy_ || viewSaving_ || !pendingViews_.isEmpty()) return false;
    if (!viewsLoaded_) return reload();
    busy_ = true; emit changed();
    auto* watcher = new QFutureWatcher<LoadedViewStates>(this);
    connect(watcher, &QFutureWatcher<LoadedViewStates>::finished, this, [this, watcher] {
        const auto result = watcher->result(); watcher->deleteLater(); busy_ = false;
        restoreViewStates(result); publish(); emit changed();
    });
    QSet<QString> ids;
    for (const auto& source : sources_.sources) for (const auto& context : source.contexts) ids.insert(context.id);
    for (const auto& session : catalog_.sessions) ids.insert(session.config.contextId);
    watcher->setFuture(QtConcurrent::run([profile = profile_, schemas = tableSchemas(true), ids] {
        LoadedViewStates result;
        for (const auto& id : ids) result.insert(id, ViewStateStore(profile, schemas).load(id));
        return result;
    }));
    return true;
}
bool Workspace::confirmViewClose(bool discard) {
    if (runtime_->host && runtime_->host->layoutCloseNeedsDecision(*this)) return runtime_->host->confirmLayoutClose(*this, discard);
    if (!viewCloseNeedsDecision()) return false;
    viewClosePending_ = false;
    if (discard) { viewCloseDiscardApproved_ = true; emit windowCloseApproved(); }
    else emit windowCloseCancelled();
    emit changed(); return true;
}
}
