pragma ComponentBehavior: Bound

import QtCore
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

    // Last folder per kind of file, kept across sessions.
    Settings {
        id: folders
        objectName: "dialogFolders"
        category: "FileDialogs"
        property url images: StandardPaths.writableLocation(StandardPaths.PicturesLocation)
        property url projects: StandardPaths.writableLocation(StandardPaths.DocumentsLocation)
        property url pins: StandardPaths.writableLocation(StandardPaths.DocumentsLocation)
        property url exports: StandardPaths.writableLocation(StandardPaths.PicturesLocation)
    }

    function folderOf(fileUrl: url): url {
        const path = fileUrl.toString()
        return path.substring(0, path.lastIndexOf("/"))
    }

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
                if (window.controller.loadImage(drop.urls[0]))
                    folders.images = window.folderOf(drop.urls[0])
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
        objectName: "openImageDialog"
        currentFolder: folders.images
        title: qsTr("Open image")
        nameFilters: [qsTr("Images (*.png *.jpg *.jpeg *.bmp *.webp)"), qsTr("All files (*)")]
        onAccepted: {
            folders.images = window.folderOf(selectedFile)
            window.controller.loadImage(selectedFile)
        }
    }

    FileDialog {
        id: openProjectDialog
        currentFolder: folders.projects
        title: qsTr("Open project")
        nameFilters: [qsTr("String Art Studio project (*.sasproj)"), qsTr("JSON (*.json)"), qsTr("All files (*)")]
        onAccepted: {
            folders.projects = window.folderOf(selectedFile)
            window.controller.openProject(selectedFile)
        }
    }

    FileDialog {
        id: saveProjectDialog
        currentFolder: folders.projects
        title: qsTr("Save project")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "sasproj"
        nameFilters: [qsTr("String Art Studio project (*.sasproj)")]
        onAccepted: {
            folders.projects = window.folderOf(selectedFile)
            window.controller.saveProject(selectedFile)
        }
    }

    FileDialog {
        id: pinsDialog
        currentFolder: folders.pins
        title: qsTr("Import pin positions (x, y in mm)")
        nameFilters: [qsTr("CSV (*.csv *.txt)"), qsTr("All files (*)")]
        onAccepted: {
            folders.pins = window.folderOf(selectedFile)
            window.controller.importPins(selectedFile)
        }
    }

    FileDialog {
        id: svgDialog
        currentFolder: folders.exports
        title: qsTr("Export SVG")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "svg"
        nameFilters: [qsTr("SVG image (*.svg)")]
        onAccepted: {
            folders.exports = window.folderOf(selectedFile)
            window.controller.exportSvg(selectedFile)
        }
    }

    FileDialog {
        id: jsonDialog
        currentFolder: folders.exports
        title: qsTr("Export JSON")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "json"
        nameFilters: [qsTr("JSON (*.json)")]
        onAccepted: {
            folders.exports = window.folderOf(selectedFile)
            window.controller.exportJson(selectedFile)
        }
    }

    FileDialog {
        id: pngDialog
        currentFolder: folders.exports
        title: qsTr("Export PNG")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "png"
        nameFilters: [qsTr("PNG image (*.png)")]
        onAccepted: {
            folders.exports = window.folderOf(selectedFile)
            window.controller.exportPng(selectedFile)
        }
    }

    FileDialog {
        id: csvDialog
        currentFolder: folders.exports
        title: qsTr("Export winding sequence")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "csv"
        nameFilters: [qsTr("CSV file (*.csv)")]
        onAccepted: {
            folders.exports = window.folderOf(selectedFile)
            window.controller.exportCsv(selectedFile)
        }
    }
}
