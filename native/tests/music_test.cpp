#include "workspace.h"
#include "music.h"
#include "ui_input.h"
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QQuickStyle>
#include <QSignalSpy>
#include <QSet>
#include <QTemporaryDir>
#include <QTest>
#include <cstdio>

namespace {
bool waitFor(const std::function<bool()>& ready) {
    QElapsedTimer elapsed; elapsed.start();
    while (!ready() && elapsed.elapsed() < 5000) QTest::qWait(10);
    return ready();
}
QQuickItem* visibleItem(QQuickItem* parent, const QString& name) {
    if (parent->objectName() == name && parent->isVisible()) return parent;
    for (auto* child : parent->childItems()) if (auto* found = visibleItem(child, name)) return found;
    return nullptr;
}
bool click(QQuickWindow* window, const QString& name) {
    auto* control = visibleItem(window->contentItem(), name);
    if (!control || !control->isEnabled() || !podlord::test::scrollIntoView(window, control)) return false;
    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, control->mapToScene({control->width()/2, control->height()/2}).toPoint());
    return true;
}
bool run(const QString& scenario) {
    podlord::Music music;
    const auto tracks = podlord::bundledTracks();
    if (tracks.size() != 20 || music.playing() || music.currentId().isEmpty()) return false;
    if (scenario == "startup") { QTest::qWait(100); podlord::Music restarted; return !restarted.playing() && !restarted.currentId().isEmpty(); }
    if (scenario == "shuffle") {
        QSet<QString> expected;
        for (const auto& track : tracks) expected.insert(track.toMap()["id"].toString());
        QString previous;
        for (int cycle = 0; cycle < 3; ++cycle) {
            QSet<QString> heard;
            for (int index = 0; index < tracks.size(); ++index) {
                const auto current = music.currentId();
                if (!expected.contains(current) || heard.contains(current) || previous == current) return false;
                heard.insert(current); previous = current;
                if (!music.next() || music.playing()) return false;
            }
            if (heard != expected) return false;
        }
        return true;
    }
    if (scenario == "mute") { music.setMuted(true); music.next(); return music.muted() && !music.playing(); }
    if (scenario == "disabled") return !music.play() && !music.playing() && music.pause();
    if (scenario == "playback") {
        music.setMuted(true);
        if (!music.play() || !waitFor([&] { return music.playing() && music.error().isEmpty(); })) return false;
        QTest::qWait(1500);
        if (!music.playing() || !music.error().isEmpty() || !music.next()) return false;
        QTest::qWait(300);
        if (!music.playing() || !music.error().isEmpty() || !music.pause() || music.playing() || !music.play()) return false;
        QTest::qWait(300);
        return music.playing() && music.error().isEmpty() && music.pause() && !music.playing();
    }
    if (scenario == "ui" || scenario == "ui_narrow") {
        QTemporaryDir profile;
        podlord::Workspace workspace(profile.path());
        QSignalSpy requests(&workspace, &podlord::Workspace::requestStarted);
        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty("workspace", &workspace);
        engine.load(QUrl("qrc:/podlord/Main.qml"));
        if (engine.rootObjects().isEmpty()) return false;
        auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
        if (!window) return false;
        window->resize(scenario == "ui_narrow" ? 320 : 1440, 640);
        if (!waitFor([&] { return !workspace.busy(); })) return false;
        const bool overflow = !visibleItem(window->contentItem(), "workspaceMusicButton");
        if (overflow && !click(window, "workspaceActionsButton")) return false;
        if (!waitFor([&] { return visibleItem(window->contentItem(), "workspaceMusicButton"); })) return false;
        auto* actions = window->findChild<QObject*>("workspaceActionsMenu");
        if (overflow && (!actions || !waitFor([&] { return actions->property("opened").toBool(); }))) return false;
        auto* button = visibleItem(window->contentItem(), "workspaceMusicButton");
        const auto bounds = button->mapRectToScene({0, 0, button->width(), button->height()});
        if (button->property("text") != workspace.uiText()["music.play"] || bounds.left() < 0 || bounds.top() < 0
            || bounds.right() > window->width() || bounds.bottom() > window->height() || button->height() < (scenario == "ui_narrow" ? 44 : 32)) return false;
        if (window->findChild<QObject*>("musicMenu") || visibleItem(window->contentItem(), "musicTrack")) return false;
        const auto frame = qEnvironmentVariable("PODLORD_MUSIC_FRAME");
        if (!frame.isEmpty() && !window->grabWindow().save(frame)) return false;
        return click(window, "workspaceMusicButton") && !workspace.music()->playing() && requests.isEmpty();
    }
    return false;
}
}
int main(int argc, char** argv) {
    QGuiApplication app(argc, argv);
    if (!qEnvironmentVariableIsSet("QT_QUICK_CONTROLS_STYLE")) QQuickStyle::setStyle("Fusion");
    if (argc != 2) return 2;
    const bool passed = run(QString::fromLocal8Bit(argv[1]));
    if (!passed) std::fprintf(stderr, "Music scenario failed: %s\n", argv[1]);
    return passed ? 0 : 1;
}
