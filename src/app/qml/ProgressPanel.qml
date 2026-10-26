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

        Label {
            text: qsTr("Optimization")
            font.bold: true
            font.pixelSize: 16
        }

        Button {
            text: qsTr("Start")
            highlighted: true
            Material.foreground: "#202020"
            enabled: root.controller.hasImage && !root.controller.running
            Layout.fillWidth: true
            onClicked: root.controller.start()
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

        Label {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            opacity: 0.6
            font.pixelSize: 11
            text: qsTr("The result is a single continuous thread: every segment starts at the previous segment's end pin.")
        }

        MenuSeparator { Layout.fillWidth: true }

        Label {
            text: qsTr("Export")
            font.bold: true
            font.pixelSize: 16
        }

        RowLayout {
            Layout.fillWidth: true
            Label {
                text: qsTr("PNG size")
                opacity: 0.7
                Layout.fillWidth: true
            }
            ComboBox {
                readonly property var sizes: [1024, 2048, 4096, 8192]
                model: sizes.map(v => v + " px")
                currentIndex: Math.max(0, sizes.indexOf(root.controller.exportSize))
                onActivated: index => root.controller.exportSize = sizes[index]
                Layout.preferredWidth: 140
            }
        }
        Button {
            text: root.controller.exporting ? qsTr("Rendering…") : qsTr("Export PNG…")
            enabled: root.controller.hasResult && !root.controller.running && !root.controller.exporting
            Layout.fillWidth: true
            onClicked: root.exportPngRequested()
        }

        Switch {
            text: qsTr("CSV in millimetres")
            checked: root.controller.csvMillimeters
            onToggled: root.controller.csvMillimeters = checked
        }
        Button {
            text: qsTr("Export winding CSV…")
            enabled: root.controller.hasResult && !root.controller.running
            Layout.fillWidth: true
            onClicked: root.exportCsvRequested()
        }

        RowLayout {
            Layout.fillWidth: true
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

        Item { Layout.fillHeight: true }

        Label {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            text: root.controller.statusText
            opacity: 0.85
        }
    }
}
