#pragma once
#include "resource_client.h"
#include "credential_process.h"
#include "release_updates.h"

namespace podlord {
class Workspace;
class WindowHost;
/** Application-owned transport and placement. Windows own only their presentation. */
class WorkspaceRuntime final {
public:
    explicit WorkspaceRuntime(QString profile, std::function<QDateTime()> now, QUrl releaseEndpoint);
    const QString profile;
    const std::function<QDateTime()> now;
    ResourceClient client;
    CredentialProcess credentials;
    ReleaseUpdates releases;
    QList<Workspace*> windows;
    QMap<QString, Workspace*> owners;
    WindowHost* host = nullptr;
    bool publishCatalog(const SessionCatalog& catalog, Workspace* source);
};
}
