# src/controls

**ControlMap** (`docs/PLAN.md` section 4; contract: `docs/spikes/S4-control-map.md`, section 7): the
single binding table for the RSVP, checkpoint, reread and device-test screens, and the input adapter
that turns the firmware's key and touch events into control events. Filled by task R4a (#21); the
checkpoint (C3, C4) and device-test (D3a) screens add their screens and zones to the same table.

| File | What it holds | Host-tested |
| --- | --- | --- |
| `ControlMap.h/.cpp` | Key roles (`KEYS`), timings (hold 0.7 s, repeat 0.4 s, arming 0.3 s), events, actions and contexts, `BINDINGS` and `resolve()`, screens with their items in focus order and touch zones (`PAUSE`), `hit()` | yes |
| `ControlGesture.h/.cpp` | `GestureReader`: one frame of input to one event (short press on release, hold, hold repeat, Confirm, tap, Back; the arming guard of a new screen) | yes |
| `ControlInput.h/.cpp` | The adapter: the only control code that reads `MappedInputManager` | no (simulator scenarios) |
| `ControlDefaults.h` | Seeds "Page turn long press" from `KEYS` before the settings load (called from `src/main.cpp`) | |

Host tests: `test/rsvp_activity` (`ControlMapTest`, `ControlGestureTest`).

## The key scheme (S4 section 5.1)

- A short press of a side key, released before 0.7 s, moves the highlight on release: the `prev` key
  to the previous item, the `next` key to the next one. It never activates. No wrap.
- Holding either side key 0.7 s activates the highlighted item. On repeating items (the sentence
  steps) the activation repeats every 0.4 s while the key stays down. `Confirm` (GPIO5, role
  unconfirmed) activates on release while `KEYS.confirmSelects` is true.
- A new screen ignores input until it has been shown for 0.3 s and every key and finger is up.
- During RSVP playback the first key press or touch-down pauses; the adapter suppresses the rest of
  that touch contact.
- Touch is explicit: a tap on a zone activates its item.

When a hold activates, the adapter hands the held key to `MappedInputManager`'s long-press bookkeeping
(`wasLongPressed(button, 0)`), so its release is swallowed and the next screen (the reader, after
"Page view") never sees that key as a new long press or page turn.

## Swapped side keys

`KEYS.prev` and `KEYS.next` are `PageBack` and `PageForward`, which follow Settings > Controls > Side
button layout: switching it to Next/Prev on the device fixes page view and these screens together.
When that layout gives no prev/next pair, the fallbacks apply, and swapping
`LogicalButton::Up, LogicalButton::Down` in the `KEYS` line is the durable one-line fix
(`ControlMapTest.SwappingTheSideKeysIsOneLineOfTheTable`).

Everything here is compiled only with the RSVP feature flag (`docs/ARCHITECTURE-RSVP.md`).
