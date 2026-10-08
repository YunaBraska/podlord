import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ColumnLayout {
    id: about
    Layout.minimumWidth: 0
    spacing: 10
    property string linkError: ""
    readonly property var soundCredits: {
        const groups = {}
        for (const sound of workspace.alerts.sounds) {
            if (sound.id === "none") continue
            const key = sound.source + "\n" + sound.author + "\n" + sound.license
            if (!groups[key]) groups[key] = {source: sound.source, author: sound.author, license: sound.license, pack: sound.source.split("/").pop(), count: 0}
            ++groups[key].count
        }
        return Object.keys(groups).map(key => groups[key])
    }
    function openLink(url) { linkError = Qt.openUrlExternally(url) ? "" : "The browser could not open this link. Please try again explicitly." }
    component LinkButton: Button {
        required property var modelData
        objectName: "about" + modelData.id; text: modelData.text
        Accessible.description: "Open " + modelData.url + " in the external browser"
        onClicked: about.openLink(modelData.url)
        ToolTip.visible: hovered || activeFocus; ToolTip.text: modelData.url
    }
    RowLayout {
        Layout.fillWidth: true
        spacing: 16
        Image { objectName: "aboutLogo"; Layout.preferredWidth: 96; Layout.preferredHeight: 96; source: "qrc:/podlord/brand-logo.png"; sourceSize.width: 192; sourceSize.height: 192; fillMode: Image.PreserveAspectFit; asynchronous: true; Accessible.ignored: true }
        ColumnLayout {
            Layout.fillWidth: true; Layout.minimumWidth: 0
            Label { Layout.fillWidth: true; Layout.minimumWidth: 0; text: "PODLORD"; font.pixelSize: 24; font.bold: true; color: workspace.appearanceColors.accent; elide: Text.ElideRight }
            Label { objectName: "aboutTagline"; Layout.fillWidth: true; text: workspace.uiText["about.tagline"]; font.italic: true; wrapMode: Text.Wrap; color: workspace.appearanceColors.muted }
            Label { objectName: "settingsVersion"; Layout.fillWidth: true; text: workspace.uiText["about.version"].replace("{0}", Qt.application.version); textFormat: Text.PlainText; wrapMode: Text.Wrap; color: workspace.appearanceColors.muted }
        }
    }
    Label { objectName: "aboutUpdateStatus"; Layout.fillWidth: true; text: workspace.releaseUpdates.state.status; textFormat: Text.PlainText; wrapMode: Text.Wrap; color: workspace.releaseUpdates.state.error ? workspace.appearanceColors.danger : workspace.appearanceColors.muted; Accessible.name: text }
    Flow {
        Layout.fillWidth: true; Layout.minimumWidth: 0; spacing: 6
        Button { objectName: "aboutCheckUpdates"; text: "Check for updates"; enabled: workspace.releaseUpdates.state.enabled && !workspace.releaseUpdates.state.busy; Accessible.description: "Explicit anonymous release check; automatic checks run once a week"; onClicked: workspace.releaseUpdates.checkNow() }
        Button { objectName: "aboutDownloadUpdate"; visible: workspace.releaseUpdates.state.available; text: workspace.uiText["update.availableStatus"].replace("{0}", workspace.releaseUpdates.state.latestVersion); Accessible.name: "Download compatible native update"; onClicked: workspace.releaseUpdates.openDownload(); ToolTip.visible: hovered || activeFocus; ToolTip.text: workspace.uiText["update.downloadTip"].replace("{0}", workspace.releaseUpdates.state.latestVersion).replace("{1}", Qt.application.version) }
        Button { objectName: "aboutReleasePage"; visible: workspace.releaseUpdates.state.releaseUrl !== ""; text: "Release notes"; Accessible.description: "Open the verified project release page; does not install software"; onClicked: workspace.releaseUpdates.openRelease() }
    }
    Label { objectName: "aboutUpdateCheckedAt"; Layout.fillWidth: true; visible: workspace.releaseUpdates.state.lastCheckedAt !== ""; text: "Last checked: " + workspace.releaseUpdates.state.lastCheckedAt; textFormat: Text.PlainText; wrapMode: Text.Wrap; color: workspace.appearanceColors.muted }
    Label { text: workspace.uiText["about.supportHeading"]; font.bold: true; color: workspace.appearanceColors.accent }
    Flow {
        Layout.fillWidth: true
        Layout.minimumWidth: 0
        spacing: 6
        Repeater {
            model: about.visible ? [
                {id: "Sponsors", text: workspace.uiText["about.sponsors"], url: "https://github.com/sponsors/YunaBraska"},
                {id: "Coffee", text: workspace.uiText["about.bmc"], url: "https://buymeacoffee.com/YunaBraska"},
                {id: "Kofi", text: workspace.uiText["about.kofi"], url: "https://ko-fi.com/YunaBraska"},
                {id: "Liberapay", text: workspace.uiText["about.liberapay"], url: "https://liberapay.com/YunaBraska"}
            ] : []
            LinkButton {}
        }
    }
    Label { text: workspace.uiText["about.projectHeading"]; font.bold: true; color: workspace.appearanceColors.accent }
    Flow {
        Layout.fillWidth: true; Layout.minimumWidth: 0; spacing: 6
        Repeater {
            model: about.visible ? [
                {id: "Star", text: workspace.uiText["about.starRepo"], url: "https://github.com/YunaBraska/podlord/stargazers"},
                {id: "Project", text: workspace.uiText["about.githubRepo"], url: "https://github.com/YunaBraska/podlord"},
                {id: "Issues", text: workspace.uiText["about.createIssue"], url: "https://github.com/YunaBraska/podlord/issues/new"}
            ] : []
            LinkButton {}
        }
    }
    Flow {
        Layout.fillWidth: true; Layout.minimumWidth: 0; spacing: 6
        Button { objectName: "aboutLicense"; text: "License"; Accessible.name: "Podlord MIT license"; onClicked: { licenseDialog.title = "Podlord license"; licenseText.text = workspace.applicationLicense(); licenseDialog.open() } }
        Button { objectName: "aboutDependencyNotices"; text: "Third-party notices"; Accessible.name: "Read complete bundled dependency license texts"; onClicked: { licenseDialog.title = "Third-party notices"; licenseText.text = workspace.dependencyNotices(); licenseDialog.open() } }
    }
    Label { Layout.fillWidth: true; text: "Qt, FFmpeg, OpenSSL, yaml-cpp and libvterm retain their own licenses. Podlord's license does not replace their distribution obligations."; textFormat: Text.PlainText; wrapMode: Text.Wrap; color: workspace.appearanceColors.muted }
    Label { text: "Sound credits"; font.bold: true }
    Repeater {
        model: about.visible ? about.soundCredits : []
        RowLayout {
            required property var modelData
            Layout.fillWidth: true
            Label { Layout.fillWidth: true; Layout.minimumWidth: 0; text: modelData.pack.replace(/-/g, " ") + " (" + modelData.count + " sound" + (modelData.count === 1 ? "" : "s") + ") / " + modelData.author + " / " + modelData.license; textFormat: Text.PlainText; wrapMode: Text.Wrap }
            Button { objectName: "aboutSoundSource_" + modelData.pack; text: "Source"; Accessible.name: "Open source for " + modelData.pack; onClicked: about.openLink(modelData.source); ToolTip.visible: hovered || activeFocus; ToolTip.text: modelData.source }
        }
    }
    Label { objectName: "aboutLinkError"; Layout.fillWidth: true; visible: text !== ""; text: about.linkError; textFormat: Text.PlainText; wrapMode: Text.Wrap; color: workspace.appearanceColors.danger; Accessible.name: text }
    Dialog {
        id: licenseDialog
        objectName: "aboutLicenseDialog"
        parent: Overlay.overlay
        title: "Podlord license"
        modal: true
        width: Math.min(640, parent ? parent.width - 24 : 640)
        height: Math.min(420, parent ? parent.height - 24 : 420)
        x: parent ? Math.round((parent.width - width) / 2) : 0
        y: parent ? Math.round((parent.height - height) / 2) : 0
        standardButtons: Dialog.Close
        onClosed: licenseText.clear()
        contentItem: ScrollView {
            objectName: "aboutLicenseScroll"
            TextArea { id: licenseText; objectName: "aboutLicenseText"; readOnly: true; selectByMouse: true; wrapMode: TextEdit.Wrap; textFormat: TextEdit.PlainText; font.family: workspace.monospaceFamily; Accessible.name: licenseDialog.title }
        }
    }
}
