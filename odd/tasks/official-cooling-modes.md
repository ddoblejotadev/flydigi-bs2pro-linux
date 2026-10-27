# Official cooling modes and transport

## Specs
1. **S1** — "modo auto como funciona? realmente funciona y sube dependiendo del calor?"
2. **S2** — "en preset no puedo selecionarlo, como hago para que un usuario cree su preset"
3. **S3** — "en la app original hay 3 formas de preset, te dije que miraras eso con REA"
4. **S4** — "el modo strong y overclock funcionan con un cargador carga rapida, por eso se desconecta del notebook y se conecya por bluethooh a la app"
5. **S5** — "el modo strong y overclock funcionan con un cargador carga rapida, por eso se desconecta del notebook y se conecya por bluethooh a la app"

## Tasks
- T1 (S1, S3): Reconstruct official Quiet/Performance/Custom curves from REA; commit: pending (exact vendor curves remain unverified)
- T2 (S1, S2): Add daemon curve preset selection and custom curve persistence; commit: bfe2c4d
- T3 (S2, S3): Add QML curve editor and explicit mode UX; commit: bfe2c4d
- T4 (S4, S5): Preserve mode across USB power transition and reconnect over Bluetooth; commit: 2bc2fe6
- T5 (S1-S5): Build, socket-test, and hardware-verify all official behavior; commit: 2bc2fe6

## Backend curve contract (bounded T1/T2 unit)

- `setCurvePreset(int)` retains UI indices 0=Quiet, 1=Performance, 2=Custom,
  index clamping, repeated-selection suppression, and the existing AUTO request
  when Smart is inactive. Wire values are the daemon strings `quiet`,
  `performance`, and `custom`, not numeric IDs.
- `bool setCustomCurve(QVariantList)` accepts exactly eight maps with numeric,
  finite, integral `temp_c` and `rpm` values. Temperatures must be in 30..100 C
  and strictly increasing; RPM must be in 500..min(supplyMaxRpm, 4000).
  Invalid input is rejected, never sorted, rounded, or clamped. Falling RPM is
  allowed: only temperatures must increase. Invalid or disconnected requests
  return false without writes or state changes. True means submitted, not saved.
- Custom sends one newline-delimited JSON request, for example:
  `{"request":"set_curve_preset","preset":"custom","points":[{"temp_c":30,"rpm":500},{"temp_c":40,"rpm":500},{"temp_c":50,"rpm":500},{"temp_c":60,"rpm":500},{"temp_c":70,"rpm":500},{"temp_c":80,"rpm":500},{"temp_c":90,"rpm":500},{"temp_c":100,"rpm":2700}]}`.
  No separate AUTO request or optimistic curve/mode update accompanies a custom
  submission. Daemon errors continue through `problem`; confirmation and
  persistence remain daemon responsibilities.
- Read-only `curves` exposes daemon `curves` arrays, including point objects,
  without assigning any index to Custom. Config readback replaces the array
  (missing/non-array means unavailable/empty); status replaces it only when the
  field is present. `curve_preset` is read from status or config. No synthetic
  `custom_curve` field, official default curves, or local persistence is added.
- Protocol basis: parent-reported strings inspection of the installed daemon
  identified `Request::SetCurvePreset`, `Curve`, `Point`, `preset`, `curves`,
  `temp_c`, `rpm`, and the error `preset must be quiet, performance, or custom with valid points`.
  The parent explicitly supplied the request shape above. This is not a live
  daemon round-trip; readback availability/curve identity and persistence still
  need integration verification. Selecting Custom without points retains the
  existing selection path; whether the daemon accepts that depends on its saved
  configuration and remains unverified.
- Coverage: S1/S2 gain validated custom-curve submission and readback; S3 gains
  correct named preset serialization. T1/T2 are not complete: exact official
  Quiet/Performance defaults and daemon persistence have not been verified.
  The QML editor (T3) and USB/Bluetooth/hardware work (S4/S5, T4/T5) are unchanged.

## Bounded T3 UI unit (S2, S3)

- Quiet/Performance/Custom buttons call `setCurvePreset(int)` and highlight only
  reported `curve_preset`. AUTO still reflects only reported `smart_mode`.
- Edit opens a compact eight-row draft dialog without requesting a mode change.
  Temperatures are integral 30..100 C and strictly increasing; RPM is integral
  500..min(supplyMaxRpm, 4000), with falling RPM allowed. Invalid/disconnected
  drafts cannot submit. Supply changes revalidate without modifying the draft.
- Submit calls `setCustomCurve(QVariantList)` once, with no separate AUTO request
  or optimistic state. Feedback says submitted, not saved; daemon errors are shown.
- No official default curves or Custom readback identity are invented. RPM starts
  empty; temperature rows are an editing template, not vendor defaults. Drafts
  remain local to this component and are not persisted.
- Existing backend index suppression is preserved: repeated preset selections,
  including initial Quiet at index 0, may send nothing. Custom without points
  still depends on daemon configuration. AUTO remains a separate explicit request.
- This verifies the bounded T3 UI, not live thermal control (S1), daemon
  persistence/default curves, or USB/Bluetooth/supply hardware behavior (S4/S5).

## Log

L6: Live hardware verification (2026-10-27): after USB/cable operation at `supply=low`, `supply_max_rpm=2700`, disconnecting the notebook USB and connecting the fast charger produced two transient `no device yet` replies, then the daemon reconnected with `transport=bluetooth`, `supply=full`, and `supply_max_rpm=4000`. Strong request `3300` reached `current_rpm=3300` with `fixed_gear=true`; Overclock request `4000` reached `current_rpm=4000` with `fixed_gear=true` and remained connected over Bluetooth. The tachometer briefly reported 4100–4300 while target remained 4000, then returned to 4000. `set_automatic` returned ok and readback changed to `smart_mode=true`, `fixed_gear=false` after the normal status interval. RGB state remained reported (`brightness=80`, `indicators=true`, `strip_on=true`). Full power-cycle persistence and exact vendor curve parity remain unverified.

L5: T3 UI RED: `cmake --build build -j2` exited 0, then
`ctest --test-dir build -R '^cooling_ui_test$' --output-on-failure` exited 8:
13 existing passes, 13 new failures for missing preset/editor controls. After
implementation the focused test passed. An added preservation test then hit a
test-only null dereference looking for a closed popup in the visual tree; fixed
by accepting its removal from that tree. Final build exited 0; focused CTest
passed (3.95 s); `ctest --test-dir build --output-on-failure` exited 0 (8.19 s):
58 backend + 27 UI QtTest passes. Tests use a temporary fake socket and offscreen
software rendering. Manual/gear/AUTO behavior is preserved; RGB untouched.
No live daemon/hardware verification or commit. The crashed test left
`/tmp/gpF3cEmY/gpK2Yipr/ga7d01Hh/cooling_ui_test-OrbJta/` for parent cleanup.

L3: Bounded T1/T2 RED: `cmake --build build -j2 && ctest --test-dir build --output-on-failure`
compiled successfully, then exited 8. Backend QtTest reported 18 passed and 38
failed: missing custom-curve slot/property and numeric instead of string preset
JSON. The existing QML suite passed. Tests use an isolated QTemporaryDir socket,
not the installed daemon. No RGB or packaging changes.

L4: GREEN: `cmake --build build -j2 && ctest --test-dir build --output-on-failure`
exited 0; application and both test executables built. CTest: backend_cooling_test
passed (4.15 s), cooling_ui_test passed (1.20 s), 100% tests passed, 0 failed,
5.35 s total. Coverage includes exact newline JSON, named presets, invalid-point
atomic rejection, supply limits, daemon error propagation, disconnected calls,
raw multi-curve readback, and preserved preset clamping/reselection/AUTO behavior.
No live daemon, hardware, or persistence check was performed; no commit created.
L1: "el modo auto como funciona? realmente funciona y sube dependiendo del calor?, lo otro en preset no puedo selecionarlo, como hago para que un usuario cree su preset, ademas en la app original hay 3 formas de preset, te dije que miraras eso con REA, ademas otro punto para el modo strong y overclock el ventilador se desconecta del notebook entonces ahi usa el bluetooh para poder seguir usando la app"
L2: "dale, y lo otro el modo strong y overclock funcionan con un cargador carga rapida, por eso se desconecta del notebook y se conecya por bluethooh a la app"
