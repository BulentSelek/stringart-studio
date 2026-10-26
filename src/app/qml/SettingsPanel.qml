pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
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
                ComboField {
                    id: palette
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
