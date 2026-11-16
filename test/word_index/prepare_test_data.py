"""Writes the WordIndex tests' input files into the build tree (never committed).

  prepare_test_data.py REPO_ROOT OUT_DIR

- original/<zip path>: every spine document of fixtures/books/fixture.epub, as listed in
  fixtures/books/fixture.expected.json;
- fixture.prepared.epub and prepared/<zip path>: the fixture prepared with
  `python3 -m tools.prep build` from fixtures/books/fixture.draft.json, so the anchors are where the
  preparation tool puts them;
- test_reader_menu_progress_chapter1.xhtml: OEBPS/chapter1.xhtml of
  test/epubs/test_reader_menu_progress.epub (3,051 words);
- sources.txt: "<file> <size> <crc32>" from each zip entry's central directory record.
"""

import json
import os
import subprocess
import sys
import zipfile


def extract(epub, member, target, sources, name):
    info = zipfile.ZipFile(epub).getinfo(member)
    data = zipfile.ZipFile(epub).read(member)
    os.makedirs(os.path.dirname(target), exist_ok=True)
    with open(target, "wb") as f:
        f.write(data)
    sources.append(f"{name} {info.file_size} {info.CRC}")


def main():
    repo, out = sys.argv[1], sys.argv[2]
    books = os.path.join(repo, "fixtures", "books")
    epub = os.path.join(books, "fixture.epub")
    prepared = os.path.join(out, "fixture.prepared.epub")
    os.makedirs(out, exist_ok=True)
    if os.path.exists(prepared):
        os.remove(prepared)
    subprocess.run([sys.executable, "-m", "tools.prep", "build", epub, os.path.join(books, "fixture.draft.json"),
                    "-o", prepared], cwd=repo, check=True)

    with open(os.path.join(books, "fixture.expected.json"), encoding="utf-8") as f:
        expected = json.load(f)
    sources = []
    for document in expected["documents"]:
        member = document["zip_path"]
        extract(epub, member, os.path.join(out, "original", member), sources, "original/" + member)
        extract(prepared, member, os.path.join(out, "prepared", member), sources, "prepared/" + member)
    extract(os.path.join(repo, "test", "epubs", "test_reader_menu_progress.epub"), "OEBPS/chapter1.xhtml",
            os.path.join(out, "test_reader_menu_progress_chapter1.xhtml"), sources,
            "test_reader_menu_progress_chapter1.xhtml")
    with open(os.path.join(out, "sources.txt"), "w", encoding="utf-8") as f:
        f.write("\n".join(sources) + "\n")


if __name__ == "__main__":
    main()
