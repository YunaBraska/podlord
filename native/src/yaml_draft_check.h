#pragma once
#include <QJsonObject>
#include <QString>

namespace podlord {
struct YamlDraftCheck final { bool valid; QString message; };
/** Locally checks one YAML mapping against the immutable original resource identity.
 * Checks syntax, unique scalar mapping keys and non-cyclic aliases, not Kubernetes
 * schema, authorization, freshness or write safety. Never sends or serializes a
 * write payload, mutates either input, or includes input values in diagnostics.
 */
YamlDraftCheck checkYamlDraft(const QString& text, const QJsonObject& original);
} // namespace podlord
