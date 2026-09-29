"""Build hardware/kicad/hue-simple-switch-mains.kicad_sch from design.py.

Plain Python (no KiCad module needed). Symbols are copied from the stock KiCad
libraries, except the XIAO, which is defined here and also written to
hardware/kicad/lib/hue.kicad_sym. Every pin gets a short wire and a net label,
so the netlist is exactly design.PARTS.
"""
import os
import re
import sys
import uuid

sys.path.insert(0, os.path.dirname(__file__))
import design  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
KICAD_DIR = os.path.normpath(os.path.join(HERE, "..", "kicad"))
SCH_PATH = os.path.join(KICAD_DIR, "hue-simple-switch-mains.kicad_sch")
PROJECT = "hue-simple-switch-mains"
SYM_ROOT = os.environ.get("KICAD10_SYMBOL_DIR") or os.path.join(
    os.environ.get("LOCALAPPDATA", ""), "Programs", "KiCad", "10.0", "share", "kicad", "symbols")

ROOT_UUID = str(uuid.uuid5(design.UUID_NS, "root-sheet"))


def uid(*key):
    return str(uuid.uuid5(design.UUID_NS, "/".join(str(k) for k in key)))


# ------------------------------------------------------------------ symbols
def sexpr_block(s, start):
    d = 0
    for j in range(start, len(s)):
        if s[j] == "(":
            d += 1
        elif s[j] == ")":
            d -= 1
            if d == 0:
                return s[start:j + 1]
    raise ValueError("unbalanced")


def lib_symbol(lib_id):
    lib, name = lib_id.split(":")
    if lib == "hue":
        return XIAO_SYMBOL
    with open(os.path.join(SYM_ROOT, lib + ".kicad_sym"), encoding="utf8") as f:
        s = f.read()
    i = s.find('(symbol "%s"' % name)
    if i < 0:
        raise SystemExit("symbol not found: " + lib_id)
    block = sexpr_block(s, i)
    return block.replace('(symbol "%s"' % name, '(symbol "%s"' % lib_id, 1)


def pins_of(block):
    """[(number, x, y, angle, type)] in symbol coordinates (y up)."""
    out = []
    for m in re.finditer(r'\(pin (\w+) \w+\s*\(at ([-\d.]+) ([-\d.]+) ([-\d.]+)\)', block):
        num = re.search(r'\(number "([^"]*)"', block[m.end():]).group(1)
        out.append((num, float(m.group(2)), float(m.group(3)), float(m.group(4)), m.group(1)))
    return out


def prop_pos(block, name):
    m = re.search(r'\(property "%s" "[^"]*"\s*\(at ([-\d.]+) ([-\d.]+) ([-\d.]+)\)' % name, block)
    return (float(m.group(1)), float(m.group(2))) if m else (0.0, 0.0)


XIAO_LEFT = ["D0", "D1", "D2", "D3", "D4", "D5", "D6"]      # pads 1..7
XIAO_RIGHT = ["5V", "GND", "3V3", "D10", "D9", "D8", "D7"]  # pads 14..8


def xiao_symbol(name='hue:XIAO_ESP32C6'):
    pins = []
    for i, n in enumerate(XIAO_LEFT):
        pins.append((str(i + 1), n, -12.7, 7.62 - 2.54 * i, 0, "bidirectional"))
    types = {"5V": "power_in", "GND": "power_in", "3V3": "power_out"}
    for i, n in enumerate(XIAO_RIGHT):
        pins.append((str(14 - i), n, 12.7, 7.62 - 2.54 * i, 180, types.get(n, "bidirectional")))
    pin_s = "\n".join(
        f'      (pin {t} line (at {x} {y} {a}) (length 2.54)\n'
        f'        (name "{n}" (effects (font (size 1.27 1.27))))\n'
        f'        (number "{num}" (effects (font (size 1.27 1.27)))))'
        for num, n, x, y, a, t in pins)
    short = name.split(":")[-1]
    return f'''(symbol "{name}"
    (pin_names (offset 1.016))
    (exclude_from_sim no) (in_bom yes) (on_board yes)
    (property "Reference" "U" (at 0 12.7 0) (effects (font (size 1.27 1.27))))
    (property "Value" "XIAO ESP32-C6" (at 0 -12.7 0) (effects (font (size 1.27 1.27))))
    (property "Footprint" "XIAO:XIAO-ESP32-C6-SMD-EdgePads" (at 0 -15.24 0) (effects (font (size 1.27 1.27)) (hide yes)))
    (property "Datasheet" "https://wiki.seeedstudio.com/xiao_esp32c6_getting_started/" (at 0 -17.78 0) (effects (font (size 1.27 1.27)) (hide yes)))
    (property "Description" "Seeed Studio XIAO ESP32-C6 module, castellated pads" (at 0 0 0) (effects (font (size 1.27 1.27)) (hide yes)))
    (symbol "{short}_0_1"
      (rectangle (start -10.16 10.16) (end 10.16 -10.16) (stroke (width 0.254) (type default)) (fill (type background))))
    (symbol "{short}_1_1"
{pin_s}))'''


XIAO_SYMBOL = xiao_symbol()


def write_local_symbol_lib():
    path = os.path.join(KICAD_DIR, "lib", "hue.kicad_sym")
    body = xiao_symbol('XIAO_ESP32C6')
    with open(path, "w", encoding="utf8", newline="\n") as f:
        f.write('(kicad_symbol_lib (version 20241209) (generator "hue_build_sch")\n  ' + body + "\n)\n")
    # project library table: the local XIAO symbol plus the stock libraries used
    libs = sorted({p["symbol"].split(":")[0] for p in design.PARTS.values()} | {"power"})
    rows = []
    for lib in libs:
        uri = ("${KIPRJMOD}/lib/hue.kicad_sym" if lib == "hue"
               else "${KICAD10_SYMBOL_DIR}/%s.kicad_sym" % lib)
        rows.append('  (lib (name "%s")(type "KiCad")(uri "%s")(options "")(descr ""))' % (lib, uri))
    with open(os.path.join(KICAD_DIR, "sym-lib-table"), "w", encoding="utf8", newline="\n") as f:
        f.write("(sym_lib_table\n  (version 7)\n" + "\n".join(rows) + "\n)\n")


# ------------------------------------------------------------------ placement on the sheet
# (x, y) of each symbol on an A4 landscape sheet, mm, y down. Snapped to 1.27.
S = {}


def at(ref, x, y):
    S[ref] = (round(x / 1.27) * 1.27, round(y / 1.27) * 1.27)


at("J1", 30.48, 50.8)
at("F1", 50.8, 50.8)
at("RV1", 71.12, 55.88)
at("PS1", 104.14, 50.8)
at("D1", 147.32, 45.72)
at("C1", 135.89, 58.42)
at("C2", 163.83, 58.42)
at("U1", 60.96, 124.46)
for i in range(6):
    x = 127 + i * 22.86
    at(f"R{i + 1}", x, 104.14)
    at(f"R{i + 11}", x + 8.89, 124.46)
    at(f"C{i + 11}", x, 139.7)
at("J2", 256.54, 165.1)
at("J3", 256.54, 182.88)

FLAGS = [("AC_L", 88.9, 71.12), ("AC_N", 96.52, 71.12), ("+5V", 175.26, 45.72)]


# ------------------------------------------------------------------ emit
def fmt(v):
    return ("%.4f" % v).rstrip("0").rstrip(".")


def label(net, x, y, angle):
    """Global label: net names come out without a sheet prefix, like the PCB's."""
    just = {0: "left", 90: "left", 180: "right", 270: "right"}[angle]
    return (f'  (global_label "{net}" (shape passive) (at {fmt(x)} {fmt(y)} {angle}) (fields_autoplaced yes)\n'
            f'    (effects (font (size 1.27 1.27)) (justify {just})) (uuid "{uid("label", net, x, y)}")\n'
            f'    (property "Intersheetrefs" "${{INTERSHEET_REFS}}" (at {fmt(x)} {fmt(y)} 0)\n'
            f'      (effects (font (size 1.27 1.27)) (hide yes))))')


def wire(x1, y1, x2, y2):
    return (f'  (wire (pts (xy {fmt(x1)} {fmt(y1)}) (xy {fmt(x2)} {fmt(y2)}))\n'
            f'    (stroke (width 0) (type default)) (uuid "{uid("wire", x1, y1, x2, y2)}"))')


def stub_and_label(net, px, py, pin_angle, length=2.54):
    """pin_angle points from the pin end into the body; go the other way."""
    out = (pin_angle + 180) % 360
    dx, dy = {0: (1, 0), 90: (0, -1), 180: (-1, 0), 270: (0, 1)}[int(out)]
    ex, ey = px + dx * length, py + dy * length
    return [wire(px, py, ex, ey), label(net, ex, ey, int(out))]


def symbol_instance(ref, lib_id, x, y, props, pin_nums, hidden=(), in_bom=True, on_board=True):
    lines = [f'  (symbol (lib_id "{lib_id}") (at {fmt(x)} {fmt(y)} 0) (unit 1)',
             f'    (exclude_from_sim no) (in_bom {"yes" if in_bom else "no"}) '
             f'(on_board {"yes" if on_board else "no"}) (dnp no) (fields_autoplaced yes)',
             f'    (uuid "{design.sym_uuid(ref)}")']
    for name, (value, px, py) in props.items():
        hide = " (hide yes)" if name in hidden else ""
        lines.append(f'    (property "{name}" "{value}" (at {fmt(px)} {fmt(py)} 0)\n'
                     f'      (effects (font (size 1.27 1.27)){hide}))')
    for n in pin_nums:
        lines.append(f'    (pin "{n}" (uuid "{uid("pin", ref, n)}"))')
    lines.append(f'    (instances (project "{PROJECT}" (path "/{ROOT_UUID}" (reference "{ref}") (unit 1)))))')
    return "\n".join(lines)


def build():
    lib_ids = sorted({p["symbol"] for p in design.PARTS.values()} | {"power:PWR_FLAG"})
    blocks = {lid: lib_symbol(lid) for lid in lib_ids}
    body = []

    for ref, p in sorted(design.PARTS.items()):
        blk = blocks[p["symbol"]]
        x, y = S[ref]
        rx, ry = prop_pos(blk, "Reference")
        vx, vy = prop_pos(blk, "Value")
        # 2-pin vertical parts: put ref/value to the right
        if p["symbol"] in ("Device:R", "Device:C", "Device:Fuse", "Device:Varistor"):
            rx, ry, vx, vy = 2.54, 1.27, 2.54, -1.27
        props = {
            "Reference": (ref, x + rx, y - ry),
            "Value": (p["value"], x + vx, y - vy),
            "Footprint": (p["footprint"], x, y + 7.62),
            "Datasheet": ("", x, y),
            "Description": (p["note"], x, y),
            "LCSC": (p["lcsc"], x, y),
            "MPN": (p["mpn"], x, y),
        }
        pins = pins_of(blk)
        body.append(symbol_instance(ref, p["symbol"], x, y, props, [n for n, *_ in pins],
                                    hidden=("Footprint", "Datasheet", "Description", "LCSC", "MPN")))
        for num, px, py, ang, _t in pins:
            ax, ay = x + px, y - py
            net = p["pins"].get(num)
            if net:
                body.extend(stub_and_label(net, ax, ay, ang))
            elif (ref, num) in design.NO_CONNECT:
                body.append(f'  (no_connect (at {fmt(ax)} {fmt(ay)}) (uuid "{uid("nc", ref, num)}"))')
            else:
                raise SystemExit(f"{ref} pin {num} has no net and is not marked no-connect")

    flag_blk = blocks["power:PWR_FLAG"]
    for i, (net, x, y) in enumerate(FLAGS):
        ref = f"#FLG0{i + 1}"
        props = {"Reference": (ref, x, y - 3.81), "Value": ("PWR_FLAG", x, y - 5.08),
                 "Footprint": ("", x, y), "Datasheet": ("", x, y), "Description": ("", x, y)}
        body.append(symbol_instance(ref, "power:PWR_FLAG", x, y, props, ["1"],
                                    hidden=("Reference", "Footprint", "Datasheet", "Description"),
                                    in_bom=False, on_board=False))
        body.extend(stub_and_label(net, x, y, 90))

    notes = [
        (20.32, 30.48, "MAINS SIDE - 100-240 V AC. Keep >= 6 mm to everything on the low-voltage side."),
        (20.32, 33.02, "F1 before everything; RV1 across L/N after the fuse, at the module input."),
        (120.65, 30.48, "5 V from the certified, isolated HLK-PM01 (3 kV AC). D1 stops USB back-feeding it."),
        (20.32, 96.52, "XIAO ESP32-C6. Pin map fixed by the firmware: D0-D5 = GPIO 0, 1, 2, 21, 22, 23."),
        (20.32, 99.06, "D6-D10 unused. BOOT (GPIO9) and RESET are the buttons on the XIAO itself."),
        (120.65, 96.52, "Each input: 10k pull-up at the pin, 1k series from the terminal, 10nF at the pin."),
        (120.65, 99.06, "Closed wall switch = pin to GND = active. Terminals are LOW VOLTAGE ONLY (3.3 V)."),
        (20.32, 190.5, "NEVER connect USB while the board is on mains. Uncertified design: installation by an electrician."),
    ]
    for x, y, t in notes:
        body.append(f'  (text "{t}" (exclude_from_sim no) (at {fmt(x)} {fmt(y)} 0)\n'
                    f'    (effects (font (size 1.27 1.27)) (justify left bottom)) (uuid "{uid("text", t)}"))')

    lib_s = "\n".join("    " + b.replace("\n", "\n    ") for b in blocks.values())
    sch = f'''(kicad_sch
  (version 20250114)
  (generator "hue_build_sch")
  (generator_version "9.0")
  (uuid "{ROOT_UUID}")
  (paper "A4")
  (title_block
    (title "hue-simple-switch mains carrier")
    (date "{design.DATE}")
    (rev "{design.REV}")
    (company "github.com/tineira/hue-simple-switch")
    (comment 1 "Generated by hardware/scripts/build_sch.py from design.py. Do not edit by hand.")
    (comment 2 "Uncertified, AI-assisted mains design. Installation by a qualified electrician.")
  )
  (lib_symbols
{lib_s}
  )
{chr(10).join(body)}
  (sheet_instances (path "/" (page "1")))
  (embedded_fonts no)
)
'''
    with open(SCH_PATH, "w", encoding="utf8", newline="\n") as f:
        f.write(sch)
    write_local_symbol_lib()


if __name__ == "__main__":
    build()
    print("wrote", SCH_PATH)
