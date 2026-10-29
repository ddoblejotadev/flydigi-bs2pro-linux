# Changelog

## [0.1.17] - 2026-10-29

### Added

- Built-in Linux daemon for HID/Bluetooth control without an external runtime dependency.
- Qt/QML dashboard with manual control, fixed gears, Smart Speed/AUTO, and persistent curves.
- Safe RGB strip ON/OFF control with ACK and device readback.
- Debian and AppImage release packaging through tag-driven GitHub Actions.

### Fixed

- Synchronized Auto worker shutdown before manual or fixed-gear mutations.
- Restored persisted Auto state after daemon restart.
- Recovered a residual firmware realtime state to Standard when no Auto intent was persisted.
- Kept GUI selections aligned with daemon readback instead of optimistic requests.

### Limitations

- RGB brightness, custom colors, presets, and animated effects remain unavailable until BS2 Pro-specific HID frames and pattern readback are verified.
- `community-thrm-provisional` is a provisional community curve, not an official Flydigi curve.
