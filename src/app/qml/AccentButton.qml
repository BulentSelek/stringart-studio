import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material

// Highlighted (accent-filled) button with dark text. Material draws the text
// of highlighted buttons in white, which is unreadable on the amber accent.
Button {
    id: control

    highlighted: true
    contentItem: Label {
        text: control.text
        font: control.font
        color: control.enabled ? "#202020" : control.Material.hintTextColor
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }
}
