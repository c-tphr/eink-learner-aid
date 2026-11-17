# Evidence: task C4 (#28), reread in either mode and return

Copied from one simulator run of the five reread scenarios
(`python3 -m tools.simtest run cp_reread_rsvp_end cp_reread_page_early cp_reread_twice cp_reread_no_other_fire cp_reread_restart`,
all passed). `<scenario>.log.txt` keeps the `[CPT]` and `[CTL]` lines, RSVP's
enter/play/pause/leave/reread/banner lines and the reader's `Progress saved` lines;
`<scenario>.rsvp-state.json` is the book's state file at the end of the scenario. The scenarios' own
reference images are in `tools/simtest/scenarios/cp_reread_*/refs/`; the behaviour is described in
`src/reading/README.md`, "Rereading".

## Screens

| File | Screen |
| --- | --- |
| `cp_reread_rsvp_end-t_rsvp_reread_pause.png` | RSVP's pause screen at the start of a reread: status "Rereading", "Back to the question" in the row under Exit, Menu and Page view (S4 row 30), Play highlighted, and the next words ("The Visitor from the Mainland On the") in the row above the key hint |
| `cp_reread_rsvp_end-t_prompt_after_reread.png` | The prompt after the reread reached the section's end by itself: question 2 of 3, answer hidden, "Recorded earlier: Incorrect" |
| `cp_reread_page_early-t_page_reread_bar.png` | Page view at the section's first page with the reread bar over the status-bar row, below the text area (y 773-800): "Rereading" and "Back to the question" (button 216,774,248,26). Every line of the page is visible |
| `cp_reread_page_early-k_reread_list.png` | The list a side-key hold opens during a page-view reread: "Back to the question" (default), "Speed read from here", "Keep reading" |
| `cp_reread_no_other_fire-k_reread_item_banner.png` | An RSVP reread stopped at the image inside s0001: page view with the non-prose banner, which covers the bottom of the page (the reread bar included); Continue goes back into the reread |
| `cp_reread_twice-t_rsvp_resume_after_page_reread.png` | A checkpoint that fired in RSVP, reread in page view, skipped: RSVP's pause screen after the break (chapter 3 word 0), "Checkpoint done" |
| `cp_reread_restart-t_reopened_page.png` | After a power loss during a page-view reread: the book reopens at the reread page without the bar (nothing restores the reread until task C5) |

## State files

- `cp_reread_rsvp_end`, `cp_reread_page_early`: `c0002-p1` correct, `-p2` **incorrect** (marked
  Correct on the retry after the reread, twice), `-p3` correct; position 3:0 in the mode the checkpoint
  fired in (rsvp, page); no checkpoint in progress.
- `cp_reread_twice`: only `c0002-p1`, incorrect, after three Incorrect answers and two rereads (one in
  each mode, by touch and by keys); position 3:0 page.
- `cp_reread_no_other_fire`: only `c0001-p1`, incorrect; position 1:233 page, the first word after the
  original break, although the second reread was left by a table-of-contents jump to chapter 2.
- `cp_reread_restart`: the file checked right after the kill during a page-view reread (`expect_json`
  steps of the scenario):

  ```json
  "position": { "chapter": 2, "word": 490, "mode": "page" },
  "grades": { "c0002-p1": { "first": "correct", ... }, "c0002-p2": { "first": "incorrect", ... } },
  "active_checkpoint": {
    "id": "c0002",
    "prompt_index": 1,
    "resume": { "chapter": 3, "word": 0, "mode": "page" },
    "reread": { "mode": "page", "chapter": 2, "word": 381 }
  }
  ```

  At the end (after a reopen, a second reread in RSVP, a sleep and a wake): the two grades unchanged,
  `-p3` never graded, position 3:0 page, no checkpoint in progress.

## Key log lines

- Reread start: `[CPT] saved after=reread-rsvp ... reread=2:311:rsvp`, `[CPT] reread start mode=rsvp
  checkpoint=c0002 section=s0004 prompt=1 ch=2 start=311 last_word=607`, `[RSVP] reread ch=2 word=311`.
- Suppressed checkpoint: `[CPT] rsvp stop ch=2 last=607 next=608 step=reread-end reread=s0004`,
  `[CPT] page advance ch=2 first=490 from=490 last=607 step=reread-end reread=s0004`,
  `[CPT] page advance ch=1 first=184 from=184 last=294 step=reread-end reread=s0001`.
- Audit: `[RSVP] pause reason=reread-end ch=2 word=608 ... audit=ok drawn=415 segments=2 violations=0`
  (the jump back is a new segment).
- Return: `[CPT] reread end by=<rsvp-playback|next-page|pause-screen|bar|list> event=<reread-reached-end|return-early>
  accepted=1 state=prompt-hidden`, `[CPT] back checkpoint=c0002 prompt=2/3 mode=rsvp`, a jump out:
  `[CPT] reread moved by=page source=table-of-contents ... left_section=1`.
- Grades: `[CPT] event=correct prompt=c0002-p2 accepted=1 refusal=0 state=prompt-hidden grade_recorded=0
  save=now` (the retry's Correct records nothing); `[CPT] event=incorrect prompt=c0002-p1 ...
  grade_recorded=0 save=no` (a second Incorrect writes nothing).
- Resume: `[CPT] resume end=finished mode=rsvp ch=3 word=0 reread=1`, `[CPT] page resume rsvp ch=3
  word=0`, `[RSVP] leave to=checkpoint-done end=skipped`.
