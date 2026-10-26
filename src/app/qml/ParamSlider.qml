import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Labelled slider that reports user changes through `moved` so the caller
// can write them back to the controller without breaking the value binding.
// `help` is shown as a tooltip when hovering the label.
ColumnLayout {
    id: root

    property string label
    property string help
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

        HelpLabel {
            text: root.label
            help: root.help
            Layout.fillWidth: true
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
