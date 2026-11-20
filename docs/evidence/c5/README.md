# Evidence: task C5 (#29), restore an interrupted prompt or reread when the book opens

Copied from one simulator run of the task's scenarios
(`python3 -m tools.simtest run cp_kill_in_prompt cp_kill_in_reread cp_second_pass_grades cp_sleep_restore cp_restore_damaged cp_walkthrough_restore cp_skip_grades`,
all passed). `<scenario>.log.txt` keeps the `[CPT]` lines, RSVP's `book state`, `state`, `enter` and
`pause screen` lines, the reader's `Progress saved` lines, the harness's run lines and the sleeps.
The behaviour is described in `src/reading/README.md`, "Opening a book".

## Screens: before the kill or the sleep, and after the restore

| Before | After | What |
| --- | --- | --- |
| `cp_kill_in_prompt-t_before_kill_answer_shown.png` | `cp_kill_in_prompt-t_after_restore_prompt2.png` | Page view checkpoint, touch: killed with question 2's answer on screen; reopened from Home's card directly at question 2 of 3 with the answer hidden (no page of the book drawn first) |
| `cp_kill_in_prompt-k_before_kill_answer_shown.png` | `cp_kill_in_prompt-k_after_restore_prompt2.png` | RSVP checkpoint, keys: the same, "Recorded earlier: Correct" from pass 1 |
| `cp_kill_in_reread-t_before_kill_reread_page.png` | `cp_kill_in_reread-t_after_restore_reread_page.png` | Page-view reread: the page with words 381-489 and the strip "Rereading" / "Back to the question"; the two images are identical |
| `cp_kill_in_reread-k_before_kill_reread_rsvp.png` | `cp_kill_in_reread-k_after_restore_reread_rsvp.png` | RSVP reread paused inside the section: the pause screen at the same word ("find a young fisherman, half-frozen and"), Play highlighted; only the top line's delivered WPM differs ("-- actual" in the new session) |
| `cp_sleep_restore-t_before_sleep_prompt.png` | `cp_sleep_restore-t_after_wake_prompt.png` | Sleep while question 2 shows, wake: the same question |
| `cp_sleep_restore-k_before_sleep_reread_page.png` | `cp_sleep_restore-k_after_wake_reread_page.png` | Sleep during a page-view reread, wake: the same page with the strip |
| `cp_sleep_restore-t_before_sleep_reread_rsvp.png` | `cp_sleep_restore-t_after_wake_reread_rsvp.png` | Sleep during an RSVP reread, wake: RSVP's reread pause screen at the same word |
| `cp_walkthrough_restore-step10_before_sleep.png` | `cp_walkthrough_restore-step10_after_wake.png` | Walk-through step 10 as written: c0001's reread on chapter 1's second page |
| | `cp_walkthrough_restore-step9_reopened_question.png` | Walk-through step 9 as written: question 1 of 1, answer hidden, "Recorded earlier: Correct" |
| | `cp_restore_damaged-a_unknown_after_break.png` | An unknown checkpoint id: page view after its break (chapter 3) |
| | `cp_restore_damaged-c_prompt_instead_of_reread.png` | A reread outside its section: the prompt instead |
| | `cp_second_pass_grades-t_second_pass_p2_recorded.png`, `-k_second_pass_p3_recorded.png` | A later pass after a restart: "Recorded earlier: Incorrect" / "Correct" |

## State files

The state file as each kill left it (from copies of the two scenarios that stop at that kill), and at
the end of each scenario:

- `cp_kill_in_prompt.1-after-kill-touch.rsvp-state.json`: c0002 in progress at `prompt_index` 1 (question 2), `resume` 3:0 page, no `reread`; `c0002-p1` correct; `position` 2:490 page (before the break).
- `cp_kill_in_prompt.2-after-kill-keys.rsvp-state.json`: the same checkpoint at question 2, now fired in RSVP: `resume` 3:0 rsvp, `position` 2:607 rsvp (the frame on screen); grades p1, p2, p3 correct from pass 1.
- `cp_kill_in_prompt.final.rsvp-state.json`: p1, p2, p3 correct (pass 2's Incorrect on p2 changed nothing), `position` 3:0 rsvp, nothing in progress.
- `cp_kill_in_reread.1-after-kill-page.rsvp-state.json`: `reread` page 2:381, `prompt_index` 1, `resume` 3:0 page; p1 correct, p2 incorrect.
- `cp_kill_in_reread.2-after-kill-rsvp.rsvp-state.json`: `reread` rsvp at a word of s0004, `resume` 3:0 rsvp.
- `cp_kill_in_reread.final.rsvp-state.json`, `cp_sleep_restore.final.rsvp-state.json`: p1 correct, p2 **incorrect** after Correct on every retry; p3 never graded; nothing in progress.
- `cp_second_pass_grades.final.rsvp-state.json`: the full pass's grades (correct, incorrect, correct) after two later passes with the opposite answers.
- `cp_restore_damaged.<book>.final.rsvp-state.json`: each cleaned at open, `c0002-p1` kept in all four. a-unknown and b-prompt-range: no `active_checkpoint`, `position` at the dropped checkpoint's resume (3:0, page and rsvp); c-reread-outside: the checkpoint still in progress at question 2, without `reread`; d-other-edition: the book's own `book_sha256`, no position, no checkpoint.
- `cp_skip_grades.final.rsvp-state.json`, `cp_walkthrough_restore.final.rsvp-state.json`.

## Key log lines

What was saved before the kill, what `planRestore()` returned, what opened (`cp_kill_in_prompt`):

```
[CPT] saved after=correct prompt=c0002-p1 grade=correct active=c0002 prompt_index=1 position=none ok=1 reread=none
[simtest] run 1: sending SIGKILL to process group ... (imitates pulling the battery)
[RSVP] book state read mode=page ch=2 word=490
[CPT] restore view=prompt checkpoint=c0002 prompt=2/3 place=3:0:page resume=3:0:page notes=none progress_bytes=78056
[CPT] restore open show=prompt speed_read=0 jump=0 ch=0 offset=0 kept_bytes=78188
[CPT] back checkpoint=c0002 prompt=2/3 mode=page activity_bytes=4056
[CPT] screen=Prompt question=2/3 prompt=c0002-p2 recorded=none focus=Reveal ...
```

`cp_kill_in_reread`, page view, then RSVP:

```
[CPT] saved after=reread-moved prompt=- grade=none active=c0002 prompt_index=1 position=none ok=1 reread=2:381:page
[CPT] restore view=reread checkpoint=c0002 prompt=2/3 place=2:381:page resume=3:0:page notes=none progress_bytes=78056
[CPT] restore open show=reread speed_read=0 jump=1 ch=2 offset=1978 kept_bytes=78188
[CPT] reread page ch=2 first=381 last=489 any=1 inside=1 word=381
[CPT] reread bar shown top=773 button=216,774,248,26 clear_of_text=1 label="Rereading"
...
[CPT] restore view=reread checkpoint=c0002 prompt=2/3 place=2:3xx:rsvp resume=3:0:rsvp notes=none ...
[CPT] restore open show=reread speed_read=1 jump=1 ch=2 ...
[CPT] reread page ch=2 under rsvp: not the reread place
[RSVP] enter ch=2 ... word=3xx ... from=reread
[RSVP] pause screen focus=Play refresh=clear status="Rereading · 1000 WPM target · -- actual"
```

`cp_restore_damaged`, one line per reason:

```
[CPT] restore dropped checkpoint=c0099 prompt=1 reason=unknown-checkpoint saved=1
[CPT] restore dropped checkpoint=c0002 prompt=8 reason=prompt-out-of-range saved=1
[CPT] restore dropped checkpoint=c0002 prompt=2 reason=reread-outside-section saved=1
[CPT] restore dropped reason=other-edition grades=1 saved=1
```

## RAM

From the simulator's size lines (64-bit; the ESP32's pointers are half the size): the restored
checkpoint keeps `kept_bytes=78188` (the 78,056-byte `checkpointentry::Progress` and 132 bytes for the
run and its outcome). While a restored prompt shows: `CheckpointBook` 72,544 + layout 8,120 + session
128 + 78,188 + CheckpointActivity 4,056 = 163,036 bytes of RSVP blocks, the same as a prompt fired by
reading (no reader chapter is loaded yet), plus 78 KB and the file for the duration of a grade's write.
At book open: nothing for a book without a state file; for one with a state file and nothing in
progress, 78,056 bytes and the file's bytes, freed before the book shows (as before C5).
