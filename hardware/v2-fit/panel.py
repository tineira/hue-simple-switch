"""3-up panel of the routed board: panel/v2-fit-panel.kicad_pcb, Gerbers, BOM and CPL.

Run with KiCad's Python after build.py. Same scheme as v1's build_panel.py: frame, routed
channels, tabs with mouse bites, fiducials and tooling holes. 5 panels = 15 boards. The boards
stack in y and the tabs are on the short sides only, away from the L and N wires.
"""
import csv
import glob
import os
import re
import shutil
import subprocess
import sys
import zipfile

import pcbnew

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "scripts"))
sys.dont_write_bytecode = True
import build_pcb  # noqa: E402
import design  # noqa: E402
sys.path.insert(0, HERE)
import silk  # noqa: E402

OUT = os.path.join(HERE, "panel")
NAME = "v2-fit-panel"
PCB = os.path.join(OUT, NAME + ".kicad_pcb")
CLI = os.path.join(os.path.dirname(sys.executable), "kicad-cli.exe")

X0, Y0, X1, Y1 = -18.9, -11.85, 19.0, 11.8     # board outline about the box centre
SLOT = (-6.75, -5.75, -9.0, 9.0)               # x0, x1, y0, y1
SRC_C = (100.0, 100.0)
W, H = X1 - X0, Y1 - Y0
N, GAP = 3, 2.0
PITCH = H + GAP
STRIP = 5.0                # top and bottom strips
OUT_X = 35.0               # 70 wide: JLCPCB's standard PCBA minimum
IN_X = W / 2 + GAP
IN_Y = (N * H + (N - 1) * GAP) / 2 + GAP
OUT_Y = IN_Y + STRIP
TAB_W = 5.0                # JLCPCB: 5 mm where stamp holes are used
BITE_D, BITE_PITCH, BITE_OUT = 0.5, 1.0, 0.25
# side (-1 left, 1 right), y about the box centre: clear of the wires, D1 and PS1's DC pins
TABS = [(-1, -2.0), (-1, -9.0), (1, -6.0), (1, 8.0)]
CX, CY = 150.0, 100.0
# reused or hand-soldered: PS1 and the two wires
HAND = {"PS1", "WL", "WN", "BOX1", "XM1"}
EXTRA = {"F1": ("500mA 600V Slo-Blo", "C206981"), "RV1": ("275VAC", "C600605"), "J2": ("JST B7B-PH-SM4-TB", "C495700"),
         "U1A": ("Socket 1x6 2.54 H8.5 YQ", "C46635842"), "U1B": ("Socket 1x2 2.54 H8.5 YQ", "C46635838")}
# PS1 for the preview files only: bottom side, rotated 180 as seen from below (LCSC footprint, top view)
PS1_PART = ("HLK-PM01", "C209903", "PWRM-TH_HLK-PM01")
SOCKET = {"U1A": {"D0", "D1", "D2", "D3", "D4", "D5"}, "U1B": {"5V", "GND"}}


def mm(v):
    return pcbnew.FromMM(v)


def P(x, y):
    return pcbnew.VECTOR2I(mm(CX + x), mm(CY + y))


def rect(x0, y0, x1, y1):
    ps = pcbnew.SHAPE_POLY_SET()
    ps.NewOutline()
    for x, y in ((x0, y0), (x1, y0), (x1, y1), (x0, y1)):
        ps.Append(mm(CX + x), mm(CY + y))
    return ps


def centres():
    return [(0.0, (i - (N - 1) / 2) * PITCH) for i in range(N)]


def dup(x):
    try:
        return x.Duplicate(False).Cast()
    except TypeError:
        return x.Duplicate().Cast()


def seg(b, a, c, width=0.05):
    s = pcbnew.PCB_SHAPE(b)
    s.SetShape(pcbnew.SHAPE_T_SEGMENT)
    s.SetLayer(pcbnew.Edge_Cuts)
    s.SetWidth(mm(width))
    s.SetStart(a)
    s.SetEnd(c)
    b.Add(s)


def npth(b, x, y, d, ref):
    fp = pcbnew.FOOTPRINT(b)
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
    b.Add(fp)


def fiducial(b, x, y, ref):
    fp = pcbnew.FootprintLoad(os.path.join(build_pcb.FP_ROOT, "Fiducial.pretty"), "Fiducial_1mm_Mask2mm")
    fp.SetReference(ref)
    fp.Reference().SetVisible(False)
    fp.Value().SetVisible(False)
    fp.SetAttributes(fp.GetAttributes() | pcbnew.FP_EXCLUDE_FROM_BOM | pcbnew.FP_BOARD_ONLY)
    fp.SetPosition(P(x, y))
    b.Add(fp)


def build():
    shutil.rmtree(OUT, ignore_errors=True)
    os.makedirs(OUT)
    for ext in ("kicad_pro", "kicad_dru"):
        shutil.copy(os.path.join(HERE, "v2-fit." + ext), os.path.join(OUT, NAME + "." + ext))
    b = pcbnew.LoadBoard(os.path.join(HERE, "v2-fit.kicad_pcb"))
    all_fps = list(b.GetFootprints())
    all_drw = list(b.GetDrawings())
    src_trk = list(b.GetTracks())
    src_zon = list(b.Zones())
    src_fps = [f for f in all_fps if f.GetReference() not in ("BOX1", "XM1")]
    skip = (pcbnew.Edge_Cuts, pcbnew.F_Fab, pcbnew.B_Fab)
    src_drw = [d for d in all_drw if d.GetLayer() not in skip]
    names = sorted({p.GetNetname() for f in src_fps for p in f.Pads() if p.GetNetname() and p.GetNetname() != "N/C"}
                   | {t.GetNetname() for t in src_trk} | {z.GetNetname() for z in src_zon if z.GetNetname()})
    nets = {}
    for i in range(N):
        for n in names:
            ni = pcbnew.NETINFO_ITEM(b, f"{n}_b{i + 1}")
            b.Add(ni)
            nets[(n, i)] = ni

    placed = []
    for i, (bx, by) in enumerate(centres()):
        d = pcbnew.VECTOR2I(mm(CX + bx - SRC_C[0]), mm(CY + by - SRC_C[1]))
        for f0 in src_fps:
            f = dup(f0)
            b.Add(f)
            f.Move(d)
            ref = f0.GetReference()
            for p in f.Pads():
                if p.GetNetname() and (p.GetNetname(), i) in nets:
                    p.SetNet(nets[(p.GetNetname(), i)])
            f.SetReference(f"{ref}_{i + 1}")
            placed.append((i, ref, f))
        for t0 in src_trk:
            t = dup(t0)
            b.Add(t)
            t.Move(d)
            t.SetNet(nets[(t0.GetNetname(), i)])
        for z0 in src_zon:
            z = dup(z0)
            b.Add(z)
            z.Move(d)
            if z0.GetNetname():
                z.SetNet(nets[(z0.GetNetname(), i)])
            if z0.GetIsRuleArea():
                z.SetZoneName("MAINS_AREA")
        for s0 in src_drw:
            s = s0.Duplicate()
            b.Add(s)
            s.Move(d)
    for x in all_fps + src_trk + src_zon + all_drw:
        b.Remove(x)

    frame = rect(-35.0, -OUT_Y, 35.0, OUT_Y)
    channel = rect(-IN_X, -IN_Y, IN_X, IN_Y)
    for bx, by in centres():
        ox, oy = bx, by
        channel.BooleanSubtract(rect(ox + X0, oy + Y0, ox + X1, oy + Y1))
        for sx, ty in TABS:
            edge = ox + (X1 if sx > 0 else X0)
            a, c = edge - sx * 0.5, sx * (IN_X + 0.5)
            channel.BooleanSubtract(rect(min(a, c), oy + ty - TAB_W / 2, max(a, c), oy + ty + TAB_W / 2))
    frame.BooleanSubtract(channel)
    for oi in range(frame.OutlineCount()):
        for c in [frame.Outline(oi)] + [frame.Hole(oi, h) for h in range(frame.HoleCount(oi))]:
            n = c.PointCount()
            for k in range(n):
                a, e = c.CPoint(k), c.CPoint((k + 1) % n)
                seg(b, pcbnew.VECTOR2I(a.x, a.y), pcbnew.VECTOR2I(e.x, e.y))
    k = 0
    nh = int(TAB_W / BITE_PITCH) + 1
    for bx, by in centres():
        ox, oy = bx, by
        sx0, sx1, sy0, sy1 = SLOT
        pts = [P(ox + sx0, oy + sy0), P(ox + sx1, oy + sy0), P(ox + sx1, oy + sy1), P(ox + sx0, oy + sy1)]
        for a, c in zip(pts, pts[1:] + pts[:1]):
            seg(b, a, c)
        for sx, ty in TABS:
            edge = ox + (X1 if sx > 0 else X0)
            for j in range(nh):
                k += 1
                npth(b, edge + sx * BITE_OUT, oy + ty + (j - (nh - 1) / 2) * BITE_PITCH, BITE_D, f"MB{k}")
    rx = IN_X + (35.0 - IN_X) / 2
    fiducial(b, -rx, -OUT_Y + 12, "FID1")
    fiducial(b, rx, -OUT_Y + 12, "FID2")
    fiducial(b, -rx, OUT_Y - 12, "FID3")
    for i, (x, y) in enumerate([(-rx, -OUT_Y + 4), (rx, -OUT_Y + 4), (-rx, OUT_Y - 4), (rx, OUT_Y - 4)]):
        npth(b, x, y, 2.0, f"TH{i + 1}")
    t = pcbnew.PCB_TEXT(b)
    t.SetText("UNCERTIFIED PROTOTYPE  JLCJLCJLCJLC")
    t.SetPosition(P(rx, 0))
    t.SetLayer(pcbnew.F_SilkS)
    t.SetTextSize(pcbnew.VECTOR2I(mm(1.2), mm(1.2)))
    t.SetTextThickness(mm(0.18))
    t.SetTextAngleDegrees(90)
    b.Add(t)
    silk.frame_art(b, lambda x, y: P(x, y), 35.0, OUT_Y, IN_X, IN_Y, rx, OUT_Y - 12, OUT_Y - 4, [y for _, y in centres()])
    org = P(-35.0, OUT_Y)
    b.GetDesignSettings().SetAuxOrigin(org)
    pcbnew.SaveBoard(PCB, b)
    return b, placed, org


def key(r):
    m = re.fullmatch(r"([A-Z]+)(\d+)([AB]?)_(\d+)", r)
    return (m[1], int(m[2]), m[3], int(m[4]))


def bom_cpl(placed, org):
    rows, groups, ps1 = [], {}, []
    for i, ref, f in placed:
        if ref == "PS1":
            ps1.append((f"PS1_{i + 1}", [q.GetPosition() for q in f.Pads()], 180, "Bottom"))
        if ref in HAND:
            continue
        fpn = f.GetFPIDAsString().split(":")[1]
        if ref == "U1":
            for name, pins in SOCKET.items():
                pads = [p.GetPosition() for p in f.Pads() if p.GetNumber() in pins]
                rows.append((f"{name}_{i + 1}", pads, 0, "Top"))     # both strips lie along x
                groups.setdefault((*EXTRA[name], fpn), []).append(f"{name}_{i + 1}")
            continue
        p = design.PARTS.get(ref, {})
        val, lcsc = EXTRA.get(ref, (p.get("value"), p.get("lcsc")))
        pads = [q.GetPosition() for q in f.Pads()]
        rows.append((f"{ref}_{i + 1}", pads, f.GetOrientationDegrees() % 360, "Top"))
        groups.setdefault((val, lcsc, fpn), []).append(f"{ref}_{i + 1}")
    write_bom_cpl("", rows, groups, org)
    # same files plus PS1, to see it in JLCPCB's preview; the order files above leave it out
    write_bom_cpl("-with-ps1", rows + ps1, {**groups, PS1_PART: [r[0] for r in ps1]}, org)


def write_bom_cpl(suffix, rows, groups, org):
    with open(os.path.join(OUT, NAME + suffix + "-bom.csv"), "w", newline="", encoding="utf8") as f:
        w = csv.writer(f)
        w.writerow(["Comment", "Designator", "Footprint", "LCSC Part #"])
        for (v, lcsc, fpn), refs in sorted(groups.items(), key=lambda kv: key(sorted(kv[1], key=key)[0])):
            w.writerow([v, ",".join(sorted(refs, key=key)), fpn, lcsc])
    with open(os.path.join(OUT, NAME + suffix + "-cpl.csv"), "w", newline="", encoding="utf8") as f:
        w = csv.writer(f)
        w.writerow(["Designator", "Mid X", "Mid Y", "Layer", "Rotation"])
        for r, pads, rot, layer in sorted(rows, key=lambda r: key(r[0])):
            cx = sum(p.x for p in pads) / len(pads)
            cy = sum(p.y for p in pads) / len(pads)
            w.writerow([r, f"{pcbnew.ToMM(cx - org.x):.3f}mm", f"{pcbnew.ToMM(org.y - cy):.3f}mm", layer, f"{rot:g}"])


def export():
    subprocess.call([CLI, "pcb", "drc", "--refill-zones", "--save-board", "--severity-all", "--format", "json",
                     "-o", os.path.join(OUT, "panel-drc.json"), PCB])
    subprocess.check_call([CLI, "pcb", "render", "--side", "top", "--quality", "high", "--width", "1200", "--height", "1500",
                           "-o", os.path.join(OUT, "panel-top.png"), PCB])
    g = os.path.join(OUT, "g") + os.sep
    os.makedirs(g)
    layers = "F.Cu,B.Cu,F.Paste,B.Paste,F.SilkS,B.SilkS,F.Mask,B.Mask,Edge.Cuts"
    subprocess.check_call([CLI, "pcb", "export", "gerbers", "--layers", layers, "--use-drill-file-origin",
                           "--subtract-soldermask", "-o", g, PCB])
    subprocess.check_call([CLI, "pcb", "export", "drill", "--format", "excellon", "--excellon-units", "mm",
                           "--drill-origin", "plot", "--excellon-separate-th", "-o", g, PCB])
    with zipfile.ZipFile(os.path.join(OUT, NAME + "-gerbers.zip"), "w", zipfile.ZIP_DEFLATED) as zf:
        for f in sorted(glob.glob(g + "*")):
            zf.write(f, os.path.basename(f))


if __name__ == "__main__":
    board, parts, origin = build()
    bom_cpl(parts, origin)
    export()
    print("panel", 70.0, "x", round(2 * OUT_Y, 2))
