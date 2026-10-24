import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import FlydigiSpace

/// Everything that changes what the cooler does: the speed, the stored gears,
/// what it does on its own and whether the strip is lit.
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
        spacing: 12

        // Speed -------------------------------------------------------------
        RowLayout {
            Layout.fillWidth: true
            Text {
                text: "SPEED"
                color: Theme.muted
                font.family: Theme.ui
                font.pixelSize: 11
                font.weight: Font.DemiBold
                font.letterSpacing: 2
            }
            Item { Layout.fillWidth: true }

            RowLayout {
                spacing: 6

                Repeater {
                    model: [
                        { label: "CURVE", isManual: false },
                        { label: "MANUAL", isManual: true }
                    ]

                    delegate: Rectangle {
                        required property var modelData
                        required property int index

                        readonly property bool selected: backend.manual === modelData.isManual

                        width: 78
                        height: 28
                        radius: 9
                        color: selected ? Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.16) : "transparent"
                        border.width: 1
                        border.color: selected ? Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.5) : Theme.line

                        Text {
                            anchors.centerIn: parent
                            text: parent.modelData.label
                            color: parent.selected ? Theme.accent : Theme.muted
                            font.family: Theme.ui
                            font.pixelSize: 10
                            font.weight: Font.DemiBold
                            font.letterSpacing: 1
                        }

                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                if (parent.modelData.isManual)
                                    backend.holdSpeed(Math.max(500, backend.targetRpm));
                                else
                                    backend.releaseManual();
                            }
                        }
                    }
                }
            }
        }

        Slider {
            id: slider

            Layout.fillWidth: true
            from: 0
            to: Math.max(500, backend.supplyMaxRpm)
            stepSize: 50
            value: backend.manual ? backend.manualRpm : backend.targetRpm

            Connections {
                target: backend
                function onChanged() {
                    if (!slider.pressed)
                        slider.value = backend.manual ? backend.manualRpm : backend.targetRpm;
                }
            }

            onMoved: backend.holdSpeed(Math.round(value))

            background: Rectangle {
                x: 0
                y: (parent.height - height) / 2
                width: parent.width
                height: 6
                radius: 3
                color: Theme.line

                Rectangle {
                    width: parent.width * slider.visualPosition
                    height: parent.height
                    radius: 3
                    color: slider.pressed ? Theme.warn : Theme.accent
                }
            }

            handle: Rectangle {
                x: slider.visualPosition * (slider.width - width)
                y: (slider.height - height) / 2
                width: 18
                height: 18
                radius: 9
                color: Theme.text
                border.width: 3
                border.color: slider.pressed ? Theme.warn : Theme.accent
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Text {
                text: backend.manual ? "holding " + backend.manualRpm + " rpm" : "following the curve"
                color: Theme.muted
                font.family: Theme.ui
                font.pixelSize: 11
            }
            Item { Layout.fillWidth: true }
            Text {
                visible: backend.supplyMaxRpm < 4000
                text: "supply caps at " + backend.supplyMaxRpm
                color: Theme.warn
                font.family: Theme.ui
                font.pixelSize: 11
            }
        }

        Rectangle {
            Layout.fillWidth: true
            height: 1
            color: Theme.line
        }

        // Gears -------------------------------------------------------------
        Text {
            text: "STORED GEARS"
            color: Theme.muted
            font.family: Theme.ui
            font.pixelSize: 11
            font.weight: Font.DemiBold
            font.letterSpacing: 2
        }

        GridLayout {
            Layout.fillWidth: true
            columns: 2
            columnSpacing: 10
            rowSpacing: 10

            Repeater {
                model: backend.gears

                delegate: Rectangle {
                    id: gearCard

                    required property var modelData

                    readonly property bool active: !backend.manual
                        && Math.abs(modelData.rpm - backend.targetRpm) < 40

                    Layout.fillWidth: true
                    height: 50
                    radius: 12
                    color: active ? Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.14) : Theme.surfaceHi
                    border.width: 1
                    border.color: active ? Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.45) : Theme.line
                    opacity: modelData.allowed ? 1.0 : 0.45

                    Column {
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.left: parent.left
                        anchors.leftMargin: 14
                        spacing: 1

                        Text {
                            text: gearCard.modelData.name
                            color: Theme.text
                            font.family: Theme.ui
                            font.pixelSize: 12
                            font.weight: Font.DemiBold
                            font.capitalization: Font.AllUppercase
                            font.letterSpacing: 0.6
                        }

                        Text {
                            text: gearCard.modelData.allowed
                                  ? gearCard.modelData.rpm + " rpm"
                                  : "needs more power"
                            color: gearCard.modelData.allowed ? Theme.muted : Theme.warn
                            font.family: Theme.ui
                            font.pixelSize: 10
                        }
                    }

                    MouseArea {
                        anchors.fill: parent
                        enabled: parent.modelData.allowed
                        cursorShape: enabled ? Qt.PointingHandCursor : Qt.ForbiddenCursor
                        onClicked: backend.writeGear(parent.modelData.name, parent.modelData.rpm)
                    }
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            height: 1
            color: Theme.line
        }

        // Standby and strip --------------------------------------------------
        RowLayout {
            Layout.fillWidth: true
            spacing: 10

            Text {
                text: "STANDBY"
                color: Theme.muted
                font.family: Theme.ui
                font.pixelSize: 11
                font.weight: Font.DemiBold
                font.letterSpacing: 2
            }

            Item { Layout.fillWidth: true }

            Repeater {
                model: [
                    { label: "off", value: "off" },
                    { label: "instant", value: "instant" },
                    { label: "delayed", value: "delayed" }
                ]

                delegate: Rectangle {
                    required property var modelData

                    readonly property bool selected: backend.standby === modelData.value

                    width: 66
                    height: 26
                    radius: 8
                    color: selected ? Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.16) : "transparent"
                    border.width: 1
                    border.color: selected ? Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.45) : Theme.line

                    Text {
                        anchors.centerIn: parent
                        text: parent.modelData.label
                        color: parent.selected ? Theme.accent : Theme.muted
                        font.family: Theme.ui
                        font.pixelSize: 10
                    }

                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: backend.setStandby(parent.modelData.value)
                    }
                }
            }
        }

        // Lighting ----------------------------------------------------------
        Rectangle {
            Layout.fillWidth: true
            height: 1
            color: Theme.line
        }

        RowLayout {
            Layout.fillWidth: true

            Text {
                text: "LIGHTING"
                color: Theme.muted
                font.family: Theme.ui
                font.pixelSize: 11
                font.weight: Font.DemiBold
                font.letterSpacing: 2
            }

            Item { Layout.fillWidth: true }

            Repeater {
                model: [
                    { label: "OFF", value: "off" },
                    { label: "COLOR", value: "static" },
                    { label: "EFFECT", value: "effect" }
                ]

                delegate: Rectangle {
                    required property var modelData

                    readonly property bool selected: backend.lightMode === modelData.value

                    width: 64
                    height: 24
                    radius: 8
                    color: selected ? Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.16) : "transparent"
                    border.width: 1
                    border.color: selected ? Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.45) : Theme.line

                    Text {
                        anchors.centerIn: parent
                        text: parent.modelData.label
                        color: parent.selected ? Theme.accent : Theme.muted
                        font.family: Theme.ui
                        font.pixelSize: 10
                        font.weight: Font.DemiBold
                        font.letterSpacing: 0.8
                    }

                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: backend.setLightMode(parent.modelData.value)
                    }
                }
            }
        }

        // One fixed colour, picked from the eight the product ships with.
        RowLayout {
            Layout.fillWidth: true
            spacing: 6
            visible: backend.lightMode === "static"

            Repeater {
                model: ["#22D3EE", "#3B82F6", "#8B5CF6", "#EC4899",
                        "#EF4444", "#F59E0B", "#22C55E", "#F8FAFC"]

                delegate: Rectangle {
                    required property string modelData

                    readonly property bool selected: backend.lightColor === modelData

                    width: 22
                    height: 22
                    radius: 11
                    color: modelData
                    border.width: selected ? 2 : 1
                    border.color: selected ? Theme.text : Qt.rgba(1, 1, 1, 0.16)

                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: backend.setLightColor(parent.modelData)
                    }
                }
            }

            Item { Layout.fillWidth: true }
        }

        // The five presets the firmware keeps in its own buffer: three breath
        // colours, a gradient and the rainbow.
        RowLayout {
            Layout.fillWidth: true
            spacing: 6
            visible: backend.lightMode === "effect"

            Repeater {
                model: [
                    { label: "BREATH G", effect: 1 },
                    { label: "BREATH A", effect: 2 },
                    { label: "BREATH R", effect: 3 },
                    { label: "GRADIENT", effect: 4 },
                    { label: "RAINBOW", effect: 5 }
                ]

                delegate: Rectangle {
                    required property var modelData

                    readonly property bool selected: backend.lightEffect === modelData.effect

                    Layout.fillWidth: true
                    height: 26
                    radius: 8
                    color: selected ? Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.16) : Theme.surfaceHi
                    border.width: 1
                    border.color: selected ? Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.5) : Theme.line

                    Text {
                        anchors.centerIn: parent
                        width: parent.width - 4
                        text: parent.modelData.label
                        color: parent.selected ? Theme.accent : Theme.muted
                        font.family: Theme.ui
                        font.pixelSize: 8
                        font.weight: Font.DemiBold
                        font.letterSpacing: 0.4
                        horizontalAlignment: Text.AlignHCenter
                    }

                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: backend.setLightEffect(parent.modelData.effect)
                    }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 10
            visible: backend.lightMode !== "off"

            Text {
                text: "BRIGHTNESS"
                color: Theme.muted
                font.family: Theme.ui
                font.pixelSize: 10
                font.weight: Font.DemiBold
                font.letterSpacing: 1
            }

            Slider {
                id: brightness

                Layout.fillWidth: true
                from: 0
                to: 100
                stepSize: 5
                value: backend.lightBrightness

                Connections {
                    target: backend
                    function onChanged() {
                        if (!brightness.pressed)
                            brightness.value = backend.lightBrightness;
                    }
                }

                onMoved: backend.setLightBrightness(Math.round(value))

                background: Rectangle {
                    x: 0
                    y: (parent.height - height) / 2
                    width: parent.width
                    height: 5
                    radius: 2.5
                    color: Theme.line

                    Rectangle {
                        width: parent.width * brightness.visualPosition
                        height: parent.height
                        radius: 2.5
                        color: Theme.accent
                    }
                }

                handle: Rectangle {
                    x: brightness.visualPosition * (brightness.width - width)
                    y: (brightness.height - height) / 2
                    width: 14
                    height: 14
                    radius: 7
                    color: Theme.text
                    border.width: 2
                    border.color: Theme.accent
                }
            }

            Text {
                Layout.preferredWidth: 34
                text: backend.lightBrightness + "%"
                color: Theme.muted
                font.family: Theme.mono
                font.pixelSize: 10
                horizontalAlignment: Text.AlignRight
            }
        }

        RowLayout {
            Layout.fillWidth: true

            Text {
                text: "GEAR LIGHTS"
                color: Theme.muted
                font.family: Theme.ui
                font.pixelSize: 10
                font.weight: Font.DemiBold
                font.letterSpacing: 1
            }

            Item { Layout.fillWidth: true }

            Rectangle {
                width: 44
                height: 24
                radius: 12
                color: backend.lightIndicators ? Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.35) : Theme.surfaceHi
                border.width: 1
                border.color: backend.lightIndicators ? Theme.accent : Theme.line

                Rectangle {
                    width: 16
                    height: 16
                    radius: 8
                    y: 4
                    x: backend.lightIndicators ? parent.width - width - 4 : 4
                    color: backend.lightIndicators ? Theme.accent : Theme.muted

                    Behavior on x {
                        NumberAnimation { duration: 140; easing.type: Easing.OutCubic }
                    }
                }

                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: backend.setLightIndicators(!backend.lightIndicators)
                }
            }
        }
    }
}
