# Official UI parity and lightweight resident Linux app

Source of truth for the authorized redesign of `flydigi-bs2pro-linux`. Read through `## Log` before implementing any task.

## Specs

1. **S1** — "quiero que funcione bien como la app de windows que se queda abierta y tal, pero en linux tengo la ventaja de que puedo hacerlo que consuma el minimo"
2. **S2** — "soluciona los problema de los controles aun estan sin funcionar como deberian"
3. **S3** — "el diseño y todo eso lo estas haciendo basado en ti y no en la app original"

## Tasks

- T1 — S1 — Audit resident daemon/GUI lifecycle and define low-resource packaging/runtime behavior — commit: a8ceef3
- T2 — S2-S3 — Reverse-engineer official UI controls and command mappings with REA, then map each control to observable Linux behavior — commit: bfe2c4d, e593801
- T3 — S2-S3 — Rework Qt/QML UI to match evidence-backed official control structure and state rules — commit: a8ceef3
- T4 — S1-S2 — Add resident-app integration and functional verification for startup, tray/window behavior, USB/Bluetooth, supply limits, and control persistence — commit: a8ceef3
- T5 — S1-S3 — Run verification, record limitations, and prepare publication-facing documentation — commit: a8ceef3

- T6 — S2 — IMPLEMENTATION VERIFIED: Explicit Smart/fixed state readback and AUTO request implemented; independent build and tests pass (18 backend + 13 GUI QtTest cases). Native review: readability passed; reliability lens unavailable due to provider model catalog issue. Interactive QML behavior remains unverified; default-on Qt Test dependency needs delivery consideration. Route: worker + independent verifier (risk item 5); commit: bfe2c4d

## Bounded T3 cooling controls (S2, S3)

Implemented Quiet/Performance/Custom preset requests and a compact eight-point
custom draft dialog using the existing Backend API. Recorded REA evidence for
three curves, eight points, and supply limits informs the controls; no claim of
pixel-perfect official graph parity or verified vendor default curves is made.
Preset highlighting uses daemon readback, while AUTO/manual/fixed-gear reporting
is unchanged. Submission is not confirmation or persistence. Existing preset
index suppression, including initial Quiet, is preserved and documented in README.
See `official-cooling-modes.md` for bounds and verification evidence. This is a
bounded T3 cooling unit, not completion of the full UI parity task; RGB, resident
lifecycle (S1), live daemon integration, and hardware verification remain outside it.

## Bounded RGB model (S2, S3)

This unit uses the limited daemon schema recorded in L9 rather than sending the
Windows `SetRgbConfig` shape to `set_lighting`. Backend exposes `lightColors`
(the retained static color) and `lightColorLimit=1`; `setLightColors` validates
exactly one `#RRGGBB` entry and reuses the legacy setter and nested serialization.
Empty/malformed/multi-color arrays are rejected without state changes or writes.
`stripOn` now derives from the selected/reported mode, fixing stale enabled state
on readback. Existing effect 1..5, brightness 0..100, indicator, debounce, and
static-on-enable behavior remain unchanged.

S2/S3 coverage is bounded: there is no verified daemon representation for speed,
multi-color/custom sequences, the complete official effect enum, or a separate
hardware RGB switch. These are explicit protocol limitations, not implemented
parity. No new wire fields or QML controls are invented. Full T2/T3 completion,
S1 resident lifecycle, USB/Bluetooth, persistence, and hardware verification
remain outside this unit. Differential verification is parent-owned.

## Bounded strip switch (S2, S3)

Added a native Qt Quick Switch alongside the existing OFF/COLOR/EFFECT selectors.
It binds to `backend.stripOn`, calls the existing `backend.setStrip(checked)`,
and is disabled while disconnected. Mouse and Space-key input are verified;
the control has an accessible name and description. On selects the retained
static color and brightness, not the previous effect; off selects mode `off`.
Gear indicators and brightness are unchanged, including zero brightness.
Selection is optimistic until daemon readback, not hardware-switch confirmation.
No backend or wire-schema changes are part of this slice.

This addresses an explicit usable on/off control under S2 and bounded S3, not
full official RGB parity. S1, desktop screen-reader behavior, live hardware,
persistence, and independent hardware-switch support remain unverified here.
Current pre-existing effect serialization includes `colors`, `speed`, and
`ledCount`, unlike the limited schema recorded in L9/L15; preserving those
existing fields in a regression test does not verify daemon support for them.

## Bounded resident window lifecycle (S1)

Implemented a QApplication/Qt Widgets-backed system tray with **Show Space
Station** and **Quit** actions. Close and Escape keep the GUI resident: hide when
Qt reports a tray host, otherwise minimize for task-switcher recovery. A dashboard
Quit button provides an explicit exit without a tray. Last-window-close does not
terminate the event loop; restoration and explicit Quit are tested with the full
QML dashboard. The existing dashboard sizing/scroll changes are preserved, with
an explicit Qt Quick Controls import for those controls.

No autostart, single-instance routing, new daemon messages, external daemon edits,
or process/service management were added. The daemon remains independently
managed. Backend subscription/reconnection and the QML engine stay alive while
hidden; CPU/RAM savings are not measured. This covers a bounded T4/S1 slice, not
full S1 or S2/S3 parity. Native tray display/menu activation, Wayland/X11 focus,
host loss/reappearance, and live daemon survival still need verification. A tray
host disappearing while hidden can strand the window until it returns; no watchdog
or cross-process restoration is implemented.

## Log

- L18 — Final pre-commit verification: removed obsolete untracked `src/backend.cpp.bak2`; `cmake --build build -j2` passed; `ctest --test-dir build --output-on-failure` passed all 3 targets (backend_cooling_test, resident_lifecycle_test, cooling_ui_test); `git diff --check` passed. Independent verifier was unavailable due to a subagent error, so the parent ran the applicable checks directly. No live tray-host, hardware USB/Bluetooth transition, or full official RGB protocol verification is claimed.

- L17 — Bounded resident lifecycle (S1): RED `cmake --build build -j2` passed;
  `ctest --test-dir build -R '^resident_lifecycle_test$' --output-on-failure`
  exited 8 with two intended failures (Close accepted; Escape emitted QML quit).
  GREEN: the same build passed; the focused test passed (0.97 s).
  `ctest --test-dir build --output-on-failure` passed all three targets (15.77 s):
  77 backend, 8 resident-lifecycle, and 31 controls QtTest passes, including
  init/cleanup. Lifecycle tests exercise no-tray fallback, direct tray actions,
  hidden-window event-loop survival, window-button Quit, and visible startup;
  they do not emulate a native tray host or contact the daemon.
  `git diff --check` passed. Build reported missing optional Vulkan headers and
  existing Qt policy warnings. Full-dashboard loading also exposed the existing
  Shortcut-in-Repeater warning (`Delegate must be of Item type`); left unchanged
  as unrelated. Offscreen restoration warns that raise is unsupported, so native
  focus is not claimed. No base-copy CLI differential run was authorized;
  CLI parsing is unchanged and existing backend/control suites passed.
  No autostart, external daemon changes, hardware checks, or commit. Pre-existing
  tracked changes and `src/backend.cpp.bak2` preserved.

- L16 — Bounded strip switch (S2/S3): `qml/Controls.qml` now exposes an accessible
  on/off switch through the unchanged Backend API. RED: `cmake --build build -j2`
  passed; `ctest --test-dir build -R '^cooling_ui_test$' --output-on-failure`
  exited 8 with 28 passes and two intended missing-switch failures. GREEN: the
  same build command passed; the UI CTest command passed (5.24 s), including
  mouse/keyboard requests, exact nested on/off JSON, retained settings,
  readback after interaction, disabled input without writes/state changes, and
  preservation of all three existing lighting mode selectors.
  `ctest --test-dir build -R '^backend_cooling_test$' --output-on-failure`
  passed (8.89 s). No external daemon edits, hardware checks, or commit.
  Prior working-tree changes preserved. Base-copy differential execution was
  not among the authorized commands; existing selectors are regression-tested
  instead, and Backend code is unchanged.

- L15 — Bounded RGB model (S2/S3): added public one-color array validation and
  canonical readback; derived strip enabled state from mode. RED: build exit 0,
  focused backend CTest exit 8 (59 passed, 18 failed: missing new API and stale
  enabled default; legacy wire assertions passed). GREEN: `cmake --build build -j2`
  exit 0; `ctest --test-dir build -R '^backend_cooling_test$' --output-on-failure`
  exit 0 (8.97 s); `ctest --test-dir build --output-on-failure` exit 0 (12.50 s),
  77 backend + 27 UI QtTest passes. Fake sockets verify exact JSON, rejection
  without side effects, clamping, readback, coalescing, and legacy enable behavior.
  No invented daemon fields, QML changes, hardware/daemon integration, or commit.
  Unsupported RGB features remain as listed above; differential check is
  parent-owned. Pre-existing work preserved.

- L14 — Bounded T3 cooling UI verified: `cmake --build build -j2` exit 0;
  `ctest --test-dir build -R '^cooling_ui_test$' --output-on-failure` final exit 0
  (3.95 s); `ctest --test-dir build --output-on-failure` exit 0 (8.19 s), with
  58 backend and 27 UI QtTest passes. RED and the corrected test-only closed-popup
  lookup crash are recorded in official-cooling-modes L5. No RGB changes, live
  daemon/hardware checks, or commit. Prior unrelated working-tree changes preserved.

- L13 — T6 implementation completed and independently verified: build exit 0, CTest exit 0 (31 total QtTest passes), git diff --check exit 0. Native review readability captured (lens 0, reference rart1_247033a8717dd5bde6e15621c3ef8a00bca47eede7c179bd5bbd1c9274cc5605). Reliability lens (lens 1) blocked by provider model unavailability (minimax/minimax-m3.1-flash:free not in OpenRouter catalog). This is not a candidate defect. Independent verification already satisfies risk item 5 gate. No hardware or live daemon verification. No commit yet.

- L12 — User authorized isolated GUI verification: "dale capo sigue". Worker will exercise the actual QML AUTO control and reported-state highlighting with a fake daemon, offscreen software rendering, and bounded automatic exit. No physical hardware or live daemon access. This validates component interaction, not desktop appearance or hardware behavior.

- L11 — T6 independent verification: build exit 0, CTest exit 0 (18 QtTest cases), git diff --check exit 0. Verifier observed GREEN only; RED exit 8 is worker-reported evidence. QML compiled and was structurally inspected, not interaction-tested. No live hardware checks. Native assessment returned unassessable because untracked scope was undeclared; no native review approval exists. include(CTest) defaults BUILD_TESTING to ON, adding a Qt Test build requirement. Existing application Network transitive linkage predates this slice. No commit yet.

- L10 — Resume request: "bueno sigamos implementando esto de manera correcta". External daemon read-only inspection authorized by "dale capo con todo". Actual src/ipc.rs defines set_automatic and status smart_mode/fixed_gear/curve_preset. The handler accepts quiet/performance/custom presets, not gear names. Prior completion logs do not establish current implementation. T6 is a bounded prerequisite, not completion of T3 or the full parity feature. Existing daemon edits must remain untouched.
- L1 — User request: "dale pero quiero que funcione bien como la app de windows que se queda abierta y tal, pero en linux tengo la ventaja de que puedo hacerlo que consuma el minimo, ademas soluciona los problema de los controles aun estan sin funcionar como deberian, el diseño y todo eso lo estas haciendo basado en ti y no en la app original"
- L2 — REA evidence from `index-DbSTliNw.js`: official BS2 Pro gear definitions are Silent `[1300,1700,1900]`, Standard `[2100,2400,2700]`, Strong `[2800,3000,3300]`, and Overclock `[3500,3700,4000]`; the official UI renders Smart Speed versus fixed gear mode, uses `SetSpeedCurve` for all three curves, and sends `SetDeviceGear` with gear and speed. Smart mode uses command 35 to enter and 36 to exit. The official curve editor is an eight-point temperature/speed graph with a supply red-line clamp.
- L3 — The daemon's AUTO path was corrected to command 35 (`EnterSmartMode`); command 36 remains the fixed-gear transition before `SetDeviceGear`. Release build, 40 Rust tests, deployment, service restart, and a live performance-preset/AUTO socket test passed.
- L4 — User correction: "otra opcion por que el modo auto viene por defecto en activado??? eso no pasa en la app de flydigi original, por favot audita de nnuevo todo completamente y busca insconsistencias y reparalos, tienes REA para hacerlo de punta a punta, y no acepto errores"
- L5 — Full state audit: added explicit `smart_mode` and `fixed_gear` IPC fields. Startup now leaves Smart Speed inactive even when a curve preset is persisted; AUTO/curve selection activates Smart Speed; gears set `fixed_gear=true` and `manual=false`; manual RPM sets `manual=true`. Jobs 31-34 verified startup, AUTO, gear, manual, curve selection, final Quiet fixed state, deployment, and Bluetooth operation. Rust tests (40), GUI build/smoke, and diff checks passed.
- L6 — User correction: "todo mal me sigue marcando auto la gui no tiene los 3 submodos para los 4 modos, no hay modo manul funcional, el toogle del brillo sigue sin funcionar, ademas la app original tiene para elegir varios colores manuales y creear una secuencia custom algo asi, Por favor has el REA completo"
- L7 — Renderer/service audit evidence: the official fixed-speed panel is explicitly `固定模式下转速设置` and renders four gear cards, each with exactly three selectable levels. The renderer maps each selection to `setFanGear(uid, gear, speed)`; the device command is opcode 38 with `[gear, speed low byte, speed high byte]`. Official gear arrays are Silent `[1300,1700,1900]`, Standard `[2100,2400,2700]`, Strong `[2800,3000,3300]`, Overclock `[3500,3700,4000]`; unavailable levels are disabled from the supply-derived maximum. Smart mode is a separate button (`智能变频`) and uses enter/exit commands 35/36. The service log observed `SetSpeedCurve` with `currentMode=INTELLIGENT_COOLING_PRESET_CUSTOM`, eight points at 30..100°C, and `hysteresisDelta=2`, followed by repeated `SetFanSpeedCommand` telemetry/control activity. Renderer state treats fan mode values 1..4 as fixed gears; no separate arbitrary-RPM Manual panel was observed in the fixed-mode UI, so Linux `manual` must not be presented as an official fifth mode without further evidence.
- L8 — Renderer RGB evidence: official IPC uses `SetRgbConfig` with `rgbConfig` and `SetRgbLight` with `{uid, enable}`. The config contains `currentEffectType`, `currentSmartEffectType`, per-effect `effectConfigs`, `ledCount`, and each effect config's `colors`, `brightness`, `speed`, `ledIndices`, `effectName`, and `customParams`. Observed effect enum behavior includes OFF, STATIC_SINGLE, BREATHING, WAVE, RAINBOW, FLASH, STREAMING, STATIC_MULTI, and ROTATION; custom color sequences are represented by the `colors` array and effect-specific LED expansion. Brightness is clamped to 100; encoded speed is `floor(speed/10)` and clamped to 1..255 for animated effects. The hardware RGB switch query uses opcode 69 and setter opcode 70; the IPC adapter's `getRgbLightSwitch` currently returns false rather than querying state. Heartbeat only distinguishes RGB on/off and reports on as STATIC_SINGLE, so effect persistence requires the full RGB config, not heartbeat alone.
- L9 — Linux parity diff: `qml/Controls.qml` exposes only one static color, five locally named effects, no animation-speed control, no multi-color/custom sequence editor, and no visible indicator toggle in the audited lighting section. `src/backend.cpp` serializes only `{mode, color/effect, brightness, indicators}`; `protocol.rs` models only `Off`, one `Static` RGB, or numeric `Effect`, with five built-in buffers. This does not represent the official per-effect color arrays, `ledCount`, `speed`, custom parameters, or nine observed effect families. Linux cooling already exposes four gears with three official RPM choices and supply gating, but its arbitrary 500..4000 RPM slider is an additional Linux control, not an observed official fixed-mode subpanel. `getRgbLightSwitch`/full RGB readback remains impossible from heartbeat alone; the daemon must retain the last written full configuration and report switch state separately.
