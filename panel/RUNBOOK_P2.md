# Phase 2 runbook — the managed layer and the panel

Onion skinning becomes a **toggle**. The AEGP creates the adjustment layer, marks
it adjustment + guide, applies the effect, and removes it again. The user never
maintains a layer.

**Built:**

| File | Exports | Version |
|---|---|---|
| `C:\AE_SDK\_build_out\AEGP\onionSkin.aex` | `EffectMain` | 1.5.0 build 6 = 688134 |
| `C:\AE_SDK\_build_out\AEGP\onionSkinPanel.aex` | `EntryPointFunc` | — |

The effect was rebuilt because the disk-ID enum moved into
`shared/onionSkinIDs.h`, included by both binaries. They address params by
**index**, so an ID added to one and not the other would write Strength into
Falloff and never error. One file, no copies.

**Log:** `%TEMP%\onionskin_panel.txt`

---

## Deploy (admin, AE closed)

```
Copy-Item "C:\AE_SDK\_build_out\AEGP\onionSkin.aex"      "C:\Program Files\Adobe\Adobe After Effects 2026\Support Files\Plug-ins\AGS\onionSkin.aex" -Force
Copy-Item "C:\AE_SDK\_build_out\AEGP\onionSkinPanel.aex" "C:\Program Files\Adobe\Adobe After Effects 2026\Support Files\Plug-ins\AGS\onionSkinPanel.aex" -Force
```

The B2/B3/B5 spike binaries can be deleted now — they answered their questions.

---

## 1 — The toggle

1. Open a comp with moving artwork on a **transparent** background.
2. **Window ▸ Onion Skin** → the panel. Dock it.
3. Click **Onion Skin On / Off**.

Expect: a layer named `ONION SKIN` appears at the top of the comp, marked as an
adjustment layer and a guide layer, carrying the effect. Ghosts appear. The
panel's lamp turns green.

4. Click it again. The layer is **gone**, ghosts gone, lamp grey.
5. **Ctrl+Z once** should undo the whole create or remove in one step.

## 2 — The panel is a readout, not a memory

This is the rule the design rests on, so it gets tested directly.

1. Turn onion skinning on.
2. Select the `ONION SKIN` layer and open **Effect Controls**.
3. Change **Strength** there, by hand, to something obvious like 90.
4. **Watch the panel.** Its Strength readout must follow within a tick, without
   you touching the panel.
5. Now use the panel's Strength **+** / **−**. Effect Controls must follow.

If the two ever show different numbers, the panel is holding state and that is a
defect — the streams are the only source of truth.

## 3 — Reach

With the panel docked and **no layer selected** (click empty timeline space):

- Previous **+** / **−**
- Next **+** / **−**
- Strength **+** / **−**

All must work, and **the selection must not change**. This is what Option B
exists for.

## 4 — Shortcuts

**Edit ▸ Keyboard Shortcuts…**, search `Onion Skin`. Bind:

- `Onion Skin: Toggle` → something free, e.g. `Ctrl+Alt+O`
- `Onion Skin: More Previous Frames`
- `Onion Skin: Fewer Previous Frames`

Then, mouse over the comp viewer, nothing selected: press them. The comp and the
panel must both respond.

## 5 — The opaque-background warning

The thing that has confused every run so far, now caught before it confuses.

1. Add an **opaque full-frame solid** at the bottom of the comp.
2. Turn onion skinning on.

Expect: **an amber warning in the panel** saying an opaque full-frame layer sits
below the onion skin layer and the ghosts will be invisible.

3. Set that solid to a **guide layer** (Layer ▸ Guide Layer), or move it above
   the onion skin layer.
4. The warning must clear on its own within a tick.

The check is deliberately conservative — it only fires on a *visible,
non-adjustment, non-guide, full-frame, opacity-100* layer below ours. A false
alarm would train you to ignore it, so if you can make it cry wolf, that is a
defect worth reporting.

## 6 — Multiple instances

1. Turn onion skinning on.
2. Also apply Onion Skin by hand to a drawing layer.
3. The panel should say **"Driving 2 effect instances."**
4. Use Strength +. **Both** should change, and **Ctrl+Z once** should undo both.

## 7 — The keyframe refusal

1. Keyframe **Strength** on the managed layer.
2. Press the panel's Strength **+**.

Expect an amber note: *"Strength" is keyframed — change it in the timeline.*
`AEGP_SetStreamValue` is illegal on a keyframed stream (B3), so it refuses rather
than corrupting anything.

## 8 — The effect's own button

Select the `ONION SKIN` layer, click **Open Onion Skin Panel** in Effect Controls.
The real panel should open now — no more empty dialog.

---

## Verdict

| # | Claim | Pass |
|---|---|---|
| 1 | Toggle creates the layer, ghosts appear | |
| 1 | Toggle again removes it | |
| 1 | One Ctrl+Z undoes create / remove | |
| 2 | Effect Controls → panel follows | |
| 2 | Panel → Effect Controls follows | |
| 3 | All controls work with nothing selected | |
| 3 | Selection never changes | |
| 4 | Shortcuts bindable and working | |
| 5 | Opaque-background warning appears | |
| 5 | It clears when fixed | |
| 5 | It does NOT cry wolf | |
| 6 | Broadcasts to 2 instances, one undo | |
| 7 | Keyframed stream refused with a message | |
| 8 | Effect Controls button opens the panel | |

Rows 2 and 3 are the ones that decide whether Phase 2 delivered what Option B
promised. Row 5 is the one that turns this product's worst failure mode into a
sentence.

**Known rough edges, not defects:** the panel's buttons are `+`/`−` steppers
rather than sliders — B4 (what a real slider costs) was never run, and steppers
were enough to prove the mechanism. Colours and Frame Step are not on the panel
yet; they live in Effect Controls.
