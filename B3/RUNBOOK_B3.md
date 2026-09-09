# B3 runbook — the remaining gate

Three questions, two binaries: **B3** (does a panel write re-render AE without
stealing the selection?), **B1** (one `.aex` or two?), and the **effect-side
launcher**.

**B1 is answered: TWO BINARIES.** Run 1 installed a single `.aex` declaring both
PiPLs. AE loaded the effect and never called `EntryPointFunc` — the log held only
`FX GLOBAL_SETUP` and the Window menu item never appeared. One `.aex` is claimed
by one kind. The mechanism is not pinned down (`first PiPL wins` and `a file
already claimed as an effect is skipped by the AEGP scan` predict the same
observable) and does not need to be: the remedy is the same, B1 was always
informational, and the product is unchanged. Only the file count moved.

**Built:** two files, each exporting only its own entry point (verified):

| File | Exports | Half |
|---|---|---|
| `C:\_build_out\AEGP\osB3fx.aex` | `EffectMain` | the effect |
| `C:\_build_out\AEGP\osB3panel.aex` | `EntryPointFunc` | the panel |

They find each other by name, not by being in one file: the effect calls the
panel by its **match name** `OnionSkinB3Panel`, and the panel finds the effect by
its **match name** `aldai OnionSkinB3`. That is exactly how Phase 2 will work, so
the split costs nothing architecturally.

**Log:** `%TEMP%\onionskin_B3.txt` — both binaries append to the *same* file, so
the interleaving shows the two halves acting on one param. The prefix says which
wrote each line.

---

## Deploy (admin, AE closed)

**Delete the old combined binary first.** It registers the same effect match name
and two plug-ins claiming one match name is its own bug:

```
Remove-Item "C:\Program Files\Adobe\Adobe After Effects 2026\Support Files\Plug-ins\AGS\osB3.aex" -Force
Copy-Item "C:\_build_out\AEGP\osB3fx.aex"    "C:\Program Files\Adobe\Adobe After Effects 2026\Support Files\Plug-ins\AGS\osB3fx.aex" -Force
Copy-Item "C:\_build_out\AEGP\osB3panel.aex" "C:\Program Files\Adobe\Adobe After Effects 2026\Support Files\Plug-ins\AGS\osB3panel.aex" -Force
```

---

## 1 — Both halves load (now from two files)

1. Launch AE.
2. `type %TEMP%\onionskin_B3.txt` — expect **both**:
   - `FX      GLOBAL_SETUP - the effect half loaded`
   - `STARTUP AEGP half loaded.`
3. **Window ▸ Onion Skin B3 (Write Spike)** exists.
4. **Effect ▸ ags_utilities ▸ Onion Skin B3** exists.

If the Window item is *still* missing with a dedicated AEGP binary, that is a new
and much more interesting finding than B1 — stop and say so, because B2 and B5
both registered panels from this same folder without trouble.

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

> **Changed after run 1.** Run 1 wrote straight from the panel's window callback
> and AE raised *"internal verification failure … {no current context}"* then
> *"AEGP magic error"*. A Win32 wndproc is not a context AE has set up for
> plug-in calls, so project-touching AEGP calls are illegal there. The buttons now
> **queue** the write and an **idle hook** performs it, which is a context AE
> accepts. Only `osB3panel.aex` changed.
>
> **This is a Phase 2 constraint, not a spike detail** — every panel control that
> writes a param will go through the same queue.

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

### 5b — The direct-path control (the diagnosis, made falsifiable)

Click **Direct (expect err)** once. It performs the identical write from the
wndproc, the way run 1 did.

**It should raise the same "no current context" error.** That is the point: it
turns "the deferred path works" into "the deferred path works *and* the direct
one still does not", which is the difference between a fix and a coincidence.

If Direct now *succeeds*, tell me — it would mean the deferral is not what fixed
it and the real cause is still unidentified.

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
| 1 | Both halves load, from two binaries | |
| 2 | Brightness visibly changes the render | |
| 3 | Effect-controls button opens the panel | |
| 3 | Clicking it again does not close it | |
| 4 | Viewer repaints on a panel write | |
| 4 | **Selection intact on every write** | |
| 4 | Works with a non-effect layer selected | |
| 4 | Works with nothing selected | |
| 5 | No-op write causes no visible change | |
| 5 | Dead button logs nothing | |
| 5b | Direct button still raises "no current context" | |
| 6 | One write = one undo step | |
| 7 | Keyframed stream is refused, not corrupted | |

**Rows 4 and 5 are the gate.** Row 4's selection line is the one that decides
whether the panel is usable while animating.
