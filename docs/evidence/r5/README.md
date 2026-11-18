# Task R5 evidence: non-prose items in page view

Screenshots from the simulator (`simulator_picco`, 480x800 portrait) on `fixtures/books/fixture.epub`,
taken by the scenarios `rsvp_nonprose_touch` and `rsvp_nonprose_keys` (`tools/simtest/scenarios/`).
The behaviour is described in `src/activities/rsvp/README.md`, "Non-prose items".

| File | Scenario, moment | What it shows |
| --- | --- | --- |
| `image-banner.png` | touch, after playing words 101-107 of chapter 1 | Page view on page 2, which starts with the lighthouse figure; the banner "Image. Speed reading stopped here." with Continue highlighted. The caption is prose and is read after the image |
| `image-continue-pause.png` | touch, after Continue | RSVP's pause screen at word 108 ("Figure 1. Gannet Rock light, seen from"), Play highlighted; the status line shows the session's delivered WPM so far |
| `table-banner.png` | touch, after playing to word 160 of chapter 2 | The table's first page (the table runs over two pages) with "Table. Speed reading stopped here."; the table's caption belongs to the table |
| `table-banner-stay-focus.png` | keys, after one short Down press | The same banner with Stay highlighted (S4 row 16: Down x1, hold) |
| `table-rest-after-swipe.png` | touch, after a page-turn swipe on the banner | Stay, and the page turned: page 3 shows the rest of the table in normal page view |
| `code-banner.png` | touch, after playing to word 115 of chapter 3 | The code block's page with "Code block. Speed reading stopped here." |
| `resumed-after-restart.png` | touch, SIGKILL while the code block's banner showed, restart, book reopened from Home | RSVP opens by itself at word 141, the first word after the code block, Play highlighted |

State file (`books/fixture.epub.rsvp-state.json`), checked by the scenarios:

- At the hand-over (code block, after the kill): `{"version": 1, "overrides": {}, "position": {"chapter": 3, "word": 141, "mode": "rsvp"}}`, and the reader's `progress.bin` starts `0300 0100` (chapter 3, page 2: the code block's page).
- After Continue: unchanged until the next pause, which writes the next word in RSVP mode (`position saved reason=pause ch=1 word=113 mode=rsvp`).
- After Stay (tap, Back or swipe): `position saved reason=stay ch=2 word=203 mode=page ok=1`.
