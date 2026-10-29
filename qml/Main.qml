import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import FlydigiSpace

Window {
    id: window

    visible: true
    width: 1180
    height: 840
    minimumWidth: 820
    minimumHeight: 560
    title: "Space Station — " + (backend.model.length > 0 ? backend.model : "Flydigi cooler")
    color: Theme.bg

    onClosing: function(close) {
        close.accepted = false
        resident.hideWindow()
    }

    Shortcut {
        sequence: "Escape"
        onActivated: resident.hideWindow()
    }

    // The same actions as the buttons, for when the pointer is busy elsewhere.
    Shortcut {
        sequence: "M"
        onActivated: backend.holdSpeed(Math.max(500, backend.manual ? backend.manualRpm : backend.targetRpm))
    }

    Shortcut {
        sequence: "C"
        onActivated: backend.releaseManual()
    }

    Shortcut {
        sequence: "0"
        onActivated: backend.setLightMode("off")
    }

    Repeater {
        model: [1, 2, 3, 4, 5]

        delegate: Item {
            required property int modelData
            Shortcut {
                sequence: modelData.toString()
                onActivated: backend.setLightEffect(modelData)
            }
        }
    }

    Rectangle {
        anchors.fill: parent

        gradient: Gradient {
            GradientStop { position: 0.0; color: "#0B1222" }
            GradientStop { position: 0.55; color: "#080D16" }
            GradientStop { position: 1.0; color: "#05080D" }
        }

        // A thin accent edge, the only decoration the window needs.
        Rectangle {
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            height: 2
            gradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop { position: 0.0; color: "transparent" }
                GradientStop { position: 0.5; color: Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.55) }
                GradientStop { position: 1.0; color: "transparent" }
            }
        }

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 26
            spacing: 18

            // Header ---------------------------------------------------------
            RowLayout {
                Layout.fillWidth: true
                spacing: 14

                Rectangle {
                    width: 46
                    height: 46
                    radius: 14
                    color: Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.12)
                    border.width: 1
                    border.color: Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.35)

                    Canvas {
                        anchors.fill: parent
                        onPaint: {
                            const ctx = getContext("2d");
                            ctx.reset();
                            const cx = width / 2;
                            const cy = height / 2;
                            ctx.strokeStyle = "#22D3EE";
                            ctx.lineWidth = 2.4;
                            ctx.lineCap = "round";

                            for (let blade = 0; blade < 3; blade++) {
                                const angle = blade * Math.PI * 2 / 3 - Math.PI / 2;
                                ctx.beginPath();
                                ctx.arc(cx + Math.cos(angle) * 5.5, cy + Math.sin(angle) * 5.5, 6, angle - 0.7, angle + 0.9);
                                ctx.stroke();
                            }

                            ctx.beginPath();
                            ctx.arc(cx, cy, 2.4, 0, Math.PI * 2);
                            ctx.fillStyle = "#22D3EE";
                            ctx.fill();
                        }
                    }
                }

                ColumnLayout {
                    spacing: 1

                    Text {
                        text: "SPACE STATION"
                        color: Theme.muted
                        font.family: Theme.ui
                        font.pixelSize: 10
                        font.letterSpacing: 3
                    }

                    Text {
                        text: backend.model.length > 0 ? backend.model : "Flydigi cooler"
                        color: Theme.text
                        font.family: Theme.ui
                        font.pixelSize: 23
                        font.weight: Font.DemiBold
                    }
                }

                Item { Layout.fillWidth: true }

                Button {
                    objectName: "quitApplication"
                    text: "Quit"
                    Accessible.description: "Quit the GUI; the cooler daemon keeps running"
                    onClicked: resident.quit()
                }

                Pill {
                    label: backend.connected ? "LIVE" : "NO DAEMON"
                    tone: backend.connected ? Theme.good : Theme.bad
                    filled: true
                }

                Pill {
                    visible: backend.transport.length > 0
                    label: backend.transport === "bluetooth" ? "BLUETOOTH" : "CABLE"
                    tone: Theme.muted
                }

                Pill {
                    visible: backend.supply.length > 0
                    label: backend.supply === "full"
                           ? "SUPPLY FULL"
                           : backend.supply === "medium"
                             ? "SUPPLY MEDIUM · " + backend.supplyMaxRpm
                             : "SUPPLY LOW · " + backend.supplyMaxRpm
                    tone: backend.supply === "full" ? Theme.good : Theme.warn
                    filled: true
                }
            }

            // Body -----------------------------------------------------------
            RowLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 18

                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.minimumWidth: 460
                    radius: Theme.radius
                    color: Theme.surface
                    border.width: 1
                    border.color: Theme.line

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 20
                        spacing: 6

                        RowLayout {
                            Layout.fillWidth: true

                            Text {
                                text: "COOLING"
                                color: Theme.muted
                                font.family: Theme.ui
                                font.pixelSize: 11
                                font.weight: Font.DemiBold
                                font.letterSpacing: 2
                            }

                            Item { Layout.fillWidth: true }

                            Pill {
                                label: Math.round(100 * Math.min(1, backend.currentRpm / 4000)) + "%"
                                tone: Theme.muted
                            }
                        }

                        Gauge {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            value: backend.currentRpm
                            target: backend.targetRpm
                            ceiling: 4000
                            limit: backend.supplyMaxRpm
                            manual: backend.manual
                            leading: backend.leading
                        }
                    }
                }

                ScrollView {
                    id: sideScroll
                    Layout.preferredWidth: 430
                    Layout.fillHeight: true
                    clip: true
                    ScrollBar.vertical.policy: ScrollBar.AsNeeded
                    contentWidth: availableWidth

                    ColumnLayout {
                        width: sideScroll.availableWidth
                        spacing: 18

                        TempList {
                            Layout.fillWidth: true
                        }

                        Controls {
                            Layout.fillWidth: true
                        }
                    }
                }
            }

            // Footer ---------------------------------------------------------
            RowLayout {
                Layout.fillWidth: true
                spacing: 10

                Text {
                    text: "CURVES"
                    color: Theme.muted
                    font.family: Theme.ui
                    font.pixelSize: 10
                    font.weight: Font.DemiBold
                    font.letterSpacing: 2
                }

                Repeater {
                    model: backend.demands

                    delegate: Pill {
                        required property var modelData
                        label: modelData.name.toUpperCase() + "  " + modelData.temp + "°  →  " + modelData.rpm + " RPM"
                        tone: modelData.panic ? Theme.bad : Theme.muted
                        filled: modelData.panic
                    }
                }

                Item { Layout.fillWidth: true }

                Pill {
                    visible: backend.problem.length > 0
                    label: backend.problem
                    tone: Theme.bad
                    filled: true
                }

                Pill {
                    visible: !backend.configWritable
                    label: "CONFIG READ-ONLY"
                    tone: Theme.warn
                }

                Text {
                    text: backend.socketPath
                    color: Theme.muted
                    font.family: Theme.mono
                    font.pixelSize: 10
                    opacity: 0.6
                }
            }
        }
    }
}
