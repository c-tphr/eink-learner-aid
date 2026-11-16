# Continuous integration

One workflow, `.github/workflows/project-ci.yml` ("Project CI"), runs on every pull request and on every push to `main`. It repeats the checks the lead runs by hand when validating a pull request (`docs/PLAN.md` section 11), so a regression shows up before validation. Its token is read-only (`permissions: contents: read`); it uses no secrets, no `pull_request_target`, and never runs a PlatformIO upload target. A newer push to the same pull request cancels the older run; pushes to `main` are never cancelled.

**Branch protection needs one check: `ci-ok`.** It succeeds only when every other job succeeded.

## Jobs

Durations are each job's own time on GitHub-hosted runners (`ubuntu-24.04` with 4 vCPUs, `windows-latest`), measured on pull request #64: cold = empty caches, warm = caches from an earlier run with the same sources.

| Job | What it proves | Cold | Warm |
| --- | --- | --- | --- |
| `format` | The C/C++ files this project added or changed since the pinned upstream commit (`docs/BASE.md`) are formatted with clang-format 21 under upstream's `.clang-format`; upstream files we did not touch are not checked. Every `.py` under `tools/` and `scripts/` compiles. | 12 s | 12 s |
| `host-tests` | The CMake/GoogleTest host suite (upstream's tests plus `rsvp_core`, `checkpoint_core` and later ones) builds from a clean tree and passes, including the targets built with ASan/UBSan. | 3 min 47 s | 1 min 43 s |
| `prep-tests` | `tools/prep` tests pass with Java and the pinned EPUBCheck present. **A skipped test fails the job**, so a missing Java or a failed EPUBCheck download cannot pass silently. The committed fixture files are what `fixture/generate.py` produces. | 2 min 5 s | 2 min 27 s |
| `device-tool-tests` | `tools/device` tests pass in a virtualenv built from `tools/device/requirements-test.txt` (pinned esptool 5.6.0); a skipped test fails the job. | 36 s | 40 s |
| `device-tool-windows (powershell)`, `(pwsh)` | On Windows, under Windows PowerShell 5.1 and PowerShell 7: `session.ps1` parses; PSScriptAnalyzer 1.23.0 reports nothing, with its default rules and with the compatibility rules (`PSUseCompatibleSyntax` for 5.1 and 7.0; `PSUseCompatibleCommands` and `PSUseCompatibleTypes` against the Windows 10 1809 / Server 2019 profiles for 5.1 and 7.0, the set task D5 checked against), after a self-test that those rules are active (they must flag `Get-Error` in a scratch copy); and the offline commands run from an owner-style folder whose path contains a space: `setup` (twice: download, hash check and pip install of the private Python, then the "already set up" path), `plan`, `flash -DryRun`, `restore <backup> -DryRun`, `identify -DryRun`, `summary`, `ports`, each with its documented exit code. The kit and work folder are synthetic (see below). | 3 min 1 s / 1 min 18 s | 2 min 43 s / 1 min 15 s |
| `firmware-picco` | `scripts/cloud-setup.sh` works on a plain runner (it detects that there is no intercepting proxy and runs the same pre-fetch), and `pio run -e picco` builds. Prints the RAM/Flash lines, image sizes and the headroom against the two-slot budget (informational here). | 7 min 52 s | 2 min 15 s |
| `firmware-other-device (x4pro)` | One non-Picco device still compiles (`x4pro`: ESP32-S3, no `custom_sdkconfig`). Upstream's eight-device matrix is disabled in this fork (`docs/BASE.md`). | 7 min 45 s | 2 min 40 s |
| `simulator` | `pio run -e simulator_picco` builds with this project's simulator patches, and the binary starts headless under `xvfb-run`, reaches the Home screen (the firmware logs `Entering activity: Home`), saves a screenshot and quits with exit status 0. | 5 min 46 s | 1 min 47 s |
| `scenarios` | The scenario harness's self-tests pass and every scenario under `tools/simtest/scenarios/` passes against the `simulator_picco` binary built by the `simulator` job (screens, log lines, SD-card files, reference images). Self-activating (below): active since task E2 merged. | 1 min 8 s (first run with the harness) | 52 s |
| `firmware-2slot` | Self-activating (below). Today: prints `no picco_2slot environment yet` and succeeds. | 14 s | 12 s |
| `ci-ok` | Every job above finished with `success` (a failed, cancelled or skipped job fails it). | 3 s | 3 s |

The longest job sets the run's wall time: about 8 minutes cold, about 3 minutes warm. Cold: [run 43961711738](https://github.com/c-tphr/eink-learner-aid/actions/runs/43961711738) (empty caches; the harness was not on `main` yet, so the `scenarios` cold time is from [run 43966670239](https://github.com/c-tphr/eink-learner-aid/actions/runs/43966670239)). Warm: [run 43968230165](https://github.com/c-tphr/eink-learner-aid/actions/runs/43968230165). A change under `src/` or `lib/` falls back to an older build cache, so a firmware job then takes between the two (about 8 minutes after a large merge from `main`, run 43966670239).

## Runner images

Linux jobs run on `ubuntu-24.04`, the system of the Claude Code cloud sessions where work is done and validated (`docs/PLAN.md` section 11), so a CI result means the same as the lead's own run. They do not use `ubuntu-latest`: on 2026-11-16 that label was Ubuntu 26.04 with GCC 15.2, where two things fail that pass on Ubuntu 24.04 (GCC 13.3), both seen in [run 43960279326](https://github.com/c-tphr/eink-learner-aid/actions/runs/43960279326):

- `host-tests`: `lib/CheckpointCore/StateCodec.cpp:69:14: error: writing 4 bytes into a region of size 2 [-Werror=stringop-overflow=]` (in `Writer::putUInt`, inlined into `putPlace`, target `CheckpointCoreNoRttiCheck`, `-O3 -Werror`). The loop is correct (`n` is at most 10); GCC 15 reports a false positive, and `-Werror` makes it fatal. Upstream's own "CI (build)" unit-test job, which runs on `ubuntu-latest`, failed the same way on earlier pull requests.
- `simulator`: `.pio/libdeps/simulator_picco/QRCode/src/qrcode.h:37:23: error: 'bool' cannot be defined via 'typedef'`: GCC 15 compiles C as C23, where `bool` is a keyword, and the `ricmoo/QRCode` library typedefs it.

Moving CI to a newer image means fixing both first (in `lib/CheckpointCore`, and for QRCode with a C standard flag in the simulator environment or a patch). Windows jobs use `windows-latest` (Windows Server 2025, Windows PowerShell 5.1.26100, PowerShell 7.6 at the time of writing).

## Reproducing a job locally

Run from the repository root. In a Claude Code cloud session run `./scripts/cloud-setup.sh` once before any `pio` command (`docs/net-smoke-report.md` explains why).

| Job | Command |
| --- | --- |
| `format` | `pip install clang-format==21.1.8` (any clang-format 21 works; set `CLANG_FORMAT=<path>` if it is not on `PATH`), then `.github/scripts/check-format.sh`. It prints the diff clang-format would apply and changes nothing; fix a file with `clang-format -style=file -i <file>`. |
| `host-tests` | `cmake -S test -B build/test && cmake --build build/test -j$(nproc) && ctest --test-dir build/test --output-on-failure -j$(nproc)` (submodules checked out: `git submodule update --init --recursive`). CI only adds `ccache` through the `CMAKE_C_COMPILER_LAUNCHER`/`CMAKE_CXX_COMPILER_LAUNCHER` environment variables. |
| `prep-tests` | `python3 -m pip install -r tools/prep/requirements.txt`, then `python3 -m pytest tools/prep -q -rs --junitxml=build/prep-junit.xml && python3 .github/scripts/no_skips.py build/prep-junit.xml` and `python3 tools/prep/fixture/generate.py --check`. Needs Java 11 or newer. |
| `device-tool-tests` | `python3 -m venv build/venv-device && build/venv-device/bin/python -m pip install -r tools/device/requirements-test.txt && build/venv-device/bin/python -m pytest tools/device -q -rs` |
| `device-tool-windows` | On a Windows PC with Python 3.13: `python -m pip install -r tools/device/requirements-test.txt`, `python .github/scripts/device_session_fixture.py "C:\picco session"`, then in that folder `powershell -ExecutionPolicy Bypass -File .\session.ps1 setup`, `... plan`, `... flash -DryRun`, `... restore "work\picco-backup-<...>.bin" -DryRun`, `... identify -DryRun`, `... summary`, `... ports` (and the same with `pwsh`). The analyzer settings are in the workflow step. |
| `firmware-picco` | `./scripts/cloud-setup.sh && pio run -e picco && .github/scripts/firmware-size.sh picco` |
| `firmware-other-device` | `./scripts/cloud-setup.sh && pio run -e x4pro`. Fine in a cloud session: `x4pro` uses no `custom_sdkconfig`. |
| `simulator` | `./scripts/cloud-setup.sh && pio run -e simulator_picco && .github/scripts/sim-smoke.sh` (needs Pillow: `pip install pillow`). The screenshot is `build/sim-smoke/home.png`, the log `build/sim-smoke/simulator.log`. |
| `scenarios` | `pio run -e simulator_picco`, `python3 -m pip install -r tools/simtest/requirements.txt`, then `python3 -m pytest tools/simtest -q` and `xvfb-run -a python3 -m tools.simtest run --all --report-dir build/simtest-report` |
| `firmware-2slot` | `pio run -e picco_2slot && .github/scripts/firmware-size.sh picco_2slot "" --gate`, once the environment exists. |

## Artifacts

| Artifact | From | Contents |
| --- | --- | --- |
| `firmware-picco` | `firmware-picco` | `firmware.bin`, `bootloader.bin`, `partitions.bin`, `firmware.map`, `size-picco.txt` (RAM/Flash lines, image sizes, ELF sections, two-slot headroom). For inspection only: never flash these by hand; the device is flashed only through `tools/device/session.ps1`. |
| `simulator-picco` | `simulator` | The `simulator_picco` binary (`program`, Linux x86-64; needs SDL2 and OpenSSL; the execute bit is lost in the zip, `chmod +x` it). |
| `simulator-smoke` | `simulator` | `home.png` (480 x 800 Home screen from the headless run) and `simulator.log`. |
| `windows-session-powershell`, `windows-session-pwsh` | `device-tool-windows` | One log per `session.ps1` command, `results.txt` (command, exit code, expected), the tool's own logs from the work folder, the `summary` output and the runtime stamp. All from synthetic images; nothing from a device. |
| `host-tests-log` | `host-tests`, on failure only | CTest's `LastTest.log`. |
| `scenario-report` | `scenarios` | The harness's report folder `build/simtest-report/`: `index.html` (every step, screenshots, diffs, logs, the SD card after the run), `results.json`, `junit.xml` (tools/simtest/README.md). |
| `firmware-picco_2slot` | `firmware-2slot`, once active | As `firmware-picco`, for `picco_2slot`. |

Artifacts are kept for GitHub's default retention period (90 days for a public repository).

## The two self-activating jobs

Both jobs always run and always report; they decide from the checked-out tree whether there is work to do, so the tasks that add the harness and the environment need not touch the workflow.

- **`scenarios`** turns on when `tools/simtest/__main__.py` exists (task E2). It then downloads the `simulator-picco` artifact into `.pio/build/simulator_picco/`, installs the SDL2/OpenSSL runtime, Xvfb, `tools/simtest/requirements.txt` if present and pytest, runs `python -m pytest tools/simtest -q` and `python -m tools.simtest run --all` under `xvfb-run -a`, and uploads the report folder `build/simtest-report/`. Without the harness it prints `no scenario harness yet` and succeeds; that is how it ran until task E2 merged.
- **`firmware-2slot`** turns on when `[env:picco_2slot]` appears in `platformio.ini` or `platformio.project.ini` (task D4). It then runs `scripts/cloud-setup.sh`, `pio run -e picco_2slot` and the size gate: `.github/scripts/firmware-size.sh picco_2slot <log> --gate` fails when `firmware.bin` is larger than 7,431,782 bytes (the app slot of the two-slot table, `docs/PLAN.md` section 11 and spike S3; `APP_SLOT_BUDGET_BYTES` in the workflow). If `scripts/size_gate.py` exists it is run as well, as `python3 scripts/size_gate.py .pio/build/picco_2slot/firmware.bin`. Until then it prints `no picco_2slot environment yet` and succeeds. Note: `scripts/cloud-setup.sh` pre-fetches packages for `picco` and `simulator_picco` only; on a runner (no intercepting proxy) `pio run` fetches anything else itself.

## Caches

| Cache | Key | Used by |
| --- | --- | --- |
| `~/.platformio` (platform, toolchains, framework) | hash of `platformio.ini`, `platformio.project.ini`, `scripts/cloud-setup.sh`, `scripts/pio_prefetch.py`; falls back to the newest older key | all PlatformIO jobs |
| `.cache` (PlatformIO's `build_cache_dir`, object files) | environment, `freeink-sdk` submodule commit, hash of the `.ini` files, `src/` and `lib/`; falls back to the same environment and submodule commit, then the environment | each PlatformIO job, per environment |
| `.ccache` | hash of `test/`, `lib/`, `src/` and the submodule; falls back to the newest | `host-tests` |
| `~/.cache/eink-learner-aid` (EPUBCheck, unpacked after its SHA-256 check) | hash of `tools/prep/epubcheck.py` | `prep-tests` |

Every job also works from empty caches; a cold run only takes longer. Caches made on a pull request are visible to that pull request only; those made on `main` are visible to every pull request.

## Upstream workflows

The workflows imported from upstream (`ci.yml`, `pr-formatting-check.yml`, `pr-firmware-links.yml`, `release.yml`, `release-fonts.yml`) run their jobs only in `crosspoint-reader/crosspoint-reader`; here those jobs show as skipped. `release_candidate.yml` is unchanged (manual only, builds artifacts, no secrets, no writes). The reasons are in `docs/BASE.md`, "What differs from the pinned commit". `pr-formatting-check.yml` runs on `pull_request_target`, which always uses the workflow file of the base branch, so its title check keeps failing on pull requests until this change is on `main`.
