# src/activities/checkpoint

**CheckpointActivity** (`docs/PLAN.md` sections 4 and 7): shows a checkpoint's prompts one at a
time, reveals the reference answer, records the reader's own grade, offers the reread choice after
Incorrect and the two skip actions. It drives `checkpoint::CheckpointFlow` from `lib/CheckpointCore`
and never decides a flow rule itself. Layouts and controls follow `docs/spikes/S4-control-map.md`
(section 2 and rows 17-29) through the binding table in `src/controls/`.

Built by task C3a (#93) as a self-contained activity: a caller pushes it with "this checkpoint of
this book" and reads how it ended. C2 (#26) decides when a checkpoint fires; C3 (#27) connects the
two and removes the temporary simulator entry below; C4 (#28) does the reread itself.

Everything here is compiled only with the RSVP feature flag (`docs/ARCHITECTURE-RSVP.md`).

| File | What it holds | Host-tested |
| --- | --- | --- |
| `CheckpointLaunch.h` | The entry point: `checkpointentry::create()` with a `Request` (a Fire step of the trigger rules and the mode in use) and the shared `Outcome` it fills | |
| `CheckpointActivity.h/.cpp` | The activity: input, flow events, state file writes, drawing | no (simulator scenarios) |
| `CheckpointScreens.h/.cpp` | Pure: flow state to screen, items and default highlight per screen, item to flow event, answer paging, how a checkpoint ended | yes |
| `CheckpointSave.h/.cpp` | Pure: what a transition writes to the state file, the grade time | yes |
| `CheckpointSimEntry.h/.cpp` | TEMPORARY simulator-only entry (below) | |

Host tests: `test/checkpoint_activity` (`CheckpointScreensTest`, `CheckpointSaveTest`).

## Calling it

```cpp
#include "activities/checkpoint/CheckpointLaunch.h"

// `book`: reading::CheckpointBook::open(epub), kept while the book is open (src/reading/README.md).
// `fire`: a checkpoint::Step of kind Fire from rsvpStep(), pageAdvance() or evaluateMove().
checkpointentry::Request request{fire, checkpoint::ReadingMode::Rsvp};
auto outcome = std::make_shared<checkpointentry::Outcome>();
startActivityForResult(checkpointentry::create(renderer, mappedInput, epub->getPath(), book, request, outcome),
                       [outcome](const ActivityResult&) { /* act on *outcome */ });
```

`Outcome` once the activity has finished:

| `end` | Meaning | Where reading goes on |
| --- | --- | --- |
| `Finished` | The last prompt was graded Correct, or continued or skipped | `resume`: the first word after the break, in `Request::mode` (the state file's `position` is already that) |
| `Skipped` | Skip checkpoint | `resume`, as above |
| `RereadRsvp`, `RereadPage` | Reread chosen after Incorrect at prompt `promptIndex` | `reread`: the section's first word in the chosen mode. The state file holds the checkpoint with `reread`; task C4 rereads, then calls `CheckpointFlow::rereadMoved()` / `handle(ReturnEarly)` and shows the prompt again |
| `Failed` | Could not start: no memory for the session, or the flow refused the request (a step that is not Fire, a checkpoint without prompts) | Unchanged |
| `None` | Left another way (Home gesture, sleep). Every transition is already in the state file | |

`saved` is false if any state file write failed (logged); the screens carry on regardless.

## Screens

Portrait 480x800, black and white. Every screen has the key hint "Up/Down: move · hold: select" at the
bottom. Zones are in `controls::CHECKPOINT_PROMPT`, `CHECKPOINT_ANSWER` and `CHECKPOINT_INCORRECT`.

| Screen (flow state) | Content | Items in focus order (**default**) |
| --- | --- | --- |
| Prompt (PromptHidden) | "Checkpoint · question 2 of 3"; "Recorded earlier: Incorrect" when the prompt has a first grade from an earlier pass; the whole prompt | **Reveal answer**, Skip question, Skip checkpoint |
| Answer (AnswerShown) | The header (on a later pass "Question 2 of 3 · recorded: Incorrect"), the prompt cut to two lines with an ellipsis, the reference answer in a box | **Answer box**, Correct, Incorrect, Skip question, Skip checkpoint |
| After Incorrect (IncorrectChoice) | "Question 2 of 3 · marked Incorrect", "Reread the section, then try again.", the prompt in two lines | Reread in RSVP, Reread in page view (**the one for the mode in use**), Continue, Skip question, Skip checkpoint |

Skip checkpoint is always the last item although it sits at the bottom left. An item is activated
only when `CheckpointFlow::accepts()` allows its event in the current state.

**Text.** Prompts and answers use the reader's NotoSans 16. A prompt that does not fit the space
above Reveal in that font is drawn in NotoSans 14, then UI 12; a valid prompt (300 characters at
most) fits that, and if it ever did not, the cut is marked with an ellipsis and logged. Text wraps at
spaces and at the line feeds the file may contain (`GfxRenderer::wrappedText()` per paragraph).

**A long answer pages** (S4 row 20). The box holds 8 lines of NotoSans 16; an answer that needs more
is split into parts of whole lines, with a counter "2/3" in the box's top right. Nothing is cut:
there are as many parts as the lines need (the Paine book's longest answer, 521 characters, wraps to
23 lines: 3 parts). A tap on the box, or a hold of either side key while
the box is highlighted, shows the next part and wraps to the first after the last; a swipe up inside
the box shows the next part and a swipe down the previous one, stopping at the ends. A one-part
answer ignores all of these. The highlighted box has a 6 px border instead of being filled, so its
text stays readable.

**Input.** The key scheme of `src/controls/README.md`: a short press of Up or Down moves the
highlight on release, without wrapping; a 0.7 s hold of either key activates; Confirm activates when
the table allows it. Every new screen ignores input for 300 ms and until every key and finger is up
(`[CTL] <Screen> armed`). The Back gesture is ignored, so a swipe meant as "previous page" cannot
skip (S4 section 6); the Home gesture leaves (everything is saved).

**Refreshes** go through the display backend: `clear()` when a new screen appears, `update()` for a
highlight move or an answer part.

## Saved state

Every transition the flow marks `Save::Now` is written through `reading::editBookState()` before the
next screen is drawn (`CheckpointSave.h`):

- start: `active_checkpoint` (id, `prompt_index` 0, `resume`); `book_sha256` when the file has none;
- Correct or Incorrect that stored the prompt's first grade: that grade, recorded only if the file
  has none for the prompt (a grade the file already holds is never overwritten);
- Skip question, Continue, the next prompt: `prompt_index`; skips write no grade;
- the end (finished or skipped): `active_checkpoint` removed, `position` set to `resume`;
- a reread request: `active_checkpoint` with `reread` (the section start, in the chosen mode).

Reveal writes nothing; neither does an Incorrect for a prompt that already has a first grade.
Overrides and the grades of other prompts stay as the file has them.

**Grade time.** The `at` of a grade is the RTC's local time (`halClock.localTime()`), written only
when the clock is set: a year from 2020 to 2135 (`formatGradeTime()`). Otherwise, for example on a
device whose clock was never set, the grade is written without `at`, as the codec allows. The
simulator's clock is the host's, so its scenarios check the format of `at`, not its value.

**What a restart sees.** The state file always holds the checkpoint in progress at its current
prompt (or its reread). Restoring it on reopening is task C5: until then a reopened book reads as
usual and the next run of the checkpoint starts again at its first prompt.

## Log lines

DBG lines, tag `CPT` unless noted; stable for the scenarios. Numbers are decimal.

| Line | When |
| --- | --- |
| `open checkpoint=<id> prompts=<n> section=<id> mode=<rsvp\|page> resume_ch=<c> resume_word=<w> state=<missing\|read\|invalid\|unreadable\|no-memory> grades=<n> activity_bytes=<b> session_bytes=<b> accepted=<0\|1>` | The activity started the flow |
| `saved after=<start\|reveal\|correct\|incorrect\|skip-prompt\|skip-checkpoint\|reread-rsvp\|reread-page\|continue> prompt=<id that got its first grade\|-> grade=<correct\|incorrect\|none> active=<id\|none> prompt_index=<i> position=<ch:word:mode\|none> ok=<0\|1>` | The state file was written (always before the next `screen` line) |
| `event=<flow event> prompt=<id> accepted=<0\|1> refusal=<n> state=<flow state> grade_recorded=<0\|1> save=<no\|position\|now>` | An item was activated |
| `screen=<Prompt\|Answer\|Incorrect> question=<i>/<n> prompt=<id> recorded=<none\|correct\|incorrect> focus=<Item> part=<p>/<parts> lines=<answer lines> refresh=<clear\|update>` | After every refresh |
| `answer part=<p>/<parts> move=<cycle\|swipe-next\|swipe-previous> changed=<0\|1>` | A paging gesture on the answer box |
| `end result=<finished\|skipped\|reread-rsvp\|reread-page\|failed> prompt=<i> ch=<c> word=<w> mode=<rsvp\|page> saved=<0\|1>` | The activity finishes; the place is `resume` or `reread` |
| `left checkpoint=<n> prompt=<i> state=<flow state> (saved)` | Left without an ending (Home gesture, sleep) |
| `[CTL] <Screen> armed`, `[CTL] <Screen> focus=<Item>`, `[CTL] <Screen> select=<Item>` | The input guard released; highlight moved; item activated |
| `sim entry checkpoint=<id> mode=<m> ch=<spine> start=<w> end=<w>` | The temporary entry pushes the activity |

## Memory

| What | Bytes | When |
| --- | --- | --- |
| The activity object | 4,016 (simulator, 64-bit) | While shown. Mostly the prompt (1,201), answer (2,401) and prompt id (65) buffers, read once per prompt so that drawing never reads the card |
| `Session`: `BookState` and `CheckpointFlow` | 78,056 (simulator) | While shown; one heap block (PSRAM on the Picco). The flow needs a `BookState`; it holds the grades of the whole book |
| `editBookState()` | about 78 KB plus the file's size | During each write (`src/reading/BookStateFile.h`) |
| Wrapped lines | a vector of short strings, about 1 KB for a long answer | During one paint, freed when it ends; paints happen on input only |
| `reading::CheckpointBook` | 72,544 (simulator) | Owned by the caller while the book is open (`src/reading/README.md`) |

Nothing is allocated per frame or per key press besides the paint's line vector.

## TEMPORARY: the simulator entry

`CheckpointSimEntry.h`: with `CROSSPOINT_SIM_CHECKPOINT=<checkpoint id>[,rsvp|page]` (several
separated by `;`), once an EPUB with a page laid out is in front, the simulator opens the book's
checkpoint data, resolves the checkpoint's section through the chapter's word index, makes the Fire
step that reading past the section's last word makes (`checkpoint::rsvpStep()`), and pushes the
activity over the reader. Several entries open one after another, each once the reader is back in
front, because the scenario harness sets one value per scenario. It exists only in simulator builds
with the RSVP flag (an empty inline function everywhere else) and has one call in `src/main.cpp`,
after the display self-test's. **Task C3 (#27) removes it and that call** when it wires the activity
to C2's triggers. Scenarios: `cp_screens_touch`, `cp_screens_keys`, `cp_long_answer`,
`cp_skip_grades`.
