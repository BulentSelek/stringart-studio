pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Dialogs
import QtQuick.Layouts

import StringArtStudio

// Parameters grouped by workflow step: Image → Frame → Thread → Run. Each
// page shows the essential settings; secondary ones sit in a collapsed
// "Advanced" section.
Pane {
    id: root

    required property ProjectController controller

    readonly property int imageCategory: 0
    readonly property int frameCategory: 1
    property alias currentCategory: categories.currentIndex
    // Shared width of input controls so their right edges and sizes line up.
    readonly property int fieldWidth: 150

    signal importPinsRequested()

    Material.elevation: 2
    padding: 0

    component SectionLabel: Label {
        font.bold: true
        font.capitalization: Font.AllUppercase
        font.pixelSize: 12
        color: Material.accent
        topPadding: 10
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
        property string help
        property int value
        property int from
        property int to
        property int stepSize: 1
        signal modified(int value)
        Layout.fillWidth: true

        HelpLabel {
            text: field.label
            help: field.help
            Layout.fillWidth: true
        }
        SpinBox {
            from: field.from
            to: field.to
            stepSize: field.stepSize
            value: field.value
            editable: true
            onValueModified: field.modified(value)
            Layout.preferredWidth: root.fieldWidth
        }
    }

    component ComboField: RowLayout {
        id: combo
        property string label
        property string help
        property alias model: box.model
        property alias currentIndex: box.currentIndex
        property alias displayText: box.displayText
        signal activated(int index)
        Layout.fillWidth: true

        HelpLabel {
            text: combo.label
            help: combo.help
            Layout.fillWidth: true
        }
        ComboBox {
            id: box
            Layout.preferredWidth: root.fieldWidth
            onActivated: index => combo.activated(index)
        }
    }

    // Collapsible group for secondary parameters.
    component Advanced: ColumnLayout {
        id: advanced
        property bool expanded: false
        default property alias content: body.data
        Layout.fillWidth: true
        spacing: 4

        Button {
            id: toggle
            flat: true
            text: (advanced.expanded ? "▾ " : "▸ ") + qsTr("Advanced")
            Layout.fillWidth: true
            contentItem: Label {
                text: toggle.text
                opacity: 0.8
                horizontalAlignment: Text.AlignLeft
            }
            onClicked: advanced.expanded = !advanced.expanded
        }
        ColumnLayout {
            id: body
            visible: advanced.expanded
            Layout.fillWidth: true
            spacing: 6
        }
    }

    component Page: ScrollView {
        id: page
        default property alias content: column.data
        contentWidth: availableWidth
        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

        ColumnLayout {
            id: column
            x: 16
            width: page.availableWidth - 32
            spacing: 6
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        TabBar {
            id: categories
            objectName: "categoryTabs"
            Layout.fillWidth: true
            TabButton { text: qsTr("1 Image") }
            TabButton { text: qsTr("2 Frame") }
            TabButton { text: qsTr("3 Thread") }
            TabButton { text: qsTr("4 Run") }
        }

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: categories.currentIndex

            // ------------------------------------------------------- Image
            Page {
                SectionLabel { text: qsTr("Tone") }
                ParamSlider {
                    Layout.fillWidth: true
                    label: qsTr("Brightness")
                    help: qsTr("Adds a constant to every gray value. Raise it to keep light areas free of thread.")
                    from: -0.5; to: 0.5; stepSize: 0.01
                    value: root.controller.brightness
                    onMoved: v => root.controller.brightness = v
                }
                ParamSlider {
                    Layout.fillWidth: true
                    label: qsTr("Contrast")
                    help: qsTr("Stretches gray values around mid-gray. String art usually benefits from 1.2 – 1.6.")
                    from: 0.2; to: 3.0; stepSize: 0.05
                    value: root.controller.contrast
                    onMoved: v => root.controller.contrast = v
                }
                ParamSlider {
                    Layout.fillWidth: true
                    label: qsTr("Gamma")
                    help: qsTr("Above 1 brightens mid-tones, below 1 darkens them; black and white stay fixed.")
                    from: 0.3; to: 3.0; stepSize: 0.05
                    value: root.controller.gamma
                    onMoved: v => root.controller.gamma = v
                }
                Switch {
                    text: qsTr("Invert")
                    checked: root.controller.invert
                    onToggled: root.controller.invert = checked
                    ToolTip.visible: hovered
                    ToolTip.delay: 300
                    ToolTip.text: qsTr("For light thread on a dark background.")
                }
                Canvas {
                    id: histogram
                    Layout.fillWidth: true
                    Layout.preferredHeight: 48
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

                SectionLabel { text: qsTr("Position") }
                ParamSlider {
                    Layout.fillWidth: true
                    label: qsTr("Rotation")
                    help: qsTr("Rotates the photo clockwise. New corners are empty canvas.")
                    from: -180; to: 180; stepSize: 0.5
                    decimals: 1
                    suffix: "°"
                    value: root.controller.rotation
                    onMoved: v => root.controller.rotation = v
                }
                GridLayout {
                    Layout.fillWidth: true
                    columns: 2
                    columnSpacing: 4
                    rowSpacing: 0
                    Button {
                        text: qsTr("⟲ 90°")
                        flat: true
                        Layout.fillWidth: true
                        onClicked: root.controller.rotateBy(-90)
                    }
                    Button {
                        text: qsTr("⟳ 90°")
                        flat: true
                        Layout.fillWidth: true
                        onClicked: root.controller.rotateBy(90)
                    }
                    Button {
                        text: qsTr("Fit whole photo")
                        flat: true
                        enabled: root.controller.hasImage
                        Layout.fillWidth: true
                        onClicked: root.controller.fitWholeImage()
                    }
                    Button {
                        text: qsTr("Reset crop")
                        flat: true
                        enabled: root.controller.crop.width > 0
                        Layout.fillWidth: true
                        onClicked: root.controller.resetCrop()
                    }
                }
                Hint { text: qsTr("Move or resize the yellow frame on the Original view. It may extend beyond the photo; the extra area stays empty.") }
            }

            // ------------------------------------------------------- Frame
            Page {
                SectionLabel { text: qsTr("Shape") }
                ComboField {
                    label: qsTr("Shape")
                    help: qsTr("Physical frame the pins are mounted on. Custom reads pin positions (x, y in mm) from a CSV file.")
                    enabled: !root.controller.running
                    model: [qsTr("Circle"), qsTr("Ellipse"), qsTr("Rectangle"), qsTr("Custom (CSV)")]
                    currentIndex: root.controller.frameShape
                    onActivated: index => {
                        if (index === ProjectController.Custom && root.controller.customPinCount === 0)
                            root.importPinsRequested()
                        else
                            root.controller.frameShape = index
                    }
                }
                IntField {
                    visible: root.controller.frameShape !== ProjectController.Custom
                    label: root.controller.frameShape === ProjectController.Circle ? qsTr("Diameter (mm)")
                                                                                   : qsTr("Width (mm)")
                    help: qsTr("Physical size of the pin frame; used for thread length and exported coordinates.")
                    from: 50; to: 5000; stepSize: 10
                    value: Math.round(root.controller.frameWidthMm)
                    enabled: !root.controller.running
                    onModified: v => root.controller.frameWidthMm = v
                }
                IntField {
                    visible: root.controller.frameShape === ProjectController.Ellipse
                             || root.controller.frameShape === ProjectController.Rectangle
                    label: qsTr("Height (mm)")
                    help: qsTr("Width and height set the frame aspect. The side handles of the crop frame change it too.")
                    from: 50; to: 5000; stepSize: 10
                    value: Math.round(root.controller.frameHeightMm)
                    enabled: !root.controller.running
                    onModified: v => root.controller.frameHeightMm = v
                }
                RowLayout {
                    Layout.fillWidth: true
                    visible: root.controller.frameShape === ProjectController.Custom
                    Label {
                        text: qsTr("%1 pins imported").arg(root.controller.customPinCount)
                        opacity: 0.85
                        Layout.fillWidth: true
                    }
                    Button {
                        text: qsTr("Import CSV…")
                        enabled: !root.controller.running
                        onClicked: root.importPinsRequested()
                    }
                }

                SectionLabel { text: qsTr("Pins") }
                IntField {
                    visible: root.controller.frameShape !== ProjectController.Custom
                    label: qsTr("Pin count")
                    help: qsTr("More pins give finer detail but more winding work. Typical: 200 – 300.")
                    from: 3; to: 2000
                    value: root.controller.pinCount
                    enabled: !root.controller.running
                    onModified: v => root.controller.pinCount = v
                }
                IntField {
                    label: qsTr("Min. separation")
                    help: qsTr("Minimum number of pins between the two ends of a thread. Prevents short threads along the rim.")
                    from: 1; to: 1000
                    value: root.controller.minSeparation
                    enabled: !root.controller.running
                    onModified: v => root.controller.minSeparation = v
                }
                Hint {
                    text: qsTr("Canvas %1 × %2 px · %3 mm per px")
                          .arg(root.controller.canvasWidth).arg(root.controller.canvasHeight)
                          .arg(root.controller.mmPerPixel.toFixed(3))
                }

                Advanced {
                    IntField {
                        label: qsTr("Start pin")
                        help: qsTr("Pin where winding begins. Pin 0 is at the top (top-left corner for rectangles).")
                        from: 0; to: 19999
                        value: root.controller.startPin
                        enabled: !root.controller.running
                        onModified: v => root.controller.startPin = v
                    }
                }
            }

            // ------------------------------------------------------ Thread
            Page {
                SectionLabel { text: qsTr("Thread") }
                ParamSlider {
                    Layout.fillWidth: true
                    enabled: !root.controller.running
                    label: qsTr("Opacity")
                    help: qsTr("How much one thread darkens the pixels it crosses. Lower values need more threads and give smoother tones.")
                    from: 0.02; to: 1.0; stepSize: 0.01
                    value: root.controller.opacity
                    onMoved: v => root.controller.opacity = v
                }
                ParamSlider {
                    Layout.fillWidth: true
                    enabled: !root.controller.running
                    label: qsTr("Width")
                    help: qsTr("Thread width in working-image pixels.")
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

                SectionLabel { text: qsTr("Colours") }
                Switch {
                    text: qsTr("Multi-colour threads")
                    checked: root.controller.colorMode
                    enabled: !root.controller.running
                    onToggled: root.controller.colorMode = checked
                    ToolTip.visible: hovered
                    ToolTip.delay: 300
                    ToolTip.text: qsTr("Optimize several coloured threads at once; each colour is its own continuous thread.")
                }

                // Single colour: colours only change the preview.
                ComboField {
                    id: palette
                    visible: !root.controller.colorMode
                    readonly property var palettes: [
                        { name: qsTr("Black on white"), thread: "#000000", background: "#ffffff" },
                        { name: qsTr("White on black"), thread: "#ffffff", background: "#000000" },
                        { name: qsTr("Navy on cream"), thread: "#1a2340", background: "#f4ecd8" },
                        { name: qsTr("Red on white"), thread: "#8b0000", background: "#ffffff" }
                    ]
                    label: qsTr("Palette")
                    help: qsTr("Preview and export colours. The optimizer fits thread darkness to image luminance; for light thread on dark, enable Invert.")
                    model: palettes.map(p => p.name)
                    onActivated: index => {
                        root.controller.threadColor = palettes[index].thread
                        root.controller.backgroundColor = palettes[index].background
                    }
                }

                // Multi-colour: the palette is part of the optimization.
                ColumnLayout {
                    visible: root.controller.colorMode
                    enabled: !root.controller.running
                    Layout.fillWidth: true
                    spacing: 4

                    ComboField {
                        readonly property var presets: [
                            { name: qsTr("Black, white, red, blue, yellow"), colors: ["#000000", "#ffffff", "#c0392b", "#1f4e8c", "#f0c000"], background: "#ffffff" },
                            { name: qsTr("Black, white, red, yellow, green"), colors: ["#000000", "#ffffff", "#c0392b", "#f0c000", "#2e8b57"], background: "#ffffff" },
                            { name: qsTr("Black, red, blue"), colors: ["#000000", "#b03a2e", "#1f4e8c"], background: "#ffffff" },
                            { name: qsTr("Black and white on grey"), colors: ["#000000", "#ffffff"], background: "#8a8a8a" },
                            { name: qsTr("CMY + black"), colors: ["#00a0e0", "#e0007a", "#f0d000", "#000000"], background: "#ffffff" },
                            { name: qsTr("Warm portrait on cream"), colors: ["#000000", "#7a4a2a", "#c9a27a"], background: "#f4ecd8" }
                        ]
                        label: qsTr("Preset")
                        help: qsTr("Starting palettes; edit the colours below.")
                        model: presets.map(p => p.name)
                        currentIndex: -1
                        displayText: qsTr("Choose…")
                        onActivated: index => root.controller.setPalette(presets[index].colors, presets[index].background)
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        HelpLabel {
                            text: qsTr("From image")
                            help: qsTr("Chooses this many thread colours for the current image, frame and background by test runs on a small copy of the problem. Takes a few seconds.")
                            Layout.fillWidth: true
                        }
                        SpinBox {
                            id: suggestCount
                            from: 1
                            to: 8
                            value: Math.max(1, root.controller.palette.length)
                            editable: true
                            enabled: !root.controller.suggestingPalette
                            Layout.preferredWidth: 96
                        }
                        Button {
                            objectName: "suggestPaletteButton"
                            text: root.controller.suggestingPalette
                                  ? qsTr("%1 %").arg(Math.round(root.controller.suggestionProgress * 100))
                                  : qsTr("Suggest")
                            enabled: root.controller.hasImage
                            Layout.preferredWidth: 96
                            onClicked: root.controller.suggestingPalette ? root.controller.cancelPaletteSuggestion()
                                                                         : root.controller.suggestPalette(suggestCount.value)
                            ToolTip.visible: hovered
                            ToolTip.text: root.controller.suggestingPalette ? qsTr("Cancel")
                                                                            : qsTr("Suggest colours from the image")
                        }
                    }

                    Repeater {
                        model: root.controller.palette
                        delegate: RowLayout {
                            id: paletteRow
                            required property int index
                            required property color modelData
                            Layout.fillWidth: true

                            Rectangle {
                                implicitWidth: 28
                                implicitHeight: 20
                                radius: 3
                                color: paletteRow.modelData
                                border.color: "#80ffffff"
                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: colorDialog.edit(paletteRow.index, paletteRow.modelData)
                                }
                            }
                            Label {
                                text: qsTr("Thread %1  %2").arg(paletteRow.index + 1).arg(String(paletteRow.modelData))
                                font.family: "monospace"
                                Layout.fillWidth: true
                            }
                            ToolButton {
                                text: "✕"
                                enabled: root.controller.palette.length > 1
                                onClicked: root.controller.removePaletteColor(paletteRow.index)
                                ToolTip.visible: hovered
                                ToolTip.text: qsTr("Remove colour")
                            }
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        Button {
                            text: qsTr("Add colour…")
                            flat: true
                            enabled: root.controller.palette.length < 8
                            onClicked: colorDialog.edit(-1, "#808080")
                        }
                        Item { Layout.fillWidth: true }
                        HelpLabel {
                            text: qsTr("Background")
                            help: qsTr("Colour of the board behind the threads.")
                        }
                        Rectangle {
                            implicitWidth: 28
                            implicitHeight: 20
                            radius: 3
                            color: root.controller.colorBackground
                            border.color: "#80ffffff"
                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: colorDialog.edit(-2, root.controller.colorBackground)
                            }
                        }
                    }
                    ComboField {
                        label: qsTr("Layer order")
                        help: qsTr("Each colour is wound completely before the next, so the spool changes only a few times; each layer is optimized over the others. This sets which colour is wound first (bottom). Later layers lie on top and show more. Auto builds light-to-dark and dark-to-light and keeps the better one (twice the time). Palette order uses the list above from top to bottom.")
                        model: [qsTr("Auto"), qsTr("Light to dark"), qsTr("Dark to light"), qsTr("Palette order")]
                        currentIndex: root.controller.layerOrder
                        onActivated: index => root.controller.layerOrder = index
                    }
                    Hint { text: qsTr("Each colour is a separate continuous thread. Colour thread portraits typically combine black and white with a few colours and use about 8000 lines (Run page). Refine and the virtual reconstruction are single-colour only.") }
                }

                ColorDialog {
                    id: colorDialog
                    // -1 = add a colour, -2 = background, >= 0 = palette index
                    property int target: -1
                    function edit(index, current) {
                        target = index
                        selectedColor = current
                        open()
                    }
                    onAccepted: {
                        if (target === -2)
                            root.controller.colorBackground = selectedColor
                        else if (target === -1)
                            root.controller.addPaletteColor(selectedColor)
                        else
                            root.controller.setPaletteColor(target, selectedColor)
                    }
                }
            }

            // --------------------------------------------------------- Run
            Page {
                SectionLabel { text: qsTr("Quality") }
                ComboField {
                    readonly property var sizes: [256, 384, 512, 768, 1024]
                    label: qsTr("Working size")
                    help: qsTr("Resolution used for optimization (long side). Higher is more detailed and slower.")
                    enabled: !root.controller.running
                    model: sizes.map(s => s + " px")
                    currentIndex: Math.max(0, sizes.indexOf(root.controller.workingSize))
                    onActivated: index => root.controller.workingSize = sizes[index]
                }
                IntField {
                    label: qsTr("Max. segments")
                    help: qsTr("Upper limit on thread segments. The run also stops earlier when no thread improves the image.")
                    from: 1; to: 50000; stepSize: 100
                    value: root.controller.maxSegments
                    enabled: !root.controller.running
                    onModified: v => root.controller.maxSegments = v
                }
                ParamSlider {
                    Layout.fillWidth: true
                    enabled: !root.controller.running
                    label: qsTr("Edge emphasis")
                    help: qsTr("Weights contours and features higher, so more thread goes to eyes, mouth and outlines. 0 = plain error.")
                    from: 0; to: 5; stepSize: 0.1
                    decimals: 1
                    value: root.controller.edgeEmphasis
                    onMoved: v => root.controller.edgeEmphasis = v
                }

                Advanced {
                    IntField {
                        label: qsTr("Max. length (m)")
                        help: qsTr("Stop when the thread would exceed this length. 0 = unlimited.")
                        from: 0; to: 100000; stepSize: 100
                        value: Math.round(root.controller.maxLengthM)
                        enabled: !root.controller.running
                        onModified: v => root.controller.maxLengthM = v
                    }
                    IntField {
                        label: qsTr("Refine passes")
                        help: qsTr("Sweeps of \"Refine sequence\" over the whole thread (stops earlier when a sweep finds nothing).")
                        from: 1; to: 50
                        value: root.controller.refinePasses
                        enabled: !root.controller.running
                        onModified: v => root.controller.refinePasses = v
                    }
                    IntField {
                        label: qsTr("Refine reach (pins)")
                        help: qsTr("\"Refine sequence\" tries moving each pin up to this many positions along the frame. 0 = all pins (slower).")
                        from: 0; to: 1000
                        value: root.controller.refineNeighborhood
                        enabled: !root.controller.running
                        onModified: v => root.controller.refineNeighborhood = v
                    }
                    Switch {
                        text: qsTr("Allow repeated lines")
                        checked: root.controller.allowRepeats
                        enabled: !root.controller.running
                        onToggled: root.controller.allowRepeats = checked
                        ToolTip.visible: hovered
                        ToolTip.delay: 300
                        ToolTip.text: qsTr("Lets the same pin pair be used more than once (darker lines, harder to wind).")
                    }
                }
                Hint { text: qsTr("Start, progress and export are on the right.") }
            }
        }
    }
}
