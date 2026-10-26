pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

import StringArtStudio

Pane {
    id: root

    required property ProjectController controller

    signal openRequested()
    signal openProjectRequested()
    signal saveProjectRequested()
    signal importPinsRequested()

    Material.elevation: 2
    padding: 0

    component SectionLabel: Label {
        font.bold: true
        font.capitalization: Font.AllUppercase
        font.pixelSize: 12
        color: Material.accent
        topPadding: 14
    }

    component Hint: Label {
        Layout.fillWidth: true
        wrapMode: Text.WordWrap
        opacity: 0.6
        font.pixelSize: 11
    }

    component IntField: RowLayout {
        id: field
        property string label
        property int value
        property int from
        property int to
        property int stepSize: 1
        signal modified(int value)
        Layout.fillWidth: true

        Label {
            text: field.label
            opacity: 0.8
            Layout.fillWidth: true
        }
        SpinBox {
            from: field.from
            to: field.to
            stepSize: field.stepSize
            value: field.value
            editable: true
            onValueModified: field.modified(value)
            Layout.preferredWidth: 150
        }
    }

    ScrollView {
        anchors.fill: parent
        contentWidth: availableWidth
        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

        ColumnLayout {
            width: parent.width
            spacing: 6

            Item { Layout.preferredHeight: 4 }

            ColumnLayout {
                Layout.fillWidth: true
                Layout.leftMargin: 16
                Layout.rightMargin: 24 // keep clear of the vertical scroll bar
                spacing: 6

                Button {
                    text: qsTr("Open image…")
                    icon.name: "document-open"
                    highlighted: !root.controller.hasImage
                    enabled: !root.controller.running
                    Layout.fillWidth: true
                    onClicked: root.openRequested()
                }
                RowLayout {
                    Layout.fillWidth: true
                    enabled: !root.controller.running
                    Button {
                        text: qsTr("Open project…")
                        flat: true
                        Layout.fillWidth: true
                        onClicked: root.openProjectRequested()
                    }
                    Button {
                        text: qsTr("Save project…")
                        flat: true
                        Layout.fillWidth: true
                        onClicked: root.saveProjectRequested()
                    }
                }

                SectionLabel { text: qsTr("Preprocessing") }

                ParamSlider {
                    Layout.fillWidth: true
                    label: qsTr("Brightness")
                    from: -0.5; to: 0.5; stepSize: 0.01
                    value: root.controller.brightness
                    onMoved: v => root.controller.brightness = v
                }
                ParamSlider {
                    Layout.fillWidth: true
                    label: qsTr("Contrast")
                    from: 0.2; to: 3.0; stepSize: 0.05
                    value: root.controller.contrast
                    onMoved: v => root.controller.contrast = v
                }
                ParamSlider {
                    Layout.fillWidth: true
                    label: qsTr("Gamma")
                    from: 0.3; to: 3.0; stepSize: 0.05
                    value: root.controller.gamma
                    onMoved: v => root.controller.gamma = v
                }
                Switch {
                    text: qsTr("Invert")
                    checked: root.controller.invert
                    onToggled: root.controller.invert = checked
                }
                ParamSlider {
                    Layout.fillWidth: true
                    label: qsTr("Rotation")
                    from: -180; to: 180; stepSize: 0.5
                    decimals: 1
                    suffix: "°"
                    value: root.controller.rotation
                    onMoved: v => root.controller.rotation = v
                }
                RowLayout {
                    Layout.fillWidth: true
                    Button {
                        text: qsTr("⟲ 90°")
                        flat: true
                        onClicked: root.controller.rotateBy(-90)
                    }
                    Button {
                        text: qsTr("⟳ 90°")
                        flat: true
                        onClicked: root.controller.rotateBy(90)
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    Button {
                        text: qsTr("Fit whole photo")
                        flat: true
                        enabled: root.controller.hasImage
                        onClicked: root.controller.fitWholeImage()
                    }
                    Item { Layout.fillWidth: true }
                    Button {
                        text: qsTr("Reset crop")
                        flat: true
                        enabled: root.controller.crop.width > 0
                        onClicked: root.controller.resetCrop()
                    }
                }
                Hint { text: qsTr("Drag or resize the crop frame on the Original tab; it may extend beyond the photo (padding is empty canvas). Fit whole photo puts the entire photo inside the frame. For ellipse and rectangle frames the side handles change the frame aspect.") }

                Canvas {
                    id: histogram
                    Layout.fillWidth: true
                    Layout.preferredHeight: 60
                    visible: root.controller.hasImage
                    property var bins: root.controller.histogram
                    onBinsChanged: requestPaint()
                    onPaint: {
                        const ctx = getContext("2d")
                        ctx.reset()
                        ctx.fillStyle = "#30ffffff"
                        ctx.fillRect(0, 0, width, height)
                        if (!bins || bins.length === 0)
                            return
                        let max = 1
                        for (let i = 0; i < bins.length; ++i)
                            max = Math.max(max, bins[i])
                        ctx.fillStyle = "#ffc107"
                        const w = width / bins.length
                        for (let i = 0; i < bins.length; ++i) {
                            const h = Math.sqrt(bins[i] / max) * height
                            ctx.fillRect(i * w, height - h, Math.max(w, 1), h)
                        }
                    }
                }

                SectionLabel { text: qsTr("Frame and pins") }

                RowLayout {
                    Layout.fillWidth: true
                    enabled: !root.controller.running
                    Label {
                        text: qsTr("Shape")
                        opacity: 0.8
                        Layout.fillWidth: true
                    }
                    ComboBox {
                        model: [qsTr("Circle"), qsTr("Ellipse"), qsTr("Rectangle"), qsTr("Custom (CSV)")]
                        currentIndex: root.controller.frameShape
                        onActivated: index => {
                            if (index === ProjectController.Custom && root.controller.customPinCount === 0)
                                root.importPinsRequested()
                            else
                                root.controller.frameShape = index
                        }
                        Layout.preferredWidth: 150
                    }
                }
                IntField {
                    visible: root.controller.frameShape !== ProjectController.Custom
                    label: qsTr("Pin count")
                    from: 3; to: 2000
                    value: root.controller.pinCount
                    enabled: !root.controller.running
                    onModified: v => root.controller.pinCount = v
                }
                RowLayout {
                    Layout.fillWidth: true
                    visible: root.controller.frameShape === ProjectController.Custom
                    Label {
                        text: qsTr("%1 pins imported").arg(root.controller.customPinCount)
                        opacity: 0.8
                        Layout.fillWidth: true
                    }
                    Button {
                        text: qsTr("Import CSV…")
                        enabled: !root.controller.running
                        onClicked: root.importPinsRequested()
                    }
                }
                IntField {
                    visible: root.controller.frameShape !== ProjectController.Custom
                    label: root.controller.frameShape === ProjectController.Circle ? qsTr("Diameter (mm)")
                                                                                   : qsTr("Width (mm)")
                    from: 50; to: 5000; stepSize: 10
                    value: Math.round(root.controller.frameWidthMm)
                    enabled: !root.controller.running
                    onModified: v => root.controller.frameWidthMm = v
                }
                IntField {
                    visible: root.controller.frameShape === ProjectController.Ellipse
                             || root.controller.frameShape === ProjectController.Rectangle
                    label: qsTr("Height (mm)")
                    from: 50; to: 5000; stepSize: 10
                    value: Math.round(root.controller.frameHeightMm)
                    enabled: !root.controller.running
                    onModified: v => root.controller.frameHeightMm = v
                }
                IntField {
                    label: qsTr("Min. separation")
                    from: 1; to: 1000
                    value: root.controller.minSeparation
                    enabled: !root.controller.running
                    onModified: v => root.controller.minSeparation = v
                }
                IntField {
                    label: qsTr("Start pin")
                    from: 0; to: 19999
                    value: root.controller.startPin
                    enabled: !root.controller.running
                    onModified: v => root.controller.startPin = v
                }
                Hint {
                    visible: root.controller.frameShape === ProjectController.Ellipse
                             || root.controller.frameShape === ProjectController.Rectangle
                    text: qsTr("Width and height set the frame aspect.")
                }
                Hint {
                    text: qsTr("Canvas %1 × %2 px · %3 mm per px")
                          .arg(root.controller.canvasWidth).arg(root.controller.canvasHeight)
                          .arg(root.controller.mmPerPixel.toFixed(3))
                }

                SectionLabel { text: qsTr("Optimization") }

                RowLayout {
                    Layout.fillWidth: true
                    enabled: !root.controller.running
                    Label {
                        text: qsTr("Working size")
                        opacity: 0.8
                        Layout.fillWidth: true
                    }
                    ComboBox {
                        id: sizeBox
                        readonly property var sizes: [256, 384, 512, 768, 1024]
                        model: sizes.map(s => s + " px")
                        currentIndex: Math.max(0, sizes.indexOf(root.controller.workingSize))
                        onActivated: index => root.controller.workingSize = sizes[index]
                        Layout.preferredWidth: 150
                    }
                }
                IntField {
                    label: qsTr("Max. segments")
                    from: 1; to: 50000; stepSize: 100
                    value: root.controller.maxSegments
                    enabled: !root.controller.running
                    onModified: v => root.controller.maxSegments = v
                }
                IntField {
                    label: qsTr("Max. length (m)")
                    from: 0; to: 100000; stepSize: 100
                    value: Math.round(root.controller.maxLengthM)
                    enabled: !root.controller.running
                    onModified: v => root.controller.maxLengthM = v
                }
                Switch {
                    text: qsTr("Allow repeated lines")
                    checked: root.controller.allowRepeats
                    enabled: !root.controller.running
                    onToggled: root.controller.allowRepeats = checked
                }
                ParamSlider {
                    Layout.fillWidth: true
                    enabled: !root.controller.running
                    label: qsTr("Edge emphasis")
                    from: 0; to: 5; stepSize: 0.1
                    decimals: 1
                    value: root.controller.edgeEmphasis
                    onMoved: v => root.controller.edgeEmphasis = v
                }
                Hint { text: qsTr("Weights contours and features higher in the error, so more thread goes to eyes, mouth and outlines.") }

                SectionLabel { text: qsTr("Thread") }

                ParamSlider {
                    Layout.fillWidth: true
                    enabled: !root.controller.running
                    label: qsTr("Opacity")
                    from: 0.02; to: 1.0; stepSize: 0.01
                    value: root.controller.opacity
                    onMoved: v => root.controller.opacity = v
                }
                ParamSlider {
                    Layout.fillWidth: true
                    enabled: !root.controller.running
                    label: qsTr("Width")
                    from: 0.3; to: 4.0; stepSize: 0.1
                    decimals: 1
                    suffix: " px"
                    value: root.controller.threadWidth
                    onMoved: v => root.controller.threadWidth = v
                }
                Hint {
                    text: qsTr("≈ opaque thread of %1 mm diameter on this frame")
                          .arg((root.controller.opacity * root.controller.threadWidth
                                * root.controller.mmPerPixel).toFixed(2))
                }
                RowLayout {
                    Layout.fillWidth: true
                    Label {
                        text: qsTr("Colours")
                        opacity: 0.8
                        Layout.fillWidth: true
                    }
                    ComboBox {
                        id: paletteBox
                        readonly property var palettes: [
                            { name: qsTr("Black on white"), thread: "#000000", background: "#ffffff" },
                            { name: qsTr("White on black"), thread: "#ffffff", background: "#000000" },
                            { name: qsTr("Navy on cream"), thread: "#1a2340", background: "#f4ecd8" },
                            { name: qsTr("Red on white"), thread: "#8b0000", background: "#ffffff" }
                        ]
                        model: palettes.map(p => p.name)
                        Layout.preferredWidth: 150
                        onActivated: index => {
                            root.controller.threadColor = palettes[index].thread
                            root.controller.backgroundColor = palettes[index].background
                        }
                    }
                }
                Hint { text: qsTr("Colours change the preview only. The optimizer always fits thread darkness to image luminance; for light-on-dark, enable Invert.") }

                Item { Layout.preferredHeight: 12 }
            }
        }
    }
}
