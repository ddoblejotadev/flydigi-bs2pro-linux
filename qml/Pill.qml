import QtQuick
import FlydigiSpace

/// A small status chip. The tone carries the meaning; the label is the text.
Rectangle {
    id: pill

    property string label: ""
    property color tone: Theme.muted
    property bool filled: false

    implicitWidth: content.implicitWidth + 22
    implicitHeight: 26
    radius: height / 2
    color: filled ? Qt.rgba(tone.r, tone.g, tone.b, 0.16) : "transparent"
    border.width: 1
    border.color: Qt.rgba(tone.r, tone.g, tone.b, filled ? 0.35 : 0.28)

    Text {
        id: content
        anchors.centerIn: parent
        text: pill.label
        color: pill.tone
        font.family: Theme.ui
        font.pixelSize: 11
        font.weight: Font.Medium
        font.letterSpacing: 0.6
    }
}
