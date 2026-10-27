import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ColumnLayout {
    id: about
    objectName: "aboutContent"
    Layout.minimumWidth: 0
    spacing: 10
    property string linkError: ""
    property int quoteIndex: -1
    readonly property var quotes: [
        "kubectl shouts. etcd whispers. Podlord listens.\nBuilt with heart, not equity.\nStar the repo if it survived your Monday. Fuel me if it survived your week.",
        "Pods come and go. Your sanity should not.\nOne human built this between deploys and despair.\nStar it. Donate when it spares you another describe.",
        "YAML stands for Yet Another Misindented Line.\nPodlord stands for whatever you needed it to.\nHit the star if we agree. Coffee link is right there.",
        "There are 10 kinds of people. The other 2 wrote this.\nNo VC, no roadmap, just stubborn craftsmanship.\nUse it, star it. Love it, fuel it.",
        "Kubernetes has no developer experience.\nSo I built one. Open source, single maintainer, dangerously caffeinated.\nA star costs nothing. A coffee buys a feature.",
        "Sidecars exist because containers can't keep their lanes.\nThis UI exists because dashboards can't keep yours.\nIf it helped, leave a star. If it shipped, leave a tip.",
        "RBAC: Role Based Annoyance Constructs.\nPodlord: small joy in a heavy stack.\nStar it after use. Donate if it earned its rent on your dock.",
        "Helm chart. Helm fault. Same energy.\nMade late at night because the alternative was rage.\nStar it, fund it, file an issue. Whichever feels right.",
        "Liveness probes were named by an optimist.\nPodlord was named after stubbornness.\nIf this UI is in your week, drop a star. If it's in your day, drop a coffee.",
        "The cloud is somebody else's panic.\nThis console is mine, shared with you.\nA star lowers it by one bar. A donation by two.",
        "ConfigMap: a love letter from past you to future you, half redacted.\nPodlord just reads it back without the suffering.\nStar if useful, fuel if essential.",
        "There is no SRE. Only severely resigned engineers.\nThis was built by one of them, for the rest of you.\nStar the repo. Buy the coffee. Keep the lights on.",
        "Crashloop: a feature of consistency.\nPodlord: a feature of restraint.\nStar if you noticed the difference. Donate if you appreciate it.",
        "Operators wake up at 3 AM so you don't have to.\nThis UI wakes up at the speed of your click.\nLeave a star before the next page.",
        "The control plane is fine. It said so itself.\nPodlord checks anyway, then shows you the truth.\nIf the truth helped, send back a coffee.",
        "Service mesh: yet another layer of indirection.\nPodlord: one less layer between you and the pod.\nStar earned. Coffee earned. Trust earned.",
        "Day 1: hello world.\nDay 712: namespace not found.\nPodlord was built between those two days. A star says thanks.",
        "Init containers run first, finish first, are forgotten first.\nThis maintainer kind of relates.\nA star or a coffee fixes it both.",
        "Eventually consistent means eventually correct, possibly never.\nPodlord aims for now consistent, now visible.\nIf you saw the difference, leave a tip.",
        "Resource limits are a suggestion. So is sleep.\nThis app was built ignoring both.\nReturn the favor with a star or a small donation.",
        "Logs: 90% noise, 9% noise, 1% truth.\nPodlord finds the 1% faster.\nStar the repo. Buy the coffee. Skip the next outage.",
        "Pod disruption budget: your patience.\nMaintainer disruption budget: the donation jar.\nKeep both topped up.",
        "Stateful sets are stateful. Maintainers are tired.\nPodlord makes both more bearable.\nStar if you used it twice today.",
        "Annotations are post it notes nobody reads.\nPodlord reads them so you don't have to.\nA coffee says thanks.",
        "There is no cloud. Only computers you cry over.\nThis console makes the crying shorter.\nStar the repo, fuel the maintainer.",
        "Probes lie. Metrics lie. Pods occasionally tell the truth.\nPodlord lets you watch the truth happen.\nIf that mattered today, leave a star.",
        "kubectl get pods solves nothing.\nkubectl get pods on repeat solves less.\nPodlord solves the repeat. Coffee link below.",
        "Distributed systems: you knew the risks.\nDistributed sanity: nobody warned you.\nA donation keeps the second one online.",
        "Reconcile: a verb done by the system, a noun done by the maintainer.\nPodlord does the first. Your star does the second.",
        "Latency is a feeling.\nThis UI tries to feel quick.\nIf it did today, drop a star or a coffee on the way out.",
        "OOMKilled: out of memory, killed.\nMaintainer of Podlord: out of money, still alive.\nA donation keeps the second statement true.",
        "If Kubernetes were easy, you would not be reading this.\nThis console makes hard things visible.\nStar it. Fuel it. Send it to a friend."
    ]
    function rotateQuote() {
        let next = Math.floor(Math.random() * quotes.length)
        if (next === quoteIndex) next = (next + 1) % quotes.length
        quoteIndex = next
    }
    onVisibleChanged: if (visible) rotateQuote()
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
    component LinkButton: IconButton {
        glyph: ""
        showText: true
        font.bold: true
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
    Rectangle {
        Layout.fillWidth: true
        Layout.minimumWidth: 0
        implicitHeight: aboutQuote.implicitHeight + 24
        color: workspace.appearanceColors.inset
        border.color: workspace.appearanceColors.border
        border.width: 1
        radius: 2
        Label {
            id: aboutQuote
            objectName: "aboutQuote"
            x: 12
            y: 12
            width: parent.width - 24
            text: about.quoteIndex < 0 ? "" : about.quotes[about.quoteIndex]
            textFormat: Text.PlainText
            wrapMode: Text.Wrap
            font.italic: true
            Accessible.name: text
        }
    }
    Label { objectName: "aboutUpdateStatus"; Layout.fillWidth: true; text: workspace.releaseUpdates.state.status; textFormat: Text.PlainText; wrapMode: Text.Wrap; color: workspace.releaseUpdates.state.error ? workspace.appearanceColors.danger : workspace.appearanceColors.muted; Accessible.name: text }
    Flow {
        Layout.fillWidth: true; Layout.minimumWidth: 0; spacing: 6
        IconButton { glyph: ""; showText: true; font.bold: true; objectName: "aboutCheckUpdates"; text: "Check for updates"; enabled: workspace.releaseUpdates.state.enabled && !workspace.releaseUpdates.state.busy; Accessible.description: "Explicit anonymous release check; automatic checks run once a week"; onClicked: workspace.releaseUpdates.checkNow() }
        IconButton { glyph: ""; showText: true; font.bold: true; objectName: "aboutDownloadUpdate"; visible: workspace.releaseUpdates.state.available; text: workspace.uiText["update.availableStatus"].replace("{0}", workspace.releaseUpdates.state.latestVersion); Accessible.name: "Download compatible native update"; onClicked: workspace.releaseUpdates.openDownload(); ToolTip.visible: hovered || activeFocus; ToolTip.text: workspace.uiText["update.downloadTip"].replace("{0}", workspace.releaseUpdates.state.latestVersion).replace("{1}", Qt.application.version) }
        IconButton { glyph: ""; showText: true; font.bold: true; objectName: "aboutReleasePage"; visible: workspace.releaseUpdates.state.releaseUrl !== ""; text: "Release notes"; Accessible.description: "Open the verified project release page; does not install software"; onClicked: workspace.releaseUpdates.openRelease() }
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
        IconButton { glyph: ""; showText: true; font.bold: true; objectName: "aboutLicense"; text: "License"; Accessible.name: "Podlord MIT license"; onClicked: { licenseDialog.title = "Podlord license"; licenseText.text = workspace.applicationLicense(); licenseDialog.open() } }
        IconButton { glyph: ""; showText: true; font.bold: true; objectName: "aboutDependencyNotices"; text: "Third-party notices"; Accessible.name: "Read complete bundled dependency license texts"; onClicked: { licenseDialog.title = "Third-party notices"; licenseText.text = workspace.dependencyNotices(); licenseDialog.open() } }
    }
    Label { Layout.fillWidth: true; text: "Qt, FFmpeg, OpenSSL, yaml-cpp and libvterm retain their own licenses. Podlord's license does not replace their distribution obligations."; textFormat: Text.PlainText; wrapMode: Text.Wrap; color: workspace.appearanceColors.muted }
    IconButton { id: credits; objectName: "aboutSoundCredits"; glyph: checked ? "Visible" : "Hidden"; text: "Sound credits"; showText: true; checkable: true; Accessible.description: "Expand or collapse bundled sound attribution and source links" }
    Repeater {
        model: about.visible && credits.checked ? about.soundCredits : []
        RowLayout {
            required property var modelData
            Layout.fillWidth: true
            Label { Layout.fillWidth: true; Layout.minimumWidth: 0; text: modelData.pack.replace(/-/g, " ") + " (" + modelData.count + " sound" + (modelData.count === 1 ? "" : "s") + ") / " + modelData.author + " / " + modelData.license; textFormat: Text.PlainText; wrapMode: Text.Wrap }
            IconButton { glyph: ""; showText: true; font.bold: true; objectName: "aboutSoundSource_" + modelData.pack; text: "Source"; Accessible.name: "Open source for " + modelData.pack; onClicked: about.openLink(modelData.source); ToolTip.visible: hovered || activeFocus; ToolTip.text: modelData.source }
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
