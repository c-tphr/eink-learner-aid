# Technical implementation plan: RSVP and retrieval practice on BOOX Picco (v1)

Nov 16, 2026 · @Christopher

## Change log

**2026-11-17.** The findings of spikes S1, S2 and S3 were written back into this plan, with the lead's corrections. Section 1 is unchanged.

- Sections 4, 5, 7, 8 and 13: WordIndex is its own pass over the chapter XHTML and maps words to pages through CrossPoint's visible-text offsets, so no page table is written; anchors are empty `<span>` elements; footnote and endnote bodies are a note region. Source: `docs/spikes/S1-word-stream.md`.
- Sections 2, 6, 10, 11 and 13: the Picco has four known panels; trial tables load at run time through the SDK's `setCustomLUT()`; Trials A to C run only on the three SSD1677 panels and Trial D on any recognised panel; pace predictions include the SPI transfer. Source: `docs/spikes/S2-waveforms.md`.
- Sections 3, 10 and 13: the two-slot table with its offsets and size budget, `boot_app0.bin` at 0x15000, updates shown only on a two-slot table with the Wi-Fi updater pointed at this repo's releases, and new identify checks and an update-path step for the device session. Source: `docs/spikes/S3-two-slot.md`.
- Sections 2, 3 and 11: corrections learned during the project (upload hazard confirmed, the repo's starting state, where this plan lives, the base-import merge exception, and the live issue map in `docs/STATUS.md`).

**2026-11-19.** The finding of spike S4 (input roles and control map) was written back into this plan. Section 1 is unchanged. Source: `docs/spikes/S4-control-map.md`.

- Sections 2 and 4: only the role of GPIO5 (Confirm) is unconfirmed; the Up and Down side keys are hardware-confirmed in the SDK. Every action has a key path using only the two side keys (a short press moves the highlight, a 0.7 s hold selects; holding a side key in page view enters RSVP), and all bindings sit in one table, `src/controls/ControlMap.h`.
- Sections 6 and 7: entering RSVP lands paused; any input pauses playback; each checkpoint screen highlights the safe action by default, with Skip checkpoint last and a short input guard on new screens.
- Sections 9, 10, 11 and 13: the simulator's Picco key list, a new device-session step "Controls check" (later steps renumbered), and the Controls check screen and binding table added to tasks D3 and R4 and to the simulator risk.

**2026-11-19.** The real book was prepared in task P5 (issue #38). Section 1 is unchanged. Source: `docs/evidence/p5/README.md`.

- Sections 8 and 14: *Common Sense* is the first essay of the Standard Ebooks collection *Essays*, not a title of its own there; the collection is committed unmodified as `fixtures/books/paine-essays.epub` and prepared as a whole (READY after two rounds), because the instructions allow only front and back matter to be excluded.

**2026-11-18.** The checker's guard and where `/prep-book` runs (tasks P7 and P8, issues #75 and #84; the owner's decision recorded on #75). Section 1 is unchanged. This entry is the newest: the two entries above dated 2026-11-19 were written earlier, with dates that ran ahead of the calendar.

- Section 8, "Controls": the checker no longer has read-only tools. Since P7 it writes its own result through one guarded write, and a result without the guard's record is refused. The first real run showed that Claude Code does not run the guard's hook in a cloud session, so the plan now says that `/prep-book` needs a local Claude Code session in a trusted folder and starts with a probe that proves the guard is active. The owner chose this over a repository-wide hook.

**2026-11-18.** The state file's `book_sha256` (task R4b, pull request #90). Section 1 is unchanged.

- Section 7, "State file": the placeholder said "hash of the prepared file's source", which reads as if the device hashed a file. It is the checkpoint file's `book.source_sha256`, copied, and a book without checkpoint data has none. The wording now says so.

## 1. Summary and confirmed decisions

The reader is built as a public fork of mainline CrossPoint: RSVP works on any EPUB, and checkpoints come from prepared EPUBs that carry their own prompt data. This plan turns the v1 PRD into work that a Fable 5.2 orchestrator can split into GitHub issues for Opus 5.5 implementers. Everything is developed and verified in a simulator, then proven on the physical Picco in one final hands-on session.

The decisions below came from the interview on 16 Nov 2026 and are fixed inputs. The orchestrator must not reopen them without asking the project owner.

| Topic | Decision |
| --- | --- |
| Device | Owner has a Picco on stock Onyx firmware. Nothing is flashed until the final session. |
| Flashing | Full backup first, then a one-time move to a two-slot partition layout so later builds update from SD or Wi-Fi. Only the backup can return the device to stock. |
| Local machine | Windows PC with Chrome. Backup, first flash and serial capture run there. |
| Owner's emulator | Runs in GitHub Codespaces, started by one command from the repo root that installs and configures everything. |
| Display goal | Stock refresh is the baseline. A time-boxed set of faster waveforms is tried as an optional setting. |
| Waveform trial | Several pre-built candidates are swept in the single device session. The best clean one becomes a setting; if none is clean it stays off. No follow-up tuning round in v1. |
| Device testing | One final session. Evidence returned: benchmark results file, USB serial log, on-device readability ratings. No slow-motion video. |
| Preparation workflow | Claude Code commands in the repo: a drafter session, a fresh checker session, and a deterministic validator script. |
| RSVP scope | RSVP works on any EPUB. Checkpoints appear only when the file carries them. |
| Language | English only in v1. |
| Non-prose content | Images, tables and code blocks pause RSVP and show in page view. Footnote reference marks are passed over without pausing. |
| Prepared file | A normal EPUB with the checkpoint data embedded, so it still opens in other readers. |
| Controls | Every RSVP and checkpoint action works from both touch and the side buttons. |
| Saved state | Grades and reading state are small files on the SD card beside the book. No export screen in v1. |
| RSVP presentation | Each word is aligned on a fixed focus letter. Longer holds at punctuation count toward WPM. |
| Test books | A tiny synthetic fixture for automated tests, plus one short public-domain nonfiction title run through the full preparation workflow. |
| Repo | Public, personal project, no plan to merge upstream. The repo is c-tphr/eink-learner-aid. |
| Agents | Claude Code cloud sessions. The orchestrator turns this plan into GitHub issues, validates each pull request and merges it without waiting for the owner. |
| Network allowlist | Build, test and official documentation sites. Community sites stay blocked. |

## 2. What is known about the Picco, and what is not

The Picco is an ESP32-S3 reader with an 800x480 SSD1677-class panel, and mainline CrossPoint already builds for it. No one has published refresh timings for its panel, so every display number in this plan is a prediction until the device session.

| Area | Finding | Source |
| --- | --- | --- |
| Processor and memory | ESP32-S3, 16 MB flash, 8 MB octal PSRAM | [FreeInk SDK device table](https://github.com/Free-Ink/freeink-sdk) |
| Panel | 3.97" 800x480 black and white, portrait; one of four panels, identified at boot from the panel's user ID (CMD 0x2E) and, failing that, CMD 0x70: YRD0426, DEPG0397 or OPM040B3 on the SSD1677 driver, or SE0400NQW47 on the stock "SSD2677" driver. Each has its own stock waveforms (spike S2). | [FreeInk SDK](https://github.com/Free-Ink/freeink-sdk) |
| Touch, light, inputs | Parade TMA525C touch, two-channel warm/cool I2C frontlight, slide switch (GPIO6); power button (GPIO2: a hold sleeps); side keys Up (GPIO21) and Down (GPIO9), marked hardware-confirmed in the SDK; a third input, GPIO5, is carried as Confirm with its physical role unconfirmed | [FreeInk SDK](https://github.com/Free-Ink/freeink-sdk) |
| Stock partition layout | One app slot at 0xE0000 beside a vendor `safeboot` recovery app. On this layout, updates from the reader are impossible | [CrossPoint PR #3947](https://github.com/crosspoint-reader/crosspoint-reader/pull/3947), [crossplay PR #320](https://github.com/ma-r-s/crossplay/pull/320) |
| Upload hazard | Upstream `pio run -e picco -t upload` also writes a bootloader at 0x0, a partition table, and `boot_app0` into the stock NVS area. Confirmed by reading the build scripts at the pinned commit, although upstream's `platformio.ini` comment claims an app-only upload; see `docs/BASE.md`. | [CrossPoint issue #4248](https://github.com/crosspoint-reader/crosspoint-reader/issues/4248) |
| Two-slot move | A full flash writes the Arduino bootloader, a two-slot table, otadata and the app. NVS keeps its offset. Onyx's bootloader and safeboot come back only from a backup | [KatiePoint 2.0.0-rc3 release notes](https://github.com/InsiderPhD/crosspoint-reader/releases/tag/2.0.0-rc3) |
| Stock firmware | Not downloadable anywhere; Boox has reportedly denied warranty claims on flashed units | [KatiePoint release notes](https://github.com/InsiderPhD/crosspoint-reader/releases/tag/2.0.0-rc3) |
| Simulator | CrossPoint's SDL2 simulator has a Picco profile: 800x480, touch by mouse, slide switch, frontlight state, scripted input and screenshots | [crosspoint-simulator](https://github.com/crosspoint-reader/crosspoint-simulator) |
| Simulator limit | It does not model refresh timing, waveforms or ghosting | [crosspoint-simulator](https://github.com/crosspoint-reader/crosspoint-simulator) |
| Closest display data | On the Xteink X4 (same resolution, SSD1677), a partial refresh took about 542 ms regardless of area and a full refresh 1958 ms, giving about 111 WPM at one word per update | [inkflow measurements](https://github.com/wittyreference/dotfiles/pull/6) |

One early report said the Picco had 8 MB flash and no PSRAM ([readme.club](https://www.readme.club/news/readme-club-exclusive-boox-picco-is-running-crosspoint)). The stock partition table extends past 15 MB and the SDK and firmware both use PSRAM, so this plan treats 16 MB / 8 MB PSRAM as correct and confirms it at bring-up.

### Open questions the device session must answer

- [ ] Which panel this unit carries (YRD0426, DEPG0397, OPM040B3 or SE0400NQW47; logged at boot as `[EPD] Picco panel ...`). On the SE0400NQW47 the SDK uploads an undocumented 535-byte waveform on every refresh, so trial tables A to C cannot be designed for it.
- [ ] Actual partial and full refresh times for a word-sized region and for the full screen.
- [ ] Whether any shortened waveform leaves text readable after 30 to 60 consecutive updates.
- [ ] Flash size and PSRAM as reported by the chip.
- [ ] Which physical key is Up (GPIO21) and which is Down (GPIO9), whether GPIO5 (Confirm) is a key at all, and whether a 0.7 s hold is comfortable (Device test > Controls check).

## 3. Firmware base

Start from mainline CrossPoint ([crosspoint-reader/crosspoint-reader](https://github.com/crosspoint-reader/crosspoint-reader), `develop` branch), pinned to one commit, with the FreeInk SDK at the commit that mainline pins. It is MIT licensed, lists the Picco as a supported device, builds a `picco` target in CI, and is the firmware the official simulator's Picco profile was validated against.

| Candidate | Use | Reason |
| --- | --- | --- |
| [CrossPoint mainline](https://github.com/crosspoint-reader/crosspoint-reader) | Base | MIT; Picco in the supported list; `partitions_picco.csv`, agent guidance files (`AGENTS.md`, `CLAUDE.md`) and a `test/` directory already in the repo; simulator support |
| [KatiePoint (InsiderPhD)](https://github.com/InsiderPhD/crosspoint-reader/releases/tag/2.0.0-rc3) | Reference only | The one build reported as tested on a real Picco, and the documented precedent for the two-slot move. Experimental personal fork, so not a base |
| [crosspoint-reader-picco (mreza-yousefi)](https://github.com/mreza-yousefi/crosspoint-reader-picco) | Do not copy code | Its additions are under PolyForm Noncommercial, which would restrict a public MIT project. Its safe app-only upload behaviour is worth matching |
| [crossplay](https://github.com/ma-r-s/crossplay/pull/320) | Reference only | Targets the X4 Pro and adds games. Its upload guard (refuse any write outside the app offset) and its running-slot protection are good patterns; licence not stated, so re-implement rather than copy |
| [crosspoint-rsvp](https://github.com/NormousIndustries/crosspoint-rsvp) | Read for ideas | An RSVP mode for the Xteink X3 in landscape. No documentation, releases or Picco support; licence unverified |
| [inkflow](https://github.com/wittyreference/dotfiles/pull/6) | Method reference | Not CrossPoint, but the best public account of RSVP pacing against a fixed refresh cost: chunking, a per-update time floor, and clearing refreshes placed at paragraph breaks |

### Rules for working on the base

1. **Pin, don't track.** Record the upstream commit and the FreeInk SDK commit in `docs/BASE.md`. No upstream syncs during v1 unless the orchestrator opens a dedicated issue for one.
2. **Add, don't rewrite.** New code lives in new directories (section 4). Edits to upstream files are limited to registration points: activity routing, reader menu entries, settings, build environments.
3. **Follow upstream conventions.** Read `AGENTS.md`, `CLAUDE.md` and `docs/contributing/` first and keep `clang-format` and `pio check` clean, so the fork stays easy to rebase later.
4. **Check the existing repo first.** As of 16 Nov 2026, c-tphr/eink-learner-aid holds one initial commit with only a README on its main branch. Task F1 merges the pinned mainline commit, with its history, into main, keeps the project's own README and moves upstream's under docs/. If the repo holds anything else by then, the orchestrator stops and asks the owner before continuing. (On 16 Nov 2026 the repo also held the PRD and this plan; the owner confirmed that starting state.)

### Two-slot layout

Mainline targets the stock single-slot table and hides reader-side updates on the Picco. The owner chose the two-slot layout, so this project adds its own table and build target.

- Add `partitions_picco_2slot.csv` and a `picco_2slot` environment. NVS (0x9000, 48 KiB) and otadata (0x15000, 8 KiB) keep their stock offsets and sizes, so the move never writes the stock NVS area, as in the KatiePoint precedent. CrossPoint's own settings live on the SD card. `boot_app0.bin` is written at the otadata offset 0x15000, never at the framework default 0xE000, which lies inside the stock NVS. Table and reasoning: `docs/spikes/S3-two-slot.md`.
- Two app slots of 0x7E0000 bytes (8,257,536; 7.875 MiB) at 0x20000 and 0x800000; safeboot and spiffs are dropped, coredump stays (0xFF0000). A CI gate fails any build whose `firmware.bin` exceeds the slot minus 10%, 7,431,782 bytes. Upstream's 2.0.0 Picco image is 5,386,144 bytes.
- Re-enable SD updates only when the running table has exactly two app slots and the next slot is not the running one; keep them hidden otherwise. As built, the Wi-Fi updater fetches upstream's Picco image (`src/network/OtaUpdater.cpp:22`), which would replace this firmware. Task D4 points it at this repo's releases under the feature flag, the release workflow publishes the asset under the name the updater expects, and Wi-Fi update is shown under the same two-slot condition.
- Never use the upstream `upload` target on the Picco ([issue #4248](https://github.com/crosspoint-reader/crosspoint-reader/issues/4248)). The project ships its own flashing scripts, which print every offset they will write and refuse anything unexpected (section 10).

## 4. Firmware architecture

The logic lives in two portable C++ libraries with no hardware dependencies, so it can be unit-tested on a normal computer with a fake clock and a fake display. Thin activities connect those libraries to CrossPoint's reader, display and input layers.

| Component | Location | Responsibility | Tested by |
| --- | --- | --- | --- |
| RsvpCore | `lib/RsvpCore/` | Word splitting, focus-letter position, pacing weights, chunking, playback scheduler, delivered-WPM meter | Host unit tests |
| CheckpointCore | `lib/CheckpointCore/` | Checkpoint data parsing, trigger rules, checkpoint and reread state machine, grade rules, state file encoding | Host unit tests |
| WordIndex | `lib/WordIndex/` | Per-chapter stream of words with break anchors and non-prose markers; maps between word positions and page-view pages | Host unit tests, simulator |
| ReadingSession | `src/reading/` | Owns position, mode and navigation source; decides when a checkpoint fires; saves and restores state | Simulator scenarios |
| RsvpActivity | `src/activities/rsvp/` | Draws the word band, pause screen and settings; handles touch and keys | Simulator scenarios |
| CheckpointActivity | `src/activities/checkpoint/` | Prompt, reveal, grade, reread choice, skip actions | Simulator scenarios |
| DisplayProfile | `src/display/` | Named refresh strategies (stock and candidates), per-update timing, clearing policy | Simulator timing model, device |
| DeviceTestActivity | `src/activities/devicetest/` | Benchmark sweep, ratings, guided acceptance walk-through, results files | Simulator, device |
| ControlMap | `src/controls/` | The single binding table (key roles, timings, event-to-action bindings, touch zones) and the input adapter | Host unit tests, simulator scenarios |

### Position model

A reading position is a chapter index plus a word index within that chapter. Both reading modes, checkpoint triggers, rereads and saved state all use this one form.

- WordIndex is built once per chapter, on first use, by its own light pass over the chapter XHTML the page renderer parses. Each word records where it sits in CrossPoint's existing content coordinate: the count of visible codepoints in the chapter body. The page renderer records the same count for every page, and reading progress already uses it. WordIndex does not depend on font or layout settings, is stored in the book's existing cache folder and can always be rebuilt (spike S1, `docs/spikes/S1-word-stream.md`).
- Page view maps pages and words through that coordinate: the section cache already stores each page's start offset, so a page's first word is a binary search in WordIndex, and a word's page is CrossPoint's existing offset-to-page lookup. No page table is written and no layout code is changed. A font or margin change re-lays out pages without changing any word index.
- Elements whose id starts with `rsvp-` become anchors in the stream (section 5). Images, tables (`<table>`) and code blocks (`<pre>`) become non-prose markers that pause RSVP. Footnote reference marks are left out of the word text. The exact word rules are in `docs/spikes/S1-word-stream.md`, section 5, and the preparation tool follows them.
- Words inside footnote or endnote bodies are flagged as a note region. RSVP passes over them without pausing; page view shows them as usual.

### Constraints

- **Memory.** Words are streamed from the SD card in fixed-size blocks. No whole chapter is held in RAM, even though the Picco has 8 MB of PSRAM.
- **Scope of change.** The feature is compiled in for the Picco targets and the simulator behind one build flag, so other CrossPoint devices are unaffected.
- **Inputs.** The SDK confirms the Picco's Up and Down side keys but not the role of GPIO5 (Confirm). Every action has a key path that uses only the two side keys: a short press moves the highlight, a 0.7 s hold selects; Confirm and a power click set to Confirm are optional. In page view a 0.7 s side-key hold enters RSVP (new "Page turn long press: Speed read" option, default in feature builds). All bindings sit in `src/controls/ControlMap.h`; a swapped pair is fixed on the device by the Side button layout setting and durably by one line of that table (spike S4).
- **Every action two ways.** Each RSVP and checkpoint action has a touch target and a key path. The simulator scenarios drive each flow once by touch and once by keys. The key run uses only Up and Down; Confirm is never required.

### Where data is stored

| Data | Location | Survives cache deletion |
| --- | --- | --- |
| Global RSVP defaults (WPM, words per update, display profile) | CrossPoint's settings file | Yes |
| Per-book overrides, position, grades, in-progress checkpoint or reread | `<book file name>.rsvp-state.json` beside the book | Yes |
| WordIndex (words, anchors, non-prose markers per chapter) | `rsvp/` in the book's folder under `/.crosspoint/` | No, rebuilt on demand. Pages map to words through the section cache CrossPoint already keeps |
| Benchmark and acceptance results | `/rsvp-bench/` on the SD card | Yes |

## 5. Prepared book format

A prepared book is a copy of the original EPUB with two additions: invisible anchor elements marking each section's start and end, and one JSON file holding the sections, checkpoints, prompts and reference answers. It stays a valid EPUB, so page view uses CrossPoint's existing renderer and other reading apps can still open it.

### Layout

- File name: `<title>.prepared.epub`. The original EPUB is never modified; its SHA-256 is recorded in the prepared file and checked by the validator.
- Checkpoint data: `rsvp/checkpoints.json` next to the package (OPF) file, declared in the package manifest with id `rsvp-checkpoints` and media type `application/json`. The firmware treats a book as prepared when that manifest item exists.
- Anchors: empty `<span>` elements, for example `<span id="rsvp-s0007-end"></span>`, inserted between words (never inside a word), with ids `rsvp-<section id>-start` and `rsvp-<section id>-end`. They render as nothing, and CrossPoint's own anchor map ignores ids on `<span>`.
- No other change to the book's text or markup. The validator proves this by comparing the word sequence of the original and the prepared copy.

Anchors in the markup, not word counts, define the boundaries. The Python preparation tool and the C++ firmware split words independently, and an anchor sits at the same place in the text for both.

### Rules

1. A section lies inside one chapter file. Chapter boundaries are always section boundaries.
2. Each checkpoint names exactly one section and fires at that section's end anchor. All prompts in the checkpoint share that link.
3. A section may have no checkpoint (front matter, for example). A checkpoint has one or more prompts, kept in file order.
4. Prompt ids are stable across re-preparation, because saved grades are keyed by prompt id.
5. Prompts and answers are plain UTF-8 text. Limits: prompt 300 characters, answer 600 characters; longer answers page on the device.
6. The firmware accepts `version` 1. A higher version opens as an ordinary EPUB with a one-line notice.

### Checkpoint file, version 1

```json
{
  "format": "rsvp-checkpoints",
  "version": 1,
  "book": {
    "title": "Example Title",
    "language": "en",
    "source_sha256": "<hash of the original EPUB>",
    "instructions_version": "1.0"
  },
  "sections": [
    {
      "id": "s0007",
      "chapter_href": "text/chapter-3.xhtml",
      "start_anchor": "rsvp-s0007-start",
      "end_anchor": "rsvp-s0007-end",
      "label": "Why spacing works",
      "word_count": 1180
    }
  ],
  "checkpoints": [
    {
      "id": "c0007",
      "section": "s0007",
      "prompts": [
        {
          "id": "c0007-p1",
          "prompt": "What two effects does the author say spacing has?",
          "answer": "It forces effortful recall, and it exposes what was forgotten."
        }
      ]
    }
  ]
}
```

The JSON Schema for this file lives at `tools/prep/schema/checkpoints.schema.json`. One shared set of valid and invalid sample files is run through both the Python validator and the C++ parser tests, so the two cannot drift apart.

The validation report is delivered beside the prepared file as `<title>.validation-report.md` and `.json`, not inside the EPUB.

## 6. RSVP engine and display strategy

Playback never drops or reorders a word: when the panel cannot keep up, each update simply takes as long as it takes, and the pause screen reports the pace actually delivered. From the SDK's waveform tables (spike S2), the 240 WPM single-word default is predicted to deliver roughly 125 WPM on the stock refresh of the DEPG0397 panel, and roughly 100 to 111 WPM if the panel behaves like the Xteink X4's. Reaching 240 with one word per update needs a complete update, transfer included, of about 190 ms; Trial C is predicted to fall just short (about 232 WPM), and only Trial D on some panels might get there.

### Pacing

- Base interval is 60,000 / target WPM milliseconds per word (250 ms at 240 WPM).
- Each word has a weight: 1.0 normally, more after a comma or semicolon, more again at a sentence end and at a paragraph end, slightly more for long words. The weights sit in one table.
- Weights are normalised by a fixed constant measured from an English prose sample, so that typical text averages the target WPM with the punctuation holds included.
- With N words per update, consecutive words are grouped up to N. A group never crosses a sentence end, a paragraph, a non-prose marker or a section end, and must fit the screen width. Its interval is the sum of its words' intervals.

### Scheduler

1. Draw the next word or group and start the refresh. Record when the panel reports it has finished.
2. Hold for at least a minimum dwell after the refresh finishes (default 60 ms, one constant, tuned from device ratings).
3. Start the next update at the later of the paced deadline and the end of the dwell. Late updates do not cause catch-up bursts.
4. Stop before drawing anything past a section end that carries a checkpoint, and at any non-prose marker.

Delivered WPM is words shown divided by active playback time. Time spent paused, in checkpoints or viewing non-prose items is excluded. The pause screen shows target and delivered WPM for the current session.

### Presentation

- Each word is placed so its focus letter sits on a fixed column, marked by small ticks above and below the word band. No grey levels are used, because the fast refresh modes are black and white only.
- Only the word band is redrawn during playback. Guides and progress marks are drawn once per play.
- While paused: adjust WPM and words per update, save as global default or as this book's override, step back or forward by sentence, switch to page view at the same position.
- Entering RSVP shows the pause screen at the current word with Play highlighted; RSVP always waits for Play.
- While playing, any key press or touch pauses and nothing else.

### Display profiles

A display profile is a named refresh strategy: which waveform is used for word updates, and when a clearing refresh runs. The stock profile ships as the default. Up to four trial profiles are built once, swept in the device session, and never iterated in v1.

| Profile | Word update | Predicted delivered pace, one word per update | Status |
| --- | --- | --- | --- |
| Stock | CrossPoint's fast refresh of the full frame (only the word band changes), using the panel's reading waveform | DEPG0397: about 380 ms waveform plus at least 38 ms transfer, about 125 WPM; other panels unknown until measured | Default |
| Trial A | Shortened waveform, about 300 ms drive | About 151 WPM | Experimental |
| Trial B | Shortened waveform, about 220 ms drive | About 188 WPM | Experimental |
| Trial C | Shortened waveform, about 160 ms drive | About 232 WPM | Experimental |
| Trial D | The panel's stock non-flashing UI waveform (BOOX mode 3); 160 ms on DEPG0397, about 80 ms on YRD0426/OPM040B3, unknown on SE0400NQW47 | Unknown until measured | Experimental |

Predictions assume the 60 ms dwell and are arithmetic, not measurements. Predictions include the at least 38 ms needed to send two 48 KB planes at 20 MHz. Two words per update on the stock profile would deliver about 200 to 222 WPM at X4 timings, and the full 240 if the DEPG0397 prediction holds, which makes it the dependable route to the owner's target range if no trial waveform is clean.

- **How trial waveforms are supplied.** The SDK's `Ssd1677Config` is chosen once from the panel ID and cannot change at run time. Trial tables are loaded per update through the SDK's public `setCustomLUT()`, exposed by one pass-through in `HalDisplay`. The SDK is not forked.
- **Clearing.** Every profile runs a clearing refresh at paragraph and section ends once at least 60 word updates have passed, and unconditionally after 120. Trial profiles may use lower limits.
- **Safety limits.** Trial profiles are off by default and are always followed by a clearing refresh when playback pauses. Trials A to C run only on the three SSD1677 panels. Trial D runs on any recognised panel because it uses only stock waveforms. All trials are disabled on an unrecognised panel and while the screen is inverted. Trial tables keep the panel's stock voltages, and tables A to C leave unchanged pixels undriven. Shortened waveforms can leave faint residue and, with long uninterrupted use, may stress the panel; the limits above bound that exposure.
- **Outcome.** At the end of the device session the owner picks which profile to keep on the device itself. A trial profile rated clean becomes a selectable setting; otherwise only Stock remains visible.

## 7. Checkpoint flow, rereading and saved state

A checkpoint is a small state machine that remembers three things: which prompt is showing, where normal reading resumes, and where a reread is if one is under way. Every transition saves state first, so closing the book at any point restores the same screen.

&#91;embedded content: checkpoint flow · 6 steps, 1 decision\]

Only the first grade given to a prompt is stored, so the loop through a reread changes what the reader sees next, never what was recorded.

### Trigger rules

- A checkpoint is armed while the reading position is inside its section and fires when ordinary forward reading crosses the section's end anchor. "Ordinary forward reading" means RSVP playback or a next-page action.
- Table-of-contents jumps, go-to-percent, bookmarks, progress sync and sentence stepping carry a navigation source that never fires checkpoints, even when they pass several.
- In RSVP, playback stops with the last word of the section on screen; nothing beyond the break is drawn.
- In page view, the checkpoint appears when the reader advances from the page that shows the last character of the section's last word. Text after the break on that same page will already have been visible, which page layout makes unavoidable.
- Reading through the section again later fires the checkpoint again and presents all its prompts, with saved first grades shown as already recorded.
- During a reread, the linked section's own checkpoint is suppressed. Reaching the end anchor, or choosing Return early, goes back to the prompt.

### Per-prompt flow

1. Show the prompt with the answer hidden. Actions: Reveal answer, Skip prompt, Skip checkpoint.
2. After reveal, show the reference answer. Actions: Correct, Incorrect, Skip prompt, Skip checkpoint.
3. Correct moves to the next prompt, or finishes the checkpoint.
4. Incorrect offers Reread section (in RSVP or page view) or Continue. Continue moves on.
5. A reread starts at the section's start anchor in the chosen mode and returns to the same prompt with the answer hidden. The reader may then retry or continue.
6. On finish or Skip checkpoint, restore the mode that was active when the checkpoint appeared, at the first word after the break. RSVP waits for Play; page view waits for the next action.

Each checkpoint screen highlights the safe action by default: Reveal; on the revealed answer, the answer text (never a grade); after Incorrect, the reread in the mode in use. Skip checkpoint is always the last item. A new screen ignores input for 300 ms after it is shown and until keys and touch are released. Layouts: spike S4.

### Grade rules

- The first Correct or Incorrect for a prompt is written once and never overwritten, across retries, rereads, sessions and later passes.
- Skipping writes nothing. A skipped prompt stays ungraded until it is graded for the first time.
- A retry after Incorrect still lets the reader mark Correct to move on; the stored grade stays Incorrect.

### State file

One file per book, `<book file name>.rsvp-state.json`, written to a temporary file and renamed so a power cut cannot leave it half-written.

```json
{
  "version": 1,
  "book_sha256": "<the checkpoint file's book.source_sha256>",
  "overrides": { "wpm": 240, "words_per_update": 1 },
  "position": { "chapter": 3, "word": 1542, "mode": "rsvp" },
  "grades": {
    "c0007-p1": { "first": "correct", "at": "2026-12-01T19:04:00" },
    "c0007-p2": { "first": "incorrect", "at": "2026-12-01T19:05:10" }
  },
  "active_checkpoint": {
    "id": "c0007",
    "prompt_index": 1,
    "resume": { "chapter": 3, "word": 1543, "mode": "rsvp" },
    "reread": { "mode": "page", "chapter": 3, "word": 910 }
  }
}
```

`active_checkpoint` is absent during normal reading, and `reread` is absent unless a reread is in progress. On open, the firmware restores in this order: an active reread, then an active prompt with its answer hidden, then the plain reading position. For books without checkpoint data the file holds only overrides and position, and has no `book_sha256`. The firmware never hashes a book: `book_sha256` is copied from the checkpoint file, where the preparation tool wrote the hash of the original EPUB.

## 8. Preparation workflow

Preparation is one Claude Code command, `/prep-book <path to EPUB>`, that runs a drafter, a deterministic validator and a checker in a separate fresh session, and repeats until both pass or three rounds are used up. Everything a script can verify is verified by script; the AI checker is reserved for what needs reading comprehension.

| Step | Who | Input | Output |
| --- | --- | --- | --- |
| 1. Extract | Script `prep extract` | Original EPUB | Work folder: chapter text with numbered paragraphs, structure map, source hash |
| 2. Draft | Drafter session | Extracted text, `tools/prep/INSTRUCTIONS.md` | `draft.json`: sections as paragraph ranges; prompts and answers, each citing its supporting paragraph numbers |
| 3. Build | Script `prep build` | Original EPUB, `draft.json` | `<title>.prepared.epub` with anchors and `rsvp/checkpoints.json` |
| 4. Validate | Script `prep validate` | Original and prepared EPUB | Pass or a list of structural failures |
| 5. Check | Checker session, fresh context | Extracted text, instructions, prepared data. No drafter transcript | `check.json`: a verdict and evidence per prompt, plus an issue list |
| 6. Revise | Drafter session | Issue lists from steps 4 and 5 | Updated `draft.json`; back to step 3 |
| 7. Deliver | Script `prep report` | All of the above | Prepared EPUB plus validation report (`.md` and `.json`) |

### What the validator script checks

- The checkpoint file matches the schema, and every id is unique.
- Every checkpoint names one existing section; every section's anchors exist in the named chapter, in order, without overlap.
- Every anchor sits at a word boundary as the firmware splits words, and no anchor is inside a table or code block.
- No section spans chapters. Sections cover the book's body, with any excluded front or back matter listed explicitly.
- The word sequence of the prepared copy equals that of the original, and the original's hash is unchanged.
- Prompt and answer length limits, plain-text only, non-empty.
- The prepared file passes EPUBCheck without errors.

### What the checker session checks

- Each reference answer is supported by its own section. The checker must quote the paragraph numbers that support it, and the script confirms those paragraphs lie inside the section.
- Each answer states the essential ideas clearly enough that a reader could accept a correct paraphrase as Correct.
- Each prompt can be answered from its section alone and does not give its answer away.
- Section breaks fall at natural boundaries under the instructions.

### Controls

- **Fresh session.** The checker runs as a separate Claude Code subagent defined in `.claude/agents/`, with no access to the drafter's conversation. It sees only files. A guard (a hook in the agent's definition, `tools/prep/checker_guard.py`) lets it read only its own folder and write exactly one file, its result, and records that write; a result without the guard's record is refused.
- **Where it runs.** `/prep-book` needs a Claude Code session started on the user's own computer, in this repository, in a folder the user has trusted: Claude Code runs the guard's hook only there. In a Claude Code cloud session the hook does not run, so no book can become ready (seen in issue #75). The command's first step is a one-minute probe with the real checker on a made-up text; it stops with instructions when the guard is not active. The scripts themselves (`extract`, `build`, `validate`, `verify-check`, `report`) run anywhere.
- **No pass, no delivery.** `prep report` refuses to mark a book ready unless the latest validator run and the latest checker run both passed against the current prepared file's hash.
- **Bounded loop.** After three failed rounds the command stops and delivers a failed report listing the open issues.
- **Report contents.** Every check with its final status, the number of rounds, each issue raised and how it was resolved, the instructions version, the source hash and the models used.
- **Tooling.** Python 3 with a small dependency set (`lxml`, `jsonschema`), under `tools/prep/`, with its own unit tests. A preview or editing interface is out of scope for v1.

### Test material

- **Synthetic fixture.** A three-chapter made-up book, generated by script, with one single-prompt checkpoint, one multi-prompt checkpoint, an image, a table, a code block and footnotes. Used by automated tests on every layer.
- **Negative fixtures.** One prepared file with a broken section link (the validator must reject it) and one with an answer the section does not support (the checker must flag it). The second is run by the orchestrator as an acceptance task, with the transcript attached to the issue.
- **Real book.** Thomas Paine's *Essays* in the Standard Ebooks edition (public-domain text, CC0 edition), prepared end to end. *Common Sense*, the title first proposed, is not a stand-alone Standard Ebooks title: it is the first and longest essay (about 19,300 words) of this collection, which also holds six shorter and later essays. The preparation instructions allow only front and back matter to be excluded, so the whole collection is prepared (55 sections, 130 prompts, about 49,000 words), not *Common Sense* alone. The source EPUB is committed unmodified as `fixtures/books/paine-essays.epub`, with the prepared book, its final draft and its READY report beside it; `fixtures/books/REAL_BOOKS.md` describes them and `docs/evidence/p5/` holds the run.

## 9. Emulating the Picco

The emulator is CrossPoint's own [SDL2 simulator](https://github.com/crosspoint-reader/crosspoint-simulator) with its Picco profile: the real firmware code compiled for a desktop, shown in a window the size of the Picco's screen. The owner runs it in a browser tab from GitHub Codespaces with one command; agents run the same binary without a screen and drive it by script.

### What the Picco profile emulates

| Hardware | In the simulator |
| --- | --- |
| 800x480 screen | SDL window in portrait; screenshots on demand |
| Touch | Mouse tap and swipe |
| Side keys | Up and Down arrow keys; Return is GPIO5 Confirm (role unconfirmed); Escape, Left and Right do nothing |
| Power button | P |
| Slide switch | T |
| Frontlight | Warm and cool state is tracked, not shown as light |
| SD card | The folder `./fs_/` in the repo; books go in `./fs_/books/` |
| Wi-Fi file transfer | Served on localhost port 8080 |
| Sleep and wake | The process relaunches itself to imitate a wake |

The simulator does not model refresh timing, waveforms or ghosting, and its authors say so. This project adds its own timing model on top (below). Ghosting cannot be previewed anywhere but on the device.

### For the owner: one command in Codespaces

```
./scripts/sim.sh
```

Run from the repo root in a Codespace, the script must do all of the following without further input, and be safe to run again:

1. Install system packages: SDL2 and OpenSSL headers, a virtual display, a VNC server and the noVNC web client.
2. Install PlatformIO and fetch the pinned toolchain and libraries.
3. Build the `simulator_picco` target.
4. Copy the fixture book and the prepared real book into `./fs_/books/`.
5. Start the virtual display and noVNC on port 6080, launch the simulator in it, and print the address of the forwarded port.
6. Print a short key map and how to stop it. `./scripts/sim.sh --reset` wipes simulated settings and state to imitate a new device.

The repo also carries a `.devcontainer/devcontainer.json` that forwards port 6080 and prebuilds the simulator when the Codespace is created, so the first run is quick. The script does not depend on it and installs anything missing.

### For agents: headless and scripted

- The same binary runs under a virtual display (`xvfb-run`) in cloud sessions and in CI.
- Scenarios use the simulator's built-in scripting: timed key presses, taps and swipes, and timed screenshots, all set through environment variables.
- A small Python harness, `tools/simtest/`, runs each scenario in a clean copy of `./fs_/`, converts screenshots to PNG, compares them with stored reference images, and inspects the state files the firmware wrote.
- Kill-and-restart scenarios cover the PRD's restore cases: the harness stops the process in the middle of a prompt or a reread, starts it again and checks the screen and state.
- Timing-sensitive logic is not tested through screenshots. Pacing, slowdown and delivered WPM are tested in host unit tests with a fake clock.

### Timing model

`DisplayProfile` has a simulator backend that waits for a configured time on each word update and each clearing refresh. It starts with the Xteink X4 figures (540 ms and 1,960 ms) and reads `docs/device/picco-timings.json` once the device session has produced real numbers. This lets the simulator show the slowdown and the delivered-WPM readout that the owner will see on the device, and lets scenarios assert them.

### Three test layers

| Layer | Runs where | Proves |
| --- | --- | --- |
| Host unit tests | Cloud sessions, CI | Pacing, chunking, scheduler, trigger rules, grade rules, state encoding, checkpoint file parsing |
| Simulator scenarios | Cloud sessions, CI, Codespaces | Complete flows by touch and by keys, screens, persistence across restarts |
| Device session | The owner's Picco | Real refresh timings, readability, button roles, flashing, the PRD's on-device criterion |

## 10. On-device test protocol

All device work happens in one sitting at the owner's Windows PC, driven by a single PowerShell script and a printed checklist, and it produces three kinds of evidence: results files from the device, a USB serial log, and the owner's readability ratings. The session is designed so that the first destructive step happens only after two matching backups exist.

### The device kit

The orchestrator publishes a GitHub release containing everything the session needs. Nothing is built on the Windows PC.

| Item | Purpose |
| --- | --- |
| `picco-2slot-<version>.zip` | Bootloader (0x0), two-slot partition table (0x8000), boot data `boot_app0.bin` (0x15000) and app (0x20000), never the merged `firmware.factory.bin`, plus `flash-plan.json` listing every offset and SHA-256 |
| `session.ps1` | One entry point: identify, back up, flash, monitor, restore. Sets up Python and `esptool` in a private folder on first run |
| `DEVICE_SESSION.md` | The checklist below, with expected screens and stop conditions |
| `sd-card/` | Fixture book, prepared real book, and one ordinary EPUB |
| `update.bin` | The same firmware as an SD-card update file, for any later fix without USB |

### Session steps

1. **Prepare.** Charge the Picco. Leave it on its Home screen, not in USB drive mode, and connect it by USB. Note the stock firmware version shown in its settings.
2. **Identify (read-only).** `session.ps1 identify` records chip type, flash size, PSRAM and the partition table. It stops unless it finds an ESP32-S3 with 16 MB flash and the stock layout containing `safeboot`. It also reads the security eFuses and stops if Secure Boot or flash encryption is enabled, if download mode is restricted, or if the flash is octal. It saves the stock bootloader header and stops unless the partition table matches `partitions_picco.csv` exactly.
3. **Back up (read-only).** `session.ps1 backup` reads the full flash twice and continues only if both SHA-256 hashes match. The owner copies the backup file to a second location before going on.
4. **Confirm.** The script prints the flash plan and asks the owner to type a confirmation phrase. This is the point of no return for Onyx's bootloader.
5. **Flash.** `session.ps1 flash` writes only the offsets in the plan, verifies each, and refuses any image whose hash differs from the plan.
6. **First boot.** `session.ps1 monitor` starts logging serial output to a file. The owner confirms the home screen appears and copies the `sd-card/` files across by Wi-Fi transfer or card reader.
7. **Controls check.** From the Device test menu, press each key and the power button once and hold each side key; the screen logs the raw input and the logical button. If the side keys feel reversed, set Settings > Controls > Side button layout to Next/Prev (this fixes page view and the RSVP and checkpoint screens at once); if GPIO5 does nothing, set Short power button to Confirm if wanted. Record the result in the results issue.
8. **Prove the update path (optional).** With the cable still attached, install `update.bin` from the SD card through Settings and confirm the device reboots into it (it now runs from the second slot). Record the serial log.
9. **Display benchmark.** From the Device test menu, run the sweep (below) for the stock profile and each trial profile, rating each when asked.
10. **Guided acceptance.** Run the on-device walk-through of the PRD criteria, tapping pass or fail at each step.
11. **Choose a profile.** Pick the display profile to keep.
12. **Return evidence.** Attach the files from `/rsvp-bench/` and the serial log to the GitHub issue titled "Device session results".

Steps 2 and 3 change nothing on the device. The owner may run them earlier, at no risk, to shorten the final session and give the agents confirmed hardware facts sooner.

### Benchmark sweep, per display profile

| Measurement | Method | Recorded |
| --- | --- | --- |
| Word-band update time | 100 updates with real words | Median, 95th percentile, maximum, in ms |
| Full-screen fast update | 10 updates | Median, in ms |
| Clearing refresh | 5 refreshes | Median, in ms |
| RSVP playback, one word per update | A fixed 150-word passage at targets of 120, 180, 240 and 300 WPM | Delivered WPM, words drawn, order check, owner rating |
| RSVP playback, grouped | The same passage at 240 WPM with 2 and with 3 words per update | Delivered WPM, owner rating |
| Endurance | 300 consecutive updates under the profile's clearing policy | Owner rating, whether residue was visible |

Ratings are Clean, Tolerable or Unreadable. Each results file also records the panel name and controller model, firmware build, profile identifiers, battery level and the room temperature the owner enters. The same figures are printed to the serial log, so a failed SD write does not lose the session.

### The PRD's on-device criterion

The guided acceptance plays the passage at the 240 WPM default and at a 600 WPM target. The firmware counts every word index it draws and records whether all were drawn exactly once and in order. The owner confirms that the last sentence was readable and that the pause screen shows a delivered WPM below the target.

### Stop conditions and recovery

| If | Then |
| --- | --- |
| Identify finds a different chip, flash size or layout | Stop. Nothing has changed. Report the output in the issue. |
| Identify finds Secure Boot, flash encryption, restricted download mode, octal flash, or a partition table that differs from `partitions_picco.csv` | Stop. Nothing has changed. Report the output in the issue. |
| The two backup hashes differ | Repeat the backup with another cable or port. Do not flash. |
| Flashing fails part-way or the device does not boot | `session.ps1 restore <backup file>` writes the backup from offset 0 and returns the device to stock. |
| The home screen appears but touch or keys are wrong | Continue with whichever input works; note it. Bindings are fixed by an SD-card update afterwards. |
| A trial profile leaves heavy residue | Rate it Unreadable, run a clearing refresh from the menu, and carry on with the next profile. |

The scripts are exercised before the session by a dry-run mode that prints the exact commands, and by unit tests on the offset guard. They cannot be tested against a real Picco in advance, so the restore path depends on the verified backup above all else.

## 11. Orchestration and work breakdown

Fable 5.2 (effort: high) leads: it turns this plan into GitHub issues, assigns each to an Opus 5.5 (effort: high) implementer session, re-runs the acceptance checks itself on the resulting pull request, and merges or sends it back. It continues until every issue is validated, then hands the device kit to the owner.

### Before the agents start (owner)

- [x] Supply the repo's owner/name: c-tphr/eink-learner-aid (public; push access from cloud sessions confirmed).
- [ ] Attach the repo and the read-only repos in section 12 to the Claude Code cloud environment, and enter the allowlist.
- [ ] In the repo settings, enable GitHub Actions and "Automatically delete head branches".
- [x] Commit this plan to the repo as `docs/PLAN.md` (done; the plan was first committed at the repo root and moved in pull request #41).

### Operating rules

1. **One issue, one branch, one pull request.** Branch `task/<issue number>-<slug>`; the pull request body says `Closes #<n>`.
2. **Issue contents.** Goal, links to the plan sections it implements, in-scope and out-of-scope, acceptance checks as runnable commands, evidence required, dependencies.
3. **Implementer's definition of done.** All acceptance checks pass locally; CI is green; tests were added with the change; the pull request lists what was run and attaches outputs or screenshots; no unrelated edits.
4. **Independent validation.** The orchestrator checks out the branch in its own session, runs the acceptance commands again, reads the diff against the plan, and for user-facing work opens the simulator screenshots. It does not rely on the implementer's report.
5. **Rework.** A failed validation gets a comment listing each failed check with its output, the label `needs-rework`, and reassignment. The same issue stays open until it passes.
6. **Merging.** The orchestrator squash-merges validated pull requests into `main` without waiting for the owner. The one exception is the base import (F1, pull request #42), merged with a merge commit to keep upstream history.
7. **Escalation.** The orchestrator stops and asks the owner only for: a decision this plan fixes as owner-decided, a blocked network host, anything that changes scope, or two consecutive failed reworks on one issue.
8. **Plan changes.** Findings from spikes that change the design are written back to `docs/PLAN.md` by pull request, with the reason, before dependent issues start.

### Constraints of the cloud environment

These come from the [Claude Code cloud environment documentation](https://code.claude.com/docs/en/cloud-environments) and shape how the rules above are carried out.

- `gh issue` and `gh pr` use GitHub's GraphQL API, which cloud sessions cannot reach. Use the built-in GitHub tools or REST calls such as `gh api repos/<owner>/<repo>/issues`.
- Sessions cannot push tags or delete branches. Releases are created through the REST API or a manually triggered GitHub Actions workflow; merged branches are removed by the repo setting above.
- GitHub access is limited to repos attached to the session.
- Sessions run Ubuntu 24.04 with about 4 CPUs, 16 GB of memory and 30 GB of disk. A setup script may install packages, and is cached only if it finishes in about five minutes.

### Continuous integration (GitHub Actions)

Every pull request runs: formatting check; host unit tests; preparation-tool tests; `simulator_picco` build and the scenario suite under a virtual display; `picco_2slot` firmware build with the image-size gate. The firmware image, simulator binary and scenario screenshots are uploaded as artifacts.

### Milestones

| Milestone | Outcome | Gate to pass |
| --- | --- | --- |
| M0 Foundation | Pinned base imported; CI running; cloud setup script; network smoke test | `picco` and `simulator_picco` build in a cloud session and in CI |
| M1 Emulator and harness | `sim.sh` for Codespaces; headless scenario harness; timing model | A scripted scenario opens a book and matches reference screenshots |
| M2 Spikes | Written findings for S1 to S4 and any plan updates | Each finding merged under `docs/spikes/` |
| M3 Format and preparation | Schema, scripts, commands, fixture book | Fixture prepared and validated; broken-link fixture rejected |
| M4 RSVP | RSVP mode on any EPUB with settings and non-prose pauses | Host tests and scenarios green |
| M5 Checkpoints | Triggers, prompts, reread, skip, resume, persistence | PRD criteria 1 to 4 pass in the simulator by touch and by keys |
| M6 Display and device kit | Profiles, benchmark, two-slot build, Windows scripts, checklist | Dry run clean; benchmark runs in the simulator |
| M7 Release candidate | Real book prepared; full acceptance run; device kit released | Orchestrator's acceptance report; owner notified |
| Device session | Owner runs section 10 | Evidence attached to the results issue |
| M8 Closeout | Results parsed; real timings recorded; defaults set; fixes shipped as an SD update if needed | Final report against all six PRD criteria |

M1 and M2 run in parallel after M0. M3 can run alongside M4. M5 needs M3 and M4. M6 needs M4.

### Starting task list

The orchestrator splits these further where a task would exceed one focused pull request. The live breakdown, with the further splits and added tasks, is the issue map in `docs/STATUS.md`.

| ID | Task | Key acceptance check |
| --- | --- | --- |
| F1 | Import the pinned base; write `docs/BASE.md` | Upstream and SDK commits recorded; upstream build passes unchanged |
| F2 | Cloud setup script and network smoke test | Script completes; smoke test lists every host contacted and any refusals |
| F3 | CI workflow | All jobs listed above run on a pull request |
| F4 | Feature flag and empty module skeletons | Other device targets build byte-for-byte as before |
| E1 | `scripts/sim.sh` and devcontainer | Fresh Codespace: one command ends with the simulator visible on port 6080 |
| E2 | Scenario harness `tools/simtest/` | Sample scenario passes headless; reference-image comparison works |
| E3 | Simulator timing model in DisplayProfile | Scenario shows delivered WPM below target with a 540 ms delay configured |
| S1 | Spike: word stream and page-to-word mapping in CrossPoint's EPUB engine | Finding names the hook points and the cache format; prototype maps a page to a word index |
| S2 | Spike: SSD1677 waveform tables and how the SDK injects them | Finding lists the trial tables, their drive times and the run-time switch method |
| S3 | Spike: two-slot partition table and image-size budget | Table file, slot sizes and current image size documented |
| S4 | Spike: Picco input roles and control map | One-page control map covering every action by touch and by keys |
| P1 | Checkpoint file schema and shared sample files | Samples validate or fail as labelled in Python |
| P2 | `prep extract`, `build`, `validate`, `report` | Fixture round-trips; word sequence identical; EPUBCheck clean |
| P3 | Synthetic fixture generator and negative fixtures | Fixture contains every element type listed in section 8 |
| P4 | `/prep-book` command, drafter instructions, checker subagent | Fixture prepared end to end with a passing report |
| P5 | Prepare the real book | Prepared file and passing report committed; unsupported-answer fixture flagged |
| R1 | RsvpCore: words, focus letter, pacing, grouping | Host tests cover punctuation, long words, group boundaries |
| R2 | RsvpCore: scheduler and delivered-WPM meter | Fake-clock tests: no word skipped at any refresh delay; meter excludes pauses |
| R3 | WordIndex build and cache | Index for fixture matches expected word and anchor positions |
| R4 | RsvpActivity: playback, pause screen, settings, mode switch; single binding table `src/controls/ControlMap.h` | Scenarios by touch and by keys; global and per-book settings persist |
| R5 | Non-prose pauses and footnote handling | Scenario pauses at image, table, code; passes footnote marks |
| C1 | CheckpointCore: parsing, trigger and grade rules, state machine | Host tests for every rule in section 7 |
| C2 | ReadingSession: triggers in both modes, navigation sources | Scenario: reading in fires; table-of-contents jump does not |
| C3 | CheckpointActivity: prompt, reveal, grade, skip | Scenarios for single- and multi-prompt checkpoints |
| C4 | Reread in either mode and return | Scenario for PRD criterion 2 |
| C5 | State file, atomic save, restore on open | Kill-and-restart scenarios for prompt and reread |
| D1 | DisplayProfile on device: stock profile, clearing policy, timing capture | Builds; simulator backend unchanged |
| D2 | Trial profiles A to D with safety limits | Trials A to C disabled on SSD2677 path; off by default; unit test on limits |
| D3 | DeviceTestActivity: sweep, ratings, guided acceptance, results files; Controls check screen | Full sweep runs in the simulator and writes valid results |
| D4 | `picco_2slot` target, size gate, updates enabled on two-slot only | Image under budget; update menu hidden on single-slot table; Wi-Fi updater points at this repo's releases |
| D5 | `session.ps1`, flash plan, offset guard, dry run, restore | Guard tests pass; dry run prints only planned offsets |
| D6 | `DEVICE_SESSION.md` and the results-issue template | Checklist reviewed against section 10 step by step |
| A1 | Full acceptance run in the simulator and release of the device kit | Report maps each PRD criterion to passing evidence |
| A2 | Parse device results; write `picco-timings.json`; set defaults | Timing model updated; final report published |

## 12. Recommended network allowlist

Set the cloud environment's network access to Custom, tick "Also include default list of common package managers", and add the ten lines below. The default list already covers GitHub, PyPI, npm, Ubuntu packages, container registries and Anthropic's documentation; the additions cover PlatformIO, Espressif, and reference documentation the default list lacks. Source for the settings and the default list: [Claude Code cloud environments](https://code.claude.com/docs/en/cloud-environments).

### Lines to add

```
registry.platformio.org
*.registry.platformio.org
dl.platformio.org
docs.platformio.org
dl.espressif.com
docs.espressif.com
www.w3.org
wiki.libsdl.org
docs.github.com
standardebooks.org
```

| Domain | Needed for | Kind |
| --- | --- | --- |
| `registry.platformio.org`, `*.registry.platformio.org` | PlatformIO package index and downloads. The API and download hosts sit under this name, including numbered mirrors such as `dl.registry.ns1.platformio.org` | Build |
| `dl.platformio.org` | PlatformIO core tool downloads | Build |
| `dl.espressif.com` | Espressif compiler toolchains and tools | Build |
| `docs.platformio.org` | PlatformIO configuration reference | Docs |
| `docs.espressif.com` | ESP32-S3 partition table, OTA and `esptool` reference | Docs |
| `www.w3.org` | EPUB specifications | Docs |
| `wiki.libsdl.org` | SDL2 reference for simulator work | Docs |
| `docs.github.com` | GitHub REST API and Actions reference | Docs |
| `standardebooks.org` | One-time download of the public-domain test book. Remove after the file is committed | Test data |

### Already allowed by the default list (no action)

`github.com`, `api.github.com`, `raw.githubusercontent.com`, `objects.githubusercontent.com`, `release-assets.githubusercontent.com`, `codeload.github.com`, `pypi.org`, `files.pythonhosted.org`, `archive.ubuntu.com`, `security.ubuntu.com`, `ghcr.io`, `registry.npmjs.org`, `json-schema.org`, `docs.claude.com`, `code.claude.com`.

### GitHub repos to attach to the sessions

GitHub traffic goes through a separate proxy that serves only attached repos, whatever the allowlist says. Source archives of unattached repos are refused even when public.

| Repo | Access | Why |
| --- | --- | --- |
| c-tphr/eink-learner-aid (the project repo) | Push | All work |
| `crosspoint-reader/crosspoint-reader` | Read | Base firmware, for import and reference |
| `crosspoint-reader/crosspoint-simulator` | Read | Simulator library |
| `Free-Ink/freeink-sdk` | Read | Hardware SDK submodule |
| `pioarduino/platform-espressif32` | Read | Build platform, fetched as a release download |
| `w3c/epubcheck` | Read | EPUB validator release |

### Deliberately left out

Reddit, Discord, MobileRead, Hacker News, the PlatformIO community forum, `crosspointreader.com`, and search engines. Community findings the agents need are already written into this plan. The owner's Codespace is separate and is not governed by this list.

### What is not yet verified

I could not run a build inside the target environment, so the list is drawn from documentation and build configurations, not from observed traffic. The pioarduino platform may pull packages from further hosts or GitHub repos. Task F2 settles this: it runs a clean build of both targets, records every host contacted, and reports any request refused with `host_not_allowed` so the owner can add it. To keep later sessions independent of the network, the setup script pre-fetches the toolchain, and submodules are pinned in the repo.

## 13. Risks and acceptance mapping

The largest risk is the combination the owner chose knowingly: one device session that also replaces Onyx's bootloader. The plan answers it with a double-verified backup before any write, a guarded flash script, and a one-command restore.

| Risk | Likelihood | Consequence | Mitigation |
| --- | --- | --- | --- |
| Flash goes wrong in the only session | Low to medium | Device unusable until restored | Two matching backups before any write; offset guard; dry run; `restore` command; optional early read-only backup |
| The generic Arduino bootloader does not boot this unit (security eFuses, flash type or voltage) | Low | Device does not boot until restored | Identify-step eFuse and header checks; same bootloader already runs upstream's other ESP32-S3 octal-PSRAM boards; restore from backup over ROM download mode |
| Stock refresh is far slower than 240 WPM needs | High | Default pace delivers about 100 to 125 WPM single-word | Expected and handled by the PRD's slowdown; two words per update offered; trial profiles |
| No trial waveform is clean, or the unit has the SE0400NQW47 panel (only Stock and Trial D available) | Medium | No fast setting in v1 | Stock stays default; outcome recorded; no further waveform work in v1 |
| Shortened waveforms stress the panel | Low | Residue or long-term wear | Off by default; bounded runs; clearing refreshes; owner chooses whether to keep one |
| Word and page mapping in CrossPoint's engine is harder than assumed | Low (spike S1) | Delays M4 and M5 | Spike S1 found an exact mapping through CrossPoint's visible-text offsets with no layout changes, so the paragraph-level fallback is not needed. The remaining risk is the word pass counting offsets differently from the renderer, guarded by a host parity test (R3). |
| Simulator differs from the device (button roles, touch, memory) | Medium | Input or layout surprises in the session | Single remappable control table; both input paths for every action; fixes shipped by SD update; Controls check screen in the device test menu |
| Two-slot image outgrows its slot | Low | Build cannot be flashed | CI size gate with 10% margin from the first build |
| Cloud network blocks a build dependency | Medium | Agents cannot build | Task F2 smoke test before any feature work; owner adds hosts |
| AI checker verdicts vary between runs | Medium | Flaky preparation | Structural checks are scripted; the checker must cite paragraphs; loop capped at three rounds |
| Flashing voids the warranty | Certain | No vendor support afterwards | Accepted by the owner; backup allows return to stock firmware |

### PRD acceptance criteria

| PRD criterion | Verified by | Where |
| --- | --- | --- |
| 1. Single- and multi-prompt checkpoints fire on reading in, in both modes; a jump does not fire them | Scenarios from tasks C2 and C3, each by touch and by keys | Simulator, CI |
| 2. Mark second prompt Incorrect, reread in either mode, return early or at the end, retry hidden; later Correct keeps the saved Incorrect and the first prompt's grade | Host tests (C1) and scenario (C4) that reads the state file afterwards | Host, simulator |
| 3. Skip a prompt or leave a part-done checkpoint; ungraded stays ungraded; grades survive; resume after the break in the original mode without auto-advance | Scenarios (C3), state-file assertions | Simulator |
| 4. Close and reopen restores an interrupted prompt or reread with the right return position; a later pass shows checkpoints with first grades intact | Kill-and-restart scenarios (C5) | Simulator |
| 5. On a physical Picco, the default pace and an over-fast target slow down without dropping words, and the pause screen shows delivered WPM | Guided acceptance in the device session; word-order record in the results file; the same logic under fake clock (R2) | Device, host |
| 6. Preparation yields a ready file and report only after the checker passes; a broken link or unsupported answer is sent back | Validator tests (P2, P3); orchestrator-run checker acceptance (P5) | CI, cloud session |

## 14. Sources

Pages opened while preparing this plan, as of 16 Nov 2026.

- [CrossPoint Reader repository](https://github.com/crosspoint-reader/crosspoint-reader): licence, supported devices, repo layout, caching.
- [CrossPoint PR #3947, BOOX Picco support](https://github.com/crosspoint-reader/crosspoint-reader/pull/3947): touch controller, charger, slide switch, stock partition table.
- [CrossPoint issue #4248](https://github.com/crosspoint-reader/crosspoint-reader/issues/4248): the Picco upload target overwriting the vendor bootloader and part of NVS.
- [FreeInk SDK](https://github.com/Free-Ink/freeink-sdk): Picco hardware row, refresh modes, waveform injection, host tests.
- [CrossPoint Simulator](https://github.com/crosspoint-reader/crosspoint-simulator) and its [Picco profile pull request](https://github.com/crosspoint-reader/crosspoint-simulator/pull/45): device profiles, scripting, screenshots, stated limits.
- [KatiePoint 2.0.0-rc3 for BOOX Picco](https://github.com/InsiderPhD/crosspoint-reader/releases/tag/2.0.0-rc3): backup, app-slot and two-slot flashing, warranty note.
- [crossplay PR #320](https://github.com/ma-r-s/crossplay/pull/320): flash and PSRAM size, upload guard, running-slot protection.
- [crosspoint-reader-picco](https://github.com/mreza-yousefi/crosspoint-reader-picco): licence terms, app-only upload, connection state for flashing.
- [crosspoint-rsvp](https://github.com/NormousIndustries/crosspoint-rsvp): existing RSVP fork for the Xteink X3.
- [inkflow RSVP reader for the Xteink X4](https://github.com/wittyreference/dotfiles/pull/6): measured refresh times and pacing method.
- [readme.club report on the Picco firmware](https://www.readme.club/news/readme-club-exclusive-boox-picco-is-running-crosspoint): early hardware claim, superseded.
- [Claude Code cloud environments](https://code.claude.com/docs/en/cloud-environments): network access levels, default allowlist, GitHub proxy limits, setup scripts, resource limits.
- [pioarduino platform-espressif32](https://github.com/pioarduino/platform-espressif32): how the build platform is distributed.
- [Dev Containers desktop-lite notes](https://github.com/microsoft/vscode-dev-containers/blob/master/script-library/docs/desktop-lite.md): browser-accessible desktop on port 6080 for Codespaces.

Not verified from a primary source: the exact hosts the pioarduino toolchain downloads from and the Picco's real refresh timings. Each is resolved by a named task or by the device session. Whether *Common Sense* is available from Standard Ebooks was checked in task P5 (issue #38, 19 Nov 2026): it is not a stand-alone title there but the first essay of the collection [*Essays*](https://standardebooks.org/ebooks/thomas-paine/essays), which is committed and prepared as a whole.
