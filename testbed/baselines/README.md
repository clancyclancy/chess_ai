# Frozen engine baselines

Every entry here is a compiled `ChessUCI.exe` snapshot used as the fixed opponent in
A/B matches (`testbed/run_match.py`). The `.exe` files are gitignored (they're build
artifacts), so this log is the source of truth for what each one contains.

Workflow when freezing a new baseline:

```powershell
cmake --build build --target ChessUCI
build\ChessUCI.exe --selftest          # must pass before freezing
copy build\ChessUCI.exe testbed\baselines\ChessUCI-<label>-<yyyymmdd>.exe
```

then add a row below. Prefer freezing from a clean, committed working tree so the git
hash fully identifies the code.

| file | date | git | notes |
|---|---|---|---|
| `ChessUCI-424b0e0-20260706.exe` | 2026-07-06 | `424b0e0` (clean tree) | First frozen baseline. Search: iterative deepening, aspiration windows, null-move pruning, futility pruning, LMR, quiescence + SEE, killer/history move ordering. Built Release (Ninja/MSVC) from the commit that added the UCI adapter and testbed. Selftest passed. (Hash renamed from `7d7cdff` after a history rewrite; identical source and binary.) |
| `ChessUCI-8b4e5f3-20260707.exe` | 2026-07-07 | `8b4e5f3` (clean tree) | All seven engine-core bug fixes (SEE unwind, king-safety scaling, endgame flags, futility pruning, KPK, eval antisymmetry, minors). Beat the 424b0e0 baseline +60 -11 =29 over 100 games @ 1000 ms/move (+186 +/- 64 Elo, LOS 100%). Reference opponent for the speed/eval improvement series. |
| `ChessUCI-9d23aa4-20260707.exe` | 2026-07-07 | `9d23aa4` (clean tree) | Improvements #1+#2: per-node legal-move scan removed, single cached inCheck per node. ~1.05-1.25x faster at fixed depth. Frozen so the #1+#2-only match stays runnable after later builds. |
