# B2 runbook — does an AEGP panel exist, dock, persist, and reach us?

Half the Option B gate. The other half is B3 (writing params). If B2 fails, the
panel is dead and the product falls back to menu commands + Effect Controls.

**Built:** `C:\_build_out\AEGP\osB2.aex` — `dumpbin /EXPORTS` shows a bare
`EntryPointFunc`. ✅

**Log:** `%TEMP%\onionskin_B2.txt`, **appended, never truncated**. Do not delete
it between the two AE launches — step 4 is read off the fact that it survives.

---

## Deploy (admin, once)

AE must be closed. From an **elevated** PowerShell:

```
Copy-Item "C:\_build_out\AEGP\osB2.aex" "C:\Program Files\Adobe\Adobe After Effects 2026\Support Files\Plug-ins\AGS\osB2.aex" -Force
```

---

## The run

Do these in order. Each numbered step maps to a claim in the gate table.

### 1 — It exists and it docks

1. Launch AE. Open any project with a comp containing at least two layers.
2. **Window ▸ Onion Skin B2 (Panel Spike)**.
3. Expect: a panel with an orange rectangle, two buttons, and a click counter.
4. **Drag its tab next to Effect Controls and dock it there.** Leave it docked.

Record: did it appear? Did it dock, or did it only float?

### 2 — The live button works

Click **Live button** three times. The on-panel counter should advance each time.

### 3 — The broken control

Click **Dead button** five times. The counter must **not** move, and the log must
gain **no** lines. This is the check that makes step 2 mean anything — a handler
firing on every click looks identical to one that works.

### 4 — Reach: does it work regardless of selection?

Without touching the panel's dock:

- **a.** Select layer 1 in the timeline → click Live once.
- **b.** Select a *different* layer → click Live once.
- **c.** Click empty space in the timeline so **nothing** is selected → click Live once.
- **d.** Select *two* layers → click Live once.

All four must produce a log line. The line records what the selection was.

> Note on (d): `AEGP_GetActiveLayer` returns non-NULL only for a *single*
> selected layer, so a multi-selection logs as `none-or-multiple`. That is the
> API's limit, not a failure — B2 only asks whether the click *reached us*.

### 5 — Persistence

1. Switch workspace (e.g. Window ▸ Workspace ▸ Animation), then switch back.
   Panel should still be docked where you left it.
2. **Quit AE completely. Relaunch it.**
3. Expect the panel still docked beside Effect Controls, without reopening it
   from the Window menu.
4. Click Live once more.

---

## Reading the log

```
type %TEMP%\onionskin_B2.txt
```

Expected shape:

```
[... t=   12345]  STARTUP AE 25.6, plugin id N
[... t=   12350]  READY   panel registered as "OnionSkinB2Panel"
[... t=   45000]  MENU    toggle requested
[... t=   45010]  PANEL   created. hwnd=...
[... t=   60000]  CLICK   LIVE  #1  selection=one layer, index 1
...
[... t=     900]  STARTUP AE 25.6, plugin id N        <-- t resets: this is the relaunch
[... t=    1500]  PANEL   created. hwnd=...           <-- created WITHOUT a MENU line = restored from workspace
```

The `t=` reset is how you tell one AE session from the next. The line that
decides step 5 is a **`PANEL created` with no preceding `MENU toggle requested`
in that session** — that means AE restored it from the saved workspace rather
than you reopening it.

## Verdict

| # | Claim | Pass |
|---|---|---|
| 1 | Panel appears under Window and docks | |
| 2 | Live button reaches our code | |
| 3 | Dead button logs nothing | |
| 4 | Live works with a different layer selected | |
| 4 | Live works with **no** layer selected | |
| 5 | Panel survives a workspace switch | |
| 5 | Panel survives an AE restart (`PANEL created` with no `MENU`) | |

**Any "no" on rows 2, 3, or 4 fails the gate.** Row 5 failing is a defect to fix,
not a gate failure — a panel you must reopen each launch is annoying, not
disqualifying.

Paste the log back and I'll write up the result.
