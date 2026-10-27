# Flydigi BS2 Pro Control for Linux

[![CI](https://github.com/ddoblejotadev/flydigi-bs2pro-linux/actions/workflows/ci.yml/badge.svg)](https://github.com/ddoblejotadev/flydigi-bs2pro-linux/actions/workflows/ci.yml)
[![GitHub stars](https://img.shields.io/github/stars/ddoblejotadev/flydigi-bs2pro-linux?style=social)](https://github.com/ddoblejotadev/flydigi-bs2pro-linux/stargazers)
[![License](https://img.shields.io/github/license/ddoblejotadev/flydigi-bs2pro-linux)](LICENSE)

A native Qt 6/QML control panel for the Flydigi BS2 Pro cooler on Linux. It provides a resident desktop dashboard, official cooling gears, Smart Speed/AUTO, a bounded custom curve editor, and the lighting controls supported by the Linux daemon.

![Flydigi BS2 Pro dashboard](docs/screenshot.png)

> **Important:** this GUI does not access HID devices directly. The separate `flydigictld` daemon owns hardware access and communicates with the GUI through a Unix socket.

## Quick install

### AppImage — any Linux distribution

1. Download the latest `flydigi-bs2pro-linux-*-x86_64.AppImage` from [Releases](https://github.com/ddoblejotadev/flydigi-bs2pro-linux/releases).
2. Mark it executable in your file manager, or run:

   ```sh
   chmod +x flydigi-bs2pro-linux-*.AppImage
   ```

3. Double-click the AppImage to launch it. The application does not require a terminal.
4. Install and start the separate `flydigictld` daemon using the instructions for your distribution below.

The AppImage contains the GUI and its Qt/QML runtime. It does **not** contain the daemon, HID permissions, or a system service.

### Debian, Ubuntu, Linux Mint, and derivatives

1. Download `flydigi-bs2pro-linux_*_amd64.deb` from [Releases](https://github.com/ddoblejotadev/flydigi-bs2pro-linux/releases).
2. Open it with the graphical package installer, or run:

   ```sh
   sudo apt install ./flydigi-bs2pro-linux_*_amd64.deb
   ```

3. Open **Flydigi BS2 Pro Control** from the application menu.
4. Install and start `flydigictld` separately.

The `.deb` installs the executable, desktop entry, icon, and QML resources. It deliberately does not install or configure the daemon.

### Arch Linux and Omarchy

The project does not publish an Arch package yet. Use the AppImage for the easiest local installation:

```sh
mkdir -p ~/.local/opt/flydigi-bs2pro-linux
install -m755 flydigi-bs2pro-linux-*.AppImage ~/.local/opt/flydigi-bs2pro-linux/flydigi-bs2pro-linux.AppImage
```

Then launch it from the file manager. To add it to the application menu, create `~/.local/share/applications/flydigi-bs2pro-linux.desktop`:

```ini
[Desktop Entry]
Type=Application
Name=Flydigi BS2 Pro Control
Comment=Control a Flydigi BS2 Pro cooler
Exec=/home/YOUR_USER/.local/opt/flydigi-bs2pro-linux/flydigi-bs2pro-linux.AppImage
Icon=flydigi-bs2pro-linux
Terminal=false
Categories=System;HardwareSettings;Settings;
```

Replace `YOUR_USER`, copy `packaging/flydigi-bs2pro-linux.svg` to `~/.local/share/icons/hicolor/scalable/apps/`, and refresh the desktop database if your environment requires it. Omarchy users can then launch it from the application launcher.

## Install the daemon separately

The GUI requires the external [`flydigictl`](https://github.com/ElXreno/flydigictl) daemon. Install the daemon using its documented package/build instructions, then start `flydigictld` as a user service or command appropriate for your distribution. Confirm that one of these sockets exists before launching the GUI:

```sh
ls -l /run/flydigictl/flydigictl.sock 2>/dev/null
ls -l "$XDG_RUNTIME_DIR/flydigictl.sock" 2>/dev/null
pgrep -a flydigictld
```

The GUI never starts, stops, or reconfigures the daemon. This separation keeps the AppImage and `.deb` portable and avoids silently changing HID or service permissions.

## What works

### Cooling

- Explicit AUTO/Smart Speed state from daemon readback.
- Linux manual override separate from official Smart Speed and fixed gears.
- Official fixed gears with three speed levels:

  | Gear | RPM levels |
  | --- | --- |
  | Quiet | 1300, 1700, 1900 |
  | Standard | 2100, 2400, 2700 |
  | Strong | 2800, 3000, 3300 |
  | Overclock | 3500, 3700, 4000 |

- Supply limits disable unavailable levels instead of sending invalid requests.
- Quiet, Performance, and Custom Smart Speed presets.
- Eight-point custom curve editor with 30–100 °C temperature bounds and supply-aware RPM validation.
- State changes are shown as active only after daemon confirmation.

### Lighting

- Off, static color, supported numeric effects, brightness 0–100, and gear indicators.
- Accessible Strip on/off switch with mouse and keyboard support.
- Invalid colors and unsupported multi-color requests are rejected before writing.

### Resident desktop behavior

- Closing the window or pressing Escape hides it when a tray host is available.
- The tray provides **Show Space Station** and **Quit** actions.
- Without a tray host, close falls back to minimizing for task-switcher recovery.
- The GUI remains independent from the daemon while hidden.

## Hardware verification

The following behavior was observed with a real BS2 Pro:

- Notebook USB supply reports a 2700 RPM limit.
- Fast-charger transition reconnects over Bluetooth with a 4000 RPM limit.
- Strong reaches 3300 RPM over Bluetooth.
- Overclock reaches 4000 RPM over Bluetooth.
- Full cooler power-cycle reconnects and restores AUTO, curve, and lighting state.

Exact vendor curve values from the Windows application are not claimed as verified. Tachometer readings can briefly overshoot the requested target while settling.

## Performance and security notes

A local offscreen Release measurement of the installed GUI kept the process alive for 5 seconds with approximately **123 MiB RSS** and **0.00% process CPU** during a 5-second idle sample. This is a Qt/QML desktop baseline, not a claim about every compositor or hardware setup. The GUI remains resident while hidden; its CPU/RAM usage is not artificially throttled.

The GUI uses `QLocalSocket` only, does not execute shell commands, spawn processes, write HID devices, or request elevated privileges. Socket ownership and access permissions remain the daemon's security boundary. Packages do not install services, modify udev rules, or broaden device permissions.

## Build from source

Dependencies: Linux, Qt 6.5+ (`Core`, `Gui`, `Qml`, `Quick`, `Network`, `Widgets`, and `Test` for tests), and CMake 3.21+.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build build -j"$(nproc)"
ctest --test-dir build --output-on-failure
cmake --install build --prefix "$HOME/.local"
```

The installed executable is `flydigi-bs2pro-linux`; the desktop entry appears as **Flydigi BS2 Pro Control**.

## Build release artifacts

The repository includes reproducible packaging entry points:

```sh
# Requires linuxdeploy and appimagetool
packaging/build-appimage.sh 0.1.0

# Requires dpkg-deb; normally run on Debian/Ubuntu or CI
packaging/build-deb.sh 0.1.0
```

Both scripts package the GUI only. The daemon remains a separately installed dependency.

## Troubleshooting

### The GUI shows “NO DAEMON”

Check the daemon process and socket:

```sh
pgrep -a flydigictld
ls -la /run/flydigictl/flydigictl.sock "$XDG_RUNTIME_DIR/flydigictl.sock" 2>/dev/null
```

Start `flydigictld` using its documented service/command and relaunch the GUI. For a custom socket:

```sh
flydigi-bs2pro-linux --socket /path/to/flydigictl.sock
```

### A control appears inactive

The UI waits for daemon readback. A request written to the socket is not treated as confirmation; wait for the next status response and inspect the daemon log.

### Report a problem

Include the GUI version, daemon version, distribution, desktop/compositor, exact steps, daemon logs, and whether the hardware was connected. Report issues at the [GitHub issue tracker](https://github.com/ddoblejotadev/flydigi-bs2pro-linux/issues).

## License

MIT. See [LICENSE](LICENSE).

## Credits

Reverse-engineering evidence and workflow were provided by the [Reverse Engineering Assistant (REA) repository](https://github.com/doblejota/rea).
