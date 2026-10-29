# Interactive AppImage installer and updater

Feature document for the first installation UX slice. The installer is separate from the cooler GUI and must never mutate HID state.

## Specs

1. **S1** — "dale hagamos eso capo, pero hazlo bien, gentle-ai usa tui para que lo tomes como ejemplo"
2. **S2** — "es que justo busco que sea iteractivo que el usuario sepa algo asi, y que cuando haya actualizacion le sale en la tui para actualizar.. o no? es mucho? quiero dejar algo facil de instalar y todo claro"
3. **S3** — The first slice is an interactive AppImage installer/updater with release checking, changelog display, SHA256 verification, cancellation without partial replacement, and rollback preservation. Debian, AUR, and GUI update notifications are separate follow-up slices.

## Tasks

- I1 — S1-S3 — Inspect available TUI conventions and define the user-visible flow — route: parent — commit: pending
- I2 — S2-S3 — Implement AppImage install/update/verify/rollback TUI with injectable release endpoint — route: parent — commit: pending
- I3 — S2-S3 — Add packaging/release integration and installation documentation — route: parent — commit: pending
- I4 — S2-S3 — Test latest-release parsing, checksum rejection, cancellation, replacement, and rollback — route: parent — commit: pending

## Log

- L1 — User request verbatim: "dale hagamos eso capo, pero hazlo bien, gentle-ai usa tui para que lo tomes como ejemplo"
- L2 — User requirement verbatim: "es que justo busco que sea iteractivo que el usuario sepa algo asi, y que cuando haya actualizacion le sale en la tui para actualizar.. o no? es mucho? quiero dejar algo facil de instalar y todo claro"
- L3 — Scope decision: start with AppImage only. The TUI must show installed/latest versions, release notes, download progress, checksum verification, explicit confirmation, safe replacement, and a retained previous version for rollback. It must not use curl|bash, require root, or control the daemon.
