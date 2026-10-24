# Space Station for Linux

A native Qt 6 / QML control panel for Flydigi BS-series laptop coolers, built for
Linux desktops. It does the job the Windows-only Flydigi Space Station does, with
no Wine and no Electron in the loop.

![The dashboard with the lighting editor open](docs/screenshot.png)

## What it does

- Live speed gauge, with the target the fan curves are asking for
- CPU, NVMe and chassis temperatures, taken from the daemon
- Manual hold: drag the slider, or press `M` / `C`
- The four speeds stored in the cooler, with the ones the power supply cannot
  reach disabled and labelled
- Standby behaviour: off, instant or delayed
- Side strip: off, a solid colour from eight presets, or one of the five firmware
  animations (three breath colours, a gradient and a rainbow), with brightness
- The gear indicator LEDs, which are a separate light from the strip

## Requirements

- Qt 6.5 or newer: Core, Gui, Qml, Quick
- A running [`flydigictld`](https://github.com/ElXreno/flydigictl) daemon. It owns
  the cooler and talks to it over hidraw; this app never opens the device
  itself, it asks the daemon over its newline-delimited JSON socket
- The daemon socket at `/run/flydigictl/flydigictl.sock` or
  `$XDG_RUNTIME_DIR/flydigictl.sock` (`--socket <path>` overrides both)

## Build and install

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"
cmake --install build --prefix "$HOME/.local"
```

The install step puts the binary in `~/.local/bin`, the QML module in
`~/.local/share/flydigi-space/qml`, a desktop entry in
`~/.local/share/applications` and the icon in the hicolor theme. It then shows up
as **Space Station** in the application menu.

## Keyboard

| Key | Action |
| --- | --- |
| `M` | hold the current target speed by hand |
| `C` | give the cooler back to its fan curves |
| `1`–`5` | pick a strip animation |
| `0` | turn the strip off |
| `Esc` | quit |

## The lighting payload

`LightMode` is an internally tagged enum *inside* the `Lighting` struct, so the
mode object carries its own tag and the JSON is one level deeper than it looks:

```json
{
  "request": "set_lighting",
  "lighting": {
    "mode": { "mode": "effect", "effect": 5 },
    "brightness": 80,
    "indicators": true
  }
}
```

A flat `"mode": "effect"` is rejected with `invalid type: string "effect",
expected internally tagged enum LightMode`. That failure is easy to miss from a
GUI because the socket still answers, just with an error object.

## Notes

- The daemon decides how fast the cooler may spin: a low-power supply caps it at
  2700 rpm, and the UI shows that ceiling instead of pretending otherwise.
- Manual mode is a hold, not a lock: the daemon keeps it until `C` or another
  client asks for the curves again.

## Credits

The protocol knowledge and the daemon this app talks to come from
[`flydigictl`](https://github.com/ElXreno/flydigictl) by ElXreno. The vendor
feature vocabulary (Static, Breath, Gradient, Rainbow) comes from the official
Space Station app, inspected for reference only.
