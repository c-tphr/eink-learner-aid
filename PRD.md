# v1 PRD — RSVP and retrieval practice on BOOX Picco

**Product goal.** Build a reader for the BOOX Picco, running CrossPoint or a fork, that lets a reader move through longform text in RSVP or normal page view, stop at meaningful breaks to recall what they read, and revisit the relevant passage without losing their place. The first usable version includes a workflow for preparing books and checking their checkpoints before loading them onto the Picco.

## Reading experience

The reader opens a prepared reading file. The original EPUB remains a separate, unchanged file. The prepared file contains the reading text, section boundaries, and checkpoints with their prompts and reference answers. Each checkpoint links to one section. All prompts at that checkpoint share the same section link.

- **RSVP.** Default to **240 words per minute and one word per display update**. While paused, the reader can change both the target WPM and the number of words shown at a time. Settings can be saved as global defaults or as overrides for the current book.
- **Display performance.** When the Picco cannot keep up with the selected pace, slow playback so every word is presented in order and has time to become readable. Keep the selected WPM as the reader’s preference. Show **actual WPM** on the RSVP pause screen. Actual WPM reflects the delivered pace during active playback, including display delays and excluding time spent paused or answering checkpoints.
- **Checkpoint arrival.** Trigger a checkpoint when the reader reaches its embedded section break through ordinary forward reading in either mode. In RSVP, stop before displaying text beyond the break. In normal view, show the checkpoint when the reader advances past the section. A jump through the table of contents or another navigation control does not trigger checkpoints passed along the way. Reading through a section again triggers its checkpoint again.

## Checkpoint and reread flow

1. **Present one prompt at a time.** A checkpoint has one or more prompts, in the order supplied by the file. The answer starts hidden. The reader recalls the answer privately, then chooses **Reveal answer** when ready. There is no typed response.
2. **Let the reader self-grade.** After revealing the reference answer, the reader marks the attempt **Correct** or **Incorrect**. A correct response advances to the next prompt, if any. After an incorrect response, the reader can reread the linked section or continue.
3. **Support rereading in either mode.** Rereading starts at the beginning of the linked section in the reader’s choice of RSVP or normal text. Reaching the end of the section returns to the same prompt with its answer hidden. **Return early** does the same at any point in the reread. Back at the prompt, the reader can retry or continue. Earlier prompts and their grades are preserved.
4. **Allow skipping at any point in the checkpoint.** **Skip prompt** advances past the current prompt. **Skip checkpoint** exits the whole checkpoint, including when some prompts have already been answered. Neither action erases saved grades or assigns a grade to an unanswered prompt.
5. **Resume after the original break.** When the checkpoint is finished or skipped, restore the mode in use when it appeared and place the reader immediately after that break. RSVP waits for **Play**. Normal view waits for the reader’s next action. A reread does not change this return position.

## Saved state and grades

- Save **only the first grade per prompt** across retries, rereads, and reading sessions. If the reader first marks a prompt **Incorrect** and then answers correctly after rereading, the saved grade remains **Incorrect**. The retry still lets the reader finish the prompt.
- A skipped prompt stays **ungraded** until the reader grades it for the first time. On a later pass, the checkpoint presents its prompts again and preserves any grades already saved.
- If the reader closes and reopens the book during a checkpoint, restore the same prompt with its answer hidden. If they leave during a reread, restore the reread position and mode, with the route back to the original prompt intact. Either way, preserve where regular reading should resume afterward.

## Preparing and validating a book

1. **Draft.** An AI session uses the original EPUB and the preparation instructions to propose natural section breaks and draft one or more prompts and reference answers at each checkpoint. Prompt wording and recall tasks can vary by section.
2. **Check in a fresh session.** A second AI session checks the prepared data against the source and instructions. It verifies that the structure is complete and internally consistent, each checkpoint resolves to its intended section, and each reference answer is supported by that section. It also checks that reference answers state the essential ideas clearly enough for the reader to accept correct paraphrases.
3. **Revise until the check passes.** The checker flags issues for revision. Revised data goes through the check again before it is treated as ready.
4. **Deliver.** Produce the prepared reading file and a validation report. The report records the checks and their final status. The original EPUB stays available separately. A preview or editing interface is outside v1.

## v1 acceptance criteria

- A prepared test book includes both a single-prompt checkpoint and a multi-prompt checkpoint. Reading into either checkpoint triggers it in both reading modes. Jumping past it does not.
- A reader can mark the second prompt **Incorrect**, reread its section in either mode, return early or at the end, and retry with the answer hidden. A later **Correct** leaves the original **Incorrect** saved and preserves the grade on the first prompt.
- A reader can skip one prompt or exit a partially completed checkpoint. Unanswered prompts stay ungraded, saved grades survive, and reading resumes after the break in the original mode without auto-advancing.
- Closing and reopening the book restores an interrupted prompt or reread with the correct return position. Rereading the book later shows checkpoints again with first grades intact.
- On a physical Picco, test the default pace and a target above what the display can deliver cleanly. Verify that playback slows without dropping words and that the pause screen shows the delivered WPM.
- The preparation workflow produces a ready file and report only after the checker passes the draft. A broken section link or an unsupported answer is flagged and sent back for revision.

**Later scope.** On-device review of missed prompts and spaced repetition on the Picco are future features. This PRD leaves the file format, firmware base, display strategy, and persistence design to the technical implementation plan.