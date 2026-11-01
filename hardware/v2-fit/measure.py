"""Measure the saved board, independently of DRC: reads v2-fit.kicad_pcb and writes
reports/measure.json. Distances are from copper shapes in the file; the part bodies
standing above the board use the same numbers build.py gave the 3D models."""
import itertools
import json
import math
import os
import sys

import pcbnew

sys.dont_write_bytecode = True
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import build  # noqa: E402

OX, OY = 100.0, 100.0


def P(v):
    return (pcbnew.ToMM(v.x) - OX, pcbnew.ToMM(v.y) - OY)


def seg_seg(a, b, c, d):
    def pseg(p, u, v):
        vx, vy = v[0] - u[0], v[1] - u[1]
        L = vx * vx + vy * vy
        t = 0 if L == 0 else max(0, min(1, ((p[0] - u[0]) * vx + (p[1] - u[1]) * vy) / L))
        return math.hypot(p[0] - u[0] - t * vx, p[1] - u[1] - t * vy)

    def cross(o, p, q):
        return (p[0] - o[0]) * (q[1] - o[1]) - (p[1] - o[1]) * (q[0] - o[0])
    if a != b and c != d and cross(a, b, c) * cross(a, b, d) < 0 and cross(c, d, a) * cross(c, d, b) < 0:
        return 0.0
    return min(pseg(a, c, d), pseg(b, c, d), pseg(c, a, b), pseg(d, a, b))


def shapes(board):
    """Every copper item as (label, net, kind, layers, segments, radius).
    Rectangular pads become a set of edges with radius 0."""
    out = []
    for fp in board.GetFootprints():
        for pad in fp.Pads():
            c = P(pad.GetPosition())
            sz = pad.GetSize(pad.GetLayer() if pad.IsOnLayer(pad.GetLayer()) else pcbnew.F_Cu)
            w, h = pcbnew.ToMM(sz.x), pcbnew.ToMM(sz.y)
            th = pad.GetAttribute() == pcbnew.PAD_ATTRIB_PTH
            layers = ("F", "B") if th else ("B" if fp.IsFlipped() else "F",)
            label = "%s.%s" % (fp.GetReference(), pad.GetNumber())
            if abs(w - h) < 1e-6 and th:
                out.append((label, pad.GetNetname(), "pad", layers, [(c, c)], w / 2))
            else:
                a = math.radians(pad.GetOrientationDegrees())
                ca, sa = math.cos(a), math.sin(a)
                cs = [(c[0] + x * ca + y * sa, c[1] - x * sa + y * ca)
                      for x, y in ((-w / 2, -h / 2), (w / 2, -h / 2), (w / 2, h / 2), (-w / 2, h / 2))]
                out.append((label, pad.GetNetname(), "pad", layers,
                            list(zip(cs, cs[1:] + cs[:1])), 0.0))
    for t in board.GetTracks():
        if t.GetClass() == "PCB_VIA":
            continue
        lay = "F" if t.GetLayer() == pcbnew.F_Cu else "B"
        s, e = P(t.GetStart()), P(t.GetEnd())
        out.append(("track %s %s" % (t.GetNetname(), lay), t.GetNetname(), "track", (lay,), [(s, e)],
                    pcbnew.ToMM(t.GetWidth()) / 2))
    return out


def dist(a, b):
    d = min(seg_seg(p, q, r, s) for (p, q), (r, s) in itertools.product(a[4], b[4]))
    return d - a[5] - b[5]


def main():
    board = pcbnew.LoadBoard(build.PCB)
    items = shapes(board)
    mains = [i for i in items if i[1].startswith("AC_")]
    others = [i for i in items if not i[1].startswith("AC_")]     # copper with no net counts: J2's tabs
    rows = []
    for m, o in itertools.product(mains, others):
        same = set(m[3]) & set(o[3])
        rows.append((round(dist(m, o), 3), m[0], o[0], "same layer" if same else "other layer, in plan"))
    best = {}
    for r in sorted(rows):
        best.setdefault((r[1].split(" ")[0] + r[1].split(" ")[-1], r[2]), r)
    flat = sorted(best.values())

    x0, y0, x1, y1 = build.BOARD
    edge = []
    for m in mains:
        for (p, q) in m[4]:
            for pt in (p, q):
                edge.append((round(min(pt[0] - x0, x1 - pt[0], pt[1] - y0, y1 - pt[1]) - m[5], 3), m[0]))

    # slot: nearest mains copper to its edges
    (sx0, sx1), (sy0, sy1) = build.SLOT_X, build.SLOT_Y

    def slot_d(pt):
        return math.hypot(max(sx0 - pt[0], 0, pt[0] - sx1), max(sy0 - pt[1], 0, pt[1] - sy1))
    mains_slot = []
    for i in mains:
        for (p, q) in i[4]:
            n = max(2, int(math.dist(p, q) / 0.05))
            for k in range(n + 1):
                pt = (p[0] + (q[0] - p[0]) * k / n, p[1] + (q[1] - p[1]) * k / n)
                mains_slot.append((round(slot_d(pt) - i[5], 3), i[0]))

    # ---------------- 3D, z = 0 at the board's top face
    z = build.XIAO_Z
    def rect_gap(a, b):
        dx = max(a[0] - b[2], b[0] - a[2], 0)
        dy = max(a[1] - b[3], b[1] - a[3], 0)
        return dx, dy

    cx, cy = build.X_C
    xiao = (cx - 10.52, cy - 8.89, cx + 10.52, cy + 8.89)
    usb = (cx + 4.47, cy - 4.47, cx + 11.82, cy + 4.47)
    rows = {"socket D0-D5": (build.ROW_A[-1][1] - 1.27, build.ROW_A[0][2] - 1.25, build.ROW_A[0][1] + 1.27, build.ROW_A[0][2] + 1.25),
            "socket 5V GND": (build.ROW_B[1][1] - 1.27, build.ROW_B[0][2] - 1.25, build.ROW_B[0][1] + 1.27, build.ROW_B[0][2] + 1.25)}
    f = (build.F1_C[0] - 1.56, build.F1_C[1] - 5.05, build.F1_C[0] + 1.56, build.F1_C[1] + 5.05)
    rv = (build.RV_C[0] - 4, build.RV_C[1] - 3.15, build.RV_C[0] + 4, build.RV_C[1] + 3.15)
    d3 = {}

    def rect_gap(a, b):
        return max(a[0] - b[2], b[0] - a[2], 0), max(a[1] - b[3], b[1] - a[3], 0)
    for nm, body, h in (("F1 body", f, 3.12), ("RV1 body", rv, 4.7)):
        for other, b, zb in (("XIAO board", xiao, z), ("USB-C", usb, z + build.XIAO_T)):
            dx, dy = rect_gap(body, b)
            d3[nm + " to " + other] = round(math.hypot(dx, dy, zb - h), 3)
        for other, b in rows.items():
            dx, dy = rect_gap(body, b)
            d3[nm + " to " + other] = round(math.hypot(dx, dy), 3)
    ta = build.TERM_AT
    j = (ta[0] - 4.25, ta[1] - 8.975, ta[0] + 0.75, ta[1] + 8.975)
    d3["J2 body to socket D0-D5, plan"] = rect_gap(j, rows["socket D0-D5"])
    d3["J2 body to socket 5V GND, plan"] = rect_gap(j, rows["socket 5V GND"])
    d3["room between mated J2 and XIAO"] = round(z - build.TERM_H, 3)
    d3["J2 wire entry to XIAO left edge, plan"] = round(ta[0] - 1.75 - xiao[0], 3)
    d3["J2 wire entry to XIAO lower edge, plan"] = round(xiao[3] - ta[1], 3)
    d3["USB margin"] = round(build.TOP_Z - z - build.XIAO_T - build.USB_H, 3)
    x1, y1 = -18.0, 0
    pads3 = []
    for mi in mains:
        for (p, q) in mi[4]:
            for pt in (p, q):
                dx = max(xiao[0] - pt[0], 0, pt[0] - xiao[2])
                dy = max(xiao[1] - pt[1], 0, pt[1] - xiao[3])
                pads3.append(math.hypot(math.hypot(dx, dy), z))
    d3["mains copper on the board to XIAO underside"] = round(min(pads3), 3)

    stack = dict(ps1_bottom=0.0, ps1_top=15.0, board_bottom=15 + build.LIFT,
                 board_top=15 + build.LIFT + build.BOARD_T)
    t = stack["board_top"]
    stack.update(j2_top=t + build.TERM_H, f1_top=t + 3.12, rv1_top=t + 4.7, socket_top=t + build.SOCKET_H,
                 xiao_bottom=t + z, xiao_top=t + z + build.XIAO_T, header_tail_top=t + z + build.HEADER_TAIL,
                 usb_top=t + z + build.XIAO_T + build.USB_H, box_top=36.0)
    stack["tail_gap_to_ps1"] = None
    stack["ps1_pin_above_top"] = build.PIN - build.LIFT - build.BOARD_T
    res = dict(smallest_copper=flat[:14], mains_to_edge=sorted(edge)[:3], mains_to_slot=sorted(mains_slot)[:3], d3=d3, stack=stack)
    with open(os.path.join(build.REPORTS, "measure.json"), "w") as f:
        json.dump(res, f, indent=1)
    print(json.dumps(res, indent=1))


if __name__ == "__main__":
    main()
