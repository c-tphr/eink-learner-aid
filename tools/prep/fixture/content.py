"""Source data for the synthetic fixture book (docs/PLAN.md section 8, "Test material").

Everything the generator writes (the EPUB, the draft and the expected word
data) is derived from the constants in this file. Edit here, then run
``python3 tools/prep/fixture/generate.py`` and commit the regenerated files.

Inline markup inside ``("p", ...)`` and heading strings:

- ``*word*``        -> ``<em>word</em>``
- ``**word**``      -> ``<strong>word</strong>``
- ``[^fn1]``        -> a footnote reference mark linking to footnote ``fn1``.
  It must follow a non-space character and be followed by a space or the
  end of the paragraph, so it never sits inside a word.

Prose is plain ASCII with single spaces, except for the one paragraph that
deliberately holds non-ASCII characters (``c2-p009``: U+00E9 and U+2014).

Block ids are not written here: the generator numbers every block of a
document in order (``<prefix>-p001``, ``<prefix>-p002``, ...) and every
footnote paragraph as ``<prefix>-n001``, ... The draft below cites those ids,
and the generator fails if a cited id does not exist.
"""

from __future__ import annotations

TITLE = "The Lighthouse Keeper's Almanac"
AUTHOR = "Mara Ellery Quill"
LANGUAGE = "en"
# uuid.uuid5(uuid.NAMESPACE_URL, "https://github.com/c-tphr/eink-learner-aid/fixtures/books/fixture.epub")
IDENTIFIER = "urn:uuid:2b6afafc-70f4-5bfc-99c8-9b5cc02d7b39"
MODIFIED = "2026-01-01T00:00:00Z"
# Timestamp of every entry in the EPUB zip (local time fields, no zone).
ZIP_DATE_TIME = (2026, 1, 1, 0, 0, 0)
INSTRUCTIONS_VERSION = "fixture"

IMAGE_HREF = "images/gannet-rock.png"

# Each document: href (relative to the OPF), role, id prefix, <title>, blocks.
FRONT_MATTER = {
    "href": "text/title.xhtml",
    "role": "front-matter",
    "prefix": "fm",
    "title": "Title page",
    "blocks": [
        ("h1", TITLE),
        ("p", AUTHOR),
        ("p", "A synthetic book made for automated tests. Its people and places are invented."),
    ],
}

CHAPTERS = [
    {
        "href": "text/chapter-1.xhtml",
        "role": "chapter",
        "prefix": "c1",
        "title": "The Lamp Room",
        "blocks": [
            ("h1", "The Lamp Room"),
            (
                "p",
                "The supply boat reached Gannet Rock a little after noon. It had crossed the strait at "
                "3.5 knots, which was as fast as old Tobin would ever push it. Dr. Vane stepped onto the "
                "landing stage with a leather bag in each hand and looked up at the tower. Was this really "
                "where she would spend the winter?",
            ),
            (
                "p",
                "Ione Hartwell, the keeper, met her at the foot of the stairs. She had kept the light for "
                "nineteen years, and she did not waste words. \"The lamp is lit at sunset and put out at "
                "sunrise, whatever the weather.\" Then she turned and began to climb.",
            ),
            (
                "figure",
                {
                    "src": IMAGE_HREF,
                    "alt": "Line drawing of a striped lighthouse tower on a rock, with rays of light on both sides.",
                    "caption": "Figure 1. Gannet Rock light, seen from the landing stage.",
                },
            ),
            (
                "p",
                "There were one hundred and twelve steps. At the top, the lamp room was round and bright, "
                "with glass on every side. In the middle stood the great lens, taller than a man, built "
                "from rings of cut glass that bent the light of a single oil flame into one narrow beam. "
                "Dr. Vane had read about such lenses, but she had never stood beside one.[^fn1]",
            ),
            (
                "p",
                "She asked how ships could tell one light from another in the dark. Ione explained that "
                "every light on the coast had its own pattern, and that sailors counted the flashes. "
                "Gannet Rock gave one white flash every ten seconds; no other light on the coast did "
                "the same.",
            ),
            ("h2", "Winding the Clockwork"),
            (
                "p",
                "The lens did not turn by magic. Beneath it sat a clockwork drive, worked by a heavy iron "
                "weight that hung on a cable down the hollow centre of the tower. As the weight sank, the "
                "gears turned the lens once every forty seconds, and its four bright panels gave four "
                "flashes on each turn. When the weight reached the bottom, the lens stopped, and a still "
                "light was worse than no light at all, because a ship might mistake it for another.",
            ),
            (
                "p",
                "So the weight had to be wound back up every four hours, all night long. Ione showed "
                "Dr. Vane the crank and the brass counter beside it. Forty turns of the crank lifted the "
                "weight to the top. Forty turns! By the tenth, the doctor's arms were burning, and Ione "
                "only smiled.",
            ),
            (
                "p",
                "That first night they sat in the watch room below the lamp and took turns to sleep. Every "
                "hour Ione wrote the time, the weather and the state of the light in the log. Nothing "
                "happened at all, and Dr. Vane decided that this was exactly what a good night at a "
                "lighthouse should look like.",
            ),
        ],
    },
    {
        "href": "text/chapter-2.xhtml",
        "role": "chapter",
        "prefix": "c2",
        "title": "The Fog Season",
        "blocks": [
            ("h1", "The Fog Season"),
            (
                "p",
                "In November the fog came. It rolled in from the sea in the late afternoon, and within an "
                "hour the beam of the lamp was swallowed a few yards from the glass. On those nights the "
                "light was almost useless; the keepers had to rely on sound instead.",
            ),
            (
                "p",
                "Fog was the danger every keeper feared most. On a clear night a ship could see the light "
                "from twenty miles away, but in thick fog even the strongest beam could vanish within a "
                "stone's throw of the tower, and a ship might run onto the rocks without ever seeing it.",
            ),
            (
                "p",
                "The fog signal stood in a low brick house beside the tower. An engine filled a tank with "
                "air, and the air was let out through a horn in a pattern of two blasts every minute. Ione "
                "started the engine as soon as she could no longer see the bell buoy, which lay half a "
                "mile to the south.",
            ),
            (
                "table",
                {
                    "caption": "Lights and fog signals of the eastern coast",
                    "header": ["Station", "Light", "Fog signal"],
                    "rows": [
                        ["Gannet Rock", "1 white flash every 10 s", "2 blasts every 60 s"],
                        ["Skerry Point", "2 red flashes every 15 s", "Bell every 20 s"],
                        ["Harrow Head", "Fixed white", "None"],
                    ],
                },
            ),
            (
                "p",
                "Ione kept a printed list of every light and fog signal on the coast pinned beside the "
                "door. Sailors learned these patterns by heart, because in the dark or in fog a pattern "
                "was the only thing that told one station from another.",
            ),
            (
                "p",
                "Dr. Vane asked why the horn was not simply sounded all the time. Ione explained that the "
                "engine burned coal, and coal came out on the supply boat only once a month. If the fog "
                "lasted a week, they would have to choose between warmth and safety. They always chose "
                "safety.",
            ),
            ("h2", "The Visitor from the Mainland"),
            (
                "p",
                "On the ninth night of fog, someone knocked at the door. Ione opened it to find a young "
                "fisherman, half-frozen and shaking, who had rowed across from the mainland after his "
                "engine failed. He had steered by the sound of the horn alone\u2014two blasts, then "
                "silence, then two blasts again\u2014until his boat grounded on the shingle below the "
                "tower. His name was Peter Lund, and he ran a small caf\u00e9 on the harbour at Skerry "
                "Point.",
            ),
            (
                "p",
                "They gave him dry clothes and hot tea. While he warmed his hands, he told them something "
                "strange. Out on the water, every stroke of his oars had left a trail of green light, as "
                "if the sea itself were glowing. Dr. Vane knew the word for it: phosphorescence, the light "
                "made by tiny living creatures when the water is disturbed.[^fn2]",
            ),
            (
                "p",
                "Peter asked whether the glow was dangerous. It was *not*, said Dr. Vane, but it was a "
                "sign that the water was full of plankton, and where there was plankton there would be "
                "fish. The fisherman grinned for the first time that night.",
            ),
            (
                "p",
                "Ione made up a bed for him in the watch room, but he did not sleep. He sat by the window "
                "and listened to the horn, counting the blasts under his breath as if he were still out on "
                "the water, until at last the sky began to grow pale.",
            ),
            (
                "p",
                "At dawn the fog lifted at last. Ione rowed Peter back to his boat, and together they "
                "pulled it off the shingle. Before he left, she gave him one piece of advice: **never** "
                "put to sea in November without oars, however good the engine.[^fn3] Then she went back "
                "up the one hundred and twelve steps to put out the lamp.",
            ),
        ],
    },
    {
        "href": "text/chapter-3.xhtml",
        "role": "chapter",
        "prefix": "c3",
        "title": "The Keeper's Log",
        "blocks": [
            ("h1", "The Keeper's Log"),
            (
                "p",
                "When spring came, Dr. Vane began to copy the old logbooks into a simple list so that she "
                "could study them. Ione had kept the log for nineteen years, and its pages held the time, "
                "the weather and the state of the light for every hour of every night. Some pages were "
                "stained with salt water, and a few were so faded that she had to read them by the window.",
            ),
            (
                "p",
                "The doctor wanted to know how often the fog signal had been needed. Counting by hand "
                "would take weeks, so she wrote a short program instead. It read one line per hour and "
                "added up the hours marked as fog.",
            ),
            (
                "pre",
                [
                    "fog_hours = 0",
                    'for line in open("log.txt"):',
                    '    fields = line.strip().split(",")',
                    '    if len(fields) >= 3 and fields[2] == "fog":',
                    "        fog_hours += 1",
                    'print("hours of fog:", fog_hours)',
                ],
            ),
            (
                "p",
                "The answer surprised them both. Over nineteen years the horn had sounded for more than "
                "four thousand hours, and nearly half of those hours fell in November and December. Why "
                "had nobody noticed before? Ione said that a keeper only ever sees one night at a time.",
            ),
            (
                "p",
                "She also counted the nights when the clockwork had stopped. There were only three in "
                "nineteen years. Each time, Ione had climbed into the lamp room and turned the great lens "
                "by hand until dawn, walking it round and round so that passing ships would still see one "
                "white flash every ten seconds.",
            ),
            (
                "p",
                "Dr. Vane sent her figures to the lighthouse board with a request for a second engine and "
                "a larger coal store. The reply came in June: the board agreed, and the new engine would "
                "arrive before the next fog season.",
            ),
            (
                "p",
                "On her last evening on the rock, the doctor climbed the steps once more and watched Ione "
                "light the lamp. The beam swept out across the strait, one white flash every ten seconds, "
                "exactly as it had for nineteen years.",
            ),
        ],
    },
]

# Footnote bodies, keyed by the id used in ``[^...]`` marks. Each body is a
# list of paragraphs. Marks are numbered 1, 2, 3 ... in order of first
# reference across the book, and the note is placed in an
# <aside epub:type="footnote"> at the end of the chapter that references it.
FOOTNOTES = {
    "fn1": [
        "Lenses of this kind were first built in the 1820s. They are made in rings so that the glass "
        "can stay thin and light."
    ],
    "fn2": ["Most of this light comes from single-celled plankton that glow when they are shaken."],
    "fn3": ["Oars weigh little and never run out of fuel, so many small boats carried a pair as a spare."],
}

# Deliberate tokenisation cases: (case, block id, the exact word token).
# The generator fails if the token is not a word of that block.
TOKEN_CASES = [
    ("Sentence ending in a full stop", "c1-p002", "noon."),
    ("Decimal number (the point is not a sentence end)", "c1-p002", "3.5"),
    ("Abbreviation with a full stop (not a sentence end)", "c1-p002", "Dr."),
    ("Sentence ending in a question mark", "c1-p002", "winter?"),
    ("Comma", "c1-p003", "Hartwell,"),
    ("Quoted sentence: opening ASCII double quote attached to the first word", "c1-p003", '"The'),
    ("Quoted sentence: closing double quote after the full stop", "c1-p003", 'weather."'),
    ("Semicolon", "c1-p006", "seconds;"),
    ("Sentence ending in an exclamation mark", "c1-p009", "turns!"),
    ("Apostrophe inside a word", "c1-p009", "doctor's"),
    ("Hyphenated compound (one word)", "c2-p009", "half-frozen"),
    ("Em dash U+2014 with no spaces: joins its neighbours into one word", "c2-p009", "alone\u2014two"),
    ("Em dash U+2014 with no spaces (second)", "c2-p009", "again\u2014until"),
    ("Non-ASCII letter: precomposed e acute U+00E9", "c2-p009", "caf\u00e9"),
    ("Colon", "c2-p010", "it:"),
    ("Long word (15 letters) with trailing comma", "c2-p010", "phosphorescence,"),
    ("Word inside an em element, comma outside it", "c2-p011", "not,"),
    ("Word inside a strong element", "c2-p013", "never"),
]

# ---------------------------------------------------------------------------
# The draft (docs/PLAN.md section 8, step 2), as a drafter session would write
# it. Paragraph ranges are inclusive and cite block ids.

EXCLUDED = [
    {
        "chapter_href": "text/title.xhtml",
        "first_paragraph": "fm-p001",
        "last_paragraph": "fm-p003",
        "reason": "Front matter: title page.",
    }
]

SECTIONS = [
    {
        "id": "s0001",
        "chapter_href": "text/chapter-1.xhtml",
        "first_paragraph": "c1-p001",
        "last_paragraph": "c1-p006",
        "label": "Arrival at Gannet Rock",
    },
    {
        "id": "s0002",
        "chapter_href": "text/chapter-1.xhtml",
        "first_paragraph": "c1-p007",
        "last_paragraph": "c1-p010",
        "label": "Winding the clockwork",
    },
    {
        "id": "s0003",
        "chapter_href": "text/chapter-2.xhtml",
        "first_paragraph": "c2-p001",
        "last_paragraph": "c2-p007",
        "label": "Fog signals",
    },
    {
        "id": "s0004",
        "chapter_href": "text/chapter-2.xhtml",
        "first_paragraph": "c2-p008",
        "last_paragraph": "c2-p013",
        "label": "The visitor from the mainland",
    },
    {
        "id": "s0005",
        "chapter_href": "text/chapter-3.xhtml",
        "first_paragraph": "c3-p001",
        "last_paragraph": "c3-p008",
        "label": "The keeper's log",
    },
]

CHECKPOINTS = [
    {
        "id": "c0001",
        "section": "s0001",
        "prompts": [
            {
                "id": "c0001-p1",
                "prompt": "How could sailors tell the Gannet Rock light apart from the other lights on the coast?",
                "answer": "Every light had its own pattern of flashes, and sailors counted them. Gannet Rock "
                "gave one white flash every ten seconds, which no other light on the coast did.",
                "supporting_paragraphs": ["c1-p006"],
            }
        ],
    },
    {
        "id": "c0002",
        "section": "s0004",
        "prompts": [
            {
                "id": "c0002-p1",
                "prompt": "How did the fisherman find his way to the lighthouse in the fog?",
                "answer": "He steered by the sound of the fog horn alone, following its pattern of two "
                "blasts and silence until his boat grounded on the shingle below the tower.",
                "supporting_paragraphs": ["c2-p009"],
            },
            {
                "id": "c0002-p2",
                "prompt": "What caused the green light on the water, and what did it tell Dr. Vane?",
                "answer": "Phosphorescence: light made by tiny living creatures when the water is disturbed. "
                "It was not dangerous, but it showed the water was full of plankton, so there would be fish.",
                "supporting_paragraphs": ["c2-p010", "c2-p011"],
            },
            {
                "id": "c0002-p3",
                "prompt": "What advice did Ione give the fisherman before he left?",
                "answer": "Never put to sea in November without oars, however good the engine.",
                "supporting_paragraphs": ["c2-p013"],
            },
        ],
    },
]
