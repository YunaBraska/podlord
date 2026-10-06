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
    RowLayout {
        Layout.fillWidth: true
        Label { Layout.fillWidth: true; Layout.minimumWidth: 0; text: "PODLORD"; font.pixelSize: 24; font.bold: true; color: workspace.appearanceColors.accent; elide: Text.ElideRight }
        Button { objectName: "aboutLicense"; text: "License"; Accessible.name: "Podlord MIT license"; onClicked: { licenseDialog.title = "Podlord license"; licenseText.text = workspace.applicationLicense(); licenseDialog.open() } }
    }
    Label { text: "Native Kubernetes workspace"; font.italic: true }
    Label { objectName: "settingsVersion"; text: "Podlord Native " + Qt.application.version; textFormat: Text.PlainText }
    Label { Layout.fillWidth: true; text: "C++ and Qt Quick. Views use cached snapshots; explicit refresh and inspection schedule background reads."; textFormat: Text.PlainText; wrapMode: Text.Wrap }
    Label { text: "Project and support"; font.bold: true }
    Flow {
        Layout.fillWidth: true
        Layout.minimumWidth: 0
        spacing: 6
        Repeater {
            model: about.visible ? [
                {id: "Project", text: "Project", url: "https://github.com/YunaBraska/podlord"},
                {id: "Issues", text: "Report an issue", url: "https://github.com/YunaBraska/podlord/issues/new"},
                {id: "Star", text: "Star", url: "https://github.com/YunaBraska/podlord/stargazers"},
                {id: "Sponsors", text: "Sponsors", url: "https://github.com/sponsors/YunaBraska"},
                {id: "Coffee", text: "Buy Me a Coffee", url: "https://buymeacoffee.com/YunaBraska"},
                {id: "Kofi", text: "Ko-fi", url: "https://ko-fi.com/YunaBraska"}
            ] : []
            Button {
                required property var modelData
                objectName: "about" + modelData.id
                text: modelData.text
                Accessible.description: "Open " + modelData.url + " in the external browser"
                onClicked: about.openLink(modelData.url)
                ToolTip.visible: hovered || activeFocus
                ToolTip.text: modelData.url
            }
        }
    }
    Label { Layout.fillWidth: true; text: "Qt, FFmpeg, OpenSSL, yaml-cpp and libvterm retain their own licenses. Podlord's license does not replace their distribution obligations."; textFormat: Text.PlainText; wrapMode: Text.Wrap; color: workspace.appearanceColors.muted }
    Button { objectName: "aboutDependencyNotices"; text: "Third-party notices"; Accessible.name: "Read complete bundled dependency license texts"; onClicked: { licenseDialog.title = "Third-party notices"; licenseText.text = workspace.dependencyNotices(); licenseDialog.open() } }
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
