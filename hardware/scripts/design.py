"""Single source of truth for the hue-simple-switch mains carrier board.

Parts, nets and placement live here. build_sch.py and build_pcb.py read it, so
the schematic and the board cannot drift apart.

Coordinates are millimetres relative to the board centre, KiCad style
(x right, y down). "top" is the XIAO side, "bottom" the power-module side.
"""
import uuid

# Board outline: a disc cut by two straight sides. It fits a Ø56 x 46 mm
# enclosure (EU round box, US single gang, UK 86x86, Chilean rectangular).
BOARD_R = 26.0        # radius of the round ends
BOARD_HALF_W = 21.0   # half width between the straight sides
BOARD_T = 1.6

# Pin map from README.md / docs/wiring-switches.svg. Do not change.
INPUTS = ["D0", "D1", "D2", "D3", "D4", "D5"]

# XIAO ESP32-C6 edge pads (Seeed footprint numbering, USB at pad 1/14 end).
XIAO_PADS = {
    "1": "D0", "2": "D1", "3": "D2", "4": "D3", "5": "D4", "6": "D5", "7": "D6",
    "8": "D7", "9": "D8", "10": "D9", "11": "D10", "12": "3V3", "13": "GND", "14": "5V",
}

REV = "1.0"
DATE = "2026-09-28"

# Stable ids, so the schematic symbols and the PCB footprints stay linked
# across regenerations.
UUID_NS = uuid.UUID("5b1c4a9e-2f59-4d7e-9a57-8c1f3e0b6a21")


def sym_uuid(ref):
    return str(uuid.uuid5(UUID_NS, "symbol/" + ref))


# ref: dict(value, symbol, footprint, lcsc, mpn, pins{pad: net}, side, at(x, y), rot, dnp)
PARTS = {}


def part(ref, value, symbol, footprint, pins, side, at, rot=0, lcsc="", mpn="", note=""):
    PARTS[ref] = dict(value=value, symbol=symbol, footprint=footprint, pins=pins,
                      side=side, at=at, rot=rot, lcsc=lcsc, mpn=mpn, note=note)


# ---------------------------------------------------------------- mains side
part("J1", "MAINS L/N", "Connector:Screw_Terminal_01x02",
     "TerminalBlock_Phoenix:TerminalBlock_Phoenix_MKDS-1,5-2-5.08_1x02_P5.08mm_Horizontal",
     {"1": "AC_L_IN", "2": "AC_N"}, "bottom", (0, 0),
     lcsc="C474952", mpn="KF128-5.08-2P (Cixi Kefa)", note="300 V, 5.08 mm pitch, up to 2.5 mm2")
part("F1", "T500mA 250V", "Device:Fuse", "Fuse:Fuse_Littelfuse_372_D8.50mm",
     {"1": "AC_L_IN", "2": "AC_L"}, "bottom", (0, 0),
     lcsc="C142835", mpn="Littelfuse 37205000001 (TR5 372, time-lag)")
part("RV1", "275VAC", "Device:Varistor", "Resistor_SMD:R_1210_3225Metric",
     {"1": "AC_L", "2": "AC_N"}, "top", (0, 0),
     lcsc="C211106", mpn="TDK SIOV B72650M0271K072 (CU3225K275G2), 275 VAC, SMD 3225",
     note="across the module input, after the fuse")
part("PS1", "HLK-PM01", "Converter_ACDC:HLK-PM01",
     "Converter_ACDC:Converter_ACDC_Hi-Link_HLK-PMxx",
     {"1": "AC_L", "2": "AC_N", "3": "GND", "4": "+5V_PSU"}, "bottom", (0, 0),
     lcsc="C209903", mpn="Hi-Link HLK-PM01, 5 V 0.6 A, 3 kVAC isolation")

# ---------------------------------------------------------- low-voltage side
part("D1", "SS34", "Device:D_Schottky", "Diode_SMD:D_SMA",
     {"1": "+5V", "2": "+5V_PSU"}, "top", (0, 0),
     lcsc="C8678", mpn="SS34", note="blocks USB back-feed into the power module")
part("C1", "22uF 25V", "Device:C", "Capacitor_SMD:C_0805_2012Metric",
     {"1": "+5V_PSU", "2": "GND"}, "top", (0, 0), lcsc="C45783", mpn="CL21A226MAQNNNE")
part("C2", "22uF 25V", "Device:C", "Capacitor_SMD:C_0805_2012Metric",
     {"1": "+5V", "2": "GND"}, "top", (0, 0), lcsc="C45783", mpn="CL21A226MAQNNNE")
UNUSED_XIAO = ("D6", "D7", "D8", "D9", "D10")
part("U1", "XIAO ESP32-C6", "hue:XIAO_ESP32C6", "XIAO:XIAO-ESP32-C6-SMD-EdgePads",
     {p: {"5V": "+5V", "3V3": "+3V3"}.get(n, n)
      for p, n in XIAO_PADS.items() if n not in UNUSED_XIAO}, "top", (0, 0),
     mpn="Seeed Studio XIAO ESP32-C6 (113991254)", note="soldered flat by its castellated edge")

for i, name in enumerate(INPUTS):
    n = i + 1
    part(f"R{n}", "10k", "Device:R", "Resistor_SMD:R_0603_1608Metric",
         {"1": "+3V3", "2": name}, "top", (0, 0), lcsc="C25804", mpn="0603WAF1002T5E",
         note="pull-up, next to the pin")
    part(f"R{n + 10}", "1k", "Device:R", "Resistor_SMD:R_0603_1608Metric",
         {"1": f"IN_{name}", "2": name}, "bottom", (0, 0), lcsc="C21190", mpn="0603WAF1001T5E",
         note="series, limits surge current into the pin")
    part(f"C{n + 10}", "10nF", "Device:C", "Capacitor_SMD:C_0603_1608Metric",
         {"1": name, "2": "GND"}, "top", (0, 0), lcsc="C57112", mpn="0603B103K500NT",
         note="with the 1k, filters noise picked up by the wall wiring")

# Wall-switch inputs, low voltage only. 3.5 mm pitch, 0.5 to 1.0 mm2 solid wire.
part("J2", "D0 D1 D2 D3", "Connector:Screw_Terminal_01x04",
     "TerminalBlock_Phoenix:TerminalBlock_Phoenix_PT-1,5-4-3.5-H_1x04_P3.50mm_Horizontal",
     {"1": "IN_D0", "2": "IN_D1", "3": "IN_D2", "4": "IN_D3"}, "bottom", (0, 0),
     lcsc="C557656", mpn="XY350V-3.5-4P (Ningbo Xinlaiya)")
part("J3", "D4 D5 GND", "Connector:Screw_Terminal_01x03",
     "TerminalBlock_Phoenix:TerminalBlock_Phoenix_PT-1,5-3-3.5-H_1x03_P3.50mm_Horizontal",
     {"1": "IN_D4", "2": "IN_D5", "3": "GND"}, "bottom", (0, 0),
     lcsc="C474893", mpn="KF350-3.5-3P (Cixi Kefa)")

NO_CONNECT = {("U1", p) for p, n in XIAO_PADS.items() if n in UNUSED_XIAO}
