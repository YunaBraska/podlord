#include "workspace.h"
#include "ui_input.h"
#include <QGuiApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLockFile>
#include <QProcess>
#include <QProcessEnvironment>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QTest>
#include <cstdio>
#include <stdexcept>

namespace {
bool require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
    return true;
}
QByteArray config(const QByteArray& endpoint = "https://127.0.0.1:9") {
    return "apiVersion: v1\nkind: Config\nclusters:\n- name: local\n  cluster:\n    server: " + endpoint
        + "\nusers:\n- name: local\n  user:\n    token: source-input-secret\ncontexts:\n- name: local\n  context:\n    cluster: local\n    user: local\n";
}
bool write(const QString& path, const QByteArray& bytes) {
    require(QDir().mkpath(QFileInfo(path).absolutePath()), "Cannot create test directory");
    QFile file(path);
    return require(file.open(QIODevice::WriteOnly | QIODevice::Truncate) && file.write(bytes) == bytes.size(), "Cannot write external kubeconfig");
}
struct Response final { int code; QJsonObject json; };
Response run(const QString& executable, const QString& profile, const QStringList& args, const QString& home) {
    QProcess process;
    auto environment = QProcessEnvironment::systemEnvironment();
    environment.insert("HOME", home);
    environment.insert("USERPROFILE", home);
    process.setProcessEnvironment(environment);
    process.start(executable, QStringList{"--profile", profile} + args);
    require(process.waitForFinished(15000) && process.exitStatus() == QProcess::NormalExit, "Source CLI did not finish normally");
    const auto bytes = process.exitCode() == 0 ? process.readAllStandardOutput() : process.readAllStandardError();
    require(!bytes.contains("source-input-secret"), "Source report exposed credentials");
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(bytes, &error);
    require(error.error == QJsonParseError::NoError && document.isObject(), "Source CLI did not return a JSON result");
    return {process.exitCode(), document.object()};
}
bool click(QQuickWindow* window, QQuickItem* target) {
    if (!target || !target->isVisible() || !target->isEnabled()) return false;
    QList<QQuickItem*> ancestors;
    for (auto* item = target; item; item = item->parentItem()) ancestors.prepend(item);
    for (auto* item : ancestors) item->ensurePolished();
    QCoreApplication::processEvents();
    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
        target->mapToScene(QPointF(target->width() / 2, target->height() / 2)).toPoint());
    return true;
}
bool ui(const QString& profile, const QString& directory, bool partial, int width = 0) {
    podlord::Workspace workspace(profile);
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("workspace", &workspace);
    engine.load(QUrl("qrc:/podlord/Main.qml"));
    require(!engine.rootObjects().isEmpty(), "Cannot create real source UI");
    auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
    require(window, "Source UI has no window");
    require(QTest::qWaitFor([&] { return !workspace.busy(); }, 5000), "Initial catalogs did not settle");
    auto* input = window->findChild<QQuickItem*>("sourcePath");
    if (width > 0) {
        window->setWidth(width);
        window->setHeight(720);
        require(QTest::qWaitFor([&] { return window->contentItem()->width() == width && input && input->width() >= 160; }, 2000),
            "Kubeconfig path field is too narrow to read or edit");
        const auto controls = input->parentItem()->childItems();
        QList<QRectF> occupied;
        int actions = 0;
        for (auto* control : controls) {
            const auto text = control->property("text").toString();
            const bool action = QStringList{"Browse", "Folder", "Import"}.contains(text);
            if (control != input && !action) continue;
            require(control->isVisible() && control->isEnabled(), "Source import control is unavailable");
            require(!action || (control->width() >= 44 && control->height() >= 26), "Source action has an unusable target");
            const QRectF bounds(control->mapToScene(QPointF()), QSizeF(control->width(), control->height()));
            require(bounds.left() >= 0 && bounds.right() <= width && bounds.top() >= 0 && bounds.bottom() <= 720,
                "Source import control extends beyond the viewport");
            for (const auto& previous : occupied) require(!bounds.intersects(previous), "Source import controls overlap");
            occupied.append(bounds);
            if (action) ++actions;
        }
        require(actions == 3, "Source import actions are missing");
        auto* selector = window->findChild<QQuickItem*>("contexts");
        require(selector && selector->width() >= 160, "Context selector is too narrow to read");
        for (const auto* name : {"contexts", "openContext", "reloadSources"}) {
            auto* control = window->findChild<QQuickItem*>(name);
            require(control && control->isVisible(), "Context action is unavailable");
            const QRectF bounds(control->mapToScene(QPointF()), QSizeF(control->width(), control->height()));
            require(bounds.left() >= 0 && bounds.right() <= width && bounds.bottom() <= 720,
                "Context controls extend beyond the viewport");
        }
        input->forceActiveFocus();
        QTest::keyClick(window, Qt::Key_X);
        require(input->property("text").toString() == "x", "Source path cannot be edited with the keyboard");
    }
    require(input && input->setProperty("text", directory), "Source path control is unavailable");
    require(click(window, window->findChild<QQuickItem*>("importButton")), "Cannot activate public Import button");
    require(QTest::qWaitFor([&] { return !workspace.busy(); }, 5000), "Source import did not settle");
    require(workspace.contexts().size() == 1, "Folder import did not populate the context selector");
    if (width > 0) {
        const QString evidence = qEnvironmentVariable("PODLORD_TEST_EVIDENCE_DIR");
        if (!evidence.isEmpty()) require(window->grabWindow().save(evidence + "/source-layout-" + QString::number(width)
            + "-" + qEnvironmentVariable("QT_QUICK_CONTROLS_STYLE") + ".png"), "Cannot save actual source import frame");
    }
    auto* notice = window->findChild<QQuickItem*>("sourceImportNotice");
    require(notice && notice->property("visible").toBool(), "Import outcome is not visible");
    require(notice->property("text").toString().contains("1"), "Import outcome does not describe imported sources");
    if (partial) {
        require(click(window, window->findChild<QQuickItem*>("sourceImportDetailsButton")), "Cannot open partial import details");
        auto* dialog = window->findChild<QObject*>("sourceImportDetails");
        require(dialog && dialog->property("visible").toBool(), "Partial import dialog did not open");
        require(!dialog->property("title").toString().isEmpty(), "Import errors have no accessible title");
    }
    return true;
}
bool management(const QString& scenario, const QString& root, const QString& profile) {
    const bool session = scenario.startsWith("session_manage_");
    const bool alias = scenario.startsWith("source_alias_");
    const QString mode = scenario.mid(alias ? 13 : session ? 15 : 14);
    const QString origin = root + "/external/config";
    const QString home = root + "/isolated-home";
    qputenv("HOME", home.toUtf8());
    qputenv("USERPROFILE", home.toUtf8());
    const QByteArray yaml = config() + "- name: second\n  context:\n    cluster: local\n    user: local\n";
    const podlord::KubeconfigStore sources(profile);
    const podlord::SessionStore sessions(profile);
    const bool seeded = session || alias || mode.startsWith("refresh") || mode == "paste_busy";
    if (seeded) {
        write(origin, yaml);
        require(std::holds_alternative<podlord::SourceSnapshot>(sources.importFile(origin)), "Cannot prepare owned source");
    }
    QString sessionId, contextId, otherContext;
    if (session || alias) {
        const auto catalog = sources.list();
        require(std::holds_alternative<podlord::SourceCatalog>(catalog), "Cannot read prepared source");
        const auto contexts = std::get<podlord::SourceCatalog>(catalog).sources.first().contexts;
        contextId = contexts.first().id;
        otherContext = contexts.last().id;
        const auto created = sessions.create({contextId, {}});
        require(std::holds_alternative<podlord::SessionCatalog>(created), "Cannot prepare closed session");
        sessionId = std::get<podlord::SessionCatalog>(created).sessions.first().id.toString(QUuid::WithoutBraces);
    }
    const auto sourceCount = [&] {
        const auto result = sources.list();
        require(std::holds_alternative<podlord::SourceCatalog>(result), "Owned sources became unreadable");
        return std::get<podlord::SourceCatalog>(result).sources.size();
    };
    const auto catalog = [&] {
        const auto result = sessions.list();
        require(std::holds_alternative<podlord::SessionCatalog>(result), "Saved sessions became unreadable");
        return std::get<podlord::SessionCatalog>(result);
    };
    if (mode == "paste_limit" || mode == "file_limit") {
        const QByteArray huge(16 * 1024 * 1024 + 1, 'a');
        if (mode == "file_limit") write(origin, huge);
        const auto result = mode == "file_limit" ? sources.importFile(origin) : sources.importText(origin, QString::fromLatin1(huge));
        require(std::holds_alternative<podlord::Failure>(result) && sourceCount() == 0, "Oversize import must fail without persisting credentials");
        return true;
    }
    podlord::Workspace workspace(profile);
    bool bindingLoop = false;
    QQmlApplicationEngine engine;
    QObject::connect(&engine, &QQmlEngine::warnings, &engine, [&](const QList<QQmlError>& errors) {
        for (const auto& error : errors) bindingLoop = bindingLoop || error.description().contains("Binding loop");
    });
    engine.rootContext()->setContextProperty("workspace", &workspace);
    engine.load(QUrl("qrc:/podlord/Main.qml"));
    require(!engine.rootObjects().isEmpty(), "Cannot create real management UI");
    auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
    require(window && QTest::qWaitFor([&] { return !workspace.busy(); }, 5000), "Management catalogs did not settle");
    const auto item = [&](const char* name) -> QQuickItem* {
        for (auto* target : window->findChildren<QQuickItem*>(QString::fromLatin1(name)))
            if (target->isVisible()) return target;
        return podlord::test::visibleItem(window->contentItem(), QString::fromLatin1(name));
    };
    const auto press = [&](const char* name) { return require(click(window, item(name)), name); };
    const auto fill = [&](const char* name, const QByteArray& value) {
        auto* target = item(name);
        require(target && target->isEnabled(), "Editable management control is unavailable");
        target->forceActiveFocus();
        QTest::keySequence(window, QKeySequence::SelectAll);
        if (value.isEmpty()) QTest::keyClick(window, Qt::Key_Backspace);
        for (const auto character : value) {
            if (character == '\n') QTest::keyClick(window, Qt::Key_Return);
            else QTest::keyClick(window, character);
        }
    };
    const auto settle = [&] { require(QTest::qWaitFor([&] { return !workspace.busy(); }, 5000), "Management operation did not settle"); };
    press("settingsWorkspaceButton");
    press(session ? "settingsWorkspaceSection" : "settingsSourcesSection");
    if (alias) {
        const auto original = catalog();
        const auto connection = sources.connection(contextId);
        require(std::holds_alternative<podlord::ClusterConnection>(connection), "Cannot resolve original context");
        require(QTest::qWaitFor([&] { return item(("renameSource_" + contextId).toUtf8().constData()) != nullptr; }, 2000), "Source context rows did not appear");
        press(("renameSource_" + contextId).toUtf8().constData());
        require(QTest::qWaitFor([&] { return item("sourceAliasName") != nullptr; }, 2000), "Source alias editor did not open");
        if (mode == "narrow") {
            window->setWidth(390); window->setHeight(720);
            require(QTest::qWaitFor([&] { return window->contentItem()->width() == 390; }, 2000), "Alias editor did not resize");
            auto* editor = item("sourceAliasName");
            require(editor->mapToScene(QPointF()).x() >= 0 && editor->mapToScene(QPointF(editor->width(), 0)).x() <= 390, "Alias editor exceeds the viewport");
        }
        fill("sourceAliasName", mode == "long" ? QByteArray(513, 'a') : QByteArray("  Operations  "));
        if (mode == "cancel" || mode == "escape" || mode == "narrow") {
            if (mode == "escape") QTest::keyClick(window, Qt::Key_Escape);
            else press("cancelSourceAlias");
        } else {
            QLockFile lock(profile + "/kubeconfigs/sources.lock");
            if (mode == "busy") { lock.setStaleLockTime(0); require(lock.tryLock(0), "Cannot lock source metadata"); }
            if (mode == "keyboard") {
                item("saveSourceAlias")->forceActiveFocus(); QTest::keyClick(window, Qt::Key_Space);
            } else press("saveSourceAlias");
            settle();
            if (mode == "busy" || mode == "long") {
                require(item("sourceAliasName") && item("sourceAliasName")->property("text").toString().trimmed() == (mode == "long" ? QString(513, 'a') : QString("Operations")), "Rejected alias lost its draft");
                require(item("sourceAliasError") && !item("sourceAliasError")->property("text").toString().isEmpty(), "Rejected alias has no visible error");
            } else {
                require(QTest::qWaitFor([&] { return item("sourceAliasName") == nullptr; }, 2000), "Saved alias editor remained open");
                require(workspace.contexts().first().toMap()["name"] == "Operations", "Saved source alias is not displayed");
                if (mode == "reset" || mode == "repeat") {
                    press(("renameSource_" + contextId).toUtf8().constData());
                    require(QTest::qWaitFor([&] { return item("sourceAliasName") != nullptr; }, 2000), "Cannot reopen alias editor");
                    if (mode == "reset") fill("sourceAliasName", "");
                    press("saveSourceAlias"); settle();
                    require(workspace.contexts().first().toMap()["name"] == (mode == "reset" ? "local" : "Operations"), "Reset or repeated alias changed its semantics");
                }
                if (mode == "refresh") { press("refreshSourceFilesButton"); settle(); require(workspace.contexts().first().toMap()["name"] == "Operations", "Unchanged import discarded source alias"); }
                if (mode == "restart") {
                    podlord::Workspace reopened(profile);
                    require(QTest::qWaitFor([&] { return !reopened.busy(); }, 5000) && reopened.contexts().first().toMap()["name"] == "Operations", "Restart lost source alias");
                }
                if (mode == "changed") {
                    write(origin, yaml + "# changed\n"); press("refreshSourceFilesButton"); settle();
                    require(sourceCount() == 2 && workspace.contexts().first().toMap()["name"] == "local", "A new immutable snapshot inherited another snapshot's alias");
                }
            }
        }
        const auto after = sources.connection(contextId);
        require(std::holds_alternative<podlord::ClusterConnection>(after), "Renaming broke connection resolution");
        const auto beforeConnection = std::get<podlord::ClusterConnection>(connection), afterConnection = std::get<podlord::ClusterConnection>(after);
        require(beforeConnection.server == afterConnection.server && beforeConnection.authorization == afterConnection.authorization
            && beforeConnection.credentialId == afterConnection.credentialId && catalog() == original && workspace.currentSession().isEmpty(), "Source alias changed credentials, sessions or activation");
        require(!workspace.sourceImportError().contains("source-input-secret"), "Alias error exposed credentials");
        require(!bindingLoop, "Alias UI emitted a binding loop");
        return true;
    }
    if (!session) {
        if (mode == "home") {
            write(home + "/.kube/config", yaml);
            press("importHomeButton"); settle();
            require(sourceCount() == 1 && workspace.contexts().size() == 2, "Explicit home import did not populate owned contexts");
        } else if (mode.startsWith("refresh")) {
            if (mode == "refresh_changed") write(origin, yaml + "# updated source\n");
            if (mode == "refresh_missing") require(QFile::remove(origin), "Cannot remove external source for failure case");
            press("refreshSourceFilesButton"); settle();
            require(sourceCount() == (mode == "refresh_changed" ? 2 : 1), "Source refresh lost or duplicated an immutable snapshot");
            require(workspace.sourceImportIssues().size() == (mode == "refresh_missing" ? 1 : 0), "Refresh did not report unavailable source files");
            if (mode == "refresh_repeat") { press("refreshSourceFilesButton"); settle(); require(sourceCount() == 1, "Repeated refresh duplicated snapshots"); }
        } else {
            press("pasteSourceButton");
            require(QTest::qWaitFor([&] { return item("pasteSourceYaml") != nullptr; }, 2000), "Paste dialog did not open");
            if (mode == "paste_empty") {
                require(!item("importPastedSource")->isEnabled(), "Empty paste can be submitted");
                press("cancelPastedSource");
            } else {
                fill("pasteSourceOrigin", (mode == "paste_origin_invalid" ? QString("https://invalid.example/config") : origin).toUtf8());
                const QByteArray input = mode == "paste_invalid" ? QByteArray("apiVersion: [\n") : yaml;
                fill("pasteSourceYaml", input);
                if (mode == "paste_cancel" || mode == "paste_escape") {
                    if (mode == "paste_escape") QTest::keyClick(window, Qt::Key_Escape);
                    else press("cancelPastedSource");
                    require(QTest::qWaitFor([&] { return item("pasteSourceYaml") == nullptr; }, 2000), "Cancelled paste dialog remained visible");
                    for (auto* editor : window->findChildren<QObject*>("pasteSourceYaml")) require(editor->property("text").toString().isEmpty(), "Cancelled pasted credentials were retained in the editor");
                } else {
                    QLockFile lock(profile + "/kubeconfigs/sources.lock");
                    if (mode == "paste_busy") { lock.setStaleLockTime(0); require(lock.tryLock(0), "Cannot lock source store"); }
                    press("importPastedSource"); settle();
                    const bool rejected = mode == "paste_invalid" || mode == "paste_origin_invalid" || mode == "paste_busy";
                    if (rejected) {
                        auto* error = item("pasteSourceError");
                        require(error && !error->property("text").toString().isEmpty(), "Rejected paste has no inline error");
                        require(item("pasteSourceYaml")->property("text").toString().toUtf8() == input, "Rejected import lost the editable input");
                        require(!error->property("text").toString().contains("source-input-secret"), "Paste error exposed credentials");
                    } else {
                        require(sourceCount() == 1 && workspace.contexts().size() == 2, "Pasted import did not populate contexts");
                        require(!QFileInfo(origin).exists(), "Paste wrote a source file instead of only its owned snapshot");
                        require(QTest::qWaitFor([&] { return item("pasteSourceYaml") == nullptr; }, 2000), "Successful paste did not close the editor");
                        if (mode == "paste_repeat" || mode == "paste_changed") {
                            press("pasteSourceButton");
                            fill("pasteSourceOrigin", origin.toUtf8());
                            fill("pasteSourceYaml", mode == "paste_changed" ? yaml + "# changed\n" : yaml);
                            press("importPastedSource"); settle();
                            require(sourceCount() == (mode == "paste_changed" ? 2 : 1), "Pasted content identity did not preserve immutable deduplication");
                        }
                    }
                }
            }
            if (QStringList{"paste_empty", "paste_cancel", "paste_escape", "paste_invalid", "paste_origin_invalid", "paste_busy"}.contains(mode))
                require(sourceCount() == (seeded ? 1 : 0), "Failed or cancelled paste changed owned snapshots");
        }
        require(workspace.currentSession().isEmpty() && catalog().sessions.isEmpty(), "Import unexpectedly opened a session");
        return true;
    }
    press("settingsManageSessions");
    require(QTest::qWaitFor([&] { return item("sessionManageNamespaces") != nullptr; }, 2000), "Session manager did not open");
    if (mode == "narrow") {
        window->setWidth(390); window->setHeight(720);
        require(QTest::qWaitFor([&] { return window->contentItem()->width() == 390
            && window->contentItem()->height() == 720; }, 2000), "Narrow scene did not resize to the actual viewport");
    }
    const QString evidence = qEnvironmentVariable("PODLORD_TEST_EVIDENCE_DIR");
    if (!evidence.isEmpty()) require(window->grabWindow().save(evidence + "/" + scenario + ".png"), "Cannot save actual session-manager frame");
    if (mode == "narrow") {
        auto* namespaces = item("sessionManageNamespaces");
        require(namespaces && namespaces->mapToScene(QPointF()).x() >= 0
            && namespaces->mapToScene(QPointF(namespaces->width(), 0)).x() <= window->width(), "Narrow session editor overflows the screen");
        press("cancelSessionManagement");
        require(QTest::qWaitFor([&] { return item("sessionManageNamespaces") == nullptr; }, 2000), "Narrow Close button is clipped or inaccessible");
        require(!bindingLoop, "Narrow management emitted a layout binding loop");
        return true;
    }
    if (mode == "cancel" || mode == "escape") {
        fill("sessionManageNamespaces", "team-a");
        if (mode == "escape") QTest::keyClick(window, Qt::Key_Escape);
        else press("cancelSessionManagement");
        require(QTest::qWaitFor([&] { return item("sessionManageNamespaces") == nullptr; }, 2000), "Cancelled session editor remained visible");
        require(catalog().sessions.size() == 1, "Cancelled editing mutated sessions");
        return true;
    }
    if (mode == "missing") {
        require(!workspace.saveSessionConfiguration("missing", contextId, "team-a") && !workspace.sessionManagementError().isEmpty(), "Missing session was not rejected explicitly");
    } else if (mode == "invalid_context") {
        require(!workspace.saveSessionConfiguration(sessionId, "missing", "team-a") && !workspace.sessionManagementError().isEmpty(), "Missing context was not rejected explicitly");
    } else if (mode.startsWith("duplicate")) {
        fill("sessionManageNamespaces", "unsaved-scope");
        if (mode != "duplicate_unnamed") fill("sessionCopyName", "Independent copy");
        press("duplicateSessionConfiguration"); settle();
        const auto saved = catalog();
        require(saved.sessions.size() == 2 && !saved.activeSession, "Copy did not create a closed independent session");
        require(saved.sessions.last().sequenceId != saved.sessions.first().sequenceId
            && saved.sessions.last().config == saved.sessions.first().config && saved.sessions.last().usageAt.isEmpty(), "Copy inherited sequence/history or unsaved draft fields");
        require(saved.sessions.last().name == (mode == "duplicate_unnamed" ? QString{} : QString("Independent copy")), "Copy did not preserve the requested name");
    } else {
        QLockFile lock(profile + "/sessions.lock");
        if (mode == "busy") { lock.setStaleLockTime(0); require(lock.tryLock(0), "Cannot lock session store"); }
        if (mode != "unchanged") fill("sessionManageNamespaces", mode == "invalid_namespace" ? "INVALID!" : " team-b, team-a, team-b ");
        if (mode == "context") {
            auto* context = item("sessionManageContext"); context->forceActiveFocus();
            QTest::keyClick(window, Qt::Key_End); QTest::keyClick(window, Qt::Key_Return);
        }
        press("saveSessionConfiguration"); settle();
        const auto saved = catalog();
        const bool rejected = mode == "busy" || mode == "invalid_namespace";
        require(saved.sessions.size() == ((rejected || mode == "unchanged") ? 1 : 2), "Configuration save did not respect snapshot identity");
        if (rejected) {
            require(!workspace.sessionManagementError().isEmpty(), "Rejected session save has no inline error");
            require(item("sessionManageNamespaces")->property("text").toString().contains(mode == "busy" ? "team-a" : "INVALID!"), "Rejected session save lost the draft");
        } else {
            require(!workspace.sessionManagementNotice().isEmpty(), "Successful session save has no outcome");
            if (mode != "unchanged") {
                require(saved.sessions.first().config == podlord::SessionConfig{contextId, {}}, "Original session configuration changed");
                require(saved.sessions.last().config.namespaces == QStringList{"team-a", "team-b"}
                    && saved.sessions.last().sequenceId == saved.sessions.first().sequenceId && saved.sessions.last().ordinal == 2
                    && !saved.sessions.last().open && saved.sessions.last().usageAt.isEmpty(), "Snapshot lost namespace canonicalization or closed-series semantics");
                require(saved.sessions.last().config.contextId == (mode == "context" ? otherContext : contextId), "Snapshot used the wrong context");
            }
            if (mode == "repeat") { press("saveSessionConfiguration"); settle(); require(catalog().sessions.size() == 2, "Repeated snapshot created a duplicate"); }
            if (mode == "restart") {
                podlord::Workspace restored(profile);
                require(QTest::qWaitFor([&] { return !restored.busy(); }, 5000) && restored.sessions().size() == 2
                    && restored.currentSession().isEmpty(), "Restart did not restore closed configuration snapshots");
            }
        }
    }
    require(workspace.currentSession().isEmpty() && !catalog().activeSession, "Managing sessions unexpectedly activated a cluster");
    return true;
}
} // namespace

int main(int argc, char* argv[]) {
    const QGuiApplication app(argc, argv);
    try {
        require(argc == 3, "Expected source executable and scenario");
        const QString executable = QString::fromLocal8Bit(argv[1]);
        const QString scenario = QString::fromLocal8Bit(argv[2]);
        QTemporaryDir temporary;
        require(temporary.isValid(), "Cannot create isolated source profile");
        const QString root = temporary.path();
        QString profile = root + "/profile";
        const QString directory = root + "/sources";
        const QString home = root + "/home";
        require(QDir().mkpath(directory) && QDir().mkpath(home), "Cannot create isolated inputs");
        if (scenario.startsWith("source_alias_contract_")) {
            const QString mode = scenario.mid(22);
            const QString origin = directory + "/config";
            write(origin, config() + "- name: second\n  context:\n    cluster: local\n    user: local\n");
            const auto imported = run(executable, profile, {"import", origin}, home);
            require(imported.code == 0, "Cannot prepare original format-1 source");
            const auto contexts = imported.json["contexts"].toArray();
            const QString id = contexts.first().toObject()["id"].toString();
            const QString owned = imported.json["ownedPath"].toString();
            const auto bytes = [&] { QFile file(owned); require(file.open(QIODevice::ReadOnly), "Cannot read owned public artifact"); return file.readAll(); };
            const auto original = bytes();
            QString name = "Operations";
            QString target = id;
            if (mode == "empty_context") target.clear();
            if (mode == "missing_context") target = "missing";
            if (mode == "long") name = QString(513, 'a');
            if (mode == "control") name = "bad\nname";
            if (mode == "unicode") name = QString::fromUtf8("\xC3\x9C" "berblick");
            if (mode == "missing_profile") profile = root + "/missing-profile";
            if (mode == "symlink") {
                require(QFile::rename(owned, owned + ".original") && QFile::link(owned + ".original", owned), "Cannot establish symlink boundary");
            }
            auto args = QStringList{"rename", target, name};
            if (mode == "arguments") args.append("extra");
            const auto renamed = run(executable, profile, args, home);
            const bool rejected = QStringList{"empty_context", "missing_context", "long", "control", "missing_profile", "symlink", "arguments"}.contains(mode);
            if (rejected) {
                const int expectedCode = QStringList{"missing_context", "missing_profile", "symlink"}.contains(mode) ? 1 : 2;
                require(renamed.code == expectedCode && renamed.json.contains("error") && bytes() == original, "Rejected CLI alias changed original data or lacked an explicit error");
                if (mode == "missing_profile") require(!QFileInfo(profile).exists(), "Missing source mutation created a profile");
                return 0;
            }
            require(renamed.code == 0 && renamed.json["id"] == imported.json["id"]
                && renamed.json["contentHash"] == imported.json["contentHash"]
                && renamed.json["contexts"].toArray().first().toObject()["name"] == "local"
                && renamed.json["contexts"].toArray().first().toObject()["displayName"].toString().normalized(QString::NormalizationForm_C) == name.normalized(QString::NormalizationForm_C)
                && renamed.json["contexts"].toArray().last().toObject()["displayName"] == "second", "Alias changed canonical identity or its sibling");
            auto record = QJsonDocument::fromJson(bytes()).object();
            const auto originalRecord = QJsonDocument::fromJson(original).object();
            require(record["version"] == 2 && record["yamlBase64"] == originalRecord["yamlBase64"]
                && record["importedAt"] == originalRecord["importedAt"], "Alias migrated original bytes or import recency");
            if (mode == "repeat") {
                const auto saved = bytes();
                require(run(executable, profile, args, home).code == 0 && bytes() == saved, "Repeated alias rewrote the snapshot");
            } else if (mode == "reset") {
                require(run(executable, profile, {"rename", id, ""}, home).code == 0 && bytes() == original, "Reset failed to restore compatible format 1");
            } else if (mode.startsWith("corrupt_")) {
                const auto corruption = mode.mid(8);
                QJsonObject aliases{{id, "Operations"}};
                if (corruption == "missing_field") record.remove("contextAliases");
                else if (corruption == "extra_field") record.insert("unexpected", true);
                else if (corruption == "version") record.insert("version", 99);
                else if (corruption == "fractional_version") record.insert("version", 2.5);
                else if (corruption == "map_type") record.insert("contextAliases", QJsonArray{});
                else {
                    if (corruption == "unknown_context") aliases = {{"missing", "Operations"}};
                    if (corruption == "name_type") aliases.insert(id, true);
                    if (corruption == "empty") aliases.insert(id, "");
                    if (corruption == "whitespace") aliases.insert(id, " Operations ");
                    if (corruption == "control") aliases.insert(id, "bad\nname");
                    if (corruption == "long") aliases.insert(id, QString(513, 'a'));
                    record.insert("contextAliases", aliases);
                }
                const auto damaged = QJsonDocument(record).toJson();
                write(owned, damaged);
                const auto listed = run(executable, profile, {"list"}, home);
                require(listed.code == 0 && listed.json["sources"].toArray().isEmpty() && listed.json["errors"].toArray().size() == 1, "Corrupt aliases were accepted or hidden");
                require(run(executable, profile, args, home).code == 1 && bytes() == damaged, "Alias mutation overwrote a malformed snapshot");
                require(run(executable, profile, {"import", origin}, home).code == 1 && bytes() == damaged, "Reimport overwrote a malformed snapshot");
            }
            return 0;
        }
        if (scenario.startsWith("source_manage_") || scenario.startsWith("session_manage_") || scenario.startsWith("source_alias_"))
            return management(scenario, root, profile) ? 0 : 1;
        const QString first = directory + "/config";
        if (scenario.startsWith("url_")) {
            const QString mode = scenario.mid(4);
            require(QStringList{"file_url", "pretty_url", "plain_path", "literal_percent", "unicode", "upper_scheme", "repeat", "folder_url", "folder_plain", "empty_folder", "partial_folder", "private_folder", "query", "fragment", "credentials", "port", "malformed_percent", "encoded_null", "empty_url", "ui_file", "ui_folder", "ui_partial"}.contains(mode), "Unknown file URL scenario");
            QString file = directory + "/config # % space";
            if (mode == "literal_percent") file = directory + "/config%20literal";
            if (mode == "unicode") file = directory + QString::fromUtf8("/config-\xC3\xA4-\xE6\x97\xA5");
            write(file, config());
            QString input = QUrl::fromLocalFile(file).toString(QUrl::FullyEncoded);
            if (mode == "pretty_url") input = QUrl::fromLocalFile(file).toString();
            if (mode == "plain_path") input = file;
            if (mode == "upper_scheme") input.replace(0, 4, "FILE");
            if (mode == "query") input += "?ignored=1";
            if (mode == "fragment") input += "#ignored";
            if (mode == "credentials") input.insert(7, "user:password@localhost");
            if (mode == "port") input.insert(7, "localhost:1234");
            if (mode == "malformed_percent") input += "%XY";
            if (mode == "encoded_null") input += "%00";
            if (mode == "empty_url") input = "file://";
            const bool invalid = QStringList{"query", "fragment", "credentials", "port", "malformed_percent", "encoded_null", "empty_url"}.contains(mode);
            const bool folder = QStringList{"folder_url", "folder_plain", "empty_folder", "partial_folder", "private_folder"}.contains(mode);
            if (mode.startsWith("ui_")) {
                if (mode != "ui_file") input = QUrl::fromLocalFile(directory).toString();
                if (mode == "ui_partial") write(directory + "/broken.yaml", "not kubeconfig: [\n");
                return ui(profile, input, mode == "ui_partial") ? 0 : 1;
            }
            if (folder) {
                if (mode == "empty_folder") require(QFile::remove(file), "Cannot prepare empty folder");
                else {
                    write(directory + "/nested/deeper/no-extension", config());
                    write(directory + "/arbitrary.txt", config());
                    write(directory + "/.hidden", config());
                }
                if (mode == "partial_folder") write(directory + "/broken.yaml", "not kubeconfig: [\n");
                input = mode == "folder_plain" ? directory : QUrl::fromLocalFile(directory).toString(QUrl::FullyEncoded);
            }
            if (mode == "private_folder") {
                require(run(executable, profile, {"import", file}, home).code == 0, "Cannot prepare owned profile");
                input = QUrl::fromLocalFile(profile).toString(QUrl::FullyEncoded);
            }
            const auto result = run(executable, profile, {folder ? "import-path" : "import", input}, home);
            if (invalid || mode == "private_folder") {
                require(result.code == 2 && result.json["error"].toObject()["code"] == "InvalidInput"
                    && !result.json["error"].toObject()["message"].toString().isEmpty(), "Invalid file URL must fail explicitly");
                const auto catalog = run(executable, profile, {"list"}, home);
                require(catalog.code == 0 && catalog.json["sources"].toArray().size() == (mode == "private_folder" ? 1 : 0), "Rejected URL mutated imported snapshots");
                return 0;
            }
            if (folder) {
                const int count = mode == "empty_folder" ? 0 : 4;
                const int errors = mode == "partial_folder" ? 1 : 0;
                require(result.code == errors && result.json["sources"].toArray().size() == count
                    && result.json["errors"].toArray().size() == errors, "URL folder import lost extension-independent sources or per-file errors");
                const auto catalog = run(executable, profile, {"list"}, home);
                require(catalog.code == 0 && catalog.json["sources"].toArray().size() == count, "Healthy folder snapshots were not retained");
                return 0;
            }
            require(result.code == 0 && result.json["sourcePath"] == file && result.json["contexts"].toArray().size() == 1, "URL did not import the exact file path");
            if (mode == "repeat") {
                const auto again = run(executable, profile, {"import", file}, home);
                require(again.code == 0 && again.json["id"] == result.json["id"], "Path and URL produced different snapshot identities");
                const auto catalog = run(executable, profile, {"list"}, home);
                require(catalog.code == 0 && catalog.json["sources"].toArray().size() == 1, "Repeated URL import duplicated snapshots");
            }
            return 0;
        }
        if (scenario == "empty") {
            const auto result = run(executable, profile, {"import-path", directory}, home);
            require(result.code == 0 && result.json["sources"].toArray().isEmpty() && result.json["errors"].toArray().isEmpty(), "Empty directory did not return an explicit empty report");
            return 0;
        }
        if (scenario == "missing" || scenario == "blank" || scenario == "arguments") {
            const QStringList args = scenario == "arguments" ? QStringList{"import-path", directory, "extra"}
                : QStringList{"import-path", scenario == "blank" ? " " : root + "/missing"};
            const auto result = run(executable, profile, args, home);
            require(result.code != 0 && result.json.contains("error"), "Invalid import request was not rejected explicitly");
            require(!QFileInfo(profile).exists(), "Rejected input created a profile");
            return 0;
        }
        write(first, config());
        if (scenario.startsWith("layout_")) return ui(profile, directory, false, scenario.mid(7).toInt()) ? 0 : 1;
        if (scenario == "nested") write(directory + "/deep/more/other.yml", config("https://127.0.0.1:10"));
        if (scenario == "partial" || scenario == "ui_partial") write(directory + "/broken.yml", "not kubeconfig: [\n");
        if (scenario == "ui" || scenario == "ui_partial") return ui(profile, directory, scenario == "ui_partial") ? 0 : 1;
        if (scenario == "inside_profile") {
            profile = directory + "/owned";
            write(profile + "/do-not-scan.yml", config("https://127.0.0.1:11"));
        }
        if (scenario == "profile_root") {
            const auto result = run(executable, directory, {"import-path", directory}, home);
            require(result.code != 0 && result.json.contains("error"), "Owned profile was recursively imported");
            return 0;
        }
        if (scenario == "symlink") {
            write(root + "/outside/config", config("https://127.0.0.1:11"));
            require(QFile::link(root + "/outside", directory + "/linked"), "Cannot create directory symlink");
            require(QFile::link(first, directory + "/duplicate"), "Cannot create file symlink");
        }
        QString path = scenario == "file" ? first : directory;
        if (scenario == "home") {
            write(home + "/kube/config", config());
            path = "~/kube";
        }
        if (scenario == "unsupported_home") path = "~another/kube";
        if (scenario == "all_invalid") {
            write(first, "malformed: [\n");
        }
        if (scenario == "busy") {
            require(QDir().mkpath(profile + "/kubeconfigs"), "Cannot create lock directory");
            QLockFile lock(profile + "/kubeconfigs/sources.lock");
            require(lock.tryLock(0), "Cannot establish external lock contention");
            const auto result = run(executable, profile, {"import-path", directory}, home);
            require(result.code == 1 && result.json["sources"].toArray().isEmpty() && result.json["errors"].toArray().size() == 1, "Busy source store lost the per-file failure");
            return 0;
        }
        if (scenario == "denied") {
            require(QFile::setPermissions(directory, {}), "Cannot set directory permission boundary");
            const auto result = run(executable, profile, {"import-path", directory}, home);
            QFile::setPermissions(directory, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
            require(result.code != 0 && result.json.contains("error"), "Unreadable root was treated as an empty directory");
            return 0;
        }
        const auto result = run(executable, profile, {"import-path", path}, home);
        if (scenario == "unsupported_home") {
            require(result.code == 2 && result.json.contains("error"), "Unsupported home syntax did not fail explicitly");
            return 0;
        }
        if (scenario == "partial" || scenario == "all_invalid") {
            require(result.code == 1 && result.json["sources"].toArray().size() == (scenario == "partial" ? 1 : 0)
                && result.json["errors"].toArray().size() == 1, "Partial import did not retain healthy sources and expose failures");
            require(result.json["errors"].toArray().first().toObject().contains("sourcePath"), "Failure does not identify its source");
            return 0;
        }
        require(result.code == 0 && result.json["errors"].toArray().isEmpty(), "Valid import failed");
        require(result.json["sources"].toArray().size() == (scenario == "nested" ? 2 : 1), "Wrong imported source count");
        if (scenario == "repeat" || scenario == "changed") {
            if (scenario == "changed") write(first, config("https://127.0.0.1:12"));
            const auto again = run(executable, profile, {"import-path", directory}, home);
            require(again.code == 0, "Repeated folder import failed");
            const auto catalog = run(executable, profile, {"list"}, home);
            require(catalog.code == 0 && catalog.json["sources"].toArray().size() == (scenario == "repeat" ? 1 : 2), "Snapshot identity or retention changed");
        }
        if (scenario == "deleted") {
            require(QFile::remove(first), "Cannot remove external source");
            const auto catalog = run(executable, profile, {"list"}, home);
            require(catalog.code == 0 && catalog.json["sources"].toArray().size() == 1, "Imported snapshot still depends on original file");
        }
        return 0;
    } catch (const std::exception& exception) {
        std::fprintf(stderr, "%s\n", exception.what());
        return 1;
    }
}
