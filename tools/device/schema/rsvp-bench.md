# Device test results files (`/rsvp-bench/`), format version 2

The firmware's Device test screens (`src/activities/devicetest/`, tasks D3a and D3b) write these
files on the Picco's SD card. Task A2 parses them after the device session. The JSON Schema is
`rsvp-bench.schema.json` beside this file; check a file with

```
cd tools/device && python -m piccoflash.results validate <file> [<file> ...]
```

(needs `jsonschema`, as in `requirements-test.txt`). Exit 0: valid; 1: a file breaks the schema or a
rule below; 2: usage. Examples written by the simulator: `docs/evidence/d3a/` (version 1) and
`docs/evidence/d3b/` (version 2).

**Versions.** Version 1 (task D3a) had the benchmark and controls files. Version 2 (task D3b) adds the
guided acceptance and profile choice files; the benchmark and controls layouts did not change, so
firmware from D3b on writes `"version": 2` in all four kinds, and benchmark and controls files of
either version are valid. The acceptance and choice files exist only in version 2.

## Files and names

| Name | What |
| --- | --- |
| `NNNN-bench.json` | One display benchmark run |
| `NNNN-controls.json` | One Controls check |
| `NNNN-acceptance.json` | One guided acceptance walk-through (version 2) |
| `NNNN-choice.json` | One "Choose a profile" (version 2) |
| `NNNN-*.json.tmp` | A write that was cut off (the previous complete file is still beside it); ignore |

`NNNN` is a run number shared by every kind: the largest number already in the folder plus one. Names
therefore sort in the order the runs were made, whether or not the device clock is set, and a run
never overwrites another. The file is rewritten (temporary file, then rename) after every step of a
benchmark, when the owner pauses in the Controls check and after every answer in the guided
acceptance, so a file whose `status` is `running` was cut short by a crash or a power loss, or (for
the acceptance) is not finished yet: everything in it up to that moment is valid. The same figures
are in the serial log (`[DTS] ...` lines, README of `src/activities/devicetest/`).

The guided acceptance also keeps a progress file outside this folder,
`/.crosspoint/acceptance-progress.txt` (plain text, `src/activities/devicetest/AcceptanceProgress.h`),
so that it can resume after the owner leaves, a sleep or a restart. It holds nothing the acceptance
file does not; it need not be returned.

## Members common to every kind

| Member | Type | Meaning |
| --- | --- | --- |
| `format` | `"rsvp-bench"` | Always this |
| `version` | `1` or `2` | The version of this document the file follows (above) |
| `kind` | `"benchmark"`, `"controls"`, `"acceptance"` or `"choice"` | Which of the layouts below follows |
| `run` | integer 1..9999 | The run number, also the file name's first four digits |
| `file` | string | The file's own name |
| `status` | `running`, `complete`, `ended-by-owner`, `interrupted` | `running`: still going, or cut short (crash, power loss); an acceptance file stays `running` until every step is answered. `complete`: every profile that could run finished (benchmark), the owner left the check, every acceptance step is answered, or the choice was saved. `ended-by-owner`: "End the benchmark". `interrupted`: the device went to sleep, or the screen was left (Home), during the run (benchmark and controls only) |
| `firmware.version` | string | `CROSSPOINT_VERSION`, as Settings > About shows it |
| `firmware.elf_sha256` | 64 hex digits, `simulator` or `unknown` | The ELF SHA-256 ESP-IDF embeds in the app image (`esp_app_get_elf_sha256`): identifies the exact build. About shows no build id, and the git commit is not compiled in |
| `device.board` | string | `BoardConfig::ACTIVE.name` |
| `device.panel` | `unknown`, `YRD0426`, `DEPG0397`, `OPM040B3`, `SE0400NQW47`, `simulator` | The panel the display backend acts on (`PanelRecords.h`, `resolvePanel`) |
| `device.panel_variant` | integer | The SDK probe's raw value (`displayControllerVariant`, 0..4) |
| `device.controller` | `SSD1677`, `SSD2677`, `none` | The controller that panel runs on (the panel record) |
| `device.driver` | string | The driver name the SDK reports (`HalDisplay::controllerName()`), `none` in the simulator |
| `device.recognised` | boolean | One of the four Picco panels: trials can run |
| `device.simulated` | boolean | Written by the desktop simulator: every time is modelled |
| `device.fading_fix` | boolean | The "fading fix" setting in force (adds controller power-up time to each refresh) |
| `device.inverted` | boolean | The screen was inverted (night mode) when the run started: no trial runs then |
| `device.async_split` | boolean | The panel supports timing transfer and waveform apart (not used by this version) |
| `device.stock_update_waveform`, `device.clear_waveform` | string | What a stock word update and a clearing refresh run on this panel |
| `device.predicted_update_ms`, `device.predicted_clear_ms` | integer or null | Waveform-only predictions from the SDK tables (DEPG0397 only), without the frame transfer; null when not computable |
| `conditions.battery_percent` | 0..100 | Battery level at the start |
| `conditions.usb_connected` | boolean | USB power at the start |
| `conditions.clock` | `YYYY-MM-DDTHH:MM:SS` or null | Local time at the start, null when the device has no clock set |
| `conditions.uptime_ms` | integer | `millis()` at the start |
| `simulator` | null, or `{ "time_scale": n }` | Null on the device. In the simulator, the factor `CROSSPOINT_SIM_BENCH_TIME_SCALE` sped the benchmark's clock up by (1 when unset) |

## Benchmark (`kind: "benchmark"`)

| Member | Meaning |
| --- | --- |
| `conditions.room_temperature_c` | The temperature the owner entered, whole °C; **null means "unknown"** (the owner chose "Temperature unknown", or never set one) |
| `plan` | The counts this firmware runs: `word_band_updates` 100, `word_band_wpm` (the scheduler's pace for the word band, 1000: updates follow each other after the dwell), `full_screen_updates` 10, `clearing_refreshes` 5, `passage_words` 150, `endurance_updates` 300, `endurance_wpm` 240, `min_dwell_ms` 60 |
| `trial_setting.before`, `.after` | The stored display-profile byte (`TrialUnlock.h`) before the run and after it unlocked the trials it runs |
| `trial_setting.unlocked_before`, `.unlocked_after` | The same as lists of profile keys |
| `trial_setting.chosen_before`, `.chosen_after` | The profile RSVP plays with; the benchmark never changes it |
| `profiles` | Always five, in this order: `stock`, `trial-a`, `trial-b`, `trial-c`, `trial-d` |

Each profile:

| Member | Meaning |
| --- | --- |
| `key`, `name` | `stock` / `Stock`, `trial-a` / `Trial A`, ... |
| `status` | `done` (every step finished), `skipped-part-way` (the owner skipped the rest, or the run ended inside it), `not-run` (the run ended before it), `not-allowed` (the allow rule refuses it here), `running`, `pending` (only in a file of a run still going or cut short) |
| `reason` | Null unless `not-allowed` or `not-run`: `panel-unrecognised` (no trial on an unrecognised panel), `panel-no-tables` (Trials A to C on SE0400NQW47), `screen-inverted`, `refused` (the backend refused it at run time), `ended-by-owner`, `interrupted` |
| `skipped_from` | For `skipped-part-way`: the first step that did not finish (`stopped` or `skipped`); null otherwise |
| `clearing.soft`, `clearing.hard` | The profile's clearing limits: a clearing refresh at the first paragraph end after `soft` word updates, and always after `hard` |
| `waveform` | What a word update with this profile runs on this panel (`TrialWaveforms.h`, `wordWaveform`) |
| `predicted_waveform_ms` | Its waveform-only prediction; null when not computable |
| `steps` | Always ten, in this order: `word-band`, `full-screen`, `clearing`, `playback-120-1`, `playback-180-1`, `playback-240-1`, `playback-300-1`, `playback-240-2`, `playback-240-3`, `endurance` |

Each step:

| Member | Meaning |
| --- | --- |
| `step` | The id above; `playback-<wpm>-<words per update>` |
| `kind` | `word-band`, `full-screen`, `clearing`, `playback`, `endurance` |
| `status` | `done`; `unrated` (finished, rating not given: the run ended at the prompt); `stopped` (the owner stopped it part-way: the figures are partial); `skipped` (never ran); `pending` (not reached yet) |
| `attempts` | Series run for this step: more than 1 when the owner chose "Repeat step" (the figures are the last attempt's) |
| `requested` | The planned count: updates (word band, full screen), clearing refreshes, or passage words handed out (playback 150, endurance 300) |
| `target_wpm`, `words_per_update` | The scheduler's settings (word band 1000 and 1); null for full screen and clearing |
| `updates` | Word updates of the series (null if it never ran): `count`, `median_ms`, `p95_ms` (nearest rank), `max_ms`, `min_ms`, `mean_ms`, `timeouts` (updates of 5 s or more). The backend's statistics, reset at the start of the series; the window holds 128 calls, more than any series' updates except endurance, whose median and p95 cover its last 128 updates while count, min, max and mean cover all 300 |
| `clears` | Clearing refreshes during the series, the same members (timeouts: 10 s or more). For playback and endurance these are the clearing policy's refreshes; for the clearing step, the five measured ones. The refresh each step starts from is not counted |
| `promotions` | Requests the backend ran as a clearing refresh because of the trial exposure rules (`TrialExposure.h`); 0 in a normal run, so a non-zero value explains an odd `clears` count |
| `duration_ms` | From the first display call of the series to the end of the last |
| `playback` | Null for full screen and clearing. `delivered_wpm` (words shown / active playback time, one decimal), `words_shown`, `frames` (word updates), `clearing_refreshes` (by the clearing policy), `active_ms`, and `order`: `in_order` (every word drawn exactly once and in order), `words_drawn`, `segments`, `violations`, `gaps`, `repeats` (`WordOrderAudit`). In the simulator these use the sped-up clock (`simulator.time_scale`), so delivered WPM there is not a prediction |
| `rating` | Rated steps (playback, endurance): `clean`, `tolerable`, `unreadable`, or null (not rated yet) |
| `residue` | Endurance only: `yes`, `no`, or null |

Rules the validator checks beyond the schema: a `done` word band has `updates.count` 100; a `done`
full-screen step 10 updates and no clears; a `done` clearing step 5 clears and no updates; a
`done` or `unrated` playback or endurance step shows and draws exactly `requested` words and has as
many `updates` as `frames`; a `done` rated step has a rating, a `done` endurance step a residue
answer; a stopped series has no more than `requested`; a `skipped-part-way` profile is finished up to
`skipped_from`, `stopped` or `skipped` there, and `skipped` after it; `not-allowed` and `not-run`
profiles have a reason and only skipped steps; Stock is never `not-allowed`; a `complete` run has no
profile pending, running or not run.

## Controls check (`kind: "controls"`)

| Member | Meaning |
| --- | --- |
| `settings.side_button_layout`, `_value` | Settings > Controls > Side button layout at the start (`prev-next`, `next-prev`, `disabled`, `next-next`, `prev-prev`) and its stored number |
| `settings.short_power_button`, `_value` | The "Short power button" action (`ignore`, `sleep`, `page-turn`, `force-refresh`, `footnotes`, `confirm`, ...) |
| `settings.prev_key`, `settings.next_key` | The logical buttons the new screens read as previous and next (`ControlMap.h`, `sideKeys`) |
| `settings.confirm_selects` | Whether Confirm (GPIO5) selects on the new screens (`KEYS.confirmSelects`) |
| `settings.hold_ms`, `settings.exit_hold_ms` | 700 (a hold), 3000 (a hold that leaves the check) |
| `summary` | What the owner tried: `up_pressed`, `up_held`, `down_pressed`, `down_held`, `confirm_pressed`, `power_clicked`, `touched` |
| `events` | At most 64, in order; `events_dropped` counts any after that (all are in the serial log) |

Each event:

| Member | Meaning |
| --- | --- |
| `n` | 1, 2, 3 ... |
| `t_ms` | `millis()` when it ended |
| `source` | `key` or `touch` |
| `kind` | Keys: `short` (released before 700 ms; a power click is always short: a longer power hold sleeps the device), `hold`, `exit-hold` (3 s: the owner left). Touch: `tap`, `long-press`, `swipe-left`, `swipe-right`, `swipe-up`, `swipe-down` (start point), `back-gesture` (left-edge swipe), `home-gesture` (bottom-edge swipe: the device goes Home) |
| `raw`, `raw_name` | Keys: the `HalGPIO` button index and name: 0 `BTN_BACK`, 1 `BTN_CONFIRM` (GPIO5 on the Picco), 4 `BTN_UP` (GPIO21), 5 `BTN_DOWN` (GPIO9), 6 `BTN_POWER` (GPIO2). Null for touch |
| `logical` | The `MappedInputManager` buttons that reported the press or the release (for example `Up`, `PageBack`, `NavPrevious`); `Back` for the back gesture |
| `role` | What the new screens do with it: `prev`, `next`, `confirm`, `none` |
| `held_ms` | Keys: press to release (exit hold: until it fired). Null for touch |
| `x`, `y`, `x2`, `y2` | Touch position in portrait logical pixels (480x800); null where not given |

Rules beyond the schema: events are numbered 1, 2, 3 ...; key events have `raw` and `held_ms`, touch
events neither; a `hold` is at least `hold_ms`, a `short` key press other than power is shorter, an
`exit-hold` at least `exit_hold_ms`.

## Guided acceptance (`kind: "acceptance"`, version 2)

The device session's step 10 (docs/PLAN.md section 10). One file per walk-through, rewritten after
every answer. `conditions` are those of the walk-through's first answer (no room temperature).

| Member | Meaning |
| --- | --- |
| `profile.key`, `.name` | The display profile Part A played with: the chosen one, as RSVP resolves it on this panel when Part A started (`stock` unless a trial was chosen and is allowed) |
| `profile.waveform` | What a word update with it runs on this panel |
| `profile.trial_setting` | The stored display-profile byte then (`TrialUnlock.h`) |
| `part_a.status` | `pending` (not started), `running`, `done` (both runs and the three answers given), `skipped` (the owner skipped Part A, from its start screen or after stopping a run) |
| `part_a.passage_words`, `.min_dwell_ms` | 150, 60: the benchmark's passage and dwell |
| `part_a.runs` | Always two, in this order: `acceptance-240-1` (target 240 WPM), `acceptance-600-1` (target 600 WPM); one word per update |
| `part_a.slower` | The answer to "Was it slower than 600 WPM?" after the second run: `yes`, `no`, or null |

Each run:

| Member | Meaning |
| --- | --- |
| `run`, `target_wpm`, `words_per_update`, `requested` | The run's id, its target, 1, and 150 |
| `status` | `done` (ran to the end; its figures are the last attempt's), `pending` (not run to the end; no figures), `stopped` (stopped by the owner, then Part A skipped; partial figures) |
| `attempts` | Runs started, including stopped and interrupted ones and "Repeat the run" |
| `updates`, `clears`, `promotions`, `duration_ms`, `playback` | As in a benchmark playback step; null without figures |
| `measured_ok` | The automatic check of this run: every word index drawn exactly once and in order (`in_order`, `words_drawn` = `words_shown` = 150, no violations, gaps or repeats) and delivered WPM above 0 and at most the target. Null unless `done` |
| `readable` | "Was the last sentence readable?": `yes`, `no`, or null |

Part B: `part_b.book` (the book the steps are written for), `current` (the id of the step in progress,
null when every step is answered), `away` (the owner left the device test to do it), `answered`, and
`steps`, in order. Each step:

| Member | Meaning |
| --- | --- |
| `n`, `id` | 1, 2, 3 ...; a stable id (`c0001-rsvp-touch` ...; the step table is in `src/activities/devicetest/README.md`) |
| `criteria` | The PRD acceptance criteria (1 to 5) it gives evidence for; empty for a plan decision only |
| `trace` | What it proves, in a few words |
| `depends_on` | Tasks whose behaviour it describes and that were not merged when the text was written (`C2` ... `C5`) |
| `result` | `pass`, `fail`, `skipped` (the owner chose "Skip this step"), `not-done` |
| `reason` | For `fail` only: `did-not-appear`, `wrong-place`, `wrong-mode`, `grade-wrong` (a grade lost or wrong), `other` |
| `answers` | Answers given (1 once answered) |

Verdicts:

| Member | Meaning |
| --- | --- |
| `criteria` | Always five, criteria 1 to 5: `result` over its `evidence` (the steps that name it; for criterion 5 also `part-a`): any `fail` fails it, else any `not-done` leaves it `not-done`, else any `skipped` leaves it `skipped`, else `pass` |
| `criterion_5.measured`, `.measured_pass` | The automatic verdict: both runs `done` and `measured_ok`. `pass`/`true`, `fail`/`false`, or `not-done`/`skipped` with null |
| `criterion_5.owner`, `.owner_pass` | The owner's answers: both runs readable and slower `yes`. The same values |
| `criterion_5.pause_screen` | The result of the step on RSVP's real pause screen at 600 WPM (`pause-screen-600`) |

Rules beyond the schema: a `done` run has figures, as many updates as frames, and `measured_ok` equal to
the check above; other runs have no answer and no verdict, and a `pending` run no figures; a `done`
Part A has both runs done and every answer; steps are numbered in order with unique ids, a `fail` has a
reason and nothing else has one, an answered step has an answer; `current` is the first step not
answered and no later step is answered; `answered` counts the answered steps; `complete` means Part A
is done or skipped and every step is answered; every verdict in `criteria` and `criterion_5` agrees with
the runs and the steps.

## Profile choice (`kind: "choice"`, version 2)

The device session's step 11 (docs/PLAN.md section 6, "Outcome"). One file per saved choice;
`status` is always `complete`. `conditions` have no room temperature.

| Member | Meaning |
| --- | --- |
| `chosen` | The profile chosen: the one RSVP plays with from now on (on a panel that allows it) |
| `saved` | Whether CrossPoint's settings file was written |
| `trial_setting` | The stored byte before and after, as in a benchmark file (`before`, `after`, `unlocked_*`, `chosen_*`). After a choice only the chosen trial and the kept ones stay unlocked |
| `kept_before`, `kept_after` | The trials kept selectable for reading (`rsvpProfilesKept`), before and after; the chosen trial is among them |
| `locked` | Trials unlocked before and locked again now |
| `benchmark_file` | The newest benchmark file the ratings were read from, or null when none could be read |
| `candidates` | What the screen offered, Stock first: `key`, `name`, `chosen`, `kept` (Stock always), `ratings` (each rated benchmark step's rating, or null when the file has none for this profile), `residue` (the endurance answer) and `delivered_wpm_240` (the 240 WPM passage's delivered WPM), all null without a benchmark file |

Rules beyond the schema: `before` and `after` decode to their lists; `chosen_after` is `chosen`;
`unlocked_after` equals `kept_after`; the chosen trial is kept; `locked` is `unlocked_before` minus
`unlocked_after`; Stock with nothing kept stores 0; the candidates are Stock then trials in table order,
each unlocked before, exactly one chosen, `kept` as `kept_after` says; no ratings without a benchmark
file.
