# B3 runbook — the remaining gate

Three questions in one binary: **B3** (does a panel write re-render AE without
stealing the selection?), **B1** (one `.aex` or two?), and the **effect-side
launcher**.

**Built:** `C:\_build_out\AEGP\osB3.aex`. Exports **both** `EffectMain` and
`EntryPointFunc`; the generated resource script carries **both** PiPLs (16000
effect, 16001 AEGP). So B1 is already half-answered at build time — the remaining
half is whether AE *loads* it that way.

**Log:** `%TEMP%\onionskin_B3.txt`, appended, millisecond stamps.

---

## Deploy (admin, AE closed)

```
Copy-Item "C:\_build_out\AEGP\osB3.aex" "C:\Program Files\Adobe\Adobe After Effects 2026\Support Files\Plug-ins\AGS\osB3.aex" -Force
```

> **One file, one folder — on purpose.** AE scans `Support Files\Plug-ins`
> recursively, so a dual-kind binary in `AGS\` should register both halves.
> Whether it actually does is part of what step 1 measures. If the effect does
> not appear in the Effect menu, try the MediaCore path instead and note that the
> two kinds want different homes — that is a real finding, not a mistake.

---

## 1 — B1: does one binary carry both halves?

1. Launch AE.
2. `type %TEMP%\onionskin_B3.txt` — expect **both** of these lines:
   - `FX      GLOBAL_SETUP - the effect half loaded`
   - `STARTUP AEGP half loaded.`
3. **Window ▸ Onion Skin B3 (Write Spike)** should exist.
4. **Effect ▸ ags_utilities ▸ Onion Skin B3** should exist.

| Result | Meaning |
|---|---|
| both lines, both menus | **B1 = one binary.** |
| only one | B1 = two binaries; split the project, no harm done |
| neither / AE won't start | remove the .aex, tell me, we split |

## 2 — Set up the comp

1. New comp, a couple of layers with visible colour.
2. Select **layer 2** (not layer 1) and apply **Onion Skin B3**.
3. In Effect Controls, drag **Brightness** — the layer should visibly brighten and
   darken. If it doesn't, stop: nothing downstream is measurable.
4. Set Brightness back to 100.

## 3 — The launcher

Click the **Open Onion Skin Panel** button in Effect Controls. The panel should
open. Click it again with the panel already open — **it must stay open, not
close.** (`AEGP_ToggleVisibility` alone would close it; the code checks
`AEGP_IsShown` first.)

## 4 — The gate

Dock the panel where you can see the comp viewer at the same time.

1. **Select layer 1** — deliberately *not* the layer carrying the effect.
2. Click **Brightness +10** five times.
3. Watch two things: the comp viewer must visibly brighten, and **the timeline
   selection must stay on layer 1.**
4. Click **Brightness -10** five times.
5. Now click empty timeline space so **nothing** is selected, and click
   **+10** twice more.

Every line logs `sel N -> M`. Any line reading `*** SELECTION CHANGED ***` fails
the gate.

## 5 — The broken control

Click **No-op write** five times. It writes the value the param already holds.

- The log must show `readback` equal to the value it already had.
- **The comp viewer must NOT visibly change.**

If a no-op write produces a visible repaint, we are watching AE's idle redraw
rather than our write, and steps 4's observations are unreadable.

Then click **Dead button** five times — no log lines at all.

## 6 — Undo

After a `+10`, press **Ctrl+Z once**. Brightness must return to its previous
value in **one** step. Repeat a few times. If one write costs two or three undos,
that is a defect worth knowing now.

## 7 — The keyframe case

1. In Effect Controls, put a **keyframe** on Brightness (click the stopwatch).
2. Click **Brightness +10** in the panel.

Expect a log line reading `REFUSED: stream has N keyframes`. This is not a bug —
`AE_GeneralPlug.h` states `AEGP_SetStreamValue` is only legal when the stream has
no keyframes. The spike detects it rather than writing into undefined behaviour.
**It is a genuine constraint on the Phase 2 panel** and better found here.

Remove the keyframe afterwards.

---

## Reading the log

```
type %TEMP%\onionskin_B3.txt
```

```
WRITE   PLUS   layer_idx=1  100.0 -> 110.0  readback=110.0  set_ms=3  sel 12 -> 12  SELECTION INTACT
WRITE   NOOP   layer_idx=1  110.0 -> 110.0  readback=110.0  set_ms=2  sel 12 -> 12  SELECTION INTACT
WRITE   PLUS   REFUSED: stream has 1 keyframes; SetStreamValue is illegal here
```

`sel -1` means none-or-multiple selected — that is expected and fine in step 4.5.
What matters is that the number is **the same either side of the arrow**.

## Verdict

| # | Claim | Pass |
|---|---|---|
| 1 | Both halves load from one binary (B1) | |
| 2 | Brightness visibly changes the render | |
| 3 | Effect-controls button opens the panel | |
| 3 | Clicking it again does not close it | |
| 4 | Viewer repaints on a panel write | |
| 4 | **Selection intact on every write** | |
| 4 | Works with a non-effect layer selected | |
| 4 | Works with nothing selected | |
| 5 | No-op write causes no visible change | |
| 5 | Dead button logs nothing | |
| 6 | One write = one undo step | |
| 7 | Keyframed stream is refused, not corrupted | |

**Rows 4 and 5 are the gate.** Row 4's selection line is the one that decides
whether the panel is usable while animating.
