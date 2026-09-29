"""Placement and copper for the mains carrier board.

Board coordinates in mm from the board centre, x right, y down (KiCad).
Top = XIAO side, bottom = power-module side.

Zones on the board:
  mains  y < -10 on the left (power-module AC pins), and the top-right corner
         (L/N terminal J1, fuse F1); varistor RV1 on top between the AC pins.
  low V  everything below the isolation slot and left of J1.
The custom rules in hue-simple-switch-mains.kicad_dru keep >= 6 mm clearance
and creepage between the Mains net class and everything else.
"""

# anchor "body": "at" is the courtyard centre; otherwise the footprint origin.
PLACE = {
    # bottom (power-module side)
    "PS1": dict(side="bottom", at=(-9.5, 0.0), rot=270, anchor="body"),
    "J1": dict(side="bottom", at=(16.1, -10.2), rot=270, anchor="body"),
    "F1": dict(side="bottom", at=(6.6, -18.6), rot=180, anchor="body"),
    "J2": dict(side="bottom", at=(16.6, 6.25), rot=270, anchor="body"),
    "J3": dict(side="bottom", at=(7.85, 18.2), rot=180, anchor="body"),
    # top (XIAO side)
    "RV1": dict(side="top", at=(-9.5, -18.0), rot=0, anchor="body"),
    "U1": dict(side="top", at=(-10.1, 2.8), rot=90, anchor="body"),
    "D1": dict(side="top", at=(-9.4, 17.4), rot=0, anchor="body"),
    "C1": dict(side="top", at=(-3.9, 20.4), rot=0, anchor="body"),
    "C2": dict(side="top", at=(-14.2, 18.6), rot=270, anchor="body"),
}

# One row per input D0..D5, shared by the top and bottom parts of that line:
# top    [10k pull-up: 3V3 | Dk]  via(Dk)  [10nF: Dk | GND]
# bottom                           via(Dk)  [1k: Dk | IN] -> terminal
ROWS = [-0.5, 2.1, 4.7, 7.3, 9.9, 12.5]
VIA_X = 4.0
for i, y in enumerate(ROWS):
    PLACE[f"R{i + 1}"] = dict(side="top", at=(2.2, y), rot=0, anchor="body", hide_ref=True)
    PLACE[f"C{i + 11}"] = dict(side="top", at=(5.8, y), rot=0, anchor="body", hide_ref=True)
    PLACE[f"R{i + 11}"] = dict(side="bottom", at=(6.0, y), rot=180, anchor="body", hide_ref=True)

MAINS_W = 0.8
PWR_W = 0.5
SIG_W = 0.25
VIA_D, VIA_DRILL = 0.6, 0.3

# Isolation slot under the power module, between its AC pins and the XIAO.
SLOT = [(-19.4, -10.6), (-2.2, -10.6)]
SLOT_W = 1.0


def decorate(board, fps, nets, b):
    pcbnew = b["pcbnew"]
    F, B = pcbnew.F_Cu, pcbnew.B_Cu

    def P(ref, pad):
        p = fps[ref].FindPadByNumber(pad).GetPosition()
        return (pcbnew.ToMM(p.x) - b["OX"], pcbnew.ToMM(p.y) - b["OY"])

    def T(net, pts, w, layer):
        b["track"](board, nets[net], pts, w, layer)

    def V(net, at):
        b["via"](board, nets[net], at, VIA_DRILL, VIA_D)

    # ------------------------------------------------------------ mains
    T("AC_L_IN", [P("J1", "1"), P("F1", "1")], MAINS_W, B)          # L in -> fuse
    f2, l_pin, n_pin = P("F1", "2"), P("PS1", "1"), P("PS1", "2")
    T("AC_L", [f2, (l_pin[0], f2[1]), l_pin], MAINS_W, B)          # fused L -> module
    n_in = P("J1", "2")
    T("AC_N", [n_in, (12.3, n_in[1]), (12.3, -19.9), (10.0, -22.2), (-4.7, -22.2),
               (n_pin[0], -19.9), n_pin], MAINS_W, F)                  # N, round the fuse
    r1, r2 = P("RV1", "1"), P("RV1", "2")                          # varistor at the module
    T("AC_L", [l_pin, (l_pin[0], r1[1] + 1.0), r1], MAINS_W, F)
    T("AC_N", [r2, (n_pin[0], r2[1])], MAINS_W, F)

    # ------------------------------------------------------------ 5 V
    psu_p, psu_g = P("PS1", "4"), P("PS1", "3")
    d_k, d_a = P("D1", "1"), P("D1", "2")
    c1p, c2p = P("C1", "1"), P("C2", "1")
    T("+5V_PSU", [psu_p, (psu_p[0], d_a[1] - 1.2), (psu_p[0] - 1.2, d_a[1]), d_a], PWR_W, F)
    T("+5V_PSU", [c1p, (c1p[0], d_a[1])], PWR_W, F)
    v5_bot = (-18.3, 16.9)
    T("+5V", [d_k, (-17.6, d_k[1]), v5_bot], PWR_W, F)
    T("+5V", [c2p, (c2p[0], d_k[1])], PWR_W, F)
    V("+5V", v5_bot)
    x5 = P("U1", "14")
    v5_top = (x5[0], -7.1)
    T("+5V", [x5, v5_top], PWR_W, F)
    V("+5V", v5_top)
    T("+5V", [v5_bot, (-20.2, 15.0), (-20.2, -7.1), v5_top], PWR_W, B)

    # ------------------------------------------------------------ GND
    xg = P("U1", "13")
    vg = (xg[0], -7.1)
    T("GND", [xg, vg], PWR_W, F)
    V("GND", vg)
    T("GND", [vg, (vg[0], -2.5), (-19.0, -2.5), (-19.0, 13.9), (psu_g[0] - 0.6, 13.9), psu_g],
      PWR_W, B)

    # ------------------------------------------------------------ 3V3
    x3 = P("U1", "12")
    v3 = (x3[0], -7.1)
    T("+3V3", [x3, v3], PWR_W, F)
    V("+3V3", v3)
    bus_x = 0.9
    v3b = (bus_x, -1.8)
    T("+3V3", [v3, (v3[0], v3b[1]), v3b], 0.4, B)
    V("+3V3", v3b)
    T("+3V3", [v3b, (bus_x, ROWS[-1])], 0.4, F)
    for i, y in enumerate(ROWS):
        T("+3V3", [(bus_x, y), P(f"R{i + 1}", "1")], 0.4, F)

    # ------------------------------------------------------------ inputs
    names = ["D0", "D1", "D2", "D3", "D4", "D5"]
    for i, (name, y) in enumerate(zip(names, ROWS)):
        pad = P("U1", str(i + 1))
        vx = (pad[0], 12.85)
        T(name, [pad, vx], SIG_W, F)                 # out of the castellated pad
        V(name, vx)
        node = (VIA_X, y)
        T(name, [vx, (vx[0], y), node], SIG_W, B)    # lane under the module
        V(name, node)
        T(name, [node, P(f"R{i + 11}", "2")], SIG_W, B)
        T(name, [P(f"R{i + 1}", "2"), node, P(f"C{i + 11}", "1")], SIG_W, F)
    term = [P("J2", "1"), P("J2", "2"), P("J2", "3"), P("J2", "4"), P("J3", "1"), P("J3", "2")]
    for i, name in enumerate(names):
        rin, tp = P(f"R{i + 11}", "1"), term[i]
        if i < 4:   # right edge: run across, then 45 degrees into the pin
            k = tp[1] - rin[1]
            T("IN_" + name, [rin, (tp[0] - k, rin[1]), tp], SIG_W, B)
        elif i == 4:
            T("IN_" + name, [rin, (tp[0] - 1.5, rin[1]), (tp[0], rin[1] + 1.5), tp], SIG_W, B)
        else:
            T("IN_" + name, [rin, (rin[0], tp[1] - 1.5), (tp[0], tp[1] - 1.0), tp], SIG_W, B)

    # ------------------------------------------------------------ slot, pours, keepouts
    b["add_slot"](board, SLOT, SLOT_W)
    outline = b["outline_poly"]()
    for layer in (F, B):
        z = b["zone"](board, nets["GND"], layer, outline, clearance=0.3, min_w=0.25)
        z.SetIslandRemovalMode(pcbnew.ISLAND_REMOVAL_MODE_ALWAYS)
    # no copper under the XIAO body (its bottom has bare test and battery pads)
    b["keepout"](board, [(-20.3, -3.7), (0.2, -3.7), (0.2, 9.3), (-20.3, 9.3)], [F],
                 name="under XIAO")
    # GND stitching between the pours in the low-voltage area
    for at in [(13.4, 17.0), (-1.2, 21.5), (-8.0, 22.5), (2.4, 21.5)]:
        V("GND", at)

    # ------------------------------------------------------------ silkscreen
    txt = b["text"]
    txt(board, "MAINS 100-240V~", (-9.5, -21.0), pcbnew.B_SilkS, 0.9, bold=True)
    txt(board, "L", (19.4, -12.75), pcbnew.B_SilkS, 1.2, bold=True)
    txt(board, "N", (19.4, -7.67), pcbnew.B_SilkS, 1.2, bold=True)
    txt(board, "LOW VOLTAGE ONLY", (10.3, 10.0), pcbnew.B_SilkS, 0.8, rot=90)
    for n, name in enumerate(["D0", "D1", "D2", "D3"]):
        at = P("J2", str(n + 1))
        txt(board, name, (at[0] - 2.4, at[1] - 1.9), pcbnew.B_SilkS, 0.8)
    for n, name in enumerate(["D4", "D5", "G"]):
        at = P("J3", str(n + 1))
        txt(board, name, (at[0], at[1] - 2.3), pcbnew.B_SilkS, 0.8)
    txt(board, "hue-simple-switch", (-9.5, -24.2), pcbnew.F_SilkS, 0.8)
    # legends for the rows of small parts (top to bottom = D0..D5)
    txt(board, "R1-6", (2.2, -2.3), pcbnew.F_SilkS, 0.8)
    txt(board, "C11-16", (6.2, -2.3), pcbnew.F_SilkS, 0.8)
    txt(board, "D0", (9.0, ROWS[0]), pcbnew.F_SilkS, 0.8)
    txt(board, "D5", (9.0, ROWS[-1]), pcbnew.F_SilkS, 0.8)
    txt(board, "R11-16", (6.0, -2.3), pcbnew.B_SilkS, 0.8)
    txt(board, "NO USB ON MAINS", (-6.5, 23.3), pcbnew.F_SilkS, 0.8, bold=True)
    txt(board, "!", (-3.0, -16.0), pcbnew.F_SilkS, 2.0, bold=True)
