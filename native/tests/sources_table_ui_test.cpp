#include "workspace.h"
#include "ui_input.h"
#include <QAbstractItemModel>
#include <QClipboard>
#include <QFile>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QTemporaryDir>
#include <stdexcept>

namespace {
bool require(bool accepted, const char* message) {
    if (!accepted) throw std::runtime_error(message);
    return true;
}
bool run(const QString& scenario) {
    using namespace podlord;
    QTemporaryDir directory;
    require(directory.isValid(), "Private source profile unavailable.");
    const auto profile = directory.filePath("profile");
    const auto source = directory.filePath("config");
    QFile config(source);
    const QByteArray bytes("apiVersion: v1\nkind: Config\nclusters:\n- name: local\n  cluster: {server: 'http://127.0.0.1:9'}\nusers:\n- name: local\n  user: {}\ncontexts:\n- name: zulu\n  context: {cluster: local, user: local}\n- name: alpha\n  context: {cluster: local, user: local}\n");
    require(config.open(QIODevice::WriteOnly) && config.write(bytes) == bytes.size(), "Cannot write test kubeconfig.");
    config.close();
    Workspace workspace(profile);
    require(QTest::qWaitFor([&] { return !workspace.busy(); }) && workspace.importFile(source)
        && QTest::qWaitFor([&] { return !workspace.busy() && workspace.contexts().size() == 2; }), "Source import failed.");
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("workspace", &workspace);
    engine.load(QUrl("qrc:/podlord/Main.qml"));
    require(!engine.rootObjects().isEmpty(), "Sources window unavailable.");
    auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
    require(window != nullptr, "Sources window is not native.");
    if (scenario == "narrow") window->setWidth(360);
    const auto control = [&](const QString& name) { return test::visibleItem(window->contentItem(), name); };
    const auto click = [&](const QString& name) {
        const bool available=QTest::qWaitFor([&] { auto* target=control(name); return target && target->isEnabled(); });
        if (!available) {
            std::fprintf(stderr,"Unavailable Sources action: %s\n",qPrintable(name));
            const auto capture=qEnvironmentVariable("PODLORD_SOURCES_SCREENSHOT");
            if (!capture.isEmpty()) window->grabWindow().save(capture);
        }
        require(available, "Sources action did not become available.");
        require(test::clickVisible(window,name), "Sources action unavailable.");
        QCoreApplication::processEvents();
    };
    const auto fill = [&](const QString& name, const QString& value) {
        click(name); QTest::keySequence(window, QKeySequence::SelectAll);
        for (const auto character : value) QTest::keyClick(window, character.toLatin1());
        QCoreApplication::processEvents();
    };
    const auto focus = [&](const QString& name) {
        auto* item = control(name);
        require(item && test::scrollIntoView(window,item), "Sources focus target missing.");
        item->forceActiveFocus(Qt::TabFocusReason);
        require(QTest::qWaitFor([&] { return item->hasActiveFocus() && item->property("current").toBool(); }), "Sources focus target did not activate.");
    };
    click("settingsWorkspaceButton");
    require(test::selectSettingsSection(window, "sources"), "Cannot select Sources.");
    require(QTest::qWaitFor([&] { return control("sourceFindButton") && control("settingsSourceList")
        && control("settingsSourceList")->property("count").toInt() == 2; }), "Shared Sources table missing.");
    auto* model = workspace.property("sourceTable").value<QAbstractItemModel*>();
    require(model && model->columnCount() == 9, "Sources columns missing.");
    require(model->index(0,1).data() == "local", "Source cluster metadata missing.");
    const auto identity = model->index(0,0).data(Qt::UserRole).toString();
    const auto firstName = model->index(0,0).data().toString();
    if (scenario == "table" || scenario == "narrow") {
        require(control("sourceColumnsButton") && control("renameSource_"+identity) && control("removeSource_"+identity), "Source row actions missing.");
    } else if (scenario == "sort") {
        click("sourceHeader_0");
        require(model->index(0,0).data() == "alpha" && workspace.property("sourceSortDirection") == "ASC", "Ascending source sort failed.");
        click("sourceHeader_0");
        require(model->index(0,0).data() == "zulu" && workspace.property("sourceSortDirection") == "DESC", "Descending source sort failed.");
        click("sourceHeader_0");
        require(workspace.property("sourceSortDirection") == "NONE" && model->index(0,0).data() == firstName, "Unsorted source order failed.");
    } else if (scenario == "copy_keyboard") {
        focus("sourceCell_0_0"); QTest::keySequence(window, QKeySequence::Copy);
        require(QTest::qWaitFor([&] { return QGuiApplication::clipboard()->text() == firstName; }), "Source clipboard value incorrect.");
    } else if (scenario == "find" || scenario == "find_empty" || scenario == "find_keyboard") {
        if (scenario == "find_keyboard") { focus("sourceCell_0_0"); QTest::keySequence(window, QKeySequence::Find); }
        else click("sourceFindButton");
        fill("sourceFindInput", scenario == "find_empty" ? "absent-context" : "local");
        require(QTest::qWaitFor([&] { return control("sourceFindCount") && control("sourceFindCount")->property("text") == (scenario == "find_empty" ? "0/0" : "1/2"); }), "Sources Find count incorrect.");
        if (scenario != "find_empty") {
            click("sourceFindNext"); require(control("sourceFindCount")->property("text") == "2/2", "Next source match missing.");
            click("sourceFindNext"); require(control("sourceFindCount")->property("text") == "1/2", "Source Find did not wrap.");
            click("sourceFindPrevious"); require(control("sourceFindCount")->property("text") == "2/2", "Previous source match missing.");
        }
    } else if (scenario == "columns" || scenario == "columns_restart") {
        click("sourceColumnsButton"); click("sourceColumnVisible_auth"); click("sourceColumnPinned_cluster"); click("sourceSaveColumns");
        require(QTest::qWaitFor([&] { return !workspace.property("tableLayoutSaving").toBool(); }), "Source layout save pending.");
        const auto accepted = [](const QVariantList& columns) {
            bool hidden = false, pinned = false;
            for (const auto& value : columns) { const auto column = value.toMap();
                if (column["id"] == "auth") hidden = !column["visible"].toBool();
                if (column["id"] == "cluster") pinned = column["pinned"].toBool();
            }
            return hidden && pinned;
        };
        require(accepted(workspace.property("sourceColumns").toList()), "Source layout not applied.");
        if (scenario == "columns_restart") {
            Workspace restored(profile);
            require(QTest::qWaitFor([&] { return !restored.busy(); }) && accepted(restored.property("sourceColumns").toList()), "Source layout not restored.");
        }
    } else if (scenario == "hidden") {
        require(test::selectSettingsSection(window,"appearance"), "Cannot hide Sources.");
        require(workspace.renameSourceContext(identity,"hidden-source") && QTest::qWaitFor([&] { return !workspace.busy(); }), "Hidden source rename failed.");
        require(model->index(0,0).data() == firstName, "Hidden Sources updated its table.");
        require(test::selectSettingsSection(window,"sources") && QTest::qWaitFor([&] { return model->index(0,0).data() == "hidden-source"; }), "Sources did not publish cached metadata on return.");
    } else if (scenario == "repeat") {
        for (int i=0;i<20;++i) require(workspace.refreshSourceTable(), "Repeated cache publication failed.");
        require(model->rowCount()==2 && model->index(0,0).data(Qt::UserRole)==identity, "Repeated publication changed membership.");
    } else if (scenario.startsWith("sort_rejected_")) {
        const int column=scenario.endsWith("negative") ? -1 : scenario.endsWith("action") ? 7 : 9;
        require(!workspace.sortSourceColumn(column) && workspace.sourceSortDirection()=="NONE", "Unsupported source sort accepted.");
    } else if (scenario.startsWith("copy_rejected_")) {
        QGuiApplication::clipboard()->setText("retained-clipboard");
        const int column=scenario.endsWith("negative") ? -1 : scenario.endsWith("high") ? 9 : 0;
        const auto key=scenario.endsWith("empty") ? QString{} : scenario.endsWith("missing") ? "absent-source" : identity;
        require(!workspace.copySourceCell(key,column) && QGuiApplication::clipboard()->text()=="retained-clipboard", "Unsupported source copy changed clipboard.");
    } else if (scenario == "rename") {
        click("renameSource_"+identity);
        fill("sourceAliasName", "renamed-source");
        require(control("sourceAliasName")->property("text")=="renamed-source", "Source alias text input failed.");
        click("saveSourceAlias");
        const bool renamed=QTest::qWaitFor([&] { return !workspace.busy() && model->index(0,0).data() == "renamed-source"; });
        if (!renamed) {
            std::fprintf(stderr,"Source rename: displayed=%s error=%s\n",qPrintable(model->index(0,0).data().toString()),qPrintable(workspace.sourceImportError()));
            for (const auto& value:workspace.contexts()) { const auto context=value.toMap();
                std::fprintf(stderr,"  context=%s label=%s selected=%d\n",qPrintable(context["context"].toString()),qPrintable(context["name"].toString()),context["id"]==identity);
            }
        }
        require(renamed, "Renamed source not published.");
        require(model->index(0,0).data(Qt::UserRole) == identity, "Renaming changed source identity.");
    } else if (scenario == "refresh_changed") {
        require(config.open(QIODevice::WriteOnly|QIODevice::Truncate) && config.write(bytes+"# changed\n")==bytes.size()+10, "Cannot change external kubeconfig.");
        config.close(); click("refreshSourceFilesButton");
        require(QTest::qWaitFor([&] { return !workspace.busy() && workspace.contexts().size()==4
            && model->rowCount()==4 && control("settingsSourceList")->property("count").toInt()==4; }), "Pinned Sources table did not grow after reimport.");
    } else if (scenario == "remove_cancel" || scenario == "remove" || scenario == "remove_all") {
        click("removeSource_"+identity);
        click(scenario == "remove_cancel" ? "cancelSourceRemoval" : "confirmSourceRemoval");
        require(QTest::qWaitFor([&] { return !workspace.busy() && model->rowCount() == (scenario == "remove_cancel" ? 2 : 1); }), "Source removal outcome incorrect.");
        if (scenario=="remove_all") {
            click("removeSource_"+model->index(0,0).data(Qt::UserRole).toString()); click("confirmSourceRemoval");
            require(QTest::qWaitFor([&] { return !workspace.busy() && model->rowCount()==0 && control("sourceEmpty"); }), "Sources empty state missing after confirmed removal.");
        }
    } else throw std::runtime_error("Unknown Sources scenario.");
    require(workspace.sessions().isEmpty() && workspace.settingsDiagnostics()["requests"].toList().isEmpty(), "Source table contacted Kubernetes or opened a session.");
    const auto screenshot = qEnvironmentVariable("PODLORD_SOURCES_SCREENSHOT");
    return screenshot.isEmpty() || window->grabWindow().save(screenshot);
}
}
int main(int argc, char** argv) {
    const QGuiApplication application(argc, argv);
    try { return argc == 2 && run(QString::fromLocal8Bit(argv[1])) ? 0 : 1; }
    catch (const std::exception& error) { std::fprintf(stderr, "%s\n", error.what()); return 1; }
}
