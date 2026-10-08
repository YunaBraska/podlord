#include "workspace.h"
#include "ui_input.h"
#include <QClipboard>
#include <QElapsedTimer>
#include <QFile>
#include <QGuiApplication>
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
bool waitFor(const std::function<bool()>& ready, int timeout = 7000) {
    QElapsedTimer timer; timer.start();
    while (!ready() && timer.elapsed() < timeout) QTest::qWait(10);
    return ready();
}
// Only the external Kubernetes HTTP and Metrics APIs are simulated.
class MetricsBoundary final : public QTcpServer {
public:
    int requests = 0;
    const QDateTime at = QDateTime::currentDateTimeUtc();
    explicit MetricsBoundary(const QString& scenario) {
        connect(this, &QTcpServer::newConnection, this, [this, scenario] {
            while (hasPendingConnections()) {
                auto* socket = nextPendingConnection();
                auto input = std::make_shared<QByteArray>();
                connect(socket, &QTcpSocket::readyRead, this, [this, socket, input, scenario] {
                    input->append(socket->readAll());
                    if (!input->contains("\r\n\r\n")) return;
                    socket->disconnect(this); ++requests;
                    const auto path = QUrl::fromEncoded(input->split(' ')[1]).path();
                    QJsonObject document;
                    int status = 200;
                    if (path == "/api") document = {{"versions", QJsonArray{"v1"}}};
                    else if (path == "/apis") document = {{"groups", QJsonArray{QJsonObject{{"preferredVersion", QJsonObject{{"groupVersion", "metrics.k8s.io/v1beta1"}}}}}}};
                    else if (path == "/api/v1") document = {{"resources", QJsonArray{
                        QJsonObject{{"name", "pods"}, {"kind", "Pod"}, {"namespaced", true}, {"verbs", QJsonArray{"get", "list"}}},
                        QJsonObject{{"name", "persistentvolumeclaims"}, {"kind", "PersistentVolumeClaim"}, {"namespaced", true}, {"verbs", QJsonArray{"get", "list"}}}}}};
                    else if (path == "/apis/metrics.k8s.io/v1beta1") document = {{"resources", QJsonArray{
                        QJsonObject{{"name", "pods"}, {"kind", "PodMetrics"}, {"namespaced", true}, {"verbs", QJsonArray{"list"}}}}}};
                    else if (path == "/api/v1/pods" || path == "/apis/metrics.k8s.io/v1beta1/pods") {
                        const bool metrics = path.contains("metrics.k8s.io");
                        QJsonArray values;
                        const QStringList names{"half", "one", "zero", "missing"};
                        for (int index = 0; index < (metrics ? 3 : 4); ++index) {
                            QJsonObject value{{"apiVersion", metrics ? "metrics.k8s.io/v1beta1" : "v1"}, {"kind", metrics ? "PodMetrics" : "Pod"},
                                {"metadata", QJsonObject{{"name", names[index]}, {"namespace", "default"}, {"uid", "uid-" + names[index]},
                                    {"resourceVersion", "1"}, {"creationTimestamp", at.addDays(-1).toString(Qt::ISODateWithMs)}}}};
                            if (metrics) {
                                value["timestamp"] = at.addSecs(scenario == "stale" || scenario.startsWith("display_stale") ? -35 : 0).toString(Qt::ISODateWithMs);
                                value["window"] = "15s";
                                value["containers"] = QJsonArray{QJsonObject{{"name", "main"}, {"usage", QJsonObject{
                                    {"cpu", index == 0 && scenario == "cpu_fraction" ? "0.125m" : QStringList{"500m", "1", "0"}[index]},
                                    {"memory", index == 0 && scenario == "memory_fraction" ? "1.5" : QStringList{"64Mi", "128Mi", "0"}[index]}}}}};
                            } else {
                                QJsonArray containers{QJsonObject{{"name", "main"}, {"image", "busybox:1"},
                                    {"resources", QJsonObject{{"requests", QJsonObject{{"cpu", "100m"}, {"memory", "32Mi"}, {"ephemeral-storage", "1Gi"}}},
                                        {"limits", QJsonObject{{"cpu", "2"}, {"memory", "256Mi"}, {"ephemeral-storage", "2Gi"}}}}}}};
                                if ((scenario == "incomplete" || scenario == "display_incomplete") && index == 0) containers.append(QJsonObject{{"name", "sidecar"}, {"image", "busybox:1"}});
                                value["spec"] = QJsonObject{{"containers", containers}};
                                value["status"] = QJsonObject{{"phase", "Running"}};
                                if (scenario.startsWith("ready_") || scenario.startsWith("restart_")) {
                                    if (index != 3) value["status"] = QJsonObject{{"phase", "Running"}, {"containerStatuses", QJsonArray{
                                        QJsonObject{{"name", "main"}, {"ready", index < 2}, {"restartCount", index == 1 ? 4 : 0},
                                            {"state", QJsonObject{{"running", QJsonObject{}}}}}}}};
                                }
                            }
                            values.append(value);
                        }
                        document = {{"metadata", QJsonObject{}}, {"items", values}};
                    } else if (path == "/api/v1/persistentvolumeclaims") {
                        document = {{"metadata", QJsonObject{}}, {"items", QJsonArray{QJsonObject{
                            {"apiVersion", "v1"}, {"kind", "PersistentVolumeClaim"},
                            {"metadata", QJsonObject{{"name", "capacity-only"}, {"namespace", "default"}, {"uid", "uid-volume"},
                                {"resourceVersion", "1"}, {"creationTimestamp", at.addDays(-1).toString(Qt::ISODateWithMs)}}},
                            {"spec", QJsonObject{{"resources", QJsonObject{{"requests", QJsonObject{{"storage", "1Gi"}}}}}}},
                            {"status", QJsonObject{{"phase", "Bound"}, {"capacity", QJsonObject{{"storage", "2Gi"}}}}}}}}};
                    } else status = 404;
                    const auto body = QJsonDocument(document).toJson(QJsonDocument::Compact);
                    socket->write("HTTP/1.1 " + QByteArray::number(status) + " Result\r\nContent-Type: application/json\r\nContent-Length: " +
                        QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body);
                    socket->disconnectFromHost();
                });
                connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            }
        });
    }
};
QQuickItem* visualItem(QQuickItem* root, const QString& name) {
    if (root->objectName() == name || (name == "closeSession" && root->objectName().startsWith("closeSession_"))) return root;
    for (auto* child : root->childItems()) if (auto* found = visualItem(child, name)) return found;
    return nullptr;
}
QQuickItem* item(QObject* root, const QString& name) {
    const auto parts = name.split('_');
    if (parts.size() == 3 && (parts[0] == "cell" || parts[0] == "eventCell" || parts[0] == "pinnedCell")) {
        const QStringList views = parts[0] == "pinnedCell" ? QStringList{"resourcePinnedTable", "eventPinnedTable"}
            : QStringList{parts[0] == "eventCell" ? "eventTable" : "resourceTable"};
        for (const auto& viewName : views) {
            auto* view = item(root, viewName);
            if (!view || !view->isVisible() || view->width() <= 0) continue;
            // Use the public view API: cached delegates can keep the same object name.
            QQmlExpression expression(qmlContext(view), view, QString("itemAtIndex(model.index(%1, %2))").arg(parts[1], parts[2]));
            auto* cell = expression.evaluate().value<QQuickItem*>();
            if (expression.hasError()) std::fprintf(stderr, "Table cell lookup failed: %s\n", qPrintable(expression.error().toString()));
            if (cell) return cell;
        }
        return nullptr;
    }
    if (auto* found = root->findChild<QQuickItem*>(name)) return found;
    auto* window = qobject_cast<QQuickWindow*>(root);
    return window ? visualItem(window->contentItem(), name) : nullptr;
}
QString text(QObject* root, const QString& name) {
    auto* value = item(root, name); return value ? value->property("text").toString() : QString{};
}
bool click(QQuickWindow* window, QQuickItem* target, Qt::MouseButton button = Qt::LeftButton) {
    if (target && !target->isVisible() && (target->objectName()=="radarWorkspaceButton" || target->objectName()=="resourceFieldFilters")) {
        const auto* expander=item(window,"toggleLandscapeFilters");
        const auto action=target->objectName()=="resourceFieldFilters" && expander && expander->isVisible()
            ? "toggleLandscapeFilters" : "toggleSidebar";
        if (!click(window,item(window,action)) || !waitFor([&] { return target->isVisible(); })) return false;
    }
    if (!target || !target->isVisible() || !target->isEnabled()) return false;
    QList<QQuickItem*> ancestors;
    for (auto* parent = target; parent; parent = parent->parentItem()) ancestors.prepend(parent);
    for (auto* parent : ancestors) parent->ensurePolished();
    QCoreApplication::processEvents();
    if (!podlord::test::scrollIntoView(window,target)) return false;
    const auto position = target->mapToScene(QPointF(target->width() / 2, target->height() / 2)).toPoint();
    QTest::mouseClick(window, button, Qt::NoModifier, position); return true;
}
bool type(QQuickWindow* window, QQuickItem* target, const QString& value) {
    if (!target || !target->isVisible() || !target->isEnabled()) return false;
    target->forceActiveFocus();
    QTest::keySequence(window, QKeySequence::SelectAll);
    if (value.isEmpty()) QTest::keyClick(window, Qt::Key_Backspace);
    for (const auto c : value) QTest::keyClick(window, c.toLatin1());
    return true;
}

struct Case final { QString field, expression; QStringList names; bool invalid = false; };
std::optional<Case> testcase(const QString& scenario) {
    const QMap<QString, Case> cases{
        {"ready_exact", {"ready", "\"1/1\"", {"half", "one"}}},
        {"ready_unready", {"ready", "\"0/1\"", {"zero", "missing"}}},
        {"ready_regex", {"ready", "/^1\\/1$/", {"half", "one"}}},
        {"ready_prefix", {"ready", "~1/", {"half", "one"}}},
        {"ready_suffix", {"ready", "1~", {"half", "one", "zero", "missing"}}},
        {"ready_alternatives", {"ready", "\"1/1\" \"0/1\"", {"half", "one", "zero", "missing"}}},
        {"ready_unavailable", {"ready", "\"-\"", {"capacity-only"}}},
        {"ready_numeric", {"ready", "1", {}}},
        {"ready_picker", {"ready", "\"1/1\"", {"half", "one"}}},
        {"ready_invalid", {"ready", "/[/", {}, true}},
        {"restart_zero", {"restarts", "=0", {"half", "zero"}}},
        {"restart_greater", {"restarts", ">3", {"one"}}},
        {"restart_alias", {"restarts", "=>4", {"one"}}},
        {"restart_alternatives", {"restarts", "=0 =4", {"half", "one", "zero"}}},
        {"restart_text", {"restarts", "\"4\"", {"one"}}},
        {"restart_missing", {"restarts", "\"-\"", {"missing", "capacity-only"}}},
        {"restart_no_match", {"restarts", "=9", {}}},
        {"restart_or", {"restarts", "<1 >3", {"half", "one", "zero"}}},
        {"restart_and", {"restarts", ">=1", {"one"}}},
        {"restart_radar", {"restarts", "=4", {"one"}}},
        {"restart_picker", {"restarts", "=4", {"one"}}},
        {"cpu_milli", {"cpu", "=500m", {"half"}}},
        {"cpu_millicores", {"cpu", "500millicores", {"half"}}},
        {"cpu_alias_c", {"cpu", ".5c", {"half"}}},
        {"cpu_upper_m", {"cpu", "500M", {"half"}}},
        {"cpu_fraction", {"cpu", "=0.125m", {"half"}}},
        {"cpu_limit", {"cpu", "=2", {}}},
        {"cpu_operator_alias", {"cpu", "=> 500m =< 500m", {"half"}}},
        {"cpu_core", {"cpu", "=.5", {"half"}}},
        {"cpu_display", {"cpu", "500 mCPU", {"half"}}},
        {"cpu_core_label", {"cpu", "0.5 cores", {"half"}}},
        {"cpu_range", {"cpu", ">=250m <1", {"half"}}},
        {"cpu_range_reverse", {"cpu", "<1 >=250m", {"half"}}},
        {"cpu_conflict", {"cpu", ">=1 <.5", {}}},
        {"cpu_alternatives", {"cpu", "500m 1", {"half", "one"}}},
        {"cpu_range_alternatives", {"cpu", ">=.5 .5 1 0", {"half", "one"}}},
        {"cpu_less", {"cpu", "<500m", {"zero"}}},
        {"cpu_less_equal", {"cpu", "<=500m", {"half", "zero"}}},
        {"cpu_greater", {"cpu", ">500m", {"one"}}},
        {"cpu_greater_equal", {"cpu", ">=500m", {"half", "one"}}},
        {"cpu_zero", {"cpu", "=0", {"zero"}}},
        {"cpu_missing", {"cpu", ">=0", {"half", "one", "zero"}}},
        {"cpu_exponent", {"cpu", "=5e-1", {"half"}}},
        {"cpu_spaced_operator", {"cpu", ">= 500m < 1", {"half"}}},
        {"cpu_quoted", {"cpu", "\"500 mCPU\"", {"half"}}},
        {"memory_binary", {"memory", "=64Mi", {"half"}}},
        {"memory_lowercase", {"memory", "=64mi", {"half"}}},
        {"memory_lowercase_range", {"memory", ">=64m <65mi", {"half"}}},
        {"memory_fraction", {"memory", "=1.5B", {"half"}}},
        {"memory_limit", {"memory", "=256Mi", {}}},
        {"memory_display", {"memory", "64 MiB", {"half"}}},
        {"memory_bytes", {"memory", "=67108864", {"half"}}},
        {"memory_decimal", {"memory", ">=64M <65Mi", {"half"}}},
        {"memory_kilo", {"memory", "=65536Ki", {"half"}}},
        {"memory_range", {"memory", ">=64Mi <=128Mi", {"half", "one"}}},
        {"memory_missing", {"memory", ">=0", {"half", "one", "zero"}}},
        {"memory_zero", {"memory", "=0B", {"zero"}}},
        {"storage_capacity", {"storage", ">=0", {}}},
        {"storage_limit", {"storage", "=2Gi", {}}},
        {"storage_zero", {"storage", "=0", {}}},
        {"negative", {"cpu", "=-1", {}, true}},
        {"overflow", {"cpu", "=1e9999", {}, true}},
        {"underflow", {"cpu", "=1e-9999", {}, true}},
        {"bad_unit", {"memory", "64bananas", {}, true}},
        {"bad_operator", {"cpu", ">>500m", {}, true}},
        {"missing_operand", {"cpu", ">=", {}, true}},
        {"regex", {"cpu", "/[/", {}, true}},
        {"display_regex", {"cpu", "/500/", {"half"}}},
        {"display_prefix", {"cpu", "~500", {"half"}}},
        {"display_suffix", {"cpu", "mCPU~", {"half", "one", "zero"}}},
        {"display_contains", {"memory", "MiB", {"half", "one"}}},
        {"display_unavailable", {"storage", "\"-\"", {"half", "one", "zero", "missing", "capacity-only"}}},
        {"display_regex_unavailable", {"storage", "/^-$/", {"half", "one", "zero", "missing", "capacity-only"}}},
        {"display_stale", {"cpu", "stale", {"half", "one", "zero"}}},
        {"display_stale_exact", {"cpu", "\"500 mCPU (stale)\"", {"half"}}},
        {"display_incomplete", {"cpu", "incomplete", {"half"}}},
        {"display_mixed", {"cpu", ">=.5 /no-match/", {"half", "one"}}},
        {"center", {"cpu", "500m", {"half"}}},
        {"narrow_center", {"cpu", "500m", {"half"}}},
        {"empty", {"cpu", "", {"half", "one", "zero", "missing", "capacity-only"}}},
        {"clear", {"cpu", "500m", {"half"}}},
        {"stale", {"cpu", "500m", {"half"}}},
        {"incomplete", {"cpu", "500m", {"half"}}},
        {"and", {"cpu", ">=500m", {"one"}}},
        {"radar", {"cpu", "500m", {"half"}}}};
    const auto found = cases.constFind(scenario);
    return found == cases.cend() ? std::nullopt : std::optional<Case>(*found);
}
bool execute(const QString& scenario) {
    const auto test = testcase(scenario);
    if (!test) return false;
    QTemporaryDir temporary;
    MetricsBoundary server(scenario);
    if (!temporary.isValid() || !server.listen(QHostAddress::LocalHost, 0)) return false;
    QFile source(temporary.filePath("source.config"));
    const auto yaml = "apiVersion: v1\nkind: Config\nclusters:\n- name: local\n  cluster:\n    server: http://127.0.0.1:" + QByteArray::number(server.serverPort()) +
        "\nusers:\n- name: local\n  user: {token: local-test-token}\ncontexts:\n- name: local\n  context: {cluster: local, user: local}\n";
    if (!source.open(QIODevice::WriteOnly) || source.write(yaml) != yaml.size()) return false;
    source.close();
    podlord::Workspace workspace(temporary.filePath("profile"));
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("workspace", &workspace);
    engine.load(QUrl("qrc:/podlord/Main.qml"));
    if (engine.rootObjects().isEmpty()) return false;
    auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
    if (!window || !waitFor([&] { auto* control = item(window, "importButton"); return control && control->isEnabled(); }) ||
        !type(window, item(window, "sourcePath"), source.fileName()) || !click(window, item(window, "importButton")) ||
        !waitFor([&] { auto* control = item(window, "openContext"); return control && control->isEnabled() && !workspace.busy(); }) ||
        !click(window, item(window, "openContext"))) return false;
    if (!waitFor([&] {
        if (workspace.loading() || workspace.totalResourceCount() != 5) return false;
        auto* model = workspace.table();
        for (int row = 0; row < model->rowCount(); ++row)
            if (model->data(model->index(row, 0)).toString() == "half") return model->data(model->index(row, 7), Qt::UserRole + 6).isValid();
        return false;
    })) return false;
    const auto calls = server.requests;
    if (scenario == "narrow_center") { window->resize(QSize(680, 480)); QTest::qWait(10); }
    auto* fieldsButton = item(window, "resourceFieldFilters");
    if (!click(window, fieldsButton)) return false;
    if (scenario == "center" || scenario == "narrow_center") {
        QObject* dialog = nullptr;
        for (auto* control : window->findChildren<QObject*>())
            if (control->property("title").toString() == "Resource field filters") { dialog = control; break; }
        if (!dialog || !waitFor([&] {
            return qAbs(dialog->property("x").toDouble() - (window->width() - dialog->property("width").toDouble()) / 2) <= 2 &&
                qAbs(dialog->property("y").toDouble() - (window->height() - dialog->property("height").toDouble()) / 2) <= 2;
        })) { std::fprintf(stderr, "Field dialog is not centered in the visible window.\n"); return false; }
    }
    auto* choice = item(window, "fieldFilterColumn");
    if (!choice) { std::fprintf(stderr, "Quantity fields are absent from the public picker.\n"); return false; }
    const auto choose = [&](const QString& field) {
        const auto options = choice->property("model").toList();
        int index = -1;
        for (int number = 0; number < options.size(); ++number) if (options[number].toMap().value("id") == field) index = number;
        if (index < 0 || !click(window, choice)) return false;
        QTest::keyClick(window, Qt::Key_Home);
        for (int number = 0; number < index; ++number) QTest::keyClick(window, Qt::Key_Down);
        QTest::keyClick(window, Qt::Key_Return);
        return waitFor([&] { return workspace.filterPickerField() == field; });
    };
    const auto expressionControl = [&] { return item(window, "fieldFilterExpression"); };
    if (!choose(test->field) || !type(window, expressionControl(), test->expression)) return false;
    if (scenario == "restart_and" && (!choose("ready") || !type(window, expressionControl(), "\"1/1\""))) return false;
    if (scenario == "ready_picker" && workspace.filterPickerValues() != QStringList{"0/1", "1/1"}) return false;
    if (scenario == "restart_picker" && workspace.filterPickerValues() != QStringList{"0", "4"}) return false;
    if (scenario == "and" && (!choose("memory") || !type(window, expressionControl(), ">=128Mi"))) return false;
    if (scenario == "clear" && !type(window, expressionControl(), "")) return false;
    auto expected = test->names;
    if (scenario == "clear") expected = {"half", "one", "zero", "missing", "capacity-only"};
    expected.sort();
    if (!waitFor([&] {
        if (test->invalid) return !workspace.filterError().isEmpty() && workspace.resourceCount() == 0;
        QStringList names;
        auto* model = workspace.table();
        for (int row = 0; row < model->rowCount(); ++row) names.append(model->data(model->index(row, 0)).toString());
        names.sort();
        return workspace.filterError().isEmpty() && names == expected;
    })) {
        std::fprintf(stderr, "Quantity filter failed: %s %s; matches=%d; error=%s\n", qPrintable(test->field),
            qPrintable(test->expression), workspace.resourceCount(), qPrintable(workspace.filterError()));
        return false;
    }
    if (scenario == "radar" || scenario == "restart_radar") {
        QTest::keyClick(window, Qt::Key_Escape);
        auto* radarButton = item(window,"radarWorkspaceButton");
        if (!click(window, radarButton) || !waitFor([&] {
            auto* radar = item(window, "resourceRadar"); return radar && radar->isVisible() && radar->property("count").toInt() == 1;
        })) return false;
    }
    const auto screenshot = qEnvironmentVariable("PODLORD_METRIC_FILTER_SCREENSHOT");
    if (!screenshot.isEmpty() && !window->grabWindow().save(screenshot, "PNG")) return false;
    return server.requests == calls;
}
}
bool realCluster(const QString& source) {
    QTemporaryDir temporary;
    if (!temporary.isValid()) return false;
    podlord::Workspace workspace(temporary.filePath("profile"));
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("workspace", &workspace);
    engine.load(QUrl("qrc:/podlord/Main.qml"));
    if (engine.rootObjects().isEmpty()) return false;
    auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
    const auto fail = [&](const char* stage) {
        std::fprintf(stderr, "Real cached field filter failed: %s; rows=%d/%d; error=%s; status=%s\n",
            stage, workspace.resourceCount(), workspace.totalResourceCount(), qPrintable(workspace.filterError()), qPrintable(workspace.status()));
        return false;
    };
    if (!window || !waitFor([&] { auto* control = item(window, "importButton"); return control && control->isEnabled(); }) ||
        !type(window, item(window, "sourcePath"), QUrl::fromLocalFile(source).toString()) || !click(window, item(window, "importButton")) ||
        !waitFor([&] { auto* control = item(window, "openContext"); return control && control->isEnabled() && !workspace.busy(); }) ||
        !click(window, item(window, "openContext"))) return fail("import");
    std::fprintf(stdout, "Real source import: file URL through public source field and Import button.\n");
    const auto measuredIdentities = [&](const QString& field) {
        QStringList identities;
        auto* model = workspace.table();
        int column = -1;
        for (int index = 0; index < model->columnCount(); ++index)
            if (model->headerData(index, Qt::Horizontal, Qt::UserRole) == field) column = index;
        if (column < 0) return identities;
        for (int row = 0; row < model->rowCount(); ++row) {
            const auto value = model->data(model->index(row, column), Qt::UserRole + 6);
            if (value.metaType().id() == QMetaType::Double || value.metaType().id() == QMetaType::LongLong)
                identities.append(model->data(model->index(row, 0), Qt::UserRole).toString());
        }
        identities.sort(); return identities;
    };
    if (!waitFor([&] { return !workspace.loading() && workspace.totalResourceCount() > 1000 &&
        !measuredIdentities("cpu").isEmpty() && !measuredIdentities("memory").isEmpty(); }, 120000)) return fail("real resource and metric cache");
    QStringList expectedConfigMaps;
    int ownedConfigMaps = 0;
    for (int row = 0; row < workspace.table()->rowCount(); ++row) {
        auto* model = workspace.table();
        const auto ns = model->data(model->index(row, 2)).toString();
        if (model->data(model->index(row, 1)) != "ConfigMap" || (ns != "visual-a" && ns != "visual-b")) continue;
        expectedConfigMaps.append(model->data(model->index(row, 0), Qt::UserRole).toString());
        if (model->data(model->index(row, 0)).toString().startsWith("visual-config-")) ++ownedConfigMaps;
    }
    if (ownedConfigMaps != 128) return fail("owned ConfigMap inventory");
    expectedConfigMaps.sort();
    auto* fieldsButton = item(window,"resourceFieldFilters");
    if (!click(window, fieldsButton)) return fail("open field dialog");
    const auto setField = [&](const QString& field, const QString& expression) {
        auto* choice = item(window, "fieldFilterColumn");
        if (!choice) return false;
        const auto options = choice->property("model").toList();
        int index = -1;
        for (int number = 0; number < options.size(); ++number) if (options[number].toMap().value("id") == field) index = number;
        if (index < 0 || !click(window, choice)) return false;
        QTest::keyClick(window, Qt::Key_Home);
        for (int number = 0; number < index; ++number) QTest::keyClick(window, Qt::Key_Down);
        QTest::keyClick(window, Qt::Key_Return);
        return waitFor([&] { return workspace.filterPickerField() == field; }) &&
            type(window, item(window, "fieldFilterExpression"), expression);
    };
    if (!setField("kind", "\"ConfigMap\"") || !setField("namespace", "\"visual-a\" \"visual-b\"") ||
        !waitFor([&] {
            QStringList actual;
            for (int row = 0; row < workspace.table()->rowCount(); ++row)
                actual.append(workspace.table()->data(workspace.table()->index(row, 0), Qt::UserRole).toString());
            actual.sort();
            return actual == expectedConfigMaps && workspace.filterError().isEmpty();
        })) return fail("text OR/AND including Kubernetes-created ConfigMaps");
    if (!setField("name", "\"visual-config-0017\"") || !setField("namespace", "\"visual-a\"") ||
        !waitFor([&] { return workspace.resourceCount() == 1 && workspace.table()->data(workspace.table()->index(0, 0)) == "visual-config-0017"; }))
        return fail("exact text fields");
    const auto evidence = qEnvironmentVariable("PODLORD_REAL_FIELD_FILTER_EVIDENCE");
    if (!evidence.isEmpty()) {
        QTest::qWait(50);
        if (!window->grabWindow().save(evidence + "-text.png", "PNG")) return fail("text screenshot");
    }
    if (!click(window, item(window, "clearFieldFilters")) || !waitFor([&] { return workspace.resourceCount() == workspace.totalResourceCount(); })) return fail("reset text");
    const auto expectedCpu = measuredIdentities("cpu");
    if (!setField("cpu", ">=0") || !waitFor([&] { return workspace.filterError().isEmpty() && measuredIdentities("cpu") == expectedCpu &&
        workspace.resourceCount() == expectedCpu.size(); })) return fail("CPU measured-only");
    if (workspace.filterPickerValues().isEmpty()) return fail("cached CPU options");
    if (!evidence.isEmpty()) {
        QTest::qWait(50);
        if (!window->grabWindow().save(evidence + "-cpu.png", "PNG")) return fail("CPU screenshot");
    }
    if (!setField("cpu", "/mCPU/") || !waitFor([&] { return workspace.filterError().isEmpty() &&
        measuredIdentities("cpu") == expectedCpu && workspace.resourceCount() == expectedCpu.size(); })) return fail("real quantity display regex");
    QList<double> frameTimes;
    const int snapshotRecords = workspace.totalResourceCount();
    for (int sample = 0; sample < 20; ++sample) {
        const bool empty = sample % 2 != 0;
        QElapsedTimer elapsed; elapsed.start();
        if (!type(window, item(window, "fieldFilterExpression"), empty ? ">1e9" : ">=0") || !waitFor([&] {
            return workspace.filterError().isEmpty() && (empty ? workspace.resourceCount() == 0 : workspace.resourceCount() > 0);
        })) return fail("cached field benchmark input");
        QCoreApplication::processEvents();
        if (window->grabWindow().isNull()) return fail("cached field benchmark frame");
        frameTimes.append(elapsed.nsecsElapsed() / 1000000.0);
    }
    std::sort(frameTimes.begin(), frameTimes.end());
    const QJsonObject benchmark{{"boundary", "Public field keyboard input through completed Qt frame"},
        {"source", "Owned real Kubernetes resource and Metrics API cache"}, {"records", snapshotRecords}, {"samples", frameTimes.size()},
        {"backend", qEnvironmentVariable("QT_QUICK_BACKEND")}, {"qt_version", qVersion()},
        {"p50_ms", (frameTimes[9] + frameTimes[10]) / 2}, {"p95_ms", frameTimes[18]}, {"max_ms", frameTimes.last()}};
    std::printf("%s\n", QJsonDocument(benchmark).toJson(QJsonDocument::Compact).constData());
    if (!click(window, item(window, "clearFieldFilters")) || !waitFor([&] { return workspace.resourceCount() == workspace.totalResourceCount(); })) return fail("reset CPU");
    const auto expectedMemory = measuredIdentities("memory");
    if (!setField("memory", ">=0") || !waitFor([&] { return workspace.filterError().isEmpty() && measuredIdentities("memory") == expectedMemory &&
        workspace.resourceCount() == expectedMemory.size(); })) return fail("memory measured-only");
    if (!click(window, item(window, "clearFieldFilters")) || !setField("storage", ">=0") ||
        !waitFor([&] { return workspace.filterError().isEmpty() && workspace.resourceCount() == 0; }) || !workspace.filterPickerValues().isEmpty())
        return fail("storage request is not usage");
    if (!click(window, item(window, "clearFieldFilters")) || !setField("kind", "\"Pod\"") || !setField("ready", "\"2/2\"") || !waitFor([&] {
        return workspace.filterError().isEmpty() && workspace.resourceCount() == 1 &&
            workspace.table()->data(workspace.table()->index(0, 0), Qt::UserRole) == "/api/v1/namespaces/visual-a/pods/visual-multi-container";
    }) || !workspace.filterPickerValues().contains("2/2")) return fail("real Ready fraction and cached options");
    if (!evidence.isEmpty() && !window->grabWindow().save(evidence + "-ready.png", "PNG")) return fail("Ready screenshot");
    if (!click(window, item(window, "clearFieldFilters"))) return fail("reset Ready");
    const auto expectedRestarts = measuredIdentities("restarts");
    if (expectedRestarts.isEmpty() || !setField("restarts", ">=0") || !waitFor([&] {
        return workspace.filterError().isEmpty() && workspace.resourceCount() == expectedRestarts.size() && measuredIdentities("restarts") == expectedRestarts;
    }) || workspace.filterPickerValues().isEmpty()) return fail("real observed restart counts");
    if (!evidence.isEmpty() && !window->grabWindow().save(evidence + "-restarts.png", "PNG")) return fail("Restarts screenshot");
    if (!click(window, item(window, "clearFieldFilters")) || !setField("kind", "\"ConfigMap\"") ||
        !setField("namespace", "\"visual-a\" \"visual-b\"") || !setField("name", "~visual-config-") ||
        !waitFor([&] { return workspace.resourceCount() == 128; })) return fail("Radar filter setup");
    QTest::keyClick(window, Qt::Key_Escape);
    if (!click(window, item(window, "radarWorkspaceButton")) || !waitFor([&] {
        auto* radar = item(window, "resourceRadar");
        return radar && radar->isVisible() && radar->property("count").toInt() == 128;
    })) return fail("shared filtered Radar");
    if (!evidence.isEmpty()) {
        QTest::qWait(50);
        if (!window->grabWindow().save(evidence + "-radar.png", "PNG")) return fail("Radar screenshot");
    }
    std::printf("Real cached field filters passed: exact text, AND/OR, %lld CPU measurements, %lld memory measurements, missing storage, Ready 2/2, %lld observed restart counts, 128 filtered Radar resources.\n",
        static_cast<long long>(expectedCpu.size()), static_cast<long long>(expectedMemory.size()), static_cast<long long>(expectedRestarts.size()));
    return true;
}

int main(int argc, char** argv) {
    QGuiApplication app(argc, argv);
    if (argc != 2 && argc != 3) return 2;
    const bool passed = argc == 3 ? QString::fromLocal8Bit(argv[1]) == "real" && realCluster(QString::fromLocal8Bit(argv[2]))
        : execute(QString::fromLocal8Bit(argv[1]));
    if (!passed) std::fprintf(stderr, "Measured filter UI scenario failed: %s\n", argv[1]);
    return passed ? 0 : 1;
}
