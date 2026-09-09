# Phase 1 runbook — the real effect

First build of the actual product. Ports the model from
`python-proto/onion_skin/os_step1_composite.py`, whose seven offline checks pass
with two controls correctly failing.

**Built:** `C:\_build_out\AEGP\onionSkin.aex` — exports `EffectMain`, version
524289 (= 1.0.0 build 1, matching `PF_VERSION` in `GlobalSetup`).

---

## Deploy (admin, AE closed)

```
Copy-Item "C:\_build_out\AEGP\onionSkin.aex" "C:\Program Files\Adobe\Adobe After Effects 2026\Support Files\Plug-ins\AGS\onionSkin.aex" -Force
```

The B3 spike binaries can stay; different match names, no collision.

---

## 1 — Pass-through is exact

Before anything else, prove the effect does nothing when asked to do nothing.

1. Comp with a layer that has **moving** artwork (a shape layer with a position
   keyframe is enough — it must differ frame to frame).
2. Apply **Effect ▸ ags_utilities ▸ Onion Skin** directly to that layer.
3. **Uncheck Enable.** The frame must look *identical* to the un-effected layer.
4. Re-check Enable, set **Strength = 0**. Identical again.
5. Set Strength back to 55, set **Previous = 0 and Next = 0**. Identical again.

All three are one early exit in the code. If any of them alters the image, stop —
the invariant the proto established is broken and nothing downstream is
trustworthy.

## 2 — It looks like onion skinning

Enable, Previous 3, Next 3, Strength 55, Falloff 60, Tint 100.

Expect: red ghosts trailing, blue ghosts leading, current frame untouched and
full strength, nearer ghosts stronger than farther ones.

Scrub the timeline. The ghosts must follow.

Compare against `python-proto/onion_skin/out_os1_both3.png` — same model, so it
should read the same way.

## 3 — The controls

- **Frame Step 2** — skins every other frame. The usual want on 2s.
- **Falloff 100** — every skin equally strong (flat look).
- **Tint Amount 0** — ghosts keep the drawing's own colours, only faded.
- **Tint Amount 50** — pulled halfway toward the tint hue.
- **Past / Future Colour** — should change the two directions independently.
- **Previous 8 / Next 0** — trailing only, no leading ghosts.

## 4 — The ends of the timeline

Go to **frame 0**. There are no previous frames. Expect no red ghosts and **no
error** — AE hands back an empty layer off the ends and it contributes nothing.
Same at the last frame for blue.

## 5 — The opaque-background case (why Source Layers exist)

This is the finding that reshaped Phase 1. Reproduce it, then fix it.

1. Add an **opaque solid** at the bottom of the comp.
2. Add an **adjustment layer** above everything, apply Onion Skin to it, leave
   all three Source Layers as **None**.
3. **Expect NO ghosts at all.** Not faint — none. The adjustment layer receives
   the composite below, which is fully opaque, so every skin covers the last and
   the current frame covers them all. The proto measured this as exactly 0.000
   difference.
4. Now set **Source Layer 1** to your drawing layer.
5. **Ghosts appear**, over the solid background.

That is the whole reason the layer params exist. Row 5.3 failing to be *empty*
would be more surprising than it working.

## 6 — Multiple sources

Set Source Layer 1 and 2 to two different moving layers. Both should be ghosted,
composited in order.

## 7 — The launcher

Click **Open Onion Skin Panel**. Nothing will happen yet and that is correct —
the Phase 2 panel does not exist. It must **not error**; the code treats a
missing panel as non-fatal. Check no error dialog appears.

## 8 — 16-bit

Set the project to **16 bpc** (File ▸ Project Settings ▸ Depth). Repeat step 2.
The result should look the same, not banded or wrong-coloured. There is a
separate 16-bit code path and this is the only thing that exercises it.

---

## Verdict

| # | Claim | Pass |
|---|---|---|
| 1 | Enable off = identical image | |
| 1 | Strength 0 = identical image | |
| 1 | Prev 0 + Next 0 = identical image | |
| 2 | Reads as onion skinning, past/future distinguishable | |
| 2 | Nearer ghosts stronger than farther | |
| 3 | Frame Step, Falloff, Tint, Colours all behave | |
| 4 | No error and no ghosts at the timeline ends | |
| 5 | Adjustment layer + opaque BG + no source = **no ghosts** | |
| 5 | Setting Source Layer 1 makes ghosts appear | |
| 6 | Two sources both ghosted | |
| 7 | Open Panel button does not error | |
| 8 | 16 bpc looks correct | |

Rows 1 and 5 are the ones I most want to see. Row 1 is the invariant; row 5 is
the finding that changed the design.

Known not-yet-done, for context rather than testing: this is a legacy `PF_Cmd_RENDER`
effect, not SmartFX, so it renders at full resolution regardless of the viewer's
resolution setting. If it feels slow at Full on a big comp, that is expected and
is a Phase 2 item, not a defect.
