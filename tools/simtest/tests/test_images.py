"""Image conversion, comparison and diff output."""

from PIL import Image, ImageDraw

from tools.simtest import images


def page(w=60, h=40, color=255):
    img = Image.new("RGB", (w, h), (color, color, color))
    ImageDraw.Draw(img).rectangle([5, 5, 20, 10], fill=(0, 0, 0))
    return img


def test_identical_images_are_equal():
    c = images.compare(page(), page())
    assert c.equal and c.diff_pixels == 0 and c.bbox is None


def test_difference_reports_count_and_bounding_box():
    a = page()
    ImageDraw.Draw(a).rectangle([30, 12, 34, 15], fill=(0, 0, 0))  # 5x4 block
    c = images.compare(a, page())
    assert not c.equal
    assert c.diff_pixels == 20
    assert c.bbox == (30, 12, 5, 4)
    assert "x=30..34, y=12..15 (5x4)" in c.message


def test_mask_hides_a_region():
    a = page()
    a.putpixel((50, 30), (0, 0, 0))
    assert not images.compare(a, page()).equal
    assert images.compare(a, page(), masks=[(45, 25, 10, 10)]).equal
    assert not images.compare(a, page(), masks=[(0, 0, 10, 10)]).equal


def test_tolerance():
    a = page()
    a.putpixel((50, 30), (0, 0, 0))
    a.putpixel((51, 30), (0, 0, 0))
    assert not images.compare(a, page(), max_diff_pixels=1).equal
    c = images.compare(a, page(), max_diff_pixels=2)
    assert c.equal and "within tolerance" in c.message


def test_size_mismatch():
    c = images.compare(page(60, 40), page(61, 40))
    assert not c.equal and c.size_mismatch and "61x40" in c.message


def test_compact_picks_smallest_lossless_mode(tmp_path):
    bw = page()
    assert images.compact(bw).mode == "1"
    gray = page()
    gray.putpixel((1, 1), (128, 128, 128))
    assert images.compact(gray).mode == "L"
    color = page()
    color.putpixel((1, 1), (255, 0, 0))
    assert images.compact(color).mode == "RGB"
    for name, img in (("bw", bw), ("gray", gray), ("color", color)):
        p = tmp_path / f"{name}.png"
        images.save_png(img, p)
        with Image.open(p) as back:
            assert images.compare(back, img).equal, name
            assert images.pixel_hash(back) == images.pixel_hash(img)


def test_bmp_to_png(tmp_path):
    bmp = tmp_path / "s.bmp"
    src = page().convert("RGBA")  # the simulator writes 32-bit BMPs
    src.save(bmp, format="BMP")
    img = images.bmp_to_png(bmp, tmp_path / "s.png")
    assert (tmp_path / "s.png").is_file()
    assert images.compare(img, page()).equal


def test_diff_image_is_side_by_side():
    a = page()
    ImageDraw.Draw(a).rectangle([30, 12, 34, 15], fill=(0, 0, 0))
    c = images.compare(a, page())
    out = images.diff_image(a, page(), [], c.bbox)
    assert out.size == (60 * 3 + 16, 40 + 18)
    # a changed pixel is red in the third panel
    assert out.getpixel((2 * (60 + 8) + 31, 18 + 13)) == (220, 0, 0)
