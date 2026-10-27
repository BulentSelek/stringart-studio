pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

import StringArtStudio

// Full-window helper for winding the real piece: one large instruction per
// step, readable from a distance, driven by keys (foot pedals usually send
// PageDown/PageUp) or by an automatic timer. Progress is stored per result.
Popup {
    id: root

    required property ProjectController controller

    readonly property int position: root.controller.playbackPosition
    readonly property int total: root.controller.resultSegments
    readonly property var ahead: root.position >= 0 ? root.controller.stepsAhead(4) : []

    parent: Overlay.overlay
    x: 0
    y: 0
    width: parent ? parent.width : 800
    height: parent ? parent.height : 600
    modal: true
    focus: true
    padding: 24
    closePolicy: Popup.CloseOnEscape
    Material.elevation: 0
    background: Rectangle { color: "#18181b" }

    onClosed: root.controller.endWinding()

    Connections {
        target: root.controller
        function onWindingChanged() {
            if (root.controller.winding)
                root.open()
            else
                root.close()
        }
    }

    function step(delta: int) {
        if (delta < 0 && root.position <= 1)
            return
        root.controller.stepPlayback(delta)
        autoTimer.restart()
    }

    property int spokenPosition: -1
    onPositionChanged: {
        if (!root.visible || !speakSwitch.checked || root.position === root.spokenPosition)
            return
        root.spokenPosition = root.position
        let text = qsTr("Pin %1").arg(root.controller.currentToPin)
        if (root.controller.resultIsColor && (root.controller.threadChangesHere || root.position === 1))
            text = qsTr("Thread %1, from pin %2 to pin %3").arg(root.controller.currentColorIndex + 1)
                       .arg(root.controller.currentFromPin).arg(root.controller.currentToPin)
        root.controller.speak(text)
    }

    Timer {
        id: autoTimer
        interval: secondsBox.value * 1000
        repeat: true
        running: root.visible && autoSwitch.checked && root.position < root.total
        onTriggered: root.controller.stepPlayback(1)
    }

    Shortcut {
        sequences: ["Space", "Right", "Return", "Enter", "PgDown", "Down"]
        enabled: root.visible
        autoRepeat: false
        onActivated: root.step(1)
    }
    Shortcut {
        sequences: ["Left", "Backspace", "PgUp", "Up"]
        enabled: root.visible
        autoRepeat: false
        onActivated: root.step(-1)
    }

    contentItem: RowLayout {
        spacing: 32

        // Frame preview with the current segment.
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.preferredWidth: 5

            Image {
                id: preview
                anchors.fill: parent
                source: root.visible ? "image://studio/result/" + root.controller.resultRevision : ""
                fillMode: Image.PreserveAspectFit
                cache: false
                mipmap: true
                retainWhileLoading: true

                Canvas {
                    x: (preview.width - preview.paintedWidth) / 2
                    y: (preview.height - preview.paintedHeight) / 2
                    width: preview.paintedWidth
                    height: preview.paintedHeight
                    property var line: root.controller.currentSegmentLine
                    // The result's own pins: the frame settings may have changed since the run.
                    property var pins: root.visible ? root.controller.resultPinPositions : []
                    onLineChanged: requestPaint()
                    onPinsChanged: requestPaint()
                    onWidthChanged: requestPaint()
                    onHeightChanged: requestPaint()
                    onPaint: {
                        const ctx = getContext("2d")
                        ctx.reset()
                        // Every pin, numbered every ten on the inside, to find the target on the frame.
                        if (pins && pins.length > 0) {
                            let cx = 0, cy = 0
                            for (const p of pins) { cx += p.x; cy += p.y }
                            cx /= pins.length
                            cy /= pins.length
                            ctx.fillStyle = "#9e9e9e"
                            ctx.font = "11px sans-serif"
                            ctx.textAlign = "center"
                            ctx.textBaseline = "middle"
                            for (let i = 0; i < pins.length; ++i) {
                                const px = pins[i].x * width, py = pins[i].y * height
                                ctx.fillRect(px - 1.5, py - 1.5, 3, 3)
                                if (i % 10 === 0) {
                                    const dx = pins[i].x - cx, dy = pins[i].y - cy
                                    const len = Math.max(1e-6, Math.hypot(dx * width, dy * height))
                                    ctx.fillText(i, px - dx * width / len * 14, py - dy * height / len * 14) // inside: never clipped
                                }
                            }
                        }
                        if (!line || line.length !== 2)
                            return
                        const a = line[0], b = line[1]
                        ctx.lineWidth = 3
                        ctx.strokeStyle = "#e53935"
                        ctx.beginPath()
                        ctx.moveTo(a.x * width, a.y * height)
                        ctx.lineTo(b.x * width, b.y * height)
                        ctx.stroke()
                        ctx.fillStyle = "#9e9e9e"
                        ctx.beginPath()
                        ctx.arc(a.x * width, a.y * height, 7, 0, 2 * Math.PI)
                        ctx.fill()
                        ctx.fillStyle = "#ffc107"
                        ctx.beginPath()
                        ctx.arc(b.x * width, b.y * height, 10, 0, 2 * Math.PI)
                        ctx.fill()
                    }
                }
            }
        }

        // Instruction.
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.preferredWidth: 4
            spacing: 12

            RowLayout {
                Layout.fillWidth: true
                Label {
                    objectName: "windingStepLabel"
                    text: qsTr("Step %1 of %2").arg(root.position).arg(root.total)
                    font.pixelSize: 26
                    Layout.fillWidth: true
                }
                Button {
                    text: qsTr("Exit (Esc)")
                    flat: true
                    onClicked: root.close()
                }
            }
            ProgressBar {
                Layout.fillWidth: true
                from: 0
                to: Math.max(1, root.total)
                value: root.position
            }

            Rectangle {
                Layout.fillWidth: true
                implicitHeight: changeLabel.implicitHeight + 16
                radius: 6
                color: "#ffc107"
                visible: root.controller.resultIsColor
                         && (root.controller.threadChangesHere || root.position === 1)
                Label {
                    id: changeLabel
                    anchors.centerIn: parent
                    text: qsTr("Change thread")
                    color: "#202020"
                    font.pixelSize: 24
                    font.bold: true
                }
            }

            RowLayout {
                visible: root.controller.resultIsColor
                spacing: 12
                Rectangle {
                    implicitWidth: 40
                    implicitHeight: 40
                    radius: 6
                    color: root.controller.currentThreadColor
                    border.color: "#80ffffff"
                }
                Label {
                    text: qsTr("Thread %1  %2").arg(root.controller.currentColorIndex + 1)
                                               .arg(root.controller.currentThreadColor.toString())
                    font.pixelSize: 24
                }
            }

            Label {
                text: qsTr("from pin %1").arg(root.controller.currentFromPin)
                font.pixelSize: 30
                opacity: 0.6
            }
            Label {
                objectName: "windingTargetPin"
                text: "→ " + root.controller.currentToPin
                font.pixelSize: Math.max(60, Math.min(220, root.height * 0.22))
                font.bold: true
                color: Material.accent
                Layout.fillWidth: true
                fontSizeMode: Text.HorizontalFit
                minimumPixelSize: 40
            }
            Label {
                visible: root.ahead.length > 0
                text: qsTr("then ") + root.ahead.map(s => s.to).join("  ·  ")
                font.pixelSize: 26
                opacity: 0.7
            }

            Item { Layout.fillHeight: true }

            RowLayout {
                Layout.fillWidth: true
                spacing: 16
                Button {
                    text: qsTr("◀  Back")
                    font.pixelSize: 22
                    enabled: root.position > 1
                    Layout.fillWidth: true
                    Layout.preferredWidth: 1
                    Layout.preferredHeight: 80
                    onClicked: root.step(-1)
                }
                AccentButton {
                    text: root.position >= root.total ? qsTr("Done") : qsTr("Next  ▶")
                    font.pixelSize: 22
                    enabled: root.position < root.total
                    Layout.fillWidth: true
                    Layout.preferredWidth: 2
                    Layout.preferredHeight: 80
                    onClicked: root.step(1)
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 12
                Switch {
                    id: speakSwitch
                    text: qsTr("Speak pins")
                    enabled: root.controller.speechAvailable
                    ToolTip.visible: hovered
                    ToolTip.text: root.controller.speechAvailable
                                  ? qsTr("Read each target pin aloud")
                                  : qsTr("No text-to-speech engine is available")
                }
                Switch {
                    id: autoSwitch
                    text: qsTr("Auto-advance every")
                }
                SpinBox {
                    id: secondsBox
                    from: 2
                    to: 60
                    value: 8
                    editable: true
                    Layout.preferredWidth: 120
                }
                Label {
                    text: qsTr("s")
                }
            }
            Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                opacity: 0.5
                text: qsTr("Next: Space, →, ↓, Enter or Page Down (foot pedal).  Back: ←, ↑, Backspace or Page Up.  Progress is saved for this result.")
            }
        }
    }
}
