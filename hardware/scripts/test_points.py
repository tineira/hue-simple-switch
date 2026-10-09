"""Test-point map for TESTING.md: hardware/images/board-test-points.png.

Draws callouts on the top render (images/board-top.png) for every point the
multimeter checks use, with the pad positions read from the PCB. The render
has no pixel scale in it, so the mm -> pixel mapping below was measured on it;
check() fails if a re-render moves the board, so the callouts never point at
the wrong place silently.

Run with KiCad's Python (it has pcbnew and Pillow); build_all.py calls it
after the renders.
"""
import os
import sys

import pcbnew
from PIL import Image, ImageChops, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
HW = os.path.normpath(os.path.join(HERE, ".."))
PCB = os.path.join(HW, "kicad", "hue-simple-switch-mains.kicad_pcb")
RENDER = os.path.join(HW, "images", "board-top.png")
OUT = os.path.join(HW, "images", "board-test-points.png")

# board-top.png: pixel = O + S * mm, mm from the centre of the board outline (y down).
S, OX, OY = 18.49, 586.0, 493.5
CROP = (190, 0, 990, 984)          # the board in the render
MARGIN = 330                        # label columns, left and right
BOARD_R = 26.05                     # outline radius, mm
BOARD_HALF_W = 21.05                # the two flat sides, mm

MAINS = (192, 57, 43)
LOW = (31, 111, 178)
XIAO = (196, 120, 14)
GROUND = (246, 247, 244)

# (label, ref, pad, colour). Every point named in TESTING.md, "Finding the test points".
# Pads too close together for callouts get a small tag on the board instead:
# (label, ref, pad, -1 above / +1 below). The XIAO pads are 2.54 mm apart.
INLINE = [("5V", "U1", "14", -1), ("GND", "U1", "13", -1), ("3V3", "U1", "12", -1)] +          [(f"D{i}", "U1", str(i + 1), 1) for i in range(6)] +          [("K", "D1", "1", 1), ("A", "D1", "2", 1)]
POINTS = [
    ("J1 L", "J1", "1", MAINS),
    ("J1 N", "J1", "2", MAINS),
    ("F1, J1 side", "F1", "1", MAINS),
    ("F1, PS1 side", "F1", "2", MAINS),
    ("PS1 AC L", "PS1", "1", MAINS),
    ("PS1 AC N", "PS1", "2", MAINS),
    ("PS1 -Vo (GND)", "PS1", "3", LOW),
    ("PS1 +Vo (5 V)", "PS1", "4", LOW),
    ("J2 D0", "J2", "1", LOW),
    ("J2 D1", "J2", "2", LOW),
    ("J2 D2", "J2", "3", LOW),
    ("J2 D3", "J2", "4", LOW),
    ("J3 D4", "J3", "1", LOW),
    ("J3 D5", "J3", "2", LOW),
    ("J3 GND", "J3", "3", LOW),
]


def pads():
    board = pcbnew.LoadBoard(PCB)
    box = board.GetBoardEdgesBoundingBox()
    cx, cy = pcbnew.ToMM(box.GetCenter().x), pcbnew.ToMM(box.GetCenter().y)
    fps = {fp.GetReference(): fp for fp in board.GetFootprints()}
    out = {}
    for ref, pad in [(r, p) for _, r, p, _ in POINTS] + [(r, p) for _, r, p, _ in INLINE]:
        p = fps[ref].FindPadByNumber(pad).GetPosition()
        out[(ref, pad)] = (pcbnew.ToMM(p.x) - cx, pcbnew.ToMM(p.y) - cy)
    return out


def px(mm):
    return OX + S * mm[0], OY + S * mm[1]


def check(img, at):
    """Every through-hole pad must land on its gold ring in the render."""
    bad = []
    for ref, pad in (("J1", "1"), ("J1", "2"), ("J2", "4"), ("J3", "3"), ("PS1", "1"), ("PS1", "4")):
        x, y = px(at[(ref, pad)])
        ring = [img.getpixel((round(x + dx), round(y + dy)))
                for dx, dy in ((16, 0), (-16, 0), (0, 16), (0, -16))]
        if sum(1 for r, g, b in ring if r > 170 and g > 140 and b < 110) < 3:
            bad.append(f"{ref}.{pad}")
    if bad:
        raise SystemExit("test_points: board-top.png moved (pads off their rings: "
                         + ", ".join(bad) + "); measure S, OX, OY again")


def font(size):
    for name in ("DejaVuSans.ttf", "arial.ttf"):
        try:
            return ImageFont.truetype(name, size)
        except OSError:
            pass
    return ImageFont.load_default(size)


def spread(items, top, bottom, h, gap):
    """Give each point a label slot near its height, slots pushed apart so none overlap.

    Points in one row (same height) take the slots nearest the margin first: with
    the slots at or below the row, the leaders then fan out without crossing.
    """
    items.sort(key=lambda it: (round(it["y"] / 8), it["dist"]))
    slots, y = [], top
    for it in items:
        slots.append(max(it["y"] - h / 2, y))
        y = slots[-1] + h + gap
    y = bottom
    for k in reversed(range(len(slots))):
        slots[k] = min(slots[k], y - h)
        y = slots[k] - gap
    for it, ly in zip(items, slots):
        it["ly"] = ly


def main():
    render = Image.open(RENDER).convert("RGB")
    at = pads()
    check(render, at)

    x0, y0, x1, y1 = CROP
    W, H = (x1 - x0) + 2 * MARGIN, (y1 - y0)
    shift = MARGIN - x0
    bx = OX + shift                                  # board centre, x, on the canvas

    # The render inside the board outline (a disc with two flat sides), plain ground around it.
    r, hw = BOARD_R * S, BOARD_HALF_W * S
    disc = Image.new("L", (W, H), 0)
    ImageDraw.Draw(disc).ellipse((bx - r, OY - r, bx + r, OY + r), fill=255)
    flats = Image.new("L", (W, H), 0)
    ImageDraw.Draw(flats).rectangle((bx - hw, 0, bx + hw, H), fill=255)
    outline = ImageChops.darker(disc, flats)
    board = Image.new("RGB", (W, H), GROUND)
    board.paste(render.crop(CROP), (MARGIN, 0))
    img = Image.composite(board, Image.new("RGB", (W, H), GROUND), outline)

    # Mains area: above the isolation slot, plus the J1 / F1 corner.
    zone = Image.new("L", (W, H), 0)
    zpts = [(-22, -10.1), (-1.7, -10.1), (-1.7, -16.5), (12.3, -16.5), (12.3, -4.7), (22, -4.7), (22, -27), (-22, -27)]
    ImageDraw.Draw(zone).polygon([(px(p)[0] + shift, px(p)[1]) for p in zpts], fill=70)
    zone = ImageChops.darker(zone, outline)
    img = Image.composite(Image.new("RGB", (W, H), MAINS), img, zone)

    d = ImageDraw.Draw(img)
    f, fs, fb = font(22), font(17), font(26)
    d.text((bx + 120, 62), "230 V", font=fb, fill=(255, 255, 255), anchor="mm",
           stroke_width=4, stroke_fill=MAINS)

    h, gap = 32, 6
    sides = {"l": [], "r": []}
    for label, ref, pad, col in POINTS:
        x, y = px(at[(ref, pad)])
        left = at[(ref, pad)][0] < 0
        sides["l" if left else "r"].append({"label": label, "x": x + shift, "y": y, "col": col,
                                             "dist": (x + shift - MARGIN) if left else (W - MARGIN - x - shift)})

    marks = []
    for side, items in sides.items():
        kx = MARGIN + 6 if side == "l" else W - MARGIN - 6
        spread(items, 12, H - 12, h, gap)
        for it in items:
            w = d.textlength(it["label"], font=f) + 20
            lx = MARGIN - 24 - w if side == "l" else W - MARGIN + 24
            ly = it["ly"]
            ex = lx + w if side == "l" else lx
            # Leader: level out of the label to the board edge, then straight to the point.
            d.line([(ex, ly + h / 2), (kx, ly + h / 2), (it["x"], it["y"])], fill=it["col"], width=3)
            d.rounded_rectangle((lx, ly, lx + w, ly + h), radius=6, fill=it["col"])
            d.text((lx + 10, ly + h / 2), it["label"], font=f, fill=(255, 255, 255), anchor="lm")
            marks.append((it["x"], it["y"], it["col"]))

    for label, ref, pad, direction in INLINE:
        x, y = px(at[(ref, pad)])
        x += shift
        ty = y + direction * 30
        col = XIAO if ref == "U1" else LOW
        w = d.textlength(label, font=fs) + 8
        d.rounded_rectangle((x - w / 2, ty - 11, x + w / 2, ty + 11), radius=4, fill=col)
        d.text((x, ty), label, font=fs, fill=(255, 255, 255), anchor="mm")
        marks.append((x, y, col))

    for x, y, c in marks:
        d.ellipse((x - 13, y - 13, x + 13, y + 13), outline=(255, 255, 255), width=6)
        d.ellipse((x - 13, y - 13, x + 13, y + 13), outline=c, width=3)
        d.ellipse((x - 4, y - 4, x + 4, y + 4), fill=c)

    img.save(OUT, optimize=True)
    print("wrote", os.path.relpath(OUT, HW))


if __name__ == "__main__":
    sys.exit(main())
