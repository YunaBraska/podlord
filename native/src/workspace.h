#pragma once
#include "resource_client.h"
#include "container_terminal.h"
#include "credential_process.h"
#include "yaml_apply.h"
#include "appearance.h"
#include "table_layout.h"
#include "view_state.h"
#include <QFuture>
#include "alerts.h"
#include <QAbstractTableModel>
#include <QSortFilterProxyModel>
#include <QVariantList>

namespace podlord {
class ResourceTable final : public QAbstractTableModel {
    Q_OBJECT
public:
    explicit ResourceTable(QObject* parent = nullptr,
                           QStringList fields = {"name", "kind", "namespace", "status", "node", "image", "cluster", "cpu", "memory", "storage", "createdAt", "ready", "restarts", "owner", "issue", "uid"},
                           QStringList captions = {"Name", "Kind", "Namespace", "Status", "Node", "Image", "Cluster", "CPU", "Memory", "Storage", "Age", "Ready", "Restarts", "Owner", "Issue", "UID"}, QString identityField = "path");
    int rowCount(const QModelIndex& parent = {}) const override;
    int columnCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    bool publish(const QJsonArray& rows, const QString& cluster = {});
    QJsonObject row(int index) const;
    bool setAppearance(const Appearance& appearance);
private:
    const QStringList fields_, captions_;
    const QString identityField_;
    QList<QJsonObject> rows_;
    Appearance appearance_;
    QString cluster_;
};
class ResourceFilter : public QSortFilterProxyModel {
    Q_OBJECT
    Q_PROPERTY(QString error READ error NOTIFY filterStateChanged)
public:
    explicit ResourceFilter(QObject* parent = nullptr);
    Q_INVOKABLE bool filter(const QString& text, const QMap<QString, QString>& fields = {}, const QString& mode = {});
    static QStringList exactValues(const QString& expression);
    static Result<QString> selectValue(const QString& expression, const QString& value, bool selected);
    QString error() const { return error_; }
signals:
    void filterStateChanged();
protected:
    bool filterAcceptsRow(int row, const QModelIndex& parent) const override;
    bool lessThan(const QModelIndex& left, const QModelIndex& right) const override;
private:
    QString text_, mode_;
    struct Token final {
        enum Kind { Contains, Exact, Prefix, Suffix, Number, Regex, Quantity } kind = Contains;
        QString value, operation, raw;
        double number = 0;
        QRegularExpression pattern;
    };
    static Result<QList<Token>> compileQuantity(const QString& text, bool cpu);
    static Result<QList<Token>> compileDuration(const QString& text);
    bool matchesQuantity(const QVariant& value, const QList<Token>& tokens) const;
    QList<Token> tokens_;
    QMap<QString, QString> fields_;
    QMap<int, QList<Token>> fieldTokens_;
    static Result<QList<Token>> compile(const QString& text);
    bool matches(const QString& value, const QList<Token>& tokens) const;
    bool compiled_ = false;
    mutable QString error_;
    mutable bool errorNotificationPending_ = false;
    bool refreshFilter();
};
/** UI reads snapshots only. Public actions schedule real stores/transport work. */
class Workspace final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList contexts READ contexts NOTIFY catalogsChanged)
    Q_PROPERTY(podlord::Alerts* alerts READ alerts CONSTANT)
    Q_PROPERTY(QStringList themeNames READ themeNames CONSTANT)
    Q_PROPERTY(QString themeName READ themeName NOTIFY appearanceChanged)
    Q_PROPERTY(QString themeVariant READ themeVariant NOTIFY appearanceChanged)
    Q_PROPERTY(QString themeIntensity READ themeIntensity NOTIFY appearanceChanged)
    Q_PROPERTY(QPalette appearancePalette READ appearancePalette NOTIFY appearanceChanged)
    Q_PROPERTY(QVariantMap appearanceColors READ appearanceColors NOTIFY appearanceChanged)
    Q_PROPERTY(QString sourceImportNotice READ sourceImportNotice NOTIFY changed)
    Q_PROPERTY(QVariantList sourceImportIssues READ sourceImportIssues NOTIFY changed)
    Q_PROPERTY(QVariantList sessions READ sessions NOTIFY catalogsChanged)
    Q_PROPERTY(QVariantList tabs READ tabs NOTIFY catalogsChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(bool workspaceRestoreEnabled READ workspaceRestoreEnabled NOTIFY changed)
    Q_PROPERTY(bool loading READ loading NOTIFY changed)
    Q_PROPERTY(bool syncLoading READ syncLoading NOTIFY changed)
    Q_PROPERTY(double loadingProgress READ loadingProgress NOTIFY changed)
    Q_PROPERTY(bool problemsOnly READ problemsOnly NOTIFY fieldFiltersChanged)
    Q_PROPERTY(bool activityOnly READ activityOnly NOTIFY fieldFiltersChanged)
    Q_PROPERTY(QStringList filterPresets READ filterPresets NOTIFY filterPresetsChanged)
    Q_PROPERTY(QString selectedFilterPreset READ selectedFilterPreset NOTIFY fieldFiltersChanged)
    Q_PROPERTY(bool filterPresetsBusy READ filterPresetsBusy NOTIFY filterPresetsChanged)
    Q_PROPERTY(QString filterPresetsError READ filterPresetsError NOTIFY filterPresetsChanged)
    Q_PROPERTY(bool authenticationRequired READ authenticationRequired NOTIFY changed)
    Q_PROPERTY(bool authenticationRunning READ authenticationRunning NOTIFY changed)
    Q_PROPERTY(QString authenticationPrompt READ authenticationPrompt NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
    Q_PROPERTY(bool viewStateFailed READ viewStateFailed NOTIFY changed)
    Q_PROPERTY(bool viewCloseNeedsDecision READ viewCloseNeedsDecision NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(QString currentSession READ currentSession NOTIFY changed)
    Q_PROPERTY(QString title READ title NOTIFY changed)
    Q_PROPERTY(QString sessionRenameError READ sessionRenameError NOTIFY changed)
    Q_PROPERTY(QVariantList resourceGuidance READ resourceGuidance NOTIFY inspectorPresentationChanged)
    Q_PROPERTY(QString sourceImportError READ sourceImportError NOTIFY changed)
    Q_PROPERTY(QVariantMap sourceRemoval READ sourceRemoval NOTIFY sourceRemovalChanged)
    Q_PROPERTY(QString defaultSourceOrigin READ defaultSourceOrigin CONSTANT)
    Q_PROPERTY(QString sessionManagementError READ sessionManagementError NOTIFY changed)
    Q_PROPERTY(QString sessionManagementNotice READ sessionManagementNotice NOTIFY changed)
    Q_PROPERTY(QString filterText READ filterText NOTIFY changed)
    Q_PROPERTY(QVariantList filterFields READ filterFields CONSTANT)
    Q_PROPERTY(QVariantMap resourceFieldFilters READ resourceFieldFilters NOTIFY fieldFiltersChanged)
    Q_PROPERTY(QString filterPickerField READ filterPickerField NOTIFY filterPickerChanged)
    Q_PROPERTY(QStringList filterPickerValues READ filterPickerValues NOTIFY filterPickerChanged)
    Q_PROPERTY(QString filterError READ filterError NOTIFY changed)
    Q_PROPERTY(QString workspacePage READ workspacePage NOTIFY changed)
    Q_PROPERTY(QString eventFilterText READ eventFilterText NOTIFY changed)
    Q_PROPERTY(int eventSortColumnIndex READ eventSortColumnIndex NOTIFY changed)
    Q_PROPERTY(QString eventSortDirection READ eventSortDirection NOTIFY changed)
    Q_PROPERTY(QAbstractItemModel* eventTable READ eventTable CONSTANT)
    Q_PROPERTY(int eventCount READ eventCount NOTIFY resourcePresentationChanged)
    Q_PROPERTY(int totalEventCount READ totalEventCount NOTIFY resourcePresentationChanged)
    Q_PROPERTY(int resourceCount READ resourceCount NOTIFY resourcePresentationChanged)
    Q_PROPERTY(int totalResourceCount READ totalResourceCount NOTIFY resourcePresentationChanged)
    Q_PROPERTY(QVariantMap healthSummary READ healthSummary NOTIFY resourcePresentationChanged)
    Q_PROPERTY(QAbstractItemModel* dashboardTable READ dashboardTable CONSTANT)
    Q_PROPERTY(QString dashboardSummary READ dashboardSummary NOTIFY dashboardChanged)
    Q_PROPERTY(QVariantList pulseMetrics READ pulseMetrics NOTIFY dashboardChanged)
    Q_PROPERTY(QVariantMap radarView READ radarView NOTIFY changed)
    Q_PROPERTY(bool radarWaterEnabled READ radarWaterEnabled NOTIFY changed)
    Q_PROPERTY(int radarWaterSpeedPercent READ radarWaterSpeedPercent NOTIFY changed)
    Q_PROPERTY(int sortColumnIndex READ sortColumnIndex NOTIFY changed)
    Q_PROPERTY(QString sortDirection READ sortDirection NOTIFY changed)
    Q_PROPERTY(QString inspected READ inspected NOTIFY changed)
    Q_PROPERTY(QVariantList overviewFields READ overviewFields NOTIFY inspectorPresentationChanged)
    Q_PROPERTY(QVariantList inspectorEvents READ inspectorEvents NOTIFY inspectorRelatedChanged)
    Q_PROPERTY(QVariantList inspectorLinks READ inspectorLinks NOTIFY inspectorRelatedChanged)
    Q_PROPERTY(QAbstractItemModel* inspectorEventTable READ inspectorEventTable CONSTANT)
    Q_PROPERTY(QAbstractItemModel* inspectorLinkTable READ inspectorLinkTable CONSTANT)
    Q_PROPERTY(QVariantList inspectorEventColumns READ inspectorEventColumns NOTIFY tableLayoutChanged)
    Q_PROPERTY(QVariantList inspectorLinkColumns READ inspectorLinkColumns NOTIFY tableLayoutChanged)
    Q_PROPERTY(int inspectorEventSortColumn READ inspectorEventSortColumn NOTIFY changed)
    Q_PROPERTY(int inspectorLinkSortColumn READ inspectorLinkSortColumn NOTIFY changed)
    Q_PROPERTY(QString inspectorEventSortDirection READ inspectorEventSortDirection NOTIFY changed)
    Q_PROPERTY(QString inspectorLinkSortDirection READ inspectorLinkSortDirection NOTIFY changed)
    Q_PROPERTY(QString inspectorPage READ inspectorPage NOTIFY changed)
    Q_PROPERTY(QString inspectorPath READ inspectorPath NOTIFY changed)
    Q_PROPERTY(QString inspectorName READ inspectorName NOTIFY inspectorPresentationChanged)
    Q_PROPERTY(QString inspectorScope READ inspectorScope NOTIFY inspectorPresentationChanged)
    Q_PROPERTY(QString inspectorStatus READ inspectorStatus NOTIFY changed)
    Q_PROPERTY(bool canInspectBack READ canInspectBack NOTIFY changed)
    Q_PROPERTY(bool canPortForward READ canPortForward NOTIFY changed)
    Q_PROPERTY(bool canStartTerminal READ canStartTerminal NOTIFY changed)
    Q_PROPERTY(podlord::ContainerTerminal* containerTerminal READ containerTerminal NOTIFY changed)
    Q_PROPERTY(QStringList terminalContainers READ terminalContainers NOTIFY inspectorPresentationChanged)
    Q_PROPERTY(QVariantMap portForwardTarget READ portForwardTarget NOTIFY changed)
    Q_PROPERTY(QVariantList portForwards READ portForwards NOTIFY changed)
    Q_PROPERTY(QAbstractItemModel* portTable READ portTable CONSTANT)
    Q_PROPERTY(QVariantList portColumns READ portColumns NOTIFY tableLayoutChanged)
    Q_PROPERTY(QString portFilterText READ portFilterText NOTIFY changed)
    Q_PROPERTY(int portSortColumnIndex READ portSortColumnIndex NOTIFY changed)
    Q_PROPERTY(QString portSortDirection READ portSortDirection NOTIFY changed)
    Q_PROPERTY(QString portForwardError READ portForwardError NOTIFY changed)
    Q_PROPERTY(bool canInspectForward READ canInspectForward NOTIFY changed)
    Q_PROPERTY(bool canDeleteResource READ canDeleteResource NOTIFY deletionAvailabilityChanged)
    Q_PROPERTY(bool deletionPending READ deletionPending NOTIFY changed)
    Q_PROPERTY(bool deletionRunning READ deletionRunning NOTIFY changed)
    Q_PROPERTY(bool canReadBackDeletion READ canReadBackDeletion NOTIFY changed)
    Q_PROPERTY(QString deletionTarget READ deletionTarget NOTIFY changed)
    Q_PROPERTY(QString deletionStatus READ deletionStatus NOTIFY changed)
    Q_PROPERTY(QString yamlText READ yamlText NOTIFY yamlTextChanged)
    Q_PROPERTY(bool canEditYaml READ canEditYaml NOTIFY yamlEditChanged)
    Q_PROPERTY(bool yamlEditing READ yamlEditing NOTIFY yamlEditChanged)
    Q_PROPERTY(bool yamlDirty READ yamlDirty NOTIFY yamlEditChanged)
    Q_PROPERTY(QString yamlDraftStatus READ yamlDraftStatus NOTIFY yamlEditChanged)
    Q_PROPERTY(QString yamlCheckStatus READ yamlCheckStatus NOTIFY yamlCheckChanged)
    Q_PROPERTY(bool yamlChecking READ yamlChecking NOTIFY yamlCheckChanged)
    Q_PROPERTY(bool yamlApplyLocked READ yamlApplyLocked NOTIFY yamlApplyChanged)
    Q_PROPERTY(bool yamlApplyPreview READ yamlApplyPreview NOTIFY yamlApplyChanged)
    Q_PROPERTY(bool yamlReconcilePending READ yamlReconcilePending NOTIFY yamlApplyChanged)
    Q_PROPERTY(QString yamlApplyTarget READ yamlApplyTarget NOTIFY yamlApplyChanged)
    Q_PROPERTY(QString yamlApplyDiff READ yamlApplyDiff NOTIFY yamlApplyChanged)
    Q_PROPERTY(QString yamlComparison READ yamlComparison NOTIFY yamlApplyChanged)
    Q_PROPERTY(QString yamlApplyStatus READ yamlApplyStatus NOTIFY yamlApplyChanged)
    Q_PROPERTY(bool canReconcileYaml READ canReconcileYaml NOTIFY yamlApplyChanged)
    Q_PROPERTY(bool canReadBackYaml READ canReadBackYaml NOTIFY yamlApplyChanged)
    Q_PROPERTY(QVariantList yamlConflicts READ yamlConflicts NOTIFY yamlConflictsChanged)
    Q_PROPERTY(bool yamlReconcileReady READ yamlReconcileReady NOTIFY yamlApplyChanged)
    Q_PROPERTY(int yamlLimitMiB READ yamlLimitMiB NOTIFY changed)
    Q_PROPERTY(bool discardPending READ discardPending NOTIFY changed)
    Q_PROPERTY(QString discardPrompt READ discardPrompt NOTIFY changed)
    Q_PROPERTY(QVariantList resourceValues READ resourceValues NOTIFY inspectorPresentationChanged)
    Q_PROPERTY(QAbstractItemModel* valuesTable READ valuesTable CONSTANT)
    Q_PROPERTY(QVariantList valuesColumns READ valuesColumns NOTIFY tableLayoutChanged)
    Q_PROPERTY(int valuesSortColumn READ valuesSortColumn NOTIFY changed)
    Q_PROPERTY(QString valuesSortDirection READ valuesSortDirection NOTIFY changed)
    Q_PROPERTY(bool yamlVisible READ yamlVisible NOTIFY changed)
    Q_PROPERTY(bool valuesVisible READ valuesVisible NOTIFY changed)
    Q_PROPERTY(bool valuesAvailable READ valuesAvailable NOTIFY changed)
    Q_PROPERTY(QString monospaceFamily READ monospaceFamily CONSTANT)
    Q_PROPERTY(QAbstractItemModel* table READ table CONSTANT)
    Q_PROPERTY(QVariantList resourceColumns READ resourceColumns NOTIFY tableLayoutChanged)
    Q_PROPERTY(QVariantList eventColumns READ eventColumns NOTIFY tableLayoutChanged)
    Q_PROPERTY(QString tableLayoutError READ tableLayoutError NOTIFY tableLayoutStatusChanged)
    Q_PROPERTY(bool tableLayoutSaving READ tableLayoutSaving NOTIFY tableLayoutStatusChanged)
    Q_PROPERTY(int requestLimit READ requestLimit NOTIFY changed)
    Q_PROPERTY(int inactiveSyncMinutes READ inactiveSyncMinutes NOTIFY changed)
    Q_PROPERTY(bool podInspected READ podInspected NOTIFY changed)
    Q_PROPERTY(bool logsVisible READ logsVisible NOTIFY changed)
    Q_PROPERTY(bool logsPaused READ logsPaused NOTIFY changed)
    Q_PROPERTY(QVariantList logContainers READ logContainers NOTIFY changed)
    Q_PROPERTY(QString logSelection READ logSelection NOTIFY changed)
    Q_PROPERTY(QString logStatus READ logStatus NOTIFY changed)
    Q_PROPERTY(int logLimitMb READ logLimitMb NOTIFY changed)
    Q_PROPERTY(QAbstractItemModel* logRows READ logRows CONSTANT)
    Q_PROPERTY(QString logAnchor READ logAnchor NOTIFY logViewChanged)
    Q_PROPERTY(double logOffset READ logOffset NOTIFY logViewChanged)
    Q_PROPERTY(bool logFollow READ logFollow NOTIFY logViewChanged)
    Q_PROPERTY(QString logPositionNotice READ logPositionNotice NOTIFY logViewChanged)
public:
    bool canStartTerminal() const;
    ContainerTerminal* containerTerminal() const { return client_.terminal(active_); }
    QStringList terminalContainers() const;
    Q_INVOKABLE bool startContainerTerminal(const QString& container, const QString& shell);
    Q_INVOKABLE bool stopContainerTerminal();
    bool canPortForward() const;
    QVariantMap portForwardTarget() const { return portForwardTarget_; }
    QVariantList portForwards() const { return client_.portForwards(active_); }
    QAbstractItemModel* portTable() { return &ports_; }
    QVariantList portColumns() const;
    QString portFilterText() const { return navigation_.value(active_).portFilter; }
    int portSortColumnIndex() const { return navigation_.value(active_).portColumn; }
    QString portSortDirection() const { return navigation_.value(active_).portOrder == Qt::AscendingOrder ? "ASC" : "DESC"; }
    Q_INVOKABLE bool filterPorts(const QString& text);
    Q_INVOKABLE bool sortPortColumn(int column);
    Q_INVOKABLE bool copyPortValue(const QString& token, int column);
    QString portForwardError() const { return client_.portForwardError(active_); }
    Q_INVOKABLE bool preparePortForward();
    Q_INVOKABLE bool cancelPreparedPortForward();
    Q_INVOKABLE bool startPreparedPortForward(const QString& localPort, const QString& remotePort);
    Q_INVOKABLE bool stopPreparedPortForward();
    Q_INVOKABLE bool stopPortForward(const QString& token);
    Q_INVOKABLE bool copyPortForwardEndpoint(const QString& token);
    /** Explicitly open an active session's loopback endpoint. Never accepts an arbitrary URL. */
    Q_INVOKABLE bool openPortForwardEndpoint(const QString& token, bool secure);
    explicit Workspace(QString profile, QObject* parent = nullptr, std::function<QDateTime()> now = QDateTime::currentDateTimeUtc);
    ~Workspace() override;
    Alerts* alerts() { return &alerts_; }
    Q_INVOKABLE int alertResourceIndex(const QString& path) const;
    Q_INVOKABLE bool previewAlertZoom(const QVariantMap& draft);
    QString sourceImportNotice() const { return sourceImportNotice_; }
    QVariantList sourceImportIssues() const { return sourceImportIssues_; }
    QStringList themeNames() const { return podlord::themeNames(); }
    QString themeName() const { return settings_.themeName; }
    QString themeVariant() const { return settings_.themeVariant; }
    QString themeIntensity() const { return settings_.themeIntensity; }
    QPalette appearancePalette() const { return appearance_.palette; }
    QVariantMap appearanceColors() const { return appearance_.colors; }
    Q_INVOKABLE bool saveAppearance(const QString& name, const QString& variant, const QString& intensity);
    bool radarWaterEnabled() const { return settings_.radarWaterEnabled; }
    int radarWaterSpeedPercent() const { return settings_.radarWaterSpeedPercent; }
    Q_INVOKABLE bool saveRadarWater(bool enabled, int speedPercent);
    QVariantList contexts() const;
    QVariantList sessions() const;
    QVariantList tabs() const;
    bool busy() const;
    bool workspaceRestoreEnabled() const { return settings_.workspaceRestore; }
    Q_INVOKABLE bool saveWorkspaceRestore(bool enabled);
    bool loading() const;
    bool syncLoading() const { return authenticationRunning() || client_.syncLoading(active_); }
    double loadingProgress() const { return authenticationRunning() ? 0 : client_.loadingProgress(active_); }
    bool problemsOnly() const { return navigation_.value(active_).mode == "problems"; }
    bool activityOnly() const { return navigation_.value(active_).mode == "activity"; }
    Q_INVOKABLE bool setFilterMode(const QString& mode);
    QStringList filterPresets() const;
    QString selectedFilterPreset() const;
    bool filterPresetsBusy() const { return presetsBusy_; }
    QString filterPresetsError() const { return presetsError_; }
    Q_INVOKABLE bool reloadFilterPresets();
    Q_INVOKABLE bool importFilterPresets(const QUrl& source);
    Q_INVOKABLE bool loadFilterPreset(const QString& name);
    Q_INVOKABLE bool saveFilterPreset(const QString& name);
    Q_INVOKABLE bool renameFilterPreset(const QString& name, const QString& replacement);
    Q_INVOKABLE bool deleteFilterPreset(const QString& name);
    bool authenticationRequired() const;
    bool authenticationRunning() const;
    QString authenticationPrompt() const;
    QString error() const;
    QString status() const;
    QString currentSession() const;
    QString title() const;
    QString sessionRenameError() const { return sessionRenameError_; }
    QVariantList resourceGuidance() const;
    QString sourceImportError() const { return sourceImportError_; }
    QString defaultSourceOrigin() const;
    QString sessionManagementError() const { return sessionManagementError_; }
    QString sessionManagementNotice() const { return sessionManagementNotice_; }
    Q_INVOKABLE bool importText(QString originPath, const QString& yaml);
    Q_INVOKABLE bool importHome();
    Q_INVOKABLE bool refreshSources();
    Q_INVOKABLE bool renameSourceContext(const QString& contextId, const QString& displayName);
    QVariantMap sourceRemoval() const { return sourceRemoval_; }
    Q_INVOKABLE bool requestSourceRemoval(const QString& contextId);
    Q_INVOKABLE bool confirmSourceRemoval();
    Q_INVOKABLE bool cancelSourceRemoval();
    Q_INVOKABLE QVariantMap sessionConfiguration(const QString& id) const;
    Q_INVOKABLE bool saveSessionConfiguration(const QString& id, const QString& contextId, const QString& namespaces);
    Q_INVOKABLE bool duplicateSession(const QString& id, const QString& name);
    Q_INVOKABLE bool renameSession(const QString& id, const QString& name);
    QString filterText() const;
    QVariantList filterFields() const;
    QVariantMap resourceFieldFilters() const;
    QString filterPickerField() const { return filterPickerField_; }
    QStringList filterPickerValues() const { return filterPickerValues_; }
    Q_INVOKABLE bool filterField(const QString& field, const QString& expression);
    Q_INVOKABLE bool resetResourceFilters();
    Q_INVOKABLE bool prepareFilterPicker(const QString& field);
    Q_INVOKABLE bool filterValueSelected(const QString& value) const;
    Q_INVOKABLE bool selectFilterValue(const QString& value, bool selected);
    Q_INVOKABLE bool copyFilterValue(const QString& value);
    QString filterError() const { return workspacePage() == "events" ? events_.error() : table_.error(); }
    QString workspacePage() const { return navigation_.value(active_).workspace; }
    QString eventFilterText() const { return navigation_.value(active_).eventFilter; }
    int eventSortColumnIndex() const { return navigation_.value(active_).eventColumn; }
    QString eventSortDirection() const { return navigation_.value(active_).eventOrder == Qt::AscendingOrder ? "ASC" : "DESC"; }
    QAbstractItemModel* eventTable() { return &events_; }
    int eventCount() const { return events_.rowCount(); }
    int totalEventCount() const { return eventRows_.rowCount(); }
    Q_INVOKABLE bool filterEvents(const QString& text);
    Q_INVOKABLE bool sortEventColumn(int column);
    Q_INVOKABLE bool inspectEventPath(const QString& path);
    Q_INVOKABLE bool copyEventCell(int row, int column);
    Q_INVOKABLE bool copyPathCell(const QString& path, int column, bool event);
    int resourceCount() const { return table_.rowCount(); }
    int totalResourceCount() const { return rows_.rowCount(); }
    QAbstractItemModel* dashboardTable() { return &dashboardRows_; }
    QString dashboardSummary() const { return dashboardSummary_; }
    QVariantList pulseMetrics() const { return pulseMetrics_; }
    QVariantMap healthSummary() const { return healthSummary_; }
    QVariantMap radarView() const { return navigation_.value(active_).radarView; }
    Q_INVOKABLE bool setWorkspacePage(const QString& page);
    Q_INVOKABLE bool rememberRadarView(const QVariantMap& view);
    int sortColumnIndex() const;
    QString sortDirection() const;
    QString inspected() const;
    QVariantList overviewFields() const { return overviewFields_; }
    QVariantList inspectorEvents() const { return inspectorEvents_; }
    QVariantList inspectorLinks() const { return inspectorLinks_; }
    QAbstractItemModel* inspectorEventTable() { return &inspectorEventsTable_; }
    QAbstractItemModel* inspectorLinkTable() { return &inspectorLinksTable_; }
    QVariantList inspectorEventColumns() const { return tableColumns("inspectorEvent", inspectorEventRows_); }
    QVariantList inspectorLinkColumns() const { return tableColumns("inspectorLink", inspectorLinkRows_); }
    int inspectorEventSortColumn() const { return navigation_.value(active_).inspectorEventColumn; }
    int inspectorLinkSortColumn() const { return navigation_.value(active_).inspectorLinkColumn; }
    QString inspectorEventSortDirection() const { return navigation_.value(active_).inspectorEventOrder == Qt::AscendingOrder ? "ASC" : "DESC"; }
    QString inspectorLinkSortDirection() const { return navigation_.value(active_).inspectorLinkOrder == Qt::AscendingOrder ? "ASC" : "DESC"; }
    Q_INVOKABLE bool sortInspectorColumn(const QString& table, int column);
    Q_INVOKABLE bool copyInspectorCell(const QString& table, const QString& identity, int column);
    QString inspectorPage() const { return navigation_.value(active_).page; }
    /** Open a listed cache identity, never an arbitrary URL supplied by presentation. */
    Q_INVOKABLE bool inspectPath(const QString& path);
    QString inspectorPath() const;
    QString inspectorName() const;
    QString inspectorScope() const;
    QString inspectorStatus() const;
    bool canInspectBack() const;
    bool canInspectForward() const;
    /** Move one available cached history entry in this session. Invalid steps do nothing. */
    Q_INVOKABLE bool navigateInspector(int step);
    bool canDeleteResource() const;
    bool deletionPending() const;
    bool deletionRunning() const;
    bool canReadBackDeletion() const;
    QString deletionTarget() const;
    QString deletionStatus() const;
    /** Freeze the listed resource identity and show confirmation. Does not send a request. */
    Q_INVOKABLE bool previewDeletion();
    /** Authorize only the frozen identity; a changed UI target cancels instead of retargeting. */
    Q_INVOKABLE bool confirmDeletion(bool confirmed);
    /** Observe an unresolved deletion target. Never retries a mutation or initiates authentication. */
    Q_INVOKABLE bool readBackDeletion();
    QString yamlText() const;
    bool canEditYaml() const;
    bool yamlEditing() const;
    bool yamlDirty() const;
    QString yamlDraftStatus() const;
    bool discardPending() const;
    QString discardPrompt() const;
    /** Begin a local draft only after a successful current-scope fresh GET. */
    Q_INVOKABLE bool beginYamlEdit();
    /** Schedule a local syntax check; accepted work never dispatches a request or changes the draft. */
    Q_INVOKABLE bool checkYamlDraft();
    QString yamlCheckStatus() const { return yamlCheckStatus_; }
    bool yamlChecking() const { return yamlChecking_; }
    bool yamlApplyLocked() const;
    bool yamlApplyPreview() const;
    bool yamlReconcilePending() const;
    QString yamlApplyTarget() const;
    QString yamlApplyDiff() const;
    QString yamlComparison() const;
    QString yamlApplyStatus() const;
    bool canReconcileYaml() const;
    bool canReadBackYaml() const;
    QVariantList yamlConflicts() const;
    bool yamlReconcileReady() const;
    int yamlLimitMiB() const { return settings_.yamlLimitMiB; }
    Q_INVOKABLE bool previewYamlApply();
    Q_INVOKABLE bool confirmYamlApply(bool confirmed);
    Q_INVOKABLE bool readBackYaml();
    Q_INVOKABLE bool reconcileYaml();
    Q_INVOKABLE bool chooseYamlConflict(int index, bool mine);
    Q_INVOKABLE QString yamlConflictChoice(int index) const;
    Q_INVOKABLE bool finishYamlReconcile(bool confirmed);
    Q_INVOKABLE bool saveYamlLimit(const QString& value);
    /** Free-form local text; validation and server writes are not implicit side effects. */
    Q_INVOKABLE bool setYamlDraft(const QString& text);
    Q_INVOKABLE bool leaveYamlEdit();
    Q_INVOKABLE bool confirmDiscard(bool discard);
    Q_INVOKABLE bool requestWindowClose();
    Q_INVOKABLE bool reloadSavedViews();
    Q_INVOKABLE bool confirmViewClose(bool discard);
    bool viewStateFailed() const;
    bool viewCloseNeedsDecision() const;
    QVariantList resourceValues() const;
    QAbstractItemModel* valuesTable() { return &valuesTable_; }
    QVariantList valuesColumns() const { return tableColumns("value", valueRows_); }
    int valuesSortColumn() const { return navigation_.value(active_).valueColumn; }
    QString valuesSortDirection() const { return navigation_.value(active_).valueOrder == Qt::AscendingOrder ? "ASC" : "DESC"; }
    bool yamlVisible() const;
    bool valuesVisible() const;
    bool valuesAvailable() const;
    QString monospaceFamily() const;
    /** Explicit, stable diagnostics snapshot; contains no credentials or response bodies. */
    Q_INVOKABLE QVariantMap settingsDiagnostics() const;
    /** Read the immutable bundled license only on an explicit About action. */
    Q_INVOKABLE QString applicationLicense() const;
    /** Read immutable dependency notices only on an explicit About action. */
    Q_INVOKABLE QString dependencyNotices() const;
    Q_INVOKABLE bool setInspectorPage(const QString& page);
    Q_INVOKABLE bool closeInspector();
    Q_INVOKABLE bool refreshInspector();
    Q_INVOKABLE bool copyYaml();
    Q_INVOKABLE bool revealValue(const QString& id, bool revealed);
    Q_INVOKABLE bool copyValue(const QString& id, const QString& representation = "preferred");
    QAbstractItemModel* table();
    QVariantList resourceColumns() const;
    QVariantList eventColumns() const;
    Q_INVOKABLE QVariantList defaultTableColumns(const QString& table) const;
    QString tableLayoutError() const { return tableLayoutError_; }
    bool tableLayoutSaving() const { return tableLayoutSaving_; }
    Q_INVOKABLE bool saveTableLayout(const QString& table, const QVariantList& columns);
    Q_INVOKABLE bool reloadTableLayouts();
    int requestLimit() const;
    int inactiveSyncMinutes() const;
    bool podInspected() const;
    bool logsVisible() const;
    bool logsPaused() const;
    QVariantList logContainers() const;
    QString logSelection() const;
    QString logStatus() const;
    int logLimitMb() const;
    QAbstractItemModel* logRows();
    QString logAnchor() const;
    double logOffset() const;
    bool logFollow() const;
    QString logPositionNotice() const;
    Q_INVOKABLE bool setLogsVisible(bool visible);
    Q_INVOKABLE bool setWindowVisible(bool visible);
    Q_INVOKABLE bool selectLogContainer(const QString& container);
    Q_INVOKABLE bool pauseLogs(bool paused);
    Q_INVOKABLE bool rememberLogPosition(const QString& anchor, double offset, bool follow);
    Q_INVOKABLE bool followLogs();
    Q_INVOKABLE bool reportLogEviction();
    Q_INVOKABLE QString logEntry(int index) const;
    Q_INVOKABLE bool copyLogEntry(int index);
    Q_INVOKABLE bool saveReadSettings(int requestLimit, int inactiveMinutes, const QString& logLimit);
    Q_INVOKABLE bool reload();
    Q_INVOKABLE bool importFile(const QString& path);
    Q_INVOKABLE bool importK3d();
    Q_INVOKABLE bool openContext(const QString& context);
    Q_INVOKABLE bool activate(const QString& id);
    Q_INVOKABLE bool close(const QString& id);
    Q_INVOKABLE bool refresh(bool confirmedAuthentication = false);
    Q_INVOKABLE bool cancelAuthentication();
    Q_INVOKABLE bool filter(const QString& text);
    Q_INVOKABLE bool sortColumn(int column);
    Q_INVOKABLE bool inspectRow(int row);
    Q_INVOKABLE bool copyCell(int row, int column);
signals:
    void deletionAvailabilityChanged();
    void sourceImportFinished(bool success);
    void sourceRemovalChanged();
    void sessionRenamed(const QString& id);
    /** Request scheduler admission; safe metadata only, using the owning client's clock. */
    void requestStarted(const QString& id, const QString& path, qint64 monotonicMs);
    void changed();
    void catalogsChanged();
    void resourcePresentationChanged();
    void fieldFiltersChanged();
    void filterPresetsChanged();
    void filterPickerChanged();
    void dashboardChanged();
    void inspectorRelatedChanged();
    void appearanceChanged();
    void tableLayoutChanged();
    void tableLayoutStatusChanged();
    void logViewChanging();
    void logViewChanged();
    void inspectorPresentationChanging();
    void inspectorPresentationChanged();
    void yamlTextChanged();
    void yamlEditChanged();
    void yamlCheckChanged();
    void yamlApplyChanged();
    void yamlConflictsChanged();
    void windowCloseApproved();
private:
    QVariantMap portForwardTarget_;
    TableViewStates presets_;
    QString presetsError_;
    bool presetsBusy_ = false, presetsReady_ = false;
    QFuture<Result<TableViewStates>> presetsFuture_;
    bool persistFilterPresets(const TableViewStates& desired);
    bool updateFilterPresets(const std::function<Result<TableViewStates>(const ViewStateStore&)>& operation, bool reload = false);
    struct Deletion final { QString token, session, path, target, phase, status; QJsonObject baseline; QUrl server; };
    std::optional<Deletion> deletion_;
    bool bindDeletion();
    bool cancelPendingDeletion();
    struct YamlDraft final { QString session, path; QJsonObject document; QString baseline, text; QJsonObject hiddenDraft = {}; };
    struct YamlApply final {
        QString token, session, path, target, text, phase, status, comparison;
        PreparedYaml prepared;
        QJsonObject baseline, current;
        QStringList conflicts;
        QMap<QString, bool> choices;
    };
    std::optional<YamlApply> yamlApply_;
    bool yamlChecking_ = false;
    QString yamlCheckToken_;
    bool bindYamlApply();
    enum class Leave { Resource, Session, Context, Tab, Inspector, Window, Draft, Reload, History };
    struct PendingLeave final { Leave action; QString target, origin; int historyIndex = -1; };
    struct Navigation final {
        QString filter, mode;
        QMap<QString, QString> fields;
        int column = -1;
        Qt::SortOrder order = Qt::AscendingOrder;
        QString inspected, page = "overview", workspace = "resources";
        QVariantMap radarView{{"x",0},{"y",0},{"zoom",1}};
        QString eventFilter, portFilter;
        int eventColumn = -1, portColumn = -1, inspectorEventColumn = -1, inspectorLinkColumn = -1, valueColumn = -1;
        Qt::SortOrder eventOrder = Qt::AscendingOrder, portOrder = Qt::AscendingOrder,
            inspectorEventOrder = Qt::AscendingOrder, inspectorLinkOrder = Qt::AscendingOrder, valueOrder = Qt::AscendingOrder;
        QStringList history;
        int historyIndex = -1;
    };
    struct LogPosition final { QString anchor; double offset = 0; bool follow = true, evicted = false; PodLogHistory::Removal removal = PodLogHistory::Removal::None; };
    const QString profile_;
    QString filterPickerField_, filterPickerSession_;
    QStringList filterPickerValues_;
    bool applyResourceFilters();
    QMap<QString, TableViewStates> savedViews_, pendingViews_;
    QFuture<Result<TableViewStates>> viewSaveFuture_;
    QString viewSaveSession_;
    QMap<QString, QString> viewStateErrors_;
    bool viewsLoaded_ = false, viewSaving_ = false, viewClosePending_ = false, viewCloseDiscardApproved_ = false;
    bool restoreViewStates(const LoadedViewStates& states);
    bool queueViewSave(const QString& table);
    bool dispatchViewSave();
    QString viewStateError() const;
    TableLayouts tableLayouts_;
    QString tableLayoutError_;
    bool tableLayoutSaving_ = false;
    TableSchemas tableSchemas(bool includeAuxiliary = false) const;
    QVariantList tableColumns(const QString& table, const ResourceTable& model, bool defaults = false) const;
    SourceCatalog sources_;
    QVariantMap sourceRemoval_;
    Appearance appearance_;
    bool publishAppearance();
    QString sourceImportNotice_;
    QString sourceImportError_, sessionManagementError_, sessionManagementNotice_;
    QFuture<Result<SourceImportReport>> sourceImportFuture_;
    QFuture<Result<SessionCatalog>> sessionMutationFuture_;
    bool importSource(const std::function<Result<SourceImportReport>(const KubeconfigStore&)>& operation, const QString& notice = {});
    bool contextUsable(const QString& id) const;
    QVariantList sourceImportIssues_;
    SessionCatalog catalog_;
    SessionCatalog selection_;
    quint64 selectionRevision_ = 0;
    void refreshSessionSelection();
    ResourceClient client_;
    Alerts alerts_;
    CredentialProcess credentials_;
    ResourceTable rows_;
    ResourceFilter table_;
    ResourceTable eventRows_;
    ResourceFilter events_;
    ResourceTable portRows_;
    QSortFilterProxyModel ports_;
    ResourceTable inspectorEventRows_, inspectorLinkRows_;
    QSortFilterProxyModel inspectorEventsTable_, inspectorLinksTable_;
    ResourceTable valueRows_;
    QSortFilterProxyModel valuesTable_;
    bool publishPorts();
    LogRows logs_;
    QMap<QString, LogPosition> logPositions_;
    QString logScope_;
    QString inspectorScope_, overview_, yaml_;
    QJsonObject inspectorDocument_, inspectorSummary_;
    QVariantList overviewFields_, inspectorEvents_, inspectorLinks_;
    QSortFilterProxyModel dashboardRows_;
    QString dashboardSummary_;
    QVariantList pulseMetrics_;
    QVariantMap healthSummary_;
    bool dashboardDirty_ = true;
    bool publishDashboard();
    QSet<QString> revealedValues_;
    std::optional<YamlDraft> yamlDraft_;
    std::optional<PendingLeave> pendingLeave_;
    bool yamlFresh_ = false;
    QMap<QString, Navigation> navigation_;
    QString active_, error_;
    QString settingsError_, sessionRenameError_;
    ReadSettings settings_;
    bool settingsReady_ = false;
    bool busy_ = false;
    bool windowVisible_ = true;
    bool select(const SessionCatalog& catalog);
    bool resolve(const QString& id, bool retry = false);
    bool mutate(const std::function<Result<SessionCatalog>(const SessionStore&)>& operation, const QString& target = {}, const QString& renamedSession = {}, const QString& managementNotice = {});
    bool publish();
    QString activeCluster() const;
    bool publishLogs();
    bool publishInspector(bool revealChanged = false);
    bool publishRelated();
    bool inspectResource(const QString& path);
    bool openInspector(const QString& path, int historyIndex = -1);
    int inspectorHistoryTarget(int step) const;
    bool inspectHistory(const QString& path, int index);
    bool allowLeave(Leave action, const QString& target = {}, int historyIndex = -1);
    bool clearYamlDraft();
    bool clearYamlCheck();
    QString yamlCheckStatus_;
    bool savePolicy(ReadSettings desired);
    bool eventFilter(QObject* watched, QEvent* event) override;
};
} // namespace podlord
