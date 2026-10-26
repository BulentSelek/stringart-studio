import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Labelled slider that reports user changes through `moved` so the caller
// can write them back to the controller without breaking the value binding.
ColumnLayout {
    id: root

    property string label
    property real value
    property real from: 0
    property real to: 1
    property real stepSize: 0.01
    property int decimals: 2
    property string suffix

    signal moved(real value)

    spacing: 0

    RowLayout {
        Layout.fillWidth: true

        Label {
            text: root.label
            Layout.fillWidth: true
            opacity: 0.8
        }
        Label {
            text: root.value.toFixed(root.decimals) + root.suffix
            font.family: "monospace"
        }
    }

    Slider {
        Layout.fillWidth: true
        from: root.from
        to: root.to
        stepSize: root.stepSize
        value: root.value
        onMoved: root.moved(value)
    }
}
