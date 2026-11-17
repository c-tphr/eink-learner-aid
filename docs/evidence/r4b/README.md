# Evidence for task R4b (#22)

Screenshots and files from the simulator scenarios `rsvp_settings_touch`, `rsvp_settings_keys`,
`rsvp_mode_switch` and `rsvp_restore_position` (`tools/simtest/scenarios/`), one run of each with
`--keep`, on the `simulator_picco` build of this branch. The display timing model was set to 0 ms.

## Screenshots

| File | What it shows |
| --- | --- |
| `pause-steppers.png` | The pause screen on entry: minus and plus steppers around "240 WPM" and "1 word per update", then "Save as default" and "Save for this book" |
| `pause-values-100wpm-2words.png` | After the touch steppers: 100 WPM, 2 words per update (status line target 100) |
| `touch-saved-default.png`, `touch-saved-book.png` | After each save by touch: the status line starts "Saved as default", "Saved for this book" |
| `keys-focus-wpm-up.png` | Key path: the highlight on WPM + (Down x3 from Play), global default 940 WPM from the settings file |
| `keys-saved-default.png`, `keys-saved-book.png` | After each save by a key hold, at 1000 WPM and 2 words |
| `mode-switch-page-view-word13.png` | Page view after RSVP at word 13: page 1, which holds it |
| `mode-switch-rsvp-again-word13.png` | RSVP entered again from that page: it opens at word 13 ("It had crossed the strait at ..."), not at the page's first word |
| `restore-after-home-word108.png` | The book reopened after the Home gesture: RSVP opens by itself at word 108 ("Figure 1. Gannet Rock ...") |
| `restore-after-kill-word111.png` | After SIGKILL and restart: RSVP at word 111 ("Rock light, seen from ...") |
| `restore-after-sleep-word118.png` | After sleep and wake: RSVP at word 118 ("There were one hundred ...") |

## Files as written

State files (`books/fixture.epub.rsvp-state.json` at the end of each scenario):

| File | Notes |
| --- | --- |
| `rsvp_settings_touch.rsvp-state.json` | Overrides 100 WPM, 2 words (Save for this book); position: the pause, RSVP mode (Exit keeps RSVP mode) |
| `rsvp_settings_keys.rsvp-state.json` | Overrides 1000 WPM, 2 words |
| `rsvp_mode_switch.rsvp-state.json` | Empty overrides; word 0, RSVP mode (left by Exit). The card started with a version 2 file |
| `rsvp_mode_switch.rsvp-state.json.bad` | That version 2 file, moved aside by the first save |
| `rsvp_restore_position.rsvp-state.json` | The seeded override (60 WPM) kept through every position write; word 118 saved at sleep |

Settings file entries (`/.crosspoint/settings.json`, the RSVP keys and the long-press option only;
the rest of the file is upstream's):

| File | Notes |
| --- | --- |
| `rsvp_settings_touch.settings-rsvp.json` | `rsvpWpmTens` 10 (100 WPM), `rsvpWordsPerUpdate` 2 |
| `rsvp_settings_keys.settings-rsvp.json` | `rsvpWpmTens` 100 (1000 WPM), `rsvpWordsPerUpdate` 2 |
| `rsvp_restore_position.settings-rsvp.json` | The seeded global default, 300 WPM, which the book's 60 WPM override beat |
