# Workflows

Every branch carries the same set of files. A workflow does not belong to a branch; it declares
which events start it. The file name therefore says **when it runs**, and the run title says
**which branch or PR it ran on**.

## Naming

GitHub allows no subdirectories here, so the prefix is the grouping. The display `name:` repeats it,
because the Actions sidebar sorts by display name, not by file.

| Prefix | Runs when | Display name |
|---|---|---|
| `ci-` | PRs to `development` / `main`, and pushes to them (`ci-packaging.yml`: pushes to `development` only; `ci-codeql.yml` also weekly and skips Markdown/`docs/` changes) | `CI · …` |
| `build-` | on demand, and on push to `main` | `Build · …` |
| `release.yml` | a `v*` tag (dispatch is a rehearsal that publishes nothing) | `Release` |
| `scheduled-` | a cron schedule | `Scheduled · …` |
| `repo-` | issue and comment events; never builds code | `Repo · …` |
| `_build-` | only when another workflow calls it (`workflow_call`) | `Reusable · …` |

Workflows started by a PR or a push set `run-name` so the run list reads `PR #153 → development: …`
or `development: <commit subject>`.

## Inventory

| File | What it does |
|---|---|
| `ci-checks.yml` | clang-format gate, Qt version drift check (`scripts/check-qt-version.sh`), unit tests and ASAN + UBSAN on the pinned official Qt |
| `ci-codeql.yml` | CodeQL static analysis → Security tab; also weekly |
| `ci-packaging.yml` | Builds and packages all four platforms, unsigned, through the same `_build-*.yml` builders a release uses. Skips docs-only changes. Its `Packaging result` job is the one to require |
| `build-signed.yml` | Signed, notarized artifacts for every platform, one run page. Dispatch it on `development` for a tester build. No Release, no gh-pages |
| `release.yml` | The same builders, then the GitHub Release and gh-pages. Refuses a tag unless an on-demand `build-signed.yml` run succeeded on the same shipped code, so what testers approved is what ships |
| `scheduled-sanitizers.yml` | Weekly ThreadSanitizer; failures go to a sticky issue. Builds and caches its own TSan-instrumented qtbase (`scripts/build-qtbase-tsan.sh`) and configures the tests with `-DQK4_TESTS_ONLY=ON`: a distro Qt hides its locking from TSan (#172) |
| `scheduled-qt611-canary.yml` | Monthly: is there an aqtinstall release newer than 3.3.0, and does it install Qt 6.11 for Windows (red if not)? A control job installs 6.11 with aqtinstall from its fix commit, so a broken probe is red too. Delete once the 6.11 bump lands |
| `repo-issue-triage.yml` | First-pass triage of new issues |
| `repo-comment-guard.yml` | Hides disguised-download spam from non-maintainers |
| `_build-macos.yml` `_build-windows.yml` `_build-pi.yml` `_build-flatpak.yml` | The one definition of each platform's build, packaging and packaged-artifact verification |

## macOS packaging

`_build-macos.yml` builds against the Qt Company's binary Qt (`install-qt-action`, version from
`.github/qt-version`), not Homebrew's; Homebrew supplies only OpenSSL, Opus, hidapi and libusb. The
app is arm64 only, because those libraries are. Bundling and the release gate are scripts, so a
local run does exactly what CI does:

- `scripts/macos-bundle.sh <QK4.app> <qt-prefix> [openssl-prefix]` runs macdeployqt, thins Qt's
  universal binaries to arm64 and copies OpenSSL into `Contents/Frameworks`, which macdeployqt
  cannot see because Qt's TLS plugin loads it at run time. `cmake --build build --target deploy`
  calls the same script.
- `scripts/macos-verify-bundle.sh <QK4.app>` runs on the app inside the mounted DMG: framework
  symlinks intact, strict codesign, no library or run path outside the bundle except the OS's own,
  and `QK4 --tls-self-check` passing with libssl loaded from inside the bundle.

## Qt version

Every runner builds with the one exact Qt in `.github/qt-version`. A workflow reads it after checkout:

```yaml
- name: Read Qt version
  run: echo "QT_VERSION=$(tr -d '[:space:]' < .github/qt-version)" >> "$GITHUB_ENV"   # shell: bash on Windows
- uses: jurplel/install-qt-action@b344308e4483fc892e0d27b3cd1b173ebdd95f06 # v4.5.0
  with:
    version: ${{ env.QT_VERSION }}
```

| Runner | Qt |
|---|---|
| macOS, Windows, CodeQL, unit tests, ASAN/UBSAN | official Qt from install-qt-action |
| Raspberry Pi | official `linux_arm64` Qt, installed on `ubuntu-24.04-arm` and mounted into a `debian:trixie` build container; `check-pi-deps.sh` then runs `ldd` on the tarball in a clean Trixie container |
| TSan | qtbase built from source by `scripts/build-qtbase-tsan.sh`, checked against its per-version sha256 table |
| Flatpak | the KDE runtime branch with the same major.minor (`kde-X.Y` image, `runtime-version`); the runtime decides the patch |

Places that cannot read the file (the container image tag, the Flatpak manifest, the TSan checksum
table, the documented `~/Qt/<version>/macos` paths) are checked by `scripts/check-qt-version.sh` in
`ci-checks.yml`. To move Qt: change `.github/qt-version`, then fix whatever the script reports.
A step that deliberately installs another version (the 6.11 canary) marks its `version:` line
`# qt-version: probe`.

## Renaming a workflow

A rename starts a new entry in the Actions sidebar; the old file's runs stay under the old name.
Update the README badge, any `gh run list --workflow <file>` in docs, and any required status check
in branch protection. CodeQL's results survive a rename only because `ci-codeql.yml` pins its
`category`; keep that line.
