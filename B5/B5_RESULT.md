# B5 result — run 1, 2026-09-09. PASS, with one row untested.

Log: `_spikes/B5_run1.txt` (62 lines, copied from `%TEMP%\onionskin_B5.txt`).

| # | Claim | Verdict | Evidence |
|---|---|---|---|
| 1 | Three commands appear under Animation | **pass** | operator |
| 1 | They fire and change the state | **pass** | 55 `CMD` lines |
| 1 | Toggle's checkmark agrees with the panel | **pass** | operator |
| 1 | Clamp holds at max | **pass** | line 53, `prev=10 (at max, unchanged)` |
| 1 | Clamp holds at min | **untested** | `prev=0` reached (lines 10, 63) but never pressed *past* it — no `(at min, unchanged)` line exists |
| 2 | All three work with **no layer selected** | **UNTESTED** | every one of the 62 lines reads `selection=one layer, index 4` |
| 3 | All three appear in Edit ▸ Keyboard Shortcuts | **pass** | operator |
| 3 | All three are bindable | **pass** | operator |
| 3 | **The bound keys change the panel** | **pass** | operator, and corroborated below |
| 4 | Dead button logs nothing | **pass** | only 3 `CLICK LIVE` lines, no stray entries |

## The keystroke evidence the log gave up by accident

The runbook said the log cannot distinguish a menu click from a keystroke,
because both arrive at the same hook. That is true per line — but not in
aggregate. Lines 32–36 are five `MORE` commands inside one second, and lines
43–48 six more in the same second. **A human cannot open the Animation menu and
pick an item six times per second.** Those bursts can only be a held or rapidly
struck key.

So step 3 has two independent confirmations: the operator watching the panel, and
a timing signature in the log that a menu-driven run could not have produced. That
is a stronger result than the runbook was designed to deliver.

## The row that is not evidenced

**Every line in the run reads `selection=one layer, index 4`.** Layer 4 stayed
selected from the first click to the last, so the "works with nothing selected"
step was never performed.

This is not a failure — it is an absence. And it very probably passes: the
`UpdateMenuHook` enables all four commands unconditionally, precisely so AE will
deliver their shortcuts, and B2 already established that AE routes input to a
panel regardless of selection.

But "very probably passes" is what this project does not accept for its central
claim. **Reach regardless of selection is the whole premise of Option B**, and it
is the one row a 20-second retest would settle:

> Click empty timeline space so nothing is selected. Press the three bound keys
> once each. Confirm three new lines reading `selection=none-or-multiple`.

Kept open rather than inferred, per the standing rule that checks are
measurements and not assertions.

## Also worth one retest press

Press **Fewer** once while `prev` already reads 0. Expect
`prev=0 (at min, unchanged)`. The max clamp proved itself at line 53; the min
clamp has never been asked to hold.

## Verdict

B5 works. The command path reaches us, the state changes, the keys drive it, and
the broken control stayed silent. It is a shippable fallback and — for an
animator with a hand on the pen — probably the primary interface rather than the
fallback.

Two lines of evidence outstanding, both cheap.
