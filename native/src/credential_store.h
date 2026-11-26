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
    enum class Service { LegacyAi, Profile };
    explicit SystemCredentialStore(Service service = Service::LegacyAi);
    ReadResult read(const QString& profile, const QString& name) override;
    bool write(const QString& profile, const QString& name, const QByteArray& data, QString& error) override;
    bool remove(const QString& profile, const QString& name, QString& error) override;
private:
    const Service service_;
};

/** Keeps named secrets in one OS-protected profile entry where the platform permits it. */
class ProfileCredentialStore final : public CredentialStore {
public:
    explicit ProfileCredentialStore(CredentialStore* vaultBackend = nullptr, CredentialStore* legacyBackend = nullptr);
    ReadResult read(const QString& profile, const QString& name) override;
    bool write(const QString& profile, const QString& name, const QByteArray& data, QString& error) override;
    bool remove(const QString& profile, const QString& name, QString& error) override;
private:
    SystemCredentialStore system_{SystemCredentialStore::Service::Profile};
    SystemCredentialStore legacy_;
    CredentialStore* vaultBackend_;
    CredentialStore* legacyBackend_;
};
} // namespace podlord
