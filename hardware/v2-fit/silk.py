"""Silkscreen art for the v2 board and its panel frame, in the look of hue.tineira.com: blueprint
grid, mono labels, numbered circles, and the ring logo from the console's app/icon.svg.

Lines and text stay at JLCPCB's minimum or above: 0.15 wide, text 1.0 tall. Everything is drawn in
one colour, so the logo's grey ring is the thin circle and its amber arc the thick one. Reference
text is hidden on the footprints and drawn here, so the panel never shows F1_1 or J2_3.
"""
import math

import pcbnew

W = 0.15
F, B = pcbnew.F_SilkS, pcbnew.B_SilkS
Y_PAD = 0.2     # silk stays this far from a pad, a via or a hole

# J2 pin labels, left of the housing, one per contact (pin 7 is GND)
J2_X = -4.55
J2_ROWS = [("D0", 8.1), ("D1", 6.1), ("D2", 4.1), ("D3", 2.1), ("D4", 0.1), ("D5", -1.9), ("G", -3.9)]
J2_BODY = (-2.65, -6.875, 2.35, 11.075)

OUTLINES = {"F1": (-11.06, -0.55, -7.94, 9.55), "RV1": (-16.85, -11.55, -8.85, -5.25), "J2": J2_BODY,
            "U1A": (-0.02, -9.7, 15.74, -7.2), "U1B": (10.14, 5.54, 15.72, 8.04)}
LABELS = {"F1": (-12.2, 3.0), "RV1": (-15.3, -4.4), "J2": (-4.55, 9.9), "D1": (15.0, -1.7), "C1": (12.7, -1.9),
          "C2": (16.75, 3.95)}


def mm(v):
    return pcbnew.FromMM(v)


def shape(b, T, kind, layer, w):
    s = pcbnew.PCB_SHAPE(b)
    s.SetShape(kind)
    s.SetLayer(layer)
    s.SetWidth(mm(w))
    b.Add(s)
    return s


def line(b, T, a, c, layer=F, w=W):
    s = shape(b, T, pcbnew.SHAPE_T_SEGMENT, layer, w)
    s.SetStart(T(*a))
    s.SetEnd(T(*c))


def path(b, T, pts, layer=F, w=W):
    for a, c in zip(pts, pts[1:]):
        line(b, T, a, c, layer, w)


def circle(b, T, at, r, layer=F, w=W, filled=False):
    s = shape(b, T, pcbnew.SHAPE_T_CIRCLE, layer, w)
    s.SetStart(T(*at))
    s.SetEnd(T(at[0] + r, at[1]))
    s.SetFilled(filled)


def arc(b, T, at, r, a0, a1, layer=F, w=W):
    n = max(6, int(abs(a1 - a0) / 8))
    pts = [(at[0] + r * math.cos(math.radians(a0 + (a1 - a0) * i / n)),
            at[1] + r * math.sin(math.radians(a0 + (a1 - a0) * i / n))) for i in range(n + 1)]
    path(b, T, pts, layer, w)


def text(b, T, s, at, size=1.0, rot=0, layer=F, left=False, bold=False):
    t = pcbnew.PCB_TEXT(b)
    t.SetText(s)
    t.SetPosition(T(*at))
    t.SetLayer(layer)
    t.SetTextSize(pcbnew.VECTOR2I(mm(size), mm(size)))
    t.SetTextThickness(mm(max(W, size * (0.2 if bold else 0.15))))
    t.SetTextAngleDegrees(rot)
    if layer == B:
        t.SetMirrored(True)
    if left:
        t.SetHorizJustify(pcbnew.GR_TEXT_H_ALIGN_LEFT)
    b.Add(t)


def logo(b, T, at, r, layer=F):
    """app/icon.svg: a ring, an arc from 135 to 330 degrees, a dot."""
    circle(b, T, at, r, layer)
    arc(b, T, at, r, 135, 330, layer, max(W, r * 9 / 21))
    circle(b, T, at, r * 11 / 21, layer, W, True)


def tag(b, T, s, at, r=1.0, layer=F, size=1.0):
    """The site's numbered circle."""
    circle(b, T, at, r, layer)
    text(b, T, s, at, size, 0, layer, bold=True)


def bolt(b, T, at, side, layer=F):
    """Mains warning: triangle and lightning bolt."""
    x, y = at
    h = side * 0.866
    w = 0.2
    path(b, T, [(x, y - h / 2), (x + side / 2, y + h / 2), (x - side / 2, y + h / 2), (x, y - h / 2)], layer, w)
    s = side / 4
    path(b, T, [(x + s * .35, y - s * .55), (x - s * .4, y + s * .4), (x + s * .4, y + s * .3), (x - s * .35, y + s * 1.25)],
         layer, w)


def split(a, c, rects):
    """Parts of an axis-aligned segment left outside the rectangles."""
    (x0, y0), (x1, y1) = a, c
    horiz = y0 == y1
    lo, hi = (min(x0, x1), max(x0, x1)) if horiz else (min(y0, y1), max(y0, y1))
    fixed = y0 if horiz else x0
    cuts = []
    for rx0, ry0, rx1, ry1 in rects:
        f0, f1, s0, s1 = (ry0, ry1, rx0, rx1) if horiz else (rx0, rx1, ry0, ry1)
        if f0 < fixed < f1 and s0 < hi and s1 > lo:
            cuts.append((max(s0, lo), min(s1, hi)))
    out, at = [], lo
    for s0, s1 in sorted(cuts):
        if s0 > at:
            out.append((at, s0))
        at = max(at, s1)
    if at < hi:
        out.append((at, hi))
    return [((p, fixed), (q, fixed)) if horiz else ((fixed, p), (fixed, q)) for p, q in out if q - p > 0.3]


def outline(b, T, r, avoid, layer=F):
    x0, y0, x1, y1 = r
    for a, c in (((x0, y0), (x1, y0)), ((x1, y0), (x1, y1)), ((x0, y1), (x1, y1)), ((x0, y0), (x0, y1))):
        for p, q in split(a, c, avoid):
            line(b, T, p, q, layer)


def copper_rects(b):
    """Pads and vias on the board, grown by Y_PAD, in board mm."""
    rects = []
    to = lambda v: pcbnew.ToMM(v) - 100.0
    for f in b.GetFootprints():
        for p in f.Pads():
            s = p.GetBoundingBox()
            rects.append((to(s.GetX()) - Y_PAD, to(s.GetY()) - Y_PAD, to(s.GetRight()) + Y_PAD, to(s.GetBottom()) + Y_PAD))
    for t in b.GetTracks():
        if t.Type() == pcbnew.PCB_VIA_T:
            c, r = t.GetPosition(), pcbnew.ToMM(t.GetWidth(pcbnew.F_Cu)) / 2 + Y_PAD
            rects.append((to(c.x) - r, to(c.y) - r, to(c.x) + r, to(c.y) + r))
    return rects


def board_art(b, T):
    """Everything on the board itself. T maps board mm to KiCad units."""
    for f in b.GetFootprints():
        for g in list(f.GraphicalItems()):
            if g.GetLayer() in (F, B):
                f.Remove(g)
        f.Reference().SetVisible(False)
    avoid = copper_rects(b)

    for name, r in OUTLINES.items():
        outline(b, T, r, avoid)
    for ref, at in LABELS.items():
        text(b, T, ref, at)

    # D1: body and cathode bar, on the side of pad 1 (+5V)
    d1 = next(f for f in b.GetFootprints() if f.GetReference() == "D1")
    p, q = d1.FindPadByNumber("1").GetPosition(), d1.GetPosition()
    side = -1 if p.y < q.y else 1
    dx, dy = pcbnew.ToMM(q.x) - 100, pcbnew.ToMM(q.y) - 100
    outline(b, T, (dx - 1.35, dy - 2.25, dx + 1.35, dy + 2.25), avoid)
    line(b, T, (dx - 1.35, dy + side * 0.6), (dx + 1.35, dy + side * 0.6), F, 0.3)

    # J2: what each contact is. Read with the XIAO out, when the wires go in
    for name, y in J2_ROWS:
        text(b, T, name, (J2_X, y), bold=name == "G")
        line(b, T, (J2_X + 1.0, y), (J2_BODY[0] - 0.1, y))

    # mains side
    tag(b, T, "L", (-16.6, 10.55), 0.95)
    tag(b, T, "N", (-14.1, 3.0), 0.95)
    bolt(b, T, (-13.2, 5.9), 3.4)

    # the visible strip past the XIAO's end
    logo(b, T, (3.9, 10.55), 0.8)
    text(b, T, "HUE SWITCH", (5.2, 10.6), left=True, bold=True)
    text(b, T, "V2", (16.2, 10.6), left=True)

    # seen only with the XIAO out
    text(b, T, "HI :)", (13.5, 0.5), 1.2)

    # how the XIAO goes in: the USB end, the two power pins and where its corners land
    text(b, T, "USB", (12.1, 2.4), bold=True)
    path(b, T, [(13.8, 2.4), (15.6, 2.4)])
    path(b, T, [(15.0, 1.85), (15.6, 2.4), (15.0, 2.95)])
    text(b, T, "5V", (14.2, 4.65), bold=True)
    text(b, T, "GND", (11.66, 8.95), bold=True)
    path(b, T, [(15.9, 8.06), (17.1, 8.06), (17.1, 7.3)])
    path(b, T, [(17.1, -8.15), (17.1, -7.35)])     # PS1's +5V pin sits on the corner itself

    # bottom: the strip beside PS1, the only marking left once the rails are off
    path(b, T, [(-14.7, 8.35), (-7.0, 8.35)], B)
    path(b, T, [(-5.5, 8.35), (18.7, 8.35)], B)
    bolt(b, T, (-13.3, 10.1), 2.4, B)
    text(b, T, "220V AC", (-8.8, 10.45), layer=B)
    text(b, T, "UNCERTIFIED PROTOTYPE", (6.8, 10.45), layer=B)


def frame_art(b, T, half_w, half_h, in_x, in_y, rx, fid_y, hole_y, board_ys):
    """The panel's rails. Coordinates about the panel centre; all of this snaps off."""
    s_x, s_y = in_x + 0.5, in_y
    keep = [(-rx - 2.5, -fid_y - 2.5, -rx + 2.5, -fid_y + 2.5), (rx - 2.5, -fid_y - 2.5, rx + 2.5, -fid_y + 2.5),
            (-rx - 2.5, fid_y - 2.5, -rx + 2.5, fid_y + 2.5)]
    keep += [(sx * rx - 2.2, sy * hole_y - 2.2, sx * rx + 2.2, sy * hole_y + 2.2) for sx in (-1, 1) for sy in (-1, 1)]

    # left rail: logo, name, the site's headline
    at = (-29.0, -21.5)
    logo(b, T, at, 4.2)
    keep.append((at[0] - 5, at[1] - 5, at[0] + 5, at[1] + 5))
    text(b, T, "HUE SWITCH", (-28.2, -14.6), 1.3, bold=True)
    text(b, T, "SIMPLE · V2", (-28.2, -12.6))
    keep.append((-34.6, -15.8, -21.5, -11.6))
    text(b, T, "BUILD A WALL SWITCH", (-30.2, 9.0), 1.5, 90, bold=True)
    text(b, T, "FOR YOUR HUE LIGHTS", (-27.6, 9.0), 1.5, 90, bold=True)
    keep.append((-31.8, -6.0, -26.0, 24.0))

    # a numbered circle and leader for each board
    for i, y in enumerate(board_ys):
        c = (-23.2, y + 4.5)
        tag(b, T, str(i + 1), c)
        line(b, T, (c[0] + 1.0, c[1]), (-s_x, c[1]))
        keep.append((c[0] - 1.5, c[1] - 1.5, -21.0, c[1] + 1.5))

    # right rail: the order text stays, plus the address and a side view at 1:3
    keep.append((rx - 1.4, -24.0, rx + 1.4, 24.0))
    text(b, T, "HUE.TINEIRA.COM", (31.9, 0.0), 1.0, 90)
    keep.append((31.0, -8.0, 32.8, 8.0))
    side_view(b, T, (rx, 29.6), keep)

    # top strip: a real ruler
    top = -half_h
    for k in range(0, int(2 * half_w) + 1):
        x = -half_w + k
        if k in (0, 70):
            continue
        n = 2.1 if k % 10 == 0 else 1.4 if k % 5 == 0 else 0.8
        for a, c in split((x, top + 0.3), (x, top + 0.3 + n), keep):
            line(b, T, a, c)
        if k % 10 == 0 and abs(abs(x) - rx) > 3:
            text(b, T, str(k), (x, top + 3.4))
    text(b, T, "MM", (rx - 4.5, top + 3.4))

    # bottom strip: the site's three steps
    y = half_h - 2.5
    for at, a, s in ((-14.5, "1", "INSTALL"), (0.0, "2", "PAIR"), (14.5, "3", "CHOOSE")):
        tag(b, T, a, (at - 5.0, y))
        text(b, T, s, (at - 3.4, y), left=True)
    path(b, T, [(-8.8, y), (-6.0, y)])
    path(b, T, [(5.0, y), (8.0, y)])

    # blueprint grid on the rails, broken around everything above
    edge = s_x + 0.2
    for sign in (-1, 1):
        lo, hi = sorted((sign * edge, sign * (half_w - 0.4)))
        k = int(lo // 5)
        while k * 5 <= hi:
            x = k * 5
            if lo <= x <= hi:
                for a, c in split((x, -half_h + 5.3), (x, half_h - 5.3), keep):
                    line(b, T, a, c)
            k += 1
        k = int((-half_h + 5.3) // 5)
        while k * 5 <= half_h - 5.3:
            for a, c in split((lo, k * 5), (hi, k * 5), keep):
                line(b, T, a, c)
            k += 1


def side_view(b, T, at, keep):
    """The stack in the box, to scale. x along the box, z up, from the numbers in build.py."""
    import build
    s = 1 / 3
    cx, cy = at
    H = 36.0

    def p(x, z):
        return (cx + x * s, cy + (H / 2 - z) * s)

    def box(x0, z0, x1, z1):
        a, c = p(x0, z0), p(x1, z1)
        path(b, T, [a, (c[0], a[1]), c, (a[0], c[1]), a])

    box(-19, 0, 19, H)
    t = 15 + build.BOARD_T
    box(-15, 0, 19, 15)
    box(-18.9, 15, 19, t)
    x0, x1 = build.X_C[0] - 10.52, build.X_C[0] + 10.52
    z = t + build.XIAO_Z
    box(x0, z, x1, z + build.XIAO_T)
    box(x1 - 7.35, z + build.XIAO_T, x1, z + build.XIAO_T + build.USB_H)
    box(0.0, t, 3.0, t + build.SOCKET_H)
    text(b, T, "SIDE · 1:3", (cx, cy + H / 2 * s + 1.2))
    keep.append((cx - 7.0, cy - H / 2 * s - 0.8, cx + 7.0, cy + H / 2 * s + 2.2))
