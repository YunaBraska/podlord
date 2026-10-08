#include "workspace.h"
#include "ui_language.h"
#include "ui_input.h"
#include <QDir>
#include <QFile>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLockFile>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <cstdio>
#include <stdexcept>

namespace {
bool require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
    return true;
}
QByteArray read(const QString& path) {
    QFile file(path);
    require(file.open(QIODevice::ReadOnly), "Cannot read language evidence.");
    return file.readAll();
}
bool write(const QString& path, const QByteArray& bytes) {
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}
bool click(QQuickWindow* window, const QString& name) {
    auto* target = podlord::test::visibleItem(window->contentItem(), name);
    require(target && target->isEnabled() && podlord::test::scrollIntoView(window, target), "Language control is not reachable.");
    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, target->mapToScene({target->width() / 2, target->height() / 2}).toPoint());
    return true;
}
bool scenario(const QStringList& args) {
    require(args.size() >= 2, "Expected a language scenario.");
    const auto mode = args[1];
    if (mode == "catalog") {
        require(args.size() == 3, "Expected the exported reference catalog.");
        const auto root = QJsonDocument::fromJson(read(args[2])).object();
        const auto english = root["english"].toObject().toVariantMap();
        require(!english.isEmpty(), "Reference catalog is empty.");
        QVariantList options;
        for (const auto& item : root["languages"].toArray()) {
            const auto language = item.toObject();
            const auto code = language["code"].toString();
            options.append(QVariantMap{{"code", code}, {"name", language["name"].toString()}});
            if (code == "system") continue;
            auto expected = english;
            const auto translated = language["text"].toObject().toVariantMap();
            for (auto text = translated.cbegin(); text != translated.cend(); ++text) expected.insert(text.key(), text.value());
            require(podlord::uiText(code) == expected, "Native text differs from the reference public catalog.");
        }
        return require(podlord::uiLanguages() == options && options.size() == 22, "Reference language choices differ.");
    }
    if (mode.startsWith("resolve_")) {
        const QMap<QString, QPair<QString, QString>> cases{{"resolve_region", {"de_AT", "de"}}, {"resolve_brazil", {"pt_BR", "pt-BR"}},
            {"resolve_portugal", {"pt_PT", "en"}}, {"resolve_chinese", {"zh_TW", "zh-Hans"}}, {"resolve_unknown", {"fi_FI", "en"}}};
        require(cases.contains(mode), "Unknown system language scenario.");
        const auto value = cases.value(mode);
        return require(podlord::resolvedUiLanguage("system", QLocale(value.first)) == value.second, "System-language fallback differs.");
    }
    QTemporaryDir temporary;
    require(temporary.isValid(), "Cannot create an isolated language profile.");
    const auto profile = temporary.filePath("profile"), path = profile + "/read-settings.json";
    if (mode.startsWith("store_")) {
        const podlord::ReadSettingsStore store(profile);
        auto original = podlord::ReadSettings{};
        original.requestHardLimitPerMinute = 120; original.logLimitMb = 7;
        require(std::holds_alternative<podlord::ReadSettings>(store.save(original, {})), "Cannot establish the settings boundary.");
        auto root = QJsonDocument::fromJson(read(path)).object();
        if (mode == "store_old_read" || mode == "store_old_upgrade") { root["version"] = 6; root.remove("language"); root["themeIntensity"] = "subtle"; }
        else if (mode == "store_missing") root.remove("language");
        else if (mode == "store_type") root["language"] = 3;
        else if (mode == "store_empty") root["language"] = "";
        else if (mode == "store_unknown") root["language"] = "xx";
        else if (mode == "store_case") root["language"] = "DE";
        else if (mode == "store_version_fraction") root["version"] = 7.5;
        else return false;
        const auto bytes = QJsonDocument(root).toJson();
        require(write(path, bytes), "Cannot supply old or malformed external settings.");
        const auto loaded = store.load();
        if (mode != "store_old_read" && mode != "store_old_upgrade") {
            const auto* failure = std::get_if<podlord::Failure>(&loaded);
            const auto saved = store.save(original, original);
            const auto* saveFailure = std::get_if<podlord::Failure>(&saved);
            return require(failure && failure->code == podlord::StoreError::InvalidData && saveFailure
                && saveFailure->code == podlord::StoreError::InvalidData && read(path) == bytes, "Invalid language replaced retained settings.");
        }
        require(std::holds_alternative<podlord::ReadSettings>(loaded) && std::get<podlord::ReadSettings>(loaded) == original && read(path) == bytes, "Old settings did not load read-only.");
        if (mode == "store_old_read") return true;
        auto desired = original; desired.language = "de";
        return require(std::holds_alternative<podlord::ReadSettings>(store.save(desired, original))
            && std::get<podlord::ReadSettings>(store.load()) == desired
            && QJsonDocument::fromJson(read(path)).object()["version"] == 8, "Language upgrade lost unrelated settings.");
    }
    podlord::Workspace workspace(profile);
    QQmlApplicationEngine engine;
    QSignalSpy warnings(&engine, &QQmlApplicationEngine::warnings);
    engine.rootContext()->setContextProperty("workspace", &workspace);
    engine.load(QUrl("qrc:/podlord/Main.qml"));
    require(!engine.rootObjects().isEmpty(), "Cannot create actual language UI.");
    auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
    require(window && QTest::qWaitFor([&] { return !workspace.busy(); }, 5000), "Initial language settings did not settle.");
    if (mode == "repeat") return require(workspace.saveUiLanguage("system") && !QFileInfo::exists(profile), "Repeated language selection wrote data.");
    if (mode == "invalid") return require(!workspace.saveUiLanguage("xx") && workspace.uiLanguage() == "system"
        && !workspace.error().isEmpty() && !QFileInfo::exists(profile), "Rejected language changed settings or hid its error.");
    require(mode == "language" || mode == "table_tools" || QStringList{"narrow", "locked", "conflict", "restart", "preserve"}.contains(mode), "Unsupported language scenario.");
    require(click(window, "settingsWorkspaceButton") && click(window, "settingsAppearanceSection"), "Cannot reach language settings.");
    QString code = mode == "language" || mode == "table_tools" ? args.value(2) : QString("de");
    require(podlord::validUiLanguage(code), "Expected a shipped language.");
    if (mode == "narrow") { window->setWidth(720); window->setHeight(720); }
    std::optional<QLockFile> lock;
    if (mode == "locked" || mode == "conflict") {
        require(QDir().mkpath(profile), "Cannot establish a concurrent settings boundary.");
        if (mode == "locked") { lock.emplace(path + ".lock"); require(lock->tryLock(0), "Cannot hold the external settings lock."); }
        else { auto other = podlord::ReadSettings{}; other.logLimitMb = 9; require(std::holds_alternative<podlord::ReadSettings>(podlord::ReadSettingsStore(profile).save(other, {})), "Cannot save another window's settings."); }
    }
    require(click(window, "inlineUiLanguage"), "Cannot open the real language selector.");
    QTest::keyClick(window, Qt::Key_Home);
    for (int row = 0; row < podlord::uiLanguageCodes().indexOf(code); ++row) QTest::keyClick(window, Qt::Key_Down);
    QTest::keyClick(window, Qt::Key_Return);
    require(QTest::qWaitFor([&] { return !workspace.busy(); }, 5000), "Language save did not settle.");
    if (mode == "locked" || mode == "conflict") {
        auto* selector = podlord::test::visibleItem(window->contentItem(), "inlineUiLanguage");
        return require(workspace.uiLanguage() == "system" && !workspace.error().isEmpty() && selector->property("currentIndex").toInt() == 0, "Failed save left misleading language selection.");
    }
    require(workspace.uiLanguage() == code, "Public selector did not apply the chosen language.");
    if (mode == "narrow") {
        require(click(window, "toggleSidebar") && QTest::qWaitFor([&] { return podlord::test::visibleItem(window->contentItem(), "problemsOnly") != nullptr; }, 5000), "Cannot open narrow-window filters.");
    }
    for (const auto& pair : QList<QPair<QString, QString>>{{"resourcesWorkspaceButton", "nav.resources"}, {"eventsWorkspaceButton", "nav.events"},
        {"settingsWorkspaceButton", "nav.settings"}, {"settingsAppearanceSection", "settings.appearance"}, {"problemsOnly", "filters.problems"}, {"activityOnly", "filters.activity"}}) {
        auto* target = podlord::test::visibleItem(window->contentItem(), pair.first);
        require(target && target->property("text") == podlord::uiText(code)[pair.second], "Visible chrome did not update its translated label.");
    }
    require(workspace.currentSession().isEmpty() && workspace.contexts().isEmpty(), "Changing UI language changed cluster/session selection.");
    require(workspace.uiRightToLeft() == (code == "ar" || code == "ur"), "Language reading direction is incorrect.");
    if (mode == "table_tools") {
        require(click(window, "resourcesWorkspaceButton") && click(window, "resourceColumnsButton"), "Cannot open the actual table tools.");
        auto* save = podlord::test::visibleItem(window->contentItem(), "resourceSaveColumns");
        require(save && save->property("text") == workspace.uiText()["action.save"], "Table layout save action is not localized.");
        require(click(window, "resourceSaveColumns") && QTest::qWaitFor([&] { return !workspace.tableLayoutSaving(); }, 5000), "Cannot save the translated table layout.");
        require(workspace.tableLayoutError().isEmpty() && workspace.currentSession().isEmpty(), "Translated table tools changed session state or failed to save.");
    }
    if (mode == "restart") {
        podlord::Workspace reopened(profile);
        require(QTest::qWaitFor([&] { return !reopened.busy(); }, 5000) && reopened.uiLanguage() == code && reopened.uiText() == workspace.uiText(), "Reopening the profile lost its language.");
    }
    if (mode == "preserve") {
        require(workspace.saveReadSettings(120, 2, "7") && QTest::qWaitFor([&] { return !workspace.busy(); }, 5000), "Cannot save read policy after a language change.");
        const auto persisted = podlord::ReadSettingsStore(profile).load();
        require(std::holds_alternative<podlord::ReadSettings>(persisted) && std::get<podlord::ReadSettings>(persisted).language == code
            && std::get<podlord::ReadSettings>(persisted).logLimitMb == 7, "Saving read policy overwrote language.");
    }
    const auto frame = qEnvironmentVariable("PODLORD_LANGUAGE_FRAME");
    if (!frame.isEmpty()) require(QTest::qWaitFor([&] { return !window->grabWindow().isNull(); }, 5000) && window->grabWindow().save(frame), "Cannot capture the actual language surface.");
    return require(warnings.isEmpty(), "Actual language UI emitted QML warnings.");
}
} // namespace
int main(int argc, char** argv) {
    const QGuiApplication app(argc, argv);
    try { return scenario(app.arguments()) ? 0 : 1; }
    catch (const std::exception& error) { std::fprintf(stderr, "%s\n", error.what()); return 1; }
}
