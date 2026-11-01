"""Rule proof: on a scratch copy, put a GND track 3 mm from mains copper on each layer and
show DRC reports it. The scratch copy is deleted. Result in reports/proof.json."""
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


def count(path):
    with open(path, encoding="utf8") as f:
        d = json.load(f)
    c = collections.Counter(v["type"] for v in d.get("violations", []))
    return dict(violations=dict(c), unconnected=len(d.get("unconnected_items", [])),
                parity=len(d.get("schematic_parity", [])))


def main():
    tmp = tempfile.mkdtemp(prefix="v2fit-proof-")
    try:
        for ext in (".kicad_pro", ".kicad_dru"):
            shutil.copy(build.PCB[:-len(".kicad_pcb")] + ext, os.path.join(tmp, "scratch" + ext))
        scratch = os.path.join(tmp, "scratch.kicad_pcb")
        board = pcbnew.LoadBoard(build.PCB)
        gnd = board.GetNetsByName()["GND"]
        build.v1.OX, build.v1.OY = 100.0, 100.0
        # Top: on the low-voltage side of the slot, 3 mm from F1's pads.
        # Bottom: on the mains side of the slot, about 3 mm from PS1's N pin.
        build.v1.track(board, gnd, [(-4.9, 0.0), (-4.9, 4.0)], 0.25, pcbnew.F_Cu)
        build.v1.track(board, gnd, [(-8.4, -6.0), (-8.4, -2.5)], 0.25, pcbnew.B_Cu)
        board.Save(scratch)
        rpt = os.path.join(tmp, "scratch.json")
        subprocess.run([build.CLI, "pcb", "drc", "--severity-all", "--format", "json", "-o", rpt, scratch],
                       check=True, capture_output=True, text=True)
        res = dict(scratch=count(rpt), base=count(os.path.join(build.REPORTS, "drc.json")))
        with open(rpt, encoding="utf8") as f:
            d = json.load(f)
        hits = []
        for v in d.get("violations", []):
            txt = json.dumps(v["items"])
            if "GND" in txt and "AC_" in txt or v["type"] == "items_not_allowed":
                hits.append(dict(type=v["type"], description=v["description"],
                                 items=[i["description"] for i in v["items"]]))
        res["hits"] = hits
        res["on_top"] = sum("F.Cu" in json.dumps(h["items"]) and "Mains to low" in h["description"] for h in hits)
        res["on_bottom"] = sum("B.Cu" in json.dumps(h["items"]) and "Mains to low" in h["description"] for h in hits)
        rules = [h for h in hits if "Mains to low voltage" in h["description"]]
        if len({h["type"] for h in rules}) < 2:     # a broken .kicad_dru is dropped silently
            raise SystemExit("rule proof FAILED: the 6 mm rules did not fire; custom rules not loaded")
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    with open(os.path.join(build.REPORTS, "proof.json"), "w") as f:
        json.dump(res, f, indent=1)
    print(json.dumps(res, indent=1))


if __name__ == "__main__":
    main()
