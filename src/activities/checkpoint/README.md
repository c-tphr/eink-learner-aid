# src/activities/checkpoint

**CheckpointActivity** (`docs/PLAN.md` sections 4 and 7): shows a checkpoint's prompts one at a
time, reveals the reference answer, records the reader's own grade, offers the reread choice after
Incorrect and the two skip actions. It drives `checkpoint::CheckpointFlow` from `lib/CheckpointCore`
and never decides a flow rule itself. Layouts and controls follow `docs/spikes/S4-control-map.md`
(section 2 and rows 17-29) through the binding table in `src/controls/`.

Built by task C3a (#93) as a self-contained activity: a caller pushes it with "this checkpoint of
this book" and reads how it ended. C2 (#26) decides when a checkpoint fires and C3 (#27) connects the
two (`reading::ReadingSession::startCheckpoint()`, from RSVP and from the reader's next-page action;
`src/reading/README.md`, "ReadingSession"); C4 (#28) does the reread itself: the flow and its state
moved into the reading session, the activity opens again on them for the prompt after a reread, and
`RereadScreens.cpp` draws the page-view reread bar and its list (`src/reading/README.md`, "Rereading").

Everything here is compiled only with the RSVP feature flag (`docs/ARCHITECTURE-RSVP.md`).

| File | What it holds | Host-tested |
| --- | --- | --- |
| `CheckpointLaunch.h` | The entry point: `checkpointentry::create()` with a `Request` (a Fire step of the trigger rules, the mode in use, and the session's `Progress`: the flow and its state) and the shared `Outcome` it fills; `saveTransition()`, the state file write of a transition, which the session also uses during a reread; the reread bar and list (task C4) | |
| `CheckpointActivity.h/.cpp` | The activity: input, flow events, state file writes, drawing | no (simulator scenarios) |
| `CheckpointScreens.h/.cpp` | Pure: flow state to screen, items and default highlight per screen, item to flow event, answer paging, how a checkpoint ended | yes |
| `CheckpointSave.h/.cpp` | Pure: what a transition writes to the state file, the grade time | yes |
| `RereadScreens.cpp` | The page-view reread bar (drawn over the page, S4 row 31) and the list a side-key hold opens during a reread (task C4) | no (simulator scenarios) |

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
| `RereadRsvp`, `RereadPage` | Reread chosen after Incorrect at prompt `promptIndex` | `reread`: the section's first word in the chosen mode. The state file holds the checkpoint with `reread`; the session rereads (`ReadingSession::startReread()`), and when the reread ends a new activity opens on the same flow at that prompt (task C4) |
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

- start: `active_checkpoint` (id, `prompt_index` 0, `resume`); `position`, the place before the break
  (`Request::before`, task C2); `book_sha256` when the file has none;
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

**Back at the prompt after a reread** (task C4). The caller passes the session's flow in PromptHidden:
the activity reads and writes nothing first (the session saved the return), logs `back`, and shows the
prompt with its answer hidden and, since the prompt has its first grade, "Recorded earlier: Incorrect".
Reveal is the retry and Skip question is Continue (S4 row 32).

**What a restart sees.** The state file always holds the checkpoint in progress at its current
prompt (or its reread), and `position` before the break. Restoring it on reopening is task C5: until
then a reopened book opens before the break (the reader's page, or RSVP at the frame that was on
screen), and reading on fires the checkpoint again from its first prompt (`cp_skip_grades`).

## Log lines

DBG lines, tag `CPT` unless noted; stable for the scenarios. Numbers are decimal.

| Line | When |
| --- | --- |
| `open checkpoint=<id> prompts=<n> section=<id> mode=<rsvp\|page> resume_ch=<c> resume_word=<w> state=<missing\|read\|invalid\|unreadable\|no-memory> grades=<n> activity_bytes=<b> session_bytes=<b> accepted=<0\|1>` | The activity started the flow |
| `saved after=<start\|reveal\|correct\|incorrect\|skip-prompt\|skip-checkpoint\|reread-rsvp\|reread-page\|continue\|reread-moved\|reread-reached-end\|return-early\|reread-left-section> prompt=<id that got its first grade\|-> grade=<correct\|incorrect\|none> active=<id\|none> prompt_index=<i> position=<ch:word:mode\|none> ok=<0\|1> reread=<ch:word:mode\|none>` | The state file was written (always before the next `screen` line). The last four reasons are the session's writes during a reread (`saveTransition()`) |
| `back checkpoint=<id> prompt=<i>/<n> mode=<rsvp\|page> activity_bytes=<b>` | The activity opened on a flow that went back to its prompt after a reread |
| `event=<flow event> prompt=<id> accepted=<0\|1> refusal=<n> state=<flow state> grade_recorded=<0\|1> save=<no\|position\|now>` | An item was activated |
| `screen=<Prompt\|Answer\|Incorrect> question=<i>/<n> prompt=<id> recorded=<none\|correct\|incorrect> focus=<Item> part=<p>/<parts> lines=<answer lines> refresh=<clear\|update>` | After every refresh |
| `answer part=<p>/<parts> move=<cycle\|swipe-next\|swipe-previous> changed=<0\|1>` | A paging gesture on the answer box |
| `end result=<finished\|skipped\|reread-rsvp\|reread-page\|failed> prompt=<i> ch=<c> word=<w> mode=<rsvp\|page> saved=<0\|1>` | The activity finishes; the place is `resume` or `reread` |
| `left checkpoint=<n> prompt=<i> state=<flow state> (saved)` | Left without an ending (Home gesture, sleep) |
| `[CTL] <Screen> armed`, `[CTL] <Screen> focus=<Item>`, `[CTL] <Screen> select=<Item>` | The input guard released; highlight moved; item activated |

## Memory

| What | Bytes | When |
| --- | --- | --- |
| The activity object | 4,056 (simulator, 64-bit; 4,016 before task C4) | While shown. Mostly the prompt (1,201), answer (2,401) and prompt id (65) buffers, read once per prompt so that drawing never reads the card |
| `checkpointentry::Progress`: `BookState` and `CheckpointFlow` | 78,056 (simulator) | Allocated by `ReadingSession::startCheckpoint()` and kept until the checkpoint ends, a reread included (task C4; before it the activity allocated the same block while it showed). One heap block (PSRAM on the Picco). The flow needs a `BookState`; it holds the grades of the whole book. Logged as `session_bytes` in the `open` line |
| The reread list | one small activity per hold | While the list shows |
| `editBookState()` | about 78 KB plus the file's size | During each write (`src/reading/BookStateFile.h`) |
| Wrapped lines | a vector of short strings, about 1 KB for a long answer | During one paint, freed when it ends; paints happen on input only |
| `reading::CheckpointBook` | 72,544 (simulator) | Owned by the caller while the book is open (`src/reading/README.md`) |

Nothing is allocated per frame or per key press besides the paint's line vector.

## Opened by the triggers

`ReadingSession::startCheckpoint()` makes the activity when ordinary forward reading fires a checkpoint
(RSVP pushes it over itself; page view over the reader), with `Request::before`, the reading position
before the break, which the start's write saves as `position` together with the checkpoint in progress.
A book closed while the checkpoint shows therefore reopens before the break, where reading on fires it
again (restoring the prompt itself is task C5). C3a's temporary simulator entry
(`CheckpointSimEntry`, `CROSSPOINT_SIM_CHECKPOINT`) is gone; its scenarios `cp_screens_touch`,
`cp_screens_keys`, `cp_long_answer` and `cp_skip_grades` reach their checkpoints by reading. Every
read and write of the state file uses the book's identity (`book_sha256` and the open index), so the
rule for another edition (`checkpoint::applyBookChangeRule()`) applies as the file is read.
