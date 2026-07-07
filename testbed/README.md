# Engine A/B testbed

In-house tooling for answering one question: **did my algorithm change make the engine
stronger?** It pits two separately compiled engine builds against each other over a
paired-opening match and reports W/L/D + Elo with error bars.

Two pieces, cleanly separated:

1. **`ChessUCI` (C++ build target)** — a minimal UCI front-end over the existing
   `ChessEngine` (`src/uci_main.cpp`). Every build you want to test gets compiled into
   its own `ChessUCI.exe`.
2. **`testbed/run_match.py` (Python referee)** — launches two such executables as child
   processes and plays them against each other. It never links your C++; it only speaks
   the UCI subset below. Requires `pip install chess` (python-chess).

---

## Workflow

```powershell
# 1. build the baseline once and squirrel it away
cmake --build build --target ChessUCI
copy build\ChessUCI.exe testbed\baselines\ChessUCI-v0.exe

# 2. hack on the engine, rebuild
cmake --build build --target ChessUCI

# 3. play new vs frozen baseline: 50 openings x 2 colors = 100 games @ 1000 ms/move
python testbed\run_match.py build\ChessUCI.exe testbed\baselines\ChessUCI-v0.exe `
    --label-a new --label-b v0 --movetime 1000
```

Outputs land in `testbed/results/match_<timestamp>/`:

- `summary.txt` — W/L/D, score, Elo ± 95% margin, LOS, per-opening breakdown
- `games.pgn` — every game with opening name, termination reason, "end of book" marker
- `incidents.log` — every timeout, illegal move, crash, soft budget overrun

A running summary (score/Elo so far) prints after each game.

## UCI subset implemented by ChessUCI

| command | behaviour |
|---|---|
| `uci` | `id name <name>` (set with `--name <id>`), `id author`, `uciok` |
| `isready` | `readyok` (also answered mid-search) |
| `ucinewgame` | resets to the start position |
| `position startpos [moves ...]` | rebuilds the board by re-applying the full move list |
| `position fen <FEN> [moves ...]` | ditto from a FEN (Board::loadFromFEN) |
| `go movetime <ms>` | time-limited iterative deepening (depth cap 64), prints `info score cp ... pv ...` then `bestmove <lan>` |
| `go depth <n>` | fixed-depth search, effectively no time limit — reproducible mode |
| `stop` | aborts the running search (best-so-far move is returned) |
| `quit` | clean shutdown |

Flags: `--selftest` (notation round-trip + board application tests), `--verbose`
(re-enables the engine's std::cout debug spew — protocol output is interleaved with it,
so only use this for eyeballing), `--name <id>`.

Because it speaks real UCI you can also load `ChessUCI.exe` into a chess GUI
(Arena, CuteChess, BanksiaGUI, ...) and play it against Stockfish to sanity-check
strength or debug a build interactively.

## Referee details

- **Paired openings**: every line in `openings.txt` is played twice with colors
  reversed, so first-move advantage and any opening imbalance cancel out of the A-vs-B
  comparison. The file is plain SAN and freely editable; every line is validated with
  python-chess at startup.
- **Authority**: python-chess owns the board. Checkmate, stalemate, insufficient
  material, fifty-move rule, and threefold repetition are adjudicated by the referee —
  the engines are never asked their opinion.
- **Robustness**: an illegal move or a `bestmove` that arrives later than
  `movetime + --margin` (default 300 ms) forfeits the game for the offender; a crash or
  hang gets the process killed and scored as a loss; games past `--ply-cap` (default
  300) are drawn. Everything is logged and the match continues.
- **Fresh processes per game**: no state (hash, history, killers) leaks between games,
  and a crashed engine can't poison the next game.
- **Concurrency**: `--concurrency N` plays N games in parallel. Both engines are hit by
  the extra CPU load equally, so the comparison stays fair, but keep
  N ≤ physical cores / 2 so each search still gets a full core.
- **Determinism**: `--seed` fixes the opening order. The games themselves are NOT
  deterministic under `movetime` (see caveats).
- **SPRT mode**: `--sprt --elo0 0 --elo1 10 --alpha 0.05 --beta 0.05` runs a sequential
  test instead of a fixed 100 games: it stops as soon as the data is decisive
  (often well under 100 games for big changes, more for small ones; capped by
  `--max-games`).

## Reading the numbers honestly

- `score = (W + 0.5 D) / N`, `Elo = -400 log10(1/score - 1)`, error bar from the
  observed per-game variance (95%, normal approximation), `LOS` = likelihood of
  superiority = P(A is stronger | decisive games).
- **~100 games ⇒ ±50–70 Elo error bar.** Only changes of roughly **40+ Elo** are
  reliably detectable at that sample size. A +20 Elo tweak will routinely come out
  negative in a 100-game match. When the error bar straddles zero, the honest answer
  is "not enough data": re-run, run more games, or use `--sprt`.
- Time-based search is nondeterministic (OS scheduling changes node counts), so two
  identical matches will not reproduce. For bit-reproducible comparisons use
  `--depth N` — but remember fixed-depth ignores real search-speed differences, so it
  only measures move *quality* per depth, not strength per second.
