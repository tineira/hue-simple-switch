"""Rule proof: on a scratch copy, break each mains rule on purpose and show DRC reports it. A
.kicad_dru KiCad cannot parse is dropped silently, so a rule that does not fire stops the build.
The scratch copy is deleted. Results in reports/proof.json and panel/panel-proof.json."""
import collections
import json
import os
import shutil
import subprocess
import sys
import tempfile

import pcbnew

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import build  # noqa: E402

CLEARANCE, CREEPAGE = "Mains to low voltage clearance", "Mains to low voltage creepage"
EDGE, HOLE = "Mains to board edge and slot", "Mains to unplated holes"
KEEPOUT = "No low voltage copper in the mains area"


def count(path):
    with open(path, encoding="utf8") as f:
        d = json.load(f)
    c = collections.Counter(v["type"] for v in d.get("violations", []))
    return dict(violations=dict(c), unconnected=len(d.get("unconnected_items", [])),
                parity=len(d.get("schematic_parity", [])))


def track(board, net, a, c, layer):
    t = pcbnew.PCB_TRACK(board)
    t.SetStart(pcbnew.VECTOR2I(pcbnew.FromMM(a[0]), pcbnew.FromMM(a[1])))
    t.SetEnd(pcbnew.VECTOR2I(pcbnew.FromMM(c[0]), pcbnew.FromMM(c[1])))
    t.SetWidth(pcbnew.FromMM(0.25))
    t.SetLayer(layer)
    t.SetNet(net)
    board.Add(t)


def break_rules(board, ox, oy, suffix, bites):
    """One board, its centre at (ox, oy) on the sheet."""
    nets = board.GetNetsByName()
    gnd, mains = nets["GND" + suffix], nets["AC_N" + suffix]

    def add(net, x0, y0, x1, y1, layer):
        track(board, net, (ox + x0, oy + y0), (ox + x1, oy + y1), layer)
    # Top: on the low-voltage side of the slot, 3 mm from F1's pads.
    # Bottom: on the mains side of the slot, about 3 mm from PS1's N pin.
    add(gnd, -4.9, 0.0, -4.9, 4.0, pcbnew.F_Cu)
    add(gnd, -8.4, -6.0, -8.4, -2.5, pcbnew.B_Cu)
    # Mains on the left edge, between the tabs, and at the mouse bites of the lower left tab
    add(mains, -18.75, -5.8, -18.75, -5.2, pcbnew.F_Cu)
    if bites:
        add(mains, -18.75, -3.5, -18.75, -2.5, pcbnew.F_Cu)


def prove(pcb, boards, sheet, bites, out):
    """boards: (x, y, net suffix) about the sheet centre. sheet: that centre."""
    tmp = tempfile.mkdtemp(prefix="v2fit-proof-")
    try:
        stem = pcb[:-len(".kicad_pcb")]
        for ext in (".kicad_pro", ".kicad_dru"):
            shutil.copy(stem + ext, os.path.join(tmp, "scratch" + ext))
        scratch = os.path.join(tmp, "scratch.kicad_pcb")
        board = pcbnew.LoadBoard(pcb)
        for x, y, suffix in boards:
            break_rules(board, sheet[0] + x, sheet[1] + y, suffix, bites)
        board.Save(scratch)
        rpt = os.path.join(tmp, "scratch.json")
        subprocess.run([build.CLI, "pcb", "drc", "--severity-all", "--format", "json", "-o", rpt, scratch],
                       check=True, capture_output=True, text=True)
        res = dict(scratch=count(rpt))
        with open(rpt, encoding="utf8") as f:
            d = json.load(f)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    names = [CLEARANCE, CREEPAGE, EDGE, KEEPOUT] + ([HOLE] if bites else [])
    hits = {n: sum("'%s'" % n in v["description"] for v in d["violations"]) for n in names}
    res["rule_hits"] = hits
    with open(out, "w") as f:
        json.dump(res, f, indent=1)
    print(json.dumps(res, indent=1))
    # the GND tracks and the mains track must each fire on every board
    want = {n: len(boards) * (2 if n in (CLEARANCE, CREEPAGE) else 1) for n in names}
    short = {n: (hits[n], want[n]) for n in names if hits[n] < want[n]}
    if short:
        raise SystemExit("rule proof FAILED, rule: (hits, wanted): %s. Custom rules may not be loaded." % short)


def main():
    prove(build.PCB, [(0.0, 0.0, "")], (100.0, 100.0), False, os.path.join(build.REPORTS, "proof.json"))


if __name__ == "__main__":
    main()
