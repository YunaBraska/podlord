import QtQuick
import QtQuick.Controls

TextArea {
    readOnly: true
    selectByMouse: true
    selectByKeyboard: true
    textFormat: TextEdit.PlainText
    wrapMode: TextEdit.Wrap
    padding: 0
    background: null
    Accessible.name: text
}
