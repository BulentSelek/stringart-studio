pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

import StringArtStudio

// Side-by-side comparison of finished runs of the current image. Click
// thumbnails to choose up to two runs; a run's settings can be restored.
Item {
    id: root

    required property ProjectController controller

    readonly property var runs: root.controller.runHistory
    // Ids of the compared runs; defaults to the two newest.
    property var selection: []
    readonly property var shown: {
        const ids = root.runs.map(r => r.id)
        const kept = root.selection.filter(id => ids.indexOf(id) >= 0)
        const chosen = kept.length > 0 ? kept : ids.slice(0, 2).reverse()
        return chosen.map(id => root.runs.find(r => r.id === id))
    }

    function toggle(id: int) {
        const current = root.shown.map(r => r.id)
        const at = current.indexOf(id)
        if (at >= 0)
            current.splice(at, 1)
        else
            current.push(id)
        root.selection = current.slice(-2)
    }

    Label {
        anchors.centerIn: parent
        visible: root.runs.length === 0
        width: Math.min(parent.width - 48, 520)
        wrapMode: Text.WordWrap
        horizontalAlignment: Text.AlignHCenter
        opacity: 0.6
        text: qsTr("Finished runs of this image are kept here (up to 12) to compare settings side by side.")
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 8
        visible: root.runs.length > 0
        spacing: 8

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 12

            Repeater {
                model: root.shown
                delegate: ColumnLayout {
                    id: slot
                    required property var modelData
                    required property int index
                    readonly property var other: root.shown.length === 2 ? root.shown[1 - slot.index] : null
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.preferredWidth: 1
                    spacing: 4

                    RowLayout {
                        Layout.fillWidth: true
                        Label {
                            text: qsTr("Run #%1").arg(slot.modelData.id)
                            font.bold: true
                            font.pixelSize: 16
                        }
                        Label {
                            text: slot.modelData.time
                            opacity: 0.6
                            Layout.fillWidth: true
                        }
                        Button {
                            text: qsTr("Use settings")
                            flat: true
                            enabled: !root.controller.running
                            onClicked: root.controller.restoreRunSettings(slot.modelData.id)
                        }
                        ToolButton {
                            text: "✕"
                            onClicked: root.controller.removeRun(slot.modelData.id)
                            ToolTip.visible: hovered
                            ToolTip.text: qsTr("Remove from the list")
                        }
                    }
                    Image {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        source: "image://studio/run" + slot.modelData.id + "/0"
                        fillMode: Image.PreserveAspectFit
                        cache: false
                        mipmap: true
                    }
                    Label {
                        Layout.fillWidth: true
                        font.family: "monospace"
                        text: qsTr("error %1   %2 segments   %3 m   %4 s")
                                  .arg(slot.modelData.error.toFixed(5))
                                  .arg(slot.modelData.segments)
                                  .arg(slot.modelData.lengthM.toFixed(1))
                                  .arg(slot.modelData.seconds.toFixed(1))
                    }
                    Label {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 20 // same layout in both slots
                        font.bold: true
                        color: slot.other && slot.modelData.error <= slot.other.error ? "#81c784" : "#e57373"
                        text: slot.other && slot.index === 1
                              ? qsTr("Error %1 % vs run #%2").arg(
                                    ((slot.modelData.error / slot.other.error - 1) * 100).toFixed(1)).arg(slot.other.id)
                              : ""
                    }
                    RowLayout {
                        spacing: 4
                        Repeater {
                            model: slot.modelData.colors
                            delegate: Rectangle {
                                required property color modelData
                                implicitWidth: 14
                                implicitHeight: 14
                                radius: 2
                                color: modelData
                                border.color: "#80ffffff"
                            }
                        }
                        Label {
                            text: slot.modelData.summary
                            opacity: 0.7
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }
                    }
                }
            }
        }

        Label {
            text: qsTr("Click runs to compare (two at a time):")
            opacity: 0.6
        }
        ListView {
            id: strip
            objectName: "runStrip"
            Layout.fillWidth: true
            Layout.preferredHeight: 128
            orientation: ListView.Horizontal
            spacing: 8
            clip: true
            model: root.runs
            ScrollBar.horizontal: ScrollBar {}
            delegate: Rectangle {
                id: card
                required property var modelData
                readonly property bool selected: root.shown.some(r => r.id === card.modelData.id)
                width: 104
                height: 120
                radius: 4
                color: selected ? "#3a3320" : "#26262b"
                border.color: selected ? Material.accent : "transparent"
                border.width: 2
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 4
                    spacing: 2
                    Image {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        source: "image://studio/run" + card.modelData.id + "/0"
                        sourceSize: Qt.size(96, 96)
                        fillMode: Image.PreserveAspectFit
                        cache: false
                    }
                    Label {
                        Layout.alignment: Qt.AlignHCenter
                        text: "#" + card.modelData.id + "  " + card.modelData.error.toFixed(4)
                        font.pixelSize: 11
                        font.family: "monospace"
                    }
                }
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.toggle(card.modelData.id)
                }
            }
        }
    }
}
