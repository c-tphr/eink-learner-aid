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
3. Approve the file writes Claude Code asks about (the draft and the checker's reply). Then wait. You do not need to answer anything else.

What happens:

- A script reads the book into numbered paragraphs.
- Claude (the **drafter**) chooses the sections and writes the prompts and reference answers.
- Scripts build the prepared EPUB and check its structure (anchors, word counts, nothing else changed, EPUBCheck).
- A separate AI (the **checker**), which sees only the book text, the instructions and the prompts, checks that every answer is supported by its own section, is clear enough to grade, and that no prompt gives its answer away.
- If anything fails, the drafter gets only the list of problems and fixes them. This repeats at most three times.
- A script writes the report and decides READY or NOT READY. Claude never decides that itself.

Your original EPUB is only read; it is never changed.

**How long it takes.** It depends on the book's length. The tiny test book takes a few minutes per round. For a short book (around 20,000 words), expect roughly half an hour to an hour; each round reads the whole book once for drafting and once for checking. The first real book (*Common Sense*) will give a measured figure.

## READY or NOT READY

The first line of the report says which.

- **READY**: the validator and the checker both passed exactly this prepared file. It is safe to load onto the device.
- **NOT READY**: something is still wrong after the last round, or a check could not run (for example EPUBCheck could not be downloaded). The prepared file must not be loaded onto the device. The report says why.

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
