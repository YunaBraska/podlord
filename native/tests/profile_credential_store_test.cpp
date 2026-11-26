#include "credential_store.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QMap>
#include <QTemporaryDir>
#include <QMutex>
#include <cstdio>
#include <stdexcept>
#include <thread>

using namespace podlord;

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

class RawStore final : public CredentialStore {
public:
    QMap<QString, QByteArray> entries;
    bool failWrites = false;
    bool corruptWrites = false;
    bool failRemoves = false;
    ReadResult read(const QString& profile, const QString& name) override {
        QMutexLocker lock(&mutex_);
        const auto key = profile + '/' + name;
        return {true, entries.contains(key), entries.value(key), {}};
    }
    bool write(const QString& profile, const QString& name, const QByteArray& data, QString& error) override {
        QMutexLocker lock(&mutex_);
        if (failWrites) { error = "Unavailable"; return false; }
        entries.insert(profile + '/' + name, corruptWrites ? QByteArray("corrupt") : data);
        return true;
    }
    bool remove(const QString& profile, const QString& name, QString& error) override {
        QMutexLocker lock(&mutex_);
        if (failRemoves) { error = "Unavailable"; return false; }
        entries.remove(profile + '/' + name);
        return true;
    }
private:
    QMutex mutex_;
};

void run() {
    QTemporaryDir directory;
    require(directory.isValid(), "Cannot create profile directory");
    const auto profile = directory.path();
    RawStore vault;
    RawStore legacy;
    ProfileCredentialStore first(&vault, &legacy);
    QString error;
#if defined(Q_OS_MACOS) || defined(Q_OS_LINUX)
    const auto newProfile = directory.filePath("not-created");
    const auto absent = first.read(newProfile, "chatgpt");
    require(absent.ok && !absent.found && !QDir(newProfile).exists(), "Missing profile prevented first sign-in");
    require(first.write(profile, "chatgpt", "chat-token", error), "First entry failed");
    require(first.write(profile, "claude", "api-key", error), "Second entry failed");
    require(vault.entries.size() == 1 && legacy.entries.isEmpty(), "Multiple system password entries were created");
    ProfileCredentialStore reopened(&vault, &legacy);
    require(reopened.read(profile, "chatgpt").data == "chat-token", "First entry was not restored");
    require(reopened.read(profile, "claude").data == "api-key", "Second entry was not restored");
    require(reopened.remove(profile, "claude", error), "Removing one entry failed");
    require(!reopened.read(profile, "claude").found && reopened.read(profile, "chatgpt").found,
        "Removing one entry affected another");

    vault.entries.clear();
    legacy.entries.insert(profile + "/chatgpt", "old-chat-token");
    legacy.entries.insert(profile + "/claude", "old-api-key");
    vault.failWrites = true;
    require(!first.read(profile, "chatgpt").ok && legacy.entries.contains(profile + "/chatgpt"),
        "Failed migration removed an old credential");
    vault.failWrites = false;
    vault.corruptWrites = true;
    require(!first.read(profile, "chatgpt").ok && legacy.entries.contains(profile + "/chatgpt"),
        "Unverified migration removed an old credential");
    vault.corruptWrites = false;
    vault.entries.clear();
    require(first.read(profile, "chatgpt").data == "old-chat-token", "ChatGPT migration failed");
    require(first.read(profile, "claude").data == "old-api-key", "Claude migration failed");
    require(vault.entries.size() == 1 && legacy.entries.isEmpty(), "Old system entries remained after verified migration");

    vault.entries.clear();
    legacy.entries.insert(profile + "/chatgpt", "keep-old-token");
    vault.entries.insert(profile + "/podlord-profile-vault-v1", "{\"version\":99,\"entries\":{}}");
    require(!first.read(profile, "chatgpt").ok && legacy.entries.contains(profile + "/chatgpt"),
        "Unknown vault version discarded an old credential");

    vault.entries.clear();
    legacy.entries.clear();
    ProfileCredentialStore second(&vault, &legacy);
    std::thread a([&] { QString failure; require(first.write(profile, "future-setting", "one", failure), "Concurrent write A failed"); });
    std::thread b([&] { QString failure; require(second.write(profile, "another-setting", "two", failure), "Concurrent write B failed"); });
    a.join(); b.join();
    require(first.read(profile, "future-setting").data == "one" && first.read(profile, "another-setting").data == "two",
        "Concurrent writes lost an entry");
    legacy.entries.insert(profile + "/future-setting", "stale-old-value");
    require(first.remove(profile, "future-setting", error), "Removing an old and new entry failed");
    require(!first.read(profile, "future-setting").found && !legacy.entries.contains(profile + "/future-setting"),
        "Removed entry returned from an old system item");
    legacy.failRemoves = true;
    require(!first.remove(profile, "another-setting", error) && first.read(profile, "another-setting").found,
        "Failed cleanup removed a still-needed credential");
    legacy.failRemoves = false;
    const auto oldFile = directory.filePath("old-secret.json");
    QFile file(oldFile);
    require(file.open(QIODevice::WriteOnly) && file.setPermissions(QFile::ReadOwner | QFile::WriteOwner)
        && file.write("file-secret") == 11, "Cannot create private legacy file");
    file.close();
    const auto migrated = first.readMigrating(profile, "future-file-secret", "old-secret.json", 1024);
    require(migrated.ok && migrated.data == "file-secret" && !QFile::exists(oldFile)
        && first.read(profile, "another-setting").data == "two", "Private file migration lost another vault entry");
#else
    require(first.write(profile, "chatgpt", "chat-token", error), "Platform credential write failed");
    require(legacy.entries.size() == 1 && vault.entries.isEmpty() && first.read(profile, "chatgpt").data == "chat-token",
        "Platform credential store did not retain the entry");
#endif
}
}

int main(int argc, char** argv) {
    QCoreApplication application(argc, argv);
    try { run(); return 0; }
    catch (const std::exception& error) { std::fprintf(stderr, "%s\n", error.what()); return 1; }
}
