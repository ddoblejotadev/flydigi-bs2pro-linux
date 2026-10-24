# flydigi-space — native Omarchy dashboard for the Flydigi BS2 Pro

Objective: a native Linux dashboard that controls the Flydigi BS2 Pro through the
running `flydigictld` daemon, with a Space Station-like interface.
Branch: `main` (new repository). Delivery: work-unit commits. Test runner: none
(QML/C++ UI; verification is build + live run against the daemon).

## Specs

S1. The application connects to `flydigictld` over its newline-delimited JSON
    unix socket. Default path `/run/flydigictl/flydigictl.sock`, falling back to
    `$XDG_RUNTIME_DIR/flydigictl.sock`; `--socket <path>` overrides. It reconnects
    every second while the daemon is absent and never blocks the UI thread.

S2. It shows live cooler state from the daemon's `status` reply: model,
    `current_rpm`, `target_rpm`, `manual`, `supply`, `supply_max_rpm`,
    `transport`, `leading`, and `connected`. Updates arrive through a
    `subscribe` connection, not polling.

S3. It shows temperatures reported by the daemon's `sensors` reply, one row per
    selected input, refreshed with the status stream.

S4. Manual control: a slider spans 0 to `supply_max_rpm` (falling back to 4000
    when the daemon reports no ceiling). Moving it sends `set_manual`; a single
    "Auto" action sends `set_manual` with `rpm: null`. Manual mode is visible.

S5. Gear presets: the four stored gears from `gears` are shown with name and rpm.
    Gears the supply cannot carry (`allowed: false`) are disabled with the reason
    visible. Selecting one sends `set_gear`.

S6. Standby selector: `off`, `instant`, `delayed` send `set_standby`.

S7. Lighting editor against `set_lighting`: mode `off`; mode `static` with a
    colour from eight presets; mode `effect` with one of the five firmware
    presets (breath green, breath amber, breath red, gradient, rainbow). One
    brightness slider (0-100) and one toggle for the gear indicator LEDs, which
    are a separate light. The wire shape nests the mode tag:
    `"mode":{"mode":"effect","effect":5}` — a flat `"mode":"effect"` is rejected
    because `LightMode` is an internally tagged enum inside `Lighting`.

S8. Desktop integration: installs as `flydigi-space` with a `.desktop` entry and
    icon, launches without a terminal, and follows the Omarchy dark palette.

S9. Vendor reference: the Windows installer is Inno Setup 6.7.0, which
    `innoextract` 1.9 cannot read. It is installed under Wine and its Electron
    payload extracted from `resources/app.asar`; the vendor feature names
    (Static, Breath, Gradient, Rainbow, Gear Light Display) set the vocabulary
    this UI uses.

## Tasks

T1. Scaffold: CMake + Qt6 Quick app, backend client over the daemon socket, window. — S1
T2. Dashboard: gauge, live status, temperature rows. — S2, S3
T3. Controls: manual slider, gears, standby. — S4, S5, S6
T4. Desktop integration: `.desktop`, icon, README. — S8
T5. Vendor reference extraction. — S9 (done: Wine + app.asar, not innoextract)
T6. Curve editor with draggable points. — follow-up
T7. Lighting editor: mode, colour, effects, brightness, gear lights. — S7

## Log

L1 (user, verbatim): "quiero hacer ingenieria inversa con un repo llamado rea:
https://github.com/morluto/rea para hacer que el software de flydigi que esta en
un exe en mi descargar funcione para linux omarchy, vi un repo que hizo un
programa para la bspro3, el mio es el bs2pro pero el programa que hizo este dev
no me gusto la interface, quiero hacer que el exe lo transformes en tar o gz que
sea compatible con omarchy y hacerlo que funcione con mi ventilador"

L2 (user, decision): native GUI (option 1), using the exe as reverse-engineering
material through REA.

L3 (evidence): the cooler is already controlled natively. `flydigictl list` ->
`/dev/hidraw2 BS2 Pro`; `status` -> 1700 rpm, gear mode, supply low (2700 rpm
cap). `~/flydigictl` carries the user's own commit `d762c26 fix(protocol): parse
BS2 Pro status frames`, and `docs/FIRMWARE.md` already documents the complete
command surface read out of the firmware.

L4 (evidence): the installer is `PE32 executable for MS Windows 6.01 (GUI), Intel
i386`, built with Inno Setup, 240 MB. A `.exe` cannot be converted into a Linux
package: tar/gzip is a container, not a translation layer. The only two routes
are Wine or a native reimplementation; the protocol reimplementation exists.

L5 (evidence): daemon started in user space with `-c ~/.config/flydigictl/config.toml
-s $XDG_RUNTIME_DIR/flydigictl.sock`. `status` answers with a full JSON snapshot;
`sensors`, `gears` and `get_config` answer too. Gears read: quiet 1700, standard
2400, strong 3000 (not allowed), overclock 4000 (not allowed).

L6 (evidence): `innounp 0.50` under Wine cannot read this installer ("made by
incompatible version"). `innoextract 1.9` is in `extra` but installing needs the
user's sudo password, which this session cannot supply. Blocked on the user
running `sudo pacman -S --needed innoextract`.

L7 (user, verbatim): "ya lo instale usa rea para que funcione en mi linux, ya
que el que me hiciste tiene erroes como que no funciona el modo manual y no
puedo configurar los leds"

L8 (evidence): `innoextract` 1.9 still refuses — the installer is Inno Setup
6.7.0 and that release reads up to 6.0.5. The installer does run under Wine:
`wine setup.exe /VERYSILENT /SUPPRESSMSGBOXES /NORESTART /DIR=C:\space`
produced `resources/app.asar`, an Electron + Vite + React app extracted with
`@electron/asar`. `locales/en/translation.json` gives the vendor vocabulary:
`cooler_led_static/breath/gradient/rainbow`, `light_mode`, `brightness`,
`gear_light_display`. The HID protocol is not in the shell; the daemon already
documents it.

L9 (root cause, lighting): the app sent a flat `"mode":"effect"`. `Lighting` has
a `mode: LightMode` field and `LightMode` is an internally tagged enum, so the
wire shape nests one level: `"mode":{"mode":"effect","effect":N}`. Every
lighting request — including the old strip toggle — was rejected with
"invalid type: string \"effect\", expected internally tagged enum LightMode".
The UI showed the click and the strip never changed, which is exactly the
reported symptom.

L10 (evidence): clicking with a real pointer under XWayland (absolute X
coordinate = logical x 1.6) and reading the daemon journal confirms the paths:
`lighting off`, `lighting effect 5 at 80%`, and `manual=true` after clicking
MANUAL. Keyboard `M` and `C` confirm the same slots independently.

L11 (user, verbatim): "esto merece que este en un folder en un proyecto muevlo
porfa, ya que pienso subirlo a github" -> moved to `~/Projects/flydigi-space`,
git repository initialized on `main`.
