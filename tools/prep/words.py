"""The one word definition shared by every preparation tool.

Standard library only: the fixture generator imports it too.
"""

from __future__ import annotations

import re

# The four XML white space characters. Everything else, including U+00A0
# NO-BREAK SPACE and U+2014 EM DASH, is part of a word.
SEPARATORS = " \t\n\r"
WORD_RE = re.compile(r"[^ \t\n\r]+")


def tokenize(text: str) -> list[str]:
    """Split visible prose text into words.

    A word is a maximal run of characters other than the four XML white space
    characters (U+0020, U+0009, U+000A, U+000D). Punctuation stays attached;
    nothing is normalised, case-folded or stripped.
    """
    return WORD_RE.findall(text)


def is_separator_only(text: str) -> bool:
    """True when ``text`` holds no word character (empty or only separators)."""
    return WORD_RE.search(text) is None
