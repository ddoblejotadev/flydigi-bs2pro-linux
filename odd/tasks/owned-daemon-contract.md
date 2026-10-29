# Owned daemon observable contract

This document records the client-visible contract to implement without importing ElXreno runtime code. It is derived from the current Qt client, REA evidence, and observed original-app behavior.

## Transport

- Unix domain socket, newline-delimited UTF-8 JSON.
- Request connection is independent from subscription connection.
- `{"request":"subscribe"}` converts one connection to status updates; commands use a second connection.
- Default socket: `/run/flydigictl/flydigictl.sock`.
- A user-runtime socket may be supported for unprivileged development.
- Socket ownership/mode must be explicit; no world-writable socket.

## Requests used by the GUI

| Request | Required fields | Observable effect |
| --- | --- | --- |
| `status` | none | Current connection, transport, supply, RPM, mode, curve, lighting, and demands |
| `sensors` | none | Sensor rows with kind/hwmon/device/kernel/label/temp_c |
| `gears` | none | Four named gears with RPM and allowed flag |
| `get_config` | none | Persisted configuration and writable flag |
| `subscribe` | none | Subsequent status snapshots on that connection |
| `set_manual` | `rpm` integer or JSON null | Hold a manual RPM, or release manual mode with null |
| `set_automatic` | none | Enter Smart Speed; state changes only after readback |
| `set_gear` | `gear`, `rpm` | Enter fixed gear and apply one official speed |
| `set_curve_preset` | `preset`, optional `points` | Select quiet/performance/custom Smart Speed curve |
| `set_lighting` | nested lighting object | Replace complete lighting state atomically |
| `set_standby` | `standby`: off/instant/delayed | Change behavior when host disappears |

## Status minimum

```json
{
  "reply":"status",
  "model":"BS2 Pro",
  "connected":true,
  "current_rpm":2400,
  "target_rpm":2400,
  "smart_mode":false,
  "fixed_gear":true,
  "manual":false,
  "manual_rpm":null,
  "curve_preset":"quiet",
  "supply":"low|medium|full",
  "supply_max_rpm":2700,
  "transport":"cable|bluetooth",
  "lighting":{},
  "strip_on":true,
  "leading":"cpu",
  "revision":0,
  "demands":[]
}
```

Disconnected status must be explicit (`connected:false`) rather than silence. During physical reconnect, clients may receive a bounded error such as `{"reply":"error","message":"no device yet"}` but the daemon must continue retrying.

## Safety and state rules

- Validate every JSON field and range before touching HID or persistent configuration.
- Never expose firmware-destructive opcodes or arbitrary raw HID writes through IPC.
- Supply gating must reject or mark unavailable speeds above the current limit.
- Config writes must be atomic and report read-only/save-failed warnings.
- Reconnect must restore the last valid configuration without treating stale UI state as confirmation.
- The daemon must not require the GUI to run as root.

## Evidence boundary

This is an observable contract, not a code-copy instruction. HID opcodes, report layouts, Bluetooth transport details, and official curve values require independent implementation evidence and focused tests before D3-D6 can be marked complete.
