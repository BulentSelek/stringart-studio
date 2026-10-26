pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

import StringArtStudio

// Centre panel: original (with crop frame) / processed / result / residual
// views with zoom, pan and an optional pin overlay.
Pane {
    id: root

    required property ProjectController controller

    readonly property int originalTab: 0
    readonly property int resultTab: 2
    readonly property int residualTab: 3

    padding: 0
    Material.background: "#1b1b1f"

    // Frame outline through the pins (normalized canvas coordinates), with
    // the unreachable area outside the frame dimmed.
    component FrameOutline: Canvas {
        id: outline
        property var positions
        property real dim: 0.55
        onPositionsChanged: requestPaint()
        onWidthChanged: requestPaint()
        onHeightChanged: requestPaint()
        onVisibleChanged: if (visible) requestPaint()
        onPaint: {
            const ctx = getContext("2d")
            ctx.reset()
            if (!positions || positions.length < 3)
                return
            ctx.beginPath()
            ctx.rect(0, 0, width, height)
            ctx.moveTo(positions[0].x * width, positions[0].y * height)
            for (let i = 1; i < positions.length; ++i)
                ctx.lineTo(positions[i].x * width, positions[i].y * height)
            ctx.closePath()
            ctx.fillRule = Qt.OddEvenFill
            ctx.fillStyle = Qt.rgba(0, 0, 0, outline.dim)
            ctx.fill()
            ctx.beginPath()
            ctx.moveTo(positions[0].x * width, positions[0].y * height)
            for (let i = 1; i < positions.length; ++i)
                ctx.lineTo(positions[i].x * width, positions[i].y * height)
            ctx.closePath()
            ctx.lineWidth = 2
            ctx.strokeStyle = "#ffc107"
            ctx.stroke()
        }
    }

    readonly property string imageSource: {
        switch (tabs.currentIndex) {
        case 0: return "image://studio/original/" + root.controller.originalRevision
        case 1: return "image://studio/processed/" + root.controller.processedRevision
        case 2: return "image://studio/result/" + root.controller.resultRevision
        default: return "image://studio/residual/" + root.controller.resultRevision
        }
    }
    readonly property bool imageAvailable: tabs.currentIndex < root.resultTab ? root.controller.hasImage
                                                                              : root.controller.hasResult
                                                                                || root.controller.running

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
                TabButton { text: qsTr("Residual") }

                Connections {
                    target: root.controller
                    function onStateChanged() {
                        if (root.controller.running && tabs.currentIndex < root.resultTab)
                            tabs.currentIndex = root.resultTab
                    }
                }
            }
            CheckBox {
                id: pinToggle
                text: qsTr("Pins")
                enabled: tabs.currentIndex !== root.originalTab
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
                interactive: !cropFrame.active
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
                        source: root.imageAvailable ? root.imageSource : ""
                        cache: false
                        smooth: view.zoom < 2
                        mipmap: true
                        fillMode: Image.Stretch
                        retainWhileLoading: true

                        FrameOutline {
                            anchors.fill: parent
                            visible: tabs.currentIndex === 1 && root.controller.hasImage
                            positions: root.controller.pinPositions
                        }

                        Canvas {
                            id: pins
                            anchors.fill: parent
                            visible: pinToggle.checked && pinToggle.enabled
                            property var positions: root.controller.pinPositions
                            onPositionsChanged: requestPaint()
                            onVisibleChanged: if (visible) requestPaint()
                            onWidthChanged: requestPaint()
                            onHeightChanged: requestPaint()
                            onPaint: {
                                const ctx = getContext("2d")
                                ctx.reset()
                                if (!positions || positions.length === 0)
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

                        // Crop frame on the original image. The corner handle keeps the
                        // canvas aspect; for ellipse and rectangle frames the edge
                        // handles change it and update the frame height to match.
                        Item {
                            id: cropFrame
                            anchors.fill: parent
                            visible: tabs.currentIndex === root.originalTab && root.controller.hasImage
                                     && image.status === Image.Ready
                            enabled: !root.controller.running
                            readonly property bool active: moveArea.pressed || resizeArea.pressed
                                                           || rightArea.pressed || bottomArea.pressed
                            readonly property bool freeAspect:
                                root.controller.frameShape === ProjectController.Ellipse
                                || root.controller.frameShape === ProjectController.Rectangle
                            // Normalized width / height ratio of a crop with the canvas aspect.
                            readonly property real normalizedAspect: image.implicitHeight > 0
                                ? (root.controller.canvasWidth / root.controller.canvasHeight)
                                  * (image.implicitHeight / image.implicitWidth) : 1
                            property rect crop: root.controller.effectiveCrop

                            function commit() {
                                if (freeAspect) {
                                    // Frame aspect follows the crop's pixel aspect (width kept).
                                    const pixelAspect = (crop.width * image.implicitWidth)
                                                      / (crop.height * image.implicitHeight)
                                    const heightMm = root.controller.frameWidthMm / pixelAspect
                                    root.controller.frameHeightMm = Math.max(50, Math.min(5000, Math.round(heightMm)))
                                }
                                root.controller.crop = crop
                                crop = Qt.binding(() => root.controller.effectiveCrop)
                            }

                            Rectangle {
                                id: cropRect
                                x: cropFrame.crop.x * cropFrame.width
                                y: cropFrame.crop.y * cropFrame.height
                                width: cropFrame.crop.width * cropFrame.width
                                height: cropFrame.crop.height * cropFrame.height
                                color: "transparent"
                                border.color: Material.accent
                                border.width: 2

                                FrameOutline {
                                    anchors.fill: parent
                                    positions: root.controller.pinPositions
                                    dim: 0.35
                                }

                                MouseArea {
                                    id: moveArea
                                    anchors.fill: parent
                                    cursorShape: Qt.SizeAllCursor
                                    property point start
                                    property rect startCrop
                                    onPressed: mouse => {
                                        start = mapToItem(cropFrame, mouse.x, mouse.y)
                                        startCrop = cropFrame.crop
                                    }
                                    onPositionChanged: mouse => {
                                        const p = mapToItem(cropFrame, mouse.x, mouse.y)
                                        const c = startCrop
                                        const nx = Math.max(0, Math.min(1 - c.width, c.x + (p.x - start.x) / cropFrame.width))
                                        const ny = Math.max(0, Math.min(1 - c.height, c.y + (p.y - start.y) / cropFrame.height))
                                        cropFrame.crop = Qt.rect(nx, ny, c.width, c.height)
                                    }
                                    onReleased: cropFrame.commit()
                                }

                                Rectangle {
                                    width: 16
                                    height: 16
                                    radius: 3
                                    color: Material.accent
                                    anchors.right: parent.right
                                    anchors.bottom: parent.bottom
                                    anchors.margins: -8

                                    MouseArea {
                                        id: resizeArea
                                        anchors.fill: parent
                                        cursorShape: Qt.SizeFDiagCursor
                                        onPositionChanged: mouse => {
                                            const p = mapToItem(cropFrame, mouse.x, mouse.y)
                                            const c = cropFrame.crop
                                            const a = cropFrame.normalizedAspect
                                            // Width from the pointer, limited by the image bounds.
                                            let w = Math.max(0.05, p.x / cropFrame.width - c.x)
                                            w = Math.min(w, 1 - c.x, (1 - c.y) * a)
                                            cropFrame.crop = Qt.rect(c.x, c.y, w, w / a)
                                        }
                                        onReleased: cropFrame.commit()
                                    }
                                }

                                Rectangle {
                                    visible: cropFrame.freeAspect
                                    width: 10
                                    height: Math.min(48, parent.height / 3)
                                    radius: 3
                                    color: Material.accent
                                    anchors.horizontalCenter: parent.right
                                    anchors.verticalCenter: parent.verticalCenter

                                    MouseArea {
                                        id: rightArea
                                        anchors.fill: parent
                                        anchors.margins: -6
                                        cursorShape: Qt.SizeHorCursor
                                        onPositionChanged: mouse => {
                                            const p = mapToItem(cropFrame, mouse.x, mouse.y)
                                            const c = cropFrame.crop
                                            const w = Math.max(0.05, Math.min(1 - c.x, p.x / cropFrame.width - c.x))
                                            cropFrame.crop = Qt.rect(c.x, c.y, w, c.height)
                                        }
                                        onReleased: cropFrame.commit()
                                    }
                                }

                                Rectangle {
                                    visible: cropFrame.freeAspect
                                    width: Math.min(48, parent.width / 3)
                                    height: 10
                                    radius: 3
                                    color: Material.accent
                                    anchors.horizontalCenter: parent.horizontalCenter
                                    anchors.verticalCenter: parent.bottom

                                    MouseArea {
                                        id: bottomArea
                                        anchors.fill: parent
                                        anchors.margins: -6
                                        cursorShape: Qt.SizeVerCursor
                                        onPositionChanged: mouse => {
                                            const p = mapToItem(cropFrame, mouse.x, mouse.y)
                                            const c = cropFrame.crop
                                            const h = Math.max(0.05, Math.min(1 - c.y, p.y / cropFrame.height - c.y))
                                            cropFrame.crop = Qt.rect(c.x, c.y, c.width, h)
                                        }
                                        onReleased: cropFrame.commit()
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
                visible: !root.controller.hasImage && !root.imageAvailable
                text: qsTr("Open or drop a photo to start")
                font.pixelSize: 22
                opacity: 0.5
            }
            Label {
                anchors.centerIn: parent
                visible: root.controller.hasImage && tabs.currentIndex >= root.resultTab
                         && !root.controller.running && !root.controller.hasResult
                text: qsTr("Press Start to generate string art")
                font.pixelSize: 20
                opacity: 0.5
            }

            Pane {
                anchors.left: parent.left
                anchors.bottom: parent.bottom
                anchors.margins: 12
                visible: tabs.currentIndex === root.residualTab && root.imageAvailable
                opacity: 0.9
                padding: 8
                RowLayout {
                    spacing: 12
                    Rectangle { width: 14; height: 14; color: "#ff4040" }
                    Label { text: qsTr("lighter than target (needs thread)") }
                    Rectangle { width: 14; height: 14; color: "#4040ff" }
                    Label { text: qsTr("darker than target") }
                }
            }
        }
    }
}
