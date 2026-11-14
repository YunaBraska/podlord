#include "workspace.h"
#include "workspace_runtime.h"
#include "window_host.h"
#include <QQuickWindow>
#include "resource_guidance.h"
#include "runtime_diagnostics.h"
#include <QDir>
#include "resource_metrics.h"
#include "kind_glyph.h"
#include "radar_island.h"
#include "yaml_presentation.h"
#include <qqml.h>
#include <QClipboard>
#include <QLocale>
#include <QColor>
#include <QFutureWatcher>
#include <QEvent>
#include <QGuiApplication>
#include <QFontDatabase>
#include <QJsonDocument>
#include <QStringDecoder>
#include <QtConcurrent/QtConcurrentRun>
#include <algorithm>
#include <array>
#include <tuple>
#include <utility>
#include <QRegularExpression>
#include <cmath>
#ifdef Q_OS_ANDROID
#include <QJniObject>
#endif

namespace podlord {
namespace {
#ifdef Q_OS_ANDROID
bool retainDocumentReadAccess(const QString& path) {
    if (!path.startsWith("content://")) return true;
    const auto context = QNativeInterface::QAndroidApplication::context();
    const auto source = QJniObject::fromString(path);
    return QJniObject::callStaticMethod<jboolean>(
        "berlin/yuna/podlord/DocumentAccess", "retainRead",
        "(Landroid/content/Context;Ljava/lang/String;)Z",
        context.object<jobject>(), source.object<jstring>());
}
#else
bool retainDocumentReadAccess(const QString&) { return true; }
#endif
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
        result.append(QVariantMap{{"id", sessionId(session.id)}, {"name", session.config.contextId}});
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
int InspectorFields::rowCount(const QModelIndex& parent) const { return parent.isValid() ? 0 : static_cast<int>(fields_.size()); }
QVariant InspectorFields::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.model() != this || index.row() >= fields_.size() || role != Qt::UserRole) return {};
    return fields_[index.row()];
}
QHash<int, QByteArray> InspectorFields::roleNames() const { return {{Qt::UserRole, "modelData"}}; }
void InspectorFields::publish(QVariantList fields) {
    if (fields_.isEmpty()) {
        if (fields.isEmpty()) return;
        beginInsertRows({},0,static_cast<int>(fields.size()-1));
        fields_ = std::move(fields);
        endInsertRows();
        return;
    }
    for (int row = 0; row < fields.size(); ++row) {
        const auto id = fields[row].toMap().value("id");
        int retained = row;
        while (retained < fields_.size() && fields_[retained].toMap().value("id") != id) ++retained;
        if (retained == fields_.size()) {
            beginInsertRows({},row,row);
            fields_.insert(row,std::move(fields[row]));
            endInsertRows();
            continue;
        }
        if (retained != row) {
            beginMoveRows({},retained,retained,{},row);
            fields_.move(retained,row);
            endMoveRows();
        }
        if (fields_[row] != fields[row]) {
            fields_[row] = std::move(fields[row]);
            emit dataChanged(index(row),index(row),{Qt::UserRole});
        }
    }
    if (fields_.size() > fields.size()) {
        beginRemoveRows({},static_cast<int>(fields.size()),static_cast<int>(fields_.size()-1));
        fields_.erase(fields_.begin()+fields.size(),fields_.end());
        endRemoveRows();
    }
}
ResourceTable::ResourceTable(QObject* parent, QStringList fields, QStringList captions, QString identityField, bool preserveInputOrder)
    : QAbstractTableModel(parent), fields_(std::move(fields)), captions_(std::move(captions)), identityField_(std::move(identityField)), preserveInputOrder_(preserveInputOrder) {}
int ResourceTable::rowCount(const QModelIndex& parent) const { return parent.isValid() ? 0 : static_cast<int>(rows_.size()); }
int ResourceTable::columnCount(const QModelIndex& parent) const { return parent.isValid() ? 0 : static_cast<int>(fields_.size()); }
QVariant ResourceTable::data(const QModelIndex& index, int role) const {
    using namespace Qt::StringLiterals;
    if (!index.isValid() || index.row() >= rows_.size() || index.column() >= fields_.size()) return {};
    const auto& entry = rows_[index.row()];
    const auto& row = entry.document;
    if (role >= Qt::UserRole && role <= Qt::UserRole + 4) return metadata(index.row(), role);
    switch (role) {
    case Qt::UserRole + 5: return appearanceStatus(appearance_, metadata(index.row(), Qt::UserRole + 4));
    case Qt::UserRole + 7: return resourceMetricPresentation(row);
    case Qt::UserRole + 8: return row.contains("metricReferences"_L1);
    case Qt::UserRole + 9: return row["problemSeverity"_L1].toInt();
    case Qt::UserRole + 10: return row["activity"_L1].toBool();
    case Qt::UserRole + 13: return row["value"_L1].toString();
    case Qt::UserRole + 14: return row["base64"_L1].toBool();
    case Qt::UserRole + 15: return QVariantMap{{"issue", row["issue"_L1].toVariant()}, {"ready", row["ready"_L1].toVariant()},
        {"restarts", row["restarts"_L1].toVariant()}, {"node", row["node"_L1].toVariant()},
        {"image", row["image"_L1].toVariant()}, {"owner", row["owner"_L1].toVariant()}};
    default: break;
    }
    const auto& field = fields_[index.column()];
    if (role == Qt::ToolTipRole)
        return "<span>" + data(index, Qt::DisplayRole).toString().toHtmlEscaped().replace('\n', "<br>") + "</span>";
    const bool cacheDisplay = role == Qt::DisplayRole && field != "createdAt"_L1 && field != "preview"_L1;
    if (cacheDisplay) {
        if (entry.display.size() != fields_.size()) entry.display.resize(fields_.size());
        const auto& cached = std::as_const(entry.display)[index.column()];
        if (cached) return *cached;
    }
    if (role == Qt::UserRole + 6) {
        if (index.column() == sortValueColumn_ && sortValues_.size() == rows_.size()) return sortValues_[index.row()];
        const auto sortValue = row["sortValues"_L1].toObject().value(field);
        if (!sortValue.isUndefined()) return sortValue.toVariant();
        if (field == "preview") return row["value"_L1].toString();
        if (field == "eventTime") return QDateTime::fromString(row[field].toString(), Qt::ISODateWithMs);
        if (field == "createdAt") {
            const auto created = QDateTime::fromString(row[field].toString(), Qt::ISODateWithMs);
            return created.isValid() && created <= QDateTime::currentDateTimeUtc() ? QVariant(-created.toMSecsSinceEpoch()) : QVariant{};
        }
        if (field == "ready") return !row[field].toString().isEmpty() && row["containerCount"_L1].toInt() > 0
            ? QVariant(double(row["readyCount"_L1].toInt())/row["containerCount"_L1].toInt()) : QVariant{};
        if (field == "cluster") return row.contains(field) ? row[field].toString() : cluster_;
        return row[field].toVariant();
    }
    const auto cell = row[field];
    const auto text = field == "cluster" && !row.contains(field) ? cluster_ : cell.toString();
    const bool presentation = role == Qt::DisplayRole;
    const bool age = field == "createdAt" && (presentation || role == Qt::UserRole + 11);
    const auto created = age ? QDateTime::fromString(text, Qt::ISODateWithMs) : QDateTime{};
    const auto now = age ? QDateTime::currentDateTimeUtc() : QDateTime{};
    if (presentation) {
        const bool metric = field == "cpu" || field == "memory" || field == "storage";
        QString value = cell.isDouble() ? QString::number(cell.toDouble(), 'g', 16) : text;
        if (metric) {
            value = cell.isDouble() ? formatMetricQuantity(cell.toDouble(), field == "cpu") : QStringLiteral("-");
            if (cell.isDouble() && !row["metricComplete"].toObject()[field].toBool()) value += " (incomplete)";
        } else if (field == "eventTime" || field == "time" || field == "imported") {
            const auto date = QDateTime::fromString(text, Qt::ISODate);
            value = date.isValid() ? QLocale::system().toString(date.toLocalTime(), QLocale::ShortFormat) : QString{};
        } else if (field == "createdAt") {
            if (!created.isValid() || created > now) value = QStringLiteral("-");
            else {
                const auto seconds = created.secsTo(now);
                value = seconds >= 86400 ? QString::number(seconds/86400)+"d" : seconds >= 3600 ? QString::number(seconds/3600)+"h"
                    : seconds >= 60 ? QString::number(seconds/60)+"m" : QString::number(seconds)+"s";
            }
        } else if ((field == "restarts" || field == "ready" || field == "owner" || field == "issue" || field == "cluster") && value.isEmpty()) value = QStringLiteral("-");
        if (cacheDisplay) entry.display[index.column()] = value;
        return value;
    }
    if (role == Qt::UserRole + 12) return field == "from" || field == "to" ? row[field + "Path"].toString()
        : metadata(index.row(), Qt::UserRole);
    if (role == Qt::ForegroundRole) {
        if (field == "status" || field == "eventType" || (field == "type" && fields_.contains("reason"))) return appearanceStatus(appearance_, text);
        static const QStringList identities{"kind", "namespace", "status", "node", "image", "cluster", "eventType", "type", "from", "to"};
        return identities.contains(field) && !text.isEmpty() ? appearanceIdentity(appearance_, text) : QColor(Qt::transparent);
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
        {Qt::UserRole + 4, "resourceStatus"}, {Qt::UserRole + 5, "statusColor"}, {Qt::UserRole + 6, "sortValue"}, {Qt::UserRole + 7, "resourceMetrics"}, {Qt::UserRole + 8, "hasResourceMetrics"}, {Qt::UserRole + 9, "resourceHealth"}, {Qt::UserRole + 12, "endpointPath"}, {Qt::UserRole + 13, "valueDetail"}, {Qt::UserRole + 14, "encodedValue"}, {Qt::UserRole + 15, "resourceDetails"}};
}
QJsonObject ResourceTable::row(int index) const { return index >= 0 && index < rows_.size() ? rows_[index].document : QJsonObject{}; }
quint64 ResourceTable::populatedColumns(int row) const {
    if (row < 0 || row >= rows_.size()) return 0;
    const auto& entry = rows_[row];
    if (entry.prepared) return entry.populated;
    quint64 populated = 0;
    for (int column = 0; column < fields_.size() && column < 64; ++column) {
        const auto value = entry.document[fields_[column]];
        if (value.isDouble() || (value.isString() && !value.toString().isEmpty())) populated |= quint64{1} << column;
    }
    return populated;
}
QString ResourceTable::metadata(int row, int role) const {
    if (row < 0 || row >= rows_.size()) return {};
    const auto& entry = rows_[row];
    if (role == Qt::UserRole) return entry.prepared ? entry.identity : entry.document[identityField_].toString();
    const int column = role - Qt::UserRole - 1;
    if (column < 0 || column >= 4) return {};
    if (entry.prepared) return entry.metadataStrings & (1 << column) ? *std::as_const(entry.display)[column] : QString{};
    using namespace Qt::StringLiterals;
    static constexpr std::array<QLatin1StringView, 4> fields{"name"_L1, "kind"_L1, "namespace"_L1, "status"_L1};
    return entry.document[fields[column]].toString();
}
bool ResourceTable::setAppearance(const Appearance& appearance) {
    if (appearance_ == appearance) return true;
    appearance_ = appearance;
    if (!rows_.isEmpty()) emit dataChanged(index(0, 0), index(rowCount() - 1, columnCount() - 1), {Qt::ForegroundRole, Qt::UserRole + 5});
    return true;
}
bool ResourceTable::publish(const QJsonArray& rows, const QString& cluster, bool replace) {
    sortValues_.clear(); sortValueColumn_ = -1;
    const bool clusterChanged = cluster_ != cluster;
    cluster_ = cluster;
    if (clusterChanged) {
        const auto column = fields_.indexOf("cluster");
        if (column >= 0) for (auto& row : rows_)
            if (row.display.size() > column) row.display[column].reset();
    }
    if (preserveInputOrder_) {
        QList<Row> ordered;
        ordered.reserve(rows.size());
        for (const auto& value : rows) ordered.append(Row{value.toObject(), {}});
        if (ordered != rows_ || clusterChanged) {
            beginResetModel(); rows_ = std::move(ordered); endResetModel();
        }
        return true;
    }
    QMap<QString, QJsonObject> incoming;
    for (const auto& value : rows) { const auto row = value.toObject(); incoming.insert(row[identityField_].toString(), row); }
    if (replace && (rows_.size() != incoming.size() || !std::equal(rows_.cbegin(), rows_.cend(), incoming.keyBegin(),
            [this](const auto& row, const auto& key) { return row.document[identityField_].toString() == key; }))) {
        const int previous = rowCount(), next = static_cast<int>(incoming.size());
        QList<Row> replacement;
        replacement.reserve(next);
        for (const auto& value : incoming) replacement.append(Row{value, {}});
        const int retained = std::min(previous, next);
        // Complete retained slots before Qt records either structural boundary.
        // Their values then stay stable through begin/end and are notified below.
        if (next != previous) for (int row = 0; row < retained; ++row) rows_[row] = replacement[row];
        if (next > previous) {
            beginInsertRows({}, previous, next - 1);
            for (int row = previous; row < next; ++row) rows_.append(replacement[row]);
            endInsertRows();
        } else if (next < previous) {
            beginRemoveRows({}, next, previous - 1);
            rows_.resize(next);
            endRemoveRows();
        }
        // Retain visible slots instead of destroying their delegates.
        rows_ = std::move(replacement);
        if (retained > 0) emit dataChanged(index(0, 0), index(retained - 1, columnCount() - 1));
        return true;
    }
    for (int last = static_cast<int>(rows_.size()) - 1; last >= 0;) {
        if (incoming.contains(rows_[last].document[identityField_].toString())) { --last; continue; }
        int first = last;
        while (first > 0 && !incoming.contains(rows_[first-1].document[identityField_].toString())) --first;
        beginRemoveRows({}, first, last); rows_.remove(first, last-first+1); endRemoveRows();
        last = first-1;
    }
    int firstChanged = -1, lastChanged = -1, firstColumn = columnCount(), lastColumn = -1;
    for (int index = 0; index < rows_.size(); ++index) {
        const auto value = incoming.take(rows_[index].document[identityField_].toString());
        if (value != rows_[index].document) {
            const auto& previous = rows_[index].document;
            const bool presentationChanged = fields_.contains("createdAt") || std::any_of(fields_.cbegin(), fields_.cend(), [&](const auto& field) {
                return field == "cluster" ? (value.contains(field) || previous.contains(field) ? value[field] != previous[field] : clusterChanged)
                    : value[field] != previous[field] || (field == "preview" && value["value"] != previous["value"])
                        || ((field == "cpu" || field == "memory" || field == "storage")
                            && (value["metricComplete"] != previous["metricComplete"] || value["metricStale"] != previous["metricStale"]));
            });
            if (presentationChanged) { firstColumn = 0; lastColumn = columnCount()-1; }
            rows_[index] = Row{value, {}};
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
        const auto position = std::lower_bound(rows_.cbegin(), rows_.cend(), next.key(), [this](const auto& row, const auto& key) { return row.document[identityField_].toString() < key; });
        int index = static_cast<int>(position - rows_.cbegin());
        QList<QJsonObject> added;
        do { added.append(next.value()); ++next; }
        while (next != incoming.cend() && (position == rows_.cend() || next.key() < position->document[identityField_].toString()));
        beginInsertRows({}, index, index+static_cast<int>(added.size())-1);
        for (const auto& value : added) rows_.insert(index++, Row{value, {}});
        endInsertRows();
    }
    return true;
}
void ResourceTable::prepareSort(int column) {
    if (column < 0 || column >= fields_.size() || fields_[column] == "createdAt") return;
    if (column == sortValueColumn_ && sortValues_.size() == rows_.size()) return;
    sortValues_.clear(); sortValueColumn_ = -1;
    // Non-clock keys remain valid for this snapshot, including while sort is NONE.
    sortValues_.reserve(rows_.size());
    for (int row = 0; row < rows_.size(); ++row) sortValues_.append(data(index(row, column), Qt::UserRole + 6));
    sortValueColumn_ = column;
}
Workspace::Workspace(QString profile, QObject* parent, std::function<QDateTime()> now, QUrl releaseEndpoint)
    : Workspace(std::make_shared<WorkspaceRuntime>(std::move(profile), std::move(now), std::move(releaseEndpoint)), parent, true) {}
Workspace::Workspace(std::shared_ptr<WorkspaceRuntime> runtime, QObject* parent, bool restore)
    : QObject(parent), runtime_(std::move(runtime)), viewId_(restore ? QString{} : QUuid::createUuid().toString(QUuid::WithoutBraces)),
    profile_(runtime_->profile), savedViews_(runtime_->savedViews), pendingViews_(runtime_->pendingViews),
    viewSaveFuture_(runtime_->viewSaveFuture), viewSaveContext_(runtime_->viewSaveContext), viewStateErrors_(runtime_->viewStateErrors),
    viewsLoaded_(runtime_->viewsReady), viewSaving_(runtime_->viewSaving), client_(runtime_->client), releaseUpdates_(runtime_->releases),
    alerts_(profile_, &client_, this, runtime_->now), credentials_(runtime_->credentials),
    eventRows_(nullptr, {"eventTime", "eventType", "eventReason", "namespace", "eventTargetName", "eventCount", "eventMessage", "name", "owner"},
        {"Last observed", "Status", "Reason", "Namespace", "Target name", "Count", "Message", "Name", "Object"}),
    portRows_(nullptr, {"endpoint", "name", "kind", "namespace", "remotePort", "resolvedPort", "status"},
        {"Connection", "Name", "Kind", "Namespace", "Remote", "Resolved", "Status"}, "id"),
    inspectorEventRows_(nullptr, {"time", "type", "reason", "count", "message"}, {"Last observed", "Type", "Reason", "Count", "Message"}),
    inspectorLinkRows_(nullptr, {"from", "relation", "to", "namespace", "status"}, {"From", "Link", "To", "Namespace", "Status"}),
    valueRows_(nullptr, {"name", "encoding", "preview", "copy", "reveal"}, {"Key", "Encoding", "Value", "Copy", "Reveal"}, "id"),
    diagnosticRows_(nullptr, {"label", "value", "description"}, {"Metric", "Value", "Description"}, "id", true),
    requestAuditRows_(nullptr, {"time", "method", "path", "priority", "status", "duration", "outcome"},
        {"Time", "Method", "Path", "Priority", "Status", "Duration", "Outcome"}, "id") {
    connect(&runtime_->typography, &SystemTypography::changed, this, &Workspace::typographyChanged);
    connect(this, &Workspace::changed, this, &Workspace::sessionWindowActionsReadyChanged);
    connect(this, &Workspace::catalogsChanged, this, [this] { refreshMapContexts(mapContexts_.isEmpty()); });
    connect(this, &Workspace::filterPresetsChanged, this, &Workspace::sessionWindowActionsReadyChanged);
    connect(this, &Workspace::tableLayoutStatusChanged, this, &Workspace::sessionWindowActionsReadyChanged);
    diagnosticTable_.setSourceModel(&diagnosticRows_); diagnosticTable_.setSortRole(Qt::UserRole + 6);
    diagnosticTable_.setSortCaseSensitivity(Qt::CaseInsensitive);
    requestAuditTable_.setSourceModel(&requestAuditRows_); requestAuditTable_.setSortRole(Qt::UserRole + 6);
    requestAuditTable_.setSortCaseSensitivity(Qt::CaseInsensitive);
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
    static const int terminalSessionType = qmlRegisterUncreatableType<ContainerTerminal>("Podlord.Graphics", 1, 0, "ContainerTerminal", "Owned by the selected tab");
    Q_UNUSED(terminalSessionType);
    static const int findType = qmlRegisterType<ResourceFilter>("Podlord.Graphics", 1, 0, "ResourceFindFilter");
    Q_UNUSED(findType);
    static const int yamlType = qmlRegisterType<YamlPresentation>("Podlord.Graphics", 1, 0, "YamlPresentation");
    Q_UNUSED(yamlType);
    runtime_->windows.append(this);
    alerts_.setPresentationText(uiText());
    connect(this, &Workspace::languageChanged, this, [this] { alerts_.setPresentationText(uiText()); });
    alerts_.setSessionOwnership([this](const QString& id) {
        return !windowClosed_ && (runtime_->owners.value(id) == this || (!runtime_->owners.contains(id) && !detached_));
    });
    connect(&alerts_, &Alerts::catalogSaved, this, [this] {
        for (auto* other : runtime_->windows) if (other != this && !other->windowClosed_) other->alerts_.synchronizeCatalog(alerts_);
    });
    if (restore) client_.enableRequests(false);
    connect(&client_, &ResourceClient::requestStarted, this, &Workspace::requestStarted);
    const auto connectionsChanged = [this](const QString& id) {
        if (id == active_) publishPorts();
        if (id == active_ || runtime_->owners.value(id) == this) emit changed();
    };
    connect(&client_, &ResourceClient::portForwardsChanged, this, connectionsChanged);
    connect(&client_, &ResourceClient::terminalChanged, this, connectionsChanged);
    publishAppearance();
    bindYamlApply();
    bindDeletion();
    connect(this, &Workspace::yamlTextChanged, this, &Workspace::clearYamlCheck);
    connect(this, &Workspace::yamlEditChanged, this, &Workspace::clearYamlCheck);
    const auto watchInspector = [this] {
        const bool visible = !windowClosed_ && windowVisible_ && !sourceInspection_ && !yamlDraft_
            && workspacePage() != "settings" && !(workspacePage() == "map" && mapOverview())
            && (inspectorPage() == "overview" || inspectorPage() == "yaml" || inspectorPage() == "values");
        client_.watchInspector(active_, visible ? inspectorPath() : QString{}, viewId_);
    };
    connect(this, &Workspace::changed, this, watchInspector);
    connect(this, &Workspace::yamlEditChanged, this, watchInspector);
    connect(this, &Workspace::sourceInspectionChanged, this, watchInspector);
    if (auto* app = qobject_cast<QGuiApplication*>(QCoreApplication::instance())) {
        client_.setFocused(app->applicationState() == Qt::ApplicationActive, viewId_);
        connect(app, &QGuiApplication::applicationStateChanged, this, [this](Qt::ApplicationState state) {
            if (!windowClosed_) client_.setFocused(windowVisible_ && windowFocused_ && state == Qt::ApplicationActive, viewId_);
        });
    }
    table_.setSourceModel(&rows_);
    // Workspace applies the incoming snapshot's filter and order after publication.
    table_.setDynamicSortFilter(false);
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
    connect(&runtime_->previews, &MapPreviews::changed, this, &Workspace::mapPreviewsChanged);
    connect(&client_, &ResourceClient::changed, this, [this](const QString& id) { if (id == active_) {
        publishInspector(); emit yamlEditChanged(); emit changed();
        if (mapPreviewNeeded()) emit mapPreviewRequested(id);
    } });
    connect(&client_, &ResourceClient::rowsChanged, this, [this](const QString& id) {
        preparedSnapshots_.remove(id);
        if (snapshotPreparation_ && snapshotPreparation_->session == id) snapshotPreparation_->watcher->cancel();
        if (id == active_ && windowVisible_) publish();
    });
    connect(&client_, &ResourceClient::collectionAccepted, this, [this](const QString& id) {
        if (id==active_) { pendingMapPreview_=id; if (mapPreviewNeeded()) emit mapPreviewRequested(id); }
    });
    sourceTable_.setSourceModel(&sourceRows_);
    sourceTable_.setSortCaseSensitivity(Qt::CaseInsensitive);
    connect(this, &Workspace::tableLayoutChanged, this, &Workspace::resourceColumnsChanged);
    connect(this, &Workspace::resourcePresentationChanged, this, &Workspace::publishResourceColumns);
    connect(this, &Workspace::resourcePresentationChanged, this, [this] {
        QStringList visible;
        visible.reserve(table_.rowCount());
        for (int row = 0; row < table_.rowCount(); ++row)
            visible.append(table_.index(row, 0).data(Qt::UserRole).toString());
        alerts_.setVisibleResources(active_, visible);
    });
    connect(&client_, &ResourceClient::detailFinished, this, [this](const QString& id, const QString& path, bool accepted) {
        if (id != active_ || path != inspectorPath()) return;
        publishInspector();
        const auto revision = client_.detailRevision(id, path);
        const bool freshRead = accepted && revision.has_value();
        yamlFresh_ = revision.has_value() && (freshRead || yamlFresh_);
        if (freshRead && yamlDraft_ && yamlDraft_->document["metadata"].toObject()["uid"] == inspectorDocument_["metadata"].toObject()["uid"])
            yamlDraft_->revision = *revision;
        emit yamlEditChanged();
    });
    connect(&client_, &ResourceClient::logsChanged, this, [this](const QString& id, const QString& path) {
        if (id == active_ && path == currentNavigation().inspected) { publishLogs(); emit changed(); }
    });
    connect(&credentials_, &CredentialProcess::changed, this, &Workspace::changed);
    if (restore) reload();
}
QVariantList Workspace::contexts() const {
    QVariantList result;
    for (const auto& source : sources_.sources)
        for (const auto& context : source.contexts)
            result.append(QVariantMap{{"id", context.id}, {"name", context.displayName}, {"context", context.name}, {"source", source.sourcePath},
                {"auth", context.authType}, {"usable", context.brokenReferences.isEmpty()}, {"cluster",context.cluster}, {"user",context.user},
                {"imported",source.importedAt.toString(Qt::ISODate)}, {"detail",context.brokenReferences.join(", ")}, {"current",source.current}});
    return result;
}
QVariantList Workspace::tabs() const {
    if (!viewsLoaded_) return {};
    auto result = sessionRows(catalog_, true);
    result.erase(std::remove_if(result.begin(), result.end(), [this](const auto& row) {
        return runtime_->owners.value(row.toMap()["id"].toString()) != this;
    }), result.end());
    for (auto& value : result) {
        auto row = value.toMap();
        for (const auto& session : catalog_.sessions) if (sessionId(session.id) == row["id"].toString())
            for (const auto& source : sources_.sources) for (const auto& context : source.contexts)
                if (context.id == session.config.contextId) { row["name"] = context.displayName; row["contextId"] = context.id; }
        row["connectionCount"] = client_.connectionCount(row["id"].toString());
        value = row;
    }
    return result;
}
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
    return "Context: " + title() + "\nCluster: " + connection->server.toString()
        + (connection->exec && connection->exec->oidc ? "\nOpenID Connect will open the system browser. No imported command runs."
            : connection->exec && connection->exec->eks ? "\nAmazon EKS uses native AWS authentication. SSO may open the system browser; configured roles use AWS STS and may request an MFA code here. No imported command runs. Only trust configurations you know."
            : connection->exec && connection->exec->oci ? "\nOracle OKE will sign a short-lived token locally using the imported OCI API key. No imported command runs. Only trust configurations you know."
            : connection->exec && connection->exec->gke ? "\nGoogle GKE will use native Google authorization or the service-account key explicitly imported for this context. No imported command runs. Personal sign-in may open the system browser or a Google account dialog."
            : connection->exec ? "\nCommand: " + connection->exec->program + "\nThis runs the imported configuration's command and arguments with your permissions. Only trust configurations you know. Login may open a browser."
            : "\nConfirm a new authentication attempt.");
}
QString Workspace::authenticationInstruction() const {
    const auto connection = client_.connection(active_);
    return connection ? credentials_.instruction(connection->credentialId) : QString{};
}
QVariantMap Workspace::authenticationChallenge() const {
    const auto connection = client_.connection(active_);
    const auto challenge = connection ? credentials_.challenge(connection->credentialId) : std::optional<AwsMfaChallenge>{};
    return challenge ? QVariantMap{{"id", challenge->id}, {"role", challenge->role}, {"device", challenge->device}} : QVariantMap{};
}
bool Workspace::submitAuthenticationCode(const QString& id, const QString& code) {
    const auto connection = client_.connection(active_);
    return connection && credentials_.submitMfa(connection->credentialId, id, code);
}
QString Workspace::error() const {
    if (runtime_->host && runtime_->host->layoutCloseNeedsDecision(*this)) return runtime_->host->layoutCloseError();
    if (!settingsError_.isEmpty()) return settingsError_;
    if (viewStateFailed()) return viewStateError();
    if (const auto connection = client_.connection(active_); connection && connection->exec && !authenticationRunning()) {
        const auto result = credentials_.result(connection->credentialId);
        if (const auto* failure = std::get_if<Failure>(&result); failure && failure->code != StoreError::NotFound) return failure->message;
    }
    return error_.isEmpty() ? client_.portForwardError(active_) : error_;
}
QString Workspace::status() const { return authenticationRunning() ? "Signing in…" : client_.status(active_); }
QString Workspace::currentSession() const { return active_; }
QString Workspace::title() const {
    const auto identity = mapContextIdentity();
    if (!identity.isEmpty()) return identity.value("name").toString();
    return "Resources";
}
QString Workspace::filterText() const { return currentNavigation().filter; }
int Workspace::sortColumnIndex() const { return currentNavigation().column; }
QString Workspace::sortDirection() const { return currentNavigation().order == Qt::AscendingOrder ? "ASC" : "DESC"; }
QString Workspace::inspected() const {
    return overview_;
}
QString Workspace::inspectorPath() const { return currentNavigation().inspected; }
QString Workspace::inspectorName() const { return inspectorSummary_.value("name").toString(); }
QString Workspace::inspectorScope() const { return inspectorScope_; }
QString Workspace::inspectorStatus() const { return client_.detailStatus(active_, inspectorPath()); }
QString Workspace::yamlText() const { return yamlDraft_ ? yamlDraft_->text : yaml_; }
bool Workspace::canEditYaml() const {
    const auto metadata = inspectorDocument_.value("metadata").toObject();
    return !yamlDraft_ && yamlFresh_ && client_.detailRevision(active_, inspectorPath()).has_value()
        && windowVisible_ && !authenticationRequired() && !authenticationRunning()
        && !metadata.value("uid").toString().isEmpty() && !metadata.value("resourceVersion").toString().isEmpty();
}
bool Workspace::yamlEditing() const { return yamlDraft_.has_value(); }
bool Workspace::yamlDraftCredentialsCurrent() const {
    return yamlDraft_ && client_.credentialRevision(yamlDraft_->session) == std::optional<quint64>{yamlDraft_->revision};
}
bool Workspace::yamlDirty() const {
    return (yamlDraft_ && yamlDraft_->text != yamlDraft_->baseline)
        || (sourceInspection_ && sourceInspection_->editing && sourceInspection_->draft!=sourceInspection_->yaml);
}
QString Workspace::yamlDraftStatus() const {
    if (!yamlDraft_) return canEditYaml() ? QString{} : "Read-only. A successful fresh detail read with resource identity and version is required.";
    if (inspectorDocument_.isEmpty()) return "Current server detail is unavailable. Your local draft is preserved.";
    if (!yamlDraftCredentialsCurrent())
        return "Local draft preserved. Confirm sign-in and read fresh YAML under current credentials before Apply.";
    const auto baseline = yamlDraft_->document.value("metadata").toObject(), current = inspectorDocument_.value("metadata").toObject();
    if (baseline.value("uid") != current.value("uid") || baseline.value("resourceVersion") != current.value("resourceVersion"))
        return "Server resource version changed. Your local draft is preserved.";
    return yamlDirty() ? "Unapplied YAML draft. Changes are never sent automatically." : QString{};
}
bool Workspace::discardPending() const { return pendingLeave_.has_value(); }
QString Workspace::discardPrompt() const {
    if (pendingLeave_ && sourceInspection_ && sourceInspection_->editing)
        return "Discard unapplied kubeconfig edits for "+sourceInspection_->metadata.value("name").toString()
            +"?\nStay keeps your draft. Discard continues without importing it.";
    if (!pendingLeave_ || !yamlDraft_) return {};
    return "Discard unapplied YAML changes for " + yamlDraft_->document.value("metadata").toObject().value("name").toString()
        + "?\nStay keeps your draft. Discard continues the originally requested action without applying it.";
}
bool Workspace::beginYamlEdit() {
    if (yamlApplyLocked() || deletionPending() || deletionRunning()) return false;
    if (busy_ || !canEditYaml() || pendingLeave_) return false;
    const auto revision = client_.detailRevision(active_, inspectorPath());
    if (!revision) return false;
    yamlDraft_ = YamlDraft{active_, inspectorPath(), inspectorDocument_, yaml_, yaml_, {}, *revision};
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
    if (sourceInspection_ && sourceInspection_->editing) {
        sourceInspection_->editing=false; sourceInspection_->draft.clear();
        sourceInspection_->error.clear();
        emit sourceInspectionChanged();
        emit sourceDraftChanged(); emit yamlEditChanged();
    }
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
    if (sourceInspection_ && busy_) return false;
    if (pendingLeave_) return false;
    cancelPendingDeletion();
    if (!yamlDirty()) return clearYamlDraft();
    pendingLeave_ = PendingLeave{action, target, active_, historyIndex}; emit changed(); return false;
}
bool Workspace::leaveYamlEdit() { return allowLeave(Leave::Draft) && clearYamlDraft(); }
bool Workspace::requestWindowClose() {
    if (!allowLeave(Leave::Window)) { viewCloseDiscardApproved_ = false; return false; }
    if (viewCloseDiscardApproved_) return prepareWindowClose();
    if (viewSaving_ || presetsBusy_ || !pendingViews_.isEmpty() || viewStateFailed()) {
        viewClosePending_ = true; emit changed(); return false;
    }
    viewClosePending_ = false;
    return prepareWindowClose();
}
bool Workspace::confirmDiscard(bool discard) {
    if (!pendingLeave_) return false;
    const auto pending = *pendingLeave_; pendingLeave_.reset();
    if (!discard) {
        if (pending.action == Leave::Window) emit windowCloseCancelled();
        emit changed(); return true;
    }
    if (pending.origin != active_) {
        if (pending.action == Leave::Window) emit windowCloseCancelled();
        error_ = "The context changed. Your draft is preserved. Try again."; emit changed(); return false;
    }
    clearYamlDraft(); emit changed();
    switch (pending.action) {
    case Leave::Resource: return inspectResource(pending.target);
    case Leave::Source: return inspectSourceContext(pending.target);
    case Leave::Session: return activate(pending.target);
    case Leave::Context: return openContext(pending.target);
    case Leave::ContextTab: return openContext(pending.target, false, true);
    case Leave::Tab: return close(pending.target);
    case Leave::Inspector: return closeInspector();
    case Leave::Window: emit windowCloseApproved(); return true;
    case Leave::Detach: return detachSession(pending.target);
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
bool Workspace::yamlVisible() const { return currentNavigation().page == "yaml"; }
bool Workspace::valuesVisible() const { return currentNavigation().page == "values" && valuesAvailable(); }
bool Workspace::valuesAvailable() const { return isCoreValueResource(client_.resource(active_, inspectorPath())); }
QString Workspace::monospaceFamily() const { return QFontDatabase::families().contains("Menlo") ? QString("Menlo") : QFontDatabase::systemFont(QFontDatabase::FixedFont).family(); }
QString Workspace::syncProblem() const {
    auto message = client_.failureStatus(active_).section('\n', 0, 0);
    if (message.isEmpty()) return {};
    if (message.startsWith('/')) message = message.section(": ", 1);
    const auto context = mapContextCaption();
    return context.isEmpty() ? message : context + ": " + message;
}
QString Workspace::displayDateTime(const QString& value) const {
    const auto time = QDateTime::fromString(value, Qt::ISODateWithMs);
    return time.isValid() ? QLocale::system().toString(time.toLocalTime(), QLocale::ShortFormat) : QString{};
}
QVariantMap Workspace::settingsDiagnostics() const {
    const auto cache = client_.cacheDiagnostics();
    QVariantList metrics{
        QVariantMap{{"id", "cachePayload"}, {"label", "Cached payload (bytes)"}, {"value", cache.value("payloadBytes")},
            {"description", QString("%1 collections, %2 resources, %3 details, %4 log histories, %5 log lines. JSON and text estimate across contexts. Allocation overhead and shared UI data are excluded.")
                .arg(cache.value("collections").toLongLong()).arg(cache.value("resources").toLongLong()).arg(cache.value("details").toLongLong())
                .arg(cache.value("logHistories").toLongLong()).arg(cache.value("logLines").toLongLong())}},
        QVariantMap{{"label","Cached resources"},{"value",rows_.rowCount()},{"description","Current context, before filters"}},
        QVariantMap{{"label","Matching resources"},{"value",table_.rowCount()},{"description","Current cached filter result"}},
        QVariantMap{{"label","Cached Events"},{"value",eventRows_.rowCount()},{"description","Canonical Events, without duplicate API aliases"}},
        QVariantMap{{"label","Open tabs"},{"value",tabs().size()},{"description","Closed tabs do not refresh"}},
        QVariantMap{{"label","Sync progress"},{"value",QString::number(loadingProgress()*100,'f',0)+"%"},{"description",status()}},
        QVariantMap{{"label","Request limit"},{"value",settings_.requestHardLimitPerMinute},{"description","Per minute. 0 disables the extra limit. Reads are spaced at least 400 ms apart."}},
        QVariantMap{{"label","Retained logs"},{"value",QString::number(settings_.logLimitMb)+" MB / Pod"},{"description","Bounded in-memory log retention"}}};
    metrics = runtimeDiagnostics() + metrics;
    return {{"metrics",metrics},{"requests",client_.requestAudit(active_)},{"sampledAt",QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)}};
}
bool Workspace::publishInspector(bool revealChanged, bool resourcesChanged) {
    const auto path = inspectorPath();
    auto summary = path.isEmpty() ? QJsonObject{} : client_.resource(active_, path);
    const auto document = path.isEmpty() ? QJsonObject{} : client_.document(active_, path);
    const auto scope = path.isEmpty() ? QString{} : active_ + '\n' + path + '\n' + summary["uid"].toString();
    const bool scopeChanged = scope != inspectorScope_;
    const bool documentChanged = document != inspectorDocument_;
    if (!client_.detailRevision(active_, path)) {
        yamlFresh_ = false;
        if (!revealedValues_.isEmpty()) { revealedValues_.clear(); revealChanged = true; }
    }
    if (!path.isEmpty() && summary.isEmpty()) summary = {{"path", path}, {"name", QUrl::fromPercentEncoding(path.section('/', -1).toLatin1())}};
    if (!summary.isEmpty()) summary["cluster"] = activeCluster();
    if (!revealChanged && scope == inspectorScope_ && summary == inspectorSummary_ && document == inspectorDocument_) {
        if (resourcesChanged) publishRelated();
        return true;
    }
    emit inspectorPresentationChanging();
    if (scopeChanged) yamlFresh_ = false;
    if (yamlDraft_ && (yamlDraft_->session != active_ || yamlDraft_->path != path)) clearYamlDraft();
    if (scopeChanged || document.isEmpty()) revealedValues_.clear();
    inspectorScope_ = scope; inspectorSummary_ = summary;
    QVariantList fields;
    QStringList overview;
    const QStringList keys{"name", "kind", "namespace", "cluster", "status", "issue", "ready", "restarts", "owner", "node", "image", "createdAt", "uid", "resourceVersion", "fetchedAt"};
    const QStringList labels{"Name", "Kind", "Namespace", "Cluster", "Status", "Issue", "Ready", "Restarts", "Owner", "Node", "Image", "Created", "UID", "Version", "Detail retrieved"};
    const auto supported = filterFields();
    for (int index = 0; !summary.isEmpty() && index < keys.size(); ++index) {
        const auto cell = summary[keys[index]];
        auto value = cell.isDouble() ? QString::number(cell.toDouble(), 'g', 15) : cell.toString();
        if (keys[index] == "createdAt" || keys[index] == "fetchedAt") value = displayDateTime(value);
        if (value.isEmpty() && keys[index] == "namespace") value = "Cluster-scoped";
        if (value.isEmpty()) continue;
        QVariantMap field{{"id", keys[index]}, {"label", labels[index]}, {"value", value}};
        field["filterable"] = keys[index] != "createdAt" && cell.isString() && !cell.toString().isEmpty()
            && std::any_of(supported.cbegin(),supported.cend(),[&](const QVariant& candidate) { return candidate.toMap().value("id") == keys[index]; });
        if (keys[index] == "ready" && summary.contains("readyCount") && summary.contains("containerCount")) {
            const int ready = summary["readyCount"].toInt(), desired = summary["containerCount"].toInt();
            field["readiness"] = QVariantMap{{"fraction", desired > 0 ? std::clamp(double(ready)/desired, 0.0, 1.0) : 0.0},
                {"tone", desired == 0 ? "unknown" : ready >= desired ? "success" : ready == 0 ? "danger" : "warning"},
                {"description", QString("%1 ready: %2 of %3").arg(summary["apiVersion"] == "v1" && summary["kind"] == "Pod" ? "Containers" : "Replicas").arg(ready).arg(desired)}};
        }
        fields.append(field);
        overview.append(keys[index] + ": " + value);
    }
    overview_ = overview.join('\n');
    for (const auto& value : resourceMetricPresentation(summary)) {
        const auto metric=value.toMap();
        fields.append(QVariantMap{{"id", "metric_"+metric["id"].toString()}, {"label", metric["label"]}, {"value", metric["usage"]}, {"metric", metric}});
    }
    overviewFields_.publish(std::move(fields));
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
    valuesTable_.sort(currentNavigation().valueColumn, currentNavigation().valueOrder);
    emit inspectorPresentationChanged();
    if (!yamlDraft_ && (documentChanged || scopeChanged)) emit yamlTextChanged();
    if (documentChanged || scopeChanged) emit yamlEditChanged();
    if (resourcesChanged || scopeChanged || documentChanged) publishRelated();
    return true;
}
bool Workspace::publishRelated() {
    if (!inspectorPath().isEmpty() && inspectorPage() != "events" && inspectorPage() != "links") return true;
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
    const auto nav = currentNavigation();
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
    if (currentNavigation().page == page) return true;
    client_.hideLogs(viewId_); navigation_[active_].page = page;
    publishRelated(); publishLogs(); emit changed();
    if (page == "yaml" && !yamlDraft_ && inspectorDocument_.isEmpty()) refreshInspector();
    return true;
}
bool Workspace::closeInspector() {
    if (!allowLeave(Leave::Inspector)) return false;
    clearSourceInspection();
    client_.dismissInspector(active_); client_.hideLogs(viewId_);
    navigation_[active_].terminalError.clear();
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
    if (revealed && !client_.detailRevision(active_, inspectorPath())) return false;
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
int Workspace::collectionCacheHours() const { return settings_.collectionCacheHours; }
void Workspace::refreshCacheUsage() {
    cachePayloadBytes_=client_.cacheDiagnostics().value("payloadBytes").toLongLong(); emit cacheUsageChanged();
}
bool Workspace::saveReadSettings(int requestLimit, int inactiveMinutes, const QString& value, int cacheHours) {
    static const QRegularExpression integer("^[1-9][0-9]*$");
    bool valid = false;
    const int limit = value.toInt(&valid);
    if (!valid || !integer.match(value).hasMatch()) { settingsError_ = "Log-size limit must be a positive whole number of MB."; emit changed(); return false; }
    auto desired = settings_; desired.requestHardLimitPerMinute = requestLimit; desired.inactiveSyncMinutes = inactiveMinutes; desired.logLimitMb = limit; desired.collectionCacheHours=cacheHours;
    return savePolicy(desired);
}
bool Workspace::savePolicy(ReadSettings desired) {
    if (busy_) return false;
    if (!desired.valid()) { settingsError_ = "Invalid request settings."; emit changed(); return false; }
    busy_ = true; emit changed();
    auto* watcher = new QFutureWatcher<Result<ReadSettings>>(this);
    const bool audioChanged = desired.musicVolumePercent != settings_.musicVolumePercent
        || desired.alertVolumePercent != settings_.alertVolumePercent;
    connect(watcher, &QFutureWatcher<Result<ReadSettings>>::finished, this, [this, watcher, audioChanged] {
        const auto result = watcher->result(); watcher->deleteLater(); busy_ = false;
        const auto previousLanguage = settings_.language;
        if (const auto* failure = std::get_if<Failure>(&result)) settingsError_ = failure->message;
        else {
            settings_ = std::get<ReadSettings>(result); settingsError_.clear(); settingsReady_ = true;
            publishAppearance(); client_.configure(settings_); client_.enableRequests(true); refreshCacheUsage();
            for (auto* other : runtime_->windows) {
                if (other == this || other->windowClosed_) continue;
                const auto language = other->settings_.language;
                other->settings_ = settings_; other->settingsReady_ = true; other->publishAppearance();
                other->alerts_.setVolumePercent(settings_.alertVolumePercent);
                emit other->audioVolumesChanged(); emit other->changed();
                if (language != settings_.language) emit other->languageChanged();
            }
        }
        runtime_->music.setVolumePercent(settings_.musicVolumePercent);
        alerts_.setVolumePercent(settings_.alertVolumePercent);
        emit audioVolumesChanged(); emit changed(); emit appearanceChanged();
        if (previousLanguage != settings_.language || std::holds_alternative<Failure>(result)) emit languageChanged();
        if (audioChanged) emit audioVolumesSaveFinished(std::holds_alternative<ReadSettings>(result));
    });
    const auto profile = profile_; const auto expected = settings_;
    watcher->setFuture(QtConcurrent::run([profile, desired, expected] { return ReadSettingsStore(profile).save(desired, expected); }));
    return true;
}
bool Workspace::saveRadarWater(bool enabled, int speedPercent) {
    auto desired=settings_; desired.radarWaterEnabled=enabled; desired.radarWaterSpeedPercent=speedPercent;
    return savePolicy(desired);
}
bool Workspace::saveAudioVolumes(int musicPercent, int alertPercent) {
    auto desired = settings_;
    desired.musicVolumePercent = musicPercent; desired.alertVolumePercent = alertPercent;
    return desired == settings_ || savePolicy(desired);
}
bool Workspace::previewAudioVolume(bool music) {
    return alerts_.previewVolume(music ? settings_.musicVolumePercent : settings_.alertVolumePercent);
}
bool Workspace::saveWorkspaceRestore(bool enabled) {
    auto desired = settings_; desired.workspaceRestore = enabled;
    return savePolicy(desired);
}
bool Workspace::podInspected() const { return client_.resource(active_, currentNavigation().inspected)["kind"] == "Pod"; }
bool Workspace::logsVisible() const { return windowVisible_ && currentNavigation().page == "logs" && podInspected(); }
bool Workspace::logsPaused() const { return client_.logsPaused(active_, currentNavigation().inspected); }
QVariantList Workspace::logContainers() const {
    QVariantList options;
    for (const auto& id : client_.logContainers(active_, currentNavigation().inspected))
        options.append(QVariantMap{{"id", id}, {"name", id == "*" ? "all" : id == "all" ? "all (container)" : id}});
    return options;
}
QString Workspace::logSelection() const { return client_.logSelection(active_, currentNavigation().inspected); }
QString Workspace::logStatus() const { return client_.logStatus(active_, currentNavigation().inspected); }
int Workspace::logLimitMb() const { return settings_.logLimitMb; }
QAbstractItemModel* Workspace::logRows() { return &logs_; }
QString Workspace::logAnchor() const { return logPositions_.value(logScope_).anchor; }
double Workspace::logOffset() const { return logPositions_.value(logScope_).offset; }
bool Workspace::logFollow() const { return logPositions_.value(logScope_).follow; }
QString Workspace::logPositionNotice() const {
    const auto position = logPositions_.value(logScope_);
    if (!position.evicted) return {};
    const auto reason = position.removal == PodLogHistory::Removal::Limit ? "was removed by the log-size limit" : position.removal == PodLogHistory::Removal::Expiry ? "was removed by history retention" : "is no longer retained";
    return QString("The section being read %1. Showing the oldest retained entry; live following remains off.").arg(reason);
}
bool Workspace::rememberLogPosition(const QString& anchor, double offset, bool follow) {
    if (logScope_.isEmpty() || !std::isfinite(offset) || offset < 0) return false;
    auto& position = logPositions_[logScope_]; position.anchor = anchor; position.offset = offset; position.follow = follow;
    return true;
}
bool Workspace::followLogs() {
    if (logScope_.isEmpty()) return false;
    emit logViewChanging();
    logPositions_[logScope_] = LogPosition{}; emit logViewChanged(); return true;
}
bool Workspace::reportLogEviction() {
    if (logScope_.isEmpty()) return false;
    auto& position = logPositions_[logScope_]; position.anchor = logs_.entryId(0); position.offset = 0; position.follow = false; position.evicted = true;
    position.removal = client_.logRemoval(active_, currentNavigation().inspected);
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
    const auto path = currentNavigation().inspected;
    logScope_ = logsVisible() ? active_ + '\n' + path + '\n' + client_.resource(active_, path)["uid"].toString() + '\n' + logSelection() : QString{};
    logs_.publish(logsVisible() ? client_.logEntries(active_, currentNavigation().inspected) : QList<LogEntry>{});
    emit logViewChanged(); return true;
}
bool Workspace::setLogsVisible(bool visible) {
    emit logViewChanging();
    if (visible && (!podInspected() || !client_.showLogs(active_, currentNavigation().inspected, viewId_))) { error_ = "Container details are not available yet."; emit changed(); return false; }
    navigation_[active_].page = visible ? "logs" : "overview";
    if (!visible) client_.hideLogs(viewId_);
    publishLogs(); emit changed(); return true;
}
bool Workspace::setWindowVisible(bool visible) {
    if (windowVisible_ == visible) return true;
    emit logViewChanging(); windowVisible_ = visible;
    if (!visible && !revealedValues_.isEmpty()) { revealedValues_.clear(); publishInspector(true); }
    setWindowFocused(windowFocused_);
    if (visible) publish();
    else if (snapshotPreparation_) snapshotPreparation_->watcher->cancel();
    if (logsVisible()) client_.showLogs(active_, currentNavigation().inspected, viewId_);
    else client_.hideLogs(viewId_);
    publishLogs(); emit changed(); return true;
}
bool Workspace::selectLogContainer(const QString& container) { emit logViewChanging(); return client_.selectLogContainer(container, viewId_); }
bool Workspace::pauseLogs(bool paused) { return client_.pauseLogs(paused, viewId_); }
bool Workspace::eventFilter(QObject* watched, QEvent* event) {
    if (watched == window_ && (event->type() == QEvent::MouseButtonPress || event->type() == QEvent::KeyPress || event->type() == QEvent::Wheel || event->type() == QEvent::TouchBegin)) client_.userActivity(viewId_);
    return QObject::eventFilter(watched, event);
}
QString Workspace::activeCluster(const QString& session) const {
    if (const auto connection = client_.connection(session.isEmpty() ? active_ : session))
        for (const auto& source : sources_.sources) for (const auto& context : source.contexts)
            if (context.id == connection->contextId) return context.cluster;
    return {};
}
bool Workspace::publish() {
    const bool sessionChanged = publishedSession_ != active_;
    if (sessionChanged) {
        const auto outgoing = preparedSnapshots_.find(publishedSession_);
        if (outgoing != preparedSnapshots_.end()) rows_.retainPreparedSort(outgoing.value());
    }
    publishedSession_ = active_;
    alerts_.showSession(active_);
    const auto snapshot = client_.rows(active_);
    const auto cluster = activeCluster();
    const auto nav = currentNavigation();
    visibleTable_.setLimit(nav.limit);
    const auto prepared = preparedSnapshots_.constFind(active_);
    const bool ready = prepared != preparedSnapshots_.cend() && prepared->source == snapshot && prepared->cluster == cluster
        && rows_.publishPrepared(*prepared, sessionChanged);
    if (!ready) rows_.publish(snapshot, cluster, sessionChanged);
    QJsonArray eventSnapshot;
    int healthy=0, warning=0, critical=0;
    if (ready) {
        eventSnapshot = prepared->events;
        healthSummary_ = prepared->health;
    } else for (const auto& value : snapshot) {
        const auto row=value.toObject();
        if (row["kubernetesEvent"].toBool()) eventSnapshot.append(value);
        const int severity=row["problemSeverity"].toInt();
        if (severity==2) ++critical;
        else if (severity==1) ++warning;
        else ++healthy;
    }
    if (!ready) healthSummary_={{"total", snapshot.size()}, {"healthy", healthy}, {"warning", warning}, {"critical", critical}};
    if (!eventSnapshot.isEmpty() || eventRows_.rowCount() != 0) eventRows_.publish(eventSnapshot, cluster, sessionChanged);
    publishPorts();
    table_.filter(nav.filter, nav.fields, nav.mode); table_.sort(nav.column, nav.order);
    if (filterPickerSession_ != active_) {
        filterPickerSession_.clear(); filterPickerField_.clear(); filterPickerValues_.clear();
        emit filterPickerChanged(); emit fieldFiltersChanged();
    }
    events_.filter(nav.eventFilter); events_.sort(nav.eventColumn, nav.eventOrder);
    dashboardDirty_=true; publishDashboard();
    publishInspector(false, true); emit resourcePresentationChanged(); emit changed();
    prepareSnapshot();
    return true;
}
bool Workspace::reload() { return reloadCatalogs(true); }
bool Workspace::reloadCatalogs(bool reconnect) {
    if (busy_) return false;
    const auto credentialContext = credentialImportContext_;
    credentialImportContext_.clear();
    busy_ = true; emit changed();
    using Catalogs = std::tuple<Result<SourceCatalog>, Result<SessionCatalog>, Result<ReadSettings>>;
    const auto revision = runtime_->contextCatalogRevision;
    auto* watcher = new QFutureWatcher<Catalogs>(this);
    connect(watcher, &QFutureWatcher<Catalogs>::finished, this, [this, watcher, credentialContext, reconnect, revision] {
        const auto [sources, sessions, settings] = watcher->result(); watcher->deleteLater(); busy_ = false;
        if (revision != runtime_->contextCatalogRevision) {
            credentialImportContext_ = credentialContext;
            reloadCatalogs(reconnect);
            return;
        }
        if (const auto* failure = std::get_if<Failure>(&settings)) { settingsError_ = failure->message; settingsReady_ = false; client_.enableRequests(false); }
        else {
            const auto previousLanguage = settings_.language;
            settings_ = std::get<ReadSettings>(settings); settingsError_.clear(); settingsReady_ = true;
            publishAppearance(); client_.configure(settings_); client_.enableRequests(true);
            runtime_->music.setVolumePercent(settings_.musicVolumePercent);
            alerts_.setVolumePercent(settings_.alertVolumePercent);
            emit audioVolumesChanged();
            if (previousLanguage != settings_.language) emit languageChanged();
        }
        sourceCatalogFailed_ = std::holds_alternative<Failure>(sources);
        if (const auto* failure = std::get_if<Failure>(&sources)) error_ = failure->message;
        else {
            sources_ = std::get<SourceCatalog>(sources);
            for (auto use = sources_.usage.begin(); use != sources_.usage.end(); ++use) {
                auto& cached = runtime_->contextUsage[use.key()];
                if (use->count > cached.count) cached = use.value();
            }
            reconcileMapPreviews();
            error_ = sources_.errors.isEmpty() ? QString{} : "Some owned source records could not be read; healthy sources remain available.";
        }
        if (const auto* failure = std::get_if<Failure>(&sessions)) error_ = failure->message;
        else {
            if (!viewsLoaded_) catalog_ = std::get<SessionCatalog>(sessions);
            if (!settingsReady_) { emit catalogsChanged(); emit changed(); return; }
            const auto profile = profile_;
            using OrderedViews = std::pair<Result<SessionCatalog>, LoadedViewStates>;
            auto* order = new QFutureWatcher<OrderedViews>(this);
            const bool restore = !viewsLoaded_;
            const bool closeOnStartup = restore && !settings_.workspaceRestore;
            const auto schemas = tableSchemas(true);
            busy_ = true;
            connect(order, &QFutureWatcher<OrderedViews>::finished, this, [this, order, restore, credentialContext, reconnect] {
                const auto [result, views] = order->result(); order->deleteLater(); busy_ = false;
                if (const auto* failure = std::get_if<Failure>(&result)) { error_ = failure->message; emit changed(); }
                else {
                    if (restore) { catalog_ = std::get<SessionCatalog>(result); restoreViewStates(views); viewsLoaded_ = true; }
                    restoringMap_=restore;
                    const bool selected=select(std::get<SessionCatalog>(result));
                    restoringMap_=false;
                    if (restore) {
                        navigation_[active_].workspace="map";
                        emit changed();
                    } else if (reconnect && selected && !active_.isEmpty() && (client_.connection(active_)
                        || (!credentialContext.isEmpty() && sessionConfiguration(active_).value("contextId").toString() == credentialContext))) resolve(active_);
                }
            });
            QSet<QString> contextIds;
            for (const auto& source : sources_.sources) for (const auto& context : source.contexts) contextIds.insert(context.id);
            order->setFuture(QtConcurrent::run([profile, schemas, restore, closeOnStartup, contextIds] {
                const SessionStore store(profile);
                const ViewStateStore preferences(profile, schemas);
                auto catalog = store.list();
                if (closeOnStartup && std::holds_alternative<SessionCatalog>(catalog)) catalog = store.closeWorkspace();
                LoadedViewStates views;
                if (restore) if (const auto* tabs = std::get_if<SessionCatalog>(&catalog)) {
                    auto ids = contextIds;
                    for (const auto& tab : tabs->sessions) ids.insert(tab.config.contextId);
                    for (const auto& id : ids) views.insert(id, preferences.load(id));
                }
                return OrderedViews{std::move(catalog), std::move(views)};
            }));
        }
        emit catalogsChanged(); emit changed();
    });
    const auto profile = profile_;
    watcher->setFuture(QtConcurrent::run([profile] { return Catalogs{KubeconfigStore(profile).list(), SessionStore(profile).list(), ReadSettingsStore(profile).load()}; }));
    return true;
}
bool Workspace::select(const SessionCatalog& incoming, bool local) {
    auto catalog = incoming;
    if (!sourceCatalogFailed_) {
        QSet<QString> contexts;
        for (const auto& source : sources_.sources) for (const auto& context : source.contexts) contexts.insert(context.id);
        catalog.sessions.erase(std::remove_if(catalog.sessions.begin(), catalog.sessions.end(), [&](const auto& tab) { return !contexts.contains(tab.config.contextId); }), catalog.sessions.end());
        if (catalog.activeSession && std::none_of(catalog.sessions.begin(), catalog.sessions.end(), [&](const auto& tab) { return tab.id == *catalog.activeSession; })) catalog.activeSession.reset();
    }
    if (local) runtime_->publishCatalog(catalog, this);
    const auto selected = catalog.activeSession ? sessionId(*catalog.activeSession) : QString{};
    QString next = local && runtime_->owners.value(selected) == this ? selected
        : runtime_->owners.value(active_) == this ? active_ : QString{};
    if (!local && next.isEmpty())
        for (const auto& session : catalog.sessions)
            if (session.open && runtime_->owners.value(sessionId(session.id)) == this) { next = sessionId(session.id); break; }
    const bool overviewWasVisible = workspacePage() == "map" && mapOverview();
    const bool sessionChanged = next != active_;
    if (sessionChanged && !allowLeave(Leave::Reload)) return false;
    if (sessionChanged) clearSourceInspection();
    emit logViewChanging();
    catalog_ = catalog;
    active_ = next;
    const auto context = sessionConfiguration(active_).value("contextId").toString();
    if (runtime_->contextViews.contains(context)) applyContextViews(context, runtime_->contextViews.value(context), false);
    rememberMapSession(sessionChanged);
    if (!overviewWasVisible && workspacePage() == "map" && mapOverview()) refreshMapContexts(true);
    client_.showSession(workspacePage()=="map" && mapOverview() ? QString{} : active_, viewId_);
    emit catalogsChanged();
    if (sessionChanged || active_.isEmpty()) publish();
    else emit changed();
    if (logsVisible()) client_.showLogs(active_, currentNavigation().inspected, viewId_);
    else { client_.hideLogs(viewId_); publishLogs(); }
    return true;
}
bool Workspace::importFile(const QString& path) {
    if (!retainDocumentReadAccess(path)) {
        sourceImportError_ = error_ = "Android cannot retain access to this document. Select it again from the system file picker.";
        emit changed();
        return false;
    }
    return importSource([path](const KubeconfigStore& store) { return store.importPath(path); });
}
bool Workspace::importOciCredentials(const QString& contextId, const QString& configPath, const QString& keyPath) {
    if (!retainDocumentReadAccess(configPath) || !retainDocumentReadAccess(keyPath)) {
        sourceImportError_ = error_ = "Android cannot retain access to the selected OCI files. Select them again from the system file picker.";
        emit changed();
        return false;
    }
    return importSource([contextId, configPath, keyPath](const KubeconfigStore& store) -> Result<SourceImportReport> {
        const auto imported = store.importOciCredentials(contextId, configPath, keyPath);
        if (const auto* failure = std::get_if<Failure>(&imported)) return *failure;
        return SourceImportReport{{std::get<SourceSnapshot>(imported)}, {}};
    }, false, contextId);
}
bool Workspace::importGkeServiceAccount(const QString& contextId, const QString& keyPath) {
    if (!retainDocumentReadAccess(keyPath)) {
        sourceImportError_ = error_ = "Android cannot read the selected Google service-account key. Select it again from the system file picker.";
        emit changed();
        return false;
    }
    return importSource([contextId, keyPath](const KubeconfigStore& store) -> Result<SourceImportReport> {
        const auto imported = store.importGkeServiceAccount(contextId, keyPath);
        if (const auto* failure = std::get_if<Failure>(&imported)) return *failure;
        return SourceImportReport{{std::get<SourceSnapshot>(imported)}, {}};
    }, false, contextId);
}
bool Workspace::importAwsProfile(const QString& contextId, const QString& configPath, const QString& credentialsPath) {
    for (const auto& path : {configPath, credentialsPath}) if (!path.isEmpty() && !retainDocumentReadAccess(path)) {
        sourceImportError_ = error_ = "Cannot read the selected AWS profile document. Select it again from the system file picker.";
        emit changed(); return false;
    }
    return importSource([contextId, configPath, credentialsPath](const KubeconfigStore& store) -> Result<SourceImportReport> {
        const auto imported = store.importAwsProfile(contextId, configPath, credentialsPath);
        if (const auto* failure = std::get_if<Failure>(&imported)) return *failure;
        return SourceImportReport{{std::get<SourceSnapshot>(imported)}, {}};
    }, false, contextId);
}
bool Workspace::importFiles(const QStringList& inputs) {
    auto paths = inputs; paths.removeDuplicates();
    if (paths.isEmpty()) return false;
    for (const auto& path : paths) if (!retainDocumentReadAccess(path)) {
        sourceImportError_ = error_ = "Cannot access the selected file. Choose it again.";
        emit changed(); return false;
    }
    return importSource([paths](const KubeconfigStore& store) -> Result<SourceImportReport> {
        SourceImportReport report; std::optional<Failure> rejected;
        for (const auto& path : paths) {
            const auto imported = store.importPath(path);
            if (const auto* failure = std::get_if<Failure>(&imported)) {
                if (!rejected || (rejected->sourceInput && !failure->sourceInput)) rejected = *failure;
                if (!failure->sourceInput) report.errors.append({path, *failure});
            } else {
                const auto& result = std::get<SourceImportReport>(imported);
                report.sources.append(result.sources); report.errors.append(result.errors);
            }
        }
        if (report.sources.isEmpty() && rejected) return *rejected;
        return report;
    }, true);
}
bool Workspace::quickImportFile(const QString& path) {
    if (!retainDocumentReadAccess(path)) {
        sourceImportError_ = error_ = "Android cannot retain access to this document. Select it again from the system file picker.";
        emit changed();
        return false;
    }
    return importSource([path](const KubeconfigStore& store) -> Result<SourceImportReport> {
        const auto imported = store.importFile(path);
        if (const auto* failure = std::get_if<Failure>(&imported)) return *failure;
        return SourceImportReport{{std::get<SourceSnapshot>(imported)}, {}};
    }, true);
}
QString Workspace::defaultSourceOrigin() const { return appStore() ? QDir(profile_).filePath("pasted-kubeconfig") : QDir::home().filePath(".kube/pasted-kubeconfig"); }
bool Workspace::importHome() { return !appStore() && importFile(QDir::home().filePath(".kube/config")); }
bool Workspace::importText(QString originPath, const QString& yaml) {
    return importSource([originPath = std::move(originPath), yaml](const KubeconfigStore& store) -> Result<SourceImportReport> {
        const auto result = store.importText(originPath, yaml);
        if (const auto* failure = std::get_if<Failure>(&result)) return *failure;
        return SourceImportReport{{std::get<SourceSnapshot>(result)}, {}};
    });
}
bool Workspace::refreshSources() {
    if (appStore()) return false;
    QStringList paths;
    for (const auto& source : sources_.sources)
        paths.append(source.sourcePath);
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
    }, false, {}, SourceReload::MetadataOnly);
}
bool Workspace::requestSourceRemoval(const QString& contextId) {
    if (busy_ || pendingLeave_ || !sourceRemoval_.isEmpty()) return false;
    sourceImportError_.clear(); sourceImportNotice_.clear();
    for (const auto& source : sources_.sources) for (const auto& context : source.contexts) {
        if (context.id != contextId) continue;
        QStringList ids;
        for (const auto& session : catalog_.sessions) if (session.config.contextId == contextId) {
            ids.append(sessionId(session.id));
        }
        if (yamlDirty() && (ids.contains(active_) || (sourceInspection_ && sourceInspection_->metadata.value("contextId")==contextId))) {
            sourceImportError_ = "Save or discard the YAML draft before removing its context.";
            emit changed(); return false;
        }
        sourceRemoval_ = {{"contextId", contextId}, {"name", context.displayName}, {"sessionIds", ids}};
        emit sourceRemovalChanged(); emit changed(); return true;
    }
    sourceImportError_ = "The selected imported context is unavailable.";
    emit changed(); return false;
}
bool Workspace::cancelSourceRemoval() {
    if (busy_ || sourceRemoval_.isEmpty()) return false;
    sourceRemoval_.clear(); sourceImportError_.clear();
    emit sourceRemovalChanged(); emit changed(); return true;
}
bool Workspace::confirmSourceRemoval() {
    if (busy_ || pendingLeave_ || sourceRemoval_.isEmpty()) return false;
    const auto contextId = sourceRemoval_["contextId"].toString();
    if ((viewSaving_ && viewSaveContext_ == contextId) || pendingViews_.contains(contextId)) {
        sourceImportError_ = "Wait for the context settings to finish saving."; emit changed(); return false;
    }
    const auto ids = sourceRemoval_["sessionIds"].toStringList();
    for (auto* owner : runtime_->windows)
        if (owner->sourceInspection_ && owner->sourceInspection_->metadata.value("contextId")==contextId
            && (owner->busy_ || owner->sourceEditing() || owner->yamlDirty() || owner->pendingLeave_)) {
            sourceImportError_="Save or discard the source draft in its owning window before removing this context.";
            emit changed(); return false;
        }
    for (const auto& id : ids)
        if (const auto* owner = runtime_->owners.value(id); owner && owner != this && (owner->busy_ || owner->yamlDirty() || owner->pendingLeave_)) {
            sourceImportError_ = "Finish the operation or YAML draft in the owning window before removing this context.";
            emit changed(); return false;
        }
    if (yamlDirty() && ids.contains(active_)) {
        sourceImportError_ = "Save or discard the YAML draft before removing its context.";
        emit changed(); return false;
    }
    sourceImportError_.clear(); busy_ = true; emit changed();
    for (auto* owner : runtime_->windows)
        if (owner->sourceInspection_ && owner->sourceInspection_->metadata.value("contextId")==contextId)
            emit owner->sourceInspectionChanged();
    auto* watcher = new QFutureWatcher<Result<SessionCatalog>>(this);
    connect(watcher, &QFutureWatcher<Result<SessionCatalog>>::finished, this, [this, watcher, contextId, ids] {
        const auto result = watcher->result(); watcher->deleteLater(); busy_ = false;
        const auto* failure = std::get_if<Failure>(&result);
        if (failure && failure->code!=StoreError::CleanupFailed)
            for (auto* owner : runtime_->windows)
                if (owner->sourceInspection_ && owner->sourceInspection_->metadata.value("contextId")==contextId)
                    emit owner->sourceInspectionChanged();
        if (!failure || failure->code == StoreError::CleanupFailed) {
            ++runtime_->contextCatalogRevision;
            for (auto* owner : runtime_->windows)
                if (owner->sourceInspection_ && owner->sourceInspection_->metadata.value("contextId")==contextId)
                    owner->clearSourceInspection();
            runtime_->contextUsage.remove(contextId);
            runtime_->pendingUsage.remove(contextId);
            runtime_->savingUsage.remove(contextId);
            credentials_.revoke(importedCredentialId(contextId));
            for (const auto& id : ids) {
                client_.close(id); alerts_.closeSession(id); navigation_.remove(id);
            }
        }
        if (failure) {
            sourceImportError_ = failure->message; emit changed();
            if (failure->code == StoreError::CleanupFailed) {
                sourceRemoval_.clear(); emit sourceRemovalChanged(); reload();
            } else if (failure->code == StoreError::Conflict) reload();
            return;
        }
        for (auto& source : sources_.sources)
            source.contexts.erase(std::remove_if(source.contexts.begin(), source.contexts.end(),
                [&](const auto& context) { return context.id == contextId; }), source.contexts.end());
        sources_.sources.erase(std::remove_if(sources_.sources.begin(), sources_.sources.end(),
            [](const auto& source) { return source.contexts.isEmpty(); }), sources_.sources.end());
        reconcileMapPreviews(); runtime_->previews.forget(contextId);
        runtime_->contextViews.remove(contextId); savedViews_.remove(contextId);
        pendingViews_.remove(contextId); viewStateErrors_.remove(contextId);
        const auto& catalog = std::get<SessionCatalog>(result);
        if (viewsLoaded_) select(catalog);
        else catalog_ = catalog;
        sourceRemoval_.clear();
        sourceImportNotice_.clear();
        sourceImportError_.clear();
        emit sourceRemovalChanged(); emit changed();
        if (!viewsLoaded_) reloadCatalogs(false);
    });
    const auto profile = profile_;
    sessionMutationFuture_ = QtConcurrent::run([profile, contextId, ids] { return KubeconfigStore(profile).removeContext(contextId, ids); });
    watcher->setFuture(sessionMutationFuture_);
    return true;
}
bool Workspace::importSource(const std::function<Result<SourceImportReport>(const KubeconfigStore&)>& operation, bool ignoreInvalid, const QString& credentialContext, SourceReload reload) {
    if (busy_) return false;
    if (!ignoreInvalid) { sourceImportError_.clear(); sourceImportNotice_.clear(); sourceImportIssues_.clear(); }
    busy_ = true; emit changed();
    auto* watcher = new QFutureWatcher<Result<SourceImportReport>>(this);
    connect(watcher, &QFutureWatcher<Result<SourceImportReport>>::finished, this, [this, watcher, ignoreInvalid, credentialContext, reload] {
        const auto result = watcher->result(); watcher->deleteLater(); busy_ = false;
        if (const auto* failure = std::get_if<Failure>(&result)) {
            if (!ignoreInvalid || !failure->sourceInput) {
                sourceImportError_ = failure->message; error_ = failure->message;
            }
            emit changed(); emit sourceImportFinished(false);
        }
        else {
            const auto& report = std::get<SourceImportReport>(result);
            ++runtime_->contextCatalogRevision;
            sourceImportError_.clear(); sourceImportIssues_.clear();
            sourceImportNotice_ = report.errors.isEmpty() ? QString{} : QString("Some files could not be opened.");
            for (const auto& issue : report.errors) sourceImportIssues_.append(QVariantMap{{"sourcePath", issue.sourcePath}, {"message", issue.failure.message}});
            if (!credentialContext.isEmpty()) credentials_.revoke(importedCredentialId(credentialContext));
            credentialImportContext_ = credentialContext;
            if (!reloadCatalogs(reload == SourceReload::Configuration)) credentialImportContext_.clear();
            emit sourceImportFinished(!report.sources.isEmpty() && report.errors.isEmpty());
        }
    });
    const auto profile = profile_;
    sourceImportFuture_ = QtConcurrent::run([profile, operation] { return operation(KubeconfigStore(profile)); });
    watcher->setFuture(sourceImportFuture_);
    return true;
}
bool Workspace::mutate(const std::function<Result<SessionCatalog>(const SessionStore&)>& operation, const QString& target, bool inWindow, const QString& usedContext) {
    if (busy_) return false;
    busy_ = true; emit changed();
    auto* watcher = new QFutureWatcher<Result<SessionCatalog>>(this);
    connect(watcher, &QFutureWatcher<Result<SessionCatalog>>::finished, this, [this, watcher, target, inWindow, usedContext] {
        const auto result = watcher->result(); watcher->deleteLater(); busy_ = false;
        if (const auto* failure = std::get_if<Failure>(&result)) { error_ = failure->message; select(catalog_); return; }
        const auto catalog = std::get<SessionCatalog>(result);
        if (!usedContext.isEmpty()) runtime_->recordContextUse(usedContext);
        if (!target.isEmpty()) { client_.close(target); alerts_.closeSession(target); navigation_.remove(target); }
        if (inWindow && catalog.activeSession) {
            catalog_ = catalog; runtime_->publishCatalog(catalog, this);
            const auto id = sessionId(*catalog.activeSession);
            if (!runtime_->host || !runtime_->host->detach(*this, id)) {
                error_ = "The context tab is open, but its separate window could not be created. Retry from its tab.";
                emit changed(); emit catalogsChanged();
            } else if (auto* owner = runtime_->owners.value(id)) owner->resolve(id);
        } else {
            if (!select(catalog)) return;
            if (!active_.isEmpty()) {
                const auto connection = client_.connection(active_);
                const auto session = std::find_if(catalog.sessions.cbegin(), catalog.sessions.cend(),
                    [&](const auto& value) { return sessionId(value.id) == active_; });
                if (connection && session != catalog.sessions.cend())
                    client_.open(active_, *connection, runtime_->contextViews.value(session->config.contextId).value("resource").namespaces);
                else resolve(active_);
            } else emit changed();
        }
    });
    sessionMutationFuture_ = QtConcurrent::run([profile = profile_, operation] { return operation(SessionStore(profile)); });
    watcher->setFuture(sessionMutationFuture_);
    return true;
}
QVariantMap Workspace::sessionConfiguration(const QString& id) const {
    const auto found = std::find_if(catalog_.sessions.cbegin(), catalog_.sessions.cend(), [&](const auto& session) { return sessionId(session.id) == id; });
    if (found == catalog_.sessions.cend()) return {};
    return {{"id", id}, {"contextId", found->config.contextId},
        {"namespaces", runtime_->contextViews.value(found->config.contextId).value("resource").namespaces.join(", ")}};
}
QVariantList Workspace::resourceGuidance() const {
    return podlord::resourceGuidance(client_.document(active_, currentNavigation().inspected));
}
bool Workspace::openContext(const QString& context, bool inWindow, bool duplicate) {
    if (busy_ || pendingLeave_) return false;
    if (!viewsLoaded_ || viewStateErrors_.contains(context)) {
        if (viewsLoaded_ || error_.isEmpty())
            error_ = viewStateErrors_.value(context, "Context settings could not be loaded. Reload the app settings before opening a context.");
        emit changed(); return false;
    }
    if (!contextUsable(context)) { error_ = "Choose an available, usable imported context."; emit changed(); return false; }
    if (inWindow && !windowActionsAvailable()) return false;
    if (!inWindow && !duplicate) {
        auto existing = std::find_if(catalog_.sessions.cbegin(), catalog_.sessions.cend(), [&](const auto& tab) {
            return tab.config.contextId == context && sessionId(tab.id) == active_;
        });
        if (existing == catalog_.sessions.cend()) existing = std::find_if(catalog_.sessions.cbegin(), catalog_.sessions.cend(), [&](const auto& tab) {
            return tab.config.contextId == context && runtime_->owners.value(sessionId(tab.id)) == this;
        });
        if (existing == catalog_.sessions.cend()) existing = std::find_if(catalog_.sessions.cbegin(), catalog_.sessions.cend(), [&](const auto& tab) {
            return tab.config.contextId == context;
        });
        if (existing != catalog_.sessions.cend()) {
            const auto id = sessionId(existing->id);
            auto* owner = runtime_->owners.value(id, this);
            owner->navigation_[id].workspace = "map";
            const bool accepted = owner->activate(id);
            if (accepted) { owner->rememberMapSession(); emit owner->changed(); }
            return accepted && (owner == this || (runtime_->host && runtime_->host->focus(*owner)));
        }
    }
    if (inWindow && (viewSaving_ || !pendingViews_.isEmpty() || viewStateFailed())) return false;
    if (!inWindow && !allowLeave(duplicate ? Leave::ContextTab : Leave::Context, context)) return false;
    return mutate([context](const SessionStore& store) { return store.openTab(context); }, {}, inWindow, context);
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
    if (!viewsLoaded_ || viewStateErrors_.contains(found->config.contextId)) return false;
    if (auto* owner = runtime_->owners.value(id); owner && owner != this)
        return owner->activate(id) && runtime_->host && runtime_->host->focus(*owner);
    const bool entered = id != active_ || (workspacePage() == "map" && mapOverview());
    if (id == active_ && found->open && catalog_.activeSession == QUuid(id)) {
        if (workspacePage()=="map") { rememberMapSession(); client_.showSession(id,viewId_); emit changed(); }
        const bool accepted = client_.connection(id) ? true : resolve(id);
        if (accepted && entered) runtime_->recordContextUse(found->config.contextId);
        return accepted;
    }
    if (pendingLeave_ || (id != active_ && !allowLeave(Leave::Session, id))) return false;
    if (!client_.showSession(id, viewId_)) return false;
    emit logViewChanging(); active_ = id; rememberMapSession(); publish();
    if (logsVisible()) client_.showLogs(id, navigation_.value(id).inspected, viewId_);
    else publishLogs();
    const bool accepted = mutate([id](const SessionStore& store) { return store.activate(QUuid(id)); });
    if (accepted && entered) runtime_->recordContextUse(found->config.contextId);
    return accepted;
}
bool Workspace::close(const QString& id) {
    if (auto* owner = runtime_->owners.value(id); owner && owner != this) return owner->close(id);
    if (busy_ || pendingLeave_ || (id == active_ && !allowLeave(Leave::Tab, id))) return false;
    return mutate([id](const SessionStore& store) { return store.close(QUuid(id)); }, id);
}
bool Workspace::resolve(const QString& id, bool retry) {
    if (!settingsReady_ || !viewsLoaded_) return false;
    const auto found = std::find_if(catalog_.sessions.cbegin(), catalog_.sessions.cend(), [&](const auto& session) { return sessionId(session.id) == id; });
    if (found == catalog_.sessions.cend()) return false;
    if (viewStateErrors_.contains(found->config.contextId)) return false;
    auto config = found->config;
    config.namespaces = runtime_->contextViews.value(config.contextId).value("resource").namespaces;
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
            if (id == active_ && !yamlFresh_ && !inspectorPath().isEmpty()) client_.inspect(id, inspectorPath());
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
    if (currentNavigation().filter == text) return true;
    navigation_[active_].filter = text;
    return applyResourceFilters();
}
bool Workspace::publishDashboard() {
    if (!dashboardDirty_) return true;
    int problems=0;
    ResourceMetricSummary metricSummary;
    for (int index=0; index<table_.rowCount(); ++index) {
        const int sourceRow = table_.mapToSource(table_.index(index, 0)).row();
        if (rows_.row(sourceRow)["problems"].toBool()) ++problems;
        rows_.accumulateMetrics(sourceRow, metricSummary);
    }
    const auto summary=uiText().value("dashboard.summary").toString()
        .arg(table_.rowCount()).arg(rows_.rowCount()).arg(problems);
    const auto metrics=metricSummary.presentation();
    const bool changed=dashboardSummary_!=summary || pulseMetrics_!=metrics;
    dashboardSummary_=summary; pulseMetrics_=metrics; dashboardDirty_=false;
    if (changed) emit dashboardChanged();
    return true;
}
bool Workspace::setWorkspacePage(const QString& page) {
    if (page != "map" && page != "resources" && page != "ports" && page != "settings" && page != "events" && page != "alerts" && page != "dashboard") return false;
    if (currentNavigation().workspace == page) return true;
    navigation_[active_].workspace = page;
    if (page=="map") rememberMapSession();
    client_.showSession(page=="map" && mapOverview() ? QString{} : active_,viewId_);
    if (page=="dashboard") publishDashboard();
    emit changed();
    if ((page=="resources" || page=="events" || page=="ports" || page=="dashboard") && !active_.isEmpty() && !client_.connection(active_)) return resolve(active_);
    return true;
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
    table_.sort(nav.column, nav.order); publishResourceColumns(); queueViewSave("resource"); emit changed(); return true;
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
    if (currentNavigation().eventFilter == text) return true;
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
    clearSourceInspection();
    emit logViewChanging();
    auto& nav = navigation_[active_];
    if (historyIndex >= 0) nav.historyIndex = historyIndex;
    else if (nav.historyIndex < 0 || nav.history.value(nav.historyIndex) != path) {
        nav.history.insert(nav.historyIndex + 1, path); ++nav.historyIndex;
        if (nav.history.size() > 32) { nav.history.removeFirst(); --nav.historyIndex; }
    }
    if (nav.inspected != path) {
        nav.terminalError.clear();
        if (const auto* shell = containerTerminal(); shell && !shell->active()) client_.stopTerminal(active_);
    }
    nav.inspected = path;
    yamlFresh_ = false;
    publishInspector();
    emit yamlEditChanged();
    if (logsVisible()) client_.showLogs(active_, navigation_[active_].inspected, viewId_);
    else { client_.hideLogs(viewId_); publishLogs(); }
    emit changed();
    return client_.inspect(active_, navigation_[active_].inspected);
}
int Workspace::inspectorHistoryTarget(int step) const {
    if ((step != -1 && step != 1) || inspectorPath().isEmpty() || busy_ || pendingLeave_) return -1;
    const auto nav = currentNavigation();
    for (int i = nav.historyIndex + step; i >= 0 && i < nav.history.size(); i += step)
        if (client_.containsResource(active_, nav.history[i])) return i;
    return -1;
}
bool Workspace::canInspectBack() const { return inspectorHistoryTarget(-1) >= 0; }
bool Workspace::canInspectForward() const { return inspectorHistoryTarget(1) >= 0; }
bool Workspace::inspectHistory(const QString& path, int index) {
    const auto nav = currentNavigation();
    if (index < 0 || index >= nav.history.size() || nav.history[index] != path || !client_.containsResource(active_, path)) {
        error_ = "This resource is no longer available in this context."; emit changed(); return false;
    }
    openInspector(path, index);
    return true;
}
bool Workspace::navigateInspector(int step) {
    const auto index = inspectorHistoryTarget(step);
    if (index < 0) return false;
    const auto path = currentNavigation().history[index];
    return allowLeave(Leave::History, path, index) && inspectHistory(path, index);
}
bool Workspace::copyCell(int row, int column) {
    const auto index = table_.index(row, column);
    if (!index.isValid()) return false;
    QGuiApplication::clipboard()->setText(table_.data(index).toString()); return true;
}
} // namespace podlord
