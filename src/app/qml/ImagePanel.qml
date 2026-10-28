pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

import StringArtStudio

// Centre panel: original (with crop frame) / processed / result / runs
// views with zoom, pan, an optional pin overlay and, on the result, an
// error map (residual) and a before/after comparison.
Pane {
    id: root

    required property ProjectController controller
    property bool highlightSegment: true

    readonly property int originalTab: 0
    readonly property int processedTab: 1
    readonly property int resultTab: 2
    readonly property int runsTab: 3
    // String art view mode: the thread, its error map, or a comparison with the target.
    readonly property bool resultModeActive: tabs.currentIndex === root.resultTab && root.controller.hasResult
    readonly property bool showErrorMap: root.resultModeActive && resultMode.currentIndex === 1
    readonly property bool showCompare: root.resultModeActive && resultMode.currentIndex === 2

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

    function showView(index) {
        tabs.currentIndex = index
    }

    readonly property string imageSource: {
        switch (tabs.currentIndex) {
        case 0: return "image://studio/original/" + root.controller.originalRevision
        case 1: return "image://studio/processed/" + root.controller.processedRevision
        default: return root.showErrorMap ? "image://studio/residual/" + root.controller.resultRevision
                                          : "image://studio/result/" + root.controller.resultRevision
        }
    }
    readonly property bool imageAvailable: {
        if (tabs.currentIndex < root.resultTab)
            return root.controller.hasImage
        return root.controller.hasResult || root.controller.running
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            // What the centre shows. Deliberately not tabs: rounded "chips"
            // after a "View" label, so they are not mistaken for the
            // parameter pages on the left.
            Label {
                text: qsTr("View")
                font.capitalization: Font.AllUppercase
                font.pixelSize: 12
                font.bold: true
                opacity: 0.6
                Layout.leftMargin: 12
            }
            RowLayout {
                id: tabs
                objectName: "viewTabs"
                property int currentIndex: 0
                spacing: 4
                Layout.topMargin: 4
                Layout.bottomMargin: 4

                Repeater {
                    model: [qsTr("Original"), qsTr("Processed"), qsTr("String art"),
                            root.controller.runHistory.length > 0
                            ? qsTr("Runs (%1)").arg(root.controller.runHistory.length) : qsTr("Runs")]
                    delegate: Button {
                        id: chip
                        required property string modelData
                        required property int index
                        objectName: "viewChip" + index
                        text: modelData
                        checkable: true
                        checked: tabs.currentIndex === index
                        topInset: 0
                        bottomInset: 0
                        leftPadding: 14
                        rightPadding: 14
                        contentItem: Label {
                            text: chip.text
                            font: chip.font
                            color: chip.checked ? "#202020" : chip.Material.foreground
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }
                        background: Rectangle {
                            implicitHeight: 30
                            radius: height / 2
                            color: chip.checked ? chip.Material.accent
                                                : chip.hovered ? "#20ffffff" : "transparent"
                            border.color: chip.checked ? chip.Material.accent : "#50ffffff"
                            border.width: 1
                        }
                        onClicked: tabs.currentIndex = index
                    }
                }

                Connections {
                    target: root.controller
                    function onStateChanged() {
                        if (root.controller.running && tabs.currentIndex < root.resultTab)
                            tabs.currentIndex = root.resultTab
                    }
                }
            }
            Item { Layout.fillWidth: true }
            ComboBox {
                id: resultMode
                objectName: "resultModeBox"
                visible: tabs.currentIndex === root.resultTab
                enabled: root.controller.hasResult
                model: [qsTr("Thread"), qsTr("Error map"), qsTr("Compare")]
                Layout.preferredWidth: 140
                ToolTip.visible: hovered
                ToolTip.delay: 300
                ToolTip.text: qsTr("Error map: red where the result is lighter than the target, blue where darker. "
                                   + "Compare: the target left of a movable divider.")
            }
            ToolButton {
                id: pinToggle
                text: qsTr("Pins")
                checkable: true
                enabled: tabs.currentIndex !== root.originalTab && tabs.currentIndex !== root.runsTab
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Show the pins with their numbers")
            }
            Button {
                text: qsTr("Fit")
                flat: true
                visible: tabs.currentIndex !== root.runsTab
                onClicked: view.fit()
            }
            Label {
                text: Math.round(view.zoom * 100) + " %"
                visible: tabs.currentIndex !== root.runsTab
                font.family: "monospace"
                Layout.rightMargin: 12
            }
        }

        RunsView {
            objectName: "runsView"
            controller: root.controller
            visible: tabs.currentIndex === root.runsTab
            Layout.fillWidth: true
            Layout.fillHeight: true
        }

        Item {
            id: view
            visible: tabs.currentIndex !== root.runsTab
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true

            property real zoom: 1.0
            // Area to show, in fractions of the image: the image itself, plus
            // the committed crop on the Original tab (it may extend beyond).
            readonly property bool showCrop: tabs.currentIndex === root.originalTab && root.controller.hasImage
            readonly property rect crop: root.controller.effectiveCrop
            readonly property real x0: showCrop ? Math.min(0, crop.x) : 0
            readonly property real y0: showCrop ? Math.min(0, crop.y) : 0
            readonly property real x1: showCrop ? Math.max(1, crop.x + crop.width) : 1
            readonly property real y1: showCrop ? Math.max(1, crop.y + crop.height) : 1
            readonly property real fitZoom: image.implicitWidth > 0
                ? Math.min(width / ((x1 - x0) * image.implicitWidth),
                           height / ((y1 - y0) * image.implicitHeight)) * 0.95 : 1.0

            function fit() {
                zoom = fitZoom
                flick.contentX = Math.max(0, (flick.contentWidth - flick.width) / 2)
                flick.contentY = Math.max(0, (flick.contentHeight - flick.height) / 2)
            }

            onFitZoomChanged: fit()

            Flickable {
                id: flick
                anchors.fill: parent
                contentWidth: Math.max(width, (view.x1 - view.x0) * image.implicitWidth * view.zoom)
                contentHeight: Math.max(height, (view.y1 - view.y0) * image.implicitHeight * view.zoom)
                boundsBehavior: Flickable.StopAtBounds
                interactive: !cropFrame.active && !dividerArea.pressed
                ScrollBar.horizontal: ScrollBar {}
                ScrollBar.vertical: ScrollBar {}

                Item {
                    id: content
                    width: flick.contentWidth
                    height: flick.contentHeight

                    // Empty canvas behind the crop: padding beyond the image is white.
                    Rectangle {
                        visible: view.showCrop && cropFrame.visible
                        color: "white"
                        x: image.x + cropFrame.crop.x * image.width
                        y: image.y + cropFrame.crop.y * image.height
                        width: cropFrame.crop.width * image.width
                        height: cropFrame.crop.height * image.height
                    }

                    Image {
                        id: image
                        x: (content.width - (view.x1 - view.x0) * width) / 2 - view.x0 * width
                        y: (content.height - (view.y1 - view.y0) * height) / 2 - view.y0 * height
                        width: implicitWidth * view.zoom
                        height: implicitHeight * view.zoom
                        source: root.imageAvailable ? root.imageSource : ""
                        cache: false
                        smooth: view.zoom < 2
                        mipmap: true
                        fillMode: Image.Stretch
                        retainWhileLoading: true

                        // Before/after comparison: the processed target left of the divider.
                        Item {
                            id: compare
                            anchors.fill: parent
                            visible: root.showCompare
                            property real divider: 0.5

                            Item {
                                width: compare.divider * compare.width
                                height: compare.height
                                clip: true
                                Image {
                                    width: compare.width
                                    height: compare.height
                                    source: compare.visible
                                            ? "image://studio/processed/" + root.controller.processedRevision : ""
                                    cache: false
                                    smooth: view.zoom < 2
                                    mipmap: true
                                }
                            }
                            Rectangle {
                                x: compare.divider * compare.width - 1
                                width: 2
                                height: compare.height
                                color: Material.accent
                            }
                            Rectangle {
                                x: compare.divider * compare.width - width / 2
                                y: compare.height / 2 - height / 2
                                width: 22
                                height: 44
                                radius: 6
                                color: Material.accent
                                Label {
                                    anchors.centerIn: parent
                                    text: "⇔"
                                    color: "#202020"
                                }
                                MouseArea {
                                    id: dividerArea
                                    anchors.fill: parent
                                    anchors.margins: -8
                                    cursorShape: Qt.SplitHCursor
                                    onPositionChanged: mouse => {
                                        const p = mapToItem(compare, mouse.x, mouse.y)
                                        compare.divider = Math.max(0, Math.min(1, p.x / compare.width))
                                    }
                                }
                            }
                            Label {
                                x: 8
                                y: 8
                                visible: compare.divider > 0.15
                                text: qsTr("Target")
                                padding: 4
                                background: Rectangle { color: "#a0000000"; radius: 3 }
                            }
                            Label {
                                x: compare.width - width - 8
                                y: 8
                                visible: compare.divider < 0.85
                                text: qsTr("String art")
                                padding: 4
                                background: Rectangle { color: "#a0000000"; radius: 3 }
                            }
                        }

                        // Current winding segment during playback.
                        Canvas {
                            id: segmentOverlay
                            anchors.fill: parent
                            visible: root.highlightSegment
                                     && tabs.currentIndex === root.resultTab
                                     && root.controller.hasResult && !root.controller.running
                            property var line: root.controller.currentSegmentLine
                            onLineChanged: requestPaint()
                            onVisibleChanged: if (visible) requestPaint()
                            onWidthChanged: requestPaint()
                            onHeightChanged: requestPaint()
                            onPaint: {
                                const ctx = getContext("2d")
                                ctx.reset()
                                if (!line || line.length !== 2)
                                    return
                                const a = line[0], b = line[1]
                                ctx.lineWidth = Math.max(1.5, view.zoom * 1.2)
                                ctx.strokeStyle = "#e53935"
                                ctx.beginPath()
                                ctx.moveTo(a.x * width, a.y * height)
                                ctx.lineTo(b.x * width, b.y * height)
                                ctx.stroke()
                                ctx.fillStyle = "#e53935"
                                for (const p of [a, b]) {
                                    ctx.beginPath()
                                    ctx.arc(p.x * width, p.y * height, 5, 0, 2 * Math.PI)
                                    ctx.fill()
                                }
                            }
                        }

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
                                        // Keep some overlap with the image; the rest may be padding.
                                        const nx = Math.max(Math.max(-1, 0.05 - c.width), Math.min(Math.min(2 - c.width, 0.95),
                                                            c.x + (p.x - start.x) / cropFrame.width))
                                        const ny = Math.max(Math.max(-1, 0.05 - c.height), Math.min(Math.min(2 - c.height, 0.95),
                                                            c.y + (p.y - start.y) / cropFrame.height))
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
                                            // Width from the pointer; the crop may extend up to one
                                            // image size beyond the right and bottom edges.
                                            let w = Math.max(0.05, p.x / cropFrame.width - c.x)
                                            w = Math.min(w, 2 - c.x, (2 - c.y) * a)
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
                                            const w = Math.max(0.05, Math.min(2 - c.x, p.x / cropFrame.width - c.x))
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
                                            const h = Math.max(0.05, Math.min(2 - c.y, p.y / cropFrame.height - c.y))
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
                visible: root.showErrorMap && root.imageAvailable
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
