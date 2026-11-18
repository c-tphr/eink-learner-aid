# Evidence: task C3a (#93), checkpoint data loader and screens

Copied from one simulator run of the four scenarios (`python3 -m tools.simtest run cp_screens_touch
cp_screens_keys cp_long_answer cp_skip_grades`, all passed) on the branch after merging `main` with
D2 (#92). The scenarios' own reference images are in `tools/simtest/scenarios/cp_*/refs/`.

## Screens

| File | Screen |
| --- | --- |
| `cp_screens_touch-a_prompt.png` | Prompt, answer hidden, Reveal highlighted (fixture `c0001`, one prompt) |
| `cp_screens_touch-a_answer.png` | Answer revealed, the answer box highlighted (6 px border), never a grade |
| `cp_screens_touch-b_incorrect_page.png` | After Incorrect, from page view: Reread in page view is the default |
| `cp_screens_touch-f_incorrect_rsvp.png` | After Incorrect, from RSVP: Reread in RSVP is the default |
| `cp_screens_touch-c_prompt1_recorded.png` | A later pass: "Recorded earlier: Incorrect" on the prompt |
| `cp_screens_touch-c_answer1_recorded.png` | A later pass, answer: "Question 1 of 3 · recorded: Incorrect" |
| `cp_screens_keys-a_answer_focus_correct.png` | Keys: Down x1 from the answer box highlights Correct |
| `cp_screens_keys-b_incorrect_focus_continue.png` | Keys: Down x1 after Incorrect highlights Continue |
| `cp_screens_keys-b_prompt3_focus_skip_checkpoint.png` | Keys: Skip checkpoint is last; a further Down does not wrap |
| `cp_long_answer-touch_part1.png`, `-touch_part2.png`, `-touch_part3.png` | The Paine book's longest answer (`c0038-p1`, 521 characters, 23 lines) in three parts with the counter "1/3", "2/3", "3/3" |

## State files written

`<scenario>.rsvp-state.json`: the book's state file at the end of each scenario. The `at` times are
the simulator's clock (the host's), which the scenarios check for format only.

- `cp_screens_touch` (the same grades for `cp_screens_keys`): `c0001-p1` correct; `c0002-p1` incorrect
  (two later answers did not overwrite it); `c0002-p2` incorrect; `c0002-p3`, only ever skipped, has
  no grade. The last run ended with "Reread in page view", so the checkpoint is in progress with its
  `reread` place (chapter 2, word 311, the section's first word) and `resume` in RSVP.
- `cp_skip_grades`: after four runs, each ended by SIGKILL right after the next screen appeared (the
  last by Skip checkpoint): `c0002-p1` incorrect (a later Correct did not overwrite it), `c0002-p2`
  correct (skipped in run 1, graded in run 3), `c0002-p3` never graded, nothing in progress, and the
  reading position after the section's break.
- `cp_long_answer`: paging and skipping grade nothing.

## Log lines

`<scenario>.log.txt`: the `[CPT]` and `[CTL]` lines of each scenario (format in
`src/activities/checkpoint/README.md`, "Log lines"). Each `saved ... ok=1` line comes before the
`screen=` line of the screen it leads to.
