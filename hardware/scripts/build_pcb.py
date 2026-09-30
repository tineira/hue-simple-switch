"""Build hardware/kicad/hue-simple-switch-mains.kicad_pcb from design.py.

Run with KiCad's bundled Python (it has the pcbnew module):
    "%LOCALAPPDATA%/Programs/KiCad/10.0/bin/python.exe" hardware/scripts/build_pcb.py
"""
import math
import os
import sys

import pcbnew

sys.path.insert(0, os.path.dirname(__file__))
import design  # noqa: E402
import layout  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
KICAD_DIR = os.path.normpath(os.path.join(HERE, "..", "kicad"))
PCB_PATH = os.path.join(KICAD_DIR, "hue-simple-switch-mains.kicad_pcb")
FP_ROOT = os.environ.get("KICAD10_FOOTPRINT_DIR") or os.path.join(
    os.path.dirname(os.path.dirname(sys.executable)), "share", "kicad", "footprints")
LOCAL_LIBS = {"XIAO": os.path.join(KICAD_DIR, "lib", "XIAO.pretty"),
              "hue": os.path.join(KICAD_DIR, "lib", "hue.pretty")}

OX, OY = 100.0, 100.0  # board centre on the KiCad sheet


def mm(v):
    return pcbnew.FromMM(v)


def pt(x, y):
    return pcbnew.VECTOR2I(mm(OX + x), mm(OY + y))


def load_fp(fpid):
    lib, name = fpid.split(":", 1)
    path = LOCAL_LIBS.get(lib) or os.path.join(FP_ROOT, lib + ".pretty")
    fp = pcbnew.FootprintLoad(path, name)
    if fp is None:
        raise SystemExit(f"footprint not found: {fpid}")
    fp.SetFPID(pcbnew.LIB_ID(lib, name))
    return fp


def board_outline(board):
    r, w = design.BOARD_R, design.BOARD_HALF_W
    yc = math.sqrt(r * r - w * w)
    ang = math.degrees(math.atan2(yc, w))  # angle of the arc/side corner
    segs = []
    # right side, bottom arc, left side, top arc
    segs.append(("line", (w, -yc), (w, yc)))
    segs.append(("arc", (w, yc), (0, r), (-w, yc)))
    segs.append(("line", (-w, yc), (-w, -yc)))
    segs.append(("arc", (-w, -yc), (0, -r), (w, -yc)))
    for s in segs:
        shape = pcbnew.PCB_SHAPE(board)
        shape.SetLayer(pcbnew.Edge_Cuts)
        shape.SetWidth(mm(0.1))
        if s[0] == "line":
            shape.SetShape(pcbnew.SHAPE_T_SEGMENT)
            shape.SetStart(pt(*s[1]))
            shape.SetEnd(pt(*s[2]))
        else:
            shape.SetShape(pcbnew.SHAPE_T_ARC)
            shape.SetArcGeometry(pt(*s[1]), pt(*s[2]), pt(*s[3]))
        board.Add(shape)
    del ang


def outline_poly(n=24):
    """Board outline as a polygon (for copper pours)."""
    r, w = design.BOARD_R, design.BOARD_HALF_W
    a0 = math.atan2(math.sqrt(r * r - w * w), w)
    pts = []
    for sgn in (1, -1):  # bottom arc (y > 0), then top arc (y < 0)
        for i in range(n + 1):
            a = a0 + (math.pi - 2 * a0) * i / n
            x, y = r * math.cos(a), r * math.sin(a)
            pts.append((x if sgn > 0 else -x, y * sgn))
    return pts


def add_slot(board, pts, width):
    """A routed isolation slot, drawn as a closed outline on Edge.Cuts."""
    # offset the centre polyline by width/2 on both sides; only straight runs
    hw = width / 2
    left, right = [], []
    for i, (x, y) in enumerate(pts):
        if i == 0:
            dx, dy = pts[1][0] - x, pts[1][1] - y
        elif i == len(pts) - 1:
            dx, dy = x - pts[i - 1][0], y - pts[i - 1][1]
        else:
            dx, dy = pts[i + 1][0] - pts[i - 1][0], pts[i + 1][1] - pts[i - 1][1]
        L = math.hypot(dx, dy)
        nx, ny = -dy / L, dx / L
        k = 1.0
        if 0 < i < len(pts) - 1:  # mitre at corners
            ax, ay = x - pts[i - 1][0], y - pts[i - 1][1]
            la = math.hypot(ax, ay)
            n1 = (-ay / la, ax / la)
            k = 1.0 / max(0.5, n1[0] * nx + n1[1] * ny)
        left.append((x + nx * hw * k, y + ny * hw * k))
        right.append((x - nx * hw * k, y - ny * hw * k))
    # round the two ends with arcs
    poly = left
    outline = []
    for a, b in zip(poly, poly[1:]):
        outline.append(("line", a, b))
    end, start = pts[-1], pts[0]
    outline.append(("arc", left[-1], _cap(end, pts[-2], hw), right[-1]))
    rr = list(reversed(right))
    for a, b in zip(rr, rr[1:]):
        outline.append(("line", a, b))
    outline.append(("arc", right[0], _cap(start, pts[1], hw), left[0]))
    for s in outline:
        shape = pcbnew.PCB_SHAPE(board)
        shape.SetLayer(pcbnew.Edge_Cuts)
        shape.SetWidth(mm(0.1))
        if s[0] == "line":
            shape.SetShape(pcbnew.SHAPE_T_SEGMENT)
            shape.SetStart(pt(*s[1]))
            shape.SetEnd(pt(*s[2]))
        else:
            shape.SetShape(pcbnew.SHAPE_T_ARC)
            shape.SetArcGeometry(pt(*s[1]), pt(*s[2]), pt(*s[3]))
        board.Add(shape)


def _cap(p, prev, hw):
    dx, dy = p[0] - prev[0], p[1] - prev[1]
    L = math.hypot(dx, dy)
    return (p[0] + dx / L * hw, p[1] + dy / L * hw)


def place(board, nets):
    fps = {}
    for ref, p in design.PARTS.items():
        fp = load_fp(p["footprint"])
        fp.SetReference(ref)
        fp.SetValue(p["value"])
        fp.SetPath(pcbnew.KIID_PATH("/" + design.sym_uuid(ref)))
        if p.get("model"):   # height model for a stock footprint that has none
            m = pcbnew.FP_3DMODEL()
            m.m_Filename = p["model"]
            fp.Models().push_back(m)
        fp.SetSheetname("/")
        fp.SetSheetfile("hue-simple-switch-mains.kicad_sch")
        board.Add(fp)
        pl = layout.PLACE[ref]
        side, (x, y), rot = pl["side"], pl["at"], pl.get("rot", 0)
        if side == "bottom":
            fp.Flip(fp.GetPosition(), pcbnew.FLIP_DIRECTION_LEFT_RIGHT)
        fp.SetOrientationDegrees(rot)
        # "at" is where the footprint's courtyard/body centre should land
        anchor = pl.get("anchor", "origin")
        fp.SetPosition(pt(x, y))
        if anchor == "body":
            c = body_centre(fp)
            fp.Move(pcbnew.VECTOR2I(pt(x, y).x - c.x, pt(x, y).y - c.y))
        for pad in fp.Pads():
            num = pad.GetNumber()
            net = p["pins"].get(num) or nc_net(board, nets, ref, num)
            if net:
                pad.SetNet(nets[net])
        for key, val in (("LCSC", p["lcsc"]), ("MPN", p["mpn"]), ("Description", p["note"])):
            fp.SetField(key, val)
            fld = fp.GetField(key)
            fld.SetVisible(False)
            fld.SetLayer(pcbnew.F_Fab if side == "top" else pcbnew.B_Fab)
        # silkscreen: values live on the fab layer; tiny parts get a legend instead
        fp.Value().SetVisible(False)
        ref_t = fp.Reference()
        ref_t.SetTextSize(pcbnew.VECTOR2I(mm(0.8), mm(0.8)))
        ref_t.SetTextThickness(mm(0.12))
        if pl.get("hide_ref"):
            ref_t.SetVisible(False)
        elif "ref_at" in pl:
            ref_t.SetPosition(pt(*pl["ref_at"]))
        fps[ref] = fp
    return fps


def nc_net(board, nets, ref, num):
    """Name KiCad's schematic gives a no-connect pin, so parity checks agree."""
    if (ref, num) not in design.NO_CONNECT:
        return None
    pin = design.XIAO_PADS[num]
    name = f"unconnected-({ref}-{pin}-Pad{num})"
    if name not in nets:
        ni = pcbnew.NETINFO_ITEM(board, name)
        board.Add(ni)
        nets[name] = ni
    return name


def body_centre(fp):
    layer = pcbnew.F_CrtYd if not fp.IsFlipped() else pcbnew.B_CrtYd
    cy = fp.GetCourtyard(layer)
    if cy.OutlineCount():
        bb = cy.BBox()
    else:
        bb = fp.GetBoundingBox(False)
    return bb.Centre()


def track(board, net, pts, width, layer):
    for a, b in zip(pts, pts[1:]):
        t = pcbnew.PCB_TRACK(board)
        t.SetStart(pt(*a))
        t.SetEnd(pt(*b))
        t.SetWidth(mm(width))
        t.SetLayer(layer)
        t.SetNet(net)
        board.Add(t)


def via(board, net, at, drill=0.4, size=0.8):
    v = pcbnew.PCB_VIA(board)
    v.SetPosition(pt(*at))
    v.SetDrill(mm(drill))
    v.SetWidth(mm(size))
    v.SetNet(net)
    board.Add(v)


def zone(board, net, layer, poly, clearance=0.3, min_w=0.25, prio=0):
    z = pcbnew.ZONE(board)
    z.SetLayer(layer)
    z.SetNet(net)
    z.SetLocalClearance(mm(clearance))
    z.SetMinThickness(mm(min_w))
    z.SetAssignedPriority(prio)
    z.SetPadConnection(pcbnew.ZONE_CONNECTION_THERMAL)
    ol = z.Outline()
    ol.NewOutline()
    for x, y in poly:
        ol.Append(mm(OX + x), mm(OY + y))
    board.Add(z)
    return z


def keepout(board, poly, layers, tracks=True, vias=True, pads=False, pours=True, name=""):
    z = pcbnew.ZONE(board)
    z.SetIsRuleArea(True)
    z.SetDoNotAllowTracks(tracks)
    z.SetDoNotAllowVias(vias)
    z.SetDoNotAllowPads(pads)
    z.SetDoNotAllowZoneFills(pours)
    z.SetDoNotAllowFootprints(False)
    ls = pcbnew.LSET()
    for l in layers:
        ls.AddLayer(l)
    z.SetLayerSet(ls)
    if name:
        z.SetZoneName(name)
    ol = z.Outline()
    ol.NewOutline()
    for x, y in poly:
        ol.Append(mm(OX + x), mm(OY + y))
    board.Add(z)


def text(board, s, at, layer, size=1.0, rot=0, bold=False):
    t = pcbnew.PCB_TEXT(board)
    t.SetText(s)
    t.SetPosition(pt(*at))
    t.SetLayer(layer)
    t.SetTextSize(pcbnew.VECTOR2I(mm(size), mm(size)))
    t.SetTextThickness(mm(size * (0.2 if bold else 0.15)))
    t.SetTextAngleDegrees(rot)
    if layer in (pcbnew.B_SilkS, pcbnew.B_Cu, pcbnew.B_Fab):
        t.SetMirrored(True)
    board.Add(t)


DRU = """(version 1)

# Mains (L, N, fused L) to anything else: reinforced isolation for 250 V,
# pollution degree 2, with margin. The switch wires on J2/J3 are touchable.
(rule "Mains to low voltage clearance"
  (constraint clearance (min 6.0mm))
  (condition "A.NetClass == 'Mains' && B.NetClass != 'Mains' && B.Pad_Type != 'NPTH, mechanical'"))

(rule "Mains to low voltage creepage"
  (constraint creepage (min 6.0mm))
  (condition "A.NetClass == 'Mains' && B.NetClass != 'Mains' && B.Pad_Type != 'NPTH, mechanical'"))

# Unplated holes (the panel's mouse bites) become board edge once the boards
# are snapped out: same margin as the edge.
(rule "Mains to board edge and slot"
  (constraint edge_clearance (min 1.0mm))
  (condition "A.NetClass == 'Mains'"))

(rule "Mains to unplated holes"
  (constraint hole_clearance (min 1.0mm))
  (condition "A.NetClass == 'Mains' && B.Pad_Type == 'NPTH, mechanical'"))
"""


def write_project_rules(pcb_path):
    """Net classes in the .kicad_pro and the custom rules next to the board."""
    import json
    pro = pcb_path[:-len(".kicad_pcb")] + ".kicad_pro"
    with open(pro, encoding="utf8") as f:
        d = json.load(f)
    ns = d["net_settings"]
    base = dict(ns["classes"][0])
    classes = [c for c in ns["classes"] if c["name"] == "Default"]
    for name, clearance, width, prio in (("Mains", 1.5, 0.8, 0), ("Power", 0.2, 0.5, 1)):
        c = dict(base)
        c.update(name=name, clearance=clearance, track_width=width, priority=prio)
        classes.append(c)
    ns["classes"] = classes
    ns["netclass_patterns"] = [
        {"netclass": "Mains", "pattern": "AC_*"},
        {"netclass": "Power", "pattern": "+5V*"},
        {"netclass": "Power", "pattern": "+3V3*"},
        {"netclass": "Power", "pattern": "GND*"},
    ]
    d.setdefault("meta", {})["filename"] = os.path.basename(pro)
    with open(pro, "w", encoding="utf8", newline="\n") as f:
        json.dump(d, f, indent=2)
        f.write("\n")
    with open(pcb_path[:-len(".kicad_pcb")] + ".kicad_dru", "w", encoding="utf8", newline="\n") as f:
        f.write(DRU)
    # project library table: stock KiCad libraries plus the local XIAO footprint
    libs = sorted({p["footprint"].split(":")[0] for p in design.PARTS.values()})
    rows = []
    for lib in libs:
        uri = ("${KIPRJMOD}/lib/%s.pretty" % lib if lib in LOCAL_LIBS
               else "${KICAD10_FOOTPRINT_DIR}/%s.pretty" % lib)
        rows.append('  (lib (name "%s")(type "KiCad")(uri "%s")(options "")(descr ""))' % (lib, uri))
    with open(os.path.join(KICAD_DIR, "fp-lib-table"), "w", encoding="utf8", newline="\n") as f:
        f.write("(fp_lib_table\n  (version 7)\n" + "\n".join(rows) + "\n)\n")


def new_board():
    board = pcbnew.CreateEmptyBoard() if hasattr(pcbnew, "CreateEmptyBoard") else pcbnew.BOARD()
    ds = board.GetDesignSettings()
    ds.SetCopperLayerCount(2)
    ds.SetBoardThickness(mm(design.BOARD_T))
    # JLCPCB standard capabilities, with margin
    ds.m_MinClearance = mm(0.15)
    ds.m_TrackMinWidth = mm(0.15)
    ds.m_ViasMinSize = mm(0.6)
    ds.m_MinThroughDrill = mm(0.3)
    ds.m_CopperEdgeClearance = mm(0.4)
    ds.m_HoleClearance = mm(0.25)
    ds.m_HoleToHoleMin = mm(0.5)
    ds.m_SolderMaskExpansion = mm(0.05)
    ds.m_TentViasFront = True
    ds.m_TentViasBack = True
    return board


def populate(board, ox=100.0, oy=100.0, suffix="", outline=True):
    """Add one complete board (parts, copper, slot, pours, silk) centred at (ox, oy).

    suffix is appended to every net name, so copies on a panel stay separate."""
    global OX, OY
    OX, OY = ox, oy
    netnames = sorted({n for p in design.PARTS.values() for n in p["pins"].values()})
    nets = _SuffixNets(board, suffix)
    for n in netnames:
        nets[n]
    if outline:
        board_outline(board)
    fps = place(board, nets)
    layout.decorate(board, fps, nets, globals())
    return fps, nets


class _SuffixNets(dict):
    """net name -> NETINFO_ITEM, creating '<name><suffix>' on first use."""

    def __init__(self, board, suffix):
        super().__init__()
        self.board, self.suffix = board, suffix

    def __missing__(self, name):
        ni = pcbnew.NETINFO_ITEM(self.board, name + self.suffix)
        self.board.Add(ni)
        self[name] = ni
        return ni

    def __contains__(self, name):
        return True


def build(save=True):
    board = new_board()
    fps, nets = populate(board)
    # drill/place origin at the board's bottom-left corner, for Gerbers and the CPL
    board.GetDesignSettings().SetAuxOrigin(pt(-design.BOARD_HALF_W, design.BOARD_R))
    if save:
        board.Save(PCB_PATH)
        write_project_rules(PCB_PATH)
    return board, fps, nets


if __name__ == "__main__":
    b, fps, nets = build()
    for ref, fp in sorted(fps.items()):
        for pad in fp.Pads():
            p = pad.GetPosition()
            print(f"{ref}.{pad.GetNumber():>2} {pcbnew.ToMM(p.x) - OX:7.2f} {pcbnew.ToMM(p.y) - OY:7.2f} "
                  f"{pad.GetNetname()}")
