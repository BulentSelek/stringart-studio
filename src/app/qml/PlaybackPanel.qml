pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

import StringArtStudio

// Winding playback of the finished sequence: timeline, stepping, speed,
// jump to segment, current pins and frame export. Replays the computed
// sequence; it never re-runs the optimization.
Pane {
    id: root

    required property ProjectController controller
    property alias highlight: highlightBox.checked

    signal viewRequested()
    signal exportFrameRequested()

    Material.elevation: 2
    padding: 8
    enabled: root.controller.hasResult && !root.controller.running

    ColumnLayout {
        anchors.fill: parent
        spacing: 2

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            Label {
                text: qsTr("Winding")
                font.bold: true
            }
            Slider {
                id: timeline
                objectName: "timeline"
                Layout.fillWidth: true
                from: 0
                to: Math.max(1, root.controller.resultSegments)
                stepSize: 1
                value: root.controller.playbackPosition
                onMoved: {
                    root.controller.stopPlayback()
                    root.controller.playbackPosition = Math.round(value)
                    root.viewRequested()
                }
            }
            Label {
                text: (root.controller.currentFromPin >= 0
                       ? qsTr("pin %1 → %2").arg(root.controller.currentFromPin).arg(root.controller.currentToPin)
                       : qsTr("start at pin %1").arg(root.controller.startPin))
                      + "   " + root.controller.playbackPosition + " / " + root.controller.resultSegments
                font.family: "monospace"
                Layout.minimumWidth: 260
                horizontalAlignment: Text.AlignRight
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 2

            ToolButton {
                text: "⏮"
                onClicked: root.controller.stepPlayback(-root.controller.resultSegments)
                ToolTip.visible: hovered
                ToolTip.text: qsTr("First segment")
            }
            ToolButton {
                text: "◀"
                autoRepeat: true
                onClicked: {
                    root.controller.stepPlayback(-1)
                    root.viewRequested()
                }
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Previous segment (Left)")
            }
            ToolButton {
                text: root.controller.playing ? "⏸" : "▶"
                highlighted: true
                onClicked: {
                    if (root.controller.playing) {
                        root.controller.stopPlayback()
                    } else {
                        root.controller.play()
                        root.viewRequested()
                    }
                }
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Play / pause (Space)")
            }
            ToolButton {
                text: "▶"
                autoRepeat: true
                onClicked: {
                    root.controller.stepPlayback(1)
                    root.viewRequested()
                }
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Next segment (Right)")
            }
            ToolButton {
                text: "⏭"
                onClicked: root.controller.stepPlayback(root.controller.resultSegments)
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Last segment")
            }

            ComboBox {
                readonly property var speeds: [1, 5, 20, 50, 200, 1000]
                model: speeds.map(s => s + qsTr(" seg/s"))
                currentIndex: Math.max(0, speeds.indexOf(root.controller.playbackSpeed))
                onActivated: index => root.controller.playbackSpeed = speeds[index]
                Layout.preferredWidth: 130
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Playback speed")
            }

            Label {
                text: qsTr("Go to")
                opacity: 0.8
                Layout.leftMargin: 8
            }
            SpinBox {
                from: 0
                to: root.controller.resultSegments
                value: root.controller.playbackPosition
                editable: true
                Layout.preferredWidth: 130
                onValueModified: {
                    root.controller.stopPlayback()
                    root.controller.playbackPosition = value
                    root.viewRequested()
                }
            }

            Item { Layout.fillWidth: true }

            CheckBox {
                id: highlightBox
                text: qsTr("Highlight")
                checked: true
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Mark the current segment in red")
            }
            Button {
                text: qsTr("Export frame…")
                flat: true
                enabled: !root.controller.exporting
                onClicked: root.exportFrameRequested()
            }
        }
    }
}
