# B5 runbook — can a keystroke drive onion-skin state?

Not part of the gate. Runs before B4 because it is cheap, needs no GUI code, and
covers the highest-frequency friction on its own — toggling while drawing.

**Built:** `C:\_build_out\AEGP\osB5.aex` — bare `EntryPointFunc`. ✅
**Log:** `%TEMP%\onionskin_B5.txt`, appended.

Four commands: one panel toggle (Window menu), three actions (**Animation** menu).
The B2 panel is carried over so the state is *visible*, not just logged.

---

## Deploy (admin, AE closed)

```
Copy-Item "C:\_build_out\AEGP\osB5.aex" "C:\Program Files\Adobe\Adobe After Effects 2026\Support Files\Plug-ins\AGS\osB5.aex" -Force
```

> You can leave `osB2.aex` installed — B5 uses a different panel match name on
> purpose, so they will not fight over a workspace slot.

---

## 1 — The commands exist and fire from the menu

1. Launch AE, open a comp.
2. **Window ▸ Onion Skin B5 (Panel Spike)** → dock it where you can see it.
3. Open the **Animation** menu. Expect three items at the bottom:
   - Onion Skin: Toggle
   - Onion Skin: More Previous Frames
   - Onion Skin: Fewer Previous Frames
4. Click **Toggle**. The panel's lamp should go grey → green, and the text
   `Onion Skin: OFF` → `ON`.
5. Reopen the Animation menu. **Toggle should now show a checkmark.**
6. Click **More Previous Frames** three times → panel reads `prev frames: 4`.
7. Click **Fewer** five times → it should stop at `0`, not go negative.

Step 5 matters: the checkmark and the panel are two independent readouts of the
same bool. If they ever disagree, the state is not single-sourced — which is the
exact bug the "panel holds no state" rule exists to prevent.

## 2 — They work with nothing selected

Click empty space in the timeline so no layer is selected. Run Toggle, More and
Fewer once each. All three must work and the log must say `none-or-multiple`.

## 3 — Shortcuts (the real question)

1. **Edit ▸ Keyboard Shortcuts…**
2. Search for `Onion Skin`.
3. Expect all three to be listed and bindable. Assign:
   - Toggle → `Ctrl+Alt+O`
   - More → `Ctrl+Alt+]`
   - Fewer → `Ctrl+Alt+[`
   *(If any of those collide with an existing binding, pick anything free and
   note what you used.)*
4. Close the dialog.
5. **With the mouse over the comp viewer and no layer selected**, press each key.

The panel must change on every press. This is step 5 of the spike and the only
one that actually saves anybody a trip — a command that works from the menu but
not from its key has done nothing for an animator.

## 4 — The broken control

Click the panel's **Dead button** five times. Nothing may change and the log must
gain no lines. Same guard as B2, kept because it costs one line.

---

## Reading the log

```
type %TEMP%\onionskin_B5.txt
```

```
[...]  STARTUP AE 25.6, plugin id N
[...]  READY   4 commands registered (panel, toggle, more, fewer)
[...]  CMD     TOGGLE  -> ON    selection=none-or-multiple
[...]  CMD     MORE    -> prev=2   selection=one layer, index 1
[...]  CMD     FEWER   -> prev=0 (at min, unchanged)   selection=none-or-multiple
```

The `(at min, unchanged)` / `(at max, unchanged)` lines are deliberate: a command
that silently does nothing at its limit looks exactly like a broken one, so the
clamp says when it bites.

**The log cannot tell a menu click from a keystroke** — both arrive at the same
hook. So step 3 is judged by your eyes on the panel, not by the log. Note in your
report which lines came from keys.

## Verdict

| # | Claim | Pass |
|---|---|---|
| 1 | Three commands appear under Animation | |
| 1 | They fire and change the state | |
| 1 | Toggle's checkmark agrees with the panel | |
| 1 | Clamps hold at 0 and 10 | |
| 2 | All three work with no layer selected | |
| 3 | All three appear in Edit ▸ Keyboard Shortcuts | |
| 3 | All three are bindable | |
| 3 | **The bound keys change the panel** | |
| 4 | Dead button logs nothing | |

Row 3's last line is the one that decides whether B5 is a product feature or just
three more menu items.
