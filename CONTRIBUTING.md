# Contributing

Thanks for contributing to Flydigi BS2 Pro Linux.

## Branch policy

- `main` is the stable, releasable branch.
- Do not push directly to `main`.
- Create one short-lived branch per change from the latest `main`:
  - `feat/<name>` for new behavior
  - `fix/<name>` for bug fixes
  - `docs/<name>` for documentation-only changes
  - `hotfix/<name>` for urgent fixes to a released version
- Submit a Pull Request targeting `main`.
- Keep unrelated work out of the same branch and Pull Request.

```sh
git switch main
git pull --ff-only origin main
git switch -c feat/example-change
```

## Commits

Use small, reviewable Conventional Commits. Keep tests and documentation with the behavior they describe.

```text
feat: add reconnect status to the dashboard
fix: recover standard gear after daemon restart
docs: clarify Arch installation
```

Do not commit generated build output, `.deb` files, AppImages, `build/`, `daemon/target/`, or `dist/`. CI creates release artifacts.

## Pull Requests

Before opening a PR:

```sh
cargo test --offline --locked --manifest-path daemon/Cargo.toml
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure
```

The PR description should state:

- what changed and why;
- how it was tested;
- whether real BS2 Pro hardware was used;
- any limitations or unsupported operations;
- whether the change affects the IPC contract, HID behavior, packaging, or permissions.

A PR is merged only after CI passes and the change has been reviewed. Update the branch from `main` before merging if GitHub reports it as stale.

## Releases

Releases are created from `main` after the release PR is merged:

```sh
git switch main
git pull --ff-only origin main
git tag -a v0.1.17 -m "Release 0.1.17"
git push origin v0.1.17
```

Pushing a semantic-version tag runs the release workflow, which tests the project and publishes the `.deb`, AppImage, and SHA256 checksums to the GitHub Release. Never rewrite a published tag. Urgent post-release fixes use a `hotfix/*` branch and receive the next patch version.

## Reviewing outside contributions

Review the diff and CI results before reviewing the contributor's description. Check behavior through the public GUI, daemon IPC, packaging scripts, or tests rather than relying only on internal implementation details. Changes that send HID mutations must include ACK and readback evidence where the device supports it. Do not accept guessed opcodes, layouts, or RGB frames as verified behavior.
