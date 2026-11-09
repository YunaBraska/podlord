#include "workspace.h"
#include "ui_input.h"
#include <QAbstractItemModel>
#include <QClipboard>
#include <QLocale>
#include <QDateTime>
#include <QFile>
#include <QItemSelectionModel>
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
    const QByteArray bytes("apiVersion: v1\nkind: Config\nclusters:\n- name: local\n  cluster: {server: 'http://127.0.0.1:9'}\nusers:\n- name: alpha-user\n  user: {token: fixture-private-token}\n- name: zulu-user\n  user: {username: fixture-private-login, password: fixture-private-password}\ncontexts:\n- name: zulu\n  context: {cluster: local, user: alpha-user}\n- name: alpha\n  context: {cluster: local, user: zulu-user}\n");
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
    if (scenario == "sort" || scenario == "copy_keyboard") window->setWidth(900);
    const auto control = [&](const QString& name) {
        if (name.startsWith("sourceCell_")) {
            const auto parts=name.split('_');
            return test::tableCell(test::visibleItem(window->contentItem(),"sourceTable"),parts[1].toInt(),parts[2].toInt());
        }
        if (name.startsWith("sourceHeader_")) return test::tableCell(test::visibleItem(window->contentItem(),"sourceHeaderView"),0,name.sliced(13).toInt());
        return test::visibleItem(window->contentItem(), name);
    };
    const auto click = [&](const QString& name) {
        if (name == "refreshSourceFilesButton") require(test::clickVisible(window,"sourceActionsMenuButton"), "Cannot open source actions.");
        const bool available=QTest::qWaitFor([&] { auto* target=control(name); return target && target->isEnabled(); });
        if (!available) {
            std::fprintf(stderr,"Unavailable Sources action: %s\n",qPrintable(name));
            const auto capture=qEnvironmentVariable("PODLORD_SOURCES_SCREENSHOT");
            if (!capture.isEmpty()) window->grabWindow().save(capture);
        }
        require(available, "Sources action did not become available.");
        if (name.startsWith("sourceHeader_")) {
            require(QTest::qWaitFor([&] {
                auto* header=control(name);
                return header && !test::visibleBounds(window,header).isEmpty();
            }), "Source header has no visible pointer target.");
            auto* header=control(name);
            // Sorting can synchronously replace the header; the scenario checks the resulting order.
            QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,test::visibleBounds(window,header).center().toPoint());
        } else require(test::clickVisible(window,name), "Sources action unavailable.");
        if (name == "sourceColumnsButton") {
            auto* dialog = window->findChild<QObject*>("sourceColumnsDialog");
            require(dialog && QTest::qWaitFor([&] { return dialog->property("opened").toBool(); }),
                "Sources columns dialog did not finish opening.");
        }
        QCoreApplication::processEvents();
    };
    const auto fill = [&](const QString& name, const QString& value) {
        click(name); QTest::keySequence(window, QKeySequence::SelectAll);
        for (const auto character : value) QTest::keyClick(window, character.toLatin1());
        QCoreApplication::processEvents();
    };
    const auto focus = [&](const QString& name) {
        for (int attempt=0; attempt<4; ++attempt) {
            auto* item = control(name);
            require(item && test::scrollIntoView(window,item), "Sources focus target missing.");
            item->forceActiveFocus(Qt::TabFocusReason);
            QSignalSpy frame(window,&QQuickWindow::frameSwapped);
            window->update();
            require(frame.wait(2000), "Sources keyboard target was not rendered.");
            if (window->activeFocusItem() && window->activeFocusItem()->objectName()==name) return;
        }
        require(false, "Sources focus target did not settle.");
    };
    const auto navigateColumn = [&](int column) {
        auto* view=control("sourceTable");
        auto* selection=view ? view->property("selectionModel").value<QItemSelectionModel*>() : nullptr;
        require(selection && selection->currentIndex().isValid(), "Sources keyboard selection missing.");
        QList<int> order;
        for (const auto& value : workspace.sourceColumns()) {
            const auto state=value.toMap();
            if (state["visible"].toBool() && !state["pinned"].toBool()) order.append(state["column"].toInt());
        }
        require(order.contains(column), "Sources keyboard target is not a visible scrolling column.");
        for (int step=0;step<order.size() && selection->currentIndex().column()!=column;++step) {
            const auto before=selection->currentIndex();
            require(order.contains(before.column()), "Sources keyboard left the scrolling table.");
            QTest::keyClick(window,order.indexOf(before.column())<order.indexOf(column) ? Qt::Key_Right : Qt::Key_Left);
            require(QTest::qWaitFor([&] { return selection->currentIndex()!=before; }), "Sources arrow input did not move selection.");
        }
        require(selection->currentIndex().column()==column && QTest::qWaitFor([&] {
            return test::tableCell(view,selection->currentIndex().row(),column)!=nullptr;
        }), "Sources keyboard did not reveal its live target cell.");
    };
    click("settingsWorkspaceButton");
    require(test::selectSettingsSection(window, "sources"), "Cannot select Sources.");
    require(QTest::qWaitFor([&] { return control("sourceFindButton") && control("settingsSourceList")
        && control("settingsSourceList")->property("count").toInt() == 2; }), "Shared Sources table missing.");
    require(QTest::qWaitFor([&] {
        auto* view=control("sourceTable");
        return test::tableColumnsReady(view,workspace.sourceColumns()) && test::tableCell(view,0,0);
    }), "Sources table has not published its columns and first live cell.");
    auto* model = workspace.property("sourceTable").value<QAbstractItemModel*>();
    require(model && model->columnCount() == 10 && model->headerData(2,Qt::Horizontal).toString() == "User", "Sources User column missing.");
    require(model->index(0,1).data() == "local", "Source cluster metadata missing.");
    for (int row=0;row<model->rowCount();++row) {
        const auto expectedUser = model->index(row,3).data() == "zulu" ? "alpha-user" : "zulu-user";
        require(model->index(row,2).data() == expectedUser, "Source User is not the canonical kubeconfig user entry name.");
        for (int column=0;column<model->columnCount();++column)
            require(!model->index(row,column).data().toString().contains("fixture-private-"), "Source table leaked credential contents.");
    }
    QStringList columnOrder;
    for (const auto& value : workspace.sourceColumns()) columnOrder.append(value.toMap()["id"].toString());
    require(columnOrder == QStringList{"rename","remove","name","imported","cluster","user","context","auth","source","status"}, "Default Sources User column is not after Cluster.");
    const auto identity = model->index(0,0).data(Qt::UserRole).toString();
    const auto firstName = model->index(0,0).data().toString();
    const auto firstUser = model->index(0,2).data().toString();
    if (scenario == "timestamp") {
        const auto imported = workspace.contexts().first().toMap()["imported"].toString();
        const auto time = QDateTime::fromString(imported, Qt::ISODate);
        const auto expected = QLocale::system().toString(time.toLocalTime(), QLocale::ShortFormat);
        require(time.isValid() && model->index(0,6).data() == expected, "Import time is not displayed in system-local short format.");
        require(model->index(0,6).data(Qt::UserRole+6) == imported, "Localized display changed the canonical timestamp sort value.");
        require(workspace.copySourceCell(identity,6) && QGuiApplication::clipboard()->text() == expected,
            "Copied import time differs from the displayed local time.");
    } else if (scenario == "table" || scenario == "narrow") {
        require(control("sourceColumnsButton") && control("renameSource_"+identity) && control("removeSource_"+identity), "Source row actions missing.");
        if (scenario == "table") require(QTest::qWaitFor([&] { auto* cell=control("sourceCell_0_2"); return cell && cell->property("text")==firstUser; }), "Canonical User name is not rendered in the Sources table.");
    } else if (scenario == "sort") {
        click("sourceHeader_0");
        require(model->index(0,0).data() == "alpha" && workspace.property("sourceSortDirection") == "ASC", "Ascending source sort failed.");
        click("sourceHeader_0");
        require(model->index(0,0).data() == "zulu" && workspace.property("sourceSortDirection") == "DESC", "Descending source sort failed.");
        click("sourceHeader_0");
        require(workspace.property("sourceSortDirection") == "NONE" && model->index(0,0).data() == firstName, "Unsorted source order failed.");
        focus("sourceCell_0_0"); navigateColumn(2);
        click("sourceHeader_2");
        require(model->index(0,2).data() == "alpha-user" && model->index(0,0).data() == "zulu" && workspace.sourceSortDirection() == "ASC", "Ascending User sort failed.");
        click("sourceHeader_2");
        require(model->index(0,2).data() == "zulu-user" && model->index(0,0).data() == "alpha" && workspace.sourceSortDirection() == "DESC", "Descending User sort failed.");
        click("sourceHeader_2");
        require(workspace.sourceSortDirection() == "NONE" && model->index(0,0).data() == firstName, "Unsorted User order failed.");
        require(workspace.sortSourceColumn(7) && workspace.sourceSortColumn() == 7 && workspace.sourceSortDirection() == "ASC", "Shifted Status column is not sortable.");
    } else if (scenario == "copy_keyboard") {
        focus("sourceCell_0_0"); navigateColumn(2); QTest::keySequence(window, QKeySequence::Copy);
        require(QGuiApplication::clipboard()->text() == firstUser, "Source User clipboard value is not the entry name.");
        navigateColumn(0); QTest::keySequence(window, QKeySequence::Copy);
        const auto copied = QGuiApplication::clipboard()->text();
        if (copied != firstName) std::fprintf(stderr,"Source copy: expected=%s actual=%s focus=%s\n",qPrintable(firstName),qPrintable(copied),qPrintable(window->activeFocusItem() ? window->activeFocusItem()->objectName() : QString{}));
        require(copied == firstName, "Source clipboard value incorrect.");
    } else if (scenario == "find" || scenario == "find_empty" || scenario == "find_keyboard") {
        if (scenario == "find_keyboard") { focus("sourceCell_0_0"); QTest::keySequence(window, QKeySequence::Find); }
        else click("sourceFindButton");
        fill("sourceFindInput", scenario == "find_empty" ? "absent-context" : "local");
        require(QTest::qWaitFor([&] { return control("sourceFindCount") && control("sourceFindCount")->property("text") == (scenario == "find_empty" ? "0/0" : "1/2"); }), "Sources Find count incorrect.");
        if (scenario != "find_empty") {
            click("sourceFindNext"); require(control("sourceFindCount")->property("text") == "2/2", "Next source match missing.");
            click("sourceFindNext"); require(control("sourceFindCount")->property("text") == "1/2", "Source Find did not wrap.");
            click("sourceFindPrevious"); require(control("sourceFindCount")->property("text") == "2/2", "Previous source match missing.");
            fill("sourceFindInput", "alpha-user");
            require(QTest::qWaitFor([&] { return control("sourceFindCount")->property("text") == "1/1"; }), "Sources Find did not match the canonical User name.");
            fill("sourceFindInput", "fixture-private-");
            require(QTest::qWaitFor([&] { return control("sourceFindCount")->property("text") == "0/0"; }), "Sources Find exposed credential contents.");
        }
    } else if (scenario == "columns" || scenario == "columns_restart") {
        click("sourceColumnsButton"); click("sourceColumnVisible_auth");
        const auto shown = control("sourceColumnVisible_auth")->property("checked").toBool();
        click("sourceColumnVisible_user");
        require(!control("sourceColumnVisible_user")->property("checked").toBool(), "Sources User column could not be hidden.");
        click("sourceColumnPinned_cluster");
        const auto pinned = control("sourceColumnPinned_cluster")->property("checked").toBool();
        if (shown || !pinned) {
            qWarning() << "Sources column draft after input: auth shown=" << shown << "cluster pinned=" << pinned;
            const auto capture = qEnvironmentVariable("PODLORD_SOURCES_SCREENSHOT");
            if (!capture.isEmpty()) window->grabWindow().save(capture);
        }
        click("sourceSaveColumns");
        const auto accepted = [](const QVariantList& columns) {
            bool hidden = false, userHidden = false, pinned = false;
            for (const auto& value : columns) { const auto column = value.toMap();
                if (column["id"] == "auth") hidden = !column["visible"].toBool();
                if (column["id"] == "user") userHidden = !column["visible"].toBool();
                if (column["id"] == "cluster") pinned = column["pinned"].toBool();
            }
            return hidden && userHidden && pinned;
        };
        require(QTest::qWaitFor([&] { return !workspace.property("tableLayoutSaving").toBool()
            && accepted(workspace.property("sourceColumns").toList()); }), "Source layout not applied.");
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
        const int column=scenario.endsWith("negative") ? -1 : scenario.endsWith("action") ? 8 : 10;
        require(!workspace.sortSourceColumn(column) && workspace.sourceSortDirection()=="NONE", "Unsupported source sort accepted.");
        if (scenario.endsWith("action")) require(!workspace.sortSourceColumn(9), "Remove action column accepted sorting.");
    } else if (scenario.startsWith("copy_rejected_")) {
        QGuiApplication::clipboard()->setText("retained-clipboard");
        const int column=scenario.endsWith("negative") ? -1 : scenario.endsWith("high") ? 10 : 0;
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
        require(QTest::qWaitFor([&] { return !workspace.busy() && model->rowCount() == (scenario == "remove_cancel" ? 2 : 1)
            && !control("confirmSourceRemoval") && !control("cancelSourceRemoval"); }), "Source removal outcome incorrect.");
        if (scenario=="remove_all") {
            focus("removeSource_"+model->index(0,0).data(Qt::UserRole).toString());
            QTest::keyClick(window,Qt::Key_Space); click("confirmSourceRemoval");
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
