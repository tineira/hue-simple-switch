"""3-up assembly panel for JLCPCB: hardware/kicad/hue-simple-switch-mains-panel.kicad_pcb.

Three copies of the board built by build_pcb.populate(), side by side, held by
a frame on routed tabs with mouse bites. The frame is the assembly line's
edge rails (the single board is under JLCPCB's 70 x 70 mm minimum), and it
carries the fiducials and tooling holes. Order 5 panels = 15 boards.

Run with KiCad's Python; build_all.py calls it and exports the panel's
Gerbers, BOM and CPL into hardware/fab/jlcpcb/.
"""
import math
import os
import sys

import pcbnew

sys.path.insert(0, os.path.dirname(__file__))
import build_pcb  # noqa: E402
import design  # noqa: E402

PANEL_PATH = os.path.join(build_pcb.KICAD_DIR, "hue-simple-switch-mains-panel.kicad_pcb")

N = 3                     # boards per panel, in a row
GAP = 2.0                 # routed channel around each board (router bit Ø2)
PITCH = 2 * design.BOARD_HALF_W + GAP
RAIL = 7.5                # top and bottom rails (conveyor edges)
SIDE = 5.0                # left and right frame strips
TAB_W = 3.0
BITE_D, BITE_PITCH = 0.5, 1.0
CX, CY = 150.0, 100.0     # panel centre on the sheet

# Tabs, as x offsets from each board's centre, on the round ends (top y<0, bottom y>0).
# Chosen away from RV1, the terminals' wire entries and the mains copper.
TABS_TOP = [-16.0, 13.0]
TABS_BOTTOM = [-9.0, 9.0]
# Side tabs across the straight sides, tying each board to its neighbour and the
# outer boards to the side frame. y = -3 is the only free spot on the right edge
# (between J1's and J2's bodies); on the left edge it is clear of the XIAO's pads.
SIDE_TAB_Y = -3.0

IN_X = N * PITCH / 2 - GAP / 2 + GAP          # inner frame edge, x
IN_Y = design.BOARD_R + GAP                   # inner rail edge, y
OUT_X = IN_X + SIDE
OUT_Y = IN_Y + RAIL


def mm(v):
    return pcbnew.FromMM(v)


def P(x, y):
    return pcbnew.VECTOR2I(mm(CX + x), mm(CY + y))


def board_centres():
    return [((i - (N - 1) / 2) * PITCH, 0.0) for i in range(N)]


def poly_from(points):
    ps = pcbnew.SHAPE_POLY_SET()
    ps.NewOutline()
    for x, y in points:
        ps.Append(mm(CX + x), mm(CY + y))
    return ps


def board_points(cx, cy, n=90):
    r, w = design.BOARD_R, design.BOARD_HALF_W
    a0 = math.atan2(math.sqrt(r * r - w * w), w)
    pts = []
    for sgn in (1, -1):
        for i in range(n + 1):
            a = a0 + (math.pi - 2 * a0) * i / n
            x, y = r * math.cos(a), r * math.sin(a)
            pts.append((cx + (x if sgn > 0 else -x), cy + y * sgn))
    return pts


def side_gaps():
    """(x_left, x_right) of each channel along the straight sides: frame-board,
    board-board, board-frame."""
    w = design.BOARD_HALF_W
    xs = [bx for bx, _ in board_centres()]
    gaps = [(-IN_X, xs[0] - w)]
    gaps += [(a + w, b - w) for a, b in zip(xs, xs[1:])]
    gaps.append((xs[-1] + w, IN_X))
    return gaps


def arc_y(x):
    return math.sqrt(design.BOARD_R ** 2 - x ** 2)


def edge_cuts(board, ps):
    """Every contour of a SHAPE_POLY_SET as Edge.Cuts segments."""
    for oi in range(ps.OutlineCount()):
        contours = [ps.Outline(oi)] + [ps.Hole(oi, h) for h in range(ps.HoleCount(oi))]
        for c in contours:
            n = c.PointCount()
            for k in range(n):
                a, b = c.CPoint(k), c.CPoint((k + 1) % n)
                s = pcbnew.PCB_SHAPE(board)
                s.SetShape(pcbnew.SHAPE_T_SEGMENT)
                s.SetLayer(pcbnew.Edge_Cuts)
                s.SetWidth(mm(0.05))
                s.SetStart(pcbnew.VECTOR2I(a.x, a.y))
                s.SetEnd(pcbnew.VECTOR2I(b.x, b.y))
                board.Add(s)


def npth(board, x, y, d, ref):
    fp = pcbnew.FOOTPRINT(board)
    fp.SetReference(ref)
    fp.Reference().SetVisible(False)
    fp.Value().SetVisible(False)
    fp.SetAttributes(pcbnew.FP_EXCLUDE_FROM_BOM | pcbnew.FP_EXCLUDE_FROM_POS_FILES | pcbnew.FP_BOARD_ONLY)
    pad = pcbnew.PAD(fp)
    pad.SetAttribute(pcbnew.PAD_ATTRIB_NPTH)
    pad.SetShape(pcbnew.PAD_SHAPE_CIRCLE)
    pad.SetSize(pcbnew.VECTOR2I(mm(d), mm(d)))
    pad.SetDrillSize(pcbnew.VECTOR2I(mm(d), mm(d)))
    pad.SetLayerSet(pad.UnplatedHoleMask())
    fp.Add(pad)
    fp.SetPosition(P(x, y))
    board.Add(fp)


def fiducial(board, x, y, ref):
    path = os.path.join(build_pcb.FP_ROOT, "Fiducial.pretty")
    fp = pcbnew.FootprintLoad(path, "Fiducial_1mm_Mask2mm")
    fp.SetReference(ref)
    fp.Reference().SetVisible(False)
    fp.Value().SetVisible(False)
    fp.SetAttributes(fp.GetAttributes() | pcbnew.FP_EXCLUDE_FROM_BOM | pcbnew.FP_BOARD_ONLY)
    fp.SetPosition(P(x, y))
    board.Add(fp)


def build():
    board = build_pcb.new_board()
    copies = []
    for i, (bx, by) in enumerate(board_centres()):
        fps, nets = build_pcb.populate(board, CX + bx, CY + by, suffix=f"_b{i + 1}", outline=False)
        copies.append(fps)

    # material = frame rectangle, minus the routed channel around each board, plus tabs
    frame = poly_from([(-OUT_X, -OUT_Y), (OUT_X, -OUT_Y), (OUT_X, OUT_Y), (-OUT_X, OUT_Y)])
    channel = poly_from([(-IN_X, -IN_Y), (IN_X, -IN_Y), (IN_X, IN_Y), (-IN_X, IN_Y)])
    for bx, by in board_centres():
        channel.BooleanSubtract(poly_from(board_points(bx, by)))
        for tx, sgn in [(t, -1) for t in TABS_TOP] + [(t, 1) for t in TABS_BOTTOM]:
            x0, x1 = bx + tx - TAB_W / 2, bx + tx + TAB_W / 2
            y_edge = min(arc_y(x0 - bx), arc_y(x1 - bx)) - 0.5
            channel.BooleanSubtract(poly_from([(x0, sgn * y_edge), (x1, sgn * y_edge),
                                               (x1, sgn * (IN_Y + 0.5)), (x0, sgn * (IN_Y + 0.5))]))
    y0, y1 = SIDE_TAB_Y - TAB_W / 2, SIDE_TAB_Y + TAB_W / 2
    for xa, xb in side_gaps():
        channel.BooleanSubtract(poly_from([(xa - 0.5, y0), (xb + 0.5, y0), (xb + 0.5, y1), (xa - 0.5, y1)]))
    frame.BooleanSubtract(channel)
    edge_cuts(board, frame)

    # mouse bites: a row of NPTH holes on the board edge across each tab
    k = 0
    for bx, by in board_centres():
        for tx, sgn in [(t, -1) for t in TABS_TOP] + [(t, 1) for t in TABS_BOTTOM]:
            nh = int(TAB_W / BITE_PITCH) + 1
            for j in range(nh):
                dx = (j - (nh - 1) / 2) * BITE_PITCH
                x = tx + dx
                y = sgn * (arc_y(x) + 0.1)   # hole centre just outside the board edge
                k += 1
                npth(board, bx + x, y, BITE_D, f"MB{k}")
    # side tabs: a column of holes at every board edge the tab touches
    w = design.BOARD_HALF_W
    edges = [bx + s * (w + 0.1) for bx, _ in board_centres() for s in (-1, 1)]
    nh = int(TAB_W / BITE_PITCH) + 1
    for x in edges:
        for j in range(nh):
            k += 1
            npth(board, x, SIDE_TAB_Y + (j - (nh - 1) / 2) * BITE_PITCH, BITE_D, f"MB{k}")

    # fiducials (3, asymmetric) and tooling holes (4) on the rails
    ry = IN_Y + RAIL / 2
    fiducial(board, -OUT_X + 12, -ry, "FID1")
    fiducial(board, OUT_X - 12, -ry, "FID2")
    fiducial(board, -OUT_X + 12, ry, "FID3")
    for i, (x, y) in enumerate([(-OUT_X + 4, -ry), (OUT_X - 4, -ry), (-OUT_X + 4, ry), (OUT_X - 4, ry)]):
        npth(board, x, y, 2.0, f"TH{i + 1}")

    t = pcbnew.PCB_TEXT(board)
    t.SetText("hue-simple-switch mains carrier - 3 per panel - JLCJLCJLCJLC")
    t.SetPosition(P(0, ry))
    t.SetLayer(pcbnew.F_SilkS)
    t.SetTextSize(pcbnew.VECTOR2I(mm(1.2), mm(1.2)))
    t.SetTextThickness(mm(0.18))
    board.Add(t)

    ds = board.GetDesignSettings()
    ds.SetAuxOrigin(P(-OUT_X, OUT_Y))       # bottom-left corner: origin of Gerbers and CPL
    board.Save(PANEL_PATH)
    build_pcb.write_project_rules(PANEL_PATH)
    return board, copies


def origin():
    return (CX - OUT_X, CY + OUT_Y)


if __name__ == "__main__":
    build()
    print("wrote", PANEL_PATH, f"{2 * OUT_X:.1f} x {2 * OUT_Y:.1f} mm")
