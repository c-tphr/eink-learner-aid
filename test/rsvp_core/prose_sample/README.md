# English prose sample for pacing normalisation

`prose_sample.txt` is the text `ProseSampleTest.cpp` measures to set `PACING.normalisation` in
`lib/RsvpCore/Pacing.h`: the mean raw word weight over this sample. With that constant, typical
prose plays at the target WPM with its punctuation holds included.

## Contents

5,746 whitespace-separated words (5,793 RSVP words once em dashes are split as WordIndex splits
them), 113 paragraphs, two public-domain texts of different kinds:

| Lines | Text | Kind |
| --- | --- | --- |
| 1 to 86 | Jerome K. Jerome, *Three Men in a Boat* (1889), chapter I, complete (synopsis and body; chapter number left out) | Narrative with dialogue |
| 87 to 113 | Thomas Paine, *Common Sense* (1776), section "Of the Origin and Design of Government in General, with Concise Remarks on the English Constitution", complete (heading and body) | Expository prose |

## Source and licence

Both texts are in the public domain (published in 1889 and 1776). The transcriptions are from
Standard Ebooks, which releases its editions under the CC0 1.0 Universal Public Domain Dedication:

- <https://standardebooks.org/ebooks/jerome-k-jerome/three-men-in-a-boat/text/chapter-1>
- <https://standardebooks.org/ebooks/thomas-paine/essays/text/common-sense>

Retrieved 2026-11-17.

## Format and extraction

- UTF-8, one paragraph (or heading) per line, no blank lines.
- Text of each `<p>` (and the section's `<h3>`) inside the chapter, with runs of whitespace,
  including no-break and hair spaces, collapsed to one ASCII space. Footnote reference links were
  dropped; there were none in these parts.
- Punctuation and typography are kept as published: curly quotes, em dashes, and the U+2060 WORD
  JOINER that Standard Ebooks puts before each em dash.

The test turns it into tokens as WordIndex would (docs/spikes/S1-word-stream.md, section 5): words
split on spaces and after an em or en dash followed by another character, and the last word of each
line flagged as a paragraph end.

## Re-measuring

After any change to the weight table, the classification rules or this file:

```sh
cmake --build build/test --target RsvpCoreTest
build/test/rsvp_core/RsvpCoreTest --gtest_filter='ProseSample.*'
```

The test prints the measured mean in `WEIGHT_ONE` units; round it into `PACING.normalisation`.
It fails while the constant is more than 0.5% from the measurement.
