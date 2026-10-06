#include "workspace.h"
#include "ui_input.h"
#include <QGuiApplication>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QJsonDocument>
#include <QJsonArray>
#include <QPalette>
#include <QLockFile>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickWindow>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QTest>
#include <QSignalSpy>
#include <cstdio>
#include <stdexcept>

namespace {
bool require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
    return true;
}
bool click(QQuickWindow* window, const QString& name) {
    auto* target = window->findChild<QQuickItem*>(name);
    if (!target || !target->isVisible() || !target->isEnabled()) return false;
    QList<QQuickItem*> ancestors;
    for (auto* item = target; item; item = item->parentItem()) ancestors.prepend(item);
    for (auto* item : ancestors) item->ensurePolished();
    QCoreApplication::processEvents();
    if (!podlord::test::scrollIntoView(target->window(), target)) return false;
    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
        target->mapToScene(QPointF(target->width() / 2, target->height() / 2)).toPoint());
    return true;
}
bool choose(QQuickWindow* window, podlord::Workspace& workspace, const QString& name, int index) {
    require(click(window, name), "Cannot open appearance selector");
    QTest::keyClick(window, Qt::Key_Home);
    for (int row = 0; row < index; ++row) QTest::keyClick(window, Qt::Key_Down);
    QTest::keyClick(window, Qt::Key_Return);
    require(QTest::qWaitFor([&] { return !workspace.busy(); }, 5000), "Appearance save did not settle");
    return true;
}
QList<QColor> legacyColors(const QString& path, const QString& name, const QString& variant, int& count) {
    QFile source(path);
    require(source.open(QIODevice::ReadOnly), "Cannot read preserved theme definitions");
    const QString text = QString::fromUtf8(source.readAll());
    const QRegularExpression entries(R"theme(Theme\(\s*"([^"]+)"\s*,\s*"[^"]+"\s*,\s*"[^"]+"\s*,\s*BuildPalette\(([^)]+)\)\s*,\s*BuildPalette\(([^)]+)\))theme");
    auto matches = entries.globalMatch(text);
    QList<QColor> result;
    count = 0;
    while (matches.hasNext()) {
        const auto entry = matches.next(); ++count;
        if (entry.captured(1) != name) continue;
        const QRegularExpression colors("#[A-Fa-f0-9]{6}");
        auto values = colors.globalMatch(entry.captured(variant == "light" ? 3 : 2));
        while (values.hasNext()) result.append(QColor(values.next().captured()));
    }
    require(result.size() == 17, "Expected an exact shipped palette");
    return result;
}
QByteArray read(const QString& path) {
    QFile file(path);
    require(file.open(QIODevice::ReadOnly), "Cannot read settings result");
    return file.readAll();
}
bool exercise(const QStringList& arguments) {
    require(arguments.size() >= 3, "Expected scenario and preserved theme source");
    const QString scenario = arguments[1], legacy = arguments[2];
    QTemporaryDir temporary;
    require(temporary.isValid(), "Cannot create isolated appearance profile");
    const QString profile = temporary.filePath("profile");
    podlord::Workspace workspace(profile);
    QQmlApplicationEngine engine;
    QSignalSpy warnings(&engine, &QQmlApplicationEngine::warnings);
    engine.rootContext()->setContextProperty("workspace", &workspace);
    engine.load(QUrl("qrc:/podlord/Main.qml"));
    require(!engine.rootObjects().isEmpty(), "Cannot create actual appearance UI");
    auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
    require(window && QTest::qWaitFor([&] { return !workspace.busy(); }, 5000), "Initial appearance settings did not settle");
    const QStringList names = workspace.property("themeNames").toStringList();
    require(!names.isEmpty(), "The shipped theme catalog is not exposed");
    require(workspace.property("themeName").toString() == "Sirocco Command"
        && workspace.property("themeVariant").toString() == "dark"
        && workspace.property("themeIntensity").toString() == "subtle", "Legacy appearance defaults were not retained");
    require(click(window, "settingsWorkspaceButton") && click(window, "settingsAppearanceSection"), "Appearance settings are missing from the public UI");
    if (scenario == "invalid_name" || scenario == "empty_name" ||
        scenario == "invalid_variant" || scenario == "invalid_intensity" ||
        scenario == "repeat") {
        const auto originalPalette = workspace.appearancePalette();
        const auto name = scenario == "invalid_name" ? QStringLiteral("Unknown Theme") :
                          scenario == "empty_name" ? QString{} : workspace.themeName();
        const auto variant = scenario == "invalid_variant" ? QStringLiteral("automatic") :
                             workspace.themeVariant();
        const auto intensity = scenario == "invalid_intensity" ? QStringLiteral("animated") :
                               workspace.themeIntensity();
        require(workspace.saveAppearance(name, variant, intensity) == (scenario == "repeat"),
                "Appearance validation or repeated selection returned the wrong result");
        require(workspace.appearancePalette() == originalPalette &&
                    workspace.themeName() == "Sirocco Command" &&
                    workspace.themeVariant() == "dark" &&
                    workspace.themeIntensity() == "subtle",
                "Rejected or repeated appearance changed the active theme");
        require(!QDir(profile).exists(), "Rejected or repeated appearance created profile data");
        if (scenario != "repeat")
            require(!workspace.property("error").toString().isEmpty(),
                    "Rejected appearance did not report a visible error");
        return true;
    }
    if (scenario == "leave_unchanged") {
        return require(click(window, "resourcesWorkspaceButton") && !QFileInfo::exists(profile), "Leaving unchanged appearance settings wrote data");
    }
    if (scenario == "busy" || scenario == "conflict") {
        require(QDir().mkpath(profile), "Cannot establish appearance failure boundary");
        std::optional<QLockFile> lock;
        if (scenario == "busy") {
            lock.emplace(profile + "/read-settings.json.lock");
            require(lock->tryLock(0), "Cannot establish settings lock contention");
        } else {
            const auto result = podlord::ReadSettingsStore(profile).save({120, 2, 7, 4}, {});
            require(std::holds_alternative<podlord::ReadSettings>(result), "Cannot establish another window's settings");
        }
        choose(window, workspace, "inlineAppearanceTheme", 3);
        require(workspace.property("themeName").toString() == "Sirocco Command" && !workspace.error().isEmpty(), "Failed save changed the active theme or hid its error");
        require(window->findChild<QObject*>("inlineAppearanceTheme")->property("currentIndex").toInt() == 1, "Failed save left a misleading selected theme");
        return true;
    }
    QString selected = "Gunmetal Sector", variant = "light", intensity = "arcade";
    if (scenario == "palette") {
        require(arguments.size() == 6, "Expected palette, variant and intensity");
        selected = arguments[3]; variant = arguments[4]; intensity = arguments[5];
    }
    require(names.contains(selected), "Shipped theme is missing");
    choose(window, workspace, "inlineAppearanceTheme", static_cast<int>(names.indexOf(selected)));
    choose(window, workspace, "inlineAppearanceVariant", variant == "light" ? 1 : 0);
    choose(window, workspace, "inlineAppearanceIntensity", QStringList{"subtle", "medium", "arcade"}.indexOf(intensity));
    require(workspace.property("themeName").toString() == selected && workspace.property("themeVariant").toString() == variant
        && workspace.property("themeIntensity").toString() == intensity, "Public selection did not apply appearance settings");
    int count = 0;
    const auto colors = legacyColors(legacy, selected, variant, count);
    require(names.size() == count, "Native and legacy theme inventories differ");
    const auto palette = workspace.property("appearancePalette").value<QPalette>();
    require(palette.color(QPalette::Window) == colors[0] && palette.color(QPalette::Base) == colors[3]
        && palette.color(QPalette::Button) == colors[2] && palette.color(QPalette::Text) == colors[9]
        && palette.color(QPalette::Accent) == colors[6], "Qt palette does not preserve shipped semantic colors");
    auto* field = window->findChild<QObject*>("sourcePath");
    auto* controlPalette = field ? field->property("palette").value<QObject*>() : nullptr;
    require(controlPalette && controlPalette->property("base").value<QColor>() == colors[3]
        && controlPalette->property("text").value<QColor>() == colors[9], "Actual Qt control palette did not follow the selected theme");
    require(controlPalette->property("placeholderText").value<QColor>() == colors[10], "Actual input placeholder did not inherit the readable theme color");
    const auto surfaces = workspace.property("appearanceColors").toMap();
    const QStringList colorNames{"app", "panel", "raised", "inset", "border", "strongBorder", "accent", "accentMuted", "accentGlow", "text", "muted", "success", "warning", "danger", "unknown", "radarShell", "radarGlass"};
    for (int index = 0; index < colors.size(); ++index)
        require(surfaces[colorNames[index]].value<QColor>() == colors[index], "A shipped semantic surface color changed");
    require(!surfaces["texture"].toString().isEmpty(), "Intensity has no visible texture output");
    if (QFileInfo::exists(profile + "/read-settings.json")) {
        const auto settings = QJsonDocument::fromJson(read(profile + "/read-settings.json")).object();
        require(settings["themeName"] == selected && settings["themeVariant"] == variant && settings["themeIntensity"] == intensity, "Appearance selection was not persisted");
    } else require(selected == "Sirocco Command" && variant == "dark" && intensity == "subtle", "Nondefault appearance was not persisted");
    require(workspace.contexts().isEmpty() && workspace.currentSession().isEmpty(), "Appearance changed cluster/session selection");
    if (scenario == "restore") {
        podlord::Workspace reopened(profile);
        require(QTest::qWaitFor([&] { return !reopened.busy(); }, 5000), "Restored settings did not settle");
        require(reopened.property("themeName").toString() == selected && reopened.property("themeVariant").toString() == variant
            && reopened.property("themeIntensity").toString() == intensity, "Restart lost appearance choices");
    }
    if (scenario == "preserve") {
        require(workspace.saveReadSettings(120, 2, "7"), "Cannot save read policy after appearance");
        require(QTest::qWaitFor([&] { return !workspace.busy(); }, 5000), "Read-policy save did not settle");
        const auto result = podlord::ReadSettingsStore(profile).load();
        require(std::holds_alternative<podlord::ReadSettings>(result), "Cannot read combined settings");
        const auto persisted = QJsonDocument::fromJson(read(profile + "/read-settings.json")).object();
        require(persisted["themeName"] == selected && persisted["themeVariant"] == variant && persisted["themeIntensity"] == intensity
            && persisted["requestHardLimitPerMinute"] == 120 && persisted["logLimitMb"] == 7, "Policy save overwrote appearance");
    }
    const QString screenshotDirectory = qEnvironmentVariable("PODLORD_THEME_SCREENSHOT_DIR");
    if (!screenshotDirectory.isEmpty()) {
        require(QDir().mkpath(screenshotDirectory), "Cannot create screenshot evidence directory");
        require(QTest::qWaitFor([&] { return !window->grabWindow().isNull(); }, 5000), "Theme surface did not render");
        const QString name = selected.toLower().replace(' ', '-') + '-' + variant + '-' + intensity;
        require(window->grabWindow().save(QDir(screenshotDirectory).filePath(name + ".png")), "Cannot capture rendered theme");
    }
    require(warnings.isEmpty(), "The actual theme UI reported a QML warning");
    return true;
}
} // namespace
int main(int argc, char** argv) {
    const QGuiApplication app(argc, argv);
    try { return exercise(app.arguments()) ? 0 : 1; }
    catch (const std::exception& error) { std::fprintf(stderr, "%s\n", error.what()); return 1; }
}
