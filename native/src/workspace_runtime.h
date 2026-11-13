#pragma once
#include "resource_client.h"
#include "credential_process.h"
#include "release_updates.h"
#include "map_previews.h"
#include "system_typography.h"
#include "music.h"
#include "view_state.h"
#include <QFuture>

namespace podlord {
class Workspace;
class WindowHost;
/** Application-owned transport and placement. Windows own only their presentation. */
class WorkspaceRuntime final {
public:
    explicit WorkspaceRuntime(QString profile, std::function<QDateTime()> now, QUrl releaseEndpoint);
    ~WorkspaceRuntime();
    const QString profile;
    const std::function<QDateTime()> now;
    ResourceClient client;
    CredentialProcess credentials;
    ReleaseUpdates releases;
    MapPreviews previews{profile};
    SystemTypography typography;
    Music music;
    QList<Workspace*> windows;
    QMap<QString, Workspace*> owners;
    QMap<QString, TableViewStates> contextViews, savedViews, pendingViews;
    QMap<QString, QString> viewStateErrors;
    QFuture<Result<TableViewStates>> viewSaveFuture;
    QString viewSaveContext;
    TableSchemas viewSchemas;
    bool viewsReady = false, viewSaving = false;
    ContextUsageMap contextUsage, pendingUsage, savingUsage;
    quint64 contextCatalogRevision = 0;
    QFuture<Result<ContextUsageMap>> usageSaveFuture;
    bool usageSaving = false;
    void recordContextUse(const QString& context);
    bool dispatchUsageSave();
    bool finishUsageSave();
    void flushUsageSaves();
    bool dispatchViewSave();
    void flushViewSaves();
    WindowHost* host = nullptr;
    bool publishCatalog(const SessionCatalog& catalog, Workspace* source);
};
}
