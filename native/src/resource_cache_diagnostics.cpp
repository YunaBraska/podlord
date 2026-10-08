#include "resource_client.h"
#include <QJsonDocument>

namespace podlord {
QVariantMap ResourceClient::cacheDiagnostics() const {
    qint64 collections = 0, resources = 0, details = 0, histories = 0, lines = 0, jsonBytes = 0, logBytes = 0;
    for (const auto& state : states_) {
        collections += state.collections.size();
        resources += state.resources.size();
        for (const auto& collection : state.collections)
            jsonBytes += QJsonDocument(collection.rows).toJson(QJsonDocument::Compact).size();
        for (const auto& detail : state.details) {
            if (detail.document.isEmpty()) continue;
            ++details;
            jsonBytes += QJsonDocument(detail.document).toJson(QJsonDocument::Compact).size();
        }
        histories += state.logs.size();
        for (const auto& history : state.logs) {
            lines += history.entries.size();
            logBytes += history.retainedBytes;
        }
    }
    return {{"collections", collections}, {"resources", resources}, {"details", details},
        {"logHistories", histories}, {"logLines", lines}, {"payloadBytes", jsonBytes + logBytes}};
}
}
