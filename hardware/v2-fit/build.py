"""Mains board v2 fit study, soldered-wire variant: rebuild the KiCad project, renders, STEP
and measurements.

Run with KiCad 10's Python, from anywhere:
    "%LOCALAPPDATA%/Programs/KiCad/10.0/bin/python.exe" hardware/v2-fit/build.py

Coordinates: mm from the box centre, x right, y down, seen from the top (XIAO side). The box
interior is x +-19, y +-12. L and N are wires soldered at the -x end; the switch wires plug
into J2, under the XIAO, bend and leave through the top face. Through-hole: PS1, the wire
holes. A slot leaves
PS1 as the only electrical link. Everything is routed; GND is a bottom pour.
"""
import glob
import json
import math
import os
import re
import shutil
import subprocess
import sys

import pcbnew

sys.dont_write_bytecode = True    # leave no __pycache__ beside the v1 scripts
HERE = os.path.dirname(os.path.abspath(__file__))
V1 = os.path.normpath(os.path.join(HERE, "..", "scripts"))
sys.path.insert(0, V1)
import build_pcb as v1  # noqa: E402  (read only: helpers for tracks, zones, text)
import footprints as fpw  # noqa: E402
sys.path.insert(0, HERE)
import silk  # noqa: E402

NAME = "v2-fit"
PCB = os.path.join(HERE, NAME + ".kicad_pcb")
PRO = os.path.join(HERE, NAME + ".kicad_pro")
DRU = os.path.join(HERE, NAME + ".kicad_dru")
LIB = os.path.join(HERE, "lib")
PRETTY = os.path.join(LIB, "v2fit.pretty")
MODELS = os.path.join(LIB, "3d")
RENDERS = os.path.join(HERE, "renders")
REPORTS = os.path.join(HERE, "reports")
BIN = os.path.dirname(sys.executable)
CLI = os.path.join(BIN, "kicad-cli.exe" if os.name == "nt" else "kicad-cli")
SHARE = os.path.join(os.path.dirname(BIN), "share", "kicad")

# fpw and v1 write to hardware/kicad by default: point them here, nothing of v1 is touched
fpw.PRETTY, fpw.MODELS, fpw.LIB = PRETTY, MODELS, LIB
fpw.MODEL_URI = "${KIPRJMOD}/lib/3d/"
v1.LOCAL_LIBS["v2fit"] = PRETTY

# ---------------------------------------------------------------- the stack (z up, mm)
BOARD_T = 1.6
LIFT = 0.0           # PS1 case flat on the board: nothing below the board reaches it
PIN = 5.0            # PS1 pins as sold: 3.4 stand proud of the top side
TAIL = 1.2           # mains wire tails, beside PS1 (assumed trim)
SOCKET_H = 8.9       # Hanxia HX PM2.54-1x6P TP H8.5-YQ, C46635842, drawing: 8.5 body, 8.9 with feet
SOCKET_END = 0.25    # the same drawing: body B + 0.5 long, 0.25 past each end position
HEADER_H = 2.5       # XFCN PZ254V-11-07P, C492406, plastic height, LCSC listing
HEADER_POST = 6.0    # the same part, mating post below the plastic
HEADER_GAP = 0.0     # standard male header: its plastic sits against the XIAO
HEADER_TAIL = 2.0    # the same part, 3 long, 1 of it inside the XIAO board
XIAO_Z = SOCKET_H + HEADER_H + HEADER_GAP  # XIAO underside above the board's top face
XIAO_T = 1.0
USB_H = 3.3
TOP_Z = 36 - 15 - LIFT - BOARD_T   # box top above the board's top face
BOT_Z = -(BOARD_T + LIFT + 15)     # box bottom

# ---------------------------------------------------------------- placement (x, y)
BOARD = (-18.9, -11.85, 19.0, 11.8)
SLOT_X = (-6.75, -5.75)              # mains left of it, low voltage right
SLOT_Y = (-9.0, 9.0)               # two bridges to the long edges remain
PS1_C = (2.0, -1.85)               # case centre: flush with the +x short side
WIRE_L, WIRE_N = (-16.6, 8.0), (-16.6, 3.0)
WIRE_PAD = (2.6, 1.5)              # pad, drill: 0.75 mm2 stranded wire (assumed)
F1_C = (-9.5, 4.5)                 # vertical, pad 1 (AC_L_IN) at the bottom
F1_PAD = (3.25, 3.43, 9.35)        # along, across, centres. Littelfuse 461 recommended pad
RV_C = (-12.85, -8.4)
X_C = (6.58, -0.83)                # XIAO centre; USB toward +x, D0-D5 along the no-wire side
ROW_A = [("D%d" % k, X_C[0] + 7.62 - 2.54 * k, X_C[1] - 7.62) for k in range(6)]
ROW_B = [("5V", X_C[0] + 7.62, X_C[1] + 7.62), ("GND", X_C[0] + 5.08, X_C[1] + 7.62)]
SOCKET_NET = {"D0": "D0", "D1": "D1", "D2": "D2", "D3": "D3", "D4": "D4", "D5": "D5",
              "5V": "+5V", "GND": "GND"}
TERM_AT = (1.6, 2.1)               # under the XIAO, along the slot; solder tails toward +x
TERM_H = 8.6                       # with its mated housing, JST PH drawing, SMT top entry
WIRE_ROOM = 2.0                    # height a 24 AWG wire needs to bend and leave sideways (assumed)
ROW_Y = [8.1, 6.1, 4.1, 2.1, 0.1, -1.9]     # J2 pins 1-6, one R/C row per input
C_X, R_X = 6.4, 9.5
ROW_R_X = [8.85, 8.85, R_X, R_X, R_X, R_X]    # rows 0 and 1 clear of the 5V/GND socket
ROW_C_X = [6.1, 6.1, C_X, C_X, C_X, C_X]
D1_AT, C1_AT, C2_AT = (17.4, -3.8), (13.2, -3.4), (16.8, 1.6)

KEEPOUT_MM = 6.5       # the keepout is all mains copper grown by this, clipped to the board


def box_part(x0, y0, x1, y1, z0, z1):
    return (x0, y0, z0, x1, y1, z1)


def pin_box(x, y, s, z0, z1):
    return (x - s / 2, y - s / 2, z0, x + s / 2, y + s / 2, z1)


def drawn_fp(name, descr, items, body, model_boxes, smd=False):
    uri = fpw.write_model(name + "_box", model_boxes)
    fpw.footprint(name, descr, items, body, smd=smd, models=[uri])


def build_library():
    os.makedirs(PRETTY)
    os.makedirs(MODELS)
    # PS1: same pads as v1 (HiLink_HLK-PM01), case at its real lift, pins 5 long
    pins = [("1", 0, 0), ("2", 0, 5.0), ("3", 29.4, -5.2), ("4", 29.4, 10.2)]
    items = [fpw.tht_pad(n, x, y, 2.2, 1.3, n == "1") for n, x, y in pins]
    body = (-2.3, -7.5, 31.7, 12.5)
    case = (body[0], body[1], LIFT, body[2], body[3], LIFT + 15)
    drawn_fp("HiLink_HLK-PM01_lifted", "HLK-PM01, v1 pads, case lifted %.1f mm" % LIFT, items, body,
             [case] + [pin_box(x, y, 0.8, LIFT - PIN, LIFT) for _, x, y in pins])
    # RV1: v1 footprint, KEMET VP3225K401R275 body (TDK out of stock): 4.7 tall
    fpw.varistor_cu3225()
    fpw.write_model("TDK_SIOV_CU3225_box", [(-4.0, -3.15, 0, 4.0, 3.15, 4.7)])

    # F1: Littelfuse 0461.500ER, body and recommended pads from the 461 series datasheet
    along, across, c = F1_PAD
    items = [fpw.smd_pad("1", -c / 2, 0, along, across), fpw.smd_pad("2", c / 2, 0, along, across)]
    drawn_fp("Fuse_Littelfuse_461", "Littelfuse 461 series, 10.10 x 3.12 x 3.12", items,
             (-5.05, -1.56, 5.05, 1.56), [(-5.05, -1.56, 0, 5.05, 1.56, 3.12)], smd=True)

    # WL, WN: a wire soldered through a hole, the model shows the wire leaving toward -x
    items = [fpw.tht_pad("1", 0, 0, WIRE_PAD[0], WIRE_PAD[1])]
    drawn_fp("Wire_0.75mm2", "Soldered mains wire", items, (-1.3, -1.3, 1.3, 1.3),
             [(-0.55, -0.55, -BOARD_T - TAIL, 0.55, 0.55, 0.8),
              (-3.4, -1.0, 0.8, 0.5, 1.0, 2.8)])

    # U1: eight through-hole socket positions, coordinates about the XIAO centre, no rotation
    cx, cy = X_C
    # staggered feet, EasyEDA footprint of C46635842: 1.02 x 3.0 pads, 1.5 off the row
    items = []
    for k, (n, x, y) in enumerate(ROW_A + ROW_B):
        side = -1 if k % 2 else 1     # matches the model JLCPCB shows for the -YQ part
        items.append(fpw.smd_pad(n, x - cx, y - cy + side * 1.5, 1.02, 3.0))
    xa = [x - cx for _, x, _ in ROW_A]
    xb = [x - cx for _, x, _ in ROW_B]
    ya, yb = ROW_A[0][2] - cy, ROW_B[0][2] - cy
    end = 1.27 + SOCKET_END
    boxes = [(min(xa) - end, ya - 1.25, 0, max(xa) + end, ya + 1.25, SOCKET_H),
             (min(xb) - end, yb - 1.25, 0, max(xb) + end, yb + 1.25, SOCKET_H)]
    uri = fpw.write_model("XIAO_socket_6+2_box", boxes)
    for xs, y in ((xa, ya), (xb, yb)):
        r = (min(xs) - end, y - 1.25, max(xs) + end, y + 1.25)
        items += [fpw.rect(*r, "F.Fab", 0.1), fpw.rect(*r, "F.SilkS", 0.12),
                  fpw.rect(r[0] - .1, r[1] - .1, r[2] + .1, r[3] + .1, "F.CrtYd", 0.05)]
    out = ['(footprint "XIAO_socket_6+2"', '  (version 20241229)', '  (generator "v2fit")', '  (layer "F.Cu")',
           fpw.text("Reference", "REF**", 0, yb - 2.2, "F.SilkS"), fpw.text("Value", "XIAO socket", 0, 0, "F.Fab", hide=True),
           '  (property "Description" "Two strips: 6 at D0-D5, 2 at 5V GND" (at 0 0 0) (layer "F.Fab") (hide yes)\n'
           '    (effects (font (size 1 1) (thickness 0.15))))', '  (attr smd)'] + items
    out += ['  (model "%s" (offset (xyz 0 0 0)) (scale (xyz 1 1 1)) (rotate (xyz 0 0 0)))' % uri, ")"]
    with open(os.path.join(PRETTY, "XIAO_socket_6+2.kicad_mod"), "w", encoding="utf8", newline="\n") as f:
        f.write("\n".join(out) + "\n")

    # XM1: the XIAO in the socket, no pads, its own footprint so a render can drop it
    z = XIAO_Z
    boxes = [(-10.52, -8.89, z, 10.52, 8.89, z + XIAO_T),
             (11.82 - 7.35, -4.47, z + XIAO_T, 11.82, 4.47, z + XIAO_T + USB_H)]
    for _, x, y in ROW_A + ROW_B:
        boxes.append(pin_box(x - cx, y - cy, 2.5, SOCKET_H, SOCKET_H + HEADER_H))
        boxes.append(pin_box(x - cx, y - cy, 0.64, SOCKET_H - HEADER_POST, z + HEADER_TAIL))
    uri = fpw.write_model("XIAO_seated_box", boxes)
    write_padless("XIAO_seated", "XIAO ESP32-C6 seated in the socket, header on the used pins only", uri)

    # BOX1: the free interior as a frame of 12 bars just outside it
    t = 0.3
    x0, x1, y0, y1, z0, z1 = -19, 19, -12, 12, BOT_Z, TOP_Z
    bars = []
    for z in (z0 - t, z1):
        bars += [(x0 - t, y0 - t, z, x1 + t, y0, z + t), (x0 - t, y1, z, x1 + t, y1 + t, z + t),
                 (x0 - t, y0, z, x0, y1, z + t), (x1, y0, z, x1 + t, y1, z + t)]
    for x in (x0 - t, x1):
        for y in (y0 - t, y1):
            bars.append((x, y, z0, x + t, y + t, z1))
    uri = fpw.write_model("box_38x24x36_frame", bars)
    write_padless("box_38x24x36", "Free interior of the box, 12 bars, no pads", uri)

    # stock SMD parts, copied in with their models so the project is self-contained
    # J2: stock footprint, box model, body only: its mating housing is not drawn
    term = "JST_PH_B7B-PH-SM4-TB_1x07-1MP_P2.00mm_Vertical"
    with open(os.path.join(SHARE, "footprints", "Connector_JST.pretty", term + ".kicad_mod"), encoding="utf8") as f:
        s = f.read()
    uri = fpw.write_model("JST_PH_7_V_box", [(-8.975, -4.25, 0, 8.975, 0.75, TERM_H)])
    s = re.sub(r'\(model "[^"]*".*?\n\t\)\n', '', s, flags=re.S)
    i = s.rindex(")")
    s = s[:i] + '  (model "%s" (offset (xyz 0 0 0)) (scale (xyz 1 1 1)) (rotate (xyz 0 0 0)))\n)\n' % uri
    with open(os.path.join(PRETTY, term + ".kicad_mod"), "w", encoding="utf8", newline="\n") as f:
        f.write(s)

    for lib, part in (("Resistor_SMD", "R_0603_1608Metric"), ("Capacitor_SMD", "C_0603_1608Metric"),
                      ("Capacitor_SMD", "C_0805_2012Metric"), ("Diode_SMD", "D_SMA")):
        with open(os.path.join(SHARE, "footprints", lib + ".pretty", part + ".kicad_mod"), encoding="utf8") as f:
            s = f.read()
        s = s.replace("${KICAD10_3DMODEL_DIR}/%s.3dshapes/" % lib, "${KIPRJMOD}/lib/3d/")
        with open(os.path.join(PRETTY, part + ".kicad_mod"), "w", encoding="utf8", newline="\n") as f:
            f.write(s)
        shutil.copyfile(os.path.join(SHARE, "3dmodels", lib + ".3dshapes", part + ".step"),
                        os.path.join(MODELS, part + ".step"))


def write_padless(name, descr, uri):
    out = ['(footprint "%s"' % name, '  (version 20241229)', '  (generator "v2fit")', '  (layer "F.Cu")',
           fpw.text("Reference", "REF**", 0, 0, "F.Fab", hide=True),
           fpw.text("Value", name, 0, 0, "F.Fab", hide=True),
           '  (property "Description" "%s" (at 0 0 0) (layer "F.Fab") (hide yes)\n'
           '    (effects (font (size 1 1) (thickness 0.15))))' % descr,
           '  (attr exclude_from_pos_files exclude_from_bom allow_missing_courtyard)',
           '  (model "%s" (offset (xyz 0 0 0)) (scale (xyz 1 1 1)) (rotate (xyz 0 0 0)))' % uri, ")"]
    with open(os.path.join(PRETTY, name + ".kicad_mod"), "w", encoding="utf8", newline="\n") as f:
        f.write("\n".join(out) + "\n")


def place(board, nets, ref, name, at, rot=0, side="top", pins=None, value=""):
    fp = pcbnew.FootprintLoad(PRETTY, name)
    if fp is None:
        raise SystemExit("footprint not found: " + name)
    fp.SetFPID(pcbnew.LIB_ID("v2fit", name))
    fp.SetReference(ref)
    fp.SetValue(value or name)
    board.Add(fp)
    if side == "bottom":
        fp.Flip(fp.GetPosition(), pcbnew.FLIP_DIRECTION_LEFT_RIGHT)
    fp.SetOrientationDegrees(rot)
    fp.SetPosition(v1.pt(*at))
    for pad in fp.Pads():
        net = (pins or {}).get(pad.GetNumber())
        if net:
            pad.SetNet(nets[net])
    fp.Value().SetVisible(False)
    fp.Reference().SetTextSize(pcbnew.VECTOR2I(v1.mm(0.8), v1.mm(0.8)))
    fp.Reference().SetTextThickness(v1.mm(0.12))
    return fp


def pad_at(fp, num):
    p = fp.FindPadByNumber(num).GetPosition()
    return (pcbnew.ToMM(p.x) - v1.OX, pcbnew.ToMM(p.y) - v1.OY)


def outline(board):
    x0, y0, x1, y1 = BOARD
    pts = [(x0, y0), (x1, y0), (x1, y1), (x0, y1), (x0, y0)]
    for a, b in zip(pts, pts[1:]):
        s = pcbnew.PCB_SHAPE(board)
        s.SetLayer(pcbnew.Edge_Cuts)
        s.SetWidth(v1.mm(0.1))
        s.SetShape(pcbnew.SHAPE_T_SEGMENT)
        s.SetStart(v1.pt(*a))
        s.SetEnd(v1.pt(*b))
        board.Add(s)
    x0, x1 = SLOT_X
    y0, y1 = SLOT_Y
    pts = [(x0, y0), (x1, y0), (x1, y1), (x0, y1), (x0, y0)]
    for a, b in zip(pts, pts[1:]):
        s = pcbnew.PCB_SHAPE(board)
        s.SetLayer(pcbnew.Edge_Cuts)
        s.SetWidth(v1.mm(0.1))
        s.SetShape(pcbnew.SHAPE_T_SEGMENT)
        s.SetStart(v1.pt(*a))
        s.SetEnd(v1.pt(*b))
        board.Add(s)


def mains_area(board):
    """Outline of every mains pad and track grown by KEEPOUT_MM, clipped to the board."""
    grown = pcbnew.SHAPE_POLY_SET()
    err, loc = v1.mm(0.01), pcbnew.ERROR_OUTSIDE
    for fp in board.GetFootprints():
        for pad in fp.Pads():
            if pad.GetNetname().startswith("AC_"):
                layer = pcbnew.B_Cu if fp.IsFlipped() else pcbnew.F_Cu
                pad.TransformShapeToPolygon(grown, layer, v1.mm(KEEPOUT_MM), err, loc)
    for t in board.GetTracks():
        if t.GetNetname().startswith("AC_"):
            t.TransformShapeToPolygon(grown, t.GetLayer(), v1.mm(KEEPOUT_MM), err, loc)
    grown.Simplify()
    x0, y0, x1, y1 = BOARD
    edge = pcbnew.SHAPE_POLY_SET()
    edge.NewOutline()
    for x, y in ((x0, y0), (x1, y0), (x1, y1), (x0, y1)):
        edge.Append(v1.mm(100 + x), v1.mm(100 + y))
    grown.BooleanIntersection(edge)
    grown.Simplify()
    assert grown.OutlineCount() == 1 and grown.HoleCount(0) == 0, (grown.OutlineCount(),)
    o = grown.Outline(0)
    return [(pcbnew.ToMM(o.CPoint(i).x) - 100, pcbnew.ToMM(o.CPoint(i).y) - 100) for i in range(o.PointCount())]


def build_board():
    v1.OX, v1.OY = 100.0, 100.0
    board = v1.new_board()
    ds = board.GetDesignSettings()
    ds.SetBoardThickness(v1.mm(BOARD_T))
    netnames = ["AC_L_IN", "AC_L", "AC_N", "GND", "+5V_PSU", "+5V"]
    netnames += ["D%d" % i for i in range(6)] + ["IN_D%d" % i for i in range(6)]
    nets = v1._SuffixNets(board, "")
    for n in netnames:
        nets[n]
    outline(board)

    fps = {}
    # PS1: pin 1 (AC_L) at the lower of the two AC pins, AC end on the mains side
    l_pin = (PS1_C[0] - 14.7, PS1_C[1] + 2.5)
    fps["PS1"] = place(board, nets, "PS1", "HiLink_HLK-PM01_lifted", l_pin, 0, "bottom",
                       {"1": "AC_L", "2": "AC_N", "3": "GND", "4": "+5V_PSU"}, "HLK-PM01")
    fps["WL"] = place(board, nets, "WL", "Wire_0.75mm2", WIRE_L, pins={"1": "AC_L_IN"}, value="L wire")
    fps["WL"].Reference().SetVisible(False)
    fps["WN"] = place(board, nets, "WN", "Wire_0.75mm2", WIRE_N, pins={"1": "AC_N"}, value="N wire")
    fps["WN"].Reference().SetVisible(False)
    jpins = {str(i + 1): "IN_D%d" % i for i in range(6)}
    jpins["7"] = "GND"
    fps["J2"] = place(board, nets, "J2", "JST_PH_B7B-PH-SM4-TB_1x07-1MP_P2.00mm_Vertical", TERM_AT, 90,
                      pins=jpins, value="D0-D5 GND")
    fps["F1"] = place(board, nets, "F1", "Fuse_Littelfuse_461", F1_C, 90,
                      pins={"1": "AC_L_IN", "2": "AC_L"}, value="T500mA 600V")
    fps["RV1"] = place(board, nets, "RV1", "RV_TDK_SIOV_CU3225", RV_C, 180,
                       pins={"1": "AC_L", "2": "AC_N"}, value="275VAC")
    fps["U1"] = place(board, nets, "U1", "XIAO_socket_6+2", X_C,
                      pins={k: v for k, v in SOCKET_NET.items() if v}, value="XIAO ESP32-C6 socket")
    # the model is drawn in board axes: USB toward +x, pins at the socket positions
    fps["XM1"] = place(board, nets, "XM1", "XIAO_seated", X_C, value="XIAO seated")
    fps["XM1"].Reference().SetVisible(False)
    fps["BOX1"] = place(board, nets, "BOX1", "box_38x24x36", (0, 0), value="Box interior")
    fps["BOX1"].Reference().SetVisible(False)

    for i, y in enumerate(ROW_Y):
        n = i + 11
        c = place(board, nets, "C%d" % n, "C_0603_1608Metric", (ROW_C_X[i], y), 0,
                  pins={"1": "IN_D%d" % i, "2": "GND"}, value="10nF")
        r = place(board, nets, "R%d" % n, "R_0603_1608Metric", (ROW_R_X[i], y), 0,
                  pins={"1": "IN_D%d" % i, "2": "D%d" % i}, value="1k")
        r.Reference().SetVisible(False)
        c.Reference().SetVisible(False)
    place(board, nets, "D1", "D_SMA", D1_AT, 90, pins={"1": "+5V", "2": "+5V_PSU"}, value="SS34")
    place(board, nets, "C1", "C_0805_2012Metric", C1_AT, 180, pins={"1": "+5V_PSU", "2": "GND"}, value="22uF 25V")
    place(board, nets, "C2", "C_0805_2012Metric", C2_AT, 90, pins={"1": "+5V", "2": "GND"}, value="22uF 25V")

    # ------------------------------------------------------------ mains copper
    F = pcbnew.F_Cu
    B = pcbnew.B_Cu
    w = 0.8
    n_pin, l_p = pad_at(fps["PS1"], "2"), pad_at(fps["PS1"], "1")
    f_in, f_out = pad_at(fps["F1"], "1"), pad_at(fps["F1"], "2")
    rv_l, rv_n = pad_at(fps["RV1"], "1"), pad_at(fps["RV1"], "2")
    T = lambda net, pts, layer: v1.track(board, nets[net], pts, w, layer)
    # one layer is enough: L in to the fuse, fused L to the module and varistor, N to both
    T("AC_L_IN", [WIRE_L, (WIRE_L[0] + 1.2, f_in[1]), f_in], F)
    T("AC_L", [f_out, (f_out[0] - 1.0, l_p[1]), l_p], F)
    T("AC_L", [f_out, (rv_l[0], rv_l[1] + 1.75)], F)
    T("AC_N", [WIRE_N, (WIRE_N[0], rv_n[1] + 1.75)], F)
    T("AC_N", [(WIRE_N[0], -2.6), (-15.0, -2.6), n_pin], F)

    # ------------------------------------------------------------ low voltage
    # Inputs on top: J2 -> C, and past C to R in the gap above the row. Signals drop to the
    # bottom after R and fan right, so their lanes keep the socket's order, D5 to D0.
    # GND is the bottom pour.
    sig, pwr, gw = 0.25, 0.5, 0.4
    V = lambda net, at: v1.via(board, nets[net], at, 0.3, 0.6)
    v1.track(board, nets["GND"], [(ROW_C_X[0] + 0.78, ROW_Y[0]), (ROW_C_X[0] + 0.78, ROW_Y[1])], gw, F)
    V("GND", (ROW_C_X[0] + 0.78, 7.1))
    lanes = [15.1, 14.4, 13.7, 13.0, 12.3, 11.6]     # D0 to D5
    row_via = [(11.3, 8.4), (11.15, 7.3), (10.9, 3.3)] + [(11.1, y) for y in ROW_Y[3:]]   # clear of the GND socket pad
    for i, y in enumerate(ROW_Y):
        inp, d = "IN_D%d" % i, "D%d" % i
        v1.track(board, nets[inp], [(4.6, y), (ROW_C_X[i] - 0.78, y)], sig, F)
        rx = ROW_R_X[i]
        lane = 1.0 if i == 0 else -1.0     # row 0 runs below, so rows 0 and 1 share one GND via
        v1.track(board, nets[inp], [(4.6, y), (5.0, y + lane), (rx - 1.1, y + lane), (rx - 0.78, y)], sig, F)
        if i >= 2:
            v1.track(board, nets["GND"], [(C_X + 0.78, y), (7.7, y)], sig, F)
            V("GND", (7.7, y))
        vx, vy = row_via[i]
        v1.track(board, nets[d], [(rx + 0.78, y), (vx, vy)], sig, F)
        V(d, (vx, vy))
        v1.track(board, nets[d], [(vx, vy), (11.3, 4.6), (lanes[i], 4.6)] if i == 2 else [(vx, vy), (lanes[i], vy)], sig, B)
    v1.track(board, nets["D0"], [(lanes[0], row_via[0][1]), (lanes[0], -5.0)], sig, B)
    v1.track(board, nets["D1"], [(lanes[1], row_via[1][1]), (lanes[1], -7.4), (11.66, -7.4), (11.66, -7.7)], sig, B)
    v1.track(board, nets["D2"], [(lanes[2], 4.6), (lanes[2], -4.9)], sig, B)
    v1.track(board, nets["D3"], [(lanes[3], row_via[3][1]), (lanes[3], -5.05), (6.58, -5.05), (6.58, -7.7)], sig, B)
    v1.track(board, nets["D4"], [(lanes[4], row_via[4][1]), (lanes[4], -4.55), (4.6, -4.55), (4.6, -5.1)], sig, B)
    v1.track(board, nets["D5"], [(lanes[5], row_via[5][1]), (lanes[5], -2.85), (1.5, -2.85), (1.5, -7.7)], sig, B)
    for net, x in (("D1", 11.66), ("D3", 6.58), ("D5", 1.5)):
        v1.track(board, nets[net], [(x, -8.7), (x, -7.7)], sig, F)
        V(net, (x, -7.7))
    v1.track(board, nets["D0"], [(14.2, -5.6), (15.1, -5.0)], sig, F)
    V("D0", (15.1, -5.0))
    v1.track(board, nets["D2"], [(9.12, -5.6), (9.12, -4.9), (13.7, -4.9)], sig, F)
    V("D2", (13.7, -4.9))
    v1.track(board, nets["D4"], [(4.04, -5.6), (4.6, -5.1)], sig, F)
    V("D4", (4.6, -5.1))
    v1.track(board, nets["GND"], [(4.6, -3.9), (5.2, -3.6)], gw, F)
    V("GND", (5.2, -3.6))
    v1.track(board, nets["GND"], [(12.25, -3.4), (11.4, -3.8)], gw, F)
    V("GND", (11.4, -3.8))
    v1.track(board, nets["GND"], [(16.1, 0.65), (15.75, 0.65)], gw, F)
    V("GND", (15.75, 0.65))
    v1.track(board, nets["GND"], [(11.9, 5.3), (12.65, 5.3)], gw, F)
    V("GND", (12.65, 5.3))
    v1.track(board, nets["+5V_PSU"], [(16.7, -9.55), (17.3, -8.5), (17.4, -7.0)], pwr, F)
    v1.track(board, nets["+5V_PSU"], [(16.6, -5.2), (16.0, -4.0), (15.6, -3.4), (14.15, -3.4)], pwr, F)
    v1.track(board, nets["+5V"], [(18.3, -0.7), (18.3, 2.55), (17.45, 2.55)], pwr, F)
    v1.track(board, nets["+5V"], [(16.15, 2.55), (14.4, 3.4), (14.4, 7.0), (14.2, 7.4)], pwr, F)
    x0, y0, x1, y1 = BOARD
    v1.zone(board, nets["GND"], B, [(SLOT_X[1] + 0.3, y0), (x1, y0), (x1, y1), (SLOT_X[1] + 0.3, y1)])

    # ------------------------------------------------------------ keepout
    # Pours are refused by the rule area itself; tracks and vias by the custom rule,
    # because a KiCad rule area cannot exempt one net class.
    v1.keepout(board, mains_area(board), [F, B], tracks=False, vias=False, pads=False, pours=True,
               name="MAINS_AREA")

    # ------------------------------------------------------------ marks
    v1.text(board, "L N WIRES", (-18.4, 0), pcbnew.F_Fab, 0.9, rot=90)
    v1.text(board, "SWITCH WIRES BEND OUT FROM UNDER THE XIAO", (0, 12.8), pcbnew.F_Fab, 0.9)
    v1.text(board, "NO WIRES ON THIS SIDE", (0, -12.8), pcbnew.F_Fab, 0.9)
    silk.board_art(board, v1.pt)

    # check the pin positions the design depends on
    assert abs(n_pin[1] - (PS1_C[1] - 2.5)) < 1e-6 and abs(n_pin[0] - l_p[0]) < 1e-6, n_pin
    gnd = pad_at(fps["PS1"], "3")
    vp = pad_at(fps["PS1"], "4")
    assert abs(gnd[1] - (PS1_C[1] + 7.7)) < 1e-6 and abs(vp[1] - (PS1_C[1] - 7.7)) < 1e-6, (gnd, vp)
    assert f_in[1] > F1_C[1] > f_out[1], (f_in, f_out)
    board.Save(PCB)


DRU_TEXT = v1.DRU + """
# v2 fit study: the keepout, for copper the rule area itself cannot name by net class.
(rule "No low voltage copper in the mains area"
  (constraint disallow track via)
  (condition "A.NetClass != 'Mains' && A.intersectsArea('MAINS_AREA')"))
"""


def write_project():
    with open(os.path.join(HERE, "..", "kicad", "hue-simple-switch-mains.kicad_pro"), encoding="utf8") as f:
        d = json.load(f)
    d["meta"]["filename"] = NAME + ".kicad_pro"
    with open(PRO, "w", encoding="utf8", newline="\n") as f:
        json.dump(d, f, indent=2)
        f.write("\n")
    with open(DRU, "w", encoding="utf8", newline="\n") as f:
        f.write(DRU_TEXT)
    with open(os.path.join(HERE, "fp-lib-table"), "w", encoding="utf8", newline="\n") as f:
        f.write('(fp_lib_table\n  (version 7)\n  (lib (name "v2fit")(type "KiCad")'
                '(uri "${KIPRJMOD}/lib/v2fit.pretty")(options "")(descr ""))\n)\n')


def run(*args):
    print("+", " ".join(os.path.basename(a) if i == 0 else a for i, a in enumerate(args)), flush=True)
    r = subprocess.run(args, capture_output=True, text=True)
    if r.returncode != 0:
        print(r.stdout, r.stderr)
        raise SystemExit("failed: %s %s" % (args[0], args[1:3]))
    return r.stdout


def render(pcb, out, *extra, width=1600, height=1200):
    run(CLI, "pcb", "render", "--quality", "high", "--background", "opaque", "--width", str(width),
        "--height", str(height), "-o", os.path.join(RENDERS, out), *extra, pcb)


def renders():
    os.makedirs(RENDERS)
    render(PCB, "top.png", "--side", "top", "--zoom", "0.95")
    render(PCB, "bottom.png", "--side", "bottom", "--zoom", "0.95")
    render(PCB, "iso-1.png", "--side", "top", "--rotate", "-60,0,35", "--zoom", "0.68")
    render(PCB, "iso-2.png", "--side", "top", "--rotate", "-60,0,215", "--zoom", "0.68")
    render(PCB, "side-long.png", "--side", "front", "--zoom", "0.72")
    render(PCB, "side-short.png", "--side", "right", "--zoom", "0.72")
    # the XIAO out: a copy of the board without XM1, deleted straight after
    b = pcbnew.LoadBoard(PCB)
    for fp in list(b.GetFootprints()):
        if fp.GetReference() == "XM1":
            b.Remove(fp)
    tmp = os.path.join(HERE, "_xiao_out.kicad_pcb")
    b.Save(tmp)
    try:
        render(tmp, "xiao-out.png", "--side", "top", "--zoom", "0.95")
    finally:
        os.remove(tmp)
        for extra in glob.glob(os.path.join(HERE, "_xiao_out.*")):
            os.remove(extra)


def main():
    for p in (LIB, RENDERS, REPORTS):
        shutil.rmtree(p, ignore_errors=True)
    for f in (PCB, PRO, DRU, os.path.join(HERE, NAME + ".step"), os.path.join(HERE, "DONE")):
        if os.path.exists(f):
            os.remove(f)
    os.makedirs(REPORTS)
    build_library()
    build_board()
    write_project()
    drc = os.path.join(REPORTS, "drc.json")
    run(CLI, "pcb", "drc", "--refill-zones", "--save-board", "--severity-all", "--format", "json", "-o", drc, PCB)
    run(CLI, "pcb", "export", "step", "--force", "--subst-models", "-o", os.path.join(HERE, NAME + ".step"), PCB)
    renders()
    import measure
    measure.main()
    import proof
    proof.main()
    print("built. Create DONE by hand once REPORT.md is written.")


if __name__ == "__main__":
    sys.path.insert(0, HERE)
    main()
