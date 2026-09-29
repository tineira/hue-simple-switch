"""Footprints and simple 3D models drawn from the manufacturers' datasheets.

Writes hardware/kicad/lib/hue.pretty/*.kicad_mod and hardware/kicad/lib/3d/*.step.
Run by build_all.py before the PCB is built. Every number below is quoted from
the datasheet named next to it, not from a distributor's package label.

The 3D models are plain boxes at the datasheet's outside dimensions: they are
for checking heights and the enclosure fit, not for pictures.
"""
import os

HERE = os.path.dirname(os.path.abspath(__file__))
LIB = os.path.normpath(os.path.join(HERE, "..", "kicad", "lib"))
PRETTY = os.path.join(LIB, "hue.pretty")
MODELS = os.path.join(LIB, "3d")
MODEL_URI = "${KIPRJMOD}/lib/3d/"


def f(v):
    return ("%.4f" % v).rstrip("0").rstrip(".") if v else "0"


# ------------------------------------------------------------------ STEP boxes
def step_boxes(name, boxes):
    """AP214 STEP with one closed box per (x0, y0, z0, x1, y1, z1), model coordinates (y up)."""
    ents = []

    def e(s):
        ents.append(s)
        return len(ents)

    solids = []
    for (x0, y0, z0, x1, y1, z1) in boxes:
        pts = [(x0, y0, z0), (x1, y0, z0), (x1, y1, z0), (x0, y1, z0),
               (x0, y0, z1), (x1, y0, z1), (x1, y1, z1), (x0, y1, z1)]
        vid = [e("VERTEX_POINT('',#%d)" % e("CARTESIAN_POINT('',(%s,%s,%s))" % tuple(map(f, p))))
               for p in pts]
        edges = {}

        def edge(a, b):
            if (a, b) in edges:
                return edges[(a, b)], True
            if (b, a) in edges:
                return edges[(b, a)], False
            pa, pb = pts[a], pts[b]
            d = [pb[i] - pa[i] for i in range(3)]
            n = max(abs(c) for c in d)
            d = [c / n for c in d]
            p = e("CARTESIAN_POINT('',(%s,%s,%s))" % tuple(map(f, pa)))
            dr = e("DIRECTION('',(%s,%s,%s))" % tuple(map(f, d)))
            vec = e("VECTOR('',#%d,1.)" % dr)
            ln = e("LINE('',#%d,#%d)" % (p, vec))
            ec = e("EDGE_CURVE('',#%d,#%d,#%d,.T.)" % (vid[a], vid[b], ln))
            edges[(a, b)] = ec
            return ec, True

        # faces as vertex loops, counter-clockwise seen from outside
        faces = [((0, 3, 2, 1), (0, 0, -1)), ((4, 5, 6, 7), (0, 0, 1)),
                 ((0, 1, 5, 4), (0, -1, 0)), ((1, 2, 6, 5), (1, 0, 0)),
                 ((2, 3, 7, 6), (0, 1, 0)), ((3, 0, 4, 7), (-1, 0, 0))]
        fids = []
        for loop, nrm in faces:
            oes = []
            for i in range(4):
                ec, same = edge(loop[i], loop[(i + 1) % 4])
                oes.append(e("ORIENTED_EDGE('',*,*,#%d,%s)" % (ec, ".T." if same else ".F.")))
            el = e("EDGE_LOOP('',(%s))" % ",".join("#%d" % o for o in oes))
            fb = e("FACE_OUTER_BOUND('',#%d,.T.)" % el)
            origin = e("CARTESIAN_POINT('',(%s,%s,%s))" % tuple(map(f, pts[loop[0]])))
            nd = e("DIRECTION('',(%s,%s,%s))" % tuple(map(f, nrm)))
            ref = (1, 0, 0) if nrm[0] == 0 else (0, 1, 0)
            rd = e("DIRECTION('',(%s,%s,%s))" % tuple(map(f, ref)))
            ax = e("AXIS2_PLACEMENT_3D('',#%d,#%d,#%d)" % (origin, nd, rd))
            pl = e("PLANE('',#%d)" % ax)
            fids.append(e("ADVANCED_FACE('',(#%d),#%d,.T.)" % (fb, pl)))
        shell = e("CLOSED_SHELL('',(%s))" % ",".join("#%d" % i for i in fids))
        solids.append(e("MANIFOLD_SOLID_BREP('%s',#%d)" % (name, shell)))

    o = e("CARTESIAN_POINT('',(0.,0.,0.))")
    z = e("DIRECTION('',(0.,0.,1.))")
    x = e("DIRECTION('',(1.,0.,0.))")
    wax = e("AXIS2_PLACEMENT_3D('',#%d,#%d,#%d)" % (o, z, x))
    lu = e("( LENGTH_UNIT() NAMED_UNIT(*) SI_UNIT(.MILLI.,.METRE.) )")
    au = e("( NAMED_UNIT(*) PLANE_ANGLE_UNIT() SI_UNIT($,.RADIAN.) )")
    su = e("( NAMED_UNIT(*) SI_UNIT($,.STERADIAN.) SOLID_ANGLE_UNIT() )")
    unc = e("UNCERTAINTY_MEASURE_WITH_UNIT(LENGTH_MEASURE(1.E-07),#%d,'distance_accuracy_value','')" % lu)
    ctx = e("( GEOMETRIC_REPRESENTATION_CONTEXT(3) GLOBAL_UNCERTAINTY_ASSIGNED_CONTEXT((#%d)) "
            "GLOBAL_UNIT_ASSIGNED_CONTEXT((#%d,#%d,#%d)) REPRESENTATION_CONTEXT('','') )" % (unc, lu, au, su))
    rep = e("ADVANCED_BREP_SHAPE_REPRESENTATION('',(%s,#%d),#%d)"
            % (",".join("#%d" % s for s in solids), wax, ctx))
    appctx = e("APPLICATION_CONTEXT('automotive_design')")
    e("APPLICATION_PROTOCOL_DEFINITION('international standard','automotive_design',2000,#%d)" % appctx)
    pctx = e("PRODUCT_CONTEXT('',#%d,'mechanical')" % appctx)
    prod = e("PRODUCT('%s','%s','',(#%d))" % (name, name, pctx))
    pdf = e("PRODUCT_DEFINITION_FORMATION('','',#%d)" % prod)
    pdctx = e("PRODUCT_DEFINITION_CONTEXT('part definition',#%d,'design')" % appctx)
    pd = e("PRODUCT_DEFINITION('design','',#%d,#%d)" % (pdf, pdctx))
    pds = e("PRODUCT_DEFINITION_SHAPE('','',#%d)" % pd)
    e("SHAPE_DEFINITION_REPRESENTATION(#%d,#%d)" % (pds, rep))
    body = "\n".join("#%d=%s;" % (i + 1, s) for i, s in enumerate(ents))
    return ("ISO-10303-21;\nHEADER;\nFILE_DESCRIPTION(('%s'),'2;1');\n"
            "FILE_NAME('%s.step','',(''),(''),'hue footprints.py','','');\n"
            "FILE_SCHEMA(('AUTOMOTIVE_DESIGN { 1 0 10303 214 1 1 1 1 }'));\nENDSEC;\nDATA;\n"
            "%s\nENDSEC;\nEND-ISO-10303-21;\n" % (name, name, body))


def write_model(name, boxes_fp):
    """boxes in footprint coordinates (x right, y DOWN, z up) -> model file (y up)."""
    boxes = [(x0, -y1, z0, x1, -y0, z1) for (x0, y0, z0, x1, y1, z1) in boxes_fp]
    os.makedirs(MODELS, exist_ok=True)
    with open(os.path.join(MODELS, name + ".step"), "w", encoding="ascii", newline="\n") as fh:
        fh.write(step_boxes(name, boxes))
    return MODEL_URI + name + ".step"


# ------------------------------------------------------------------ footprint text
def rect(x0, y0, x1, y1, layer, w):
    return (f'  (fp_rect (start {f(x0)} {f(y0)}) (end {f(x1)} {f(y1)}) '
            f'(stroke (width {f(w)}) (type solid)) (fill no) (layer "{layer}"))')


def line(x0, y0, x1, y1, layer, w):
    return (f'  (fp_line (start {f(x0)} {f(y0)}) (end {f(x1)} {f(y1)}) '
            f'(stroke (width {f(w)}) (type solid)) (layer "{layer}"))')


def text(kind, value, x, y, layer, hide=False):
    h = " (hide yes)" if hide else ""
    return (f'  (property "{kind}" "{value}" (at {f(x)} {f(y)} 0) (layer "{layer}"){h}\n'
            f'    (effects (font (size 1 1) (thickness 0.15))))')


def tht_pad(num, x, y, size, drill, first=False):
    shape = "roundrect" if first else "circle"
    rr = " (roundrect_rratio 0.25)" if first else ""
    return (f'  (pad "{num}" thru_hole {shape} (at {f(x)} {f(y)}) (size {f(size)} {f(size)}) '
            f'(drill {f(drill)}) (layers "*.Cu" "*.Mask"){rr})')


def smd_pad(num, x, y, w, h):
    return (f'  (pad "{num}" smd roundrect (at {f(x)} {f(y)}) (size {f(w)} {f(h)}) '
            f'(layers "F.Cu" "F.Paste" "F.Mask") (roundrect_rratio 0.1))')


def footprint(name, descr, items, body, smd=False, models=(), ref_y=None):
    x0, y0, x1, y1 = body
    px0, py0, px1, py1 = body_pads(items)
    m = 0.25   # courtyard margin around body and pads
    cx0, cy0, cx1, cy1 = min(x0, px0) - m, min(y0, py0) - m, max(x1, px1) + m, max(y1, py1) + m
    ry = (y0 - 1.5) if ref_y is None else ref_y
    out = [f'(footprint "{name}"', '  (version 20241229)', '  (generator "hue_footprints")',
           '  (layer "F.Cu")',
           text("Reference", "REF**", (x0 + x1) / 2, ry, "F.SilkS"),
           text("Value", name, (x0 + x1) / 2, y1 + 1.5, "F.Fab"),
           f'  (property "Description" "{descr}" (at 0 0 0) (layer "F.Fab") (hide yes)\n'
           f'    (effects (font (size 1 1) (thickness 0.15))))',
           f'  (attr {"smd" if smd else "through_hole"})',
           rect(x0, y0, x1, y1, "F.Fab", 0.1),
           rect(x0 - 0.12, y0 - 0.12, x1 + 0.12, y1 + 0.12, "F.SilkS", 0.12),
           rect(cx0, cy0, cx1, cy1, "F.CrtYd", 0.05)]
    out += [i for i in items]
    for uri in models:
        out.append(f'  (model "{uri}" (offset (xyz 0 0 0)) (scale (xyz 1 1 1)) (rotate (xyz 0 0 0)))')
    out.append(")")
    os.makedirs(PRETTY, exist_ok=True)
    with open(os.path.join(PRETTY, name + ".kicad_mod"), "w", encoding="utf8", newline="\n") as fh:
        fh.write("\n".join(out) + "\n")


def body_pads(items):
    """Bounding box of the pads in a footprint's item list (x0, y0, x1, y1)."""
    import re
    xs, ys = [], []
    for it in items:
        m = re.search(r'\(pad .*?\(at ([-\d.]+) ([-\d.]+)\) \(size ([-\d.]+) ([-\d.]+)\)', it)
        if m:
            x, y, w, h = map(float, m.groups())
            xs += [x - w / 2, x + w / 2]
            ys += [y - h / 2, y + h / 2]
    return (min(xs), min(ys), max(xs), max(ys))


# ------------------------------------------------------------------ parts
def hlk_pm01():
    # Hi-Link HLK-PM01 datasheet, "11. Dimensions and weight" (top side view):
    # body 34 x 20 x 15; AC pins 2.3 from one end at 7.5 and 12.5 from the edge
    # (5.0 apart); DC pins 2.3 from the other end at 2.3 and 17.7 (15.4 apart);
    # rows 34 - 2 x 2.3 = 29.4 apart; pins Ø0.8 ±0.2, 5 long.
    # Pin 1 (AC) at the origin, like KiCad's Converter_ACDC_Hi-Link_HLK-PMxx.
    drill, pad = 1.3, 2.2          # Ø1.0 max pin + 0.3
    pins = [("1", 0, 0), ("2", 0, 5.0), ("3", 29.4, -5.2), ("4", 29.4, 10.2)]
    items = [tht_pad(n, x, y, pad, drill, n == "1") for n, x, y in pins]
    body = (-2.3, -7.5, 31.7, 12.5)
    m = write_model("HLK-PM01_box", [(-2.3, -7.5, 0, 31.7, 12.5, 15.0)])
    footprint("HiLink_HLK-PM01", "Hi-Link HLK-PM01, pins and body from the Hi-Link datasheet",
              items, body, models=[m])


def varistor_cu3225():
    # TDK SIOV CU3225K275G2 (B72650M0271K072), SIOV CU standard series datasheet,
    # "Dimensional drawing" and "Recommended solder pad layout", 3225 at 230-300 V:
    # body l 8.0 x w 6.3 x h 4.5 (±0.3); pads A 3.5 (across) x B 2.8 (along),
    # gap C 4.5 -> pad centres 7.3 apart, outer span 10.1.
    half = (4.5 + 2.8) / 2
    items = [smd_pad("1", -half, 0, 2.8, 3.5), smd_pad("2", half, 0, 2.8, 3.5)]
    body = (-4.0, -3.15, 4.0, 3.15)
    m = write_model("TDK_SIOV_CU3225_box", [(-4.0, -3.15, 0, 4.0, 3.15, 4.5)])
    footprint("RV_TDK_SIOV_CU3225", "TDK SIOV CU3225 SMD disc varistor, TDK land pattern",
              items, body, smd=True, models=[m])


def terminal(name, descr, n, pitch, drill, pad, depth, front, height):
    """Horizontal screw terminal, pins along x, wire entry facing +y like KiCad's
    TerminalBlock footprints. front: distance from the pin row to the entry face."""
    items = [tht_pad(str(i + 1), i * pitch, 0, pad, drill, i == 0) for i in range(n)]
    w = n * pitch
    x0 = -pitch / 2
    body = (x0, front - depth, x0 + w, front)
    m = write_model(name + "_box", [(x0, front - depth, 0, x0 + w, front, height)])
    items.append(line(x0, front + 0.4, x0 + w, front + 0.4, "F.SilkS", 0.12))  # entry side mark
    footprint(name, descr, items, body, models=[m])


def terminals():
    # Cixi Kefa KF128-5.08 drawing: pitch 5.08, pins 0.8 x 0.9, PCB hole Ø1.4,
    # depth 10.7 with the pins 5.4 from the back (5.3 from the wire entry), height 14.1.
    terminal("TerminalBlock_Kefa_KF128-5.08_1x02", "Kefa KF128-5.08-2P, Kefa drawing",
             2, 5.08, 1.4, 2.4, 10.7, 5.3, 14.1)
    # Cixi Kefa KF350-3.50 drawing: pitch 3.5, pins Ø0.8, PCB hole Ø1.0 (+0.1),
    # depth 6.9 with the pins 3.5 from the back (3.4 from the entry), height 8.8.
    terminal("TerminalBlock_Kefa_KF350-3.5_1x03", "Kefa KF350-3.5-3P, Kefa drawing",
             3, 3.5, 1.2, 2.2, 6.9, 3.4, 8.8)
    # Xinlaiya XY350V drawing: pitch 3.5, pins 0.8 square (1.13 diagonal), depth 6.9,
    # height 8.6; pin position in the depth taken as the KF350's (same family).
    terminal("TerminalBlock_Xinlaiya_XY350V-3.5_1x04", "Xinlaiya XY350V-3.5-4P, Xinlaiya drawing",
             4, 3.5, 1.3, 2.2, 6.9, 3.4, 8.6)


def xiao_model():
    # Seeed XIAO ESP32-C6: board 21 x 17.8, about 1.0 thick on the carrier; USB-C
    # receptacle about 8.9 x 7.4 x 3.3 on top at the pad-1/14 end, overhanging the
    # edge by about 1.3 (Seeed dimension drawing; approximate, for height checks).
    # Footprint coordinates: board x 0..17.78, y -21.04..0.
    m = write_model("XIAO_ESP32C6_box", [
        (0, -21.04, 0, 17.78, 0, 1.0),
        (8.89 - 4.47, -22.34, 1.0, 8.89 + 4.47, -14.99, 4.3),
    ])
    path = os.path.join(LIB, "XIAO.pretty", "XIAO-ESP32-C6-SMD-EdgePads.kicad_mod")
    with open(path, encoding="utf8") as fh:
        s = fh.read()
    if "(model " not in s:
        k = s.rstrip().rfind(")")
        s = (s[:k].rstrip() + f'\n\t(model "{m}"\n\t\t(offset (xyz 0 0 0))\n\t\t(scale (xyz 1 1 1))\n'
             f'\t\t(rotate (xyz 0 0 0))\n\t)\n)\n')
        with open(path, "w", encoding="utf8", newline="\n") as fh:
            fh.write(s)


def fuse_model():
    # Littelfuse 372 (TR5) datasheet: body Ø8.5, height max 8, pins Ø0.6 at 5.08.
    # KiCad's Fuse_Littelfuse_372_D8.50mm (pad 1 at the origin) has no 3D model;
    # build_pcb.py attaches this box (the body's square hull) to it.
    return write_model("Littelfuse_TR5_372_box", [(2.54 - 4.25, -4.25, 0, 2.54 + 4.25, 4.25, 8.0)])


def build():
    fuse_model()
    xiao_model()
    hlk_pm01()
    varistor_cu3225()
    terminals()


if __name__ == "__main__":
    build()
    print("wrote", PRETTY)
