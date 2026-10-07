#include "workspace.h"
#include "resource_guidance.h"
#include <QDir>
#include <QStandardPaths>
#include "resource_metrics.h"
#include "kind_glyph.h"
#include "radar_island.h"
#include <qqml.h>
#include <QClipboard>
#include <QColor>
#include <QFutureWatcher>
#include <QEvent>
#include <QGuiApplication>
#include <QFontDatabase>
#include <QJsonDocument>
#include <QStringDecoder>
#include <QtConcurrent/QtConcurrentRun>
#include <algorithm>
#include <tuple>
#include <QRegularExpression>
#include <cmath>

namespace podlord {
namespace {
void cycleSort(int requested, int& column, Qt::SortOrder& order) {
    if (column != requested) { column = requested; order = Qt::AscendingOrder; }
    else if (order == Qt::AscendingOrder) order = Qt::DescendingOrder;
    else { column = -1; order = Qt::AscendingOrder; }
}
QString sessionId(const QUuid& id) { return id.toString(QUuid::WithoutBraces); }
QVariantList sessionRows(const SessionCatalog& catalog, bool onlyOpen) {
    QVariantList result;
    for (const auto& session : catalog.sessions) {
        if (onlyOpen && !session.open) continue;
        result.append(QVariantMap{{"id", sessionId(session.id)}, {"name", session.displayName()}});
    }
    return result;
}
struct DisplayedValue final { QString text, encoding; };
DisplayedValue valueText(const QJsonObject& document, const QString& field, const QString& key) {
    const auto value = document[field].toObject()[key].toString();
    if ((document["kind"] != "Secret" || field != "data") && field != "binaryData") return {value, "Text"};
    const auto bytes = QByteArray::fromBase64(value.toLatin1());
    QStringDecoder decoder(QStringDecoder::Utf8);
    const QString decoded = decoder(bytes);
    const bool controls = std::any_of(decoded.cbegin(), decoded.cend(), [](QChar ch) { return ch.category() == QChar::Other_Control && ch != '\n' && ch != '\r' && ch != '\t'; });
    return decoder.hasError() || controls ? DisplayedValue{value, "Binary (base64)"} : DisplayedValue{decoded, "Text"};
}
bool containsValue(const QJsonObject& document, const QString& id) {
    const auto field = id.section('/', 0, 0), key = id.section('/', 1);
    const bool secret = document["kind"] == "Secret", config = document["kind"] == "ConfigMap";
    return isCoreValueResource(document) && ((secret && (field == "data" || field == "stringData")) || (config && (field == "data" || field == "binaryData")))
        && document[field].toObject().contains(key);
}
} // namespace
ResourceTable::ResourceTable(QObject* parent, QStringList fields, QStringList captions, QString identityField)
    : QAbstractTableModel(parent), fields_(std::move(fields)), captions_(std::move(captions)), identityField_(std::move(identityField)) {}
int ResourceTable::rowCount(const QModelIndex& parent) const { return parent.isValid() ? 0 : static_cast<int>(rows_.size()); }
int ResourceTable::columnCount(const QModelIndex& parent) const { return parent.isValid() ? 0 : static_cast<int>(fields_.size()); }
QVariant ResourceTable::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() >= rows_.size() || index.column() >= fields_.size()) return {};
    const auto& row = rows_[index.row()];
    switch (role) {
    case Qt::UserRole: return row[identityField_].toString();
    case Qt::UserRole + 1: return row["name"].toString();
    case Qt::UserRole + 2: return row["kind"].toString();
    case Qt::UserRole + 3: return row["namespace"].toString();
    case Qt::UserRole + 4: return row["status"].toString();
    case Qt::UserRole + 5: return appearanceStatus(appearance_, row["status"].toString());
    case Qt::UserRole + 7: return resourceMetricPresentation(row);
    case Qt::UserRole + 8: return row.contains("metricReferences");
    case Qt::UserRole + 9: return row["problemSeverity"].toInt();
    case Qt::UserRole + 10: return row["activity"].toBool();
    case Qt::UserRole + 13: return row["value"].toString();
    case Qt::UserRole + 14: return row["base64"].toBool();
    default: break;
    }
    const auto& field = fields_[index.column()];
    const auto cell = row[field];
    const auto text = field == "cluster" ? cluster_ : cell.toString();
    const bool presentation = role == Qt::DisplayRole || role == Qt::ToolTipRole;
    const bool age = field == "createdAt" && (presentation || role == Qt::UserRole + 6 || role == Qt::UserRole + 11);
    const auto created = age ? QDateTime::fromString(text, Qt::ISODateWithMs) : QDateTime{};
    const auto now = age ? QDateTime::currentDateTimeUtc() : QDateTime{};
    if (presentation) {
        const bool metric = field == "cpu" || field == "memory" || field == "storage";
        QString value = cell.isDouble() ? QString::number(cell.toDouble(), 'g', 16) : text;
        if (metric) {
        value = cell.isDouble() ? formatMetricQuantity(cell.toDouble(), field == "cpu") : "-";
        if (cell.isDouble() && !row["metricComplete"].toObject()[field].toBool()) value += " (incomplete)";
        if (cell.isDouble() && row["metricStale"].toBool()) value += " (stale)";
    } else if (field == "createdAt") {
        if (!created.isValid() || created > now) value = "-";
        else {
            const auto seconds = created.secsTo(now);
            value = seconds >= 86400 ? QString::number(seconds/86400)+"d" : seconds >= 3600 ? QString::number(seconds/3600)+"h"
                : seconds >= 60 ? QString::number(seconds/60)+"m" : QString::number(seconds)+"s";
        }
    } else if ((field == "restarts" || field == "ready" || field == "owner" || field == "issue" || field == "cluster") && value.isEmpty()) value = "-";
        if (role == Qt::DisplayRole) return value;
        return "<span>" + value.toHtmlEscaped().replace('\n', "<br>") + "</span>";
    }
    if (role == Qt::UserRole + 12) return field == "from" || field == "to" ? row[field + "Path"].toString() : row[identityField_].toString();
    if (role == Qt::ForegroundRole) {
        if (field=="status") return appearanceStatus(appearance_,text);
        static const QStringList identities{"kind", "namespace", "status", "node", "image", "cluster", "eventType", "type", "from", "to"};
        return identities.contains(field) && !text.isEmpty() ? appearanceIdentity(appearance_, text) : QColor(Qt::transparent);
    }
    if (role == Qt::UserRole + 6) {
        if (field == "preview") return row["value"].toString();
        if (field == "eventTime") return QDateTime::fromString(text, Qt::ISODateWithMs);
        if (field == "createdAt") return created.isValid() && created <= now ? QVariant(-created.toMSecsSinceEpoch()) : QVariant{};
        if (field == "ready") return !text.isEmpty() && row["containerCount"].toInt() > 0
            ? QVariant(double(row["readyCount"].toInt())/row["containerCount"].toInt()) : QVariant{};
        return field == "cluster" ? QVariant(text) : cell.toVariant();
    }
    if (role == Qt::UserRole + 11) return age && created.isValid() && created <= now ? QVariant(created.secsTo(now)) : QVariant{};
    return {};
}
QVariant ResourceTable::headerData(int section, Qt::Orientation orientation, int role) const {
    if (orientation == Qt::Horizontal && role == Qt::UserRole && section >= 0 && section < fields_.size()) return fields_[section];
    return orientation == Qt::Horizontal && role == Qt::DisplayRole && section >= 0 && section < captions_.size() ? QVariant(captions_[section]) : QVariant{};
}
QHash<int, QByteArray> ResourceTable::roleNames() const {
    return {{Qt::DisplayRole, "display"}, {Qt::ToolTipRole, "tooltip"}, {Qt::ForegroundRole, "identityColor"}, {Qt::UserRole, "resourcePath"},
        {Qt::UserRole + 1, "resourceName"}, {Qt::UserRole + 2, "resourceKind"}, {Qt::UserRole + 3, "resourceNamespace"},
        {Qt::UserRole + 4, "resourceStatus"}, {Qt::UserRole + 5, "statusColor"}, {Qt::UserRole + 6, "sortValue"}, {Qt::UserRole + 7, "resourceMetrics"}, {Qt::UserRole + 8, "hasResourceMetrics"}, {Qt::UserRole + 9, "resourceHealth"}, {Qt::UserRole + 12, "endpointPath"}, {Qt::UserRole + 13, "valueDetail"}, {Qt::UserRole + 14, "encodedValue"}};
}
QJsonObject ResourceTable::row(int index) const { return index >= 0 && index < rows_.size() ? rows_[index] : QJsonObject{}; }
bool ResourceTable::setAppearance(const Appearance& appearance) {
    if (appearance_ == appearance) return true;
    appearance_ = appearance;
    if (!rows_.isEmpty()) emit dataChanged(index(0, 0), index(rowCount() - 1, columnCount() - 1), {Qt::ForegroundRole, Qt::UserRole + 5});
    return true;
}
bool ResourceTable::publish(const QJsonArray& rows, const QString& cluster) {
    const bool clusterChanged = cluster_ != cluster;
    cluster_ = cluster;
    QMap<QString, QJsonObject> incoming;
    for (const auto& value : rows) { const auto row = value.toObject(); incoming.insert(row[identityField_].toString(), row); }
    for (int last = static_cast<int>(rows_.size()) - 1; last >= 0;) {
        if (incoming.contains(rows_[last][identityField_].toString())) { --last; continue; }
        int first = last;
        while (first > 0 && !incoming.contains(rows_[first-1][identityField_].toString())) --first;
        beginRemoveRows({}, first, last); rows_.remove(first, last-first+1); endRemoveRows();
        last = first-1;
    }
    int firstChanged = -1, lastChanged = -1, firstColumn = columnCount(), lastColumn = -1;
    for (int index = 0; index < rows_.size(); ++index) {
        const auto value = incoming.take(rows_[index][identityField_].toString());
        if (value != rows_[index]) {
            const auto& previous = rows_[index];
            for (int column = 0; column < fields_.size(); ++column) {
                const auto& field = fields_[column];
                const bool metric = field == "cpu" || field == "memory" || field == "storage";
                const bool changed = field == "cluster" ? clusterChanged
                    : field == "createdAt" || value[field] != previous[field]
                        || (field == "preview" && value["value"] != previous["value"])
                        || (metric && (value["metricComplete"] != previous["metricComplete"] || value["metricStale"] != previous["metricStale"]));
                if (changed) { firstColumn = std::min(firstColumn,column); lastColumn = std::max(lastColumn,column); }
            }
            rows_[index] = value;
            if (firstChanged < 0) firstChanged = index;
            lastChanged = index;
        }
    }
    const int firstMetadataChanged = firstChanged, lastMetadataChanged = lastChanged;
    if (clusterChanged && fields_.contains("cluster") && !rows_.isEmpty()) {
        firstChanged = 0; lastChanged = rowCount()-1;
        const int column = int(fields_.indexOf("cluster"));
        firstColumn = std::min(firstColumn,column); lastColumn = std::max(lastColumn,column);
    }
    QList<int> roles;
    if (lastColumn >= 0) roles = {Qt::DisplayRole, Qt::ToolTipRole, Qt::ForegroundRole, Qt::UserRole+6, Qt::UserRole+11};
    // Row-wide predicates depend on metadata as well as the displayed filter column.
    if (firstMetadataChanged >= 0) {
        firstChanged = clusterChanged ? 0 : firstMetadataChanged;
        lastChanged = clusterChanged ? rowCount()-1 : lastMetadataChanged;
        firstColumn = 0; lastColumn = columnCount()-1;
        roles.append({Qt::UserRole, Qt::UserRole+1, Qt::UserRole+2, Qt::UserRole+3, Qt::UserRole+4,
            Qt::UserRole+5, Qt::UserRole+7, Qt::UserRole+8, Qt::UserRole+9, Qt::UserRole+10,
            Qt::UserRole+12, Qt::UserRole+13, Qt::UserRole+14});
    } else if (clusterChanged) roles.append(Qt::UserRole);
    if (firstChanged >= 0 && lastColumn >= 0)
        emit dataChanged(index(firstChanged,firstColumn),index(lastChanged,lastColumn),roles);
    for (auto next = incoming.cbegin(); next != incoming.cend();) {
        const auto position = std::lower_bound(rows_.cbegin(), rows_.cend(), next.key(), [this](const auto& row, const auto& key) { return row[identityField_].toString() < key; });
        int index = static_cast<int>(position - rows_.cbegin());
        QList<QJsonObject> added;
        do { added.append(next.value()); ++next; }
        while (next != incoming.cend() && (position == rows_.cend() || next.key() < (*position)[identityField_].toString()));
        beginInsertRows({}, index, index+added.size()-1);
        for (const auto& value : added) rows_.insert(index++,value);
        endInsertRows();
    }
    return true;
}
Workspace::Workspace(QString profile, QObject* parent, std::function<QDateTime()> now) : QObject(parent), profile_(std::move(profile)), client_(nullptr, now), alerts_(profile_, &client_, this, std::move(now)),
    eventRows_(nullptr, {"eventTime", "eventType", "eventReason", "namespace", "eventTargetName", "eventCount", "eventMessage"},
        {"Last observed", "Type", "Reason", "Namespace", "Regarding", "Count", "Message"}),
    portRows_(nullptr, {"endpoint", "name", "kind", "namespace", "remotePort", "resolvedPort", "status"},
        {"Local endpoint", "Name", "Kind", "Namespace", "Remote", "Resolved", "Status"}, "id"),
    inspectorEventRows_(nullptr, {"time", "type", "reason", "count", "message"}, {"Last observed", "Type", "Reason", "Count", "Message"}),
    inspectorLinkRows_(nullptr, {"from", "relation", "to", "namespace", "status"}, {"From", "Link", "To", "Namespace", "Status"}),
    valueRows_(nullptr, {"name", "encoding", "preview", "copy", "reveal"}, {"Key", "Encoding", "Value", "Copy", "Reveal"}, "id") {
    valuesTable_.setSourceModel(&valueRows_);
    valuesTable_.setSortRole(Qt::UserRole + 6);
    valuesTable_.setSortCaseSensitivity(Qt::CaseInsensitive);
    inspectorEventsTable_.setSourceModel(&inspectorEventRows_); inspectorEventsTable_.setSortRole(Qt::UserRole + 6);
    inspectorLinksTable_.setSourceModel(&inspectorLinkRows_); inspectorLinksTable_.setSortRole(Qt::UserRole + 6);
    static const int glyphType = qmlRegisterType<KindGlyph>("Podlord.Graphics", 1, 0, "KindGlyph");
    Q_UNUSED(glyphType);
    static const int islandType = qmlRegisterType<RadarIsland>("Podlord.Graphics", 1, 0, "RadarIsland");
    Q_UNUSED(islandType);
    static const int waterType = qmlRegisterType<RadarWater>("Podlord.Graphics", 1, 0, "RadarWater");
    Q_UNUSED(waterType);
    static const int terminalType = qmlRegisterType<TerminalSurface>("Podlord.Graphics", 1, 0, "TerminalSurface");
    Q_UNUSED(terminalType);
    static const int terminalSessionType = qmlRegisterUncreatableType<ContainerTerminal>("Podlord.Graphics", 1, 0, "ContainerTerminal", "Owned by the selected session");
    Q_UNUSED(terminalSessionType);
    static const int findType = qmlRegisterType<ResourceFilter>("Podlord.Graphics", 1, 0, "ResourceFindFilter");
    Q_UNUSED(findType);
    client_.enableRequests(false);
    connect(&client_, &ResourceClient::requestStarted, this, &Workspace::requestStarted);
    connect(&client_, &ResourceClient::portForwardsChanged, this, [this](const QString& id) { if (id == active_) { publishPorts(); emit changed(); } });
    connect(&client_, &ResourceClient::terminalChanged, this, [this](const QString& id) { if (id == active_) emit changed(); });
    publishAppearance();
    bindYamlApply();
    bindDeletion();
    connect(this, &Workspace::yamlTextChanged, this, &Workspace::clearYamlCheck);
    connect(this, &Workspace::yamlEditChanged, this, &Workspace::clearYamlCheck);
    if (auto* app = qobject_cast<QGuiApplication*>(QCoreApplication::instance())) {
        app->installEventFilter(this);
        client_.setFocused(app->applicationState() == Qt::ApplicationActive);
        connect(app, &QGuiApplication::applicationStateChanged, this, [this](Qt::ApplicationState state) { client_.setFocused(state == Qt::ApplicationActive); });
    }
    table_.setSourceModel(&rows_);
    events_.setSourceModel(&eventRows_);
    ports_.setSourceModel(&portRows_);
    ports_.setSortRole(Qt::UserRole + 6);
    ports_.setFilterKeyColumn(-1);
    ports_.setFilterCaseSensitivity(Qt::CaseInsensitive);
    connect(&table_, &ResourceFilter::filterStateChanged, this, &Workspace::changed);
    connect(&events_, &ResourceFilter::filterStateChanged, this, &Workspace::changed);
    dashboardRows_.setSourceModel(&table_);
    dashboardRows_.setFilterRole(Qt::UserRole + 8);
    dashboardRows_.setFilterRegularExpression("^true$");
    reloadTableLayouts();
    reloadFilterPresets();
    connect(&client_, &ResourceClient::changed, this, [this](const QString& id) { if (id == active_) { publishInspector(); emit changed(); } });
    connect(&client_, &ResourceClient::rowsChanged, this, [this](const QString& id) { if (id == active_) publish(); });
    connect(&client_, &ResourceClient::detailFinished, this, [this](const QString& id, const QString& path, bool accepted) {
        if (id != active_ || path != inspectorPath()) return;
        publishInspector(); yamlFresh_ = accepted; emit yamlEditChanged();
    });
    connect(&client_, &ResourceClient::logsChanged, this, [this](const QString& id, const QString& path) {
        if (id == active_ && path == navigation_.value(active_).inspected) { publishLogs(); emit changed(); }
    });
    connect(&client_, &ResourceClient::authenticationRejected, &credentials_, &CredentialProcess::invalidate);
    connect(&credentials_, &CredentialProcess::changed, this, &Workspace::changed);
    connect(&credentials_, &CredentialProcess::completed, this, [this](const QString& credential, const QString& session) {
        const auto result = credentials_.result(credential);
        if (const auto* connection = std::get_if<ClusterConnection>(&result)) {
            const auto current = client_.connection(active_);
            client_.authenticate(*connection, current && current->credentialId == credential ? active_ : session);
        }
        emit changed();
    });
    reload();
}
QVariantList Workspace::contexts() const {
    QVariantList result;
    for (const auto& source : sources_.sources)
        for (const auto& context : source.contexts)
            result.append(QVariantMap{{"id", context.id}, {"name", context.displayName}, {"context", context.name}, {"source", source.sourcePath},
                {"auth", context.authType}, {"usable", context.brokenReferences.isEmpty()}, {"cluster",context.cluster}, {"user",context.user},
                {"imported",source.importedAt.toString(Qt::ISODate)}, {"detail",context.brokenReferences.join(", ")}});
    return result;
}
QVariantList Workspace::sessions() const { return sessionRows(selection_, false); }
QVariantList Workspace::tabs() const { return sessionRows(catalog_, true); }
bool Workspace::busy() const { return busy_; }
bool Workspace::loading() const { return authenticationRunning() || client_.loading(active_); }
bool Workspace::authenticationRequired() const { return client_.authenticationRequired(active_); }
bool Workspace::authenticationRunning() const {
    const auto connection = client_.connection(active_);
    return connection && credentials_.running(connection->credentialId);
}
QString Workspace::authenticationPrompt() const {
    const auto connection = client_.connection(active_);
    if (!connection) return {};
    return "Session: " + title() + "\nCluster: " + connection->server.toString()
        + (connection->exec ? "\nCommand: " + connection->exec->program + "\nThis runs the imported configuration's command and arguments with your permissions. Only trust configurations you know. Login may open a browser." : "\nConfirm a new authentication attempt.");
}
QString Workspace::error() const {
    if (!settingsError_.isEmpty()) return settingsError_;
    if (viewStateFailed()) return viewStateError();
    if (const auto connection = client_.connection(active_); connection && connection->exec && !authenticationRunning()) {
        const auto result = credentials_.result(connection->credentialId);
        if (const auto* failure = std::get_if<Failure>(&result); failure && failure->code != StoreError::NotFound) return failure->message;
    }
    return error_.isEmpty() ? client_.portForwardError(active_) : error_;
}
QString Workspace::status() const { return authenticationRunning() ? "Authentication running; cached resources remain available." : client_.status(active_); }
QString Workspace::currentSession() const { return active_; }
QString Workspace::title() const {
    for (const auto& session : catalog_.sessions) if (sessionId(session.id) == active_) return session.displayName();
    return "Resources";
}
QString Workspace::filterText() const { return navigation_.value(active_).filter; }
int Workspace::sortColumnIndex() const { return navigation_.value(active_).column; }
QString Workspace::sortDirection() const { return navigation_.value(active_).order == Qt::AscendingOrder ? "ASC" : "DESC"; }
QString Workspace::inspected() const {
    return overview_;
}
QString Workspace::inspectorPath() const { return navigation_.value(active_).inspected; }
QString Workspace::inspectorName() const { return inspectorSummary_.value("name").toString(); }
QString Workspace::inspectorScope() const { return inspectorScope_; }
QString Workspace::inspectorStatus() const { return client_.detailStatus(active_, inspectorPath()); }
QString Workspace::yamlText() const { return yamlDraft_ ? yamlDraft_->text : yaml_; }
bool Workspace::canEditYaml() const {
    const auto metadata = inspectorDocument_.value("metadata").toObject();
    return !yamlDraft_ && yamlFresh_ && windowVisible_ && !authenticationRequired() && !authenticationRunning()
        && !metadata.value("uid").toString().isEmpty() && !metadata.value("resourceVersion").toString().isEmpty();
}
bool Workspace::yamlEditing() const { return yamlDraft_.has_value(); }
bool Workspace::yamlDirty() const { return yamlDraft_ && yamlDraft_->text != yamlDraft_->baseline; }
QString Workspace::yamlDraftStatus() const {
    if (!yamlDraft_) return canEditYaml() ? "Fresh YAML ready. Start editing explicitly." : "Read-only. A successful fresh detail read with resource identity and version is required.";
    if (inspectorDocument_.isEmpty()) return "Current server detail is unavailable. Your local draft is preserved.";
    const auto baseline = yamlDraft_->document.value("metadata").toObject(), current = inspectorDocument_.value("metadata").toObject();
    if (baseline.value("uid") != current.value("uid") || baseline.value("resourceVersion") != current.value("resourceVersion"))
        return "Server resource version changed. Your local draft is preserved.";
    return yamlDirty() ? "Unapplied YAML draft. Changes are never sent automatically." : "Editing fresh YAML. Changes are never sent automatically.";
}
bool Workspace::discardPending() const { return pendingLeave_.has_value(); }
QString Workspace::discardPrompt() const {
    if (!pendingLeave_ || !yamlDraft_) return {};
    return "Discard unapplied YAML changes for " + yamlDraft_->document.value("metadata").toObject().value("name").toString()
        + "?\nStay keeps your draft. Discard continues the originally requested action without applying it.";
}
bool Workspace::beginYamlEdit() {
    if (yamlApplyLocked() || deletionPending() || deletionRunning()) return false;
    if (busy_ || !canEditYaml() || pendingLeave_) return false;
    yamlDraft_ = YamlDraft{active_, inspectorPath(), inspectorDocument_, yaml_, yaml_};
    emit yamlEditChanged(); emit yamlTextChanged(); return true;
}
bool Workspace::setYamlDraft(const QString& text) {
    if (yamlApplyLocked()) return false;
    if (!yamlDraft_ || pendingLeave_ || yamlDraft_->session != active_ || yamlDraft_->path != inspectorPath()) return false;
    if (yamlDraft_->text == text) return true;
    const bool dirty = yamlDirty(); yamlDraft_->text = text;
    emit yamlTextChanged(); if (dirty != yamlDirty()) emit yamlEditChanged(); return true;
}
bool Workspace::clearYamlDraft() {
    if (yamlApply_) {
        const auto token = yamlApply_->token;
        yamlApply_.reset();
        client_.cancelYamlApply(token);
        emit yamlConflictsChanged();
        emit yamlApplyChanged();
    }
    if (!yamlDraft_) return true;
    yamlDraft_.reset(); emit yamlTextChanged(); emit yamlEditChanged(); return true;
}
bool Workspace::allowLeave(Leave action, const QString& target, int historyIndex) {
    if (pendingLeave_) return false;
    cancelPendingDeletion();
    if (!yamlDirty()) return clearYamlDraft();
    pendingLeave_ = PendingLeave{action, target, active_, historyIndex}; emit changed(); return false;
}
bool Workspace::leaveYamlEdit() { return allowLeave(Leave::Draft) && clearYamlDraft(); }
bool Workspace::requestWindowClose() {
    if (!allowLeave(Leave::Window)) { viewCloseDiscardApproved_ = false; return false; }
    if (viewCloseDiscardApproved_) return true;
    if (viewSaving_ || presetsBusy_ || !pendingViews_.isEmpty() || viewStateFailed()) {
        viewClosePending_ = true; emit changed(); return false;
    }
    return true;
}
bool Workspace::confirmDiscard(bool discard) {
    if (!pendingLeave_) return false;
    const auto pending = *pendingLeave_; pendingLeave_.reset();
    if (!discard) { emit changed(); return true; }
    if (pending.origin != active_) { error_ = "The originating session changed. Your draft is preserved; request the action again."; emit changed(); return false; }
    clearYamlDraft(); emit changed();
    switch (pending.action) {
    case Leave::Resource: return inspectResource(pending.target);
    case Leave::Session: return activate(pending.target);
    case Leave::Context: return openContext(pending.target);
    case Leave::Tab: return close(pending.target);
    case Leave::Inspector: return closeInspector();
    case Leave::Window: emit windowCloseApproved(); return true;
    case Leave::Draft: return true;
    case Leave::Reload: return reload();
    case Leave::History: return inspectHistory(pending.target, pending.historyIndex);
    }
    return false;
}
QVariantList Workspace::resourceValues() const {
    QVariantList result;
    for (int row = 0; row < valueRows_.rowCount(); ++row) result.append(valueRows_.row(row).toVariantMap());
    return result;
}
bool Workspace::yamlVisible() const { return navigation_.value(active_).page == "yaml"; }
bool Workspace::valuesVisible() const { return navigation_.value(active_).page == "values" && valuesAvailable(); }
bool Workspace::valuesAvailable() const { return isCoreValueResource(client_.resource(active_, inspectorPath())); }
QString Workspace::monospaceFamily() const { return QFontDatabase::families().contains("Menlo") ? QString("Menlo") : QFontDatabase::systemFont(QFontDatabase::FixedFont).family(); }
QVariantMap Workspace::settingsDiagnostics() const {
    const QVariantList metrics{
        QVariantMap{{"label","Cached resources"},{"value",rows_.rowCount()},{"description","Current session snapshot, before view filters"}},
        QVariantMap{{"label","Matching resources"},{"value",table_.rowCount()},{"description","Current cached filter result"}},
        QVariantMap{{"label","Cached Events"},{"value",eventRows_.rowCount()},{"description","Canonical Events, without duplicate API aliases"}},
        QVariantMap{{"label","Open sessions"},{"value",tabs().size()},{"description","Closed sessions do not synchronize"}},
        QVariantMap{{"label","Sync progress"},{"value",QString::number(loadingProgress()*100,'f',0)+"%"},{"description",status()}},
        QVariantMap{{"label","Request limit"},{"value",settings_.requestHardLimitPerMinute},{"description","Per minute; 0 adds no extra limit; reads have at least 400 ms spacing"}},
        QVariantMap{{"label","Retained logs"},{"value",QString::number(settings_.logLimitMb)+" MB / Pod"},{"description","Bounded in-memory log retention"}}};
    return {{"metrics",metrics},{"requests",client_.requestAudit(active_)}};
}
bool Workspace::publishInspector(bool revealChanged) {
    const auto path = inspectorPath();
    auto summary = path.isEmpty() ? QJsonObject{} : client_.resource(active_, path);
    const auto document = path.isEmpty() ? QJsonObject{} : client_.document(active_, path);
    const auto scope = path.isEmpty() ? QString{} : active_ + '\n' + path + '\n' + summary["uid"].toString();
    const bool scopeChanged = scope != inspectorScope_;
    const bool documentChanged = document != inspectorDocument_;
    if (!path.isEmpty() && summary.isEmpty()) summary = {{"path", path}, {"name", QUrl::fromPercentEncoding(path.section('/', -1).toLatin1())}};
    if (!summary.isEmpty()) summary["cluster"] = activeCluster();
    if (!revealChanged && scope == inspectorScope_ && summary == inspectorSummary_ && document == inspectorDocument_) return true;
    emit inspectorPresentationChanging();
    if (scopeChanged) yamlFresh_ = false;
    if (yamlDraft_ && (yamlDraft_->session != active_ || yamlDraft_->path != path)) clearYamlDraft();
    if (scopeChanged || document.isEmpty()) revealedValues_.clear();
    inspectorScope_ = scope; inspectorSummary_ = summary;
    overviewFields_.clear();
    QStringList overview;
    const QStringList keys{"name", "kind", "namespace", "cluster", "status", "issue", "ready", "restarts", "owner", "node", "image", "createdAt", "uid", "resourceVersion", "fetchedAt"};
    const QStringList labels{"Name", "Kind", "Namespace", "Cluster", "Status", "Issue", "Ready", "Restarts", "Owner", "Node", "Image", "Created", "UID", "Version", "Detail retrieved"};
    for (int index = 0; !summary.isEmpty() && index < keys.size(); ++index) {
        const auto cell = summary[keys[index]];
        auto value = cell.isDouble() ? QString::number(cell.toDouble(), 'g', 15) : cell.toString();
        if (value.isEmpty() && keys[index] == "namespace") value = "Cluster-scoped";
        if (value.isEmpty()) continue;
        QVariantMap field{{"id", keys[index]}, {"label", labels[index]}, {"value", value}};
        if (keys[index] == "ready" && summary.contains("readyCount") && summary.contains("containerCount")) {
            const int ready = summary["readyCount"].toInt(), desired = summary["containerCount"].toInt();
            field["readiness"] = QVariantMap{{"fraction", desired > 0 ? std::clamp(double(ready)/desired, 0.0, 1.0) : 0.0},
                {"tone", desired == 0 ? "unknown" : ready >= desired ? "success" : ready == 0 ? "danger" : "warning"},
                {"description", QString("%1 ready: %2 of %3").arg(summary["apiVersion"] == "v1" && summary["kind"] == "Pod" ? "Containers" : "Replicas").arg(ready).arg(desired)}};
        }
        overviewFields_.append(field);
        overview.append(keys[index] + ": " + value);
    }
    overview_ = overview.join('\n');
    for (const auto& value : resourceMetricPresentation(summary)) {
        const auto metric=value.toMap();
        overviewFields_.append(QVariantMap{{"id", "metric_"+metric["id"].toString()}, {"label", metric["label"]}, {"value", metric["usage"]}, {"metric", metric}});
    }
    if (revealChanged || scopeChanged || documentChanged || document.isEmpty()) {
        inspectorDocument_ = document;
        if (documentChanged) yaml_ = resourceYaml(document);
        QJsonArray resourceValues;
        const bool secret = document["kind"] == "Secret";
        if (isCoreValueResource(document))
            for (const auto& field : secret ? QStringList{"data", "stringData"} : QStringList{"data", "binaryData"}) {
                const auto values = document[field].toObject();
                for (auto value = values.constBegin(); value != values.constEnd(); ++value) {
                    const auto id = field + '/' + value.key();
                    const bool shown = !secret || revealedValues_.contains(id);
                    const auto display = shown ? valueText(document, field, value.key()) : DisplayedValue{"[hidden]", "Hidden"};
                    resourceValues.append(QJsonObject{{"id", id}, {"name", value.key()}, {"field", field}, {"secret", secret}, {"revealed", shown},
                        {"value", display.text}, {"preview", display.text.left(256)}, {"encoding", display.encoding}, {"copy", "Copy"}, {"base64", field == "binaryData" || (secret && field == "data")},
                        {"reveal", secret ? shown ? "Hide" : "Reveal" : ""}});
                }
            }
        valueRows_.publish(resourceValues);
    }
    valuesTable_.sort(navigation_.value(active_).valueColumn, navigation_.value(active_).valueOrder);
    emit inspectorPresentationChanged();
    if (!yamlDraft_ && (documentChanged || scopeChanged)) emit yamlTextChanged();
    if (documentChanged || scopeChanged) emit yamlEditChanged();
    if (scopeChanged || documentChanged) publishRelated();
    return true;
}
bool Workspace::publishRelated() {
    QVariantList events, links;
    const auto selected = client_.resource(active_, inspectorPath());
    const auto uid = selected["uid"].toString();
    if (!uid.isEmpty()) for (int index = 0; index < rows_.rowCount(); ++index) {
        const auto row = rows_.row(index);
        if (row["kubernetesEvent"].toBool() && row["eventTargetUid"] == uid)
            events.append(QVariantMap{{"path", row["path"].toString()}, {"type", row["eventType"].toString()}, {"reason", row["eventReason"].toString()},
                {"message", row["eventMessage"].toString()}, {"count", row["eventCount"].toDouble()}, {"time", row["eventTime"].toString()}});
        if (row["path"] == selected["path"] || row["uid"].toString().isEmpty()) continue;
        QString relation;
        const auto references = [](const QJsonObject& child, const QJsonObject& parent) {
            if (!parent["namespace"].toString().isEmpty() && child["namespace"] != parent["namespace"]) return false;
            for (const auto& value : child["owners"].toArray()) {
                const auto owner = value.toObject();
                if (owner["uid"] == parent["uid"] && owner["kind"] == parent["kind"] && owner["apiVersion"] == parent["apiVersion"]) return true;
            }
            return false;
        };
        if (references(selected, row)) relation = "Owner";
        else if (references(row, selected)) relation = "Dependent";
        else if (row["apiVersion"] == "v1" && row["kind"] == "Node" && selected["apiVersion"] == "v1" && selected["kind"] == "Pod" && row["name"] == selected["node"]) relation = "Node";
        else if (row["apiVersion"] == "v1" && row["kind"] == "Namespace" && row["name"] == selected["namespace"]) relation = "Namespace";
        if (!relation.isEmpty()) {
            const bool parent = relation != "Dependent";
            links.append(QVariantMap{{"path", row["path"].toString()}, {"relation", relation}, {"name", row["name"].toString()},
                {"kind", row["kind"].toString()}, {"namespace", row["namespace"].toString()},
                {"from", (parent ? row : selected)["name"].toString()}, {"to", (parent ? selected : row)["name"].toString()},
                {"fromPath", (parent ? row : selected)["path"].toString()}, {"toPath", (parent ? selected : row)["path"].toString()},
                {"status", (parent ? selected : row)["status"].toString()}});
        }
    }
    const auto nav = navigation_.value(active_);
    inspectorEventsTable_.sort(nav.inspectorEventColumn, nav.inspectorEventOrder);
    inspectorLinksTable_.sort(nav.inspectorLinkColumn, nav.inspectorLinkOrder);
    if (events == inspectorEvents_ && links == inspectorLinks_) return true;
    inspectorEvents_ = std::move(events); inspectorLinks_ = std::move(links);
    inspectorEventRows_.publish(QJsonArray::fromVariantList(inspectorEvents_));
    inspectorLinkRows_.publish(QJsonArray::fromVariantList(inspectorLinks_));
    emit inspectorRelatedChanged(); return true;
}
bool Workspace::setInspectorPage(const QString& page) {
    if (inspectorPath().isEmpty()) return false;
    if (page == "logs") return setLogsVisible(true);
    if (page != "overview" && page != "yaml" && page != "values" && page != "events" && page != "links" && page != "terminal") return false;
    if (page == "terminal" && !podInspected() && !containerTerminal()) return false;
    if (page == "values" && !valuesAvailable()) return false;
    if (navigation_.value(active_).page == page) return true;
    client_.hideLogs(); navigation_[active_].page = page;
    publishLogs(); emit changed();
    if (page == "yaml" && !yamlDraft_ && inspectorDocument_.isEmpty()) refreshInspector();
    return true;
}
bool Workspace::closeInspector() {
    if (!allowLeave(Leave::Inspector)) return false;
    client_.dismissInspector(active_); client_.hideLogs();
    navigation_[active_].inspected.clear(); navigation_[active_].page = "overview";
    publishInspector(); publishLogs(); emit changed(); return true;
}
bool Workspace::refreshInspector() {
    if (inspectorPath().isEmpty()) return false;
    yamlFresh_ = false; emit yamlEditChanged(); return client_.inspect(active_, inspectorPath());
}
bool Workspace::copyYaml() {
    if (yamlDraft_) return false;
    if (yaml_.isEmpty()) return false;
    QGuiApplication::clipboard()->setText(yaml_); return true;
}
bool Workspace::revealValue(const QString& id, bool revealed) {
    if (inspectorDocument_["kind"] != "Secret" || !containsValue(inspectorDocument_, id)) return false;
    if (revealed) revealedValues_.insert(id); else revealedValues_.remove(id);
    return publishInspector(true);
}
bool Workspace::previewAlertZoom(const QVariantMap& draft) {
    QStringList visible;
    for (int index = 0; index < table_.rowCount(); ++index) visible.append(table_.index(index, 0).data(Qt::UserRole).toString());
    return alerts_.previewZoom(draft, visible);
}
bool Workspace::copyValue(const QString& id, const QString& representation) {
    if (!containsValue(inspectorDocument_, id)) return false;
    const auto field = id.section('/', 0, 0), key = id.section('/', 1);
    QString copied;
    if (representation == "key") copied = key;
    else if (representation == "raw") copied = inspectorDocument_[field].toObject()[key].toString();
    else if (representation == "preferred" || representation == "decoded") {
        const auto value = valueText(inspectorDocument_, field, key);
        if (representation == "decoded" && value.encoding != "Text") return false;
        copied = value.text;
    } else return false;
    QGuiApplication::clipboard()->setText(copied);
    return true;
}
QAbstractItemModel* Workspace::table() { return &table_; }
int Workspace::requestLimit() const { return settings_.requestHardLimitPerMinute; }
int Workspace::inactiveSyncMinutes() const { return settings_.inactiveSyncMinutes; }
bool Workspace::saveReadSettings(int requestLimit, int inactiveMinutes, const QString& value) {
    static const QRegularExpression integer("^[1-9][0-9]*$");
    bool valid = false;
    const int limit = value.toInt(&valid);
    if (!valid || !integer.match(value).hasMatch()) { settingsError_ = "Log-size limit must be a positive whole number of MB."; emit changed(); return false; }
    auto desired = settings_; desired.requestHardLimitPerMinute = requestLimit; desired.inactiveSyncMinutes = inactiveMinutes; desired.logLimitMb = limit;
    return savePolicy(desired);
}
bool Workspace::savePolicy(ReadSettings desired) {
    if (busy_) return false;
    if (!desired.valid()) { settingsError_ = "Invalid request settings."; emit changed(); return false; }
    busy_ = true; emit changed();
    auto* watcher = new QFutureWatcher<Result<ReadSettings>>(this);
    connect(watcher, &QFutureWatcher<Result<ReadSettings>>::finished, this, [this, watcher] {
        const auto result = watcher->result(); watcher->deleteLater(); busy_ = false;
        const auto previousLanguage = settings_.language;
        if (const auto* failure = std::get_if<Failure>(&result)) settingsError_ = failure->message;
        else { settings_ = std::get<ReadSettings>(result); settingsError_.clear(); settingsReady_ = true; publishAppearance(); client_.configure(settings_); client_.enableRequests(true); }
        emit changed(); emit appearanceChanged();
        if (previousLanguage != settings_.language || std::holds_alternative<Failure>(result)) emit languageChanged();
    });
    const auto profile = profile_; const auto expected = settings_;
    watcher->setFuture(QtConcurrent::run([profile, desired, expected] { return ReadSettingsStore(profile).save(desired, expected); }));
    return true;
}
bool Workspace::saveRadarWater(bool enabled, int speedPercent) {
    auto desired=settings_; desired.radarWaterEnabled=enabled; desired.radarWaterSpeedPercent=speedPercent;
    return savePolicy(desired);
}
bool Workspace::saveWorkspaceRestore(bool enabled) {
    auto desired = settings_; desired.workspaceRestore = enabled;
    return savePolicy(desired);
}
bool Workspace::podInspected() const { return client_.resource(active_, navigation_.value(active_).inspected)["kind"] == "Pod"; }
bool Workspace::logsVisible() const { return windowVisible_ && navigation_.value(active_).page == "logs" && podInspected(); }
bool Workspace::logsPaused() const { return client_.logsPaused(active_, navigation_.value(active_).inspected); }
QVariantList Workspace::logContainers() const {
    QVariantList options;
    for (const auto& id : client_.logContainers(active_, navigation_.value(active_).inspected))
        options.append(QVariantMap{{"id", id}, {"name", id == "*" ? "all" : id == "all" ? "all (container)" : id}});
    return options;
}
QString Workspace::logSelection() const { return client_.logSelection(active_, navigation_.value(active_).inspected); }
QString Workspace::logStatus() const { return client_.logStatus(active_, navigation_.value(active_).inspected); }
int Workspace::logLimitMb() const { return settings_.logLimitMb; }
QAbstractItemModel* Workspace::logRows() { return &logs_; }
QString Workspace::logAnchor() const { return logPositions_.value(logScope_).anchor; }
double Workspace::logOffset() const { return logPositions_.value(logScope_).offset; }
bool Workspace::logFollow() const { return logPositions_.value(logScope_).follow; }
QString Workspace::logPositionNotice() const {
    const auto position = logPositions_.value(logScope_);
    if (!position.evicted) return {};
    const auto reason = position.removal == PodLogHistory::Removal::Limit ? "was removed by the log-size limit" : position.removal == PodLogHistory::Removal::Expiry ? "was removed by cache expiry" : "is no longer retained";
    return QString("The section being read %1. Showing the oldest retained entry; live following remains off.").arg(reason);
}
bool Workspace::rememberLogPosition(const QString& anchor, double offset, bool follow) {
    if (logScope_.isEmpty() || !std::isfinite(offset) || offset < 0) return false;
    auto& position = logPositions_[logScope_]; position.anchor = anchor; position.offset = offset; position.follow = follow;
    return true;
}
bool Workspace::followLogs() {
    if (logScope_.isEmpty()) return false;
    logPositions_[logScope_] = LogPosition{}; emit logViewChanged(); return true;
}
bool Workspace::reportLogEviction() {
    if (logScope_.isEmpty()) return false;
    auto& position = logPositions_[logScope_]; position.anchor = logs_.entryId(0); position.offset = 0; position.follow = false; position.evicted = true;
    position.removal = client_.logRemoval(active_, navigation_.value(active_).inspected);
    emit logViewChanged(); return true;
}
QString Workspace::logEntry(int index) const { return logs_.data(logs_.index(index, 0), Qt::EditRole).toString(); }
bool Workspace::copyLogEntry(int index) {
    const auto entry = logEntry(index);
    if (entry.isEmpty()) return false;
    QGuiApplication::clipboard()->setText(entry); return true;
}
bool Workspace::publishLogs() {
    emit logViewChanging();
    const auto path = navigation_.value(active_).inspected;
    logScope_ = logsVisible() ? active_ + '\n' + path + '\n' + client_.resource(active_, path)["uid"].toString() + '\n' + logSelection() : QString{};
    logs_.publish(logsVisible() ? client_.logEntries(active_, navigation_.value(active_).inspected) : QList<LogEntry>{});
    emit logViewChanged(); return true;
}
bool Workspace::setLogsVisible(bool visible) {
    emit logViewChanging();
    if (visible && (!podInspected() || !client_.showLogs(active_, navigation_.value(active_).inspected))) { error_ = "Logs require a Pod with valid cached container metadata."; emit changed(); return false; }
    navigation_[active_].page = visible ? "logs" : "overview";
    if (!visible) client_.hideLogs();
    publishLogs(); emit changed(); return true;
}
bool Workspace::setWindowVisible(bool visible) {
    if (windowVisible_ == visible) return true;
    emit logViewChanging(); windowVisible_ = visible;
    if (!visible && !revealedValues_.isEmpty()) { revealedValues_.clear(); publishInspector(true); }
    if (logsVisible()) client_.showLogs(active_, navigation_.value(active_).inspected);
    else client_.hideLogs();
    publishLogs(); emit changed(); return true;
}
bool Workspace::selectLogContainer(const QString& container) { emit logViewChanging(); return client_.selectLogContainer(container); }
bool Workspace::pauseLogs(bool paused) { return client_.pauseLogs(paused); }
bool Workspace::eventFilter(QObject* watched, QEvent* event) {
    if (event->type() == QEvent::MouseButtonPress || event->type() == QEvent::KeyPress || event->type() == QEvent::Wheel || event->type() == QEvent::TouchBegin) client_.userActivity();
    return QObject::eventFilter(watched, event);
}
QString Workspace::activeCluster() const {
    if (const auto connection = client_.connection(active_))
        for (const auto& source : sources_.sources) for (const auto& context : source.contexts)
            if (context.id == connection->contextId) return context.cluster;
    return {};
}
bool Workspace::publish() {
    alerts_.showSession(active_);
    const auto snapshot = client_.rows(active_);
    const auto cluster = activeCluster();
    rows_.publish(snapshot, cluster);
    QJsonArray eventSnapshot;
    int healthy=0, warning=0, critical=0;
    for (const auto& value : snapshot) {
        const auto row=value.toObject();
        if (row["kubernetesEvent"].toBool()) eventSnapshot.append(value);
        const int severity=row["problemSeverity"].toInt();
        if (severity==2) ++critical;
        else if (severity==1) ++warning;
        else ++healthy;
    }
    healthSummary_={{"total", snapshot.size()}, {"healthy", healthy}, {"warning", warning}, {"critical", critical}};
    eventRows_.publish(eventSnapshot, cluster);
    publishPorts();
    const auto nav = navigation_.value(active_);
    table_.filter(nav.filter, nav.fields, nav.mode); table_.sort(nav.column, nav.order);
    if (filterPickerSession_ != active_) {
        filterPickerSession_.clear(); filterPickerField_.clear(); filterPickerValues_.clear();
        emit filterPickerChanged(); emit fieldFiltersChanged();
    }
    events_.filter(nav.eventFilter); events_.sort(nav.eventColumn, nav.eventOrder);
    dashboardDirty_=true; publishDashboard();
    publishInspector(); publishRelated(); emit resourcePresentationChanged(); emit changed();
    return true;
}
bool Workspace::reload() {
    if (busy_) return false;
    ++selectionRevision_;
    busy_ = true; emit changed();
    using Catalogs = std::tuple<Result<SourceCatalog>, Result<SessionCatalog>, Result<ReadSettings>>;
    auto* watcher = new QFutureWatcher<Catalogs>(this);
    connect(watcher, &QFutureWatcher<Catalogs>::finished, this, [this, watcher] {
        const auto [sources, sessions, settings] = watcher->result(); watcher->deleteLater(); busy_ = false;
        if (const auto* failure = std::get_if<Failure>(&settings)) { settingsError_ = failure->message; settingsReady_ = false; client_.enableRequests(false); }
        else {
            const auto previousLanguage = settings_.language;
            settings_ = std::get<ReadSettings>(settings); settingsError_.clear(); settingsReady_ = true;
            publishAppearance(); client_.configure(settings_); client_.enableRequests(true);
            if (previousLanguage != settings_.language) emit languageChanged();
        }
        if (const auto* failure = std::get_if<Failure>(&sources)) error_ = failure->message;
        else {
            sources_ = std::get<SourceCatalog>(sources);
            error_ = sources_.errors.isEmpty() ? QString{} : "Some owned source records could not be read; healthy sources remain available.";
        }
        if (const auto* failure = std::get_if<Failure>(&sessions)) error_ = failure->message;
        else {
            selection_ = std::get<SessionCatalog>(sessions);
            if (!settingsReady_) { emit catalogsChanged(); emit changed(); return; }
            // Selection is ranked separately; never use it as the tab-order catalog.
            const auto profile = profile_;
            using OrderedViews = std::pair<Result<SessionCatalog>, LoadedViewStates>;
            auto* order = new QFutureWatcher<OrderedViews>(this);
            const bool restore = !viewsLoaded_;
            const bool closeOnStartup = restore && !settings_.workspaceRestore;
            const auto schemas = tableSchemas(true);
            busy_ = true;
            connect(order, &QFutureWatcher<OrderedViews>::finished, this, [this, order, restore] {
                const auto [result, views] = order->result(); order->deleteLater(); busy_ = false;
                if (const auto* failure = std::get_if<Failure>(&result)) { error_ = failure->message; emit changed(); }
                else {
                    if (restore) { restoreViewStates(views); viewsLoaded_ = true; }
                    if (select(std::get<SessionCatalog>(result)) && !active_.isEmpty()) resolve(active_);
                }
            });
            order->setFuture(QtConcurrent::run([profile, schemas, restore, closeOnStartup] {
                const SessionStore store(profile);
                auto catalog = closeOnStartup ? store.closeWorkspace() : store.list();
                LoadedViewStates views;
                if (restore) if (const auto* sessions = std::get_if<SessionCatalog>(&catalog))
                    for (const auto& session : sessions->sessions) {
                        const auto id = sessionId(session.id); views.insert(id, ViewStateStore(profile, schemas).load(id));
                    }
                return OrderedViews{std::move(catalog), std::move(views)};
            }));
        }
        emit catalogsChanged(); emit changed();
    });
    const auto profile = profile_;
    watcher->setFuture(QtConcurrent::run([profile] { return Catalogs{KubeconfigStore(profile).list(), SessionStore(profile).selection(), ReadSettingsStore(profile).load()}; }));
    return true;
}
bool Workspace::select(const SessionCatalog& catalog) {
    const auto next = catalog.activeSession ? sessionId(*catalog.activeSession) : QString{};
    const bool sessionChanged = next != active_;
    if (sessionChanged && !allowLeave(Leave::Reload)) return false;
    emit logViewChanging();
    catalog_ = catalog;
    active_ = next;
    client_.showSession(active_);
    emit catalogsChanged();
    if (sessionChanged || active_.isEmpty()) publish();
    else emit changed();
    if (logsVisible()) client_.showLogs(active_, navigation_.value(active_).inspected);
    else { client_.hideLogs(); publishLogs(); }
    return true;
}
bool Workspace::importFile(const QString& path) {
    return importSource([path](const KubeconfigStore& store) { return store.importPath(path); });
}
bool Workspace::importK3d() {
    return importSource([](const KubeconfigStore& store) {
        QString executable = QStandardPaths::findExecutable("k3d");
#ifdef Q_OS_MACOS
        if (executable.isEmpty()) executable = QStandardPaths::findExecutable("k3d", {"/opt/homebrew/bin", "/usr/local/bin"});
#endif
        return store.importK3d(executable);
    });
}
QString Workspace::defaultSourceOrigin() const { return QDir::home().filePath(".kube/pasted-kubeconfig"); }
bool Workspace::importHome() { return importFile(QDir::home().filePath(".kube/config")); }
bool Workspace::importText(QString originPath, const QString& yaml) {
    return importSource([originPath = std::move(originPath), yaml](const KubeconfigStore& store) -> Result<SourceImportReport> {
        const auto result = store.importText(originPath, yaml);
        if (const auto* failure = std::get_if<Failure>(&result)) return *failure;
        return SourceImportReport{{std::get<SourceSnapshot>(result)}, {}};
    });
}
bool Workspace::refreshSources() {
    QStringList paths;
    for (const auto& source : sources_.sources)
        if (!source.sourcePath.startsWith("podlord-generated://")) paths.append(source.sourcePath);
    paths.removeDuplicates();
    if (paths.isEmpty()) { sourceImportError_ = "No imported source files to refresh."; emit changed(); return false; }
    return importSource([paths](const KubeconfigStore& store) -> Result<SourceImportReport> {
        SourceImportReport report;
        for (const auto& path : paths) {
            const auto result = store.importFile(path);
            if (const auto* failure = std::get_if<Failure>(&result)) report.errors.append({path, *failure});
            else report.sources.append(std::get<SourceSnapshot>(result));
        }
        return report;
    });
}
bool Workspace::renameSourceContext(const QString& contextId, const QString& displayName) {
    return importSource([contextId, displayName](const KubeconfigStore& store) -> Result<SourceImportReport> {
        const auto result = store.renameContext(contextId, displayName);
        if (const auto* failure = std::get_if<Failure>(&result)) return *failure;
        return SourceImportReport{{std::get<SourceSnapshot>(result)}, {}};
    }, "Source display name saved. Context identity, credentials and sessions are unchanged.");
}
bool Workspace::requestSourceRemoval(const QString& contextId) {
    if (busy_ || pendingLeave_ || !sourceRemoval_.isEmpty()) return false;
    sourceImportError_.clear(); sourceImportNotice_.clear();
    for (const auto& source : sources_.sources) for (const auto& context : source.contexts) {
        if (context.id != contextId) continue;
        QStringList ids, names;
        for (const auto& session : catalog_.sessions) if (session.config.contextId == contextId) {
            ids.append(sessionId(session.id)); names.append(session.displayName());
        }
        if (yamlDirty() && ids.contains(active_)) {
            sourceImportError_ = "Save or discard the YAML draft before removing its context.";
            emit changed(); return false;
        }
        sourceRemoval_ = {{"contextId", contextId}, {"name", context.displayName}, {"sessions", names}, {"sessionIds", ids}};
        emit sourceRemovalChanged(); emit changed(); return true;
    }
    sourceImportError_ = "The selected imported context is unavailable.";
    emit changed(); return false;
}
bool Workspace::cancelSourceRemoval() {
    if (busy_) return false;
    sourceRemoval_.clear(); sourceImportError_.clear();
    emit sourceRemovalChanged(); emit changed(); return true;
}
bool Workspace::confirmSourceRemoval() {
    if (busy_ || pendingLeave_ || sourceRemoval_.isEmpty()) return false;
    const auto contextId = sourceRemoval_["contextId"].toString();
    const auto ids = sourceRemoval_["sessionIds"].toStringList();
    if (yamlDirty() && ids.contains(active_)) {
        sourceImportError_ = "Save or discard the YAML draft before removing its context.";
        emit changed(); return false;
    }
    sourceImportError_.clear(); busy_ = true; emit changed();
    auto* watcher = new QFutureWatcher<Result<SessionCatalog>>(this);
    connect(watcher, &QFutureWatcher<Result<SessionCatalog>>::finished, this, [this, watcher, contextId, ids] {
        const auto result = watcher->result(); watcher->deleteLater(); busy_ = false;
        if (const auto* failure = std::get_if<Failure>(&result)) {
            sourceImportError_ = failure->message; emit changed();
            if (failure->code == StoreError::Conflict) reload();
            return;
        }
        for (const auto& id : ids) {
            client_.close(id); alerts_.closeSession(id); navigation_.remove(id);
        }
        for (auto& source : sources_.sources)
            source.contexts.erase(std::remove_if(source.contexts.begin(), source.contexts.end(),
                [&](const auto& context) { return context.id == contextId; }), source.contexts.end());
        sources_.sources.erase(std::remove_if(sources_.sources.begin(), sources_.sources.end(),
            [](const auto& source) { return source.contexts.isEmpty(); }), sources_.sources.end());
        const auto& catalog = std::get<SessionCatalog>(result);
        selection_.sessions.erase(std::remove_if(selection_.sessions.begin(), selection_.sessions.end(),
            [&](const auto& session) { return session.config.contextId == contextId; }), selection_.sessions.end());
        selection_.activeSession = catalog.activeSession; selection_.removedContexts = catalog.removedContexts;
        select(catalog);
        refreshSessionSelection();
        sourceRemoval_.clear();
        sourceImportNotice_ = QString("Removed the imported context and %1 saved session(s). Original kubeconfig files are unchanged.").arg(ids.size());
        emit sourceRemovalChanged(); emit changed();
    });
    const auto profile = profile_;
    sessionMutationFuture_ = QtConcurrent::run([profile, contextId, ids] { return KubeconfigStore(profile).removeContext(contextId, ids); });
    watcher->setFuture(sessionMutationFuture_);
    return true;
}
bool Workspace::importSource(const std::function<Result<SourceImportReport>(const KubeconfigStore&)>& operation, const QString& notice) {
    if (busy_) return false;
    sourceImportError_.clear(); sourceImportNotice_.clear(); sourceImportIssues_.clear();
    busy_ = true; emit changed();
    auto* watcher = new QFutureWatcher<Result<SourceImportReport>>(this);
    connect(watcher, &QFutureWatcher<Result<SourceImportReport>>::finished, this, [this, watcher, notice] {
        const auto result = watcher->result(); watcher->deleteLater(); busy_ = false;
        if (const auto* failure = std::get_if<Failure>(&result)) { sourceImportError_ = failure->message; error_ = failure->message; emit changed(); emit sourceImportFinished(false); }
        else {
            const auto& report = std::get<SourceImportReport>(result);
            sourceImportNotice_ = notice.isEmpty() ? QString("Imported %1 source(s); %2 input(s) failed. Existing snapshots and sessions were retained.")
                .arg(report.sources.size()).arg(report.errors.size())
                : notice;
            for (const auto& issue : report.errors) sourceImportIssues_.append(QVariantMap{{"sourcePath", issue.sourcePath}, {"message", issue.failure.message}});
            reload();
            emit sourceImportFinished(!report.sources.isEmpty() && report.errors.isEmpty());
        }
    });
    const auto profile = profile_;
    sourceImportFuture_ = QtConcurrent::run([profile, operation] { return operation(KubeconfigStore(profile)); });
    watcher->setFuture(sourceImportFuture_);
    return true;
}
bool Workspace::mutate(const std::function<Result<SessionCatalog>(const SessionStore&)>& operation, const QString& target, const QString& renamedSession, const QString& managementNotice) {
    if (busy_) return false;
    busy_ = true; emit changed();
    auto* watcher = new QFutureWatcher<Result<SessionCatalog>>(this);
    connect(watcher, &QFutureWatcher<Result<SessionCatalog>>::finished, this, [this, watcher, target, renamedSession, managementNotice] {
        const auto result = watcher->result(); watcher->deleteLater(); busy_ = false;
        if (const auto* failure = std::get_if<Failure>(&result)) {
            if (!managementNotice.isEmpty()) { sessionManagementError_ = failure->message; emit changed(); }
            else if (!renamedSession.isEmpty()) { sessionRenameError_ = failure->message; emit changed(); }
            else { error_ = failure->message; select(catalog_); }
            return;
        }
        const auto catalog = std::get<SessionCatalog>(result);
        if (!target.isEmpty()) { client_.close(target); alerts_.closeSession(target); }
        if (!managementNotice.isEmpty()) {
            catalog_ = catalog; sessionManagementError_.clear(); sessionManagementNotice_ = managementNotice; emit catalogsChanged(); emit changed();
        } else if (!renamedSession.isEmpty()) {
            catalog_ = catalog; sessionRenameError_.clear(); emit catalogsChanged(); emit changed(); emit sessionRenamed(renamedSession);
        } else {
            if (!select(catalog)) return;
            if (!active_.isEmpty()) {
                const auto connection = client_.connection(active_);
                const auto session = std::find_if(catalog.sessions.cbegin(), catalog.sessions.cend(),
                    [&](const auto& value) { return sessionId(value.id) == active_; });
                if (connection && session != catalog.sessions.cend()) client_.open(active_, *connection, session->config.namespaces);
                else resolve(active_);
            } else emit changed();
        }
        refreshSessionSelection();
    });
    const auto profile = profile_;
    sessionMutationFuture_ = QtConcurrent::run([profile, operation] { return operation(SessionStore(profile)); });
    watcher->setFuture(sessionMutationFuture_);
    return true;
}
void Workspace::refreshSessionSelection() {
    // User actions own ranking refresh; stale completions cannot revive deleted sessions.
    const auto revision = ++selectionRevision_;
    auto* ranking = new QFutureWatcher<Result<SessionCatalog>>(this);
    connect(ranking, &QFutureWatcher<Result<SessionCatalog>>::finished, this, [this, ranking, revision] {
        const auto result = ranking->result(); ranking->deleteLater();
        if (revision == selectionRevision_)
            if (const auto* value = std::get_if<SessionCatalog>(&result)) { selection_ = *value; emit catalogsChanged(); }
    });
    const auto profile = profile_;
    ranking->setFuture(QtConcurrent::run([profile] { return SessionStore(profile).selection(); }));
}
QVariantMap Workspace::sessionConfiguration(const QString& id) const {
    const auto found = std::find_if(catalog_.sessions.cbegin(), catalog_.sessions.cend(), [&](const auto& session) { return sessionId(session.id) == id; });
    if (found == catalog_.sessions.cend()) return {};
    return {{"id", id}, {"name", found->name}, {"displayName", found->displayName()},
        {"contextId", found->config.contextId}, {"namespaces", found->config.namespaces.join(", ")}};
}
QVariantList Workspace::resourceGuidance() const {
    return podlord::resourceGuidance(client_.document(active_, navigation_.value(active_).inspected));
}
bool Workspace::saveSessionConfiguration(const QString& id, const QString& contextId, const QString& namespaces) {
    if (busy_ || pendingLeave_) return false;
    sessionManagementError_.clear(); sessionManagementNotice_.clear();
    const auto configuration = sessionConfiguration(id);
    if (configuration.isEmpty()) { sessionManagementError_ = "The selected session no longer exists."; emit changed(); return false; }
    if (!contextUsable(contextId)) { sessionManagementError_ = "Choose an available, usable imported context."; emit changed(); return false; }
    const SessionConfig config{contextId, namespaces.split(QRegularExpression("[,\\s]+"), Qt::SkipEmptyParts)};
    return mutate([id, config](const SessionStore& store) { return store.snapshot(QUuid(id), config); }, {}, {},
        "Configuration saved. Existing sessions are unchanged; open the result from Saved sessions.");
}
bool Workspace::duplicateSession(const QString& id, const QString& name) {
    if (busy_ || pendingLeave_) return false;
    sessionManagementError_.clear(); sessionManagementNotice_.clear();
    const auto found = std::find_if(catalog_.sessions.cbegin(), catalog_.sessions.cend(), [&](const auto& session) { return sessionId(session.id) == id; });
    if (found == catalog_.sessions.cend()) { sessionManagementError_ = "The selected session no longer exists."; emit changed(); return false; }
    const auto config = found->config;
    return mutate([config, name](const SessionStore& store) { return store.create(config, name); }, {}, {},
        "Independent session copy saved, closed and without usage history. Open it from Saved sessions.");
}
bool Workspace::openContext(const QString& context) {
    if (busy_ || pendingLeave_) return false;
    if (!contextUsable(context)) { error_ = "Choose an available, usable imported context."; emit changed(); return false; }
    if (std::any_of(catalog_.sessions.cbegin(), catalog_.sessions.cend(), [&](const auto& session) {
        return sessionId(session.id) == active_ && session.open && session.config == SessionConfig{context, {}};
    })) {
        if (!error_.isEmpty()) { error_.clear(); emit changed(); }
        return true;
    }
    const auto existing = std::find_if(catalog_.sessions.cbegin(), catalog_.sessions.cend(), [&](const auto& session) { return session.config == SessionConfig{context, {}}; });
    if ((existing == catalog_.sessions.cend() || sessionId(existing->id) != active_) && !allowLeave(Leave::Context, context)) return false;
    return mutate([context](const SessionStore& store) -> Result<SessionCatalog> {
        const auto catalog = store.list();
        if (const auto* failure = std::get_if<Failure>(&catalog)) return *failure;
        for (const auto& session : std::get<SessionCatalog>(catalog).sessions)
            if (session.config == SessionConfig{context, {}}) return store.activate(session.id);
        const auto created = store.create({context, {}});
        if (const auto* failure = std::get_if<Failure>(&created)) return *failure;
        return store.activate(std::get<SessionCatalog>(created).sessions.last().id);
    });
}
bool Workspace::contextUsable(const QString& id) const {
    for (const auto& source : sources_.sources)
        if (std::any_of(source.contexts.cbegin(), source.contexts.cend(), [&](const auto& context) {
            return context.id == id && context.brokenReferences.isEmpty();
        })) return true;
    return false;
}
bool Workspace::activate(const QString& id) {
    // The cached navigation changes immediately; persistence and resolution do not block painting.
    const auto found = std::find_if(catalog_.sessions.cbegin(), catalog_.sessions.cend(), [&](const auto& session) { return sessionId(session.id) == id; });
    if (busy_ || found == catalog_.sessions.cend()) return false;
    if (pendingLeave_ || (id != active_ && !allowLeave(Leave::Session, id))) return false;
    emit logViewChanging(); active_ = id; client_.showSession(id); publish();
    if (logsVisible()) client_.showLogs(id, navigation_.value(id).inspected);
    else publishLogs();
    return mutate([id](const SessionStore& store) { return store.activate(QUuid(id)); });
}
bool Workspace::close(const QString& id) {
    if (busy_ || pendingLeave_ || (id == active_ && !allowLeave(Leave::Tab, id))) return false;
    return mutate([id](const SessionStore& store) { return store.close(QUuid(id)); }, id);
}
bool Workspace::renameSession(const QString& id, const QString& name) {
    if (busy_ || pendingLeave_) return false;
    const auto found = std::find_if(catalog_.sessions.cbegin(), catalog_.sessions.cend(), [&](const auto& session) { return sessionId(session.id) == id; });
    sessionRenameError_.clear();
    if (found == catalog_.sessions.cend()) { sessionRenameError_ = "The selected session no longer exists."; emit changed(); return false; }
    if (name == found->displayName()) { emit changed(); emit sessionRenamed(id); return true; }
    return mutate([id, name](const SessionStore& store) { return store.rename(QUuid(id), name); }, {}, id);
}
bool Workspace::resolve(const QString& id, bool retry) {
    if (!settingsReady_) return false;
    const auto found = std::find_if(catalog_.sessions.cbegin(), catalog_.sessions.cend(), [&](const auto& session) { return sessionId(session.id) == id; });
    if (found == catalog_.sessions.cend()) return false;
    const auto config = found->config;
    busy_ = true; emit changed();
    auto* watcher = new QFutureWatcher<Result<ClusterConnection>>(this);
    connect(watcher, &QFutureWatcher<Result<ClusterConnection>>::finished, this, [this, watcher, id, config, retry] {
        const auto result = watcher->result(); watcher->deleteLater(); busy_ = false;
        if (const auto* failure = std::get_if<Failure>(&result)) error_ = failure->message;
        else {
            error_.clear();
            auto connection = std::get<ClusterConnection>(result);
            if (connection.exec) {
                if (const auto cached = credentials_.cached(connection.credentialId)) {
                    const auto context = connection.contextId; connection = *cached; connection.contextId = context;
                }
            }
            client_.open(id, connection, config.namespaces);
            if (retry) {
                if (connection.exec) credentials_.start(std::get<ClusterConnection>(result), id);
                else client_.refresh(id, true);
            }
        }
        emit changed();
    });
    const auto profile = profile_;
    watcher->setFuture(QtConcurrent::run([profile, config] { return KubeconfigStore(profile).connection(config.contextId); }));
    return true;
}
bool Workspace::refresh(bool confirmedAuthentication) {
    return confirmedAuthentication ? (!busy_ && resolve(active_, true)) : client_.refresh(active_);
}
bool Workspace::cancelAuthentication() {
    const auto connection = client_.connection(active_);
    return connection && credentials_.cancel(connection->credentialId);
}
bool Workspace::filter(const QString& text) {
    if (navigation_.value(active_).filter == text) return true;
    navigation_[active_].filter = text;
    return applyResourceFilters();
}
bool Workspace::publishDashboard() {
    if (!dashboardDirty_) return true;
    QMap<QString, int> kinds; int problems=0;
    QJsonArray scoped;
    for (int index=0; index<table_.rowCount(); ++index) {
        const auto row=rows_.row(table_.mapToSource(table_.index(index, 0)).row()); scoped.append(row); ++kinds[row["kind"].toString()]; if (row["problems"].toBool()) ++problems;
    }
    QStringList counts; for (auto it=kinds.begin(); it!=kinds.end(); ++it) counts.append(it.key()+": "+QString::number(it.value()));
    const auto summary=QString("%1 / %2 cached resources; %3 with problems. ").arg(table_.rowCount()).arg(rows_.rowCount()).arg(problems)+counts.join(" / ");
    const auto metrics=resourceMetricSummary(scoped);
    const bool changed=dashboardSummary_!=summary || pulseMetrics_!=metrics;
    dashboardSummary_=summary; pulseMetrics_=metrics; dashboardDirty_=false;
    if (changed) emit dashboardChanged(); return true;
}
bool Workspace::setWorkspacePage(const QString& page) {
    if (page != "resources" && page != "ports" && page != "settings" && page != "events" && page != "alerts" && page != "dashboard") return false;
    if (navigation_.value(active_).workspace == page) return true;
    navigation_[active_].workspace = page;
    if (page=="dashboard") publishDashboard();
    emit changed(); return true;
}
bool Workspace::rememberRadarView(const QVariantMap& view) {
    if (active_.isEmpty() || !RadarIsland::validPose(view)) return false;
    navigation_[active_].radarView = view; return true;
}
bool Workspace::inspectPath(const QString& path) {
    if (path.isEmpty()) return false;
    for (int index = 0; index < rows_.rowCount(); ++index)
        if (rows_.row(index)["path"] == path) return inspectResource(path);
    return false;
}
int Workspace::alertResourceIndex(const QString& path) const {
    for (int row=0; row<table_.rowCount(); ++row) if (table_.data(table_.index(row, 0), Qt::UserRole).toString()==path) return row;
    return -1;
}
bool Workspace::sortColumn(int column) {
    if (column < 0 || column >= rows_.columnCount()) return false;
    auto& nav = navigation_[active_];
    cycleSort(column, nav.column, nav.order);
    table_.sort(nav.column, nav.order); queueViewSave("resource"); emit changed(); return true;
}
bool Workspace::sortPortColumn(int column) {
    if (active_.isEmpty() || column < 0 || column >= portRows_.columnCount()) return false;
    auto& nav = navigation_[active_]; cycleSort(column, nav.portColumn, nav.portOrder);
    ports_.sort(nav.portColumn, nav.portOrder); queueViewSave("port"); emit changed(); return true;
}
bool Workspace::sortInspectorColumn(const QString& table, int column) {
    auto* model = table == "inspectorEvent" ? &inspectorEventsTable_ : table == "inspectorLink" ? &inspectorLinksTable_ : table == "value" ? &valuesTable_ : nullptr;
    if (!model || active_.isEmpty() || column < 0 || column >= model->columnCount()) return false;
    if (table == "value" && column >= 3) return false;
    auto& nav = navigation_[active_];
    auto& previous = table == "inspectorEvent" ? nav.inspectorEventColumn : table == "inspectorLink" ? nav.inspectorLinkColumn : nav.valueColumn;
    auto& order = table == "inspectorEvent" ? nav.inspectorEventOrder : table == "inspectorLink" ? nav.inspectorLinkOrder : nav.valueOrder;
    cycleSort(column, previous, order);
    model->sort(previous, order); queueViewSave(table); emit changed(); return true;
}
bool Workspace::filterEvents(const QString& text) {
    if (navigation_.value(active_).eventFilter == text) return true;
    navigation_[active_].eventFilter = text; events_.filter(text); queueViewSave("event"); emit resourcePresentationChanged(); emit changed(); return true;
}
bool Workspace::sortEventColumn(int column) {
    if (column < 0 || column >= eventRows_.columnCount()) return false;
    auto& nav = navigation_[active_]; cycleSort(column, nav.eventColumn, nav.eventOrder);
    events_.sort(nav.eventColumn, nav.eventOrder); queueViewSave("event"); emit changed(); return true;
}
bool Workspace::inspectEventPath(const QString& path) {
    QJsonObject event;
    for (int index = 0; index < eventRows_.rowCount(); ++index) if (eventRows_.row(index)["path"] == path) { event = eventRows_.row(index); break; }
    if (event.isEmpty()) return false;
    const auto uid = event["eventTargetUid"].toString();
    if (!uid.isEmpty()) for (int index = 0; index < rows_.rowCount(); ++index) {
        const auto target = rows_.row(index);
        if (target["uid"] == uid) return inspectResource(target["path"].toString());
    }
    // An absent/replaced target is never guessed by name; inspect the Event itself.
    return inspectResource(event["path"].toString());
}
bool Workspace::copyEventCell(int row, int column) {
    const auto index = events_.index(row, column);
    if (!index.isValid()) return false;
    QGuiApplication::clipboard()->setText(events_.data(index).toString()); return true;
}
bool Workspace::copyPathCell(const QString& path, int column, bool event) {
    const auto& source = event ? eventRows_ : rows_;
    if (column < 0 || column >= source.columnCount()) return false;
    for (int index = 0; index < source.rowCount(); ++index) if (source.row(index)["path"] == path) {
        QGuiApplication::clipboard()->setText(source.data(source.index(index, column)).toString()); return true;
    }
    return false;
}
bool Workspace::inspectRow(int row) {
    const auto source = table_.mapToSource(table_.index(row, 0));
    const auto object = rows_.row(source.row());
    if (object.isEmpty()) return false;
    return inspectResource(object["path"].toString());
}
bool Workspace::inspectResource(const QString& path) {
    if (pendingLeave_ || (path != inspectorPath() && !allowLeave(Leave::Resource, path))) return false;
    return openInspector(path);
}
bool Workspace::openInspector(const QString& path, int historyIndex) {
    emit logViewChanging();
    auto& nav = navigation_[active_];
    if (historyIndex >= 0) nav.historyIndex = historyIndex;
    else if (nav.historyIndex < 0 || nav.history.value(nav.historyIndex) != path) {
        nav.history.insert(nav.historyIndex + 1, path); ++nav.historyIndex;
        if (nav.history.size() > 32) { nav.history.removeFirst(); --nav.historyIndex; }
    }
    nav.inspected = path;
    yamlFresh_ = false;
    publishInspector();
    emit yamlEditChanged();
    if (logsVisible()) client_.showLogs(active_, navigation_[active_].inspected);
    else { client_.hideLogs(); publishLogs(); }
    emit changed();
    return client_.inspect(active_, navigation_[active_].inspected);
}
int Workspace::inspectorHistoryTarget(int step) const {
    if ((step != -1 && step != 1) || inspectorPath().isEmpty() || busy_ || pendingLeave_) return -1;
    const auto nav = navigation_.value(active_);
    for (int i = nav.historyIndex + step; i >= 0 && i < nav.history.size(); i += step)
        if (client_.containsResource(active_, nav.history[i])) return i;
    return -1;
}
bool Workspace::canInspectBack() const { return inspectorHistoryTarget(-1) >= 0; }
bool Workspace::canInspectForward() const { return inspectorHistoryTarget(1) >= 0; }
bool Workspace::inspectHistory(const QString& path, int index) {
    const auto nav = navigation_.value(active_);
    if (index < 0 || index >= nav.history.size() || nav.history[index] != path || !client_.containsResource(active_, path)) {
        error_ = "The requested history resource is no longer available in this session cache."; emit changed(); return false;
    }
    openInspector(path, index);
    return true;
}
bool Workspace::navigateInspector(int step) {
    const auto index = inspectorHistoryTarget(step);
    if (index < 0) return false;
    const auto path = navigation_.value(active_).history[index];
    return allowLeave(Leave::History, path, index) && inspectHistory(path, index);
}
bool Workspace::copyCell(int row, int column) {
    const auto index = table_.index(row, column);
    if (!index.isValid()) return false;
    QGuiApplication::clipboard()->setText(table_.data(index).toString()); return true;
}
} // namespace podlord
