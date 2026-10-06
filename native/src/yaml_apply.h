#pragma once
#include "session_store.h"
#include <QJsonArray>
#include <QMap>

namespace podlord {
struct PreparedYaml final { QJsonObject document; QJsonArray patch; QString preview; };
struct YamlMerge final { QJsonObject document; QStringList conflicts; };
/** Parse once, bound JSON alias expansion and patch bytes, retain untouched masked Secrets. */
Result<PreparedYaml> prepareYaml(const QString& text, const QJsonObject& original, qint64 byteLimit,
                                const QJsonObject& hiddenDraft = {});
/** Render the resource with Secret values and embedded last-applied Secret data masked. */
QString resourceYaml(QJsonObject document);
/** Three-way field merge; arrays are atomic. Each overlap needs an explicit mine/server choice. */
YamlMerge mergeYaml(const QJsonObject& original, const QJsonObject& desired, const QJsonObject& current,
                   const QMap<QString, bool>& choices = {});
/** Observe only the requested field changes; server-managed/defaulted fields are not equality gates. */
bool yamlChangesObserved(const QJsonObject& original, const QJsonObject& desired, const QJsonObject& current);
} // namespace podlord
