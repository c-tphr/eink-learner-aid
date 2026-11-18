# src/controls

**ControlMap** (`docs/PLAN.md` section 4; contract: `docs/spikes/S4-control-map.md`, section 7): the
single binding table for the RSVP, checkpoint, reread and device-test screens, and the input adapter
that turns the firmware's key and touch events into control events. Filled by task R4a (#21); R4b
(#22) added the pause screen's steppers and save buttons (S4 rows 6-11); C3a (#93) the three
checkpoint screens (S4 rows 17-29) and the answer box's vertical swipe; the reread (C4) and
device-test (D3a) screens add their screens and zones to the same table.

| File | What it holds | Host-tested |
| --- | --- | --- |
| `ControlMap.h/.cpp` | Key roles (`KEYS`), timings (hold 0.7 s, repeat 0.4 s, arming 0.3 s), events, actions and contexts, `BINDINGS` and `resolve()`, screens with their items in focus order and touch zones (`PAUSE`; `BANNER`, the non-prose banner's Continue and Stay, task R5; `CHECKPOINT_PROMPT`, `CHECKPOINT_ANSWER`, `CHECKPOINT_INCORRECT`), `hit()` | yes |
| `ControlGesture.h/.cpp` | `GestureReader`: one frame of input to one event (short press on release, hold, hold repeat, Confirm, tap, Back, vertical swipe with its start; the arming guard of a new screen) | yes |
| `ControlInput.h/.cpp` | The adapter: the only control code that reads `MappedInputManager` | no (simulator scenarios) |
| `ControlDefaults.h` | Seeds "Page turn long press" from `KEYS` before the settings load (called from `src/main.cpp`) | |

Host tests: `test/rsvp_activity` (`ControlMapTest`, `ControlGestureTest`); the checkpoint screens' rows
and the vertical swipe in `test/checkpoint_activity` (`CheckpointScreensTest`).

## The key scheme (S4 section 5.1)

- A short press of a side key, released before 0.7 s, moves the highlight on release: the `prev` key
  to the previous item, the `next` key to the next one. It never activates. No wrap.
- Holding either side key 0.7 s activates the highlighted item. On repeating items (the sentence
  steps and the four steppers) the activation repeats every 0.4 s while the key stays down. `Confirm` (GPIO5, role
  unconfirmed) activates on release while `KEYS.confirmSelects` is true.
- A new screen ignores input until it has been shown for 0.3 s and every key and finger is up.
- During RSVP playback the first key press or touch-down pauses; the adapter suppresses the rest of
  that touch contact.
- Touch is explicit: a tap on a zone activates its item. The one hidden gesture is the checkpoint
  answer box's own swipe (S4 row 20): a swipe up or down that starts inside the box (`Ev::SwipeUp`,
  `Ev::SwipeDown`, resolved to `Act::SwipeNext`, `Act::SwipePrev`) shows the next or previous part
  of a long answer. Other screens ignore these actions.

## The checkpoint screens (S4 section 2)

| Screen | Items in focus order (default first unless noted) | Zones |
| --- | --- | --- |
| `CHECKPOINT_PROMPT` | Reveal, SkipPrompt, SkipCheckpoint | 16,416,448,120; 264,680,200,64; 16,680,200,64 |
| `CHECKPOINT_ANSWER` | AnswerBox, Correct, Incorrect, SkipPrompt, SkipCheckpoint | 16,120,448,440; 16,576,208,88; 256,576,208,88; then as above |
| `CHECKPOINT_INCORRECT` | RereadRsvp, RereadPage, ContinueOn ("Continue"), SkipPrompt, SkipCheckpoint. The default is the reread in the mode in use, which the activity picks (`checkpointui::defaultFocus()`) | 16,216,448,80; 16,308,448,80; 16,400,448,80; then as above |

No checkpoint item repeats while held. Skip checkpoint is last in focus order on every screen.

When a hold activates, the adapter hands the held key to `MappedInputManager`'s long-press bookkeeping
(`wasLongPressed(button, 0)`), so its release is swallowed and the next screen (the reader, after
"Page view") never sees that key as a new long press or page turn.

## Device test screens (task D3a)

`ControlMap.h` holds their zones too: `CHOICE_ZONES` (eight rows of 448x72 from y=72, 8 px apart, S4
row 34) with `choiceScreen(count, default)`, which `src/activities/devicetest/ChoiceList.h` uses for
every device-test list (task D3b reuses it), and `CONTROLS_CHECK` (the Controls check's Finish button,
16,672,448,72). Their items are `ChoiceRow0` to `ChoiceRow7` and `ControlsFinish`. The lists read input
through `ControlInput::pollFocus()` like the pause screen, and a running benchmark series through
`pollPlaying()` (any key or touch stops it). The Controls check alone reads the raw `HalGPIO` indices,
to record them (S4 section 7).

## Swapped side keys

`KEYS.prev` and `KEYS.next` are `PageBack` and `PageForward`, which follow Settings > Controls > Side
button layout: switching it to Next/Prev on the device fixes page view and these screens together.
When that layout gives no prev/next pair, the fallbacks apply, and swapping
`LogicalButton::Up, LogicalButton::Down` in the `KEYS` line is the durable one-line fix
(`ControlMapTest.SwappingTheSideKeysIsOneLineOfTheTable`).

Everything here is compiled only with the RSVP feature flag (`docs/ARCHITECTURE-RSVP.md`).
