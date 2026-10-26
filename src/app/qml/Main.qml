pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Dialogs
import QtQuick.Layouts

import StringArtStudio

ApplicationWindow {
    id: window

    required property ProjectController controller

    width: 1440
    height: 900
    minimumWidth: 1100
    minimumHeight: 700
    visible: true
    title: window.controller.imageName.length > 0
           ? qsTr("String Art Studio — %1").arg(window.controller.imageName)
           : qsTr("String Art Studio")

    Material.theme: Material.Dark
    Material.accent: Material.Amber

    RowLayout {
        anchors.fill: parent
        spacing: 0

        SettingsPanel {
            controller: window.controller
            Layout.preferredWidth: 330
            Layout.fillHeight: true
            onOpenRequested: openDialog.open()
            onOpenProjectRequested: openProjectDialog.open()
            onSaveProjectRequested: saveProjectDialog.open()
            onImportPinsRequested: pinsDialog.open()
        }

        ImagePanel {
            controller: window.controller
            Layout.fillWidth: true
            Layout.fillHeight: true
        }

        ProgressPanel {
            controller: window.controller
            Layout.preferredWidth: 290
            Layout.fillHeight: true
            onExportPngRequested: pngDialog.open()
            onExportCsvRequested: csvDialog.open()
            onExportSvgRequested: svgDialog.open()
            onExportJsonRequested: jsonDialog.open()
        }
    }

    DropArea {
        id: dropArea
        anchors.fill: parent
        keys: ["text/uri-list"]
        onDropped: drop => {
            if (drop.hasUrls && drop.urls.length > 0) {
                window.controller.loadImage(drop.urls[0])
                drop.acceptProposedAction()
            }
        }

        Rectangle {
            anchors.fill: parent
            visible: dropArea.containsDrag
            color: "#40ffc107"
            border.color: Material.accent
            border.width: 3

            Label {
                anchors.centerIn: parent
                text: qsTr("Drop image to load")
                font.pixelSize: 28
            }
        }
    }

    FileDialog {
        id: openDialog
        title: qsTr("Open image")
        nameFilters: [qsTr("Images (*.png *.jpg *.jpeg *.bmp *.webp)"), qsTr("All files (*)")]
        onAccepted: window.controller.loadImage(selectedFile)
    }

    FileDialog {
        id: openProjectDialog
        title: qsTr("Open project")
        nameFilters: [qsTr("String Art Studio project (*.sasproj)"), qsTr("JSON (*.json)"), qsTr("All files (*)")]
        onAccepted: window.controller.openProject(selectedFile)
    }

    FileDialog {
        id: saveProjectDialog
        title: qsTr("Save project")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "sasproj"
        nameFilters: [qsTr("String Art Studio project (*.sasproj)")]
        onAccepted: window.controller.saveProject(selectedFile)
    }

    FileDialog {
        id: pinsDialog
        title: qsTr("Import pin positions (x, y in mm)")
        nameFilters: [qsTr("CSV (*.csv *.txt)"), qsTr("All files (*)")]
        onAccepted: window.controller.importPins(selectedFile)
    }

    FileDialog {
        id: svgDialog
        title: qsTr("Export SVG")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "svg"
        nameFilters: [qsTr("SVG image (*.svg)")]
        onAccepted: window.controller.exportSvg(selectedFile)
    }

    FileDialog {
        id: jsonDialog
        title: qsTr("Export JSON")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "json"
        nameFilters: [qsTr("JSON (*.json)")]
        onAccepted: window.controller.exportJson(selectedFile)
    }

    FileDialog {
        id: pngDialog
        title: qsTr("Export PNG")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "png"
        nameFilters: [qsTr("PNG image (*.png)")]
        onAccepted: window.controller.exportPng(selectedFile)
    }

    FileDialog {
        id: csvDialog
        title: qsTr("Export winding sequence")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "csv"
        nameFilters: [qsTr("CSV file (*.csv)")]
        onAccepted: window.controller.exportCsv(selectedFile)
    }
}
