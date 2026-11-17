# Releasing the device kit

Task A0 (#37). The owner flashes the Picco from a GitHub release; nothing is built on their PC (`docs/PLAN.md` section 10, "The device kit"). Releases are made by one manually started workflow, `.github/workflows/release-kit.yml` ("Release kit"), because cloud sessions cannot push tags (`docs/PLAN.md` section 11). The workflow only ever creates a **draft**; publishing is a separate, deliberate step.

## What a run produces

One build of `picco_2slot`, and from that build only:

| Asset | What it is |
| --- | --- |
| `picco-2slot-<version>.zip` | The device kit. The owner extracts it and runs `session.ps1` in the extracted folder (layout below) |
| `picco-2slot-<version>.zip.sha256` | Its SHA-256, `sha256sum` format |
| `eink-learner-aid-v<version>-picco2.bin` | The Wi-Fi update: the same bytes as `kit/firmware.bin` and `sd-card/update.bin`. Its name is the one the firmware's updater derives from the tag `v<version>` (`lib/UpdatePolicy/ReleaseNaming.h`) |
| `eink-learner-aid-v<version>-picco2.bin.sha256` | Its SHA-256 |
| `kit-info.json` | Version, tag, commit, run, image offsets, sizes and hashes, the updater's verdicts, tool versions (also inside the zip) |

The kit, as it appears after "Extract All":

```
picco-2slot-<version>/
  KIT-README.txt            what the kit is, version, commit, date, what to open first, the backup warning (CRLF)
  MANIFEST.sha256           SHA-256 of every other file
  kit-info.json
  LICENSES.txt              licence notices for the firmware (scripts/build_release_license.py)
  session.ps1               \
  requirements.txt           | byte-identical to tools/device
  requirements-build.txt     |
  piccoflash/*.py           /
  kit/                      flash-plan.json, bootloader.bin (0x0), partitions.bin (0x8000),
                            boot_app0.bin (0x15000), firmware.bin (0x20000)
  sd-card/
    update.bin              the SD-card update (any *.bin works; docs/TWO_SLOT.md)
    books/                  fixture.epub, fixture.prepared.epub, paine-essays.epub, paine-essays.prepared.epub
  docs/                     DEVICE_EARLY_CHECK.md, DEVICE_SESSION.md (when it exists),
                            paine-essays.validation-report.md, paine-essays.LICENSE.md
```

`paine-essays.epub` (the unprepared original) is the kit's ordinary EPUB without checkpoint data: none of the upstream test books under `test/epubs/` is real prose (they are rendering test pages), and it is real public-domain prose already in the repository (`fixtures/books/REAL_BOOKS.md`).

The kit never contains `firmware.factory.bin` or any merged image, a `work\` folder, a backup, the private Python (`runtime\`), anything else from `.pio`, or a `.bin` other than the four images and `update.bin`. `scripts/make_release_kit.py` refuses to build such a kit, and `scripts/tests/test_make_release_kit.py` tests each rule.

## Starting a run

Inputs:

| Input | Values | Meaning |
| --- | --- | --- |
| `version` | `MAJOR.MINOR.PATCH`, digits only, no leading zeros | The release version. Tag `v<version>`; firmware string `<version>-picco2`. Must be above 2.0.0 and above every existing tag and release |
| `kind` | `preview` (default) or `final` | `preview`: a draft pre-release; `docs/DEVICE_SESSION.md` is included if it exists. `final`: a draft normal release; the run fails if `docs/DEVICE_SESSION.md` does not exist |
| `notes` | text, optional | Added near the top of the release notes |
| `publish` | `false` (default) or `true` | `false`: build and check only; the kit is a workflow artifact, no tag, no release. `true`: also create the draft release (on `main` only) |
| `allow_branch` | a branch name, optional | Testing only. The workflow refuses any ref but `main` unless this equals the branch it runs on; such a run never publishes |

From the Actions tab: "Release kit", "Run workflow". From a cloud session (REST; `gh workflow run` is not available there):

```sh
gh api -X POST repos/c-tphr/eink-learner-aid/actions/workflows/release-kit.yml/dispatches \
  -f ref=main -f 'inputs[version]=2.0.1' -f 'inputs[kind]=preview' -f 'inputs[publish]=false'
```

Always run with `publish=false` first. The workflow file must be on `main` before it can be started this way (GitHub's rule for manual workflows).

## What the jobs do

| Job | Permissions | What it checks |
| --- | --- | --- |
| `preflight` | read | The ref is `main` (or the `allow_branch` branch, without `publish`); the inputs are valid; no tag or published release already has this version or a higher one (`.github/scripts/release_preflight.py`) |
| `build and assemble` | read | Checks out the dispatched commit with submodules, sets `EINK_RELEASE_VERSION`, runs `pio run -e picco_2slot -t envdump` (the define must be `<version>-picco2`) and `pio run -e picco_2slot` once, the size gate (7,431,782 bytes), the licence notices, `pytest tools/device scripts/tests` (a skipped test fails), then `scripts/make_release_kit.py`. Uploads the kit as the artifact `release-kit-v<version>`, and the reports as `release-kit-v<version>-reports` |
| `windows (powershell)`, `windows (pwsh)` | read | On `windows-latest`: the zip matches its `.sha256`; it is marked as downloaded, then extracted with Explorer's zip folder (the "Extract All" engine) and with `tar.exe` into folders whose paths contain a space; `MANIFEST.sha256` verifies in both; with a work folder from the fake device, `session.ps1 setup`, `plan` and `flash -DryRun` succeed from the extracted kit under Windows PowerShell 5.1 and PowerShell 7 (`DRY RUN RESULT: READY`) |
| `draft release` | `contents: write`, `id-token: write`, `attestations: write` | Only with `publish=true`, and only after every job above passed. Re-checks the checksums and `kit-info.json`; re-runs the version check with a token that also sees drafts; creates a build provenance attestation for the zip, the Wi-Fi asset and `kit-info.json` (`actions/attest`; free for public repositories, no secret); creates the draft through the REST API with tag `v<version>` targeting the built commit (`preview`: marked pre-release); uploads the five assets; reads the draft back and compares tag, commit, flags, asset names, sizes and SHA-256 digests. If any of this fails, it deletes the draft it created. It never changes an existing release or tag |

`scripts/make_release_kit.py` does the assembling (its docstring lists every step): `scripts/make_device_kit.py` on the build (offsets from the build's envdump, every image through `piccoflash make-plan` and `plan`), the version checks below, the kit tree with its path rules, a deterministic zip (sorted entries, stored, every entry dated with the commit's time), then it unpacks the zip into a temporary folder and runs the UNPACKED `piccoflash` (`plan`, then `flash --dry-run` against a fake-device work folder). Anything but `READY` stops it. Run locally:

```sh
./scripts/cloud-setup.sh
export EINK_RELEASE_VERSION=2.0.1
mkdir -p build && pio run -e picco_2slot -t envdump > build/envdump.txt && pio run -e picco_2slot
python3 -m venv build/venv-device && build/venv-device/bin/pip install -r tools/device/requirements-test.txt
build/venv-device/bin/python scripts/make_release_kit.py --version 2.0.1 --kind preview \
    --envdump build/envdump.txt --out build/release-kit     # add --allow-dirty with uncommitted changes
```

## Version rules

One source: the workflow's `version` input. It becomes the environment variable `EINK_RELEASE_VERSION`, which `scripts/release_version.py` (a pre-script of `[env:picco_2slot]`) turns into `CROSSPOINT_VERSION = "<version>-picco2"`, shown on the About screen and the boot screen, in the serial log, and compared by the Wi-Fi updater. Without the variable, `picco_2slot` is a development build: `CROSSPOINT_VERSION = "2.0.0-dev-picco2"` (upstream's `[crosspoint] version` plus `-dev`). Nothing in `platformio.ini` changes for a release.

- **The tag's numbers equal the build's numbers.** The tag is `v<version>` and the build string `<version>-picco2`; the updater (`lib/UpdatePolicy/ReleaseNaming.cpp`, `parseVersion`) reads both as `<version>`. The kit builder asks the updater's own code (compiled from `scripts/release_naming_cli.cpp`) and fails on any difference, and also fails if the envdump, the build log, the plan or `firmware.bin` shows another string.
- **Above 2.0.0.** Development builds and the D4 builds carry 2.0.0. A release must compare higher, so that such a device is offered it; the kit builder checks this with the updater's code.
- **Above every earlier release, never reused.** The preflight refuses a version that any tag or release (including a draft, in the release job) already has, or a lower one.
- **The asset name.** The updater downloads `eink-learner-aid-<tag_name>-picco2.bin`, so `eink-learner-aid-v<version>-picco2.bin`. The tag must be at most 19 characters (the updater's 48-byte name buffer, `src/network/OtaUpdater.cpp`), which any version up to `999.999.999` meets.

Verdicts of the updater's code (`scripts/tests/test_make_release_kit.py`, `test_updater_verdicts`):

| Running | Release tag | Verdict |
| --- | --- | --- |
| `2.0.0-picco2` (D4 builds) | `v2.0.1` | install |
| `2.0.0-dev-picco2` (development build) | `v2.0.1` | install |
| `2.0.1-picco2` (the release itself) | `v2.0.1` | no update |
| `2.0.1-picco2` | `v2.0.0` (older) | no update |

### What devices see when a release is published

The updater reads `releases/latest` of this repository, which GitHub resolves to the newest published release that is neither a draft nor a pre-release. A draft is never seen; a `preview` (pre-release) is never seen, even when published. When a `final` release is published and marked latest, every Picco on the two-slot layout whose version is lower is offered it the next time the owner chooses "Check for updates", and installs it only after confirming. A Picco on the stock single-slot table is never offered anything (`docs/TWO_SLOT.md`). A device on a higher version is not offered an older release: the updater never downgrades.

## Reviewing a draft

1. Open the run: all of `preflight`, `build and assemble`, both `windows` jobs and `draft release` are green. Read the build job's summary (the release notes) and the `windows` jobs' result tables.
2. Open the draft (Releases, or `gh api repos/c-tphr/eink-learner-aid/releases --jq '.[] | select(.draft) | [.id, .tag_name, .name] | @tsv'`). Check the tag `v<version>`, the target commit, the pre-release flag (`preview` only) and the five assets.
3. Download the zip and check it against the notes and the `.sha256`: `sha256sum -c picco-2slot-<version>.zip.sha256`; unzip it and run `sha256sum -c MANIFEST.sha256`.
4. Check the attestation: `gh attestation verify picco-2slot-<version>.zip -R c-tphr/eink-learner-aid` (and the same for the Wi-Fi asset).
5. `kit-info.json`: commit is the intended `main` commit, `firmware_version` is `<version>-picco2`, `wifi_asset.name` is `eink-learner-aid-v<version>-picco2.bin`.

A draft's assets can be downloaded by anyone with write access to the repository (signed in), so the owner can take the kit for the device session from a draft; nothing needs to be published for the session itself.

## Publishing

Publishing creates the tag `v<version>` at the target commit and, for a `final` release marked latest, offers it to devices. Do it only on purpose:

```sh
gh api -X PATCH repos/c-tphr/eink-learner-aid/releases/<id> -F draft=false -F prerelease=false -f make_latest=true
```

A `preview` is published, if at all, with `-F draft=false` only (it stays a pre-release and is never offered to devices).

### Before publishing a `final` kit

- The run that made it is green end to end, including both Windows jobs, and Project CI is green on the same commit.
- `docs/DEVICE_SESSION.md` is in the zip, and `KIT-README.txt` says to open it first.
- The version is the one intended, above every earlier release; nothing newer is waiting as a draft.
- The zip and Wi-Fi asset hashes in the notes equal the downloaded files and their attestations verify.
- Hardware: until the device session has happened, nothing in the kit has run on a Picco. Publishing a `final` kit as latest only matters for Wi-Fi updates of devices already on the two-slot layout; before the session there are none. After the session, publish a fix only once it has been checked as far as the simulator and the host tests can.

## Withdrawing a release

- **A draft**: delete it (`gh api -X DELETE repos/c-tphr/eink-learner-aid/releases/<id>`). A draft has no tag, so nothing else remains. Its version may then be used again.
- **A published release**: turn it back into a draft (`gh api -X PATCH .../releases/<id> -F draft=true`), or mark it as a pre-release. `releases/latest` then points to the previous published release, so devices are no longer offered it. Devices that already installed it keep it (the updater does not downgrade); fix forward with a higher version. The tag `v<version>` stays; never reuse the version (the preflight refuses it). Deleting the tag needs the repository owner (cloud sessions cannot delete refs).

## Not yet exercised

The first real `workflow_dispatch` run, the `draft release` job and the attestation can only run after this workflow is on `main`. Before that, the same jobs ran on the task's pull request through a temporary `pull_request` copy of the workflow, with `publish` off (pull request #81).
