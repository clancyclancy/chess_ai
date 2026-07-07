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
