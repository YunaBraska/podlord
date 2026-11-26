#pragma once
#include "credential_store.h"
#include <QMap>

class MemoryCredentialStore final : public podlord::CredentialStore {
public:
    QMap<QString, QByteArray> entries;
    bool available = true;
    ReadResult read(const QString& profile, const QString& name) override {
        if (!available) return {false, false, {}, "System credential store is unavailable."};
        const auto key = profile + "/" + name;
        return {true, entries.contains(key), entries.value(key), {}};
    }
    bool write(const QString& profile, const QString& name, const QByteArray& data, QString& error) override {
        if (!available) { error = "System credential store is unavailable."; return false; }
        entries.insert(profile + "/" + name, data);
        return true;
    }
    bool remove(const QString& profile, const QString& name, QString& error) override {
        if (!available) { error = "System credential store is unavailable."; return false; }
        entries.remove(profile + "/" + name);
        return true;
    }
};
