# Publication readiness for flydigi-bs2pro-linux

Source of truth for the authorized rename, packaging, local performance audit, security audit, and publication-facing README work.

## Specs

1. **S1** — "renombrar"
2. **S2** — "1. AppImage para distribución general.
2. .deb para Ubuntu/Debian y derivados.
3. Instalador del daemon separado, con instrucciones claras.
4. El usuario debería poder hacer:
   - descargar AppImage,
   - doble clic,
   - encontrar la app en el menú,
   - iniciar el daemon según su distribución."
3. **S3** — "yo ahora uso arch omarchy entonces quiero dejar claro como isntalar y tal."
4. **S4** — "Pero antes haz pruebas de rendimientos no quiero algo que consuma mucho tampoco, verfificar como funciona el arranque y tal primero en local y tal, auditar si existen brechas de seguridad sueltas y tal"
5. **S5** — "despues actualizar el readme con capturas toda de ejemplo repo con muchas estrellas"
6. **S6** — "en el readme el unico agradecimiento es el REPO REA que esta en github"
7. **S7** — "dale si agreguemos el workflow, quiero tambien que este proyecto tenga y se trabaje como correponde"

## Tasks

- T1 — S1 — Audit every public/internal name and startup reference before renaming — route: parent + verifier — commit: pending
- T2 — S2-S3 — Audit AppImage, Debian, daemon installation, Arch/Omarchy and desktop integration options — route: parent — commit: pending
- T3 — S4 — Measure startup, idle CPU/RAM, tray residency, and daemon/UI lifecycle locally; identify security findings — route: parent + verifier — commit: pending
- T4 — S1-S3 — Rename executable/target and implement install artifacts for AppImage/.deb release inputs and desktop launch — route: worker — commit: pending
- T5 — S2-S6 — Rewrite installation/troubleshooting/publication README and add repository-local screenshots only; acknowledge REA repository only — route: parent — commit: pending
- T6 — S1-S6 — Run build, packaging, performance, security, and release-readiness checks; record evidence — route: parent + verifier — commit: f4c624b
- T7 — S7 — Replace unsafe automatic-release CI with reproducible test, package, artifact, checksum, and tag-driven release workflows — route: parent — commit: 0d83b0f

## Log

- L2 — User request verbatim: "dale si agreguemos el workflow, quiero tambien que este proyecto tenga y se trabaje como correponde"

- L1 — User request verbatim: "renombrar, y sobre otro quiero dejar claro esto  1. AppImage para distribución general.
 2. .deb para Ubuntu/Debian y derivados.
 3. Instalador del daemon separado, con instrucciones claras.
 4. El usuario debería poder hacer:
     - descargar AppImage,
     - doble clic,
     - encontrar la app en el menú,
     - iniciar el daemon según su distribución. por que yo ahora uso arch omarchy entonces quiero dejar claro como isntalar y tal. Pero antes haz pruebas de rendimientos no quiero algo que consuma mucho tampoco, verfificar como funciona el arranque y tal primero en local y tal, auditar si existen brechas de seguridad sueltas y tal,y despues actualziar el readme con capturas toda de ejemplo repo con muchas estrellas, en el readme el unico agradecimiento es el REPO REA que esta en github"

## Evidence log

- E1 — Delegated read-only audit failed before returning output; parent audit found the executable, CMake target, application metadata, tray icon, QML install path, README quick path, and desktop entry still use `flydigi-space`, while CI already expects `flydigi-bs2pro-linux`. No AppImage or Debian packaging exists yet. The GUI uses only `QLocalSocket`, does not spawn processes or use shell commands, and relies on daemon socket filesystem permissions; packaging must not broaden socket or HID permissions. Existing README contains only a generic screenshot reference and must distinguish GUI installation from daemon installation.
- E2 — Clean Release rebuild after the rename passed; all 3 CTest targets passed and `git diff --check` passed. Staged install contains the renamed executable, QML module, desktop entry, and icon. Local offscreen startup remained alive for the 5-second bounded sample with maximum RSS 123092 KiB; the 0.000845 s value is process-observation latency, not UI-ready latency, so no false startup claim is made. `/usr/bin/time`, appimagetool, linuxdeploy, dpkg-deb, and flatpak-builder are unavailable in this Arch/Omarchy environment; packaging must be reproducible in CI or require the documented tool explicitly.
- E3 — A 5-second installed Release idle sample measured approximately 122980 KiB maximum RSS, 122947 KiB average RSS, and 0.000 s process CPU (0.00%) after startup with an unavailable daemon socket. This is an offscreen baseline and does not claim native compositor or hardware behavior.
- E4 — Replaced the previous push-to-main auto-tag/release workflow with a read-only CI workflow and a tag/manual release workflow. CI builds/tests/stages the install; release builds `.deb`, AppImage, and SHA256 checksums. AppImage bundling requires pinned linuxdeploy, Qt plugin, and appimagetool downloads in the Ubuntu runner; local Arch verification can validate scripts but cannot produce those artifacts without the tools.
