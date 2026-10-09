# Check a board before it goes on mains

Checks for an assembled carrier board, done with a multimeter, before it is
connected to mains. They find assembly faults (a short, a missing part, a
reversed diode) and confirm the mains side is isolated from the low-voltage
side. Do them on every board, not one per batch.

Sections 1 to 3 are done **without mains and without USB**. Section 4 is the
first time the board is on mains; do it only when 1 to 3 pass. Do sections 1 to
4 **before soldering the XIAO** (the readings are cleaner), then section 5
once the XIAO is soldered.

All the through-hole parts (J1, J2, J3, F1, PS1) are on the bottom, so their
solder joints are on the top (XIAO) side. Probe those joints, not the terminal
screws: a loose clamp can read open.

## Finding the test points

Seen from the top (XIAO side), USB-C at the left edge, as in
[`images/board-top.png`](images/board-top.png):

| Point | Where |
| --- | --- |
| J1 L, J1 N | The two large pads at the top right. L is the upper one (it goes to the fuse) |
| F1 | Two pads at the top, right of the varistor RV1 |
| PS1 AC L, AC N | The two pads 5 mm apart under RV1 (L on the left) |
| PS1 −Vo, +Vo | The two pads 15.4 mm apart along the bottom edge. −Vo (GND) is the one near the USB edge, +Vo (5 V) the one near C1 |
| D1 | Bottom left. Cathode (band) on the left pad, anode on the right pad |
| U1 D0 to D5 | The row of XIAO pads nearer D1. D0 is the one at the USB end, then D1, D2, ... D5 |
| U1 5V, GND, 3V3 | The other row of XIAO pads (towards the isolation slot), in that order from the USB end. The other four pads in that row are not connected |
| J2 D0 to D3 | Right edge, top to bottom |
| J3 D4, D5, GND | Bottom edge, right to left (GND is the leftmost) |
| R1–R6, R11–R16, C11–C16 | Three columns next to J2, one row per input, D0 at the top. Left column 10 kΩ (marked `01C`), middle 1 kΩ (`01B`), right 10 nF (tan, unmarked) |

The resistor markings are EIA-96 codes: `01C` = 100 × 100 = 10 kΩ, `01B` = 100 × 10 = 1 kΩ.

## 0. Look

With a magnifier:

- D1: cathode band on the left pad (towards C2 and the board edge). Reversed, the XIAO gets no power.
- Mains area (J1, F1, RV1, the AC pins of PS1, the slot): no solder balls, splashes or flux bridges.
- Through-hole joints of PS1, F1, J1, J2, J3: full fillets, no cold joints.
- J1, J2, J3: wire openings face the board edge.
- F1 is marked T500mA.

## 1. Mains side

| Check | Meter mode | Expected | Fault |
| --- | --- | --- | --- |
| J1 L to F1 (terminal side) | Continuity | Beeps, ≈ 0 Ω | Open |
| F1 across | Continuity | Beeps | Open: fuse |
| F1 (module side) to PS1 AC L | Continuity | Beeps | Open |
| J1 N to PS1 AC N | Continuity | Beeps | Open |
| J1 L to J1 N | Ω, 20 MΩ range | **Open**: no beep; OL, or rises to MΩ while the module's input capacitor charges | Beeps, or stays at a few Ω: short |

## 2. Isolation, mains to low voltage

The most important section. Ω, highest range. Hold the probes by the plastic
only and keep the board on a dry, insulating surface: fingers on both probes
read a few MΩ by themselves.

Every reading must **rise slowly and end at OL or tens of MΩ, the same on
every point**. The points on the low-voltage side are joined to each other
through kΩ resistors, so these are all one measurement: mains to the whole
low-voltage side, across the HLK-PM01. On the first boards, with a Fluke 83,
it rises for about a minute and ends near 24 MΩ, with or without the XIAO;
that is the module, not the board.

A reading that stops at a fixed value clearly lower than on other boards is a
leak. Clean the board with isopropyl alcohol (90 % or more), let it dry for an
hour, and measure again. A board with a leak does not go on mains.

- J1 L to J3 GND
- J1 N to J3 GND
- J1 L and J1 N to PS1 +Vo
- J1 L and J1 N to each input terminal, D0 to D5
- J1 L and J1 N to U1 5V
- With the XIAO soldered: J1 L and J1 N to the USB-C shell

A multimeter tests at a few volts and only finds hard shorts. An insulation
tester (500 V DC, L and N joined, against GND) is the real test; an
electrician may have one.

## 3. Low voltage

| Check | Meter mode | Expected | Fault |
| --- | --- | --- | --- |
| D1, red on the anode (right), black on the cathode (left) | Diode | 0.15 to 0.35 V; probes swapped: OL | OL both ways, or ≈ 0 |
| PS1 +Vo to −Vo | Ω, 2 MΩ range | Settles at kΩ or more, no beep | ≈ 0 Ω or beeps |
| U1 5V to U1 GND | Ω, 2 MΩ range | Settles at kΩ or more; less with the probes swapped | ≈ 0 Ω or beeps |
| J3 GND to PS1 −Vo, and to U1 GND | Continuity | Beeps | Open |
| Each input terminal to its own U1 pad (J2 D0 to U1 D0, ...) | Ω, 2 kΩ range | 1 kΩ (R11–R16) | Open or ≈ 0 |
| U1 3V3 to each U1 D0 to D5 pad | Ω, 20 kΩ range | 10 kΩ (R1–R6) | Open or ≈ 0 |
| Each input terminal to J3 GND | Ω, 2 MΩ range | No short; rises to OL (10 nF in between) | ≈ 0 Ω |
| J2 D0 to J2 D1 | Ω, **200 kΩ** range | ≈ 22 kΩ (1k + 10k + 10k + 1k through 3V3). Not a short | ≈ 0 Ω |
| U1 3V3 to U1 GND | Ω, 2 MΩ range | No short (OL without the XIAO) | ≈ 0 Ω |

The 5V and PS1 output readings move for a few seconds while C1 and C2 charge
from the meter, up or down. What matters is where they settle. The PS1 output
does not read open: the module's own regulation circuit is across it.

## 4. First time on mains (without the XIAO, without USB)

- Recommended: feed it through an incandescent lamp of 25 to 60 W in series
  (a "lamp limiter"), from a socket behind an RCD. If something is shorted, the lamp
  lights up fully and limits the current; on a good board it stays dark or
  barely glows.
- Clip the meter leads on before plugging in. Do not touch the board while it
  is plugged in.
- After unplugging, wait a minute before touching it: the module stays charged.

| Check | Meter mode | Expected | Fault |
| --- | --- | --- | --- |
| PS1 +Vo to −Vo | V DC, 20 V range | ≈ 5.0 V | 0 V, or above 5.5 V |
| D1 cathode to PS1 −Vo | V DC, 20 V range | 4.7 to 5.0 V. With no XIAO almost no current flows through D1, so it drops almost nothing; with the XIAO running it reads about 4.7 V | 0 V |

## 5. With the XIAO soldered

1. Clean the flux off the XIAO pads with isopropyl alcohol (90 % or more) and
   a toothbrush, without soaking the board. Let it dry for at least 30 minutes
   before any mains check.
2. Look again with a magnifier, above all between the U1 5V, GND and 3V3 pads,
   which sit next to each other.
3. Repeat section 2 completely, adding J1 L and J1 N to the USB-C shell. Same
   result as before.
4. The short checks below, without mains and without USB. The readings are
   lower than in section 3 now that the meter also sees the XIAO.
5. On mains, without USB: the XIAO starts, joins Wi-Fi and shows in the
   console's Devices list within seconds (if it was provisioned over USB
   first). The firmware does not use the XIAO's LEDs.

**Never connect USB while the board is on mains.**

| Check | Meter mode | Expected | Fault |
| --- | --- | --- | --- |
| U1 5V to U1 GND | Continuity, then Ω | No beep; settles at kΩ or more | Beeps |
| U1 3V3 to U1 GND | Continuity, then Ω | No beep; above about 100 Ω | Beeps |
| U1 5V to U1 3V3 | Continuity | No beep | Beeps: solder bridge |
| Each input terminal to its own U1 pad | Ω, 2 kΩ range | About 1 kΩ | Open |
| Each input terminal to J3 GND, and to its neighbour | Continuity | No beep | Beeps: solder bridge |
| On mains: D1 cathode to U1 GND | V DC, 20 V range | 4.6 to 4.9 V | 0 V |
| On mains: U1 3V3 to U1 GND | V DC, 20 V range | About 3.3 V | 0 V |

## Reference: first board

Board 1 of the first JLCPCB batch, 2026-10, measured with a Fluke 83.

| Check | Reading |
| --- | --- |
| J1 L to J1 N | Open, no beep |
| Isolation, every point in section 2 | Rises for about a minute, ends near 24 MΩ (same on a second board) |
| PS1 +Vo to −Vo | 1.6 kΩ |
| U1 5V to U1 GND, no XIAO | 2.5 MΩ (starts high, settles); probes swapped 38 kΩ |
| Input terminal to its own U1 pad | 1 kΩ |
| PS1 +Vo on mains, no XIAO | 5.0 V |
| D1 cathode on mains, no XIAO | 5.0 V |
| Section 5, XIAO soldered | Every check as expected; runs on mains and shows in the console |

If another board reads very differently from these, compare it with a third
one before deciding which is wrong.

## Mistakes that look like faults

- **≈ 21 kΩ from an input terminal to a U1 pad**: the probe is on another
  channel's pad (1 kΩ + 10 kΩ + 10 kΩ through 3V3). Move it to the pad of the
  same channel. The reading itself shows that both channels' resistors and the
  3V3 track are fine.
- **"1" at the left of the display**: on a manual-range meter this is over
  range, not open. 22 kΩ on the 20 kΩ range, or 1 kΩ on the 200 Ω range, both
  show it.
- **Open across a resistor**: a probe on the black body of an 0603 part does
  not touch metal. Press on the solder at each end. Then check the range.
- **A meter that reads open across a known 1 kΩ resistor is not usable for
  section 2.** It happened with the first board: one meter read every 1 kΩ
  input resistor as open, a second meter read them correctly.
