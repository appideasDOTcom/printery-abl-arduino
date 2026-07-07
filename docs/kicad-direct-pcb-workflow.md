# KiCad 10 — Direct PCB Editor Workflow (Fritzing-style)

No schematic. You place footprints and draw traces directly in the PCB Editor,
the same way you drag parts and pull traces in Fritzing's PCB view. This is a
real, supported KiCad workflow — just not the one KiCad's own docs push you
toward by default.

**The one real tradeoff:** no schematic means no netlist, which means no
ratsnest telling you "this still needs a connection." DRC catches physical
clearance problems, not "you forgot to wire this." You check connections
visually, the same way you already do in Fritzing when skipping its breadboard
view. Not a new risk — the same one, carried over.

Your parts, from the screenshot:

| Label | Part | Qty | Function |
|---|---|---|---|
| A | 2-pin header | 1 | Thermistor (routes under HX711) |
| B | 2-pin header | 1 | Hotend/heater core |
| C | 2-pin screw terminal | 2 | Strain gauge input |
| D | 2-pin screw terminal | 2 | Part-cooling fan + throat fan |
| E | 2-pin screw terminal | 2 | HX711 → external MCU + power |
| F | 2-pin screw terminal | 4 | Leads to main board |
| G | HX711 module | 1 | (custom footprint, already built) |

Mounting holes and final board size are deferred — you said so explicitly, so
this covers placement and routing only. Add the outline and holes last, once
everything's placed and you know how much room you actually need.

---

## 0. Orientation

- **Skip the Schematic Editor entirely.** Launch the **PCB Editor** from the
  Project Manager and do everything there.
- The page border/title block you see on a new board is not a real object —
  it's the drawing sheet template, not milled, not exported unless you ask
  for it. Ignore it.
- You'll route on `B.Cu` only (single-sided milling, same as always), viewing
  the board top-down the entire time — same as Fritzing's "View from Above."
  The mirror for milling happens at export time, not while you work.

---

## Step 1: Units and workspace

1. Open the **PCB Editor**.
2. Left-hand toolbar units toggle → click until it reads **mm**.
3. Top toolbar **Grid** dropdown → **1mm** or **2.54mm** (2.54mm snaps nicely
   to header/terminal pin spacing while you place things).

Don't draw the board outline yet — place parts first, size the board around
them once you see how much space they actually need. This mirrors how you
already work in Fritzing: components go down before the outline gets locked in.

---

## Step 2: Place every footprint

Press **A** to add a footprint. Do this once per part below. All of these are
footprints you already have or that we've already sourced:

- **A, B (headers):** search `PinHeader_1x02_P2.54mm_Vertical`
- **C, D, E, F (screw terminals):** search `KF128` or `TerminalBlock` filtered
  to `P2.54mm` — verify pad/drill size against your actual part once placed.
  Since all of C/D/E/F are the same physical part, you're placing the same
  footprint **10 times total** (2+2+2+4), just labeling each instance
  differently as you go.
- **G (HX711):** use the custom footprint file I built —
  `HX711_Module_1x6_1x4_Staggered_P2.54mm` — assuming you've already added
  it as a footprint library (Preferences > Manage Footprint Libraries > add
  the `HX711_Module.pretty` folder).

As you place each one, double-click it and rename its reference designator in
the **Reference** field to match your labeling (e.g. `A_Thermistor`,
`C1_StrainGauge`, `C2_StrainGauge`, `F1_MainBoard`...`F4_MainBoard`) — this
keeps 10 nearly-identical screw terminals from becoming an unlabeled mess on
screen, same benefit as labeling wires in Fritzing.

---

## Step 3: Rough layout, drag by eye

Drag each footprint into roughly the position you sketched in your screenshot.
Exact coordinates don't matter here — this is the whole point of skipping the
schematic-first precision. A layout matching your image:

- **A** (thermistor header) — upper-left, near where it routes under G
- **C** (strain gauge terminals ×2) — top-center, above G
- **B** (hotend header) — upper-right of center
- **G** (HX711) — center, the anchor point everything routes around
- **D** (fan terminals ×2) — right side, mid-height
- **F** (main board terminals ×4) — far right, stacked vertically
- **E** (HX711-to-MCU terminals ×2) — lower-left

Move things until trace paths look like they'll be short and won't cross each
other excessively — same "arrange around your most complex part" advice from
your own Instructable (you did this with the ESP8266 as the anchor; here it's
the HX711).

---

## Step 4: Set your trace width

Before routing, decide the width. Your normal process uses **48 mil (~1.2mm)**;
your original practice board used 3mm. Pick one for this board:

- **File > Board Setup > Design Rules > Net Classes** — set `Default` class
  **Track Width** to whichever value you're using (`1.2mm` or `3.0mm` or
  otherwise).

If different traces need different widths (e.g. thicker for anything carrying
real current, like the heater circuit vs. signal lines), you can define
multiple net classes here, but for a first pass, one consistent width is
simplest and matches how you've been working.

---

## Step 5: Route every connection directly

1. **Appearance panel > Layers tab** → click **B.Cu** to make it active.
2. Press **X** (Route Tracks), click a pad, drag to the pad you're connecting
   it to, click to finish. This is functionally identical to dragging a trace
   between two orange/red leads in Fritzing's PCB view — click, drag, done.
3. Repeat for every connection on your list. Since there's no ratsnest to
   follow, work from your own labeled part list (or the screenshot) as your
   checklist, and check off each connection as you draw it.

A few routing notes specific to your part list:
- **A (thermistor) routes under G (HX711)** per your screenshot — this just
  means the trace path passes underneath where the HX711 footprint sits.
  Since everything's on one copper layer, "under" just means the trace
  visually passes through that area; there's no actual layer conflict to
  worry about (no crossing traces are allowed on a single layer, so route
  around any pad it would otherwise cross).
- **E and G are directly related** — E is described as "from G to external
  MCU and power," so route G's digital-side pins (1R–4R on the HX711
  footprint) to E's two 2-pin terminals.

---

## Step 6: Design Rules Check

**Inspect > Design Rules Checker**, click **Run DRC**. This checks physical
clearance (trace-to-trace, trace-to-pad spacing for your isolation mill) — it
will not tell you if you missed a connection, since there's no netlist to
compare against. That verification is on you, same as it always has been in
Fritzing without the breadboard view.

---

## Step 7: Add mounting holes and finalize the outline

Once everything's placed and routed and you can see the actual footprint of
the whole layout:

1. Switch active layer to **Edge.Cuts** (Appearance panel).
2. Draw a rectangle around everything with reasonable margin.
3. Place `MountingHole_2.7mm_M2.5` footprints (bare through-hole, no copper
   ring, per your correction) at each corner, inset a consistent distance
   from the edges — 4mm worked for the practice board; adjust based on this
   board's actual size.

---

## Step 8: Export for milling

Same as your existing FlatCAM pipeline:

1. **File > Plot**.
2. Check: **B.Cu**, **B.Mask**, **Edge.Cuts**.
3. Leave "Plot border and title block" unchecked.
4. **Plot**, then **Generate Drill Files** for the Excellon file.
5. Into FlatCAM: same Double-Sided PCB Tool mirror step, same tool settings
   you already have dialed in.

---

## Quick checklist

- [ ] Place A, B (headers)
- [ ] Place C×2, D×2, E×2, F×4 (screw terminals) — same footprint, 10 instances
- [ ] Place G (HX711, custom footprint)
- [ ] Rename each footprint's reference designator for clarity
- [ ] Rough layout matching your screenshot
- [ ] Set trace width (Board Setup > Net Classes)
- [ ] Route every connection by hand — verify against your part list, no ratsnest to check against
- [ ] DRC (clearance only — verify connections yourself)
- [ ] Draw board outline + mounting holes once layout is settled
- [ ] Export B.Cu + B.Mask + Edge.Cuts + drill file
