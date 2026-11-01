# Mains board v2: routed, soldered wires, all SMD but PS1

**Do not build this yet.** Prototypes were ordered on 2026-11-01 and none has been assembled or tested. They were ordered from the files in `panel/`, unchanged since commit 32dfc4e (Gerber zip sha256 begins b4a389465dc5f92d).

**Routed and connected. Mains isolation passes on the design files and on the copper Gerbers.** A 3-up panel exists in `panel/`, but no part below has been tested. Uncertified, AI-assisted mains design. Nothing here is proof of safety. Tags: **M** measured from the files, **D** datasheet or distributor, **A** assumed.

## Arrangement
Board 37.9 x 23.65 lying flat. PS1 sits flat against its underside, with no spacer, moved 4 mm toward +x. L and N are wires soldered through two holes at the -x end, beside PS1's case. Mains parts sit left of a 1 mm slot; everything else sits right of it. PS1 is the only electrical link, and two copper-free bridges remain at the long edges (2.8 mm each). The XIAO lies long-axis along the box, USB toward +x, on SMD sockets and standard headers. J2 runs along the slot, under the XIAO's left end, and its wires bend out from under the XIAO.

## Routing
- Six input rows, one per J2 pin, each pin -> 10 nF -> 1 k, in a line under the XIAO. Tracks 0.25.
- Signals drop to the bottom after the resistor and fan out in lanes to the D0-D5 socket; D1 rises on top.
- +5V and +5V_PSU on top, 0.5.
- GND is a pour on the bottom, right of the slot. It stays 6.5 from all mains copper.
- J2 pin order is pin 1 to 6 = D0 to D5, pin 7 = GND.

## Parts
| Ref | Part | Note |
| --- | --- | --- |
| L, N | Two wires, holes 1.5 / pads 2.6 | 0.75 mm2 stranded assumed |
| F1 | Littelfuse 0461.500ER, C206981 | 500 mA Slo-Blo, 600 V, 50 A at 250 VAC, 0.56 ohm, melting I2t 0.84 A2s, UL and CSA. Pads match the datasheet. Hi-Link's datasheet V2.21 asks for 0.5 A / 250 VAC slow blow. D |
| J2 | JST B7B-PH-SM4-TB, C495700 | 7 pins, 2.0 pitch, SMD top entry, 30 to 24 AWG, wire OD 0.8 to 1.5, crimped. Body 6.6, mated 8.6. D |
| RV1 | KEMET VP3225K401R275, C600605 | v1 footprint, 4.7 tall. 275 VAC; Hi-Link suggests 10D561K, a larger 350 VAC part. D |
| U1 | Hanxia HX PM2.54-1x6P TP H8.5-YQ, C46635842, and 1x2P, C46635838 | SMD staggered feet, type B. Body 8.5, 8.9 with feet, 15.74 long, 2.5 wide, 3 A, 1000 VAC, gold flash. D. LCSC listing: 540 and 805 on 2026-10-31 |
| Header | 2.54 male header, as XFCN PZ254V-11-07P, C492406, cut to 6 and 2 | Plastic 2.5, post 6, tail 3. D. Not placed by JLCPCB |
| PS1 | HLK-PM01, reused | Flat on the board, pins 3.4 proud of the top |

## Silkscreen
`silk.py` draws all of it, in the look of hue.tineira.com: J2 pin labels (D0 to D5, G), plain references with no panel suffix, a mains warning, the ring logo, and on the bottom strip beside PS1 the 220 V and uncertified marks, so they stay on the board once the rails are off. The rails carry the grid, a ruler, the three setup steps, a 1:3 side view and the order text. Footprint silk is removed and redrawn clear of pads and vias, which is why DRC lists `lib_footprint_mismatch` 22. With the XIAO in place it hides the right edge of the J2 labels; they are meant to be read with it out, when the wires go in. The same goes for the insertion marks: a USB arrow, 5V and GND at the short socket, and marks where the XIAO's USB end lands.

## Isolation, measured two ways
| What | Design files | Copper Gerbers (10 um raster) |
| --- | --- | --- |
| Mains to other copper, same layer | 6.13 | 6.14 top, 9.75 bottom |
| Mains top to other copper bottom, in plan | n/a | 6.39 |
| Mains bottom to other copper top, in plan | n/a | 10.04 |
| Mains to board edge | 1.00 | 1.00 |
| Mains to slot | 1.03 | Included in the edge figure |

The closest pair is J2's mounting tab, copper with no net: 6.13 to F1, 6.15 to RV1. `measure.py` skipped such copper until 2026-11-01, so its 7.135 was wrong; it now agrees. The Gerber script reads each object's net attribute and draws rounded pads with square corners, so it reads low, never high. It found the bottom pour 5.94 from F1's pad across layers, which DRC does not check; the pour margin went from 6.05 to 6.5, and the figure above is after that change.

## Panel
`panel.py` builds 70 x 88.95 mm, 3 boards, 5 panels = 15 boards. Each board has 4 tabs of 5 mm with mouse bites on its short sides, plus 3 fiducials and 4 tooling holes on the frame. Measured per board on the panel's Gerbers: same layer 6.14 top and 9.75 bottom, across layers 6.39 and 10.01, edge and slot 1.00 (0.99 at raster resolution). Unconnected 0. DRC reports no clearance, creepage, short or edge error. KiCad 10 was handing the board's own `MAINS_AREA` rule area and the mouse bites to the creepage rule, which gave 12 false reports; the rule now skips items with no net, and copper with no net stays under the clearance rule. The BOM has every part JLCPCB would place; PS1 and the two wires are left out. `v2-fit-panel-with-ps1-bom.csv` and `-cpl.csv` add PS1 (C209903, bottom, rotation 180) only to see it in JLCPCB's preview. Its pads match LCSC's footprint: 5.0, 29.4, 5.2 and 10.2, with the pad centre on the case centre. Rotations in the CPL were not checked in JLCPCB's preview.

3D to the XIAO, from model numbers: F1 body 9.20, RV1 body 8.31, mains copper about 12.0. M

## Height stack (from box bottom)
PS1 0 to 15.0 (D). Board 15.0 to 16.6. F1 top 19.7, RV1 21.3, J2 with housing 25.2 (D), socket 25.5 (D), XIAO 28.0 to 29.0, USB-C top 32.3, **margin 3.7** (approximate). Room for the wires to bend between the mated J2 and the XIAO: 2.8.

## DRC and rule proof
`kicad-cli pcb drc --refill-zones --save-board --severity-all --format json -o reports/drc.json v2-fit.kicad_pcb`

Unconnected **0**. Clearance 0, creepage 0, shorts 0, edge 0, keepout 0. Others: `courtyards_overlap` 12 (pads and bodies do not touch; smallest body gaps J2 to socket 0.32, R11 and R12 to socket 0.49). The sockets no longer cover PS1's DC pads: the XIAO moved 0.6 mm left for that, and its USB-C end sits 0.6 mm inside the box.

`kicad-cli` exits 0 with violations, and a `.kicad_dru` it cannot parse is dropped silently. So `build.py` and `panel.py` now stop on any clearance, creepage, edge, hole, keepout, short or crossing error and on anything unconnected, and each runs `proof.py`, which stops the build if a rule does not fire.

Rule proof on scratch copies, deleted after: GND tracks about 3 mm from mains on each layer, and mains tracks at the left edge and at a mouse bite. Hits on the board: clearance 2, creepage 2, edge 1, keepout 2. On the panel, all three boards: clearance 9, creepage 6, edge 6, keepout 6, holes 6. Do not narrow these rules to pads or exclude `MAINS_AREA`: two reviews proposed that on 2026-11-01, and on scratch copies each stopped catching the GND tracks. No schematic, so no parity check.

## Moving J2 toward the slot
Not possible beyond 0.13 mm. Its tab is 6.13 from F1 and 6.15 from RV1, and RV1 cannot move left.

## Assembly and use
- **Tabs:** cut them, do not bend the board, and check RV1's N pad after. File every tab flush: each mouse-bite tab can stand 0.25 proud, so 37.9 can become 38.4 in a 38 box. Nominal margin is 0.05 per side along the box and 0.175 across it. M Measure a real box first.
- **Order:** crimp and mate J2 first, with the XIAO out. It cannot be reached afterwards, and any change to the wires means pulling the XIAO.
- **J2 wires:** lead them away from the mains side, never over the slot toward F1 or RV1. Nothing on the board routes them; the box should.
- **XIAO:** USB toward +x, on the silkscreen marks. Nothing keys the sockets. With the header cut to 6 and 2, one pin toward USB puts +5V on the XIAO's GND with no return, and its USB then stands 1.94 past the board edge, so it does not fit the box. One pin the other way gives it no power. Neither was tried.
- **USB:** flash on the bench with no mains connected. The XIAO's USB opening sits 0.6 from the board edge and from the box wall, so a plug fits only outside the box. Updates in the box are over Wi-Fi.

## Weak points
- **SMD sockets:** plugging and pulling the XIAO loads solder feet only, on 8 pins. Not tested.
- **Isolation margin:** 0.13 at the J2 tab. It meets 6.0 and creepage around the slot is longer, but fabrication tolerance was not added to it.
- **Socket reflow:** Hanxia's drawing limits it to 240 C for 30 to 60 s, or 260 C for 5 s. JLCPCB's profile was not checked against that.
- **Socket and header fit:** the drawing does not give the hole size or contact depth. A standard 0.64 mm square post in this socket is assumed.
- **Wire bend:** 7 wires bend within 2.8 mm under the XIAO. Thin wires are likely to fit; a 1.5 mm OD wire is not, and nothing was tried. Input parts sit right beneath that space.
- **PS1 flat:** nothing can be cleaned or inspected under it, and the GND pour lies against its case. Temperature in the closed box against its 60 C limit was not measured.
- **No EMI filter:** Hi-Link says the 0.1 uF safety capacitor and the common mode choke cannot be left out to pass certification. This board has neither and is not certified.
- Nothing holds the board or the wires. Strain relief must be designed into the box.
- 24 AWG is the connector's limit: crimped tails and a splice to thicker wire.
- **F1 breaking capacity:** 50 A at 250 VAC. A hard short on a wall circuit can exceed that, and the datasheet does not cover it. The datasheet is for telecom equipment; it neither lists nor rules out mains input. Owner's choice on 2026-10-31: keep it for this prototype order. Schurter UMT-H (1500 A, C6384960) and Littelfuse 885 do not fit the mains area; BHFUSE BSMD2410C-1100T (100 A, 1 A, C49269485) fits but its rating and UL file are unconfirmed.
- **F1 and surges:** Littelfuse says the 0.5 A part opens in the GR-1089 100 A surge tests. A strong surge can leave the switch dead, and F1 is soldered.

## Not verified
No insulation test, no physical parts, not opened in the KiCad window, no schematic. Socket pads match Hanxia's drawing of the 1x6 part (1.02 x 3.0, 6.0 overall, type B stagger); the 1x2 part has no drawing here, and its pads are 3.17 long in LCSC's footprint, 3.0 here. Panel tab strength is not tested, and the mouse bites of the upper left tab sit 1.0 from RV1's N pad, which meets the edge rule with nothing to spare.
