#include "workspace.h"
#include "radar_island.h"
#include "ui_input.h"
#include <QElapsedTimer>
#include <QAbstractItemModelTester>
#include <QAccessible>
#include <QClipboard>
#include <QFile>
#include <QFontMetricsF>
#include <QSignalSpy>
#include <QGuiApplication>
#include <QImage>
#include <QJsonDocument>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlExpression>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>
#include <cstdio>
#include <functional>

namespace {
bool waitFor(const std::function<bool()>& ready) {
    QElapsedTimer timer; timer.start();
    while (!ready() && timer.elapsed() < 12000) QTest::qWait(10);
    return ready();
}
QQuickItem* item(QQuickItem* root, const QString& name) {
    if (root->objectName() == name) return root;
    for (auto* child : root->childItems()) if (auto* found = item(child, name)) return found;
    return nullptr;
}
QQuickItem* item(QQuickWindow* window, const QString& name) {
    const auto parts = name.split('_');
    if (parts.size() == 3 && (parts[0] == "cell" || parts[0] == "eventCell" || parts[0] == "pinnedCell")) {
        auto* view = podlord::test::visibleItem(window->contentItem(), parts[0] == "cell" ? "resourceTable"
            : parts[0] == "eventCell" ? "eventTable" : "resourcePinnedTable");
        if (!view) return nullptr;
        QQmlExpression lookup(qmlContext(view), view, QString("itemAtIndex(model.index(%1, %2))").arg(parts[1], parts[2]));
        auto* cell = lookup.evaluate().value<QQuickItem*>();
        if (lookup.hasError()) std::fprintf(stderr, "Filter table lookup: %s\n", qPrintable(lookup.error().toString()));
        return cell;
    }
    if (auto* visible = podlord::test::visibleItem(window->contentItem(), name)) return visible;
    if (auto* found = item(window->contentItem(), name)) return found;
    return window->findChild<QQuickItem*>(name);
}
bool click(QQuickWindow* window, const QString& name) {
    if (!podlord::test::revealWorkspaceAction(window, name)) return false;
    QSignalSpy frames(window, &QQuickWindow::frameSwapped); window->update();
    if (!frames.wait(1000)) return false;
    if (name=="resourceFieldFilters" || name=="radarWorkspaceButton") {
        auto* target=item(window,name);
        if (target && !target->isVisible()) {
            const auto* expander=item(window,"toggleLandscapeFilters");
            const auto action=name=="resourceFieldFilters" && expander && expander->isVisible()
                ? "toggleLandscapeFilters" : "toggleSidebar";
            if (!click(window,action) || !waitFor([&] { return target->isVisible(); })) return false;
        }
    }
    auto* target = item(window, name);
    if (!target || !target->isVisible() || !target->isEnabled()) return false;
    QList<QQuickItem*> ancestors;
    for (auto* parent = target; parent; parent = parent->parentItem()) ancestors.prepend(parent);
    for (auto* parent : ancestors) parent->ensurePolished();
    QCoreApplication::processEvents();
    auto* surface = target->window();
    if (!surface || !podlord::test::scrollIntoView(surface, target)) return false;
    QTest::mouseClick(surface, Qt::LeftButton, Qt::NoModifier, target->mapToScene(QPointF(target->width()/2, target->height()/2)).toPoint());
    return true;
}
bool type(QQuickWindow* window, const QString& name, const QString& text) {
    auto* target = item(window, name);
    if (target && !target->isVisible() && name == "resourceFilter")
        if (!click(window,"workspaceSearchButton") || !waitFor([&] { return target->isVisible(); })) return false;
    if (!target || !target->isVisible() || !target->isEnabled()) return false;
    target->forceActiveFocus(); QTest::keySequence(window, QKeySequence::SelectAll);
    if (text.isEmpty()) QTest::keyClick(window, Qt::Key_Backspace);
    for (const auto character : text) QTest::keyClick(window, character.toLatin1());
    return true;
}
QJsonObject resource(const QString& name, const QString& space, bool problem = false, bool config = false) {
    const bool alpha = name == "alpha";
    QJsonObject result{{"apiVersion", "v1"}, {"kind", config ? "ConfigMap" : "Pod"},
        {"metadata", QJsonObject{{"name", name}, {"namespace", space}, {"uid", "uid-" + name}, {"resourceVersion", "1"}}}};
    if (!config) {
        auto metadata = result["metadata"].toObject();
        metadata["creationTimestamp"] = QDateTime::currentDateTimeUtc().addSecs(alpha ? -90 : -7200).toString(Qt::ISODateWithMs);
        metadata["ownerReferences"] = QJsonArray{QJsonObject{{"apiVersion", "apps/v1"}, {"kind", "Deployment"}, {"name", alpha ? "owner-a" : "owner-b"}, {"uid", alpha ? "owner-a-uid" : "owner-b-uid"}, {"controller", true}}};
        result["metadata"] = metadata;
        result["spec"] = QJsonObject{{"nodeName", alpha ? "node-a" : "node-b"}, {"containers", QJsonArray{QJsonObject{{"name", "main"}, {"image", alpha ? "busybox:1" : "busybox:2"}}}}};
        result["status"] = QJsonObject{{"phase", "Running"}, {"containerStatuses", QJsonArray{QJsonObject{{"name", "main"}, {"ready", !problem}, {"restartCount", 0},
            {"state", problem ? QJsonObject{{"waiting", QJsonObject{{"reason", "CrashLoopBackOff"}}}} : QJsonObject{{"running", QJsonObject{}}}}}}}};
    } else result["data"] = QJsonObject{{"message", "ordinary configuration"}};
    return result;
}
// Only the external Kubernetes HTTP boundary is simulated. Filtering, persistence,
// rendering, session ownership and request scheduling use the shipped implementation.
class KubernetesBoundary final : public QTcpServer {
public:
    int requests = 0;
    int extraConfigurations = 0;
    bool changed = false;
    bool delayed = false;
    bool events = false;
    bool valueCopies = false, secretValues = false;
    int active = 0, maximumActive = 0;
    QJsonObject configuration() const {
        auto value = resource("team-a", "team-b", false, true);
        if (!valueCopies) return value;
        const QJsonObject encoded{{"text", "ZGVjb2RlZC12YWx1ZQ=="}, {"binary", "/wA="}, {"empty", ""}};
        if (secretValues) { value["kind"] = "Secret"; value["data"] = encoded; }
        else value["binaryData"] = encoded;
        return value;
    }
    KubernetesBoundary() {
        connect(this, &QTcpServer::newConnection, this, [this] {
            while (hasPendingConnections()) {
                auto* socket = nextPendingConnection();
                auto bytes = std::make_shared<QByteArray>();
                connect(socket, &QTcpSocket::readyRead, this, [this, socket, bytes] {
                    bytes->append(socket->readAll()); if (!bytes->contains("\r\n\r\n")) return;
                    socket->disconnect(this); ++requests; ++active; maximumActive = std::max(maximumActive, active);
                    const auto path = QUrl::fromEncoded(bytes->split(' ')[1]).path();
                    QJsonObject document; int status = 200;
                    if (path == "/api") document = {{"versions", QJsonArray{"v1"}}};
                    else if (path == "/apis") document = {{"groups", QJsonArray{}}};
                    else if (path == "/api/v1") {
                        QJsonArray resources{
                        QJsonObject{{"name", "pods"}, {"kind", "Pod"}, {"namespaced", true}, {"verbs", QJsonArray{"get", "list"}}},
                        QJsonObject{{"name", secretValues ? "secrets" : "configmaps"}, {"kind", secretValues ? "Secret" : "ConfigMap"}, {"namespaced", true}, {"verbs", QJsonArray{"get", "list"}}}};
                        if (events) resources.append(QJsonObject{{"name", "events"}, {"kind", "Event"}, {"namespaced", true}, {"verbs", QJsonArray{"get", "list"}}});
                        document = {{"resources", resources}};
                    }
                    else if (path == "/api/v1/events") document = {{"metadata", QJsonObject{}}, {"items", QJsonArray{QJsonObject{
                        {"apiVersion", "v1"}, {"kind", "Event"}, {"metadata", QJsonObject{{"name", "scheduled"}, {"namespace", "team-a"}, {"uid", "event-uid"}, {"resourceVersion", "1"}}},
                        {"type", "Normal"}, {"reason", "Scheduled"}, {"message", "Assigned alpha"}, {"count", 1},
                        {"lastTimestamp", QDateTime::currentDateTimeUtc().toString(Qt::ISODate)},
                        {"involvedObject", QJsonObject{{"apiVersion", "v1"}, {"kind", "Pod"}, {"name", "alpha"}, {"namespace", "team-a"}, {"uid", "uid-alpha"}}}}}}};
                    else if (path == "/api/v1/pods") document = {{"metadata", QJsonObject{}}, {"items", QJsonArray{
                        resource(changed ? "charlie" : "alpha", "team-a"), resource("bravo", "team-b", true)}}};
                    else if (path == "/api/v1/configmaps" || path == "/api/v1/secrets") {
                        QJsonArray configurations{configuration()};
                        for (int index = 0; index < extraConfigurations; ++index)
                            configurations.append(resource(QString("limit-%1").arg(index, 3, 10, QChar('0')), "team-b", false, true));
                        document = {{"metadata", QJsonObject{}}, {"items", configurations}};
                    }
                    else if (path == "/api/v1/namespaces/team-b/configmaps/team-a" || path == "/api/v1/namespaces/team-b/secrets/team-a") document = configuration();
                    else if (path == "/api/v1/namespaces/team-a/pods/alpha") document = resource("alpha", "team-a");
                    else status = 404;
                    const auto body = QJsonDocument(document).toJson(QJsonDocument::Compact);
                    const auto send = [this, socket, status, body] {
                        --active;
                        socket->write("HTTP/1.1 " + QByteArray::number(status) + " OK\r\nContent-Type: application/json\r\nConnection: close\r\nContent-Length: " + QByteArray::number(body.size()) + "\r\n\r\n" + body);
                        socket->disconnectFromHost();
                    };
                    if (delayed) QTimer::singleShot(1100, socket, send); else send();
                });
                connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            }
        });
    }
};
const QStringList fields{"name", "kind", "namespace", "status", "node", "image", "cluster", "owner", "issue", "cpu", "memory", "storage", "ready", "restarts", "createdAt", "uid"};
bool run(const QString& scenario, const QString& referencePresets) {
    QTemporaryDir temporary; KubernetesBoundary server;
    server.extraConfigurations = scenario.startsWith("limit_") ? 298 : 0;
    server.delayed = scenario == "loading_silent" || scenario == "loading_parallel";
    server.events = scenario.startsWith("table_keyboard_event_");
    server.valueCopies = scenario.startsWith("value_copy_");
    server.secretValues = scenario.startsWith("value_copy_secret_");
    if (!temporary.isValid() || !server.listen(QHostAddress::LocalHost, 0)) return false;
    const auto source = temporary.filePath("source.config"), profile = temporary.filePath("profile");
    QFile file(source); if (!file.open(QIODevice::WriteOnly)) return false;
    const auto yaml = "apiVersion: v1\nkind: Config\nclusters:\n- name: local\n  cluster: {server: 'http://127.0.0.1:" + QByteArray::number(server.serverPort())
        + "'}\nusers:\n- name: local\n  user: {token: local-test-token}\ncontexts:\n- name: local\n  context: {cluster: local, user: local}\n- name: other\n  context: {cluster: local, user: local}\n";
    if (file.write(yaml) != yaml.size()) return false; file.close();
    QString session;
    {
        podlord::Workspace workspace(profile);
        QQmlApplicationEngine engine;
        QQuickWindow* window = nullptr;
        if (!scenario.startsWith("contract_")) {
            engine.rootContext()->setContextProperty("workspace", &workspace); engine.load(QUrl("qrc:/podlord/Main.qml"));
            if (engine.rootObjects().isEmpty()) return false;
            window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
            if (!window) return false;
        }
        if (!waitFor([&] { return !workspace.busy(); })) return false;
        if (scenario.startsWith("contract_empty_")) {
            const QString operation = scenario.mid(QStringLiteral("contract_empty_").size());
            const bool accepted = operation == "filter" ? workspace.filterField("name", "alpha")
                : operation == "reset" ? workspace.resetResourceFilters()
                : operation == "mode" ? workspace.setFilterMode("problems")
                : operation == "picker" ? workspace.prepareFilterPicker("name")
                : operation == "load" ? workspace.loadFilterPreset("default")
                : operation == "save" ? workspace.saveFilterPreset("Empty session") : true;
            return !accepted && workspace.currentSession().isEmpty() && workspace.resourceCount() == 0 && server.requests == 0;
        }
        if (!workspace.importFile(source)
            || !waitFor([&] { return !workspace.busy() && workspace.contexts().size() == 2; })) return false;
        const auto context = workspace.contexts().first().toMap().value("id").toString();
        QSignalSpy sounds(workspace.alerts(), &podlord::Alerts::soundRequested), focus(workspace.alerts(), &podlord::Alerts::focusRequested);
        QList<double> progress;
        QObject::connect(&workspace, &podlord::Workspace::changed, &workspace, [&] {
            if (workspace.property("syncLoading").toBool()) progress.append(workspace.property("loadingProgress").toDouble());
        });
        if (!workspace.openContext(context) || !waitFor([&] { return !workspace.busy() && !workspace.loading()
            && (server.events ? workspace.totalResourceCount() >= 3 : workspace.totalResourceCount() == 3 + server.extraConfigurations); })) return false;
        session = workspace.currentSession();
        if (scenario == "selection" && (!workspace.inspectPath("/api/v1/namespaces/team-a/pods/alpha")
            || !waitFor([&] { return workspace.canEditYaml(); }))) return false;
        const int requests = server.requests;
        if (scenario.startsWith("limit_")) {
            auto* displayed = workspace.property("visibleResourceTable").value<QAbstractItemModel*>();
            if (!displayed || workspace.property("resourceLimit").toInt() != 256 || displayed->rowCount() != 256 || workspace.resourceCount() != 301) return false;
            QAbstractItemModelTester consistency(displayed, QAbstractItemModelTester::FailureReportingMode::Fatal);
            const auto setLimit = [&](const QString& text) {
                bool accepted = false;
                return QMetaObject::invokeMethod(&workspace, "setResourceLimit", Q_RETURN_ARG(bool, accepted), Q_ARG(QString, text)) && accepted;
            };
            if (scenario == "limit_default") return server.requests == requests;
            if (scenario.startsWith("limit_normalize_")) {
                const auto value = scenario.mid(QString("limit_normalize_").size());
                const QMap<QString, QString> inputs{{"empty", ""}, {"zero", "0"}, {"negative", "-1"}, {"invalid", "abc"},
                    {"overflow", "2147483648"}, {"upper", "2147483647"}, {"spaces", " 7 "}, {"one", "1"}, {"max", "5000"}};
                const int expected = value == "upper" || value == "max" ? 5000 : value == "spaces" ? 7 : value == "one" ? 1 : 256;
                return inputs.contains(value) && setLimit(inputs[value]) && workspace.property("resourceLimit").toInt() == expected
                    && displayed->rowCount() == std::min(expected, 301) && workspace.resourceCount() == 301 && server.requests == requests;
            }
            if (scenario == "limit_ui" || scenario == "limit_narrow") {
                if (scenario == "limit_narrow") {
                    window->resize(844, 390);
                    if (!waitFor([&] { return item(window, "landscapeNavigation")->isVisible() && !item(window, "sidebarFilterScroll")->isVisible(); })) return false;
                }
                auto* control = item(window, "resourceLimit");
                if (!control) return false;
                if (!control->isVisible()) {
                    const auto* expander = item(window, "toggleLandscapeFilters");
                    if (!click(window, expander && expander->isVisible() ? "toggleLandscapeFilters" : "toggleSidebar")) return false;
                }
                if (!waitFor([&] {
                    const auto* activity = item(window, "activityOnly");
                    const auto* firstFilter = item(window, "sidebarField_cluster");
                    if (!activity || !firstFilter || !control->isVisible()) return false;
                    const auto origin = control->mapToItem(window->contentItem(), QPointF{});
                    const auto activityOrigin = activity->mapToItem(window->contentItem(), QPointF{});
                    if (origin.y() >= firstFilter->mapToItem(window->contentItem(), QPointF{}).y()) return false;
                    return scenario == "limit_narrow" || (origin.x() >= activityOrigin.x() + activity->width()
                        && qAbs(origin.y() + control->height()/2 - activityOrigin.y() - activity->height()/2) < 8);
                }) || !podlord::test::scrollIntoView(window, control)) return false;
                control->forceActiveFocus(); QTest::keyClick(window, Qt::Key_Up);
                return waitFor([&] { return workspace.property("resourceLimit").toInt() == 257 && displayed->rowCount() == 257
                    && item(window, "resourceTable")->property("rows").toInt() == 257; }) && server.requests == requests;
            }
            if (!setLimit("7") || displayed->rowCount() != 7 || workspace.resourceCount() != 301) return false;
            if (scenario == "limit_sort") {
                if (!workspace.sortColumn(0) || !workspace.sortColumn(0) || workspace.sortDirection() != "DESC") return false;
                for (int row = 0; row < 7; ++row)
                    if (displayed->data(displayed->index(row, 0), Qt::UserRole) != workspace.table()->data(workspace.table()->index(row, 0), Qt::UserRole)) return false;
                return displayed->data(displayed->index(0, 0)).toString() == "team-a" && server.requests == requests;
            }
            if (scenario == "limit_filter") return workspace.filterField("name", "\"limit-297\"") && displayed->rowCount() == 1 && workspace.resourceCount() == 1
                && displayed->data(displayed->index(0, 0)).toString() == "limit-297" && server.requests == requests;
            if (scenario == "limit_picker") return workspace.prepareFilterPicker("name") && workspace.filterPickerValues().contains("limit-297") && server.requests == requests;
            if (scenario == "limit_reset") return workspace.filterField("name", "limit-") && workspace.resetResourceFilters()
                && workspace.property("resourceLimit").toInt() == 256 && displayed->rowCount() == 256 && workspace.resourceCount() == 301 && server.requests == requests;
            if (scenario == "limit_preset") {
                if (!workspace.saveFilterPreset("Seven rows") || !waitFor([&] { return !workspace.filterPresetsBusy(); })
                    || !workspace.resetResourceFilters() || workspace.selectedFilterPreset() != "default" || !workspace.loadFilterPreset("Seven rows")) return false;
                return workspace.selectedFilterPreset() == "Seven rows" && workspace.property("resourceLimit").toInt() == 7 && displayed->rowCount() == 7 && server.requests == requests;
            }
            if (scenario == "limit_session") {
                const auto other = workspace.contexts().last().toMap().value("id").toString();
                if (!workspace.openContext(other) || !waitFor([&] { return !workspace.busy() && !workspace.loading() && workspace.totalResourceCount() == 301; })
                    || workspace.property("resourceLimit").toInt() != 256) return false;
                const int afterOpen = server.requests;
                return workspace.activate(session) && waitFor([&] { return !workspace.busy(); })
                    && workspace.property("resourceLimit").toInt() == 7 && displayed->rowCount() == 7 && server.requests == afterOpen;
            }
            if (scenario == "limit_refresh") {
                server.extraConfigurations = 5;
                if (!workspace.refresh() || !waitFor([&] { return !workspace.loading() && workspace.totalResourceCount() == 8; })) return false;
                return displayed->rowCount() == 7 && workspace.resourceCount() == 8 && workspace.table()->rowCount() == 8 && server.requests > requests;
            }
            if (scenario == "limit_radar") {
                auto* radar = window->findChild<podlord::RadarIsland*>();
                return radar && radar->source() == workspace.table() && radar->source()->rowCount() == 301 && server.requests == requests;
            }
            if (scenario == "limit_restart") {
                if (!waitFor([&] { return workspace.requestWindowClose(); })) return false;
                podlord::Workspace restored(profile);
                return waitFor([&] { return !restored.busy() && !restored.loading() && restored.currentSession() == session && restored.resourceCount() == 301; })
                    && restored.property("resourceLimit").toInt() == 7 && restored.property("visibleResourceTable").value<QAbstractItemModel*>()->rowCount() == 7;
            }
            return false;
        }
        if (scenario.startsWith("reference_presets_")) {
            QFile original(referencePresets);
            if (!original.open(QIODevice::ReadOnly)) return false;
            const auto exported = original.readAll();
            original.close();
            if (!waitFor([&] { return !workspace.filterPresetsBusy(); })) return false;
            if (scenario == "reference_presets_conflict") {
                if (!workspace.filterField("name", "bravo") || !type(window, "filterPresetName", "Pod alpha")
                    || !click(window, "saveFilterPreset") || !waitFor([&] { return !workspace.filterPresetsBusy(); })) return false;
            }
            const auto beforeImport = workspace.filterPresets();
            if (!workspace.importFilterPresets(QUrl::fromLocalFile(referencePresets))
                || !waitFor([&] { return !workspace.filterPresetsBusy(); })) return false;
            if (!original.open(QIODevice::ReadOnly) || original.readAll() != exported) return false;
            if (scenario == "reference_presets_conflict")
                return workspace.filterPresetsError().contains("already in use") && workspace.filterPresets() == beforeImport
                    && workspace.resourceCount() == 1 && workspace.resourceFieldFilters().value("name") == "bravo" && server.requests == requests;
            if (!workspace.filterPresetsError().isEmpty() || workspace.filterPresets().size() != 5) return false;
            if (scenario == "reference_presets_repeat") {
                const auto imported = workspace.filterPresets();
                if (!workspace.importFilterPresets(QUrl::fromLocalFile(referencePresets))
                    || !waitFor([&] { return !workspace.filterPresetsBusy(); }) || !workspace.filterPresetsError().isEmpty()
                    || workspace.filterPresets() != imported) return false;
            }
            const QString name = scenario == "reference_presets_problems" ? "Problem pods"
                : scenario == "reference_presets_activity" ? "Recently active"
                : scenario == "reference_presets_missing_metrics" ? "Measured workloads" : "Pod alpha";
            const int selected = workspace.filterPresets().indexOf(name);
            if (selected < 0 || !click(window, "filterPreset")) return false;
            QTest::keyClick(window, Qt::Key_Home);
            for (int index = 0; index < selected; ++index) QTest::keyClick(window, Qt::Key_Down);
            QTest::keyClick(window, Qt::Key_Return);
            const int expected = scenario == "reference_presets_activity" ? 2 : scenario == "reference_presets_missing_metrics" ? 0 : 1;
            if (!waitFor([&] { return workspace.resourceCount() == expected && workspace.selectedFilterPreset() == name; })
                || !workspace.filterError().isEmpty() || server.requests != requests) return false;
            if (name == "Pod alpha") {
                const QVariantMap fields{{"name", "alpha"}, {"namespace", "team-a"}, {"kind", "Pod"}, {"cluster", "local"},
                    {"status", "Running"}, {"createdAt", ">=1m <2m"}, {"node", "node-a"}, {"image", "busybox:1"},
                    {"ready", "1/1"}, {"restarts", "=0"}, {"owner", "owner-a"}};
                if (workspace.resourceFieldFilters() != fields || workspace.filterText() != "Pod" || workspace.resourceLimit() != 7) return false;
            }
            const auto screenshot = qEnvironmentVariable("PODLORD_FIELD_FILTER_SCREENSHOT");
            if (!screenshot.isEmpty() && !window->grabWindow().save(screenshot)) return false;
            if (scenario != "reference_presets_restart") return true;
            const auto expectedFields = workspace.resourceFieldFilters();
            const auto expectedPresets = workspace.filterPresets();
            if (!waitFor([&] { return workspace.requestWindowClose(); })) return false;
            podlord::Workspace restored(profile);
            return waitFor([&] { return !restored.busy() && !restored.loading() && !restored.filterPresetsBusy()
                && restored.currentSession() == session && restored.resourceCount() == 1; })
                && restored.selectedFilterPreset() == name && restored.resourceFieldFilters() == expectedFields
                && restored.filterPresets() == expectedPresets && restored.resourceLimit() == 7 && restored.filterError().isEmpty();
        }
        if (scenario == "filter_render_frame") {
            window->requestActivate();
            if (!QTest::qWaitForWindowExposed(window, 5000)) {
                std::fprintf(stderr,"Filter rendering requires an exposed window; platform=%s.\n",qPrintable(QGuiApplication::platformName()));
                return false;
            }
            if (!click(window,"workspaceSearchButton") || !waitFor([&] { return item(window,"resourceFilter")->isVisible(); })) return false;
            const auto* model=workspace.table();
            int nameColumn=-1;
            for (int column=0; column<model->columnCount(); ++column)
                if (model->headerData(column,Qt::Horizontal,Qt::UserRole)=="name") nameColumn=column;
            if (nameColumn<0 || !waitFor([&] { return item(window,"cell_0_"+QString::number(nameColumn))!=nullptr; })) return false;
            auto* cell=item(window,"cell_0_"+QString::number(nameColumn));
            const auto before=window->grabWindow();
            if (before.isNull() || model->data(model->index(0,nameColumn)).toString()=="bravo") return false;
            const auto scale=before.width()/double(window->width());
            const auto origin=cell->mapToScene({0,0});
            const QRect pixels(qRound(origin.x()*scale),qRound(origin.y()*scale),qRound(cell->width()*scale),qRound(cell->height()*scale));
            QSignalSpy frames(window,&QQuickWindow::frameSwapped);
            if (!type(window,"resourceFilter","bravo")
                || !waitFor([&] { return workspace.resourceCount()==1 && frames.count()>0; })) {
                std::fputs("A cache filter changed without producing a visible frame.\n",stderr); return false;
            }
            const auto after=window->grabWindow();
            const auto capture=qEnvironmentVariable("PODLORD_FIELD_FILTER_SCREENSHOT");
            return model->data(model->index(0,nameColumn)).toString()=="bravo" && server.requests==requests
                && !after.isNull() && before.copy(pixels)!=after.copy(pixels)
                && (capture.isEmpty() || after.save(capture));
        }
        if (scenario.startsWith("flyout_")) {
            if (scenario == "flyout_narrow") {
                window->resize(640, 650);
                if (!click(window, "toggleSidebar")) return false;
            }
            if (!click(window, "sidebarField_name")) return false;
            auto* button = item(window, "sidebarField_name");
            auto* popup = window->findChild<QObject*>("fieldFiltersPopup");
            if (!popup || !waitFor([&] { return popup->property("visible").toBool(); })
                || popup->property("modal").toBool() != (scenario == "flyout_narrow")) return false;
            bool passed = false;
            if (scenario == "flyout_open") {
                const auto expected = std::max(12.0, std::min(window->width() - popup->property("width").toReal() - 12,
                    button->mapToScene(QPointF()).x()));
                passed = qAbs(popup->property("x").toReal() - expected) < 2 && workspace.resourceCount() == 3;
            } else if (scenario == "flyout_edit") {
                passed = type(window, "fieldFilterExpression", "alpha") && waitFor([&] { return workspace.resourceCount() == 1; })
                    && popup->property("visible").toBool() && !popup->property("modal").toBool();
            } else if (scenario == "flyout_escape") {
                QTest::keyClick(window, Qt::Key_Escape);
                passed = waitFor([&] { return !popup->property("visible").toBool(); });
            } else if (scenario == "flyout_navigation") {
                passed = click(window, "eventsWorkspaceButton") && waitFor([&] {
                    return workspace.property("workspacePage").toString() == "events" && !popup->property("visible").toBool(); });
            } else if (scenario == "flyout_keyboard") {
                auto* value = item(window, "fieldFilterValue_0");
                if (!value) return false;
                value->forceActiveFocus(); QTest::keyClick(window, Qt::Key_Space);
                passed = waitFor([&] { return workspace.resourceCount() == 1; }) && value->property("checked").toBool();
            } else if (scenario == "flyout_anchor_scroll") {
                if (!waitFor([&] { return popup->property("opened").toBool(); })) return false;
                auto* scroll = item(window,"sidebarFilterScroll");
                if (!scroll) return false;
                QSignalSpy frames(window,&QQuickWindow::frameSwapped); window->update();
                if (!frames.wait(1000)) return false;
                const auto point = scroll->mapToScene({scroll->width()/2,10});
                QTest::mouseMove(window,point.toPoint());
                QWheelEvent wheel(point,window->mapToGlobal(point.toPoint()),{},QPoint(0,-120),Qt::NoButton,Qt::NoModifier,Qt::NoScrollPhase,false);
                QCoreApplication::sendEvent(window,&wheel);
                passed = waitFor([&] { return !popup->property("visible").toBool(); });
                if (!passed) std::fprintf(stderr,"Filter viewport wheel at (%g,%g), accepted=%d; viewport y=%g height=%g; popup y=%g height=%g\n",point.x(),point.y(),wheel.isAccepted(),scroll->mapToScene({0,0}).y(),scroll->height(),popup->property("y").toReal(),popup->property("height").toReal());
            } else if (scenario == "flyout_edge" || scenario == "flyout_narrow") {
                if (scenario == "flyout_edge") {
                    QTest::keyClick(window, Qt::Key_Escape);
                    auto* list = item(window, "sidebarFilters");
                    if (!list || !QMetaObject::invokeMethod(list, "positionViewAtEnd")
                        || !waitFor([&] { return item(window, "sidebarField_uid") != nullptr; })
                        || !click(window, "sidebarField_uid")) return false;
                }
                passed = popup->property("x").toReal() >= 0 && popup->property("y").toReal() >= 0
                    && popup->property("x").toReal() + popup->property("width").toReal() <= window->width()
                    && popup->property("y").toReal() + popup->property("height").toReal() <= window->height();
            } else if (scenario == "flyout_session") {
                if (!type(window, "fieldFilterExpression", "alpha") || workspace.resourceCount() != 1) return false;
                const auto other = workspace.contexts().last().toMap().value("id").toString();
                if (!workspace.openContext(other) || !waitFor([&] { return !workspace.busy() && !workspace.loading() && workspace.resourceCount() == 3; })
                    || popup->property("visible").toBool()) return false;
                const int beforeSwitch = server.requests;
                return workspace.activate(session) && waitFor([&] { return !workspace.busy() && workspace.resourceCount() == 1; })
                    && !popup->property("visible").toBool() && server.requests == beforeSwitch;
            }
            const auto screenshot = qEnvironmentVariable("PODLORD_FIELD_FILTER_SCREENSHOT");
            return passed && server.requests == requests && (screenshot.isEmpty() || window->grabWindow().save(screenshot));
        }
        if (scenario.startsWith("contract_")) {
            const auto unchanged = [&] { return server.requests == requests && workspace.totalResourceCount() == 3; };
            if (scenario == "contract_cluster_rebind" || scenario == "contract_metadata_activity_update" || scenario == "contract_cluster_filter_rebind") {
                const auto* proxy = qobject_cast<QAbstractProxyModel*>(workspace.table());
                const auto* source = proxy ? qobject_cast<podlord::ResourceTable*>(proxy->sourceModel()) : nullptr;
                if (!source) return false;
                QJsonArray snapshot;
                for (int row = 0; row < source->rowCount(); ++row) {
                    auto value = source->row(row); value.remove("cluster");
                    if (scenario == "contract_metadata_activity_update") value["activity"] = false;
                    snapshot.append(value);
                }
                podlord::ResourceTable model(nullptr, {"name", "cluster", "status"}, {"Name", "Cluster", "Status"}, "path");
                if (!model.publish(snapshot, "first")) return false;
                if (scenario == "contract_cluster_filter_rebind") {
                    podlord::ResourceFilter filtered;
                    filtered.setSourceModel(&model);
                    if (!filtered.filter("", {{"cluster", "first"}}) || filtered.rowCount() != snapshot.size()) return false;
                    return model.publish(snapshot, "second") && filtered.rowCount() == 0 && unchanged();
                }
                if (scenario == "contract_metadata_activity_update") {
                    podlord::ResourceFilter filtered;
                    filtered.setSourceModel(&model);
                    if (!filtered.filter("", {}, "activity") || filtered.rowCount() != 0) return false;
                    auto changed = snapshot.first().toObject(); changed["activity"] = true; snapshot[0] = changed;
                    return model.publish(snapshot, "first") && filtered.rowCount() == 1 && unchanged();
                }
                QSignalSpy changes(&model, &QAbstractItemModel::dataChanged);
                return model.publish(snapshot, "second") && changes.count() == 1
                    && qvariant_cast<QModelIndex>(changes.front()[0]).column() == 1
                    && qvariant_cast<QModelIndex>(changes.front()[1]).column() == 1
                    && model.data(model.index(0, 1)).toString() == "second"
                    && model.data(model.index(0, 0)).toString() == source->row(0)["name"].toString() && unchanged();
            }
            if (scenario == "contract_unknown_field") return !workspace.filterField("unknown", "alpha") && workspace.resourceCount() == 3 && unchanged();
            if (scenario == "contract_unknown_picker") return !workspace.prepareFilterPicker("unknown") && workspace.resourceCount() == 3 && unchanged();
            if (scenario == "contract_invalid_mode") return !workspace.setFilterMode("unknown") && !workspace.property("problemsOnly").toBool() && unchanged();
            if (scenario == "contract_reset_repeat") return workspace.resetResourceFilters() && workspace.resetResourceFilters() && workspace.resourceCount() == 3 && unchanged();
            if (scenario == "contract_mode_repeat") return workspace.setFilterMode("problems") && workspace.setFilterMode("problems") && workspace.resourceCount() == 1 && unchanged();
            if (scenario == "contract_filter_repeat") return workspace.filterField("name", "alpha") && workspace.filterField("name", "alpha") && workspace.resourceCount() == 1 && unchanged();
            if (scenario == "contract_filter_clear") return workspace.filterField("name", "alpha") && workspace.filterField("name", "") && workspace.resourceFieldFilters().isEmpty() && workspace.resourceCount() == 3 && unchanged();
            if (scenario.startsWith("contract_picker_")) {
                if (!workspace.prepareFilterPicker("name")) return false;
                const auto values = workspace.property("filterPickerValues").toStringList();
                if (values != QStringList{"alpha", "bravo", "team-a"}) return false;
                if (scenario == "contract_picker_missing_select") return !workspace.selectFilterValue("missing", true) && workspace.resourceCount() == 3 && unchanged();
                if (scenario == "contract_picker_missing_selected") return !workspace.filterValueSelected("missing") && unchanged();
                if (scenario == "contract_picker_missing_copy") {
                    QGuiApplication::clipboard()->setText("retained clipboard");
                    return !workspace.copyFilterValue("missing") && QGuiApplication::clipboard()->text() == "retained clipboard" && unchanged();
                }
                if (scenario == "contract_picker_copy") return workspace.copyFilterValue("alpha") && QGuiApplication::clipboard()->text() == "alpha" && unchanged();
                if (scenario == "contract_picker_repeat") return workspace.selectFilterValue("alpha", true) && workspace.selectFilterValue("alpha", true) && workspace.filterValueSelected("alpha") && workspace.resourceCount() == 1 && unchanged();
                if (scenario == "contract_picker_deselect") return workspace.selectFilterValue("alpha", true) && workspace.selectFilterValue("alpha", false) && !workspace.filterValueSelected("alpha") && workspace.resourceCount() == 3 && unchanged();
                if (scenario == "contract_picker_case") return workspace.filterField("name", "\"ALPHA\"") && workspace.filterValueSelected("alpha") && workspace.resourceCount() == 1 && unchanged();
                if (scenario == "contract_picker_custom_value") return workspace.filterField("name", "\"missing\"") && workspace.prepareFilterPicker("name") && workspace.property("filterPickerValues").toStringList().contains("missing") && workspace.filterValueSelected("missing") && workspace.resourceCount() == 0 && unchanged();
                if (scenario == "contract_picker_stale_session") {
                    if (!workspace.openContext(workspace.contexts().last().toMap()["id"].toString()) || !waitFor([&] { return !workspace.busy() && !workspace.loading(); })) return false;
                    QGuiApplication::clipboard()->setText("retained clipboard");
                    const int settled = server.requests;
                    return !workspace.filterValueSelected("alpha") && !workspace.selectFilterValue("alpha", true) && !workspace.copyFilterValue("alpha")
                        && QGuiApplication::clipboard()->text() == "retained clipboard" && server.requests == settled;
                }
                return false;
            }
            if (scenario.startsWith("contract_preset_")) {
                if (!waitFor([&] { return !workspace.property("filterPresetsBusy").toBool(); })) return false;
                if (scenario == "contract_preset_missing_load") return !workspace.loadFilterPreset("missing") && workspace.resourceCount() == 3 && unchanged();
                if (scenario == "contract_preset_invalid_filter") return workspace.filterField("name", "/[/") && !workspace.filterError().isEmpty()
                    && !workspace.saveFilterPreset("Invalid filter") && !workspace.property("filterPresetsError").toString().isEmpty() && unchanged();
                if (!workspace.filterField("name", "alpha") || !workspace.saveFilterPreset("Test preset")
                    || !waitFor([&] { return !workspace.property("filterPresetsBusy").toBool(); }) || !workspace.property("filterPresetsError").toString().isEmpty()) return false;
                const auto names = workspace.filterPresets();
                bool accepted = true;
                if (scenario == "contract_preset_case_collision") accepted = workspace.saveFilterPreset("TEST PRESET");
                else if (scenario == "contract_preset_reserved") accepted = workspace.saveFilterPreset("DEFAULT");
                else if (scenario == "contract_preset_empty") accepted = workspace.saveFilterPreset(" \t");
                else if (scenario == "contract_preset_long") accepted = workspace.saveFilterPreset(QString(129, 'x'));
                else if (scenario == "contract_preset_rename_missing") accepted = workspace.renameFilterPreset("missing", "Next");
                else if (scenario == "contract_preset_rename_reserved") accepted = workspace.renameFilterPreset("default", "Next");
                else if (scenario == "contract_preset_rename_empty") accepted = workspace.renameFilterPreset("Test preset", "");
                else if (scenario == "contract_preset_rename_long") accepted = workspace.renameFilterPreset("Test preset", QString(129, 'x'));
                else if (scenario == "contract_preset_rename_default") accepted = workspace.renameFilterPreset("Test preset", "DEFAULT");
                else if (scenario == "contract_preset_delete_missing") accepted = workspace.deleteFilterPreset("missing");
                else if (scenario == "contract_preset_delete_default") accepted = workspace.deleteFilterPreset("default");
                else if (scenario == "contract_preset_rename_repeat") return workspace.renameFilterPreset("Test preset", "Test preset") && workspace.filterPresets() == names && unchanged();
                else if (scenario == "contract_preset_rename_collision") {
                    if (!workspace.saveFilterPreset("Other") || !waitFor([&] { return !workspace.property("filterPresetsBusy").toBool(); })) return false;
                    return !workspace.renameFilterPreset("Test preset", "OTHER") && workspace.filterPresets().contains("Test preset") && workspace.filterPresets().contains("Other") && unchanged();
                } else if (scenario == "contract_preset_busy_save" || scenario == "contract_preset_busy_reload" || scenario == "contract_preset_busy_rename" || scenario == "contract_preset_busy_delete") {
                    if (!workspace.saveFilterPreset("First save")) return false;
                    accepted = scenario == "contract_preset_busy_save" ? workspace.saveFilterPreset("Second save")
                        : scenario == "contract_preset_busy_reload" ? workspace.reloadFilterPresets()
                        : scenario == "contract_preset_busy_rename" ? workspace.renameFilterPreset("Test preset", "Renamed") : workspace.deleteFilterPreset("Test preset");
                    if (accepted || !waitFor([&] { return !workspace.property("filterPresetsBusy").toBool(); })) return false;
                    return workspace.filterPresets().contains("First save") && workspace.filterPresets().contains("Test preset")
                        && !workspace.filterPresets().contains("Second save") && !workspace.filterPresets().contains("Renamed") && unchanged();
                } else return false;
                return !accepted && workspace.filterPresets() == names && workspace.resourceCount() == 1 && unchanged();
            }
            return false;
        }
        if (scenario.startsWith("value_copy_")) {
            const auto path = server.secretValues ? QStringLiteral("/api/v1/namespaces/team-b/secrets/team-a") : QStringLiteral("/api/v1/namespaces/team-b/configmaps/team-a");
            if (!workspace.inspectPath(path) || !waitFor([&] { return workspace.canEditYaml() && workspace.resourceValues().size() >= 3; })
                || !click(window, "valuesButton")) return false;
            const int calls = server.requests;
            const auto key = scenario.contains("_binary_") ? QStringLiteral("binary") : scenario.contains("_empty_") ? QStringLiteral("empty") : QStringLiteral("text");
            const auto id = (server.secretValues ? QStringLiteral("data/") : QStringLiteral("binaryData/")) + key;
            if (!waitFor([&] { return item(window, "value_" + id) && item(window, "value_" + id)->isVisible(); })) return false;
            const auto masked = [&] { return !server.secretValues || (item(window, "value_" + id)
                && item(window, "value_" + id)->property("text").toString() == "[hidden]"); };
            if (!masked()) return false;
            QGuiApplication::clipboard()->setText("previous clipboard");
            if (scenario.endsWith("_invalid_format") || scenario.endsWith("_missing_key")) {
                const bool copied = scenario.endsWith("_invalid_format") ? workspace.copyValue(id, "unknown")
                    : workspace.copyValue("data/missing", "raw");
                return !copied && QGuiApplication::clipboard()->text() == "previous clipboard" && masked() && server.requests == calls;
            }
            if (scenario.endsWith("_scope_reset")) {
                if (!click(window, "closeInspector")) return false;
                return !workspace.copyValue(id, "raw") && QGuiApplication::clipboard()->text() == "previous clipboard" && server.requests == calls;
            }
            if (scenario.endsWith("_repeat")) {
                if (!workspace.copyValue(id, "decoded") || QGuiApplication::clipboard()->text() != "decoded-value"
                    || !workspace.copyValue(id, "raw") || QGuiApplication::clipboard()->text() != "ZGVjb2RlZC12YWx1ZQ==") return false;
                return workspace.copyValue(id) && QGuiApplication::clipboard()->text() == "decoded-value" && masked() && server.requests == calls;
            }
            const bool preferred = scenario.endsWith("_preferred");
            if (preferred) {
                if (!click(window, "copy_" + id)) return false;
            } else {
                if (scenario.contains("_keyboard_")) {
                    auto* decoded = item(window, "copyDecoded_" + id);
                    if (!decoded || !podlord::test::scrollIntoView(window, decoded)) return false;
                    decoded->forceActiveFocus(Qt::TabFocusReason);
                    QTest::keyClick(window, Qt::Key_Space);
                } else {
                    const auto action = scenario.endsWith("_key") ? QStringLiteral("copyKey_")
                        : scenario.endsWith("_raw") ? QStringLiteral("copyRaw_") : QStringLiteral("copyDecoded_");
                    if (!click(window, action + id)) return false;
                }
            }
            const bool binaryFailure = key == "binary" && !preferred;
            const auto expected = binaryFailure ? QStringLiteral("previous clipboard") : scenario.endsWith("_key") ? key
                : key == "empty" ? QString{} : scenario.endsWith("_raw") || key == "binary" ? (key == "binary" ? QStringLiteral("/wA=") : QStringLiteral("ZGVjb2RlZC12YWx1ZQ=="))
                : QStringLiteral("decoded-value");
            if (!waitFor([&] { return QGuiApplication::clipboard()->text() == expected && masked(); }) || server.requests != calls) return false;
            if (!binaryFailure) return true;
            return waitFor([&] { auto* error = item(window, "copyValueError");
                return error && error->isVisible() && !error->property("text").toString().isEmpty(); });
        }
        if (scenario.startsWith("table_keyboard_")) {
            const bool eventTable = server.events;
            const bool pinned = scenario.startsWith("table_keyboard_pinned_");
            if (eventTable && !workspace.setWorkspacePage("events")) return false;
            if (pinned && (!click(window, "resourceColumnsButton") || !click(window, "resourceColumnPinned_name")
                || !click(window, "resourceSaveColumns") || !waitFor([&] { return !workspace.property("tableLayoutSaving").toBool(); }))) return false;
            QSignalSpy frames(window, &QQuickWindow::frameSwapped); window->update();
            if (!frames.wait(1000)) return false;
            auto* cell = item(window, eventTable ? "eventCell_0_0" : pinned ? "pinnedCell_0_0" : "cell_0_0");
            if (!cell || !cell->isVisible() || !cell->isEnabled()) return false;
            const auto path = cell->property("resourcePath").toString();
            const auto value = cell->property("text").toString();
            cell->forceActiveFocus(Qt::TabFocusReason);
            if (!waitFor([&] { return cell->hasActiveFocus(); })) return false;
            if (scenario.endsWith("_f10")) QTest::keyClick(window, Qt::Key_F10, Qt::ShiftModifier);
            else QTest::keyClick(window, Qt::Key_Menu);
            const bool copy = scenario.contains("_copy_");
            const auto action = eventTable ? (copy ? QStringLiteral("eventMenuCopy") : QStringLiteral("eventMenuInspector"))
                : copy ? QStringLiteral("menuCopy") : QStringLiteral("menuInspector");
            if (!waitFor([&] { const auto* control = item(window, action); return control && control->isVisible(); })) {
                std::fprintf(stderr, "Keyboard context action missing: %s; cell focus=%d, window active=%d\n", qPrintable(action), cell->hasActiveFocus(), window->isActive());
                return false;
            }
            if (copy) QGuiApplication::clipboard()->setText("previous clipboard");
            if (!click(window, action)) return false;
            return copy ? QGuiApplication::clipboard()->text() == value && server.requests == requests
                : waitFor([&] { return workspace.inspectorPath() == (eventTable ? QStringLiteral("/api/v1/namespaces/team-a/pods/alpha") : path); });
        }
        if (scenario == "typography") {
            const auto* cell = item(window, "cell_0_0");
            const auto* header = item(window, "header_0");
            if (!cell || !header || !cell->isVisible() || !header->isVisible()) return false;
            const auto bodyFont = cell->property("font").value<QFont>();
            const auto headerFont = header->property("font").value<QFont>();
            const auto workspaceFont = window->property("font").value<QFont>();
            const auto* navigation = item(window, "resourcesWorkspaceButton");
            if (bodyFont.weight() != QFont::Normal || bodyFont.pixelSize() != 13 || !headerFont.bold()
                || workspaceFont.weight() != QFont::Normal || !navigation || !navigation->property("font").value<QFont>().bold()) {
                std::fprintf(stderr, "Typography: body weight=%d size=%d, header weight=%d, workspace weight=%d\n",
                    int(bodyFont.weight()), bodyFont.pixelSize(), int(headerFont.weight()), int(workspaceFont.weight()));
                return false;
            }
            const auto screenshot = qEnvironmentVariable("PODLORD_FIELD_FILTER_SCREENSHOT");
            return server.requests == requests && (screenshot.isEmpty() || window->grabWindow().save(screenshot));
        }
        if (scenario.startsWith("mode_") || scenario.startsWith("preset_") || scenario.startsWith("loading_")) {
            if (!item(window, "problemsOnly") || !item(window, "activityOnly")) return false;
            if (scenario == "loading_parallel") return server.maximumActive >= 2 && server.maximumActive <= 4;
            if (scenario == "loading_silent") {
                if (!waitFor([&] { return !workspace.alerts()->matches().isEmpty(); }) || !sounds.isEmpty() || !focus.isEmpty()
                    || progress.isEmpty() || workspace.property("loadingProgress").toDouble() != 1) return false;
                bool partial = false; double previous = 0;
                for (const double value : progress) { if (value < previous || value >= 1) return false; partial = partial || value > 0; previous = value; }
                const auto effect = workspace.alerts()->effect("/api/v1/namespaces/team-a/pods/alpha");
                return partial && effect.value("color").toString() != "fresh" && !effect.contains("animation");
            }
            if (!click(window, "problemsOnly") || !waitFor([&] { return workspace.resourceCount() == 1; })) return false;
            if (scenario == "mode_problems") return workspace.property("problemsOnly").toBool() && server.requests == requests;
            if (scenario == "mode_activity") return click(window, "activityOnly") && waitFor([&] { return workspace.resourceCount() == 2; })
                && !workspace.property("problemsOnly").toBool() && workspace.property("activityOnly").toBool() && server.requests == requests;
            if (scenario == "mode_reset") return click(window, "resetResourceFilters") && workspace.resourceCount() == 3
                && !workspace.property("problemsOnly").toBool() && !workspace.property("activityOnly").toBool() && server.requests == requests;
            if (scenario == "mode_field") return workspace.filterField("name", "alpha") && workspace.resourceCount() == 0
                && workspace.totalResourceCount() == 3 && server.requests == requests;
            if (scenario == "mode_session") {
                if (!workspace.openContext(workspace.contexts().last().toMap()["id"].toString())
                    || !waitFor([&] { return !workspace.busy() && !workspace.loading() && workspace.totalResourceCount() == 3; })
                    || workspace.property("problemsOnly").toBool()) return false;
                return workspace.activate(session) && waitFor([&] { return !workspace.busy() && workspace.resourceCount() == 1; })
                    && workspace.property("problemsOnly").toBool();
            }
            if (!type(window, "filterPresetName", "Broken pods") || !click(window, "saveFilterPreset")
                || !waitFor([&] { return !workspace.property("filterPresetsBusy").toBool(); })
                || !workspace.property("filterPresetsError").toString().isEmpty()) return false;
            if (scenario == "preset_restart") {
                if (!waitFor([&] { return workspace.requestWindowClose(); })) return false;
                podlord::Workspace restored(profile);
                return waitFor([&] { return !restored.busy() && !restored.loading() && !restored.property("filterPresetsBusy").toBool()
                    && restored.totalResourceCount() == 3 && restored.resourceCount() == 1; })
                    && restored.property("selectedFilterPreset").toString() == "Broken pods" && restored.property("problemsOnly").toBool();
            }
            if (scenario == "preset_delete") return click(window, "filterPresetActions") && click(window, "deleteFilterPreset")
                && waitFor([&] { return !workspace.property("filterPresetsBusy").toBool() && workspace.property("filterPresets").toStringList() == QStringList{"default"}; })
                && workspace.resourceCount() == 1 && server.requests == requests;
            if (scenario == "preset_overwrite") {
                if (!click(window, "activityOnly") || !click(window, "saveFilterPreset")
                    || !waitFor([&] { return !workspace.property("filterPresetsBusy").toBool(); })) return false;
            }
            if (!click(window, "resetResourceFilters") || workspace.resourceCount() != 3 || !click(window, "filterPreset")) return false;
            const auto* preset = item(window, "filterPreset");
            const auto* popup = preset->property("popup").value<QObject*>();
            const auto textWidth = QFontMetricsF(preset->property("font").value<QFont>()).horizontalAdvance("Broken pods");
            if (!popup || !waitFor([&] { return popup->property("visible").toBool()
                && popup->property("width").toDouble() >= textWidth + 24; })) return false;
            QTest::keyClick(window, Qt::Key_Home); QTest::keyClick(window, Qt::Key_Down); QTest::keyClick(window, Qt::Key_Return);
            const auto expected = scenario == "preset_overwrite" ? 2 : 1;
            return waitFor([&] { return workspace.resourceCount() == expected; }) && server.requests == requests;
        }
        if (scenario == "narrow") { window->setWidth(680); window->setHeight(480); }
        if (!click(window, "resourceFieldFilters") || !waitFor([&] { return item(window, "fieldFilterExpression") && item(window, "fieldFilterExpression")->isVisible(); })) return false;
        auto select = [&](const QString& field) {
            auto* combo = item(window, "fieldFilterColumn"); if (!combo || !click(window, "fieldFilterColumn")) return false;
            QTest::keyClick(window, Qt::Key_Home);
            for (int index = 0; index < fields.indexOf(field); ++index) QTest::keyClick(window, Qt::Key_Down);
            QTest::keyClick(window, Qt::Key_Return);
            return waitFor([&] { return workspace.property("filterPickerField").toString() == field; });
        };
        auto expression = [&](const QString& field, const QString& value) { return select(field) && type(window, "fieldFilterExpression", value); };
        auto count = [&](int expected) { return waitFor([&] { return workspace.resourceCount() == expected; }) && workspace.filterError().isEmpty(); };
        bool passed = false;
        if (scenario.startsWith("age_") || scenario.startsWith("uid_")) {
            const bool age = scenario.startsWith("age_");
            const QString field = age ? "createdAt" : "uid";
            const QMap<QString, QString> values{{"age_range", ">=1m <2m"}, {"age_missing", "\"-\""}, {"age_invalid", ">1year"}, {"age_restart", ">=1h <3h"},
                {"age_preset", ">=1m <2m"}, {"age_picker", ""}, {"uid_exact", "\"uid-alpha\""}, {"uid_regex", "/^uid-bravo$/"}, {"uid_global", ""},
                {"uid_restart", "uid-alpha"}, {"uid_preset", "uid-alpha"}, {"uid_picker", ""}};
            if (!values.contains(scenario) || !expression(field, values.value(scenario))) return false;
            if (scenario == "age_invalid") return workspace.resourceCount() == 0 && !workspace.filterError().isEmpty() && server.requests == requests;
            if (scenario.endsWith("_picker")) passed = click(window, "fieldFilterValue_0") && count(1);
            else if (scenario == "uid_global") {
                QTest::keyClick(window, Qt::Key_Escape);
                passed = type(window, "resourceFilter", "uid-bravo") && count(1);
            } else passed = count(1);
            if (!passed || server.requests != requests) return false;
            if (scenario.endsWith("_preset")) {
                QTest::keyClick(window, Qt::Key_Escape);
                if (!type(window, "filterPresetName", "Time and identity") || !click(window, "saveFilterPreset")
                    || !waitFor([&] { return !workspace.property("filterPresetsBusy").toBool(); })
                    || !workspace.property("filterPresetsError").toString().isEmpty()) return false;
                return workspace.resetResourceFilters() && workspace.resourceCount() == 3
                    && workspace.loadFilterPreset("Time and identity") && count(1) && server.requests == requests;
            }
            if (!scenario.endsWith("_restart")) return true;
            QTest::keyClick(window, Qt::Key_Escape);
            if (!waitFor([&] { return workspace.requestWindowClose(); })) return false;
            podlord::Workspace restored(profile);
            return waitFor([&] { return !restored.busy() && !restored.loading() && restored.currentSession() == session && restored.resourceCount() == 1; })
                && restored.property("resourceFieldFilters").toMap().value(field) == values.value(scenario) && restored.filterError().isEmpty();
        } else if (fields.contains(scenario)) {
            const QMap<QString, QString> values{{"name", "alpha"}, {"kind", "Pod"}, {"namespace", "team-a"}, {"status", "Running"}, {"node", "node-a"}, {"image", "busybox:1"}, {"cluster", "local"}, {"owner", "owner-a"}, {"issue", "CrashLoopBackOff"}};
            const int expected = scenario == "kind" ? 2 : scenario == "cluster" ? 3 : 1;
            passed = expression(scenario, values.value(scenario)) && count(expected);
        } else if (scenario == "and") passed = expression("kind", "Pod") && expression("namespace", "team-b") && count(1);
        else if (scenario == "or") passed = expression("name", "alpha bravo") && count(2);
        else if (scenario == "exact") passed = expression("name", "\"ALPHA\"") && count(1);
        else if (scenario == "prefix") passed = expression("name", "~AL") && count(1);
        else if (scenario == "suffix") passed = expression("name", "HA~") && count(1);
        else if (scenario == "regex") passed = expression("name", "/^(alpha|bravo)$/") && count(2);
        else if (scenario == "empty") passed = expression("name", " ") && count(3);
        else if (scenario == "missing") passed = expression("node", "node-a") && expression("kind", "ConfigMap") && count(0);
        else if (scenario == "invalid") passed = expression("name", "/[/") && waitFor([&] { return workspace.resourceCount() == 0 && !workspace.filterError().isEmpty(); })
            && expression("name", "alpha") && count(1);
        else if (scenario == "unclosed") passed = expression("name", "\"alpha") && waitFor([&] { return workspace.resourceCount() == 0 && !workspace.filterError().isEmpty(); });
        else if (scenario == "invalid_picker") {
            if (!expression("name", "/[/") || !waitFor([&] { return !workspace.filterError().isEmpty(); })) return false;
            const auto before = item(window, "fieldFilterExpression")->property("text").toString();
            passed = click(window, "fieldFilterValue_0") && item(window, "fieldFilterExpression")->property("text").toString() == before
                && workspace.resourceCount() == 0 && !workspace.filterError().isEmpty();
        } else if (scenario == "clear_expression") passed = expression("name", "alpha") && count(1) && type(window, "fieldFilterExpression", "") && count(3);
        else if (scenario == "keyboard") {
            auto* value = item(window, "fieldFilterValue_0"); if (!value) return false;
            value->forceActiveFocus(); QTest::keyClick(window, Qt::Key_Space); passed = count(1);
        } else if (scenario == "copy") {
            auto* value = item(window, "fieldFilterValue_0"); if (!value) return false;
            QGuiApplication::clipboard()->setText("previous");
            value->forceActiveFocus(); QTest::keyClick(window, Qt::Key_F10, Qt::ShiftModifier);
            passed = waitFor([&] { return item(window, "copyFieldFilterValue_0") && item(window, "copyFieldFilterValue_0")->isVisible(); })
                && click(window, "copyFieldFilterValue_0") && QGuiApplication::clipboard()->text() == "alpha" && count(3);
        } else if (scenario == "narrow") {
            const auto* editor = item(window, "fieldFilterExpression"), *reset = item(window, "clearFieldFilters");
            const auto bounds = reset ? reset->mapRectToScene(QRectF(0, 0, reset->width(), reset->height())) : QRectF{};
            passed = editor && reset && bounds.top() >= 0 && bounds.bottom() <= window->height() && expression("name", "alpha") && count(1);
        } else if (scenario == "selection") passed = expression("name", "bravo") && count(1) && workspace.inspectorName() == "alpha"
            && workspace.inspectorPath() == "/api/v1/namespaces/team-a/pods/alpha";
        else if (scenario == "picker") passed = click(window, "fieldFilterValue_0") && count(1) && click(window, "fieldFilterValue_1") && count(2)
            && click(window, "fieldFilterValue_0") && count(1);
        else if (scenario == "custom_picker") passed = expression("name", "~al") && click(window, "fieldFilterValue_1") && count(2)
            && click(window, "fieldFilterValue_1") && count(1) && item(window, "fieldFilterExpression")->property("text").toString() == "~al";
        else if (scenario == "picker_search") passed = type(window, "fieldFilterOptionSearch", "BRAV") && waitFor([&] {
            const auto* first = item(window, "fieldFilterValue_0"), *second = item(window, "fieldFilterValue_1");
            return (!first || !first->isVisible()) && second && second->isVisible(); }) && click(window, "fieldFilterValue_1") && count(1);
        else if (scenario == "reset") {
            if (!expression("kind", "Pod") || !expression("name", "alpha") || !count(1)) return false;
            auto* reset = item(window, "clearFieldFilters");
            const auto* accessible = reset ? QAccessible::queryAccessibleInterface(reset) : nullptr;
            if (!reset || reset->width() > 44 || !accessible
                || accessible->text(QAccessible::Name) != "Reset all resource filters") return false;
            reset->forceActiveFocus(); QTest::keyClick(window, Qt::Key_Space);
            passed = count(3);
        }
        else if (scenario == "stable_picker" || scenario == "refresh_picker") {
            if (!click(window, "fieldFilterValue_0") || !count(1)) return false;
            const auto before = workspace.property("filterPickerValues").toStringList();
            server.changed = true;
            if (!workspace.refresh() || !waitFor([&] { return !workspace.loading() && server.requests > requests && workspace.resourceCount() == 0; })) return false;
            if (workspace.property("filterPickerValues").toStringList() != before) return false;
            if (scenario == "stable_picker") passed = count(0);
            else passed = click(window, "refreshFieldFilterValues") && waitFor([&] {
                const auto values = workspace.property("filterPickerValues").toStringList();
                return values.contains("alpha") && values.contains("charlie"); }) && count(0);
            return passed;
        } else if (scenario == "restart") passed = expression("namespace", "team-a") && count(1);
        else if (scenario == "session") {
            if (!expression("name", "alpha") || !count(1)) return false;
            QTest::keyClick(window, Qt::Key_Escape);
            const auto other = workspace.contexts().last().toMap().value("id").toString();
            if (!workspace.openContext(other) || !waitFor([&] { return !workspace.busy() && !workspace.loading() && workspace.resourceCount() == 3; })) return false;
            if (!workspace.activate(session) || !waitFor([&] { return !workspace.busy() && workspace.resourceCount() == 1; })) return false;
            return workspace.property("resourceFieldFilters").toMap().value("name").toString() == "alpha";
        } else if (scenario == "radar") {
            if (!expression("namespace", "team-a") || !count(1)) return false;
            QTest::keyClick(window, Qt::Key_Escape);
            passed = click(window, "radarWorkspaceButton") && workspace.resourceCount() == 1 && workspace.totalResourceCount() == 3;
        } else if (scenario == "global_and") {
            if (!expression("kind", "Pod")) return false;
            QTest::keyClick(window, Qt::Key_Escape);
            passed = type(window, "resourceFilter", "bravo") && count(1);
        }
        if (!passed || server.requests != requests) return false;
        const auto screenshot = qEnvironmentVariable("PODLORD_FIELD_FILTER_SCREENSHOT");
        if (!screenshot.isEmpty() && !window->grabWindow().save(screenshot)) return false;
        if (scenario != "restart") return true;
    }
    podlord::Workspace restored(profile);
    return waitFor([&] { return !restored.busy() && !restored.loading() && restored.currentSession() == session && restored.resourceCount() == 1; })
        && restored.property("resourceFieldFilters").toMap().value("namespace").toString() == "team-a" && restored.filterError().isEmpty();
}
}
int main(int argc, char** argv) {
    QGuiApplication application(argc, argv); if (argc != 2 && argc != 3) return 2;
    const auto scenario = QString::fromLocal8Bit(argv[1]);
    if (scenario.startsWith("reference_presets_") != (argc == 3)) return 2;
    const bool passed = run(scenario, argc == 3 ? QString::fromLocal8Bit(argv[2]) : QString{});
    if (!passed) std::fprintf(stderr, "Field filter UI scenario failed: %s\n", argv[1]);
    return passed ? 0 : 1;
}
