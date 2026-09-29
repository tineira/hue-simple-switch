"""Regenerate everything in hardware/ from design.py and layout.py.

Run with KiCad's bundled Python (it has pcbnew), from the repo root:

    "%LOCALAPPDATA%\\Programs\\KiCad\\10.0\\bin\\python.exe" hardware\\scripts\\build_all.py

Steps: schematic -> ERC -> PCB -> zone refill + DRC (with schematic parity)
-> Gerbers/drill zip, BOM, CPL, PDFs, STEP, renders -> enclosure STLs (if
OpenSCAD is installed). Fails if ERC or DRC reports an error.
"""
import csv
import glob
import os
import shutil
import subprocess
import sys
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import build_pcb  # noqa: E402
import build_sch  # noqa: E402
import design  # noqa: E402

HW = os.path.normpath(os.path.join(HERE, ".."))
KICAD = os.path.join(HW, "kicad")
FAB = os.path.join(HW, "fab")
ENCL = os.path.join(HW, "enclosure")
NAME = "hue-simple-switch-mains"
SCH = os.path.join(KICAD, NAME + ".kicad_sch")
PCB = os.path.join(KICAD, NAME + ".kicad_pcb")
BIN = os.path.dirname(sys.executable)
CLI = os.path.join(BIN, "kicad-cli.exe" if os.name == "nt" else "kicad-cli")
OPENSCAD = next((p for p in [shutil.which("openscad"),
                             r"C:\Program Files\OpenSCAD\openscad.com",
                             r"C:\Program Files\OpenSCAD\openscad.exe"] if p and os.path.exists(p)), None)


def run(*args):
    print("+", " ".join(os.path.basename(a) if i == 0 else a for i, a in enumerate(args)))
    r = subprocess.run(args, capture_output=True, text=True)
    if r.returncode != 0:
        print(r.stdout, r.stderr)
        raise SystemExit(f"failed: {args[0]} {args[1:3]}")
    return r.stdout


def count_errors(report):
    with open(report, encoding="utf8") as f:
        text = f.read()
    blocks = text.split("\n[")
    return sum(1 for b in blocks[1:] if "; error" in b.split("\n", 2)[1])


def main():
    os.makedirs(FAB, exist_ok=True)

    # 1. schematic + ERC
    build_sch.build()
    erc = os.path.join(FAB, "erc.rpt")
    run(CLI, "sch", "erc", "--severity-all", "-o", erc, SCH)
    if count_errors(erc):
        raise SystemExit("ERC errors, see " + erc)

    # 2. PCB, then refill pours under the project rules and check against the schematic
    build_pcb.build()
    drc = os.path.join(FAB, "drc.rpt")
    run(CLI, "pcb", "drc", "--schematic-parity", "--refill-zones", "--save-board",
        "--severity-all", "-o", drc, PCB)
    if count_errors(drc):
        raise SystemExit("DRC errors, see " + drc)

    # 3. Gerbers + drill for JLCPCB (Protel extensions, one zip)
    gdir = os.path.join(FAB, "gerbers")
    shutil.rmtree(gdir, ignore_errors=True)
    os.makedirs(gdir)
    layers = "F.Cu,B.Cu,F.Paste,B.Paste,F.SilkS,B.SilkS,F.Mask,B.Mask,Edge.Cuts"
    run(CLI, "pcb", "export", "gerbers", "--layers", layers, "--use-drill-file-origin",
        "--subtract-soldermask", "-o", gdir + os.sep, PCB)
    run(CLI, "pcb", "export", "drill", "--format", "excellon", "--excellon-units", "mm",
        "--excellon-separate-th", "--generate-map", "--map-format", "gerberx2",
        "-o", gdir + os.sep, PCB)
    zpath = os.path.join(FAB, NAME + "-gerbers-jlcpcb.zip")
    with zipfile.ZipFile(zpath, "w", zipfile.ZIP_DEFLATED) as z:
        for f in sorted(glob.glob(os.path.join(gdir, "*"))):
            z.write(f, os.path.basename(f))
    shutil.rmtree(gdir)

    # 4. BOM (JLCPCB columns plus hand-assembly notes) and CPL
    write_bom(os.path.join(FAB, NAME + "-bom.csv"))
    pos = os.path.join(FAB, "pos-raw.csv")
    run(CLI, "pcb", "export", "pos", "--format", "csv", "--units", "mm", "--side", "both",
        "--use-drill-file-origin", "-o", pos, PCB)
    write_cpl(pos, os.path.join(FAB, NAME + "-cpl.csv"))
    os.remove(pos)

    # 5. PDFs: schematic, and one assembly page per side
    run(CLI, "sch", "export", "pdf", "-o", os.path.join(FAB, NAME + "-schematic.pdf"), SCH)
    run(CLI, "pcb", "export", "pdf", "--mode-single", "--layers", "F.Fab,F.SilkS,Edge.Cuts,F.Cu",
        "--include-border-title", "-o", os.path.join(FAB, NAME + "-assembly-top.pdf"), PCB)
    run(CLI, "pcb", "export", "pdf", "--mode-single", "--mirror", "--layers",
        "B.Fab,B.SilkS,Edge.Cuts,B.Cu", "--include-border-title",
        "-o", os.path.join(FAB, NAME + "-assembly-bottom.pdf"), PCB)

    # 6. STEP (for the enclosure) and renders (for the README)
    run(CLI, "pcb", "export", "step", "--force", "--subst-models", "-o",
        os.path.join(ENCL, NAME + "-board.step"), PCB)
    img = os.path.join(HW, "images")
    os.makedirs(img, exist_ok=True)
    for side, rot, fname in (("top", "", "board-top.png"), ("bottom", "", "board-bottom.png"),
                             ("bottom", "-55,0,-35", "board-iso-bottom.png"),
                             ("top", "-50,0,30", "board-iso-top.png")):
        args = [CLI, "pcb", "render", "--side", side, "--width", "1200", "--height", "1000",
                "--quality", "high", "--background", "opaque", "-o", os.path.join(img, fname)]
        if rot:
            args += ["--rotate", rot, "--perspective", "--zoom", "0.85"]
        run(*args, PCB)

    # 7. enclosure STLs
    if OPENSCAD:
        scad = os.path.join(ENCL, "enclosure.scad")
        for part in ("base", "lid"):
            run(OPENSCAD, "-o", os.path.join(ENCL, f"enclosure-{part}.stl"),
                "-D", f'part="{part}"', scad)
    else:
        print("OpenSCAD not found: enclosure STLs not rebuilt")
    print("done")


def write_bom(path):
    groups = {}
    for ref, p in design.PARTS.items():
        key = (p["value"], p["footprint"], p["lcsc"], p["mpn"])
        groups.setdefault(key, []).append(ref)

    def refkey(r):
        head = r.rstrip("0123456789")
        return (head, int(r[len(head):]))

    rows = sorted(groups.items(), key=lambda kv: refkey(sorted(kv[1], key=refkey)[0]))
    with open(path, "w", newline="", encoding="utf8") as f:
        w = csv.writer(f)
        w.writerow(["Comment", "Designator", "Footprint", "LCSC Part #", "Qty", "Manufacturer part",
                    "Side", "Notes"])
        for (value, fp, lcsc, mpn), refs in rows:
            refs = sorted(refs, key=refkey)
            p = design.PARTS[refs[0]]
            w.writerow([value, ",".join(refs), fp.split(":")[1], lcsc, len(refs), mpn,
                        p["side"], p["note"]])


def write_cpl(raw, path):
    with open(raw, encoding="utf8") as f:
        rows = list(csv.DictReader(f))
    with open(path, "w", newline="", encoding="utf8") as f:
        w = csv.writer(f)
        w.writerow(["Designator", "Mid X", "Mid Y", "Layer", "Rotation"])
        for r in rows:
            w.writerow([r["Ref"], r["PosX"] + "mm", r["PosY"] + "mm",
                        "Top" if r["Side"] == "top" else "Bottom", r["Rot"]])


if __name__ == "__main__":
    main()
