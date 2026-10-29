# RGB lighting parity

## Specs

- S1 — Implement the RGB executor from evidence-backed frames only; every mutation requires ACK and readback confirmation.
- S2 — Preserve the existing newline-delimited JSON IPC contract for `set_lighting` and status/config readback.
- S3 — Implement the smallest safe slice first: off/on toggle, brightness, and one static color. Effects and multi-color sequences follow only after the base slice is verified.
- S4 — Never claim official effect parity when the exact frame or readback is not verified on the BS2 Pro.

## Tasks

- T1 (S1-S4): Inventory existing GUI/daemon lighting contracts and REA evidence — route: exploration — commit: working tree
- T2 (S1-S2): Implement pure RGB frame encoder and ACK/readback executor — route: daemon — commit: working tree
- T3 (S2-S3): Wire toggle, brightness, and static color through IPC — route: daemon/GUI — commit: working tree; toggle only verified; preset route blocked by realtime/cooling coupling
- T4 (S3-S4): Verify live on Bluetooth BS2 Pro and document unsupported effects — route: verification — commit: working tree

## Log

- L1 — User requested continuation after Auto was completed: RGB is the next feature, but implementation must not guess HID opcodes or bypass ACK/readback.
- L2 — Existing REA evidence: official RGB IPC uses `SetRgbConfig` and `SetRgbLight`; configuration includes `currentEffectType`, `currentSmartEffectType`, per-effect `effectConfigs`, `ledCount`, `colors`, `brightness`, `speed`, `ledIndices`, `effectName`, and `customParams`. Observed effect families include OFF, STATIC_SINGLE, BREATHING, WAVE, RAINBOW, FLASH, STREAMING, STATIC_MULTI, and ROTATION. Hardware RGB switch query/setter were observed as opcodes 69/70, but exact BS2 Pro frame layouts and readback confirmation remain to be independently verified.
- L3 — Firmware evidence identifies BS2 Pro side-strip power query `0x45` and setter `0x46`; `0x46` accepts payload `00`/`01` and ACKs with `01`. The daemon now sends `0x46`, requires its ACK, queries `0x45`, and requires the returned flag to match.
- L4 — Live Bluetooth verification on BS2 Pro succeeded: `set_lighting` mode `off` returned `readback_confirmed:true` with `strip_on:false`; mode `static` returned `readback_confirmed:true` with `strip_on:true`. Brightness and static-color frames remain unverified and unsupported; effects/multi-color sequences are explicitly unsupported until exact frames and readback are established.
- L5 — Firmware evidence documents the animation upload path (`0x41` begin, `0x42` blocks up to 15 bytes, `0x43` commit), the six-LED buffer layout, and `0x47` frame writes. It also explicitly states that animation patterns have no device readback; only strip power (`0x45`) and gear-indicator state are readable. Therefore brightness, static color, and effects cannot satisfy the project's ACK-plus-readback mutation rule and remain unsupported.
- L6 — Live preset 4 testing showed that firmware preset selection requires realtime mode: the daemon response had `preset_acknowledged:true` but status changed to `smart_mode:true`, `fixed_gear:false`, and `target_rpm:0`. This coupling is unsafe for the host-side cooling policy, so preset selection is blocked until it can be isolated from cooling control; the daemon restored Standard gear at 2400 RPM. The preset endpoint was removed rather than leaving an unsafe mutation path.
- L7 — Static analysis of the installed Flydigi Space Station 4.2.0.9 app found an Electron renderer path using `ChargerLedConfig` protobuf data and `UpdateConfig` IPC. The official LED model includes `mode`, `period`, `brightness`, `color[]`, `direction`, `useColorCount`, and `frames[]`; generated effects include default, breath, gradient, wave-gradient, diagonal-flow, rainbow, pulse, close, and custom. Custom frames contain per-LED RGB colors (162 LEDs in the renderer model), and the app reads defaults through `read_default_config` before previewing or applying. This proves the official app's data model and frame-generation strategy, but does not prove a BS2 Pro HID frame layout or device readback; advanced RGB remains blocked under S1/S4.
- L8 — Correlation against the Linux BS2 Pro implementation found only verified strip power operations (`0x45` query and `0x46` setter). The official app's `UpdateConfig`/protobuf path has no static evidence tying its rich LED frame model to BS2 Pro's `0x41`–`0x47` transport or providing pattern readback. No safe RGB extension is justified; keep brightness, static color, presets, and effects blocked.
