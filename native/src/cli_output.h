#pragma once
#include "session_store.h"
#include <QJsonDocument>
#include <cstdio>

namespace podlord::cli {
/** Publishes JSON to stdout on success or stderr otherwise; delivery failure returns 1. */
inline int output(const QJsonObject& json, int status = 0) {
    const auto bytes = QJsonDocument(json).toJson();
    FILE* const stream = status == 0 ? stdout : stderr;
    return std::fwrite(bytes.constData(), 1, static_cast<size_t>(bytes.size()), stream)
        == static_cast<size_t>(bytes.size()) && std::fflush(stream) == 0 ? status : 1;
}
/** Translates a domain failure into the shared CLI error contract and exit status. */
inline int failure(const Failure& error) {
    return output({{"error", QJsonObject{{"code", errorName(error.code)}, {"message", error.message}}}},
        error.code == StoreError::InvalidInput ? 2 : 1);
}
} // namespace podlord::cli
