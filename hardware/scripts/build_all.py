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
import build_panel  # noqa: E402
import footprints  # noqa: E402

HW = os.path.normpath(os.path.join(HERE, ".."))
KICAD = os.path.join(HW, "kicad")
FAB = os.path.join(HW, "fab")
ENCL = os.path.join(HW, "enclosure")
NAME = "hue-simple-switch-mains"
SCH = os.path.join(KICAD, NAME + ".kicad_sch")
PANEL = os.path.join(KICAD, NAME + "-panel.kicad_pcb")
JLC = os.path.join(FAB, "jlcpcb")
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

    # 0. datasheet footprints and their 3D models
    footprints.build()

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

    # 3. Gerbers + drill (Protel extensions, one zip), BOM and CPL for the single board
    export_gerbers(PCB, os.path.join(FAB, NAME + "-gerbers.zip"))
    write_bom(os.path.join(FAB, NAME + "-bom.csv"))
    write_cpl(PCB, os.path.join(FAB, NAME + "-cpl.csv"))

    # 4. JLCPCB assembly: 3-up panel, its DRC, Gerbers, BOM and CPL (XIAO not placed)
    build_panel.build()
    pdrc = os.path.join(JLC, "panel-drc.rpt")
    os.makedirs(JLC, exist_ok=True)
    run(CLI, "pcb", "drc", "--refill-zones", "--save-board", "--severity-all", "-o", pdrc, PANEL)
    if count_errors(pdrc):
        raise SystemExit("panel DRC errors, see " + pdrc)
    export_gerbers(PANEL, os.path.join(JLC, NAME + "-panel-gerbers.zip"))
    write_bom(os.path.join(JLC, NAME + "-panel-bom.csv"), boards=build_panel.N, jlc=True)
    # single board for JLCPCB assembly: same CPL as fab/, BOM without the XIAO
    write_bom(os.path.join(JLC, NAME + "-single-bom.csv"), jlc=True)
    write_cpl(PANEL, os.path.join(JLC, NAME + "-panel-cpl.csv"), panel=True)

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
    run(CLI, "pcb", "render", "--side", "top", "--width", "1600", "--height", "900", "--quality", "high",
        "--background", "opaque", "--zoom", "1.6", "-o", os.path.join(img, "panel-top.png"), PANEL)

    # 7. enclosure STLs
    if OPENSCAD:
        scad = os.path.join(ENCL, "enclosure.scad")
        for part in ("base", "lid"):
            run(OPENSCAD, "-o", os.path.join(ENCL, f"enclosure-{part}.stl"),
                "-D", f'part="{part}"', scad)
    else:
        print("OpenSCAD not found: enclosure STLs not rebuilt")
    print("done")


def export_gerbers(pcb, zpath):
    gdir = os.path.join(os.path.dirname(zpath), "gerbers-tmp")
    shutil.rmtree(gdir, ignore_errors=True)
    os.makedirs(gdir)
    layers = "F.Cu,B.Cu,F.Paste,B.Paste,F.SilkS,B.SilkS,F.Mask,B.Mask,Edge.Cuts"
    run(CLI, "pcb", "export", "gerbers", "--layers", layers, "--use-drill-file-origin",
        "--subtract-soldermask", "-o", gdir + os.sep, pcb)
    run(CLI, "pcb", "export", "drill", "--format", "excellon", "--excellon-units", "mm",
        "--drill-origin", "plot", "--excellon-separate-th", "--generate-map",
        "--map-format", "gerberx2", "-o", gdir + os.sep, pcb)
    with zipfile.ZipFile(zpath, "w", zipfile.ZIP_DEFLATED) as z:
        for f in sorted(glob.glob(os.path.join(gdir, "*"))):
            z.write(f, os.path.basename(f))
    shutil.rmtree(gdir)


NOT_ASSEMBLED = {"U1"}   # the XIAO: not an LCSC part, soldered by hand


def refkey(r):
    head = r.rstrip("0123456789_")
    tail = r[len(head):].split("_")
    return (head, [int(t) for t in tail if t])


def write_bom(path, boards=1, jlc=False):
    """boards > 1: designators get the board number (R1_1, R1_2, ...)."""
    groups = {}
    for ref, p in design.PARTS.items():
        if jlc and ref in NOT_ASSEMBLED:
            continue
        key = (p["value"], p["footprint"], p["lcsc"], p["mpn"])
        names = [ref] if boards == 1 else [f"{ref}_{i + 1}" for i in range(boards)]
        groups.setdefault(key, []).extend(names)
    rows = sorted(groups.items(), key=lambda kv: refkey(sorted(kv[1], key=refkey)[0]))
    with open(path, "w", newline="", encoding="utf8") as f:
        w = csv.writer(f)
        if jlc:
            w.writerow(["Comment", "Designator", "Footprint", "LCSC Part #"])
        else:
            w.writerow(["Comment", "Designator", "Footprint", "LCSC Part #", "Qty",
                        "Manufacturer part", "Side", "Notes"])
        for (value, fp, lcsc, mpn), refs in rows:
            refs = sorted(refs, key=refkey)
            p = design.PARTS[refs[0].split("_")[0]]
            if jlc:
                w.writerow([value, ",".join(refs), fp.split(":")[1], lcsc])
            else:
                note = p["note"] + ("; not assembled by JLCPCB" if refs[0] in NOT_ASSEMBLED else "")
                w.writerow([value, ",".join(refs), fp.split(":")[1], lcsc, len(refs), mpn,
                            p["side"], note.strip("; ")])


def write_cpl(pcb, path, panel=False):
    """JLCPCB CPL from the board itself.

    Mid X/Y is the centre of the part's pads, not the footprint origin: KiCad puts
    the origin of through-hole footprints on pin 1, which JLCPCB would read as the
    part's centre. Coordinates are from the drill/place origin, y up (as in the
    Gerbers). Rotation is KiCad's; check polarised parts in JLCPCB's preview.
    """
    import pcbnew
    b = pcbnew.LoadBoard(pcb)
    org = b.GetDesignSettings().GetAuxOrigin()
    centres = [pcbnew.FromMM(build_panel.CX + x) for x, _ in build_panel.board_centres()]
    rows = []
    for fp in b.GetFootprints():
        ref = fp.GetReference()
        if ref not in design.PARTS or ref in NOT_ASSEMBLED:
            continue   # panel fixtures (tabs, fiducials, tooling holes) and the XIAO
        pads = [p.GetPosition() for p in fp.Pads()]
        cx = sum(p.x for p in pads) / len(pads)
        cy = sum(p.y for p in pads) / len(pads)
        if panel:
            i = min(range(len(centres)), key=lambda k: abs(centres[k] - cx))
            ref = f"{ref}_{i + 1}"
        rot = (fp.GetOrientationDegrees() + design.PARTS[fp.GetReference()].get("jlc_rot", 0)) % 360
        rows.append((ref, pcbnew.ToMM(cx - org.x), pcbnew.ToMM(org.y - cy),
                     "Bottom" if fp.IsFlipped() else "Top", rot))
    rows.sort(key=lambda r: refkey(r[0]))
    with open(path, "w", newline="", encoding="utf8") as f:
        w = csv.writer(f)
        w.writerow(["Designator", "Mid X", "Mid Y", "Layer", "Rotation"])
        for ref, x, y, layer, rot in rows:
            w.writerow([ref, f"{x:.3f}mm", f"{y:.3f}mm", layer, f"{rot:g}"])


if __name__ == "__main__":
    main()
