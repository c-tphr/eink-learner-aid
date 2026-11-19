"""Versions recorded in validation results and reports.

``TOOL_VERSION`` is raised whenever a change to ``tools/prep`` can change a
verdict of ``validate``, ``verify-check`` or ``report``. ``WORD_RULES_VERSION``
is the version of the word rules (``docs/spikes/S1-word-stream.md`` section 5
plus NOTE regions) that ``words.py`` implements; it equals the
``rules_version`` of ``fixtures/books/fixture.expected.json`` (a test checks).
"""

TOOL_NAME = "tools.prep"
TOOL_VERSION = "1.0.0"
WORD_RULES_VERSION = 1
