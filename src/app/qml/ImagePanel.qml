pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

import StringArtStudio

// Centre panel: original / processed / result views with zoom, pan and an
// optional pin overlay.
Pane {
    id: root

    required property ProjectController controller

    padding: 0
    Material.background: "#1b1b1f"

    readonly property string imageSource: {
        switch (tabs.currentIndex) {
        case 0: return "image://studio/original/" + root.controller.originalRevision
        case 1: return "image://studio/processed/" + root.controller.processedRevision
        default: return "image://studio/result/" + root.controller.resultRevision
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            TabBar {
                id: tabs
                Layout.fillWidth: true
                currentIndex: 0
                TabButton { text: qsTr("Original") }
                TabButton { text: qsTr("Processed") }
                TabButton { text: qsTr("String art") }

                Connections {
                    target: root.controller
                    function onStateChanged() {
                        if (root.controller.running)
                            tabs.currentIndex = 2
                    }
                }
            }
            CheckBox {
                id: pinToggle
                text: qsTr("Pins")
                enabled: tabs.currentIndex !== 0
            }
            Button {
                text: qsTr("Fit")
                flat: true
                onClicked: view.fit()
            }
            Label {
                text: Math.round(view.zoom * 100) + " %"
                font.family: "monospace"
                Layout.rightMargin: 12
            }
        }

        Item {
            id: view
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true

            property real zoom: 1.0
            readonly property real fitZoom: image.implicitWidth > 0
                ? Math.min(width / image.implicitWidth, height / image.implicitHeight) * 0.95 : 1.0

            function fit() {
                zoom = fitZoom
                flick.contentX = Math.max(0, (flick.contentWidth - flick.width) / 2)
                flick.contentY = Math.max(0, (flick.contentHeight - flick.height) / 2)
            }

            onFitZoomChanged: fit()

            Flickable {
                id: flick
                anchors.fill: parent
                contentWidth: Math.max(width, image.implicitWidth * view.zoom)
                contentHeight: Math.max(height, image.implicitHeight * view.zoom)
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.horizontal: ScrollBar {}
                ScrollBar.vertical: ScrollBar {}

                Item {
                    width: flick.contentWidth
                    height: flick.contentHeight

                    Image {
                        id: image
                        anchors.centerIn: parent
                        width: implicitWidth * view.zoom
                        height: implicitHeight * view.zoom
                        source: root.controller.hasImage ? root.imageSource : ""
                        cache: false
                        smooth: view.zoom < 2
                        mipmap: true
                        fillMode: Image.Stretch
                        // Keep the previous frame while a new revision loads.
                        retainWhileLoading: true

                        Canvas {
                            id: pins
                            anchors.fill: parent
                            visible: pinToggle.checked && pinToggle.enabled
                            property var positions: root.controller.pinPositions
                            onPositionsChanged: requestPaint()
                            onVisibleChanged: if (visible) requestPaint()
                            onWidthChanged: requestPaint()
                            onPaint: {
                                const ctx = getContext("2d")
                                ctx.reset()
                                if (!positions)
                                    return
                                const r = Math.max(1.5, Math.min(4, width / positions.length))
                                const labelEvery = positions.length > 200 ? 50 : 10
                                ctx.font = "11px sans-serif"
                                for (let i = 0; i < positions.length; ++i) {
                                    const p = positions[i]
                                    ctx.fillStyle = i === 0 ? "#ff5252" : "#ffc107"
                                    ctx.beginPath()
                                    ctx.arc(p.x * width, p.y * height, i === 0 ? r * 1.8 : r, 0, 2 * Math.PI)
                                    ctx.fill()
                                    if (i % labelEvery === 0) {
                                        const dx = p.x - 0.5, dy = p.y - 0.5
                                        ctx.fillStyle = "#ff9800"
                                        ctx.fillText(i, p.x * width - dx * 40 - 8, p.y * height - dy * 40 + 4)
                                    }
                                }
                            }
                        }
                    }
                }
            }

            WheelHandler {
                target: null
                acceptedModifiers: Qt.NoModifier | Qt.ControlModifier
                onWheel: event => {
                    const factor = event.angleDelta.y > 0 ? 1.15 : 1 / 1.15
                    const newZoom = Math.max(0.05, Math.min(32, view.zoom * factor))
                    // Zoom around the cursor.
                    const px = flick.contentX + event.x
                    const py = flick.contentY + event.y
                    const ratio = newZoom / view.zoom
                    view.zoom = newZoom
                    flick.contentX = Math.max(0, px * ratio - event.x)
                    flick.contentY = Math.max(0, py * ratio - event.y)
                }
            }

            Label {
                anchors.centerIn: parent
                visible: !root.controller.hasImage
                text: qsTr("Open or drop a photo to start")
                font.pixelSize: 22
                opacity: 0.5
            }
            Label {
                anchors.centerIn: parent
                visible: root.controller.hasImage && tabs.currentIndex === 2
                         && !root.controller.running && !root.controller.hasResult
                text: qsTr("Press Start to generate string art")
                font.pixelSize: 20
                opacity: 0.5
            }
        }
    }
}
