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

    // Window geometry and the selected parameter page, kept across sessions.
    Settings {
        id: windowState
        objectName: "windowState"
        category: "Window"
        property alias x: window.x
        property alias y: window.y
        property alias width: window.width
        property alias height: window.height
        property bool maximized: false
        property alias parameterPage: settings.currentCategory
    }

    Component.onCompleted: {
        // Ignore a stored position that is off every screen (e.g. a monitor
        // was disconnected); the window manager then places the window.
        const onScreen = window.x + 100 > 0 && window.x < Screen.desktopAvailableWidth - 100
                         && window.y >= 0 && window.y < Screen.desktopAvailableHeight - 100
        if (!onScreen) {
            window.x = (Screen.desktopAvailableWidth - window.width) / 2
            window.y = (Screen.desktopAvailableHeight - window.height) / 2
        }
        if (windowState.maximized)
            window.showMaximized()
    }
    onVisibilityChanged: visibility => {
        if (visibility === Window.Maximized || visibility === Window.Windowed)
            windowState.maximized = visibility === Window.Maximized
    }

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
            ToolSeparator {}
            ToolButton {
                objectName: "undoButton"
                text: "↶"
                font.pixelSize: 18
                enabled: window.controller.canUndo
                onClicked: window.controller.undo()
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Undo parameter change (Ctrl+Z)")
            }
            ToolButton {
                text: "↷"
                font.pixelSize: 18
                enabled: window.controller.canRedo
                onClicked: window.controller.redo()
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Redo (Ctrl+Shift+Z)")
            }
            ToolButton {
                id: presetsButton
                objectName: "presetsButton"
                text: qsTr("Presets ▾")
                onClicked: presetsMenu.popup(presetsButton, 0, presetsButton.height)

                Menu {
                    id: presetsMenu
                    MenuItem {
                        text: qsTr("Save current parameters as…")
                        onTriggered: presetDialog.open()
                    }
                    MenuSeparator { visible: window.controller.presetNames.length > 0 }
                    Repeater {
                        model: window.controller.presetNames
                        delegate: MenuItem {
                            required property string modelData
                            text: modelData
                            onTriggered: window.controller.loadPreset(modelData)
                        }
                    }
                    Menu {
                        title: qsTr("Delete preset")
                        enabled: window.controller.presetNames.length > 0
                        Repeater {
                            model: window.controller.presetNames
                            delegate: MenuItem {
                                required property string modelData
                                text: modelData
                                onTriggered: window.controller.deletePreset(modelData)
                            }
                        }
                    }
                }
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
                if (settings.currentCategory === settings.frameCategory)
                    imagePanel.showView(imagePanel.originalTab) // crop frame editable here
                else if (settings.currentCategory === settings.toneCategory)
                    imagePanel.showView(imagePanel.processedTab)
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            ImagePanel {
                id: imagePanel
                controller: window.controller
                highlightSegment: playbackPanel.highlight
                Layout.fillWidth: true
                Layout.fillHeight: true
            }

            PlaybackPanel {
                id: playbackPanel
                controller: window.controller
                visible: window.controller.hasResult
                Layout.fillWidth: true
                onViewRequested: imagePanel.showView(imagePanel.resultTab)
                onExportFrameRequested: frameDialog.open()
            }
        }

        ProgressPanel {
            controller: window.controller
            Layout.preferredWidth: 290
            Layout.fillHeight: true
            onExportPngRequested: pngDialog.open()
            onExportCsvRequested: csvDialog.open()
            onExportSvgRequested: svgDialog.open()
            onExportJsonRequested: jsonDialog.open()
            onExportTemplateRequested: paper => {
                templateDialog.paper = paper
                templateDialog.open()
            }
            onExportGuideRequested: guideDialog.open()
        }
    }

    WindingView {
        id: windingView
        controller: window.controller
    }

    Dialog {
        id: presetDialog
        objectName: "presetDialog"
        title: qsTr("Save preset")
        modal: true
        anchors.centerIn: parent
        standardButtons: Dialog.Save | Dialog.Cancel
        onOpened: {
            presetName.text = ""
            presetName.forceActiveFocus()
        }
        onAccepted: window.controller.savePreset(presetName.text)
        ColumnLayout {
            Label {
                text: qsTr("Frame, thread, colour, tone and run parameters (not the crop).")
                opacity: 0.7
            }
            TextField {
                id: presetName
                placeholderText: qsTr("Name, e.g. Portrait 300 pins")
                Layout.fillWidth: true
                onAccepted: presetDialog.accept()
            }
        }
    }

    Shortcut {
        sequences: [StandardKey.Undo]
        enabled: !window.controller.winding
        onActivated: window.controller.undo()
    }
    Shortcut {
        sequences: [StandardKey.Redo]
        enabled: !window.controller.winding
        onActivated: window.controller.redo()
    }

    // Playback shortcuts (ignored while a text field has focus).
    Shortcut {
        sequence: "Space"
        enabled: playbackPanel.visible && playbackPanel.enabled && !window.controller.winding
        onActivated: {
            if (window.controller.playing) {
                window.controller.stopPlayback()
            } else {
                window.controller.play()
                imagePanel.showView(imagePanel.resultTab)
            }
        }
    }
    Shortcut {
        sequences: [StandardKey.MoveToPreviousChar]
        enabled: playbackPanel.visible && playbackPanel.enabled && !window.controller.winding
        autoRepeat: true
        onActivated: window.controller.stepPlayback(-1)
    }
    Shortcut {
        sequences: [StandardKey.MoveToNextChar]
        enabled: playbackPanel.visible && playbackPanel.enabled && !window.controller.winding
        autoRepeat: true
        onActivated: window.controller.stepPlayback(1)
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
        id: frameDialog
        title: qsTr("Export winding frame")
        currentFolder: folders.exports
        fileMode: FileDialog.SaveFile
        defaultSuffix: "png"
        nameFilters: [qsTr("PNG image (*.png)")]
        onAccepted: {
            folders.exports = window.folderOf(selectedFile)
            window.controller.exportFrame(selectedFile)
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

    FileDialog {
        id: templateDialog
        property int paper: 0
        currentFolder: folders.exports
        title: qsTr("Export pin template")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "pdf"
        nameFilters: [qsTr("PDF document (*.pdf)")]
        onAccepted: {
            folders.exports = window.folderOf(selectedFile)
            window.controller.exportPinTemplate(selectedFile, paper)
        }
    }

    FileDialog {
        id: guideDialog
        currentFolder: folders.exports
        title: qsTr("Export winding guide")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "pdf"
        nameFilters: [qsTr("PDF document (*.pdf)")]
        onAccepted: {
            folders.exports = window.folderOf(selectedFile)
            window.controller.exportWindingGuide(selectedFile)
        }
    }
}
