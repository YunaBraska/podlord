#include "workspace.h"
#include <QDebug>
#include <QFutureWatcher>
#include <QtConcurrent/QtConcurrentRun>

namespace podlord {
Workspace::~Workspace() {
    disconnect(&client_, nullptr, this, nullptr);
    sourceImportFuture_.waitForFinished();
    sessionMutationFuture_.waitForFinished();
    if (presetsBusy_) presetsFuture_.waitForFinished();
    // Normal window closure drains asynchronously. Direct destruction must not leave
    // profile writes running after the profile or application has been disposed.
    if (viewSaving_) {
        viewSaveFuture_.waitForFinished();
        const auto result = viewSaveFuture_.result();
        if (const auto* saved = std::get_if<TableViewStates>(&result)) savedViews_[viewSaveSession_] = *saved;
        else { pendingViews_.remove(viewSaveSession_); qWarning("Session view state could not be saved during shutdown."); }
    }
    const ViewStateStore store(profile_, tableSchemas(true));
    for (auto session = pendingViews_.cbegin(); session != pendingViews_.cend(); ++session) {
        for (auto table = session->cbegin(); table != session->cend(); ++table) {
            const auto result = store.save(session.key(), table.key(), table.value(), savedViews_.value(session.key()).value(table.key()));
            if (const auto* saved = std::get_if<TableViewStates>(&result)) savedViews_[session.key()] = *saved;
            else { qWarning("Session view state could not be saved during shutdown."); break; }
        }
    }
}
bool Workspace::viewStateFailed() const { return !viewStateErrors_.isEmpty() || !presetsError_.isEmpty(); }
QString Workspace::viewStateError() const {
    if (!presetsError_.isEmpty()) return presetsError_;
    if (viewStateErrors_.contains(active_)) return viewStateErrors_.value(active_);
    return viewStateErrors_.isEmpty() ? QString{} : viewStateErrors_.first();
}
bool Workspace::viewCloseNeedsDecision() const { return viewClosePending_ && !viewSaving_ && viewStateFailed(); }
bool Workspace::restoreViewStates(const LoadedViewStates& states) {
    viewStateErrors_.clear();
    const auto schemas = tableSchemas(true);
    for (auto session = states.cbegin(); session != states.cend(); ++session) {
        if (const auto* failure = std::get_if<Failure>(&session.value())) {
            viewStateErrors_[session.key()] = "Session " + session.key() + ": " + failure->message; continue;
        }
        const auto views = std::get<TableViewStates>(session.value());
        savedViews_[session.key()] = views;
        auto& nav = navigation_[session.key()];
        const auto resources = views.value("resource"), events = views.value("event");
        nav.filter = resources.filter; nav.fields = resources.fields; nav.column = static_cast<int>(schemas.value("resource").indexOf(resources.column));
        nav.mode = resources.mode;
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
    emit fieldFiltersChanged();
    return !viewStateFailed();
}
bool Workspace::queueViewSave(const QString& table) {
    if (active_.isEmpty() || viewStateErrors_.contains(active_)) return false;
    const auto& nav = navigation_[active_];
    const auto* model = table == "resource" ? &rows_ : table == "event" ? &eventRows_ : table == "port" ? &portRows_
        : table == "inspectorEvent" ? &inspectorEventRows_ : table == "inspectorLink" ? &inspectorLinkRows_ : table == "value" ? &valueRows_ : nullptr;
    if (!model) return false;
    const int column = table == "resource" ? nav.column : table == "event" ? nav.eventColumn : table == "port" ? nav.portColumn
        : table == "inspectorEvent" ? nav.inspectorEventColumn : table == "inspectorLink" ? nav.inspectorLinkColumn : nav.valueColumn;
    const auto order = table == "resource" ? nav.order : table == "event" ? nav.eventOrder : table == "port" ? nav.portOrder
        : table == "inspectorEvent" ? nav.inspectorEventOrder : table == "inspectorLink" ? nav.inspectorLinkOrder : nav.valueOrder;
    pendingViews_[active_][table] = {table == "resource" ? nav.filter : table == "event" ? nav.eventFilter : table == "port" ? nav.portFilter : QString{},
        column < 0 ? QString{} : model->headerData(column, Qt::Horizontal, Qt::UserRole).toString(), column >= 0 && order == Qt::DescendingOrder,
        table == "resource" ? nav.fields : QMap<QString, QString>{}, table == "resource" ? nav.mode : QString{}};
    viewCloseDiscardApproved_ = false;
    return dispatchViewSave();
}
bool Workspace::dispatchViewSave() {
    if (viewSaving_ || pendingViews_.isEmpty()) return false;
    const auto session = pendingViews_.firstKey();
    const auto table = pendingViews_[session].firstKey();
    const auto value = pendingViews_[session].take(table);
    if (pendingViews_[session].isEmpty()) pendingViews_.remove(session);
    const auto expected = savedViews_.value(session).value(table);
    viewSaving_ = true; viewSaveSession_ = session;
    auto* watcher = new QFutureWatcher<Result<TableViewStates>>(this);
    connect(watcher, &QFutureWatcher<Result<TableViewStates>>::finished, this, [this, watcher, session] {
        const auto result = watcher->result(); watcher->deleteLater(); viewSaving_ = false;
        if (const auto* failure = std::get_if<Failure>(&result)) {
            viewStateErrors_[session] = "Session " + session + ": " + failure->message; pendingViews_.remove(session);
        } else savedViews_[session] = std::get<TableViewStates>(result);
        dispatchViewSave(); emit changed();
        if (viewClosePending_ && !viewSaving_ && !presetsBusy_ && pendingViews_.isEmpty() && !viewStateFailed()) emit windowCloseApproved();
    });
    viewSaveFuture_ = QtConcurrent::run([profile = profile_, schemas = tableSchemas(true), session, table, value, expected] {
        return ViewStateStore(profile, schemas).save(session, table, value, expected);
    });
    watcher->setFuture(viewSaveFuture_);
    return true;
}
bool Workspace::reloadSavedViews() {
    if (busy_ || viewSaving_ || !pendingViews_.isEmpty()) return false;
    busy_ = true; emit changed();
    auto* watcher = new QFutureWatcher<LoadedViewStates>(this);
    connect(watcher, &QFutureWatcher<LoadedViewStates>::finished, this, [this, watcher] {
        const auto result = watcher->result(); watcher->deleteLater(); busy_ = false;
        restoreViewStates(result); publish(); emit changed();
    });
    QStringList ids;
    for (const auto& session : catalog_.sessions) ids.append(session.id.toString(QUuid::WithoutBraces));
    watcher->setFuture(QtConcurrent::run([profile = profile_, schemas = tableSchemas(true), ids] {
        LoadedViewStates result;
        for (const auto& id : ids) result.insert(id, ViewStateStore(profile, schemas).load(id));
        return result;
    }));
    return true;
}
bool Workspace::confirmViewClose(bool discard) {
    if (!viewCloseNeedsDecision()) return false;
    viewClosePending_ = false;
    if (discard) { viewCloseDiscardApproved_ = true; emit windowCloseApproved(); }
    emit changed(); return true;
}
}
