#include "workspace.h"
#include "window_host.h"
#include "ui_input.h"
#include <QClipboard>
#include <QFile>
#include <QMimeData>
#include <QQmlContext>
#include <QTemporaryDir>
#include <QThreadPool>
#include <QSemaphore>
#include <QScopeGuard>
#include <QtConcurrent/QtConcurrentRun>
#include <stdexcept>
#include <memory>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
bool run(const QString& scenario, const QString& liveConfig) {
    using namespace podlord;
    require(QStringList{"view","apply","apply_live","invalid","reset_invalid","metadata_reload","repeat","missing","tampered","symlink","dirty_stay","dirty_discard","narrow","close_loading","remove","identities","removal_editing","removal_pending"}.contains(scenario), "Unknown source inspector scenario.");
    auto previousClipboard=std::make_unique<QMimeData>();
    if (const auto* current=QGuiApplication::clipboard()->mimeData())
        for (const auto& format : current->formats()) previousClipboard->setData(format,current->data(format));
    const auto restoreClipboard=qScopeGuard([&] { QGuiApplication::clipboard()->setMimeData(previousClipboard.release()); });
    QTemporaryDir directory;
    require(directory.isValid(), "Private source profile unavailable.");
    const auto profile=directory.filePath("profile"), path=directory.filePath("config");
    const bool live=scenario=="apply_live";
    QByteArray bytes("apiVersion: v1\nkind: Config\nclusters:\n- name: local\n  cluster: {server: 'http://127.0.0.1:9'}\nusers:\n- name: reader\n  user: {token: fixture-editor-private-token}\ncontexts:\n- name: alpha\n  context: {cluster: local, user: reader}\n- name: beta\n  context: {cluster: local, user: reader}\n");
    if (live) {
        QFile config(liveConfig);
        require(!liveConfig.isEmpty() && config.open(QIODevice::ReadOnly), "Real cluster config unavailable.");
        bytes=config.read(16*1024*1024+1);
        require(!bytes.isEmpty() && bytes.size()<=16*1024*1024, "Real cluster config exceeds source limits.");
    }
    QFile original(path);
    require(original.open(QIODevice::WriteOnly) && original.write(bytes)==bytes.size(), "Cannot write source fixture.");
    original.close();
    const KubeconfigStore store(profile);
    const auto imported=store.importFile(path);
    require(std::holds_alternative<SourceSnapshot>(imported), "Fixture import failed.");
    const auto source=std::get<SourceSnapshot>(imported);
    const auto context=live ? source.contexts.first().id : std::find_if(source.contexts.cbegin(),source.contexts.cend(),[](const auto& entry) { return entry.name=="alpha"; })->id;
    const auto other=live ? QString{} : std::find_if(source.contexts.cbegin(),source.contexts.cend(),[](const auto& entry) { return entry.name=="beta"; })->id;
    auto created=SessionStore(profile).create({context,{}}, "Existing session");
    require(std::holds_alternative<SessionCatalog>(created), "Fixture session failed.");
    const bool windowScenario=scenario.startsWith("removal_");
    if (windowScenario || live) created=SessionStore(profile).activate(std::get<SessionCatalog>(created).sessions.first().id);
    require(std::holds_alternative<SessionCatalog>(created), "Fixture tab failed.");
    const auto before=std::get<SessionCatalog>(created);
    QFile owned(source.ownedPath);
    require(owned.open(QIODevice::ReadOnly), "Owned fixture unavailable.");
    const auto ownedBytes=owned.readAll(); owned.close();
    Workspace workspace(profile);
    require(QTest::qWaitFor([&] { return !workspace.busy(); }), "Workspace did not load.");
    WindowHost host(workspace);
    require(host.open(), "Source window unavailable.");
    auto* window=host.windows().first();
    Workspace* peer=nullptr;
    if (windowScenario || live) {
        const auto session=std::get<SessionCatalog>(created).sessions.first().id.toString(QUuid::WithoutBraces);
        const bool started=workspace.activate(session);
        const bool ready=started && QTest::qWaitFor([&] { return !workspace.busy() && !workspace.loading(); },live ? 60000 : 15000);
        if (!ready) std::fprintf(stderr,"Source session activation: started=%d busy=%d loading=%d resources=%d error=%s\n",started,workspace.busy(),workspace.loading(),workspace.totalResourceCount(),qPrintable(workspace.error()));
        require(ready, "Fixture session did not finish its explicit activation.");
        if (live) require(workspace.totalResourceCount()>0, "Real Kubernetes resources were not loaded.");
        if (windowScenario) {
            require(QTest::qWaitFor([&] { return workspace.sessionWindowActionsReady(); }), "Fixture tab not ready to move.");
            require(workspace.detachSession(session) && host.windows().size()==2, "Cannot create second public window.");
            peer=qmlContext(host.windows().last())->contextProperty("workspace").value<Workspace*>();
            require(peer && QTest::qWaitFor([&] { return !peer->busy(); }), "Second workspace unavailable.");
        }
    }
    const auto active=workspace.currentSession();
    const auto initialRequests=workspace.settingsDiagnostics()["requests"].toList().size();
    if (scenario=="narrow") window->resize(320,640);
    const auto control=[&](const QString& name) { return test::visibleItem(window->contentItem(),name); };
    const auto click=[&](const QString& name) {
        require(QTest::qWaitFor([&] { auto* item=control(name); return item && item->isEnabled(); }), "Source control not ready.");
        require(test::clickVisible(window,name), "Source action unavailable.");
        QCoreApplication::processEvents();
    };
    click("settingsWorkspaceButton");
    require(test::selectSettingsSection(window,"sources"), "Cannot select Sources.");
    auto* rows=workspace.property("sourceTable").value<QAbstractItemModel*>();
    require(rows && rows->rowCount()==source.contexts.size(), "Source rows missing.");
    const auto inspected=rows->index(0,0).data(Qt::UserRole).toString();
    if (scenario=="missing") require(owned.remove(), "Cannot remove owned fixture.");
    if (scenario=="tampered") {
        require(owned.open(QIODevice::WriteOnly|QIODevice::Truncate) && owned.write("{broken}")==8, "Cannot damage owned fixture.");
        owned.close();
    }
    if (scenario=="symlink") {
        require(owned.remove() && QFile::link(path,source.ownedPath), "Cannot replace owned fixture with symlink.");
    }
    if (scenario=="narrow") {
        require(QTest::qWaitFor([&] { auto* item=control("sourceCell_0_0"); return item && test::visibleBounds(window,item).height()>=44; }), "Narrow source row is not visible.");
        QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,test::visibleBounds(window,control("sourceCell_0_0")).center().toPoint());
    } else click("sourceCell_0_0");
    require(QTest::qWaitFor([&] { return !workspace.sourceInspection().isEmpty(); }), "Row did not open source inspector.");
    if (scenario=="close_loading") {
        click("closeSourceInspector");
        require(QThreadPool::globalInstance()->waitForDone(5000), "Source read did not complete.");
        QCoreApplication::sendPostedEvents(); QCoreApplication::processEvents();
        require(workspace.sourceInspection().isEmpty() && workspace.sourceYaml().isEmpty() && !control("sourceInspector"), "Late source read reopened inspector.");
    } else {
        require(QTest::qWaitFor([&] { return !workspace.sourceInspectionLoading(); }), "Source read did not finish.");
        require(workspace.sourceInspection()["contextId"]==inspected, "Wrong context inspected.");
        if (scenario=="missing" || scenario=="tampered" || scenario=="symlink") {
            require(!workspace.sourceInspectionError().isEmpty() && workspace.sourceYaml().isEmpty(), "Invalid owned copy fell back to original.");
            click("sourceYamlTab");
            require(QTest::qWaitFor([&] { return control("editSourceYaml"); }) && !control("editSourceYaml")->isEnabled(), "Unreadable source allowed editing.");
            require(!workspace.sourceInspectionError().contains("fixture-editor-private-token"), "Failure leaked credentials.");
        } else if (scenario=="metadata_reload") {
            auto* pool=QThreadPool::globalInstance();
            require(pool->waitForDone(5000), "Source worker did not settle before reload.");
            const int threads=pool->maxThreadCount();
            QSemaphore parked,resume;
            QFuture<void> blocker;
            const auto cleanup=qScopeGuard([&] { resume.release(); blocker.waitForFinished(); pool->setMaxThreadCount(threads); });
            pool->setMaxThreadCount(1);
            blocker=QtConcurrent::run([&] { parked.release(); resume.acquire(); });
            require(parked.tryAcquire(1,5000) && workspace.reload(), "Cannot hold metadata reload at its worker boundary.");
            click("sourceYamlTab");
            require(control("editSourceYaml") && !control("editSourceYaml")->isEnabled(), "Reload allowed editing while busy.");
            resume.release();
            require(QTest::qWaitFor([&] { return !workspace.busy() && control("editSourceYaml")->isEnabled(); }), "Source edit stayed disabled after metadata reload.");
        } else if (windowScenario) {
            require(peer->requestSourceRemoval(inspected), "Second window cannot request removal.");
            if (scenario=="removal_editing") {
                click("sourceYamlTab"); click("editSourceYaml");
                require(!control("removeInspectedSource")->isEnabled(), "Source editor offers removal while editing.");
                require(!peer->confirmSourceRemoval(), "Removal raced with an open source editor.");
                require(workspace.setSourceDraft(QString::fromUtf8(bytes)+"# preserve draft\n") && workspace.yamlDirty(), "Other window lost its source draft.");
                require(peer->cancelSourceRemoval(), "Cannot cancel blocked removal.");
                click("closeSourceInspector"); click("discardAccept");
            } else {
                require(peer->confirmSourceRemoval(), "Confirmed removal did not start.");
                require(!workspace.beginSourceEdit(), "Source editor opened during another window's removal.");
                require(QTest::qWaitFor([&] { return !peer->busy() && workspace.sourceInspection().isEmpty(); }), "Other window retained removed source.");
            }
        } else if (scenario=="identities") {
            require(std::holds_alternative<Failure>(store.ownedYaml("../config",context)), "Snapshot traversal accepted.");
            require(std::holds_alternative<Failure>(store.ownedYaml(source.id,"../config")), "Context traversal accepted.");
            require(std::holds_alternative<Failure>(store.ownedYaml(source.id,"ctx:"+QString(64,'f'))), "Unrelated context accepted.");
            const auto removed=store.removeContext(other,{});
            require(std::holds_alternative<SessionCatalog>(removed), "Unused context removal failed.");
            require(std::holds_alternative<Failure>(store.ownedYaml(source.id,other)), "Removed context readable.");
        } else {
            require(workspace.sourceYaml()==QString::fromUtf8(bytes), "Owned source text changed while viewing.");
            require(!workspace.sourceInspection().values().contains(QString::fromUtf8(bytes)), "Source metadata includes raw YAML.");
            const auto overviewCapture=qEnvironmentVariable("PODLORD_SOURCE_INSPECTOR_SCREENSHOT");
            if (!overviewCapture.isEmpty() && (scenario=="view" || scenario=="narrow")) {
                QSignalSpy frame(window,&QQuickWindow::afterAnimating); window->update();
                require(frame.wait(2000) && window->grabWindow().save(overviewCapture+".overview.png"), "Source Overview capture failed.");
            }
            click("sourceYamlTab");
            require(QTest::qWaitFor([&] { return control("sourceInspectorYaml"); }), "Source YAML missing.");
            require(control("sourceInspectorYaml")->property("readOnly").toBool() && !workspace.sourceEditing(), "Source editing started implicitly.");
            require(control("sourceYamlTab")->property("checked").toBool() && !control("sourceOverviewTab")->property("checked").toBool(), "Source selected tab is inconsistent.");
            if (scenario=="view" || scenario=="narrow") {
                for (const auto* name : {"closeSourceInspector","sourceOverviewTab","sourceYamlTab","editSourceYaml"}) {
                    auto* item=control(name);
                    require(item && test::visibleBounds(window,item).contains(item->mapRectToScene(QRectF(QPointF{},item->size()))), "Source control clipped.");
                }
                const auto capture=qEnvironmentVariable("PODLORD_SOURCE_INSPECTOR_SCREENSHOT");
                if (!capture.isEmpty()) require(window->grabWindow().save(capture), "Source capture failed.");
            } else if (scenario=="remove") {
                click("removeInspectedSource"); click("confirmSourceRemoval");
                require(QTest::qWaitFor([&] { return !workspace.busy() && workspace.sourceInspection().isEmpty(); }), "Removed source remains inspected.");
            } else {
                click("editSourceYaml");
                require(workspace.sourceEditing() && !control("sourceInspectorYaml")->property("readOnly").toBool(), "Explicit source edit failed.");
                const QString edited=scenario=="invalid" || scenario=="reset_invalid" ? QString("contexts: [\n") : QString::fromUtf8(bytes)+(scenario=="repeat" ? "" : "# edited locally\n");
                if (scenario!="repeat") {
                    require(QTest::qWaitFor([&] { return window->activeFocusItem()==control("sourceInspectorYaml"); }), "Edit did not focus source YAML.");
                    QTest::keySequence(window,QKeySequence::SelectAll);
                    QGuiApplication::clipboard()->setText(edited); QTest::keySequence(window,QKeySequence::Paste);
                    require(QTest::qWaitFor([&] { return workspace.sourceDraft()==edited; }), "Typed source draft not retained.");
                    require(workspace.yamlDirty(), "Edited source is not protected as dirty.");
                }
                if (scenario=="dirty_stay" || scenario=="dirty_discard") {
                    click("closeSourceInspector");
                    click(scenario=="dirty_stay" ? "discardStay" : "discardAccept");
                    require(QTest::qWaitFor([&] { return !workspace.discardPending(); }), "Discard dialog did not settle.");
                    require(scenario=="dirty_stay" ? workspace.sourceEditing() && workspace.sourceDraft()==edited
                        : workspace.sourceInspection().isEmpty() && workspace.sourceDraft().isEmpty() && workspace.sourceYaml().isEmpty(), "Source discard decision ignored.");
                    if (scenario=="dirty_stay") { click("closeSourceInspector"); click("discardAccept"); }
                } else {
                    click("applySourceYaml");
                    require(QTest::qWaitFor([&] { return !workspace.busy(); }), "Source apply did not finish.");
                    if (scenario=="invalid" || scenario=="reset_invalid") {
                        require(workspace.sourceEditing() && workspace.sourceDraft()==edited && !workspace.sourceInspectionError().isEmpty(), "Invalid source lost draft or lacks error.");
                        if (scenario=="reset_invalid") {
                            click("resetSourceDraft"); click("discardAccept");
                            require(QTest::qWaitFor([&] { auto* edit=control("editSourceYaml"); return edit && edit->isEnabled(); }), "Rejected draft permanently disabled source editing.");
                            click("editSourceYaml");
                            require(workspace.sourceDraft()==QString::fromUtf8(bytes) && !workspace.yamlDirty(), "Reset did not restore owned baseline.");
                        }
                    }
                    else {
                        require(!workspace.sourceEditing() && !workspace.yamlDirty() && workspace.sourceYaml()==edited, "Successful source apply left stale draft.");
                        require((workspace.sourceInspection()["snapshotId"].toString()==source.id)==(scenario=="repeat"), "Snapshot identity does not follow content.");
                        const auto catalog=store.list();
                        require(std::holds_alternative<SourceCatalog>(catalog) && std::get<SourceCatalog>(catalog).sources.size()==(scenario=="repeat" ? 1 : 2), "Previous immutable snapshot was lost or duplicated.");
                    }
                    if (workspace.yamlDirty()) { click("closeSourceInspector"); click("discardAccept"); }
                }
                require(original.open(QIODevice::ReadOnly) && original.readAll()==bytes, "Source editor modified original file."); original.close();
                if (scenario=="repeat") {
                    const auto unchanged=store.ownedYaml(source.id,context);
                    require(std::holds_alternative<QString>(unchanged) && std::get<QString>(unchanged)==QString::fromUtf8(bytes), "Repeated import changed immutable YAML.");
                } else {
                    require(owned.open(QIODevice::ReadOnly) && owned.readAll()==ownedBytes, "Source editor modified prior immutable content."); owned.close();
                }
                const auto sessions=SessionStore(profile).list();
                require(std::holds_alternative<SessionCatalog>(sessions) && std::get<SessionCatalog>(sessions).sessions==before.sessions, "Source editor rebound or mutated saved sessions.");
            }
        }
    }
    require(workspace.settingsDiagnostics()["requests"].toList().size()==initialRequests && workspace.currentSession()==active, "Source inspector connected or activated a session.");
    if (!workspace.sourceInspection().isEmpty()) require(workspace.closeInspector(), "Source inspector did not close.");
    return true;
}
}
int main(int argc,char** argv) {
    const QGuiApplication application(argc,argv);
    try { return (argc==2 || argc==3) && run(QString::fromLocal8Bit(argv[1]),argc==3 ? QString::fromLocal8Bit(argv[2]) : QString{}) ? 0 : 1; }
    catch (const std::exception& error) { std::fprintf(stderr,"%s\n",error.what()); return 1; }
}
