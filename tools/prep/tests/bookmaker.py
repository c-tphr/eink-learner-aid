"""Small EPUB 3 books built inside tests (temp dirs only; nothing is committed)."""

from __future__ import annotations

import hashlib
import json
import zipfile
from pathlib import Path
from typing import Any

from tools.prep.fixture.generate import lighthouse_png

XHTML_HEAD = (
    '<?xml version="1.0" encoding="UTF-8"?>\n<!DOCTYPE html>\n'
    '<html xmlns="http://www.w3.org/1999/xhtml" xmlns:epub="http://www.idpf.org/2007/ops" xml:lang="en" lang="en">\n'
)


def xhtml(body: str, title: str = "Chapter") -> str:
    return f"{XHTML_HEAD}<head>\n<title>{title}</title>\n</head>\n<body>\n{body}\n</body>\n</html>\n"


def make_epub(
    path: Path,
    chapters: list[tuple[str, str]],
    opf_dir: str = "OEBPS",
    compression: int = zipfile.ZIP_STORED,
    newline: str = "\n",
    indent: str = "",
    with_image: bool = False,
) -> Path:
    """Write an EPUB 3 with the given (href, XHTML text) chapters, nav, and an optional PNG."""
    base = f"{opf_dir}/" if opf_dir else ""
    items = ['<item id="nav" href="nav.xhtml" media-type="application/xhtml+xml" properties="nav"/>']
    spine = []
    for n, (href, _) in enumerate(chapters, start=1):
        items.append(f'<item id="ch{n}" href="{href}" media-type="application/xhtml+xml"/>')
        spine.append(f'<itemref idref="ch{n}"/>')
    if with_image:
        items.append('<item id="img" href="images/pic.png" media-type="image/png"/>')
    lines = [
        '<?xml version="1.0" encoding="UTF-8"?>',
        '<package xmlns="http://www.idpf.org/2007/opf" version="3.0" unique-identifier="uid" xml:lang="en">',
        f'{indent}<metadata xmlns:dc="http://purl.org/dc/elements/1.1/">',
        f'{indent}{indent}<dc:identifier id="uid">urn:uuid:00000000-0000-4000-8000-000000000001</dc:identifier>',
        f"{indent}{indent}<dc:title>Test Book</dc:title>",
        f"{indent}{indent}<dc:language>en</dc:language>",
        f'{indent}{indent}<meta property="dcterms:modified">2026-01-01T00:00:00Z</meta>',
        f"{indent}</metadata>",
        f"{indent}<manifest>",
        *[f"{indent}{indent}{i}" for i in items],
        f"{indent}</manifest>",
        f"{indent}<spine>",
        *[f"{indent}{indent}{s}" for s in spine],
        f"{indent}</spine>",
        "</package>",
        "",
    ]
    opf = newline.join(lines)
    toc = "".join(f'<li><a href="{href}">Chapter {n}</a></li>' for n, (href, _) in enumerate(chapters, start=1))
    nav = xhtml(f'<nav epub:type="toc" id="toc"><h1>Contents</h1><ol>{toc}</ol></nav>', "Contents")
    container = (
        '<?xml version="1.0" encoding="UTF-8"?>\n'
        '<container version="1.0" xmlns="urn:oasis:names:tc:opendocument:xmlns:container">\n'
        f'<rootfiles><rootfile full-path="{base}content.opf" media-type="application/oebps-package+xml"/></rootfiles>\n'
        "</container>\n"
    )
    path.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(path, "w") as zf:
        zf.writestr(zipfile.ZipInfo("mimetype", (2026, 1, 1, 0, 0, 0)), "application/epub+zip",
                    compress_type=zipfile.ZIP_STORED)
        entries = [("META-INF/container.xml", container), (f"{base}content.opf", opf), (f"{base}nav.xhtml", nav)]
        entries += [(f"{base}{href}", text) for href, text in chapters]
        for name, text in entries:
            info = zipfile.ZipInfo(name, (2026, 2, 3, 4, 5, 6))
            info.compress_type = compression
            zf.writestr(info, text.encode("utf-8"))
        if with_image:
            info = zipfile.ZipInfo(f"{base}images/pic.png", (2026, 2, 3, 4, 5, 6))
            info.compress_type = compression
            zf.writestr(info, lighthouse_png())
    return path


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def make_draft(
    path: Path,
    epub: Path,
    sections: list[tuple[str, str, str, str]],
    excluded: list[tuple[str, str, str]] = (),
    checkpoints: list[dict[str, Any]] | None = None,
) -> Path:
    """sections: (id, chapter_href, first, last); excluded: (chapter_href, first, last)."""
    if checkpoints is None:
        sid, _, first, _ = sections[0]
        checkpoints = [{"id": "c0001", "section": sid, "prompts": [
            {"id": "c0001-p1", "prompt": "What happened?", "answer": "Something.", "supporting_paragraphs": [first]}
        ]}]
    draft = {
        "format": "rsvp-draft",
        "version": 1,
        "book": {"title": "Test Book", "language": "en", "source_sha256": sha256(epub), "instructions_version": "test"},
        "excluded": [{"chapter_href": h, "first_paragraph": a, "last_paragraph": b, "reason": "Front matter."}
                     for h, a, b in excluded],
        "sections": [{"id": i, "chapter_href": h, "first_paragraph": a, "last_paragraph": b, "label": f"Section {i}"}
                     for i, h, a, b in sections],
        "checkpoints": checkpoints,
    }
    path.write_text(json.dumps(draft, indent=2), encoding="utf-8")
    return path
