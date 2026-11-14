#include "kubeconfig_store.h"
#include "map_previews.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QUrl>
#include <QUrlQuery>
#include <cstdio>
#include <stdexcept>
#ifdef Q_OS_UNIX
#include <unistd.h>
#endif

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
void write(const QString& path, const QByteArray& bytes) {
    QFile file(path);
    require(file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size(), "Cannot write test input.");
}
QByteArray read(const QString& path) {
    QFile file(path);
    require(file.open(QIODevice::ReadOnly), "Cannot read test artifact.");
    return file.readAll();
}
QString previewPath(const QString& profile, const QString& context) {
    const auto name = QString::fromLatin1(QCryptographicHash::hash(context.toUtf8(), QCryptographicHash::Sha256).toHex()) + ".png";
    return QDir(profile).filePath("map-previews/" + name);
}
QSet<QString> identifiers(const podlord::SourceCatalog& catalog) {
    require(catalog.errors.isEmpty(), "Unexpected error in the real source catalog.");
    QSet<QString> result;
    for (const auto& source : catalog.sources)
        for (const auto& context : source.contexts) result.insert(context.id);
    return result;
}
QImage image(QRgb color) {
    QImage result(320, 200, QImage::Format_ARGB32);
    result.fill(color);
    return result;
}
bool matches(const QString& path, const QImage& expected) {
    const QImage actual(path);
    return !actual.isNull() && actual.size() == expected.size()
        && actual.convertToFormat(QImage::Format_ARGB32) == expected.convertToFormat(QImage::Format_ARGB32);
}
void reconcile(podlord::MapPreviews& previews, const QSet<QString>& contexts, bool authoritative) {
    const auto revision = previews.revision();
    QSignalSpy changes(&previews, &podlord::MapPreviews::changed);
    previews.reconcile(contexts, authoritative);
    require(QTest::qWaitFor([&] { return !changes.isEmpty() && previews.revision() > revision; }, 5000), "Preview reconciliation did not publish a snapshot.");
}
void requireUrl(const podlord::MapPreviews& previews, const QString& context, const QString& path) {
    const QUrl url(previews.url(context));
    require(url.isLocalFile() && url.toLocalFile() == path && !QUrlQuery(url).queryItemValue("v").isEmpty(), "Preview URL has the wrong file or no cache-busting version.");
}
void save(podlord::MapPreviews& previews, const QString& profile, const QString& context, const QImage& expected) {
    const auto revision = previews.revision();
    QSignalSpy changes(&previews, &podlord::MapPreviews::changed);
    require(previews.save(context, expected), "Valid preview was not accepted.");
    require(QTest::qWaitFor([&] { return !changes.isEmpty() && previews.revision() > revision; }, 5000), "Preview save did not publish a result.");
    require(previews.error().isEmpty(), "Valid preview save reported an error.");
    const auto path = previewPath(profile, context);
    requireUrl(previews, context, path);
    require(matches(path, expected), "Saved PNG does not contain the accepted image.");
}
void run(const QString& scenario) {
    QTemporaryDir temporary;
    require(temporary.isValid(), "Cannot create an isolated preview profile.");
    const auto profile = temporary.filePath("profile");
    const auto folder = QDir(profile).filePath("map-previews");
    const podlord::KubeconfigStore store(profile);
    if (scenario == "invalid_profile") {
        write(profile, "not a directory");
        podlord::MapPreviews previews(profile);
        reconcile(previews, {"context"}, true);
        require(!previews.error().isEmpty() && previews.url("context").isEmpty() && read(profile) == "not a directory", "Invalid profile was accepted or modified.");
        return;
    }
    if (scenario == "missing_profile") {
        const auto listed = store.list();
        require(std::holds_alternative<podlord::SourceCatalog>(listed), "Missing source profile did not produce an empty catalog.");
        const auto contexts = identifiers(std::get<podlord::SourceCatalog>(listed));
        require(contexts.isEmpty() && !QFileInfo::exists(profile), "Listing created a missing profile.");
        {
            podlord::MapPreviews previews(profile);
            require(previews.url("unknown").isEmpty() && previews.error().isEmpty() && previews.revision() == 0, "New preview accessors are not empty.");
            reconcile(previews, contexts, true);
            require(previews.error().isEmpty() && previews.url("unknown").isEmpty(), "Empty startup reconciliation failed.");
        }
        require(!QFileInfo::exists(profile), "Preview startup created the missing profile.");
        return;
    }
    const auto input = temporary.filePath("source.config");
    write(input, "apiVersion: v1\nkind: Config\nclusters:\n- name: local\n  cluster: {server: 'https://127.0.0.1:9'}\nusers:\n- name: local\n  user: {}\ncontexts:\n- name: alpha\n  context: {cluster: local, user: local}\n- name: beta\n  context: {cluster: local, user: local}\n- name: gamma\n  context: {cluster: local, user: local}\n");
    const auto imported = store.importFile(input);
    require(std::holds_alternative<podlord::SourceSnapshot>(imported), "Real kubeconfig import failed.");
    const auto source = std::get<podlord::SourceSnapshot>(imported);
    require(source.contexts.size() == 3, "Imported kubeconfig did not contain three contexts.");
    const auto listed = store.list();
    require(std::holds_alternative<podlord::SourceCatalog>(listed), "Cannot read the imported source catalog.");
    const auto contexts = identifiers(std::get<podlord::SourceCatalog>(listed));
    require(contexts.size() == 3 && !contexts.contains(QString{}), "Imported context identities are missing or duplicated.");
    const auto first = source.contexts[0].id, second = source.contexts[1].id, third = source.contexts[2].id;
    const auto firstPath = previewPath(profile, first), secondPath = previewPath(profile, second), thirdPath = previewPath(profile, third);
    const auto red = image(qRgb(180, 30, 40)), green = image(qRgb(25, 170, 70)), blue = image(qRgb(30, 65, 190));
    if (scenario == "folder_file" || scenario == "preview_directory") {
        if (scenario == "folder_file") write(folder, "not a preview directory");
        else require(QDir().mkpath(firstPath), "Cannot create a directory at the preview path.");
        podlord::MapPreviews previews(profile);
        reconcile(previews, contexts, true);
        require(!previews.error().isEmpty() && previews.url(first).isEmpty(), "Invalid preview storage was exposed.");
        const auto revision = previews.revision();
        require(previews.save(first, red), "Valid image was not accepted for asynchronous storage validation.");
        require(QTest::qWaitFor([&] { return previews.revision() > revision && !previews.error().isEmpty(); }, 5000), "Invalid preview storage did not report a save failure.");
        require(previews.url(first).isEmpty() && (scenario == "folder_file" ? read(folder) == "not a preview directory" : QFileInfo(firstPath).isDir()), "Failed save replaced unrelated storage.");
        return;
    }
    if (scenario == "malformed_file" || scenario == "oversized_file" || scenario == "oversized_dimensions_file" || scenario == "oversized_height_file") {
        require(QDir().mkpath(folder) && QFile::setPermissions(folder, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner), "Cannot create private preview storage.");
        if (scenario == "oversized_dimensions_file" || scenario == "oversized_height_file") {
            QImage oversized(scenario == "oversized_height_file" ? 320 : 321, scenario == "oversized_height_file" ? 201 : 200, QImage::Format_ARGB32); oversized.fill(Qt::red);
            require(oversized.save(firstPath, "PNG"), "Cannot write oversized PNG input.");
        } else write(firstPath, scenario == "oversized_file" ? QByteArray(512 * 1024 + 1, 'x') : QByteArray("not a PNG"));
        const auto original = read(firstPath);
        podlord::MapPreviews previews(profile);
        reconcile(previews, contexts, true);
        require(previews.url(first).isEmpty() && !previews.error().isEmpty() && read(firstPath) == original, "Invalid stored preview was exposed or modified.");
        return;
    }
#ifdef Q_OS_UNIX
    if (scenario == "symlink_folder" || scenario == "symlink_file") {
        const auto external = temporary.filePath("external");
        require(QDir().mkpath(external), "Cannot create external symlink target.");
        const auto target = QDir(external).filePath("retained.png");
        require(red.save(target, "PNG"), "Cannot create external target image.");
        const auto original = read(target);
        const auto link = scenario == "symlink_folder" ? folder : firstPath;
        if (scenario == "symlink_file") require(QDir().mkpath(folder), "Cannot create preview link directory.");
        require(QFile::link(scenario == "symlink_folder" ? external : target, link) && QFileInfo(link).isSymLink(), "Cannot create a real symbolic link.");
        podlord::MapPreviews previews(profile);
        reconcile(previews, contexts, true);
        require(previews.url(first).isEmpty() && !previews.error().isEmpty(), "Symbolic-link preview storage was accepted.");
        const auto revision = previews.revision();
        require(previews.save(first, blue), "Valid image was not accepted for asynchronous storage validation.");
        require(QTest::qWaitFor([&] { return previews.revision() > revision && !previews.error().isEmpty(); }, 5000), "Symbolic-link save did not report failure.");
        require(QFileInfo(link).isSymLink() && read(target) == original && previews.url(first).isEmpty(), "Preview storage followed or replaced a symbolic link.");
        require(QDir(external).entryList(QDir::Files | QDir::NoDotAndDotDot) == QStringList{"retained.png"}, "Preview storage wrote into the external directory.");
        return;
    }
#endif
    if (scenario == "forget_pending_save") {
        {
            podlord::MapPreviews previews(profile);
            reconcile(previews, contexts, true);
            save(previews, profile, second, green);
            save(previews, profile, third, blue);
            require(previews.save(first, red) && previews.save(first, blue), "Pending images for the removal regression were not accepted.");
            require(std::holds_alternative<podlord::SessionCatalog>(store.removeContext(first, {})), "Confirmed context removal failed while its preview was pending.");
            const auto current = store.list();
            require(std::holds_alternative<podlord::SourceCatalog>(current)
                && identifiers(std::get<podlord::SourceCatalog>(current)) == QSet<QString>{second, third}, "Confirmed removal changed another context identity.");
            previews.forget(first);
            previews.reconcile({second}, false);
        }
        require(!QFileInfo::exists(firstPath), "A pending accepted save resurrected a forgotten preview after destruction.");
        require(matches(secondPath, green) && matches(thirdPath, blue), "Non-authoritative partial reconciliation removed a surviving context preview.");
        require(QDir(folder).entryList({"*.png"}, QDir::Files).size() == 2, "Explicit removal retained an orphaned PNG.");
        const auto current = store.list();
        require(std::holds_alternative<podlord::SourceCatalog>(current), "Cannot reload the catalog after explicit preview removal.");
        podlord::MapPreviews restarted(profile);
        reconcile(restarted, identifiers(std::get<podlord::SourceCatalog>(current)), true);
        require(restarted.url(first).isEmpty(), "Restart exposed a forgotten context preview.");
        requireUrl(restarted, second, secondPath); requireUrl(restarted, third, thirdPath);
        return;
    }
    if (scenario == "destructor_flush") {
        auto latest = red;
        {
            podlord::MapPreviews previews(profile);
            reconcile(previews, contexts, true);
            for (int sample = 0; sample < 32; ++sample) {
                latest.fill(qRgb(sample, 80, 200 - sample));
                require(previews.save(first, latest), "Rapid preview save was not accepted.");
            }
            require(previews.save(second, green) && previews.save(third, blue), "Pending context saves were not accepted.");
        }
        require(matches(firstPath, latest) && matches(secondPath, green) && matches(thirdPath, blue), "Destructor lost an accepted latest image or another context's pending image.");
        podlord::MapPreviews restarted(profile);
        reconcile(restarted, contexts, true);
        requireUrl(restarted, first, firstPath); requireUrl(restarted, second, secondPath); requireUrl(restarted, third, thirdPath);
        return;
    }
    podlord::MapPreviews previews(profile);
    reconcile(previews, contexts, true);
    require(previews.error().isEmpty() && previews.url(first).isEmpty() && !QFileInfo::exists(folder), "Empty reconciliation created preview storage or reported a failure.");
    if (scenario == "pending_reconcile") {
        require(previews.save(first, red) && previews.save(second, green) && previews.save(third, blue), "Concurrent context previews were not accepted.");
        previews.reconcile({second}, false);
        require(QTest::qWaitFor([&] { return !previews.url(second).isEmpty() && matches(secondPath, green); }, 5000), "Retained pending preview did not finish.");
        require(previews.error().isEmpty() && previews.url(first).isEmpty() && previews.url(third).isEmpty(), "Reconciliation published a removed context.");
        require(matches(firstPath, red) && !QFileInfo::exists(thirdPath), "Reconciliation lost a sent save or wrote a withdrawn pending save.");
        return;
    }
    if (scenario == "unknown_context" || scenario == "invalid_empty" || scenario == "invalid_width" || scenario == "invalid_height") {
        auto invalid = red;
        if (scenario == "invalid_empty") invalid = {};
        if (scenario == "invalid_width") invalid = QImage(321, 200, QImage::Format_ARGB32);
        if (scenario == "invalid_height") invalid = QImage(320, 201, QImage::Format_ARGB32);
        const auto revision = previews.revision();
        QSignalSpy changes(&previews, &podlord::MapPreviews::changed);
        require(!previews.save(scenario == "unknown_context" ? "unknown" : first, invalid), "Unknown context or invalid image was accepted.");
        require(previews.url("unknown").isEmpty() && previews.revision() == revision && changes.isEmpty() && !QFileInfo::exists(folder), "Rejected input changed or created preview storage.");
        return;
    }
    save(previews, profile, first, red);
    const auto originalUrl = previews.url(first);
    const auto original = read(firstPath);
    if (scenario == "repair_oversized_file") {
        write(firstPath, QByteArray(512 * 1024 + 1, 'x'));
        save(previews, profile, first, blue);
        require(matches(firstPath, blue) && previews.url(first) != originalUrl, "Fresh accepted preview did not replace oversized stale storage.");
        return;
    }
#ifdef Q_OS_UNIX
    if (scenario == "prune_denied" || scenario == "forget_denied") {
        require(std::holds_alternative<podlord::SessionCatalog>(store.removeContext(first, {})), "Cannot remove the context before preview cleanup.");
        const auto permissions = QFileInfo(folder).permissions();
        const auto restore = qScopeGuard([&] { QFile::setPermissions(folder, permissions); });
        require(QFile::setPermissions(folder, QFile::ReadOwner | QFile::ExeOwner), "Cannot deny preview directory writes.");
        const auto revision = previews.revision();
        if (scenario == "forget_denied") previews.forget(first);
        else previews.reconcile({second, third}, true);
        require(QTest::qWaitFor([&] { return previews.revision() > revision && !previews.error().isEmpty(); }, 5000), "Denied cleanup did not report failure.");
        require(previews.url(first).isEmpty() && read(firstPath) == original, "Failed cleanup exposed the removed context or modified its PNG.");
        return;
    }
#endif
    if (scenario == "save_restart") {
#ifdef Q_OS_UNIX
        const auto publicPermissions = QFile::ReadGroup | QFile::WriteGroup | QFile::ExeGroup | QFile::ReadOther | QFile::WriteOther | QFile::ExeOther;
        require(!(QFileInfo(folder).permissions() & publicPermissions) && !(QFileInfo(firstPath).permissions() & publicPermissions), "Preview directory or PNG is not private.");
#endif
        podlord::MapPreviews restarted(profile);
        reconcile(restarted, contexts, true);
        require(restarted.error().isEmpty() && matches(firstPath, red), "Restart lost the saved preview.");
        requireUrl(restarted, first, firstPath);
        require(restarted.url(second).isEmpty() && restarted.url(third).isEmpty(), "Restart exposed an unsaved context preview.");
        return;
    }
    if (scenario == "latest_per_context") {
        save(previews, profile, first, blue);
        require(previews.url(first) != originalUrl, "Changed image retained the old cache-busting URL.");
        require(QDir(folder).entryList(QDir::Files | QDir::NoDotAndDotDot) == QStringList{QFileInfo(firstPath).fileName()}, "Replacing a preview created history files or left temporary files.");
        require(previews.url(second).isEmpty(), "Replacing one preview changed another context.");
        return;
    }
    if (scenario == "cached_accessors") {
        const auto revision = previews.revision(); const auto error = previews.error();
        QSignalSpy changes(&previews, &podlord::MapPreviews::changed);
        require(QFile::remove(firstPath), "Cannot remove the PNG for cached-accessor coverage.");
        require(previews.url(first) == originalUrl && previews.error() == error && previews.revision() == revision && changes.isEmpty(), "Accessor rescanned storage or changed the cached snapshot.");
        reconcile(previews, contexts, true);
        require(previews.url(first).isEmpty(), "Explicit reconciliation did not observe the removed PNG.");
        return;
    }
#ifdef Q_OS_UNIX
    if (scenario == "atomic_failure") {
        const auto permissions = QFileInfo(folder).permissions();
        const auto restore = qScopeGuard([&] { QFile::setPermissions(folder, permissions); });
        require(QFile::setPermissions(folder, QFile::ReadOwner | QFile::ExeOwner), "Cannot make preview replacement fail through permissions.");
        const auto revision = previews.revision();
        require(previews.save(first, blue), "Valid replacement was not accepted for asynchronous persistence.");
        require(QTest::qWaitFor([&] { return previews.revision() > revision && !previews.error().isEmpty(); }, 5000), "Denied preview replacement did not report failure.");
        require(read(firstPath) == original && matches(firstPath, red), "Failed replacement modified the previous PNG.");
        requireUrl(previews, first, firstPath);
        return;
    }
#endif
    save(previews, profile, second, green);
    save(previews, profile, third, blue);
    const auto secondBytes = read(secondPath), thirdBytes = read(thirdPath);
    if (scenario == "unrelated_png") {
        const auto unrelated = QDir(folder).filePath("personal.png");
        write(unrelated, "unrelated content");
        reconcile(previews, contexts, true);
        require(previews.error().isEmpty() && read(unrelated) == "unrelated content" && read(firstPath) == original
            && read(secondPath) == secondBytes && read(thirdPath) == thirdBytes, "Reconciliation modified unrelated storage.");
        return;
    }
#ifdef Q_OS_UNIX
    if (scenario == "orphan_symlink") {
        const auto external = temporary.filePath("external.png"), orphan = previewPath(profile, "removed-context");
        require(red.save(external, "PNG") && QFile::link(external, orphan), "Cannot create an orphaned preview symlink.");
        const auto bytes = read(external);
        reconcile(previews, contexts, true);
        require(previews.error().isEmpty() && QFileInfo(orphan).isSymLink() && read(external) == bytes, "Reconciliation followed or removed an unrelated symlink.");
        require(read(firstPath) == original && read(secondPath) == secondBytes && read(thirdPath) == thirdBytes, "Symlink reconciliation changed a valid preview.");
        return;
    }
#endif
    if (scenario == "context_isolation") {
        require(matches(firstPath, red) && matches(secondPath, green) && matches(thirdPath, blue), "Context images overwrote one another.");
        requireUrl(previews, first, firstPath); requireUrl(previews, second, secondPath); requireUrl(previews, third, thirdPath);
        require(QDir(folder).entryList({"*.png"}, QDir::Files).size() == 3, "Context previews do not have one PNG per identity.");
        return;
    }
    if (scenario == "rapid_latest") {
        auto latest = red;
        const auto revision = previews.revision();
        bool latestPublished = false;
        QObject observer;
        QObject::connect(&previews, &podlord::MapPreviews::changed, &observer, [&] {
            latestPublished = previews.error().isEmpty() && previews.url(first) != originalUrl
                && matches(firstPath, latest) && matches(secondPath, red);
        });
        for (int sample = 0; sample < 32; ++sample) {
            latest.fill(qRgb(sample, 80, 200 - sample));
            require(previews.save(first, latest), "Rapid preview replacement was not accepted.");
        }
        require(previews.save(second, red), "Concurrent context replacement was not accepted.");
        require(QTest::qWaitFor([&] { return latestPublished && previews.revision() > revision; }, 5000), "Rapid saves did not publish the latest images.");
        require(read(thirdPath) == thirdBytes && QDir(folder).entryList(QDir::Files | QDir::NoDotAndDotDot).size() == 3, "Rapid saves modified another context or leaked files.");
        return;
    }
    if (scenario == "alias") {
        const auto renamed = store.renameContext(first, "Local display alias");
        require(std::holds_alternative<podlord::SourceSnapshot>(renamed), "Real context alias failed.");
        bool found = false;
        for (const auto& context : std::get<podlord::SourceSnapshot>(renamed).contexts)
            if (context.id == first) found = context.displayName == "Local display alias";
        require(found, "Local alias changed the context identity or did not update its display name.");
        const auto current = store.list();
        require(std::holds_alternative<podlord::SourceCatalog>(current) && identifiers(std::get<podlord::SourceCatalog>(current)) == contexts, "Alias changed the available context identities.");
        reconcile(previews, contexts, true);
        requireUrl(previews, first, firstPath);
        require(read(firstPath) == original && read(secondPath) == secondBytes && read(thirdPath) == thirdBytes, "Alias rewrote or removed a preview.");
        return;
    }
    if (scenario == "remove") {
        require(std::holds_alternative<podlord::SessionCatalog>(store.removeContext(first, {})), "Real context removal failed.");
        const auto current = store.list();
        require(std::holds_alternative<podlord::SourceCatalog>(current), "Cannot read the catalog after real removal.");
        const auto remaining = identifiers(std::get<podlord::SourceCatalog>(current));
        require(remaining == QSet<QString>{second, third}, "Context removal affected another identity.");
        reconcile(previews, remaining, true);
        require(previews.url(first).isEmpty() && !QFileInfo::exists(firstPath), "Removed context retained its preview.");
        require(read(secondPath) == secondBytes && read(thirdPath) == thirdBytes, "Pruning removed or changed a surviving preview.");
        requireUrl(previews, second, secondPath); requireUrl(previews, third, thirdPath);
        require(QDir(folder).entryList({"*.png"}, QDir::Files).size() == 2, "Pruning left an orphaned context PNG.");
        return;
    }
    if (scenario == "partial_reconcile" || scenario == "authoritative_catalog" || scenario == "failed_catalog") {
        if (scenario == "failed_catalog") {
            const auto owned = read(source.ownedPath);
            const auto restore = qScopeGuard([&] { write(source.ownedPath, owned); });
            write(source.ownedPath, "not a source snapshot");
            const auto failed = store.list();
            const auto* catalog = std::get_if<podlord::SourceCatalog>(&failed);
            require(!catalog || !catalog->errors.isEmpty(), "Corrupted real source snapshot did not fail catalog reading.");
            reconcile(previews, {}, true);
        } else reconcile(previews, {first}, scenario == "authoritative_catalog");
        require(read(firstPath) == original && read(secondPath) == secondBytes && read(thirdPath) == thirdBytes, "Incomplete or failed catalog reconciliation pruned accepted previews.");
        const auto current = store.list();
        require(std::holds_alternative<podlord::SourceCatalog>(current), "Cannot reread the real source catalog.");
        reconcile(previews, identifiers(std::get<podlord::SourceCatalog>(current)), true);
        requireUrl(previews, first, firstPath); requireUrl(previews, second, secondPath); requireUrl(previews, third, thirdPath);
        return;
    }
    throw std::runtime_error("Unknown map preview scenario.");
}
}
int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    if (argc != 2) return 2;
    const auto scenario = QString::fromLocal8Bit(argv[1]);
#ifndef Q_OS_UNIX
    if (scenario == "symlink_folder" || scenario == "symlink_file" || scenario == "orphan_symlink" || scenario == "atomic_failure" || scenario == "prune_denied" || scenario == "forget_denied") return 77;
#else
    if ((scenario == "atomic_failure" || scenario == "prune_denied" || scenario == "forget_denied") && geteuid() == 0) return 77;
#endif
    try {
        run(scenario);
        std::printf("Map previews scenario passed: %s\n", argv[1]);
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "Map previews scenario failed: %s: %s\n", argv[1], error.what());
        return 1;
    }
}
