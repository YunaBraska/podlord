#include "workspace.h"
#include "browser_boundary.h"
#include "ui_input.h"
#include <QClipboard>
#include <QDesktopServices>
#include <QElapsedTimer>
#include <QFile>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QWheelEvent>
#include <cstdio>

namespace {
bool waitFor(const std::function<bool()>& ready) {
    QElapsedTimer elapsed; elapsed.start();
    while (!ready() && elapsed.elapsed() < 5000) QTest::qWait(10);
    return ready();
}
QQuickItem* item(QQuickItem* root, const QString& name) {
    if (root->objectName() == name) return root;
    for (auto* child : root->childItems()) if (auto* found = item(child, name)) return found;
    return nullptr;
}
bool click(QQuickWindow* window, const QString& name) {
    auto* control = item(window->contentItem(), name);
    if (!control || !control->isVisible() || !control->isEnabled()) return false;
    if (!podlord::test::scrollIntoView(window, control)) return false;
    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, control->mapToScene({control->width() / 2, control->height() / 2}).toPoint());
    return true;
}
bool run(const QString& scenario) {
    QTemporaryDir profile; if (!profile.isValid()) return false;
    podlord::Workspace workspace(profile.path());
    QSignalSpy requests(&workspace, &podlord::Workspace::requestStarted);
    QQmlApplicationEngine engine; engine.rootContext()->setContextProperty("workspace", &workspace); engine.load(QUrl("qrc:/podlord/Main.qml"));
    if (engine.rootObjects().isEmpty()) return false;
    auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
    if (!window || !waitFor([&] { return !workspace.busy(); }) || !click(window, "settingsWorkspaceButton") || !click(window, "settingsAboutSection")) return false;
    BrowserBoundary browser;
    if (scenario == "credits") {
        if (item(window->contentItem(), "aboutSoundSource_interface-sounds") || !click(window, "aboutSoundCredits")) return false;
        if (!waitFor([&] { return item(window->contentItem(), "aboutSoundSource_interface-sounds"); })) return false;
        if (!click(window, "aboutSoundCredits")) return false;
        return waitFor([&] { return !item(window->contentItem(), "aboutSoundSource_interface-sounds"); }) && requests.isEmpty() && browser.urls.isEmpty();
    }
    const QMap<QString, QPair<QString, QString>> links{
        {"project", {"aboutProject", "https://github.com/YunaBraska/podlord"}},
        {"issues", {"aboutIssues", "https://github.com/YunaBraska/podlord/issues/new"}},
        {"star", {"aboutStar", "https://github.com/YunaBraska/podlord/stargazers"}},
        {"sponsors", {"aboutSponsors", "https://github.com/sponsors/YunaBraska"}},
        {"coffee", {"aboutCoffee", "https://buymeacoffee.com/YunaBraska"}},
        {"kofi", {"aboutKofi", "https://ko-fi.com/YunaBraska"}},
        {"liberapay", {"aboutLiberapay", "https://liberapay.com/YunaBraska"}},
        {"sound_warning", {"aboutSoundSource_interface-sounds", "https://kenney.nl/assets/interface-sounds"}},
        {"sound_critical", {"aboutSoundSource_sci-fi-sounds", "https://kenney.nl/assets/sci-fi-sounds"}},
        {"sound_ui", {"aboutSoundSource_ui-audio", "https://kenney.nl/assets/ui-audio"}},
        {"sound_music", {"aboutSoundSource_music-jingles", "https://kenney.nl/assets/music-jingles"}},
        {"sound_digital", {"aboutSoundSource_digital-audio", "https://kenney.nl/assets/digital-audio"}},
        {"sound_impact", {"aboutSoundSource_impact-sounds", "https://kenney.nl/assets/impact-sounds"}},
        {"sound_rpg", {"aboutSoundSource_rpg-audio", "https://kenney.nl/assets/rpg-audio"}}};
    if (scenario == "branding" || scenario == "branding_narrow") {
        if (scenario.endsWith("_narrow")) window->resize(360, 600);
        auto* logo = item(window->contentItem(), "aboutLogo");
        auto* tagline = item(window->contentItem(), "aboutTagline");
        if (!logo || !tagline || !waitFor([&] { return logo->property("status").toInt() == 1; })) return false;
        for (auto* control : {logo, tagline}) {
            if (!podlord::test::scrollIntoView(window, control)) return false;
            const auto bounds = control->mapRectToScene({0, 0, control->width(), control->height()});
            if (bounds.left() < 0 || bounds.right() > window->width() || bounds.top() < 0 || bounds.bottom() > window->height()) return false;
        }
        return tagline->property("text").toString() == workspace.uiText()["about.tagline"].toString()
            && click(window, "aboutLiberapay") && browser.urls == QList<QUrl>{QUrl("https://liberapay.com/YunaBraska")}
            && requests.isEmpty();
    }
    if (links.contains(scenario)) {
        if (scenario.startsWith("sound_") && !click(window, "aboutSoundCredits")) return false;
        const auto [name, url] = links.value(scenario);
        return click(window, name) && waitFor([&] { return browser.urls.size() == 1; }) && browser.urls.first() == QUrl(url) && requests.isEmpty();
    }
    if (scenario.startsWith("license") || scenario.startsWith("notices")) {
        const bool notices = scenario.startsWith("notices");
        const auto controlName = notices ? QStringLiteral("aboutDependencyNotices") : QStringLiteral("aboutLicense");
        if (scenario.endsWith("_narrow")) {
            window->setWidth(680); window->setHeight(480);
            if (!waitFor([&] { return window->contentItem()->width() == 680 && window->contentItem()->height() == 480; })) {
                std::fprintf(stderr, "Resize did not reach the scene: window=%dx%d scene=%gx%g\n", window->width(), window->height(), window->contentItem()->width(), window->contentItem()->height());
                return false;
            }
            window->grabWindow();
            auto* control = item(window->contentItem(), controlName);
            if (!control || !podlord::test::scrollIntoView(window, control)) return false;
            const auto bounds = control->mapRectToScene({0, 0, control->width(), control->height()});
            if (bounds.left() < 0 || bounds.top() < 0 || bounds.right() > window->width() || bounds.bottom() > window->height()) {
                std::fprintf(stderr, "About notice control outside scene: x=%g y=%g width=%g height=%g\n", bounds.x(), bounds.y(), bounds.width(), bounds.height()); return false;
            }
        }
        if (scenario == "notices_keyboard") {
            if (!click(window, controlName)) return false;
            auto* dialog = window->findChild<QObject*>("aboutLicenseDialog");
            if (!dialog || !waitFor([&] { return dialog->property("opened").toBool(); })) {
                std::fprintf(stderr, "Dependency notice dialog did not finish opening.\n");
                return false;
            }
            auto* closingText = item(window->contentItem(), "aboutLicenseText");
            if (!closingText) return false;
            QTest::keyClick(window, Qt::Key_Escape);
            if (!waitFor([&] { return !dialog->property("visible").toBool() && !closingText->isVisible() && closingText->property("text").toString().isEmpty(); })) {
                std::fprintf(stderr, "Dependency notice dialog did not finish closing.\n");
                return false;
            }
            auto* control = item(window->contentItem(), controlName);
            control->forceActiveFocus(Qt::TabFocusReason);
            QTest::keyClick(window, Qt::Key_Space);
        } else if (!click(window, controlName)) return false;
        if (!waitFor([&] { auto* dialog = window->findChild<QObject*>("aboutLicenseDialog"); return dialog && dialog->property("opened").toBool(); })) {
            auto* dialog = window->findChild<QObject*>("aboutLicenseDialog");
            std::fprintf(stderr, "About dialog did not open: exists=%d visible=%d opened=%d\n", dialog != nullptr, dialog && dialog->property("visible").toBool(), dialog && dialog->property("opened").toBool()); return false;
        }
        if (!waitFor([&] { auto* text = item(window->contentItem(), "aboutLicenseText"); return text && text->isVisible(); })) return false;
        auto* text = item(window->contentItem(), "aboutLicenseText");
        if (notices) {
            for (const auto* name : {"THIRD-PARTY-NOTICES.txt", "LGPL-3.0.txt", "GPL-3.0.txt", "LGPL-2.1.txt", "OpenSSL-LICENSE.txt", "yaml-cpp-LICENSE.txt", "libvterm-LICENSE.txt"}) {
                QFile original(QString::fromUtf8(PODLORD_TEST_NOTICES_DIR) + '/' + QString::fromLatin1(name));
                if (!original.open(QIODevice::ReadOnly) || !text->property("text").toString().contains(QString::fromUtf8(original.readAll()))) { std::fprintf(stderr, "Missing rendered notice: %s\n", name); return false; }
            }
        } else {
            QFile original(QString::fromUtf8(PODLORD_TEST_LICENSE));
            if (!original.open(QIODevice::ReadOnly) || text->property("text").toString().toUtf8() != original.readAll()) return false;
        }
        if (scenario.endsWith("_copy")) {
            text->forceActiveFocus(); QTest::keySequence(window, QKeySequence::SelectAll); QTest::keySequence(window, QKeySequence::Copy);
            if (QGuiApplication::clipboard()->text() != text->property("text").toString()) return false;
        }
        if (scenario.endsWith("_cancel")) {
            QTest::keyClick(window, Qt::Key_Escape);
            if (!waitFor([&] { return !text->isVisible() && text->property("text").toString().isEmpty(); })) return false;
        }
        if (scenario.endsWith("_narrow")) {
            const auto* scroll = item(window->contentItem(), "aboutLicenseScroll"); if (!scroll) return false;
            const auto bounds = scroll->mapRectToScene({0, 0, scroll->width(), scroll->height()});
            if (bounds.left() < 0 || bounds.top() < 0 || bounds.right() > window->width() || bounds.bottom() > window->height()) {
                std::fprintf(stderr, "About notice viewport outside scene: x=%g y=%g width=%g height=%g\n", bounds.x(), bounds.y(), bounds.width(), bounds.height()); return false;
            }
            if (const auto directory = qEnvironmentVariable("PODLORD_TEST_EVIDENCE_DIR"); !directory.isEmpty())
                if (!window->grabWindow().save(directory + "/about-" + scenario + '-' + qEnvironmentVariable("QT_QUICK_CONTROLS_STYLE") + ".png")) return false;
        }
        if (scenario == "notices_reopen_project") {
            QTest::keyClick(window, Qt::Key_Escape);
            if (!waitFor([&] { return !text->isVisible(); }) || !click(window, "aboutLicense")) return false;
            QFile original(QString::fromUtf8(PODLORD_TEST_LICENSE));
            if (!original.open(QIODevice::ReadOnly) || text->property("text").toString().toUtf8() != original.readAll()) return false;
        }
        return requests.isEmpty() && browser.urls.isEmpty();
    }
    if (scenario == "idle") {
        QTest::qWait(250);
        return browser.urls.isEmpty() && requests.isEmpty() && item(window->contentItem(), "aboutLicense");
    }
    return false;
}
}
int main(int argc, char** argv) {
    QGuiApplication app(argc, argv); Q_INIT_RESOURCE(workspace_ui);
    const bool success = argc == 2 && run(QString::fromLocal8Bit(argv[1]));
    if (!success) std::fprintf(stderr, "About UI scenario failed: %s\n", argc == 2 ? argv[1] : "missing scenario");
    return success ? 0 : 1;
}
