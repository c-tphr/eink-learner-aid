"""A small XML tree that remembers where every tag sits in the source bytes.

``prep build`` inserts anchors by splicing bytes into the original file, so it
needs the byte offsets of start and end tags, which lxml does not expose.
Expat (standard library) reports them, and this module builds a minimal tree
from its events:

- ``Element.start`` / ``start_end``: the ``<`` of the start tag and one past
  its ``>``; ``end`` / ``end_end`` the same for the end tag. For an empty
  element tag (``<br/>``) ``end == start_end`` and ``end_end == start_end``.
- ``order`` / ``last_order``: preorder number of the element and of its last
  descendant, so "is X inside Y" is a range test.
- Text is kept as ``str`` children between elements. Character references
  and the five predefined entities are resolved by expat. Named HTML
  entities such as ``&nbsp;`` (legal only when the document declares an
  external DTD, which expat does not read) are resolved from the HTML 5
  entity table, so the text matches what a reader shows.

UTF-16 documents are refused: every offset and splice assumes an
ASCII-compatible encoding.
"""

from __future__ import annotations

import re
import xml.parsers.expat
from typing import Iterator, Union

from .entities import expand as expand_entity

XHTML_NS = "http://www.w3.org/1999/xhtml"
OPS_NS = "http://www.idpf.org/2007/ops"
XML_NS = "http://www.w3.org/XML/1998/namespace"
SEP = "\x01"

_START_TAG_RE = re.compile(rb"<[^\s/>]+(?:\s+[^\s=/>]+\s*=\s*(?:\"[^\"]*\"|'[^']*'))*\s*/?>")
_ENCODING_RE = re.compile(rb"^(?:\xef\xbb\xbf)?<\?xml[^>]*?encoding\s*=\s*[\"']([A-Za-z0-9._-]+)[\"']")
_ASCII_COMPATIBLE = {"utf-8", "utf8", "us-ascii", "ascii", "iso-8859-1", "latin-1", "latin1"}


class XmlError(ValueError):
    """The bytes are not a well-formed XML document this module can handle."""


class Element:
    __slots__ = ("ns", "local", "prefix", "attrib", "parent", "children", "start", "start_end", "end", "end_end",
                 "line", "order", "last_order")

    def __init__(self, ns: str, local: str, prefix: str, attrib: dict[str, str], parent: Element | None):
        self.ns = ns
        self.local = local
        self.prefix = prefix
        self.attrib = attrib
        self.parent = parent
        self.children: list[Union[Element, str]] = []
        self.start = self.start_end = self.end = self.end_end = -1
        self.line = 0
        self.order = self.last_order = -1

    def __repr__(self) -> str:
        return f"<Element {self.local} line {self.line}>"

    @property
    def is_empty_tag(self) -> bool:
        return self.end == self.start_end and self.end_end == self.start_end

    def get(self, name: str, default: str | None = None) -> str | None:
        """Attribute by local name (no namespace) or by ``{ns}local``."""
        return self.attrib.get(name, default)

    def elements(self) -> Iterator[Element]:
        for child in self.children:
            if isinstance(child, Element):
                yield child

    def iter(self) -> Iterator[Element]:
        """This element and every descendant, in document order."""
        yield self
        for child in self.elements():
            yield from child.iter()

    def find_all(self, ns: str, local: str) -> Iterator[Element]:
        for el in self.iter():
            if el.ns == ns and el.local == local:
                yield el

    def first(self, ns: str, local: str) -> Element | None:
        return next(self.find_all(ns, local), None)

    def contains(self, other: Element) -> bool:
        return self.order <= other.order <= self.last_order

    def text(self) -> str:
        """All descendant text, concatenated."""
        out: list[str] = []
        for child in self.children:
            out.append(child if isinstance(child, str) else child.text())
        return "".join(out)

    def path(self) -> str:
        """An XPath-like locator with local names and 1-based positions among same-named siblings."""
        parts = []
        el: Element | None = self
        while el is not None:
            if el.parent is None:
                parts.append(f"{el.local}[1]")
            else:
                same = [c for c in el.parent.elements() if c.local == el.local and c.ns == el.ns]
                parts.append(f"{el.local}[{same.index(el) + 1}]")
            el = el.parent
        return "/" + "/".join(reversed(parts))

    def qname(self, local: str) -> str:
        """``local`` written with this element's namespace prefix, for inserting a sibling or child tag."""
        return f"{self.prefix}:{local}" if self.prefix else local


class Document:
    def __init__(self, data: bytes, root: Element, ids: dict[str, Element], duplicate_ids: list[str]):
        self.data = data
        self.root = root
        self.ids = ids
        self.duplicate_ids = duplicate_ids


def _split(name: str) -> tuple[str, str, str]:
    parts = name.split(SEP)
    if len(parts) == 1:
        return "", parts[0], ""
    if len(parts) == 2:
        return parts[0], parts[1], ""
    return parts[0], parts[1], parts[2]


def _attr_key(name: str) -> str:
    ns, local, _ = _split(name)
    return f"{{{ns}}}{local}" if ns else local


def check_encoding(data: bytes, where: str) -> None:
    if data.startswith((b"\xff\xfe", b"\xfe\xff")):
        raise XmlError(f"{where}: UTF-16 documents are not supported")
    m = _ENCODING_RE.match(data)
    if m and m.group(1).decode("ascii").lower() not in _ASCII_COMPATIBLE:
        raise XmlError(f"{where}: encoding {m.group(1).decode('ascii')!r} is not supported (use UTF-8)")


def parse(data: bytes, where: str = "document") -> Document:
    """Parse ``data`` into a ``Document``; raise ``XmlError`` with the location on failure."""
    check_encoding(data, where)
    parser = xml.parsers.expat.ParserCreate(namespace_separator=SEP)
    parser.namespace_prefixes = True
    parser.buffer_text = True
    stack: list[Element] = []
    root_holder: list[Element] = []
    counter = [0]
    ids: dict[str, Element] = {}
    duplicates: list[str] = []

    def add_text(text: str) -> None:
        if not stack:
            return
        children = stack[-1].children
        if children and isinstance(children[-1], str):
            children[-1] += text
        else:
            children.append(text)

    def start(name: str, attrs: dict[str, str]) -> None:
        ns, local, prefix = _split(name)
        attrib = {_attr_key(k): v for k, v in attrs.items()}
        el = Element(ns, local, prefix, attrib, stack[-1] if stack else None)
        el.start = parser.CurrentByteIndex
        el.line = parser.CurrentLineNumber
        m = _START_TAG_RE.match(data, el.start)
        if not m:
            raise XmlError(f"{where}: cannot locate the start tag of <{local}> at line {el.line}")
        el.start_end = m.end()
        el.order = counter[0]
        counter[0] += 1
        if stack:
            stack[-1].children.append(el)
        else:
            root_holder.append(el)
        stack.append(el)
        for key in ("id", f"{{{XML_NS}}}id"):
            value = attrib.get(key)
            if value is not None:
                if value in ids:
                    duplicates.append(value)
                else:
                    ids[value] = el

    def end(name: str) -> None:
        el = stack.pop()
        if data[el.start_end - 2 : el.start_end] == b"/>":
            el.end = el.end_end = el.start_end
        else:
            el.end = parser.CurrentByteIndex
            close = data.find(b">", el.end)
            if data[el.end : el.end + 2] != b"</" or close < 0:
                raise XmlError(f"{where}: cannot locate the end tag of <{el.local}> opened at line {el.line}")
            el.end_end = close + 1
        el.last_order = counter[0] - 1

    def skipped(name: str, is_parameter_entity: bool) -> None:
        if not is_parameter_entity:
            add_text(expand_entity(name))

    parser.StartElementHandler = start
    parser.EndElementHandler = end
    parser.CharacterDataHandler = add_text
    parser.SkippedEntityHandler = skipped
    try:
        parser.Parse(data, True)
    except xml.parsers.expat.ExpatError as exc:
        raise XmlError(f"{where}: not well-formed XML: {exc}") from exc
    if not root_holder:
        raise XmlError(f"{where}: no root element")
    return Document(data, root_holder[0], ids, duplicates)
