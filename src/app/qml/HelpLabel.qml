import QtQuick
import QtQuick.Controls

// Parameter label; when `help` is set, a marker is shown and hovering the
// label displays the explanation.
Label {
    id: root

    property string help

    text: ""
    opacity: 0.85
    elide: Text.ElideRight
    rightPadding: root.help.length > 0 ? 14 : 0

    // Drawn marker: the "ⓘ" glyph is missing from many UI fonts.
    Rectangle {
        visible: root.help.length > 0
        width: 11
        height: 11
        radius: 5.5
        color: "transparent"
        border.color: root.color
        border.width: 1
        opacity: 0.55
        x: Math.min(root.contentWidth, root.width - root.rightPadding) + 4
        anchors.verticalCenter: parent.verticalCenter

        Label {
            anchors.centerIn: parent
            text: "?"
            font.pixelSize: 8
            font.bold: true
        }
    }

    HoverHandler {
        id: hover
    }

    ToolTip.visible: hover.hovered && root.help.length > 0
    ToolTip.delay: 300
    ToolTip.text: root.help
}
