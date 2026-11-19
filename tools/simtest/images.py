"""Screenshot conversion, exact comparison with masks, and diff images."""

from __future__ import annotations

import hashlib
from dataclasses import dataclass, field
from pathlib import Path

from PIL import Image, ImageChops, ImageDraw

# Rectangles are [x, y, width, height] in screenshot pixels.
Rect = tuple[int, int, int, int]


def compact(img: Image.Image) -> Image.Image:
    """Smallest lossless mode: 1-bit for pure black and white, L for gray, else RGB."""
    rgb = img.convert("RGB")
    r, g, b = rgb.split()
    if ImageChops.difference(r, g).getbbox() is None and ImageChops.difference(g, b).getbbox() is None:
        gray = r
        colors = gray.getcolors(256) or []
        if {c for _, c in colors} <= {0, 255}:
            return gray.point(lambda v: 255 if v else 0).convert("1", dither=Image.Dither.NONE)
        return gray
    return rgb


def save_png(img: Image.Image, path: Path) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    compact(img).save(path, format="PNG", optimize=True)


def bmp_to_png(bmp: Path, png: Path) -> Image.Image:
    with Image.open(bmp) as raw:
        img = raw.convert("RGB")
    save_png(img, png)
    return img


def pixel_hash(img: Image.Image) -> str:
    """Hash of the RGB pixels and size, independent of the file encoding."""
    rgb = img.convert("RGB")
    h = hashlib.sha256(f"{rgb.width}x{rgb.height}".encode())
    h.update(rgb.tobytes())
    return h.hexdigest()


@dataclass
class Comparison:
    equal: bool
    size_mismatch: bool = False
    diff_pixels: int = 0
    bbox: Rect | None = None  # [x, y, w, h] of differing pixels outside masks
    message: str = ""
    masked: list[Rect] = field(default_factory=list)


def _mask_image(size: tuple[int, int], masks: list[Rect]) -> Image.Image:
    """L image, 255 where pixels count, 0 inside masks."""
    keep = Image.new("L", size, 255)
    draw = ImageDraw.Draw(keep)
    for x, y, w, h in masks:
        draw.rectangle([x, y, x + w - 1, y + h - 1], fill=0)
    return keep


def difference_mask(actual: Image.Image, reference: Image.Image, masks: list[Rect]) -> Image.Image:
    """L image, 255 where the pixels differ outside the masks."""
    diff = ImageChops.difference(actual.convert("RGB"), reference.convert("RGB")).convert("L")
    diff = diff.point(lambda v: 255 if v else 0)
    if masks:
        diff = ImageChops.multiply(diff, _mask_image(diff.size, masks))
    return diff


def compare(actual: Image.Image, reference: Image.Image, masks: list[Rect] | None = None,
            max_diff_pixels: int = 0) -> Comparison:
    masks = list(masks or [])
    if actual.size != reference.size:
        return Comparison(False, size_mismatch=True, message=(
            f"size differs: actual {actual.size[0]}x{actual.size[1]}, "
            f"reference {reference.size[0]}x{reference.size[1]}"), masked=masks)
    diff = difference_mask(actual, reference, masks)
    box = diff.getbbox()
    if box is None:
        return Comparison(True, masked=masks)
    count = diff.histogram()[255]  # the mask is 0 or 255
    rect = (box[0], box[1], box[2] - box[0], box[3] - box[1])
    ok = count <= max_diff_pixels
    where = f"x={rect[0]}..{box[2] - 1}, y={rect[1]}..{box[3] - 1} ({rect[2]}x{rect[3]})"
    msg = f"{count} pixel(s) differ in bounding box {where}"
    if ok:
        msg += f"; within tolerance max_diff_pixels={max_diff_pixels}"
    return Comparison(ok, diff_pixels=count, bbox=rect, message=msg, masked=masks)


def diff_image(actual: Image.Image, reference: Image.Image, masks: list[Rect] | None = None,
               bbox: Rect | None = None) -> Image.Image:
    """Side by side: reference | actual | changed pixels in red over a faded actual."""
    masks = list(masks or [])
    a = actual.convert("RGB")
    r = reference.convert("RGB")
    w = max(a.width, r.width)
    h = max(a.height, r.height)
    gap = 8
    out = Image.new("RGB", (w * 3 + gap * 2, h + 18), (128, 128, 128))
    out.paste(r, (0, 18))
    out.paste(a, (w + gap, 18))
    faded = Image.blend(a, Image.new("RGB", a.size, (255, 255, 255)), 0.75)
    if a.size == r.size:
        changed = difference_mask(a, r, masks)
        faded.paste((220, 0, 0), (0, 0), changed)
    overlay = ImageDraw.Draw(faded)
    for x, y, mw, mh in masks:
        overlay.rectangle([x, y, x + mw - 1, y + mh - 1], outline=(0, 90, 220))
    if bbox:
        x, y, bw, bh = bbox
        overlay.rectangle([x - 2, y - 2, x + bw + 1, y + bh + 1], outline=(255, 140, 0), width=2)
    out.paste(faded, (2 * (w + gap), 18))
    label = ImageDraw.Draw(out)
    for i, text in enumerate(("reference", "actual", "diff (red), mask (blue), bbox (orange)")):
        label.text((i * (w + gap) + 4, 3), text, fill=(255, 255, 255))
    return out
