pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

import StringArtStudio

Pane {
    id: root

    required property ProjectController controller

    signal exportPngRequested()
    signal exportCsvRequested()
    signal exportSvgRequested()
    signal exportJsonRequested()
    signal exportTemplateRequested(int paper)
    signal exportGuideRequested()

    Material.elevation: 2

    component Stat: RowLayout {
        id: stat
        property string label
        property string value
        Layout.fillWidth: true
        Label {
            text: stat.label
            opacity: 0.7
            Layout.fillWidth: true
        }
        Label {
            text: stat.value
            font.family: "monospace"
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 8

        ScrollView {
            id: scroll
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentWidth: availableWidth
            clip: true

            ColumnLayout {
                width: scroll.availableWidth
                spacing: 8

                Label {
                    text: qsTr("Optimization")
                    font.bold: true
                    font.pixelSize: 16
                }

                AccentButton {
                    text: qsTr("Start")
                    enabled: root.controller.hasImage && !root.controller.running
                    Layout.fillWidth: true
                    onClicked: root.controller.start()
                }

                RowLayout {
                    Layout.fillWidth: true

                    Button {
                        text: qsTr("Refine")
                        enabled: root.controller.hasResult && !root.controller.running && !root.controller.resultIsColor
                        Layout.fillWidth: true
                        Layout.preferredWidth: 1
                        onClicked: root.controller.refineSequence()
                        ToolTip.visible: hovered
                        ToolTip.delay: 300
                        ToolTip.text: qsTr("Refine the sequence: local search on the finished thread that moves, removes or inserts single pins where that lowers the error. The thread stays continuous.")
                    }
                    Button {
                        text: root.controller.computingVirtual ? qsTr("Computing…") : qsTr("Virtual (LS)")
                        enabled: root.controller.hasResult && !root.controller.running && !root.controller.computingVirtual
                                 && !root.controller.resultIsColor
                        Layout.fillWidth: true
                        Layout.preferredWidth: 1
                        onClicked: root.controller.computeVirtual()
                        ToolTip.visible: hovered
                        ToolTip.delay: 300
                        ToolTip.text: qsTr("Virtual reconstruction: least-squares weights for the result's lines, an upper bound on quality for this line set. Not windable; shown in the Virtual view.")
                    }
                }

                RowLayout {
                    Layout.fillWidth: true

                    Button {
                        text: root.controller.paused ? qsTr("Resume") : qsTr("Pause")
                        enabled: root.controller.running
                        Layout.fillWidth: true
                        Layout.preferredWidth: 1
                        onClicked: root.controller.paused ? root.controller.resume() : root.controller.pause()
                    }
                    Button {
                        text: qsTr("Cancel")
                        enabled: root.controller.running
                        Layout.fillWidth: true
                        Layout.preferredWidth: 1
                        onClicked: root.controller.cancel()
                    }
                }

                ProgressBar {
                    Layout.fillWidth: true
                    from: 0
                    to: Math.max(1, root.controller.runMaxSegments)
                    value: root.controller.segments
                    indeterminate: root.controller.running && root.controller.segments === 0
                }

                Stat {
                    label: qsTr("Segments")
                    value: root.controller.segments + " / " + root.controller.runMaxSegments
                }
                Stat {
                    label: qsTr("Error (weighted MSE)")
                    value: root.controller.error.toFixed(5)
                }
                Stat {
                    label: qsTr("Error reduction")
                    value: root.controller.initialError > 0
                           ? ((1 - root.controller.error / root.controller.initialError) * 100).toFixed(1) + " %"
                           : "—"
                }
                Stat {
                    label: qsTr("Elapsed")
                    value: root.controller.elapsedSeconds.toFixed(2) + " s"
                }
                Stat {
                    label: qsTr("Thread length")
                    value: root.controller.threadLengthM.toFixed(1) + " m"
                }
                // Per colour when there are several threads.
                Repeater {
                    model: root.controller.resultIsColor && !root.controller.running ? root.controller.threadUsage : []
                    delegate: RowLayout {
                        id: usageRow
                        required property var modelData
                        required property int index
                        Layout.fillWidth: true
                        Layout.leftMargin: 12
                        Rectangle {
                            implicitWidth: 12
                            implicitHeight: 12
                            radius: 2
                            color: usageRow.modelData.color
                            border.color: "#80ffffff"
                        }
                        Label {
                            text: qsTr("Thread %1").arg(usageRow.index + 1)
                            opacity: 0.7
                            Layout.fillWidth: true
                        }
                        Label {
                            text: usageRow.modelData.lengthM.toFixed(1) + " m"
                            font.family: "monospace"
                        }
                    }
                }

                Label {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    opacity: 0.6
                    font.pixelSize: 11
                    text: root.controller.resultIsColor
                          ? (root.controller.layeredColors
                             ? qsTr("Each colour is one continuous thread, wound layer by layer from the bottom; the spool changes only between layers.")
                             : qsTr("Each colour is one continuous thread; the interleaved winding order switches between spools often."))
                          : qsTr("The result is a single continuous thread: every segment starts at the previous segment's end pin.")
                }

                MenuSeparator { Layout.fillWidth: true }

                Label {
                    text: qsTr("Export")
                    font.bold: true
                    font.pixelSize: 16
                }

                RowLayout {
                    Layout.fillWidth: true
                    ComboBox {
                        readonly property var sizes: [1024, 2048, 4096, 8192]
                        model: sizes.map(v => v + " px")
                        currentIndex: Math.max(0, sizes.indexOf(root.controller.exportSize))
                        onActivated: index => root.controller.exportSize = sizes[index]
                        Layout.preferredWidth: 110
                        ToolTip.visible: hovered
                        ToolTip.delay: 300
                        ToolTip.text: qsTr("PNG size (long side)")
                    }
                    Button {
                        text: root.controller.exporting ? qsTr("Rendering…") : qsTr("PNG…")
                        enabled: root.controller.hasResult && !root.controller.running && !root.controller.exporting
                        Layout.fillWidth: true
                        onClicked: root.exportPngRequested()
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    Button {
                        text: qsTr("CSV…")
                        enabled: root.controller.hasResult && !root.controller.running
                        Layout.fillWidth: true
                        Layout.preferredWidth: 1
                        onClicked: root.exportCsvRequested()
                        ToolTip.visible: hovered
                        ToolTip.delay: 300
                        ToolTip.text: qsTr("Winding sequence as CSV")
                    }
                    Button {
                        text: qsTr("SVG…")
                        enabled: root.controller.hasResult && !root.controller.running
                        Layout.fillWidth: true
                        Layout.preferredWidth: 1
                        onClicked: root.exportSvgRequested()
                    }
                    Button {
                        text: qsTr("JSON…")
                        enabled: !root.controller.running
                        Layout.fillWidth: true
                        Layout.preferredWidth: 1
                        onClicked: root.exportJsonRequested()
                    }
                }

                Switch {
                    text: qsTr("CSV in millimetres")
                    checked: root.controller.csvMillimeters
                    onToggled: root.controller.csvMillimeters = checked
                }

                Label {
                    text: qsTr("Workshop")
                    font.bold: true
                    font.pixelSize: 16
                    Layout.topMargin: 4
                }
                RowLayout {
                    Layout.fillWidth: true
                    Button {
                        id: templateButton
                        objectName: "templateButton"
                        text: qsTr("Pin template…")
                        enabled: !root.controller.running && !root.controller.exporting
                        Layout.fillWidth: true
                        Layout.preferredWidth: 1
                        onClicked: paperMenu.popup(templateButton, 0, templateButton.height)
                        ToolTip.visible: hovered
                        ToolTip.delay: 300
                        ToolTip.text: qsTr("1:1 PDF with every pin position and number, to drill or nail through.")
                        Menu {
                            id: paperMenu
                            MenuItem { text: qsTr("A4 sheets"); onTriggered: root.exportTemplateRequested(0) }
                            MenuItem { text: qsTr("A3 sheets"); onTriggered: root.exportTemplateRequested(1) }
                            MenuItem { text: qsTr("Letter sheets"); onTriggered: root.exportTemplateRequested(2) }
                            MenuItem { text: qsTr("One sheet (plotter)"); onTriggered: root.exportTemplateRequested(3) }
                        }
                    }
                    Button {
                        text: qsTr("Winding guide…")
                        enabled: root.controller.hasResult && !root.controller.running && !root.controller.exporting
                        Layout.fillWidth: true
                        Layout.preferredWidth: 1
                        onClicked: root.exportGuideRequested()
                        ToolTip.visible: hovered
                        ToolTip.delay: 300
                        ToolTip.text: qsTr("Printable A4 list of all steps with tick boxes and thread lengths.")
                    }
                }
            }
        }

        Label {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            text: root.controller.statusText
            opacity: 0.85
        }
    }
}
