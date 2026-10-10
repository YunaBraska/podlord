#include "workspace.h"
#include "ui_input.h"
#include <QFile>
#include <QClipboard>
#include <QJsonDocument>
#include <QPointer>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QTemporaryDir>
#include <QTcpServer>
#include <QTcpSocket>
#include <QUrlQuery>
#include <QWebSocketServer>
#include <QtTest/QTest>
#include <cstdio>
#include <functional>

namespace {
bool waitFor(const std::function<bool()>& test, int timeout = 15000) { return QTest::qWaitFor(test, timeout); }
#define REQUIRE(condition) do { if (!(condition)) { std::fprintf(stderr, "Terminal check failed at %d: %s\n", __LINE__, #condition); return false; } } while (false)
bool run(const QString& scenario, const QString& configPath) {
    const bool real = scenario.startsWith("real_");
    QTemporaryDir profile; REQUIRE(profile.isValid());
    QTcpServer api; QWebSocketServer streaming("External Kubernetes exec boundary", QWebSocketServer::NonSecureMode);
    streaming.setSupportedSubprotocols({scenario == "protocol" ? "unsupported" : "v5.channel.k8s.io"});
    REQUIRE(real || api.listen(QHostAddress::LocalHost));
    const QString ns = real ? "visual-a" : "default", name = real ? "visual-multi-container" : "terminal-pod";
    const QString path = "/api/v1/namespaces/" + ns + "/pods/" + name;
    QJsonObject pod{{"apiVersion", "v1"}, {"kind", "Pod"}, {"metadata", QJsonObject{{"name", name}, {"namespace", ns}, {"uid", "original-pod"}, {"resourceVersion", "1"}}},
        {"status", QJsonObject{{"phase", scenario == "completed" ? "Succeeded" : "Running"}}},
        {"spec", QJsonObject{{"containers", QJsonArray{QJsonObject{{"name", "alpha"}, {"image", "external-kubernetes-boundary"}}, QJsonObject{{"name", "beta"}, {"image", "external-kubernetes-boundary"}}}}}}};
    QPointer<QWebSocket> peer; QUrl upgradeUrl; QByteArray input; QList<QJsonObject> sizes;
    int upgrades = 0, freshReads = 0, inputFrames = 0; bool armed = false, authorized = true;
    QObject::connect(&api, &QTcpServer::newConnection, &api, [&] {
        while (auto* socket = api.nextPendingConnection()) {
            QObject::connect(socket, &QTcpSocket::readyRead, socket, [&, socket] {
                const auto bytes = socket->peek(65536); if (!bytes.contains("\r\n\r\n")) return;
                const QUrl url(QString::fromUtf8(bytes.split('\n').first().split(' ').value(1)));
                authorized = authorized && bytes.toLower().contains("authorization: bearer local-terminal-token");
                if (bytes.toLower().contains("upgrade: websocket")) {
                    ++upgrades; upgradeUrl = url;
                    if (scenario == "upgrade_auth" || scenario == "upgrade_forbidden" || scenario == "upgrade_rate" || scenario == "upgrade_redirect") {
                        const int status = scenario == "upgrade_auth" ? 401 : scenario == "upgrade_forbidden" ? 403 : scenario == "upgrade_rate" ? 429 : 302;
                        socket->readAll(); socket->write(QByteArray("HTTP/1.1 ") + QByteArray::number(status) + " Rejected\r\nContent-Length: 0\r\nConnection: close\r\n\r\n"); socket->disconnectFromHost(); return;
                    }
                    QObject::disconnect(socket, &QTcpSocket::readyRead, socket, nullptr); streaming.handleConnection(socket); return;
                }
                socket->readAll(); QJsonObject body; int status = 200;
                if (url.path() == "/api") body = {{"versions", QJsonArray{"v1"}}};
                else if (url.path() == "/apis") body = {{"groups", QJsonArray{}}};
                else if (url.path() == "/api/v1") body = {{"resources", QJsonArray{QJsonObject{{"name", "pods"}, {"kind", "Pod"}, {"namespaced", true}, {"verbs", QJsonArray{"list", "get"}}}}}};
                else if (url.path() == "/api/v1/pods") body = {{"apiVersion", "v1"}, {"kind", "PodList"}, {"metadata", QJsonObject{}}, {"items", QJsonArray{pod}}};
                else if (url.path() == path) {
                    body = pod;
                    if (armed) {
                        ++freshReads;
                        if (scenario == "recreated") { auto metadata = body["metadata"].toObject(); metadata["uid"] = "replacement"; body["metadata"] = metadata; }
                        if (scenario == "container_removed") body["spec"] = QJsonObject{{"containers", QJsonArray{QJsonObject{{"name", "beta"}}}}};
                        if (scenario == "target_auth") status = 401;
                        if (scenario == "target_forbidden") status = 403;
                        if (scenario == "target_rate") status = 429;
                        if (scenario == "target_redirect") status = 302;
                    }
                } else status = 404;
                const auto payload = QJsonDocument(body).toJson(QJsonDocument::Compact);
                socket->write(QByteArray("HTTP/1.1 ") + QByteArray::number(status) + " Result\r\nContent-Type: application/json\r\nContent-Length: " + QByteArray::number(payload.size()) + "\r\nConnection: close\r\n\r\n" + payload); socket->disconnectFromHost();
            });
            QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
        }
    });
    QObject::connect(&streaming, &QWebSocketServer::newConnection, &streaming, [&] {
        peer = streaming.nextPendingConnection(); if (!peer) return;
        QObject::connect(peer, &QWebSocket::binaryMessageReceived, peer, [&](const QByteArray& message) {
            if (message.isEmpty()) return;
            if (message[0] == 0) { input += message.sliced(1); ++inputFrames; }
            if (message[0] == 4) sizes.append(QJsonDocument::fromJson(message.sliced(1)).object());
        });
        QObject::connect(peer, &QWebSocket::disconnected, peer, &QObject::deleteLater);
        peer->sendBinaryMessage(QByteArray(1, char(1)) + "\033[2J\033[Hnative shell ready\r\n$ ");
    });
    const auto config = profile.path() + "/source.yaml";
    if (!real) { QFile file(config); REQUIRE(file.open(QIODevice::WriteOnly)); file.write(QString("apiVersion: v1\nkind: Config\nclusters:\n- name: local\n  cluster:\n    server: http://127.0.0.1:%1\nusers:\n- name: local\n  user:\n    token: local-terminal-token\ncontexts:\n- name: local\n  context:\n    cluster: local\n    user: local\ncurrent-context: local\n").arg(api.serverPort()).toUtf8()); file.close(); }
    const auto imported = podlord::KubeconfigStore(profile.path()).importFile(real ? configPath : config);
    const auto* source = std::get_if<podlord::SourceSnapshot>(&imported); REQUIRE(source && !source->contexts.isEmpty());
    podlord::Workspace workspace(profile.path()); QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("workspace", &workspace); engine.load(QUrl("qrc:/podlord/Main.qml")); REQUIRE(!engine.rootObjects().isEmpty());
    auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first()); REQUIRE(window);
    REQUIRE(waitFor([&] { return !workspace.busy(); }) && workspace.openContext(source->contexts.first().id));
    REQUIRE(waitFor([&] { return !workspace.busy() && !workspace.loading() && workspace.table()->rowCount() > 0; }, real ? 120000 : 15000));
    REQUIRE(workspace.inspectPath(path)); REQUIRE(waitFor([&] { return !workspace.loading(); }, real ? 60000 : 15000));
    const auto click = [&](const char* object) {
        auto* item = podlord::test::visibleItem(window->contentItem(), QString::fromLatin1(object));
        if (!item || !item->isEnabled() || !podlord::test::scrollIntoView(window, item)) {
            const auto frame = qEnvironmentVariable("PODLORD_TERMINAL_FAILURE_FRAME"); if (!frame.isEmpty()) window->grabWindow().save(frame);
            return false;
        }
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, item->mapToScene({item->width() / 2, item->height() / 2}).toPoint()); return true;
    };
    REQUIRE(click("terminalButton"));
    REQUIRE(waitFor([&] { return podlord::test::visibleItem(window->contentItem(), "terminalConnect") != nullptr; }));
    if (scenario == "connect") {
        const auto* status = podlord::test::visibleItem(window->contentItem(), "terminalStatus");
        REQUIRE(status && status->property("text").toString().contains("remote processes may continue"));
    }
    if (scenario == "completed") { REQUIRE(!workspace.canStartTerminal()); REQUIRE(!workspace.startContainerTerminal("alpha", "/bin/sh")); return true; }
    if (scenario == "invalid_container" || scenario == "invalid_shell" || scenario == "empty_shell" || scenario == "relative_shell") {
        REQUIRE(!workspace.startContainerTerminal(scenario == "invalid_container" ? "missing" : "alpha", scenario == "empty_shell" ? "" : scenario == "relative_shell" ? "sh" : scenario == "invalid_shell" ? "/bin/sh;id" : "/bin/sh")); REQUIRE(upgrades == 0); return true;
    }
    armed = true; REQUIRE(click("terminalConnect"));
    REQUIRE(waitFor([&] { return workspace.containerTerminal() && (workspace.containerTerminal()->connected() || !workspace.containerTerminal()->active()); }, real ? 60000 : 15000));
    auto* terminal = workspace.containerTerminal(); REQUIRE(terminal);
    if (scenario == "recreated" || scenario == "container_removed" || scenario.startsWith("target_") || scenario.startsWith("upgrade_") || scenario == "protocol") {
        REQUIRE(!terminal->connected() && !terminal->active()); const int sent = upgrades; QTest::qWait(1300); REQUIRE(upgrades == sent && freshReads == 1);
        REQUIRE(!scenario.startsWith("target_") && scenario != "recreated" && scenario != "container_removed" ? upgrades == 1 : upgrades == 0); return true;
    }
    REQUIRE(terminal->connected());
    auto* surface = qobject_cast<podlord::TerminalSurface*>(podlord::test::visibleItem(window->contentItem(), "terminalSurface")); REQUIRE(surface);
    if (!real) {
        REQUIRE(authorized && freshReads == 1 && upgrades == 1);
        const QUrlQuery query(upgradeUrl); REQUIRE(query.queryItemValue("container") == "alpha" && query.queryItemValue("tty") == "true" && query.queryItemValue("stderr") == "false"); REQUIRE(query.allQueryItemValues("command").size() == 4);
        REQUIRE(waitFor([&] { return surface->visibleText().contains("native shell ready") && !sizes.isEmpty(); }));
    }
    surface->forceActiveFocus(); QTest::qWait(30);
    const auto keys = [&](const QByteArray& value) { for (const char c : value) QTest::keyClick(window, c); };
    const auto type = [&](const QByteArray& value) { keys(value); QTest::keyClick(window, Qt::Key_Return); };
    const auto output = [&](const QByteArray& value) { peer->sendBinaryMessage(QByteArray(1, char(1)) + value); };
    if (real) {
        type("printf '\\033[32mPODLORD_NATIVE_PTY_OK\\033[0m\\n'; stty size");
        REQUIRE(waitFor([&] { return surface->visibleText().contains("PODLORD_NATIVE_PTY_OK") && surface->visibleText().contains(QString::number(terminal->columns())); }, 15000));
        if (scenario == "real_vi" || scenario == "real_control_vi") {
            type("vi /tmp/podlord-terminal-check.txt"); QTest::qWait(1000); QTest::keyClick(window, 'i'); keys("PODLORD_VI_WRITTEN");
            if (scenario == "real_vi") { REQUIRE(click("terminalKeys")); REQUIRE(click("terminalKey_escape")); }
            else {
#ifdef Q_OS_MACOS
                const auto control = Qt::MetaModifier;
#else
                const auto control = Qt::ControlModifier;
#endif
                QKeyEvent escape(QEvent::KeyPress, Qt::Key_BracketLeft, control, QString{});
                QCoreApplication::sendEvent(window, &escape);
            }
            QTest::qWait(250); type(":wq"); QTest::qWait(500);
            type("printf '\\033[2J\\033[H'; printf 'READBACK_BEGIN\\n'; cat /tmp/podlord-terminal-check.txt; rm /tmp/podlord-terminal-check.txt; printf 'READBACK_END\\n'");
            const bool verified = waitFor([&] { return surface->visibleText().contains("READBACK_BEGIN\nPODLORD_VI_WRITTEN\nREADBACK_END"); });
            if (!verified) { std::fprintf(stderr, "Real vi screen at failed read-back:\n%s\n", qPrintable(surface->visibleText())); const auto frame = qEnvironmentVariable("PODLORD_TERMINAL_FRAME"); if (!frame.isEmpty()) window->grabWindow().save(frame + ".failure.png"); }
            REQUIRE(verified);
        }
        if (scenario == "real_touch_interrupt") {
            type("sleep 30"); QTest::qWait(250); REQUIRE(click("terminalKeys")); REQUIRE(click("terminalKey_interrupt"));
            type("printf 'TOUCH_INTERRUPT_%s\\n' OK");
            REQUIRE(waitFor([&] { return surface->visibleText().contains("TOUCH_INTERRUPT_OK"); }));
        }
        if (scenario == "real_interrupt") {
            type("sleep 30"); QTest::qWait(250);
#ifdef Q_OS_MACOS
            QTest::keyClick(window, Qt::Key_C, Qt::MetaModifier);
#else
            QTest::keyClick(window, Qt::Key_C, Qt::ControlModifier);
#endif
            type("printf '\\nPODLORD_INTERRUPT_OUTPUT_ONLY\\n'"); REQUIRE(waitFor([&] { return surface->visibleText().contains("\nPODLORD_INTERRUPT_OUTPUT_ONLY\n"); }));
        }
        const auto frame = qEnvironmentVariable("PODLORD_TERMINAL_FRAME"); if (!frame.isEmpty()) { QTest::qWait(150); REQUIRE(window->grabWindow().save(frame)); }
        type("exit 0"); REQUIRE(waitFor([&] { return !terminal->active(); })); std::fprintf(stderr, "Real terminal exit: %s\n", qPrintable(terminal->status())); REQUIRE(terminal->status() == "Shell exited successfully."); return true;
    }
    if (scenario == "empty_escape" || scenario.startsWith("control_")) {
        const QMap<QString, QPair<int, char>> controls{
            {"control_escape", {Qt::Key_BracketLeft, 27}}, {"control_tab", {Qt::Key_I, 9}},
            {"control_linefeed", {Qt::Key_J, 10}}, {"control_enter", {Qt::Key_M, 13}},
            {"control_space", {Qt::Key_Space, 0}}, {"control_backslash", {Qt::Key_Backslash, 28}},
            {"control_bracket_right", {Qt::Key_BracketRight, 29}}, {"control_caret", {Qt::Key_AsciiCircum, 30}},
            {"control_underscore", {Qt::Key_Underscore, 31}}};
        REQUIRE(scenario == "empty_escape" || controls.contains(scenario));
        const auto expected = scenario == "empty_escape" ? QPair<int, char>{Qt::Key_Escape, 27} : controls[scenario];
#ifdef Q_OS_MACOS
        const auto control = Qt::MetaModifier;
#else
        const auto control = Qt::ControlModifier;
#endif
        QKeyEvent event(QEvent::KeyPress, expected.first,
            scenario == "empty_escape" ? Qt::NoModifier : control, QString{});
        QCoreApplication::sendEvent(window, &event);
        const auto observed = waitFor([&] { return !input.isEmpty(); });
        if (!observed || input != QByteArray(1, expected.second)) std::fprintf(stderr, "Terminal control bytes: %s, accepted=%d, focused=%d\n", input.toHex().constData(), event.isAccepted(), surface->hasActiveFocus());
        REQUIRE(input == QByteArray(1, expected.second));
    }
    else if (scenario.startsWith("key_")) {
        const QMap<QString, QPair<int, QByteArray>> cases{
            {"key_enter", {Qt::Key_Enter, "\r"}}, {"key_backspace", {Qt::Key_Backspace, QByteArray(1, char(127))}},
            {"key_backtab", {Qt::Key_Backtab, "\033[Z"}}, {"key_insert", {Qt::Key_Insert, "\033[2~"}},
            {"key_delete", {Qt::Key_Delete, "\033[3~"}}, {"key_home", {Qt::Key_Home, "\033[H"}},
            {"key_end", {Qt::Key_End, "\033[F"}}, {"key_pageup", {Qt::Key_PageUp, "\033[5~"}},
            {"key_pagedown", {Qt::Key_PageDown, "\033[6~"}}, {"key_f1", {Qt::Key_F1, "\033OP"}},
            {"key_alt", {Qt::Key_X, "\033x"}}
        };
        REQUIRE(cases.contains(scenario));
        const auto expected = cases.value(scenario);
        QTest::keyClick(window, static_cast<Qt::Key>(expected.first), scenario == "key_alt" ? Qt::AltModifier
            : scenario == "key_backtab" ? Qt::ShiftModifier : Qt::NoModifier);
        REQUIRE(waitFor([&] { return input == expected.second; }));
        REQUIRE(inputFrames > 0 && upgrades == 1 && freshReads == 1 && terminal->connected());
    }
    else if (scenario.startsWith("shifted_")) {
        const QMap<QString, QPair<int, QString>> cases{
            {"shifted_colon", {Qt::Key_Colon, ":"}}, {"shifted_exclamation", {Qt::Key_Exclam, "!"}},
            {"shifted_uppercase", {Qt::Key_A, "A"}}, {"shifted_unicode", {Qt::Key_Adiaeresis, QString(QChar(0x00c4))}},
            {"shifted_alt_colon", {Qt::Key_Colon, ":"}}, {"shifted_alt_unicode", {Qt::Key_Adiaeresis, QString(QChar(0x00c4))}}};
        REQUIRE(cases.contains(scenario));
        const auto value = cases.value(scenario);
        const bool alt = scenario.startsWith("shifted_alt_");
        QKeyEvent event(QEvent::KeyPress, value.first, Qt::ShiftModifier | (alt ? Qt::AltModifier : Qt::NoModifier), value.second);
        QCoreApplication::sendEvent(window, &event);
        REQUIRE(waitFor([&] { return !input.isEmpty(); }));
        const auto expected = (alt ? QByteArray("\033") : QByteArray{}) + value.second.toUtf8();
        if (input != expected) std::fprintf(stderr, "Shifted terminal input: expected=%s actual=%s\n", expected.toHex().constData(), input.toHex().constData());
        REQUIRE(input == expected && inputFrames == 1 && upgrades == 1 && freshReads == 1 && terminal->connected());
    }
    else if (scenario == "input") { type("echo native"); REQUIRE(waitFor([&] { return input == "echo native\r"; })); }
    else if (scenario == "shortcut_input") {
#ifdef Q_OS_MACOS
        QTest::keyClick(window, Qt::Key_K, Qt::MetaModifier);
#else
        QTest::keyClick(window, Qt::Key_K, Qt::ControlModifier);
#endif
        REQUIRE(waitFor([&] { return input == QByteArray(1, char(11)); }));
    }
    else if (scenario == "interrupt") {
#ifdef Q_OS_MACOS
        QTest::keyClick(window, Qt::Key_C, Qt::MetaModifier);
#else
        QTest::keyClick(window, Qt::Key_C, Qt::ControlModifier);
#endif
        REQUIRE(waitFor([&] { return input == QByteArray(1, char(3)); }));
    } else if (scenario == "resize") { const auto old = sizes.last(); window->resize(window->width() - 100, window->height() + 120); REQUIRE(waitFor([&] { return sizes.last() != old; })); REQUIRE(sizes.last()["Width"].toInt() == terminal->columns()); }
    else if (scenario == "ansi") { output("\033[2J\033[H\033[31;1mRED\033[0m"); REQUIRE(waitFor([&] { return surface->visibleText().startsWith("RED"); })); REQUIRE(terminal->cell(0, 0).attrs.bold && terminal->color(terminal->cell(0, 0).fg).red() > 100); }
    else if (scenario == "alternate") { output("\033[?1049h\033[2J\033[Heditor screen"); REQUIRE(waitFor([&] { return surface->visibleText().contains("editor screen"); })); output("\033[?1049l"); REQUIRE(waitFor([&] { return surface->visibleText().contains("native shell ready") && !surface->visibleText().contains("editor screen"); })); }
    else if (scenario == "unicode") { output(QByteArray::fromHex("e697a5e69cacf09f8c8d")); REQUIRE(waitFor([&] { return surface->visibleText().contains(QString::fromUtf8(QByteArray::fromHex("e697a5e69cacf09f8c8d"))); })); }
    else if (scenario == "history") { QByteArray lines; for (int i = 0; i < 200; ++i) lines += "line-" + QByteArray::number(i) + "\r\n"; output(lines); REQUIRE(waitFor([&] { return terminal->historyLines() > 0; })); QTest::keyClick(window, Qt::Key_PageUp, Qt::ShiftModifier); REQUIRE(!surface->visibleText().contains("line-199")); }
    else if (scenario.startsWith("history_anchor_") || scenario == "history_clear" || scenario == "history_follow") {
        if (scenario == "history_anchor_grow") {
            window->resize(window->width(), 560);
            REQUIRE(click("expandTerminal"));
            auto* dialog = window->findChild<QObject*>("expandedTerminal");
            REQUIRE(dialog && waitFor([&] { return dialog->property("opened").toBool(); }));
            surface = qobject_cast<podlord::TerminalSurface*>(podlord::test::visibleItem(window->contentItem(), "terminalSurface"));
            REQUIRE(surface);
            surface->forceActiveFocus();
        }
        const int count = scenario == "history_anchor_trim" ? 5000 : 200;
        QByteArray lines;
        for (int index = 0; index < count; ++index) lines += "line-" + QByteArray::number(index) + "\r\n";
        output(lines);
        REQUIRE(waitFor([&] { return surface->visibleText().contains("line-" + QString::number(count - 1)); }));
        QTest::keyClick(window, Qt::Key_PageUp, Qt::ShiftModifier);
        const auto before = surface->visibleText();
        REQUIRE(!before.contains("line-" + QString::number(count - 1)));
        const int history = terminal->historyLines();
        if (scenario == "history_anchor_grow") {
            const int rows = terminal->rows();
            window->resize(window->width(), window->height() + 160);
            REQUIRE(waitFor([&] { return terminal->rows() > rows && !sizes.isEmpty() && sizes.last()["Height"].toInt() == terminal->rows(); }));
            REQUIRE(surface->visibleText().section('\n', 0, 0) == before.section('\n', 0, 0));
        } else if (scenario == "history_clear") {
            output("\033[3J\033[2J\033[Hcleared-screen");
            REQUIRE(waitFor([&] { return surface->visibleText().startsWith("cleared-screen") && terminal->historyLines() == 0; }));
        } else {
            output("new-tail-after-scroll\r\n");
            REQUIRE(waitFor([&] { return terminal->text().contains("new-tail-after-scroll"); }));
            if (scenario == "history_follow") {
                REQUIRE(click("terminalFollow"));
                REQUIRE(waitFor([&] { return surface->visibleText().contains("new-tail-after-scroll"); }));
            } else {
                REQUIRE(surface->visibleText() == before);
                REQUIRE(scenario == "history_anchor_trim" ? terminal->historyLines() == history : terminal->historyLines() > history);
            }
        }
        REQUIRE(input.isEmpty() && upgrades == 1 && freshReads == 1);
    }
    else if (scenario == "copy_selection" || scenario == "copy_tracking_selection") {
        output(scenario == "copy_tracking_selection" ? "\033[?1000h\033[?1006h\033[2J\033[HABCDE" : "\033[2J\033[HABCDE");
        REQUIRE(waitFor([&] { return surface->visibleText().startsWith("ABCDE")
            && terminal->mouseTracking() == (scenario == "copy_tracking_selection"); }));
        const double width = surface->width() / terminal->columns(), height = surface->height() / terminal->rows();
        const auto first = surface->mapToScene({width / 2, height / 2}).toPoint();
        const auto last = surface->mapToScene({width * 4.5, height / 2}).toPoint();
        const auto modifier = scenario == "copy_tracking_selection" ? Qt::ShiftModifier : Qt::NoModifier;
        QGuiApplication::clipboard()->setText("unchanged clipboard");
        QTest::mousePress(window, Qt::LeftButton, modifier, first);
        QTest::mouseMove(window, last);
        QTest::mouseRelease(window, Qt::LeftButton, modifier, last);
        REQUIRE(click("terminalCopy"));
        REQUIRE(QGuiApplication::clipboard()->text() == "ABCDE" && input.isEmpty() && inputFrames == 0 && terminal->connected());
    }
    else if (scenario == "clipboard_isolation") { QGuiApplication::clipboard()->setText("local value"); output("\033]52;c;cmVtb3Rl\007"); QTest::qWait(50); REQUIRE(QGuiApplication::clipboard()->text() == "local value"); }
    else if (scenario == "gui_modifier") {
#ifdef Q_OS_MACOS
        QTest::keyClick(window, Qt::Key_F, Qt::ControlModifier);
#else
        QTest::keyClick(window, Qt::Key_F, Qt::MetaModifier);
#endif
        QTest::qWait(30); REQUIRE(input.isEmpty() && terminal->connected() && freshReads == 1 && upgrades == 1);
    } else if (scenario == "copy_no_selection") {
        QGuiApplication::clipboard()->setText("unchanged clipboard");
#ifdef Q_OS_MACOS
        QTest::keySequence(window, QKeySequence::Copy);
#else
        QTest::keyClick(window, Qt::Key_C, Qt::ControlModifier | Qt::ShiftModifier);
#endif
        QTest::qWait(30); REQUIRE(input.isEmpty() && QGuiApplication::clipboard()->text() == "unchanged clipboard" && terminal->connected());
    } else if (scenario.startsWith("expanded_")) {
        REQUIRE(click("expandTerminal"));
        auto* dialog = window->findChild<QObject*>("expandedTerminal"); REQUIRE(dialog && waitFor([&] { return dialog->property("opened").toBool(); }));
        auto* expandedSurface = podlord::test::visibleItem(window->contentItem(), "terminalSurface"); REQUIRE(expandedSurface); expandedSurface->forceActiveFocus();
        if (scenario == "expanded_close" || scenario == "expanded_leave") {
            if (scenario == "expanded_close") REQUIRE(click("collapseTerminal"));
            else {
                QTest::keyClick(window, Qt::Key_Escape, Qt::ShiftModifier); QTest::qWait(30);
                REQUIRE(input.isEmpty() && window->activeFocusItem() != expandedSurface);
                auto* back = podlord::test::visibleItem(window->contentItem(), "collapseTerminal"); REQUIRE(back);
                for (int index = 0; index < 16 && window->activeFocusItem() != back; ++index) { QTest::keyClick(window, Qt::Key_Tab, Qt::ShiftModifier); QTest::qWait(10); }
                REQUIRE(window->activeFocusItem() == back); QTest::keyClick(window, Qt::Key_Space);
            }
            REQUIRE(waitFor([&] { return !dialog->property("visible").toBool(); }));
            REQUIRE(workspace.containerTerminal() == terminal && terminal->connected() && freshReads == 1 && upgrades == 1);
            output("\r\nSAME_STREAM_AFTER_COLLAPSE");
            REQUIRE(waitFor([&] { auto* current = podlord::test::visibleItem(window->contentItem(), "terminalSurface"); return current && current->property("visibleText").toString().contains("SAME_STREAM_AFTER_COLLAPSE"); }));
        } else {
            QTest::keyClick(window, scenario == "expanded_escape" ? Qt::Key_Escape : Qt::Key_K,
#ifdef Q_OS_MACOS
                scenario == "expanded_escape" ? Qt::NoModifier : Qt::MetaModifier);
#else
                scenario == "expanded_escape" ? Qt::NoModifier : Qt::ControlModifier);
#endif
            REQUIRE(waitFor([&] { return input == (scenario == "expanded_escape" ? QByteArray("\033") : QByteArray(1, char(11))); }));
            REQUIRE(dialog->property("visible").toBool() && inputFrames == 1 && freshReads == 1 && upgrades == 1);
        }
    } else if (scenario.startsWith("touch_")) {
        const QString control = scenario.sliced(6);
        const QMap<QString, QByteArray> expected{{"escape", "\033"}, {"tab", "\t"}, {"interrupt", QByteArray(1, char(3))}, {"eof", QByteArray(1, char(4))},
            {"left", "\033[D"}, {"right", "\033[C"}, {"up", "\033[A"}, {"down", "\033[B"}};
        if (control == "narrow") { window->resize(520, 560); QTest::qWait(30); }
        REQUIRE(click("terminalKeys"));
        if (control == "disconnected") {
            REQUIRE(click("terminalDisconnect")); REQUIRE(waitFor([&] { return !workspace.containerTerminal() || !workspace.containerTerminal()->active(); }));
            for (auto key = expected.cbegin(); key != expected.cend(); ++key) {
                auto* button = podlord::test::visibleItem(window->contentItem(), "terminalKey_" + key.key()); REQUIRE(button && !button->isEnabled());
            }
            QTest::qWait(30); REQUIRE(input.isEmpty() && inputFrames == 0); return true;
        }
        if (control == "narrow") {
            QByteArray all;
            for (auto key = expected.cbegin(); key != expected.cend(); ++key) { REQUIRE(click(qPrintable("terminalKey_" + key.key()))); all += key.value(); }
            REQUIRE(waitFor([&] { return input == all; }));
            auto* visibleSurface = podlord::test::visibleItem(window->contentItem(), "terminalSurface");
            REQUIRE(inputFrames == expected.size() && visibleSurface && visibleSurface->hasActiveFocus() && freshReads == 1 && upgrades == 1); return true;
        }
        REQUIRE(expected.contains(control)); REQUIRE(click(qPrintable("terminalKey_" + control)));
        REQUIRE(waitFor([&] { return input == expected.value(control); }));
        REQUIRE(surface->hasActiveFocus() && inputFrames == 1 && freshReads == 1 && upgrades == 1);
    } else if (scenario == "paste_empty" || scenario == "paste_oversized" || scenario == "paste_unicode_oversized") {
        const QString text = scenario == "paste_empty" ? QString{} : scenario == "paste_oversized" ? QString(65537, 'a') : QString(40000, QChar(0x65e5));
        REQUIRE(!terminal->paste(text));
        QGuiApplication::clipboard()->setText(text); REQUIRE(click("terminalPaste")); QTest::qWait(30);
        auto* confirm = podlord::test::visibleItem(window->contentItem(), "terminalPasteConfirm"); REQUIRE(confirm && !confirm->isEnabled());
        auto* error = podlord::test::visibleItem(window->contentItem(), "terminalPasteError"); REQUIRE(error && !error->property("text").toString().isEmpty());
        REQUIRE(click("terminalPasteCancel")); QTest::qWait(30); REQUIRE(input.isEmpty() && inputFrames == 0 && terminal->connected());
    } else if (scenario == "paste_unicode_limit" || scenario == "paste_ascii_limit" || scenario == "paste_bracketed") {
        const QString text = scenario == "paste_unicode_limit" ? QString(21845, QChar(0x65e5)) : scenario == "paste_ascii_limit" ? QString(65536, 'a') : QString("reviewed command\n");
        if (scenario == "paste_bracketed") { output("\033[?2004h"); QTest::qWait(30); }
        QGuiApplication::clipboard()->setText(text); REQUIRE(click("terminalPaste")); QTest::qWait(30); REQUIRE(input.isEmpty());
        REQUIRE(click("terminalPasteConfirm"));
        const QByteArray expected = scenario == "paste_bracketed" ? QByteArray("\033[200~") + text.toUtf8() + "\033[201~" : text.toUtf8();
        REQUIRE(waitFor([&] { return input == expected; })); REQUIRE(inputFrames == 1 && terminal->connected());
    } else if (scenario == "paste_cancel" || scenario == "paste_confirm" || scenario == "paste_snapshot") {
        QGuiApplication::clipboard()->setText("reviewed command"); REQUIRE(click("terminalPaste")); QTest::qWait(30); REQUIRE(input.isEmpty());
        if (scenario == "paste_snapshot") QGuiApplication::clipboard()->setText("unreviewed replacement");
        REQUIRE(click(scenario == "paste_cancel" ? "terminalPasteCancel" : "terminalPasteConfirm")); REQUIRE(waitFor([&] { return scenario == "paste_cancel" ? input.isEmpty() : input == "reviewed command"; }));
    } else if (scenario == "stderr_frame") {
        peer->sendBinaryMessage(QByteArray(1, char(2)) + "REMOTE_STDERR");
        REQUIRE(waitFor([&] { return surface->visibleText().contains("REMOTE_STDERR"); }));
        REQUIRE(terminal->connected() && upgrades == 1 && freshReads == 1);
    } else if (scenario.startsWith("status_invalid_exit_")) {
        QJsonObject cause{{"reason", "ExitCode"}, {"message", "7"}};
        if (scenario == "status_invalid_exit_reason") cause["reason"] = "Unknown";
        if (scenario == "status_invalid_exit_text") cause["message"] = "not-a-code";
        if (scenario == "status_invalid_exit_zero") cause["message"] = "0";
        if (scenario == "status_invalid_exit_large") cause["message"] = "256";
        if (scenario == "status_invalid_exit_fraction") cause["message"] = "7.5";
        const QJsonObject status{{"status", "Failure"}, {"reason", "NonZeroExitCode"},
            {"details", QJsonObject{{"causes", scenario == "status_invalid_exit_missing" ? QJsonArray{} : QJsonArray{cause}}}}};
        peer->sendBinaryMessage(QByteArray(1, char(3)) + QJsonDocument(status).toJson(QJsonDocument::Compact));
        peer->sendBinaryMessage(QByteArray::fromHex("ff01")); peer->sendBinaryMessage(QByteArray::fromHex("ff03"));
        REQUIRE(waitFor([&] { return !terminal->active(); }));
        REQUIRE(terminal->status() == "Invalid Kubernetes exec exit code.");
        QTest::qWait(100); REQUIRE(upgrades == 1 && input.isEmpty() && freshReads == 1);
    } else if (scenario == "status_array" || scenario == "status_other_failure" || scenario == "status_oversized" || scenario == "status_output_first") {
        const QByteArray status = scenario == "status_array" ? QByteArray("[]") : scenario == "status_other_failure"
            ? QByteArray("{\"status\":\"Failure\",\"reason\":\"Forbidden\"}") : scenario == "status_oversized"
            ? QByteArray(65537, 'x') : QByteArray("{\"status\":\"Success\"}");
        peer->sendBinaryMessage(QByteArray(1, char(3)) + status);
        if (scenario != "status_oversized") { peer->sendBinaryMessage(QByteArray::fromHex("ff01")); peer->sendBinaryMessage(QByteArray::fromHex("ff03")); }
        REQUIRE(waitFor([&] { return !terminal->active(); }));
        const QString expected = scenario == "status_array" ? "Invalid Kubernetes exec exit status."
            : scenario == "status_other_failure" ? "The API rejected the shell. Check the selected shell path, container and exec permissions."
            : scenario == "status_oversized" ? "Exec status exceeded the bounded response limit." : "Shell exited successfully.";
        REQUIRE(terminal->status() == expected);
        QTest::qWait(100); REQUIRE(upgrades == 1 && input.isEmpty() && freshReads == 1);
    } else if (scenario == "frame_empty" || scenario == "close_bad_size" || scenario == "close_bad_channel") {
        peer->sendBinaryMessage(scenario == "frame_empty" ? QByteArray{} : QByteArray::fromHex(scenario == "close_bad_size" ? "ff0101" : "ff05"));
        REQUIRE(waitFor([&] { return !terminal->active(); }));
        REQUIRE(!terminal->connected() && !terminal->status().isEmpty());
        QTest::qWait(100); REQUIRE(upgrades == 1 && input.isEmpty() && freshReads == 1);
    } else if (scenario == "status_success" || scenario == "status_failure" || scenario == "status_malformed" || scenario == "status_fragmented" || scenario == "status_tcp_eof") {
        QByteArray status = scenario == "status_failure" ? "{\"status\":\"Failure\",\"reason\":\"NonZeroExitCode\",\"details\":{\"causes\":[{\"reason\":\"ExitCode\",\"message\":\"7\"}]}}" : scenario == "status_malformed" ? "bad" : "{\"status\":\"Success\"}";
        if (scenario == "status_fragmented") { peer->sendBinaryMessage(QByteArray(1, char(3)) + status.first(8)); status.remove(0, 8); }
        peer->sendBinaryMessage(QByteArray(1, char(3)) + status);
        if (scenario == "status_tcp_eof") QTimer::singleShot(30, &streaming, [&] { if (peer) peer->abort(); });
        else { peer->sendBinaryMessage(QByteArray::fromHex("ff03")); peer->sendBinaryMessage(QByteArray::fromHex("ff01")); }
        REQUIRE(waitFor([&] { return !terminal->active(); })); REQUIRE(scenario == "status_failure" ? terminal->status().contains("code 7") : scenario == "status_malformed" ? terminal->status().contains("Invalid") : terminal->status() == "Shell exited successfully.");
    } else if (scenario == "bad_channel" || scenario == "text_frame" || scenario == "oversized" || scenario == "disconnect") {
        if (scenario == "bad_channel") peer->sendBinaryMessage(QByteArray::fromHex("06626164"));
        if (scenario == "text_frame") peer->sendTextMessage("not binary");
        if (scenario == "oversized") peer->sendBinaryMessage(QByteArray(1024 * 1024 + 2, 'a'));
        if (scenario == "disconnect") peer->close();
        REQUIRE(waitFor([&] { return !terminal->active(); })); REQUIRE(!terminal->connected());
    } else if (scenario == "repeat") { REQUIRE(!workspace.startContainerTerminal("alpha", "/bin/sh")); REQUIRE(upgrades == 1); }
    else if (scenario == "session_close") {
        REQUIRE(click(qPrintable("closeSession_" + workspace.currentSession())));
        REQUIRE(waitFor([&] { return !workspace.busy() && (!peer || peer->state() == QAbstractSocket::UnconnectedState); }));
        REQUIRE(workspace.currentSession().isEmpty() && !workspace.containerTerminal()); return true;
    } else if (scenario == "session_switch") {
        const auto original = workspace.currentSession(); REQUIRE(workspace.duplicateSession(original, "Independent")); REQUIRE(waitFor([&] { return !workspace.busy(); }));
        QString other;
        REQUIRE(waitFor([&] { for (const auto& entry : workspace.sessions()) { const auto session = entry.toMap(); if (session["name"] == "Independent") other = session["id"].toString(); } return !other.isEmpty(); }));
        REQUIRE(workspace.activate(other)); REQUIRE(waitFor([&] { return !workspace.busy() && workspace.currentSession() == other; }));
        REQUIRE(!workspace.containerTerminal() && peer && peer->state() == QAbstractSocket::ConnectedState);
        REQUIRE(workspace.activate(original)); REQUIRE(waitFor([&] { return !workspace.busy() && workspace.currentSession() == original; }));
        REQUIRE(workspace.containerTerminal() == terminal && terminal->connected() && upgrades == 1); REQUIRE(workspace.stopContainerTerminal()); return true;
    } else if (scenario == "reconnect") {
        REQUIRE(click("terminalDisconnect")); REQUIRE(waitFor([&] { return !peer; }));
        REQUIRE(workspace.startContainerTerminal("beta", "/bin/sh")); REQUIRE(waitFor([&] { return workspace.containerTerminal() && workspace.containerTerminal()->connected(); }));
        REQUIRE(QUrlQuery(upgradeUrl).queryItemValue("container") == "beta" && upgrades == 2); REQUIRE(workspace.stopContainerTerminal()); return true;
    }
    else if (scenario == "narrow") { window->resize(640, 680); REQUIRE(waitFor([&] { return terminal->columns() > 1 && terminal->rows() > 1; })); REQUIRE(surface->width() > 0 && surface->height() > 0); }
    else if (scenario == "hidden") { REQUIRE(workspace.setInspectorPage("overview")); output("hidden output"); REQUIRE(waitFor([&] { return terminal->text().contains("hidden output"); })); REQUIRE(workspace.setInspectorPage("terminal")); REQUIRE(waitFor([&] { return podlord::test::visibleItem(window->contentItem(), "terminalSurface") != nullptr; })); }
    else REQUIRE(scenario == "connect");
    if (terminal->active()) { REQUIRE(click("terminalDisconnect")); REQUIRE(waitFor([&] { return !peer || peer->state() == QAbstractSocket::UnconnectedState; })); REQUIRE(!workspace.containerTerminal()); }
    return true;
}
}
int main(int argc, char** argv) {
    QGuiApplication app(argc, argv); Q_INIT_RESOURCE(workspace_ui);
    if (argc < 2 || argc > 3) return 2;
    return run(QString::fromLocal8Bit(argv[1]), argc == 3 ? QString::fromLocal8Bit(argv[2]) : QString{}) ? 0 : 1;
}
