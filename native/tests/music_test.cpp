#include "workspace.h"
#include <QLockFile>
#include "music.h"
#include "ui_input.h"
#include <QAccessible>
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
#include <cmath>

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
    if (scenario == "volume") return music.volumePercent() == 35 && music.setVolumePercent(0) && music.volumePercent() == 0
        && music.setVolumePercent(100) && music.volumePercent() == 100 && !music.setVolumePercent(-1)
        && !music.setVolumePercent(101) && music.volumePercent() == 100 && music.pause() && !music.playing();
    if (scenario == "volume_persist") {
        QTemporaryDir profile;
        {
            podlord::Workspace workspace(profile.path());
            if (!waitFor([&] { return !workspace.busy(); }) || !workspace.saveAudioVolumes(62, 27)
                || !waitFor([&] { return !workspace.busy(); }) || workspace.musicVolumePercent() != 62 || workspace.alertVolumePercent() != 27
                || workspace.music()->volumePercent() != 62 || workspace.alerts()->volumePercent() != 27 || workspace.music()->playing()) return false;
        }
        podlord::Workspace restarted(profile.path());
        return waitFor([&] { return !restarted.busy(); }) && restarted.musicVolumePercent() == 62 && restarted.alertVolumePercent() == 27
            && restarted.music()->volumePercent() == 62 && restarted.alerts()->volumePercent() == 27 && !restarted.music()->playing();
    }
    if (scenario == "volume_profiles") {
        QTemporaryDir profile, separate;
        podlord::Workspace first(profile.path()), isolated(separate.path());
        if (!waitFor([&] { return !first.busy() && !isolated.busy(); }) || !first.saveAudioVolumes(48, 19)) return false;
        return waitFor([&] { return !first.busy() && first.musicVolumePercent() == 48 && first.alertVolumePercent() == 19; })
            && first.music()->volumePercent() == 48 && first.alerts()->volumePercent() == 19
            && isolated.musicVolumePercent() == 35 && isolated.alertVolumePercent() == 100 && !first.music()->playing();
    }
    if (scenario == "volume_preview") {
        QTemporaryDir profile;
        podlord::Workspace workspace(profile.path());
        if (!waitFor([&] { return !workspace.busy() && !workspace.alerts()->busy(); })) return false;
        const auto track = workspace.music()->currentId();
        if (!workspace.previewAudioVolume(true) || !workspace.previewAudioVolume(false)
            || workspace.music()->playing() || workspace.music()->currentId() != track
            || !workspace.alerts()->setPreferences(true, workspace.alerts()->reducedMotion())
            || !waitFor([&] { return !workspace.alerts()->busy(); })) return false;
        return !workspace.previewAudioVolume(true) && !workspace.previewAudioVolume(false) && !workspace.music()->playing();
    }
    if (scenario.startsWith("volume_ui")) {
        QAccessible::setActive(true);
        QTemporaryDir profile;
        podlord::Workspace workspace(profile.path());
        QSignalSpy requests(&workspace, &podlord::Workspace::requestStarted);
        QSignalSpy saves(&workspace, &podlord::Workspace::audioVolumesSaveFinished);
        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty("workspace", &workspace);
        engine.load(QUrl("qrc:/podlord/Main.qml"));
        auto* window = engine.rootObjects().isEmpty() ? nullptr : qobject_cast<QQuickWindow*>(engine.rootObjects().first());
        if (!window || !waitFor([&] { return !workspace.busy(); })) return false;
        if (scenario == "volume_ui_light" && (!workspace.saveAppearance(workspace.themeName(), "light") || !waitFor([&] { return !workspace.busy(); }))) return false;
        window->resize(scenario == "volume_ui_narrow" ? QSize(320, 640) : QSize(1200, 900));
        if (!podlord::test::clickVisible(window, "settingsWorkspaceButton") || !podlord::test::selectSettingsSection(window, "appearance")) return false;
        auto* musicSlider = window->findChild<QQuickItem*>("settingsMusicVolume");
        auto* alertSlider = window->findChild<QQuickItem*>("settingsAlertVolume");
        if (!musicSlider || !alertSlider || !podlord::test::scrollIntoView(window, musicSlider)) return false;
        const bool repeated = scenario == "volume_ui_repeat";
        const bool mixed = scenario == "volume_ui_mixed";
        const bool leaving = scenario == "volume_ui_leave";
        const bool locked = scenario == "volume_ui_locked";
        QLockFile volumeLock(profile.filePath("read-settings.json.lock"));
        if (locked && !volumeLock.tryLock(0)) return false;
        const int expectedMusic = scenario == "volume_ui_pointer" ? 70 : repeated || leaving ? 45 : mixed ? 43 : 36;
        const int expectedAlert = repeated ? 90 : mixed ? 92 : 99;
        if (scenario == "volume_ui_pointer") {
            auto* handle = musicSlider->property("handle").value<QQuickItem*>();
            if (!handle) return false;
            const qreal left = musicSlider->property("leftPadding").toDouble();
            const qreal available = musicSlider->property("availableWidth").toDouble();
            const auto point = musicSlider->mapToScene({left + handle->width()/2 + 0.7*(available-handle->width()), musicSlider->height()/2}).toPoint();
            QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, point);
        } else {
            for (int step=0; step<(repeated || leaving ? 10 : mixed ? 8 : locked ? 2 : 1); ++step) {
                musicSlider->forceActiveFocus(); QTest::keyClick(window, Qt::Key_Right);
                if (mixed) { alertSlider->forceActiveFocus(); QTest::keyClick(window, Qt::Key_Left); }
            }
        }
        if (locked) {
            if (!waitFor([&] { return !workspace.busy() && !workspace.error().isEmpty() && saves.count()==1; })) return false;
            QTest::qWait(600);
            return saves.count()==1 && !saves.first().first().toBool() && !workspace.busy()
                && workspace.musicVolumePercent()==35 && workspace.alertVolumePercent()==100
                && musicSlider->property("value").toInt()==35 && workspace.music()->volumePercent()==35
                && !workspace.music()->playing() && requests.isEmpty();
        }
        if (leaving && !podlord::test::clickVisible(window,"mapWorkspaceButton")) return false;
        if (!waitFor([&] { return !workspace.busy() && workspace.musicVolumePercent() == expectedMusic
                && (!mixed || workspace.alertVolumePercent() == expectedAlert); })
            || workspace.alertVolumePercent() != (mixed ? expectedAlert : 100) || !podlord::test::scrollIntoView(window, alertSlider)) {
            std::fprintf(stderr,"Volume input lost: music=%d/%d alerts=%d, slider=%g\n",workspace.musicVolumePercent(),expectedMusic,
                workspace.alertVolumePercent(),musicSlider->property("value").toDouble());
            return false;
        }
        if (leaving) {
            const auto persisted=podlord::ReadSettingsStore(profile.path()).load();
            const auto* settings=std::get_if<podlord::ReadSettings>(&persisted);
            return settings && settings->musicVolumePercent==45 && settings->alertVolumePercent==100
                && workspace.workspacePage()=="map" && !workspace.music()->playing() && requests.isEmpty();
        }
        if (!mixed) {
            alertSlider->forceActiveFocus();
            for (int step=0; step<(repeated ? 10 : 1); ++step) QTest::keyClick(window, Qt::Key_Left);
        }
        if (!waitFor([&] { return !workspace.busy() && workspace.alertVolumePercent() == expectedAlert; })) return false;
        QTest::qWait(350);
        auto* accessible = QAccessible::queryAccessibleInterface(musicSlider);
        auto* range = accessible ? accessible->valueInterface() : nullptr;
        if (!accessible || accessible->role() != QAccessible::Slider || accessible->text(QAccessible::Name) != workspace.uiText()["settings.musicVolume"].toString()
            || !range || range->minimumValue().toInt() != 0 || range->maximumValue().toInt() != 100 || range->currentValue().toInt() != expectedMusic) return false;
        const auto luminance = [](const QColor& color) {
            const auto linear = [](double value) { return value <= 0.04045 ? value/12.92 : std::pow((value+0.055)/1.055, 2.4); };
            return 0.2126*linear(color.redF()) + 0.7152*linear(color.greenF()) + 0.0722*linear(color.blueF());
        };
        auto* handle = musicSlider->property("handle").value<QQuickItem*>();
        if (!handle) return false;
        const auto actual = podlord::test::centerPixel(window, handle);
        const auto panel = workspace.appearanceColors()["panel"].value<QColor>();
        const double foreground = luminance(actual), background = luminance(panel);
        if (!actual.isValid() || (std::max(foreground, background)+0.05)/(std::min(foreground, background)+0.05) < 3.0) return false;
        const auto frame = qEnvironmentVariable("PODLORD_MUSIC_FRAME");
        return requests.isEmpty() && !workspace.music()->playing() && workspace.music()->volumePercent() == expectedMusic
            && workspace.alerts()->volumePercent() == expectedAlert && musicSlider->height() >= 44 && alertSlider->height() >= 44
            && (frame.isEmpty() || window->grabWindow().save(frame));
    }
    if (scenario == "disabled") return !music.play() && !music.playing() && music.pause();
    if (scenario == "playback") {
        music.setMuted(true);
        if (!music.play() || !waitFor([&] { return music.playing() && music.error().isEmpty(); })) return false;
        QTest::qWait(1500);
        if (!music.playing() || !music.error().isEmpty() || !music.next()) return false;
        if (!music.setVolumePercent(22) || music.volumePercent() != 22) return false;
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
