# Evidence for task R4a (#21): RSVP playback and pause screen

Screenshots from the simulator (`simulator_picco`, 480x800 portrait), copied from the reference
images of the scenarios in `tools/simtest/scenarios/rsvp_*` (the scenario compares against its own
`refs/` copy; these are for review).

| File | Scenario and step | What it shows |
| --- | --- | --- |
| `pause-entry.png` | `rsvp_play_touch`, after More > Speed read from chapter 1, page 1 | The pause screen as RSVP opens: status "Paused · 240 WPM target · -- actual" (no playback yet), the position (chapter title, chapter and book percent), Exit / Menu / Page view, the words Play starts with ("The Lamp Room The supply boat": the page's first words), back one sentence, **Play highlighted** (black), forward one sentence, the WPM and words-per-update values (R4b adds their steppers in these rows and the two save buttons below them), and the key hint |
| `word-frame-room.png` | `rsvp_play_touch`, frame of word 2 | One word update: "Room" in NotoSans 18 bold, its focus letter ("o", the second of four letters) centred on the fixed focus column at x 192, marked by the two ticks above and below the band. Black and white only; nothing else on the playback screen |
| `word-frame-next-chapter.png` | `rsvp_chapter_end`, frame of chapter 2's word 2 | Playback continued from chapter 1 into chapter 2 without a stop: "Season" (6 letters, focus on the third, "a") on the same column |
| `pause-after-slowdown.png` | `rsvp_slowdown`, after 13 words with the modelled 540 ms update | The pause screen after slowed playback: "240 WPM target · 100 actual" (the firmware logged delivered=99.8 for this run), the position, and the next words ("It had crossed the strait at 3.5 knots," from word 13) |
| `pause-focus-page-view.png` | `rsvp_play_keys`, after a key pause and two short Up presses | The highlight moved Play -> back one sentence -> Page view with the side keys; the next Up hold opens page view |
| `pause-after-sentence-steps.png` | `rsvp_sentence_step_touch`, after two "Forward one sentence" taps | The position moved 0 -> 3 -> 13 (fixture ground truth); the next-words row starts at "It had crossed", and the touched item keeps the highlight |
