import QtQuick
import FlydigiSpace

/// The speed dial: a 270° arc from the lower left over the top to the lower
/// right, with the supply ceiling marked so a clamped target is visible rather
/// than surprising.
Item {
    id: root

    property real value: 0
    property real target: 0
    property real ceiling: 4000
    property real limit: 4000
    property bool manual: false
    property string leading: ""

    readonly property real clamped: Math.min(value, ceiling) / Math.max(ceiling, 1)
    readonly property int percent: Math.round(100 * clamped)
    readonly property color arcColor: manual ? Theme.warn : Theme.accent

    implicitWidth: 380
    implicitHeight: 380

    onValueChanged: dial.requestPaint()
    onLimitChanged: dial.requestPaint()
    onManualChanged: dial.requestPaint()
    onWidthChanged: dial.requestPaint()
    onHeightChanged: dial.requestPaint()

    Canvas {
        id: dial
        anchors.fill: parent

        onPaint: {
            const ctx = getContext("2d");
            ctx.reset();

            const cx = width / 2;
            const cy = height / 2;
            const radius = Math.min(width, height) / 2 - 30;
            const start = 135 * Math.PI / 180;
            const span = 270 * Math.PI / 180;

            // Ticks, denser at the quarter marks.
            for (let i = 0; i <= 54; i++) {
                const angle = start + span * i / 54;
                const major = i % 9 === 0;
                const outer = radius - 16;
                const inner = outer - (major ? 9 : 4);
                ctx.beginPath();
                ctx.lineWidth = major ? 2 : 1;
                ctx.strokeStyle = major ? "rgba(233,240,250,0.28)" : "rgba(233,240,250,0.10)";
                ctx.moveTo(cx + Math.cos(angle) * inner, cy + Math.sin(angle) * inner);
                ctx.lineTo(cx + Math.cos(angle) * outer, cy + Math.sin(angle) * outer);
                ctx.stroke();
            }

            // Track.
            ctx.beginPath();
            ctx.lineWidth = 15;
            ctx.lineCap = "round";
            ctx.strokeStyle = "rgba(255,255,255,0.055)";
            ctx.arc(cx, cy, radius, start, start + span);
            ctx.stroke();

            // The part of the dial the supply cannot reach.
            const limitFraction = Math.min(1, Math.max(0, root.limit / Math.max(root.ceiling, 1)));
            if (limitFraction < 0.995) {
                ctx.beginPath();
                ctx.lineWidth = 15;
                ctx.lineCap = "butt";
                ctx.strokeStyle = "rgba(251,191,36,0.10)";
                ctx.arc(cx, cy, radius, start + span * limitFraction, start + span);
                ctx.stroke();
            }

            // The value itself, with a soft halo underneath.
            const sweep = start + span * Math.max(0, Math.min(1, root.clamped));
            if (sweep > start + 0.01) {
                ctx.beginPath();
                ctx.lineWidth = 26;
                ctx.lineCap = "round";
                ctx.strokeStyle = Qt.rgba(root.arcColor.r, root.arcColor.g, root.arcColor.b, 0.14);
                ctx.arc(cx, cy, radius, start, sweep);
                ctx.stroke();

                const gradient = ctx.createLinearGradient(cx - radius, cy + radius, cx + radius, cy - radius);
                gradient.addColorStop(0, "#22D3EE");
                gradient.addColorStop(1, root.manual ? "#FBBF24" : "#3B82F6");

                ctx.beginPath();
                ctx.lineWidth = 15;
                ctx.lineCap = "round";
                ctx.strokeStyle = gradient;
                ctx.arc(cx, cy, radius, start, sweep);
                ctx.stroke();

                // The tip.
                ctx.beginPath();
                ctx.arc(cx + Math.cos(sweep) * radius, cy + Math.sin(sweep) * radius, 7, 0, Math.PI * 2);
                ctx.fillStyle = "#EAFBFF";
                ctx.fill();
            }

            // The ceiling marker.
            if (limitFraction < 0.995) {
                const angle = start + span * limitFraction;
                ctx.beginPath();
                ctx.lineWidth = 3;
                ctx.strokeStyle = "rgba(251,191,36,0.75)";
                ctx.moveTo(cx + Math.cos(angle) * (radius - 13), cy + Math.sin(angle) * (radius - 13));
                ctx.lineTo(cx + Math.cos(angle) * (radius + 13), cy + Math.sin(angle) * (radius + 13));
                ctx.stroke();
            }
        }

        HoverHandler {
            cursorShape: Qt.ArrowCursor
        }
    }

    Column {
        anchors.centerIn: parent
        spacing: 2

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: Math.round(root.value)
            color: Theme.text
            font.family: Theme.mono
            font.pixelSize: 72
            font.weight: Font.Bold
        }

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: "RPM"
            color: Theme.muted
            font.family: Theme.ui
            font.pixelSize: 11
            font.letterSpacing: 4
        }

        Item { width: 1; height: 14 }

        Row {
            anchors.horizontalCenter: parent.horizontalCenter
            spacing: 6

            Text {
                text: "target"
                color: Theme.muted
                font.family: Theme.ui
                font.pixelSize: 12
            }

            Text {
                text: Math.round(root.target)
                color: Theme.text
                font.family: Theme.mono
                font.pixelSize: 12
                font.weight: Font.DemiBold
            }
        }

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            visible: root.leading.length > 0 && !root.manual
            text: root.leading.length > 0 ? "curve " + root.leading : ""
            color: Theme.accent
            font.family: Theme.ui
            font.pixelSize: 11
            topPadding: 2
        }

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            visible: root.manual
            text: "held by hand"
            color: Theme.warn
            font.family: Theme.ui
            font.pixelSize: 11
            topPadding: 2
        }
    }
}
