# A1c-1 runbook (run 2) — finishing the coverage

Run 1 confirmed the model: six usable captures, residuals 0.000–0.373 px, broken
control caught every time. What it did **not** get is coverage — nine captures
were impossible as specified, and the PAR states never exercised PAR.

This run fills those gaps. **14 captures, ~10 minutes.** Three fixes are already
in, so it should go straight through.

## What changed since run 1

- `MakeCalib` now asks for **comp size** as well as PAR, and lays the markers out
  proportionally. The 100% states use a small comp so the markers fit the panel.
- `os_A1c1.exe` **refuses to capture** unless the zoom log has exactly one reading
  per capture. Mispairing is now impossible rather than discouraged.
- The solver **measures** PAR from `sx/sy` instead of trusting `calib.json`.

## Step 0 — clear run 1

```bash
cd "C:\AE_SDK\ae25.6_61.64bit.AfterEffectsSDK\Examples\Template\OnionSkin\_spikes" && rm -f A1c1_cap_*.bmp A1c1_cap_*.txt A1c1_zoom_log.txt
```

## The loop, for every state

1. `A1c1_Zoom.jsx`  →  logs the zoom, tells you the reading number
2. `os_A1c1.exe`  →  hover over the viewer image area through the countdown

```bash
cd "C:\AE_SDK\ae25.6_61.64bit.AfterEffectsSDK\Examples\Template\OnionSkin\_spikes" && os_A1c1.exe
```

If you lose your place, the tool tells you: it names the capture number it is
about to write and refuses if the zoom log disagrees.

---

## SET A — `MakeCalib` with **1920x1080**, PAR **1.0**

Panel **A** = viewer as-is. Panel **B** = noticeably different size.

| # | zoom | pan | panel |
|---|---|---|---|
| 1 | fit | centred | A |
| 2 | fit | panned hard | A |
| 3 | 50% | centred | A |
| 4 | 50% | panned hard | A |
| 5 | fit | centred | B |
| 6 | fit | panned hard | B |
| 7 | 50% | centred | B |
| 8 | 50% | panned hard | B |

## SET B — `MakeCalib` with **640x360**, PAR **1.0**

The small comp is what makes 100% measurable at all — at 1920x1080 the markers
were 760 px apart and could never fit a 449 px panel.

| # | zoom | pan | panel |
|---|---|---|---|
| 9 | 100% | centred | A |
| 10 | 100% | panned hard | A |
| 11 | 100% | centred | B |
| 12 | 100% | panned hard | B |

## SET C — `MakeCalib` with **1920x1080**, PAR **2.0**

**Before capturing, turn ON Pixel Aspect Ratio Correction** — the button along
the bottom edge of the Composition panel. Run 1's PAR states measured `sx/sy` of
1.0006 on a genuine PAR 2 comp because this defaults to OFF, so they tested
nothing.

| # | zoom | pan | panel |
|---|---|---|---|
| 13 | fit | centred | A |
| 14 | 50% | panned hard | A |

If you cannot find the toggle, capture these two anyway and say so — "PAR
correction unavailable" is a legitimate finding, and it would mean AE only ever
draws square pixels in the viewer, which would simplify the plug-in.

---

## Then

Tell me, and I run:

```bash
cd "C:\AE_SDK\ae25.6_61.64bit.AfterEffectsSDK\Examples\Template\OnionSkin\_spikes" && python A1c1_solve.py
```

## Reading the outcome

- **All 14 inside 1 px, controls caught, zoom agreeing** → A1 passes, on to A2.
- **Set B fails while A passes** → something about scale 1.0 specifically. Real,
  and worth knowing.
- **Set C shows `sx/sy ≈ 2`** → PAR correction is a readable-by-inference state
  and the plug-in must account for it.
- **Errors growing with pan distance** → the one to watch. It would mean the
  translation is not a constant offset, and the model is incomplete.

A1 passing is still only a **static** result. A3 — staying glued during a scrub
or pan while AE's modal loop starves our repaint — remains the gate I would bet
against.
