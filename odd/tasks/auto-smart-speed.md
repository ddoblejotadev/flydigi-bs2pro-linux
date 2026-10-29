# Auto / Smart Speed thermal control

Implement a safe host-side Auto mode for the owned daemon, with explicit readback and no zero-RPM transition without a valid policy.

## Specs

1. **S1** — "modo auto como funciona? realmente funciona y sube dependiendo del calor?"
2. **S2** — "en preset no puedo selecionarlo, como hago para que un usuario cree su preset"
3. **S3** — "en la app original hay 3 formas de preset, te dije que miraras eso con REA"

## Tasks

- A1 — S1 — Inventory sensor sources and existing GUI/daemon status contracts — route: exploration — commit: pending
- A2 — S1 — Implement bounded CPU Package sensor readback with safe fallback — route: daemon — commit: pending
- A3 — S1-S3 — Implement Quiet/Performance/Custom curve policy and persistence contract — route: daemon — commit: pending
- A4 — S1-S3 — Run the thermal loop with bounded updates, supply limits, and readback — route: daemon — commit: pending
- A5 — S1-S3 — Integrate GUI Auto/preset state and error handling — route: GUI — commit: pending
- A6 — S1-S3 — Build, socket-test, and hardware-verify temperature/RPM transitions — route: verification — commit: pending

## Log

- L1 — User requirement verbatim: "dale sigamos tu recomendacion, por que no abres odd?"
- L2 — Recommended implementation choice: CPU Package as primary temperature source with a safe fallback to the maximum available supported sensor. Exact vendor curve parity remains unverified and must not be invented.
- L3 — Current hardware evidence: BS2 Pro accepts set_automatic after returning to fixed gear, but reports target_rpm 0 and physically stops until a gear/manual command is sent. Host thermal policy is not implemented yet.
- L4 — Sensor implementation: daemon/src/sensors.rs reads Linux hwmon input files read-only, accepts bounded -100..200 C values, identifies CPU/NVMe/GPU/chassis kinds, prioritizes `Package id 0`, and falls back to the maximum reading. Live socket verification returned Package id 0 at 57 C plus NVMe/chassis/core readings. No sensor writes or external commands are used.
- L5 — User decision verbatim: "Bloquear Auto hasta evidencia". Auto/Smart Speed and named curves must not activate a host thermal policy until official curve evidence is verified; gears and manual remain the supported controls. A3 and A4 are blocked by this external evidence decision, not marked complete.
- L6 — Release verification: package 0.1.7 installed successfully; user daemon active after daemon-reload/restart. Real BS2 Pro status and hwmon sensors were returned. `set_automatic` returned an error before HID. Standard gear at 2400 RPM returned `readback_confirmed:true`; delayed status converged to 1300 RPM with `fixed_gear:true` and `smart_mode:false`.
- L7 — User authorized applying a recommended provisional curve: use the public THRM community default as a clearly labeled non-official baseline, while retaining CPU Package as the primary host sensor and explaining host temperature as the computer's Linux hwmon reading. A GUI bug was found: it filtered by chip names (`coretemp`, `nvme`) although the daemon contract returns `hwmon7`-style paths plus `kind`; backend filtering was updated to honor `kind`.
- L8 — Live 0.1.8 hardware verification: the active Bluetooth BS2 Pro returned 21 sensor rows including `Package id 0`. Explicit Auto returned `readback_confirmed:true` with `policy_source=community-thrm-provisional`; status showed `smart_mode:true`, `fixed_gear:false`, and target/current RPM converged through 2300, 2100, then 1900. Selecting Standard 2400 returned `readback_confirmed:true`, cancelled the worker, and status returned `smart_mode:false`, `fixed_gear:true`.
