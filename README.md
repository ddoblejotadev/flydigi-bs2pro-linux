# Flydigi BS2 Pro Control for Linux

A native Qt 6/QML control panel for Flydigi BS-series laptop coolers on Linux.
The application talks to the `flydigictld` daemon over its newline-delimited JSON
Unix socket. The daemon owns the hardware; this GUI does not access HID devices
directly.

> **Current status:** cooling controls and the supported lighting protocol are
> implemented and covered by automated Qt tests. Hardware, Bluetooth power-mode
> transitions, and full Windows RGB parity still need real-device verification.

![Dashboard screenshot](docs/screenshot.png)

## Quick path

1. Install and run the `flydigictld` daemon from
   [`flydigictl`](https://github.com/ElXreno/flydigictl).
2. Build this project:

   ```sh
   cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
   cmake --build build -j"$(nproc)"
   ```

3. Run the checks:

   ```sh
   ctest --test-dir build --output-on-failure
   ```

4. Start `./build/flydigi-space` after the daemon socket is available.

## What is implemented

### Cooling

- Smart Speed (`AUTO`) is requested explicitly with `set_automatic`.
- Startup does not infer Smart Speed from a persisted curve preset.
- Manual Linux override is kept separate from official Smart Speed and fixed-gear
  state.
- Official fixed gears and their three documented speed levels are supported:

  | Gear | RPM levels |
  |---|---:|
  | Quiet | 1300, 1700, 1900 |
  | Standard | 2100, 2400, 2700 |
  | Strong | 2800, 3000, 3300 |
  | Overclock | 3500, 3700, 4000 |

- Supply limits disable unavailable gear levels instead of sending an invalid
  request.
- Smart Speed presets are named **Quiet**, **Performance**, and **Custom**.
- Custom curves use exactly eight points. Temperatures must be integral, strictly
  increasing, and within 30–100 °C. RPM is bounded by
  `500..min(supplyMaxRpm, 4000)`.
- The UI waits for daemon readback before presenting a mode as active. A submitted
  draft is not treated as saved until the daemon reports it.

### Lighting

The current daemon protocol supports:

- off;
- one static RGB color;
- five existing numeric effects;
- brightness from 0 to 100;
- gear indicators.

The backend sends the nested `set_lighting` payload required by the daemon and
reads the reported lighting state back. Invalid colors, empty color lists, and
multi-color lists are rejected without a write.

The **Strip on / Strip off** switch supports mouse and keyboard (Tab, then Space)
and is disabled while disconnected. It uses the existing lighting modes: off
selects `off`; on selects the retained static color and brightness, not the previous
effect. Brightness zero stays zero, and gear indicators are unchanged. The
OFF/COLOR/EFFECT selectors remain available.

The switch reflects the locally selected mode immediately and follows subsequent
daemon readback. It is not independent hardware-switch readback, proof that the
strip is physically lit, or confirmation of persistence.

## Known limitations

These items are **not claimed as implemented** because the available daemon
protocol has not been verified to support them:

- the official nine-effect RGB family;
- animation speed control;
- multi-color effect arrays and custom RGB sequences;
- independent hardware RGB switch opcodes;
- complete Windows RGB configuration persistence;
- real-device validation of every official Windows cooling curve;
- runtime observation of the official Windows app under Linux/Wine (its Windows
  `SpaceStationService`/`fcs.sock` dependency is unavailable).

GUI residency is implemented and tested offscreen; native Linux tray integration
and resource usage still require desktop verification (see Runtime behavior).

The application can be tested with a fake local socket, but socket tests do not
prove firmware behavior. Hardware verification is the next meaningful milestone.

## Work order

The project is being completed in this order:

1. **Cooling protocol and state model** — done. Named presets, Smart/fixed/manual
   separation, custom-curve validation, and daemon readback are implemented.
2. **Cooling UI parity** — done. The UI exposes the three curve presets and the
   bounded eight-point editor without optimistic state changes.
3. **Supported RGB behavior** — done. Existing daemon lighting behavior is
   validated and documented without inventing unsupported fields.
4. **Hardware and daemon verification** — done for the available BS2 Pro
   paths. Notebook USB limits the device to 2700 RPM; a fast charger switches
   control to Bluetooth and exposes the full 4000 RPM range. Strong, Overclock,
   AUTO settling, gear persistence, and daemon restart were verified. Exact
   Windows curve parity remains unverified.
5. **RGB parity expansion** — only after the daemon exposes and documents the
   full RGB configuration model.

## Architecture

```text
flydigi-bs2pro-linux/
├── src/                 C++ backend and daemon socket client
├── qml/                 Qt Quick user interface
├── tests/               QtTest backend and QML tests
├── packaging/           Desktop entry and application icon
├── odd/tasks/           ODD requirements, evidence, and verification log
├── CMakeLists.txt       Qt 6 build and test configuration
└── README.md            Project status and usage
```

The backend owns socket communication, JSON validation, state readback, and
request serialization. QML only renders state and invokes the public backend
API. This separation keeps the GUI lightweight and prevents it from competing
with the daemon for hardware access.

## Building and installing

### Dependencies

- Linux
- Qt 6.5 or newer (`Core`, `Gui`, `Qml`, `Quick`, `Network`, and `Widgets`;
  plus `Test` when building tests). The dashboard remains QML; `QApplication`
  and `Widgets` provide `QSystemTrayIcon` and its native `QMenu`.
- CMake 3.21 or newer
- A running `flydigictld` daemon for functional hardware use

### Build

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build build -j"$(nproc)"
cmake --install build --prefix "$HOME/.local"
```

### Test

```sh
ctest --test-dir build --output-on-failure
```

The tests use a controlled local socket and offscreen Qt Quick rendering. They
verify public requests, exact JSON payloads, validation boundaries, readback,
error handling, and UI state transitions. `resident_lifecycle_test` additionally
loads the full dashboard and checks Close/Escape, no-tray minimization, restoration,
continued event-loop operation while hidden, and explicit tray/window Quit.
The lifecycle fixture never starts the backend or contacts a real daemon. It
invokes tray actions directly, not through a desktop tray host. These tests do
not replace hardware or native desktop tests.

## Runtime behavior

### Window and tray

The GUI starts with its dashboard visible and stays resident until **Quit**:

| Action | Result |
|---|---|
| Close the main window or press Escape | Hide when a tray host is available; otherwise minimize so the task switcher can restore it |
| Tray **Show Space Station** | Show the hidden window or restore a minimized window; request focus |
| Tray primary activation/double-click (where supported) | Same as Show |
| Tray **Quit** or dashboard **Quit** | Exit only the GUI, even while the window is hidden |

No autostart entry or service is installed or enabled. Launching the application
again creates another instance; single-instance activation is not implemented.
The tray uses the installed application theme icon, with a standard computer icon
as fallback in an uninstalled build.

Native tray visibility, menus, activation/focus on Wayland/X11, and tray-host
loss/reappearance have **not** been verified. A host disappearing after the window
was hidden can leave it inaccessible until the host returns; there is no polling
watchdog or cross-process restore command. Without a tray, restore via the desktop
task switcher and use the dashboard Quit button. Desktop/compositor support for
minimization and focus requests varies.

This is a bounded resident-lifecycle implementation, not a minimum-resource claim:
the QML engine and existing backend subscription/reconnection behavior remain
alive while hidden. CPU/RAM use and further hidden-window throttling are unmeasured.

### Independent daemon

- The daemon owns the cooler and can remain active independently of the window.
  GUI Close, Escape, and Quit do not launch, stop, restart, or reconfigure its
  process/service; no daemon protocol or external daemon files changed.
- The GUI reconnects to the daemon socket when it becomes available.
- Startup requests status/configuration; it does not force a cooling mode.
- `AUTO` sends `{"request":"set_automatic"}` and waits for status confirmation.
- `C` releases the Linux manual override with `set_manual` and `rpm: null`; it
  is not an alias for `AUTO`.
- Live daemon survival after GUI Quit was not exercised in this slice; lifecycle
  tests are isolated from hardware and the running daemon.

## Troubleshooting

### The GUI cannot connect

```sh
pgrep -a flydigictld
ls -la /run/flydigictl/flydigictl.sock 2>/dev/null
ls -la "$XDG_RUNTIME_DIR/flydigictl.sock" 2>/dev/null
```

Start the daemon using its documented service or command, then restart the GUI.
If the daemon uses a custom socket path, configure the GUI accordingly.

### A control appears inactive

The GUI intentionally uses daemon-reported state. A request being written to the
socket is not treated as confirmation. Check the daemon logs and wait for the
next status response.

### Reporting an issue

Include:

- application and daemon versions;
- distribution, desktop, and Wayland/X11 session;
- exact reproduction steps;
- daemon logs and GUI output;
- whether the behavior was tested with hardware connected.

Report issues at:
https://github.com/ddoblejotadev/flydigi-bs2pro-linux/issues

## Credits

Thanks to the **Reverse Engineering Assistant (REA) repository** for the
reverse-engineering workflow and evidence used to map the Windows application
behavior. REA evidence is recorded in `odd/tasks/`. The daemon dependency is
linked in the Getting Started section.

## License

MIT. See [LICENSE](LICENSE).
