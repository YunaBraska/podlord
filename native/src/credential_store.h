#pragma once
#include <QByteArray>
#include <QString>
#include <functional>

namespace podlord {
/** Stores one profile-scoped credential in the operating system's secret store. */
class CredentialStore {
public:
    struct ReadResult { bool ok = false; bool found = false; QByteArray data; QString error; };
    virtual ~CredentialStore() = default;
    virtual ReadResult read(const QString& profile, const QString& name) = 0;
    virtual bool write(const QString& profile, const QString& name, const QByteArray& data, QString& error) = 0;
    virtual bool remove(const QString& profile, const QString& name, QString& error) = 0;
    ReadResult readMigrating(const QString& profile, const QString& name, const QString& oldFile, qsizetype maxBytes,
        std::function<QByteArray(const QByteArray&)> prepare = {});
};

class SystemCredentialStore final : public CredentialStore {
public:
    ReadResult read(const QString& profile, const QString& name) override;
    bool write(const QString& profile, const QString& name, const QByteArray& data, QString& error) override;
    bool remove(const QString& profile, const QString& name, QString& error) override;
};
} // namespace podlord
