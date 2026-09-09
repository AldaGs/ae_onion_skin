# B2 result — run 1, 2026-09-09. PASS.

Log: `_spikes/B2_run1.txt` (copied from `%TEMP%\onionskin_B2.txt`).

| # | Claim | Verdict | Evidence |
|---|---|---|---|
| 1 | Panel appears under Window and docks | **pass** | user report; `MENU`→`PANEL created` at lines 3–4 |
| 2 | Live button reaches our code | **pass** | 14 `CLICK LIVE` lines |
| 3 | Dead button logs nothing | **see note** | no `DEAD` lines — pending confirmation the button was clicked |
| 4 | Live works with a different layer selected | **pass** | indices 0, 1, 2, 4, 5 (lines 11–15) |
| 4 | Live works with **no** layer selected | **pass** | `none-or-multiple` at lines 5–10, 16–17, 22 |
| 5 | Survives a workspace switch | **pass** | line 18: `PANEL created` with no preceding `MENU` |
| 5 | Survives an AE restart | **pass** | line 19 `STARTUP`, line 21 `PANEL created`, no `MENU` between |

**The gate row is 4.** A click reaches the panel regardless of what is selected,
including nothing. That is Option B's premise — reach, not pixels — and it holds.

## A defect in this runbook, recorded rather than quietly fixed

The runbook said to read session boundaries off a `t=` reset. That was wrong:
`GetTickCount` counts from system boot, not process start, so it does not reset
on relaunch — line 19's `t=10101140` continues straight on from line 18's
`t=10080062` across an AE restart.

The verdict is unaffected, because the `STARTUP` line is itself the session
marker and that is what rows 5 were actually read from. But the instruction told
the operator to look for something that cannot happen, and an operator who
trusted it would have scored row 5 as a fail. **A check that can only fail is as
useless as one that can only pass** — worth keeping next to the standing rule
about broken controls.

If a within-session clock is ever wanted, subtract the first tick seen at
startup, or use the wall clock already on every line.

## Decision recorded: buttons inside the comp viewer

Asked during this run: could the controls live *inside* the comp viewer rather
than in a docked panel beside it?

**Not as always-on controls.** Two routes, both refused:

1. **Custom Comp UI** (`PF_Event_DO_CLICK`/`DRAW`) does draw and click inside the
   viewer — this is how Corner Pin's handles work — but AE sends those events
   only while the effect is selected in Effect Controls. In-viewer controls that
   exist only when the layer is selected do not remove the friction they exist to
   remove.

2. **A topmost overlay window over the viewer** (the pieFX mechanism). Worth being
   precise about why this is refused, because it is *not* the reason Option A
   died: a button bar anchored to a panel corner needs the panel's screen rect,
   not the comp's pan, so A's geometric ceiling does not apply here. It is
   refused because it still rests on **A2, which was never solved — nothing in AE
   can name its panels.** A3c worked around that by having the user hover over
   the viewer for five seconds to point at it. That is not a way to reach an
   on/off button.

**Resolution:** the docked panel stands. The better answer for in-viewer *reach*
is B5 (registered commands with user-assignable shortcuts) — an animator with a
hand on the pen wants a key, not a button to travel to. Phase 3's Custom Comp UI
gizmos remain worthwhile for handles that are genuinely spatial, and remain
explicitly not a substitute for the panel.

## Next

B5, then B3. B3 is the remaining gate: does a panel write re-render AE without
stealing the layer selection?
