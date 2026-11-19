# Evidence: tasks C2 (#26) and C3 (#27), checkpoint triggers and the checkpoint flow end to end

Copied from one simulator run of all scenarios (`python3 -m tools.simtest run --all`, all passed).
`<scenario>.log.txt` keeps the `[CPT]`, `[CTL]` and `[RSVP]` enter/play/pause/leave/chapter lines,
the frames around the section ends and the reader's `Progress saved` lines;
`<scenario>.rsvp-state.json` is the book's state file at the end of the scenario. The scenarios' own
reference images are in `tools/simtest/scenarios/cp_*/refs/`.

## Screens

| File | Screen |
| --- | --- |
| `cp_fire_rsvp-t_last_word.png` | RSVP: the last frame, word 232 "same.", the end of s0001 (nothing after it is drawn) |
| `cp_fire_rsvp-t_prompt_after_last_word.png` | c0001's prompt, which follows that frame |
| `cp_fire_rsvp-t_rsvp_resume.png` | RSVP's pause screen after Skip checkpoint: word 233 "Winding the Clockwork ...", Play highlighted, "Checkpoint done" |
| `cp_fire_rsvp-k_last_group.png` | 2 words per update: the last group "the lamp." ends at word 607, the end of s0004 and of chapter 2 |
| `cp_fire_rsvp-k_prompt_after_last_group.png` | c0002's first prompt after it |
| `cp_fire_rsvp-k_rsvp_resume_chapter.png` | RSVP's pause screen at chapter 3, word 0 (the section ended its chapter) |
| `cp_fire_page-t_prompt_c0001.png` | Page view: c0001's prompt, shown on the next-page action from page 3 of chapter 1 |
| `cp_fire_page-t_page_resume.png` | Page view after the checkpoint: page 3 again (it holds word 233), waiting |
| `cp_fire_page-t_prompt_c0002.png` | c0002's prompt on leaving chapter 2's last page |
| `cp_fire_page-t_page_resume_chapter.png` | Page view after it: chapter 3's first page |
| `cp_multi_prompt-t_rsvp_resume.png`, `-k_page_resume.png` | The three-prompt checkpoint finished in RSVP (pause screen, chapter 3) and in page view (chapter 3's first page) |
| `cp_paine_rsvp-prompt.png`, `-rsvp_resume.png` | The Paine book: c0001 after Common Sense's first section (word 432) in RSVP, and the pause screen at word 433 |

## State files

- `cp_single_prompt`: `c0001-p1` correct (the later Incorrect did not change it), position 1:233 rsvp.
- `cp_multi_prompt`: `c0002-p1` correct, `-p2` incorrect, `-p3` correct, kept through a second pass
  that answered differently; position 3:0 page.
- `cp_skip_prompt`: `c0002-p1` incorrect, `-p3` correct; `-p2`, skipped in both passes, has no grade.
- `cp_skip_checkpoint`: only `c0002-p1` (correct); the checkpoint was left at prompt 2, then at prompt
  1 on the second pass; no checkpoint in progress.
- `cp_screens_touch`, `cp_screens_keys`: the same grades as C3a's run; since rereads are task C4, the
  reread requests resume after the break, so no checkpoint is in progress and the position is 3:0 rsvp.
- `cp_ordinary_book`: `fixture.epub`'s file has only `version`, `overrides` and `position`.
- `cp_toc_jump_no_fire`: the scenario ends with c0001's prompt on screen: the checkpoint in progress
  and the position before the break (1:184 page, the first word of the page that was left).
