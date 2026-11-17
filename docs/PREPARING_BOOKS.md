# Preparing a book

A prepared book is a copy of an EPUB with checkpoints in it: at the end of each section the reader stops and recalls what they just read. This page tells you how to make one with `/prep-book`, what the result means, and what to do next. The technical details are in `tools/prep/README.md`; the rules the AI follows are in `tools/prep/INSTRUCTIONS.md`.

## Run it

1. Open Claude Code in this repository (a cloud session, a Codespace, or a local clone with Python 3 and Java installed). The first time, install the Python packages: `pip install -r tools/prep/requirements.txt`.
2. Type:

   ```text
   /prep-book path/to/Book.epub
   ```

   or, to choose where the results go:

   ```text
   /prep-book path/to/Book.epub path/to/output-folder
   ```

   Without an output folder, the results go into a new folder `Book-prepared` next to the EPUB. Use a new output folder for every run.
3. Approve the file writes Claude Code asks about (the draft). Then wait. You do not need to answer anything else. The checker writes its result file itself; its guard allows that one file without asking you.

What happens:

- A script reads the book into numbered paragraphs.
- Claude (the **drafter**) chooses the sections and writes the prompts and reference answers.
- Scripts build the prepared EPUB and check its structure (anchors, word counts, nothing else changed, EPUBCheck).
- A separate AI (the **checker**), which sees only the book text, the instructions and the prompts, checks that every answer is supported by its own section, is clear enough to grade, and that no prompt gives its answer away.
- If anything fails, the drafter gets only the list of problems and fixes them. This repeats at most three times.
- A script writes the report and decides READY or NOT READY. Claude never decides that itself.

Your original EPUB is only read; it is never changed.

**How much work it is.** The first real book was Thomas Paine's *Essays* (52,786 words in 12 documents; `docs/evidence/p5/README.md`). It took 2 rounds and ended READY with 55 sections, 55 checkpoints and 130 prompts. What was measured:

| | Round 1 | Round 2 |
| --- | --- | --- |
| Book text the drafter read | 116 KB (*Common Sense* only) | 187 KB (the six other essays) |
| Draft written | 41 KB, 58 prompts | 92 KB, 130 prompts |
| Checker folder read | 588 KB | somewhat more (32 more section files) |
| Checker's result | 75 KB | 160 KB |

The extracted text of the whole book is 545 KB. In total the drafter read about 310 KB of book text and wrote about 130 KB of draft. The scripts are quick: `extract` about 2 seconds, `validate` and `report` about 5 seconds each with EPUBCheck.

**How long it takes was not measured.** The run's clock times are not meaningful: the session pauses between turns, and the drafter sat idle for hours while the checker was run separately, so file times span hours of waiting. A run takes longer the longer the book is, because each round reads the book once for drafting and once for checking. A book of more than 60,000 words is drafted one document at a time (`tools/prep/INSTRUCTIONS.md` section 2), so that the drafter never has to hold the whole book at once.

## READY or NOT READY

The first line of the report says which.

- **READY**: the validator and the checker both passed exactly this prepared file. It is safe to load onto the device.
- **NOT READY**: something is still wrong after the last round, or a check could not run (for example EPUBCheck could not be downloaded). The prepared file must not be loaded onto the device. The report says why.

### READY with minor issues

A READY report can still have a table headed **"Minor issues: these do not block delivery"**. The checker passed every prompt and every section, but noted something small about a prompt that passed: a prompt that asks three things at once, or a reference answer whose wording is a little stronger than the text. A blocking issue always fails the checker, so a READY book never has one.

The book is ready as it is: load it. Fixing a minor issue would change the prepared file, and a changed file needs a new check, so `/prep-book` does not start another round for them. They can be addressed in a later re-preparation of the book (below). The first real book has four such issues; they are listed in `docs/evidence/p5/README.md`.

### Re-preparing a book, and new versions of the instructions

The preparation instructions have a version (`instructions_version` at the top of `tools/prep/INSTRUCTIONS.md`, now 1.1), and every prepared book records the version it was made under. When the instructions change:

- A book already prepared stays a valid prepared book. Its report, and every check run on it again, still give the same result; nothing needs to be done. (The first real book was prepared under 1.0 and still reports READY.)
- A new run of `/prep-book` always uses the current version. A run cannot be continued under a different version: start a new one.
- Re-preparing a book means running `/prep-book` on it again. The instructions require a re-preparation to keep the earlier prompt ids, because the grades saved on the device are keyed by them. `/prep-book` cannot yet take the earlier draft as its starting point, so today a re-prepared book would get new prompts under ids that may look the same. Until that is supported, do not re-prepare a book whose saved grades you want to keep.

## Where the files are

In the output folder:

| File | What it is |
| --- | --- |
| `Book.prepared.epub` | The prepared book. Only written when the book is READY. |
| `Book.validation-report.md` | The report, for reading: READY or NOT READY, why, every check, every issue raised in any round and how it was resolved. |
| `Book.validation-report.json` | The same report for scripts. |
| `work/` | Everything the run used: the numbered text, the draft, and `work/rounds/01`, `02`, `03` with each round's draft, validator result, checker result and issue list. Keep it if you want to see how the book was prepared; it is not needed on the device. |

## Load the prepared book into the simulator

The simulator uses a folder as its SD card: by default `fs_/` in the repository root, where `fs_/books/` is the `/books/` folder of a real SD card (see the [simulator's README](https://github.com/crosspoint-reader/crosspoint-simulator#setup); `CROSSPOINT_SIM_SD` chooses another folder).

1. Copy `Book.prepared.epub` into `fs_/books/`.
2. Start the simulator (the one-command start script comes with the simulator task; until then, `pio run -e simulator_picco` builds it).
3. Open the book from the library. The firmware recognises a prepared book by its checkpoint file and shows checkpoints at the end of each section.

On the device, copy the same file into the `/books/` folder of the SD card.

## What to do with a NOT READY report

1. Open `Book.validation-report.md` and read the reasons at the top and the rows marked **Open** in the issues table.
2. If a check could not run (EPUBCheck "Not run"), fix that first: it usually means the session had no network access to download EPUBCheck, or Java is missing. Then run `/prep-book` again with a new output folder.
3. If the checker kept failing the same prompt in all three rounds, run `/prep-book` again with a new output folder: the drafter starts fresh and often finds a better prompt. Checker verdicts can vary between runs.
4. If it fails again in the same way, open an issue in this repository and attach the report and the `work/` folder.

Never load a NOT READY file onto the device, and never edit a report or a checker result by hand: the report is rebuilt from the files themselves, so editing changes nothing except your trust in it.
