import QtQuick

TextEdit {
    required property string richText
    required property var references
    readOnly: true
    text: richText.replace(/<a /g, "<a style=\"color:" + workspace.appearanceColors.accent + "\" ")
    textFormat: TextEdit.RichText
    wrapMode: TextEdit.Wrap
    selectByMouse: true
    selectByKeyboard: true
    color: workspace.appearanceColors.text
    onLinkActivated: function(link) {
        const resource = references.find(item => item.id === link)
        if (resource && !workspace.inspectPath(resource.path) && resource.kind === "Event")
            workspace.inspectEventPath(resource.path)
    }
}
