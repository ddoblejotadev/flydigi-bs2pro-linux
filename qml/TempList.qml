import QtQuick
import QtQuick.Layouts
import FlydigiSpace

/// Temperatures the daemon can read, ranked so the CPU package, the disk and
/// the GPU are always the ones on screen.
Rectangle {
    id: card

    color: Theme.surface
    radius: Theme.radius
    border.width: 1
    border.color: Theme.line

    implicitHeight: content.implicitHeight + 44

    ColumnLayout {
        id: content
        anchors.fill: parent
        anchors.margins: 18
        spacing: 10

        RowLayout {
            Layout.fillWidth: true
            Text {
                text: "SENSORS"
                color: Theme.muted
                font.family: Theme.ui
                font.pixelSize: 11
                font.weight: Font.DemiBold
                font.letterSpacing: 2
            }
            Item { Layout.fillWidth: true }
            Text {
                visible: backend.temps.length === 0
                text: "no readings"
                color: Theme.muted
                font.family: Theme.ui
                font.pixelSize: 11
            }
        }

        Repeater {
            model: backend.temps

            delegate: RowLayout {
                required property var modelData

                Layout.fillWidth: true
                spacing: 12

                Text {
                    Layout.preferredWidth: 78
                    text: modelData.name
                    color: Theme.text
                    elide: Text.ElideRight
                    font.family: Theme.ui
                    font.pixelSize: 13
                }

                Rectangle {
                    Layout.fillWidth: true
                    height: 6
                    radius: 3
                    color: Theme.line

                    Rectangle {
                        width: parent.width * Math.min(1, modelData.value / 100)
                        height: parent.height
                        radius: 3
                        color: modelData.value >= 80 ? Theme.bad
                             : modelData.value >= 65 ? Theme.warn
                             : Theme.good

                        Behavior on width {
                            NumberAnimation { duration: 220; easing.type: Easing.OutCubic }
                        }
                    }
                }

                Text {
                    Layout.preferredWidth: 46
                    horizontalAlignment: Text.AlignRight
                    text: modelData.value + "°C"
                    color: Theme.text
                    font.family: Theme.mono
                    font.pixelSize: 13
                    font.weight: Font.DemiBold
                }
            }
        }
    }
}
