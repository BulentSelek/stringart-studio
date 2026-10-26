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

    header: ToolBar {
        Material.background: "#26262b"
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 8
            anchors.rightMargin: 8
            enabled: !window.controller.running
            ToolButton {
                text: qsTr("Open image…")
                icon.name: "document-open"
                onClicked: openDialog.open()
            }
            ToolSeparator {}
            ToolButton {
                text: qsTr("Open project…")
                onClicked: openProjectDialog.open()
            }
            ToolButton {
                text: qsTr("Save project…")
                onClicked: saveProjectDialog.open()
            }
            Item { Layout.fillWidth: true }
            Label {
                text: window.controller.imageName
                opacity: 0.7
                elide: Text.ElideMiddle
                Layout.maximumWidth: 400
            }
        }
    }

    RowLayout {
        anchors.fill: parent
        spacing: 0

        SettingsPanel {
            id: settings
            controller: window.controller
            Layout.preferredWidth: 340
            Layout.fillHeight: true
            onImportPinsRequested: pinsDialog.open()
            // Show the view that matches the selected parameter group.
            onCurrentCategoryChanged: {
                if (settings.currentCategory === settings.imageCategory)
                    imagePanel.showView(imagePanel.originalTab)
                else if (settings.currentCategory === settings.frameCategory)
                    imagePanel.showView(imagePanel.processedTab)
            }
        }

        ImagePanel {
            id: imagePanel
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
