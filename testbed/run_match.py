#!/usr/bin/env python3
"""
run_match.py - in-house A/B match referee for two UCI chess engine builds.

Pits engine A ("new") against engine B ("baseline") over a paired-opening match:
every opening in the book is played twice with colors reversed, which cancels
first-move advantage. python-chess is the authoritative arbiter for legality and
game termination; the engines are only asked "position ... / go ...".

Typical use:
    python testbed/run_match.py build_new/ChessUCI.exe build_base/ChessUCI.exe ^
        --label-a new --label-b baseline --movetime 1000

Requires: pip install chess
"""

import argparse
import datetime
import json
import math
import queue
import random
import subprocess
import sys
import threading
import time
from dataclasses import dataclass, field
from pathlib import Path

try:
    import chess
    import chess.pgn
except ImportError:
    sys.exit("python-chess is required:  pip install chess")


# ---------------------------------------------------------------------------
# UCI engine process wrapper
# ---------------------------------------------------------------------------

class EngineError(Exception):
    """Engine crashed, closed its pipes, or broke protocol."""


class EngineTimeout(Exception):
    """Engine failed to produce an expected reply within the deadline."""


class UciEngine:
    def __init__(self, path: str):
        self.path = path
        try:
            self.proc = subprocess.Popen(
                [path],
                stdin=subprocess.PIPE,
                stdout=subprocess.PIPE,
                stderr=subprocess.DEVNULL,
                text=True,
                encoding="utf-8",
                errors="replace",
                bufsize=1,
            )
        except OSError as e:
            raise EngineError(f"failed to launch {path}: {e}")
        self._lines: "queue.Queue[str|None]" = queue.Queue()
        self.notes = []   # "info string ..." lines seen while waiting (engine anomalies)
        t = threading.Thread(target=self._reader, daemon=True)
        t.start()

    def _reader(self):
        try:
            for line in self.proc.stdout:
                self._lines.put(line.rstrip("\r\n"))
        except Exception:
            pass
        self._lines.put(None)  # EOF sentinel

    def send(self, cmd: str):
        try:
            self.proc.stdin.write(cmd + "\n")
            self.proc.stdin.flush()
        except OSError as e:
            raise EngineError(f"pipe write failed ({e})")

    def wait_for(self, keyword: str, timeout: float) -> str:
        """Wait for a line whose first token is `keyword`. Other lines are ignored."""
        deadline = time.monotonic() + timeout
        while True:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise EngineTimeout(f"no '{keyword}' within {timeout:.1f}s")
            try:
                line = self._lines.get(timeout=remaining)
            except queue.Empty:
                raise EngineTimeout(f"no '{keyword}' within {timeout:.1f}s")
            if line is None:
                raise EngineError("engine closed stdout (crashed?)")
            parts = line.split()
            if parts and parts[0] == keyword:
                return line
            # engines only send "info string" for anomalies (fallback move,
            # rejected position); surface them instead of discarding
            if len(parts) >= 2 and parts[0] == "info" and parts[1] == "string":
                self.notes.append(" ".join(parts[2:]))

    def handshake(self, timeout: float = 10.0):
        self.send("uci")
        self.wait_for("uciok", timeout)
        self.send("isready")
        self.wait_for("readyok", timeout)

    def newgame(self, timeout: float = 10.0):
        self.send("ucinewgame")
        self.send("isready")
        self.wait_for("readyok", timeout)

    def bestmove(self, position_cmd: str, go_cmd: str, deadline_s: float):
        """Returns (uci_move_string, elapsed_seconds). Raises on timeout/crash."""
        self.send(position_cmd)
        self.send(go_cmd)
        t0 = time.monotonic()
        line = self.wait_for("bestmove", deadline_s)
        elapsed = time.monotonic() - t0
        parts = line.split()
        return (parts[1] if len(parts) > 1 else ""), elapsed

    def kill(self):
        try:
            if self.proc.poll() is None:
                try:
                    self.send("quit")
                except EngineError:
                    pass
                try:
                    self.proc.wait(timeout=2.0)
                except subprocess.TimeoutExpired:
                    self.proc.kill()
        except Exception:
            pass


# ---------------------------------------------------------------------------
# Openings
# ---------------------------------------------------------------------------

@dataclass
class Opening:
    name: str
    san: str
    uci_moves: list


def load_openings(path: Path):
    openings = []
    with open(path, encoding="utf-8") as f:
        for lineno, raw in enumerate(f, 1):
            line = raw.strip()
            if not line or line.startswith("#"):
                continue
            name, sep, sans = line.partition(":")
            if not sep:
                sys.exit(f"{path}:{lineno}: expected '<name> : <SAN moves>'")
            board = chess.Board()
            ucis = []
            try:
                for san in sans.split():
                    move = board.push_san(san)
                    ucis.append(move.uci())
            except ValueError as e:
                sys.exit(f"{path}:{lineno} [{name.strip()}]: illegal SAN: {e}")
            if not ucis:
                sys.exit(f"{path}:{lineno} [{name.strip()}]: no moves")
            openings.append(Opening(name.strip(), sans.strip(), ucis))
    if not openings:
        sys.exit(f"{path}: no openings found")
    return openings


# ---------------------------------------------------------------------------
# Single game
# ---------------------------------------------------------------------------

@dataclass
class GameRecord:
    game_id: str
    opening: Opening
    a_is_white: bool
    result: str = "*"          # from White's perspective: 1-0, 0-1, 1/2-1/2
    reason: str = ""
    moves: list = field(default_factory=list)   # full UCI move list incl. opening
    incidents: list = field(default_factory=list)

    @property
    def a_score(self) -> float:
        if self.result == "1/2-1/2":
            return 0.5
        if self.result == "1-0":
            return 1.0 if self.a_is_white else 0.0
        if self.result == "0-1":
            return 0.0 if self.a_is_white else 1.0
        return 0.5


def play_game(rec: GameRecord, path_a: str, path_b: str, cfg) -> GameRecord:
    """Plays one game. Never raises: failures become results + incidents."""
    board = chess.Board()
    for uci in rec.opening.uci_moves:
        board.push(chess.Move.from_uci(uci))
    rec.moves = list(rec.opening.uci_moves)

    white_path = path_a if rec.a_is_white else path_b
    black_path = path_b if rec.a_is_white else path_a
    engines = {}

    def side_name(color):
        is_a = (color == chess.WHITE) == rec.a_is_white
        return "A" if is_a else "B"

    def win_for(color, reason):
        rec.result = "1-0" if color == chess.WHITE else "0-1"
        rec.reason = reason

    def loss_for(color, reason):
        win_for(not color, reason)
        rec.incidents.append(f"game {rec.game_id}: {side_name(color)} "
                             f"({'white' if color == chess.WHITE else 'black'}) {reason}")

    try:
        starting = chess.WHITE
        try:
            for starting, path in ((chess.WHITE, white_path), (chess.BLACK, black_path)):
                engines[starting] = UciEngine(path)
                engines[starting].handshake()
                engines[starting].newgame()
        except (EngineError, EngineTimeout) as e:
            loss_for(starting, f"failed to start/handshake: {e}")
            return rec

        if cfg.depth:
            go_cmd = f"go depth {cfg.depth}"
            deadline = cfg.move_timeout
        else:
            go_cmd = f"go movetime {cfg.movetime}"
            deadline = (cfg.movetime + cfg.margin) / 1000.0

        while True:
            # referee adjudication comes first, engines are never consulted
            if board.is_checkmate():
                win_for(not board.turn, "checkmate")
                return rec
            if board.is_stalemate():
                rec.result, rec.reason = "1/2-1/2", "stalemate"
                return rec
            if board.is_insufficient_material():
                rec.result, rec.reason = "1/2-1/2", "insufficient material"
                return rec
            if board.can_claim_fifty_moves():
                rec.result, rec.reason = "1/2-1/2", "fifty-move rule"
                return rec
            if board.is_repetition(3):
                rec.result, rec.reason = "1/2-1/2", "threefold repetition"
                return rec
            if len(rec.moves) >= cfg.ply_cap:
                rec.result, rec.reason = "1/2-1/2", f"ply cap ({cfg.ply_cap})"
                return rec

            mover = engines[board.turn]
            pos_cmd = "position startpos"
            if rec.moves:
                pos_cmd += " moves " + " ".join(rec.moves)

            try:
                uci, elapsed = mover.bestmove(pos_cmd, go_cmd, deadline)
                for note in mover.notes:
                    rec.incidents.append(
                        f"game {rec.game_id}: {side_name(board.turn)} says: {note}")
                mover.notes.clear()
            except EngineTimeout:
                loss_for(board.turn,
                         f"time forfeit: no bestmove within {deadline:.2f}s "
                         f"(budget {go_cmd!r} + margin) at ply {len(rec.moves)}")
                return rec
            except EngineError as e:
                loss_for(board.turn, f"crash: {e} at ply {len(rec.moves)}")
                return rec

            # a few ms past movetime is normal pipe/scheduler overhead; only log
            # overruns that eat a meaningful chunk of the forfeit margin
            if not cfg.depth and elapsed * 1000.0 > cfg.movetime + cfg.margin / 2:
                rec.incidents.append(
                    f"game {rec.game_id}: {side_name(board.turn)} soft overrun "
                    f"{elapsed*1000.0:.0f}ms > {cfg.movetime}ms at ply {len(rec.moves)}")

            try:
                move = chess.Move.from_uci(uci)
            except ValueError:
                move = chess.Move.null()
            if move not in board.legal_moves:
                loss_for(board.turn,
                         f"illegal move '{uci}' at ply {len(rec.moves)} "
                         f"(fen {board.fen()})")
                return rec

            board.push(move)
            rec.moves.append(uci)
    finally:
        for eng in engines.values():
            eng.kill()


# ---------------------------------------------------------------------------
# Statistics
# ---------------------------------------------------------------------------

def elo_from_score(score: float) -> float:
    score = min(max(score, 1e-9), 1 - 1e-9)
    return -400.0 * math.log10(1.0 / score - 1.0)


def match_stats(w: int, d: int, l: int):
    """Returns (score, elo, elo_margin95, los). elo values may be +/-inf at 0%/100%."""
    n = w + d + l
    if n == 0:
        return 0.5, 0.0, float("inf"), 0.5
    score = (w + 0.5 * d) / n
    elo = elo_from_score(score)
    # error bar from regularized counts (half-game prior): an all-decisive
    # record must not report zero variance ("+3600 +/- 0" after one win)
    rw, rd, rl = w + 0.5, d + 0.5, l + 0.5
    rn = rw + rd + rl
    rscore = (rw + 0.5 * rd) / rn
    var = (rw * (1 - rscore) ** 2 + rd * (0.5 - rscore) ** 2 + rl * (0 - rscore) ** 2) / rn
    se = math.sqrt(var / rn)
    lo = elo_from_score(rscore - 1.96 * se)
    hi = elo_from_score(rscore + 1.96 * se)
    margin = max(hi - elo, elo - lo)  # slightly asymmetric; report the wider side
    los = 0.5 * (1 + math.erf((w - l) / math.sqrt(2 * (w + l)))) if (w + l) > 0 else 0.5
    return score, elo, margin, los


def sprt_llr(w: int, d: int, l: int, elo0: float, elo1: float) -> float:
    """GSPRT log-likelihood ratio approximation (trinomial, logistic elo)."""
    if w + d + l == 0:
        return 0.0
    # half-game prior: keeps the variance finite on lopsided records so an
    # all-wins streak registers as strong evidence instead of llr 0.0
    w, d, l = w + 0.5, d + 0.5, l + 0.5
    n = w + d + l
    ww, dd = w / n, d / n
    s = ww + dd / 2.0
    m2 = ww + dd / 4.0
    var = m2 - s * s
    if var <= 0:
        return 0.0
    var_s = var / n
    s0 = 1.0 / (1.0 + 10.0 ** (-elo0 / 400.0))
    s1 = 1.0 / (1.0 + 10.0 ** (-elo1 / 400.0))
    return (s1 - s0) * (2.0 * s - s0 - s1) / (2.0 * var_s)


def sprt_bounds(alpha: float, beta: float):
    return math.log(beta / (1 - alpha)), math.log((1 - beta) / alpha)


# ---------------------------------------------------------------------------
# PGN output
# ---------------------------------------------------------------------------

def record_to_pgn(rec: GameRecord, cfg) -> chess.pgn.Game:
    game = chess.pgn.Game()
    h = game.headers
    h["Event"] = f"{cfg.label_a} vs {cfg.label_b} (testbed)"
    h["Site"] = "run_match.py"
    h["Date"] = datetime.date.today().strftime("%Y.%m.%d")
    h["Round"] = rec.game_id
    h["White"] = cfg.label_a if rec.a_is_white else cfg.label_b
    h["Black"] = cfg.label_b if rec.a_is_white else cfg.label_a
    h["Result"] = rec.result
    h["Opening"] = rec.opening.name
    h["Termination"] = rec.reason
    h["TimeControl"] = (f"depth {cfg.depth}" if cfg.depth
                        else f"{cfg.movetime/1000:g}s/move")

    node = game
    book_plies = len(rec.opening.uci_moves)
    for i, uci in enumerate(rec.moves):
        node = node.add_variation(chess.Move.from_uci(uci))
        if i == book_plies - 1:
            node.comment = "end of book"
    return game


# ---------------------------------------------------------------------------
# Match orchestration
# ---------------------------------------------------------------------------

class Match:
    def __init__(self, cfg, openings):
        self.cfg = cfg
        self.openings = openings
        self.records = []
        self.done = 0
        self.total = 0
        self.w = self.d = self.l = 0     # from A's perspective
        self.lock = threading.Lock()
        self.stop = threading.Event()    # set when SPRT reaches a decision
        self.threads = []

    def _tally(self, rec: GameRecord):
        with self.lock:
            self.records.append(rec)
            self.done += 1
            s = rec.a_score
            if s == 1.0:
                self.w += 1
            elif s == 0.0:
                self.l += 1
            else:
                self.d += 1

            score, elo, margin, _ = match_stats(self.w, self.d, self.l)
            a_col = "white" if rec.a_is_white else "black"
            print(f"[{self.done}/{self.total}] {rec.game_id:>6}  "
                  f"{rec.opening.name[:34]:<34} A={a_col:<5} {rec.result:<7} "
                  f"({rec.reason})")
            print(f"    running: +{self.w} -{self.l} ={self.d}  "
                  f"score {score:.3f}  elo {elo:+.0f} +/- {margin:.0f}")
            for inc in rec.incidents:
                print(f"    INCIDENT: {inc}")

            if self.cfg.sprt:
                llr = sprt_llr(self.w, self.d, self.l, self.cfg.elo0, self.cfg.elo1)
                lower, upper = sprt_bounds(self.cfg.alpha, self.cfg.beta)
                print(f"    SPRT: llr {llr:.2f} in [{lower:.2f}, {upper:.2f}]")
                if llr <= lower or llr >= upper:
                    self.stop.set()

    def run(self):
        cfg = self.cfg
        rng = random.Random(cfg.seed)
        order = list(self.openings)
        rng.shuffle(order)

        # build the paired schedule: each opening once with A white, once with B white
        pairs = []
        if cfg.sprt:
            n_pairs = cfg.max_games // 2
            for i in range(n_pairs):
                pairs.append((i, order[i % len(order)]))
        else:
            n_pairs = min(cfg.games // 2, 10**9)
            if n_pairs > len(order):
                # cycle the book if more games than openings were requested
                pairs = [(i, order[i % len(order)]) for i in range(n_pairs)]
            else:
                pairs = list(enumerate(order[:n_pairs]))

        schedule = []
        for i, op in pairs:
            schedule.append(GameRecord(f"{i+1}.1", op, a_is_white=True))
            schedule.append(GameRecord(f"{i+1}.2", op, a_is_white=False))
        self.total = len(schedule)

        work = queue.Queue()
        for rec in schedule:
            work.put(rec)

        def worker():
            while not self.stop.is_set():
                try:
                    rec = work.get_nowait()
                except queue.Empty:
                    return
                try:
                    play_game(rec, cfg.engine_a, cfg.engine_b, cfg)
                except Exception as e:
                    # referee bug: score a draw and log it rather than silently
                    # killing this worker and losing the game from the report
                    rec.result, rec.reason = "1/2-1/2", "referee error"
                    rec.incidents.append(f"game {rec.game_id}: referee exception: {e!r}")
                self._tally(rec)

        self.threads = [threading.Thread(target=worker) for _ in range(cfg.concurrency)]
        for t in self.threads:
            t.start()
        for t in self.threads:
            t.join()


def preflight(path: str, label: str):
    """Fail fast on a broken engine path/binary before burning a whole match."""
    try:
        eng = UciEngine(path)
        try:
            eng.handshake()
        finally:
            eng.kill()
    except (EngineError, EngineTimeout) as e:
        sys.exit(f"preflight failed for {label} ({path}): {e}")


def append_history(match: Match, cfg, out_dir: Path, elapsed_s: float):
    """Append this match to testbed/history.json and regenerate history.js,
    the data file behind results_viewer.html."""
    w, d, l = match.w, match.d, match.l
    n = w + d + l
    if n == 0:
        return
    score, elo, margin, los = match_stats(w, d, l)
    record = {
        "date": datetime.datetime.now().isoformat(timespec="seconds"),
        "label_a": cfg.label_a,
        "label_b": cfg.label_b,
        "engine_a": str(cfg.engine_a),
        "engine_b": str(cfg.engine_b),
        "control": (f"depth {cfg.depth}" if cfg.depth else f"{cfg.movetime} ms/move"),
        "games": n,
        "wins": w,
        "losses": l,
        "draws": d,
        "score": round(score, 4),
        "elo": round(elo, 1),
        "margin": round(margin, 1),
        "los": round(los, 4),
        "incidents": sum(len(r.incidents) for r in match.records),
        "duration_s": round(elapsed_s),
        "out_dir": str(out_dir),
    }

    base = Path(__file__).parent
    hist_json = base / "history.json"
    history = []
    if hist_json.exists():
        try:
            history = json.loads(hist_json.read_text(encoding="utf-8"))
        except (ValueError, OSError):
            print(f"warning: could not parse {hist_json}; starting a fresh history")
            history = []
    history.append(record)
    hist_json.write_text(json.dumps(history, indent=1) + "\n", encoding="utf-8")

    # history.js exists because browsers block fetch() of local files;
    # a <script src> include works from file:// with no server
    (base / "history.js").write_text(
        "// generated by run_match.py -- data for results_viewer.html\n"
        "window.MATCH_HISTORY = " + json.dumps(history, indent=1) + ";\n",
        encoding="utf-8")
    print(f"history: match #{len(history)} appended to {hist_json.name} "
          f"(open testbed/results_viewer.html to browse)")


def write_outputs(match: Match, cfg, out_dir: Path, elapsed_s: float):
    out_dir.mkdir(parents=True, exist_ok=True)
    records = sorted(match.records, key=lambda r: tuple(map(int, r.game_id.split("."))))

    with open(out_dir / "games.pgn", "w", encoding="utf-8") as f:
        for rec in records:
            print(record_to_pgn(rec, cfg), file=f, end="\n\n")

    incidents = [inc for rec in records for inc in rec.incidents]
    with open(out_dir / "incidents.log", "w", encoding="utf-8") as f:
        f.write("\n".join(incidents) + ("\n" if incidents else ""))

    w, d, l = match.w, match.d, match.l
    n = w + d + l
    score, elo, margin, los = match_stats(w, d, l)

    lines = []
    p = lines.append
    p(f"Match: {cfg.label_a} (A) vs {cfg.label_b} (B)")
    p(f"  A: {cfg.engine_a}")
    p(f"  B: {cfg.engine_b}")
    p(f"Control: {'depth ' + str(cfg.depth) if cfg.depth else str(cfg.movetime) + ' ms/move'}"
      f", margin {cfg.margin} ms, ply cap {cfg.ply_cap}, seed {cfg.seed}, "
      f"concurrency {cfg.concurrency}")
    p(f"Games: {n} ({elapsed_s/60:.1f} min)")
    p("")
    p(f"Result (A's perspective):  +{w} -{l} ={d}")
    p(f"Score: {score:.4f}   ({w + 0.5*d:g} / {n})")
    p(f"Elo:   {elo:+.1f} +/- {margin:.1f} (95%)")
    p(f"LOS:   {los*100:.1f}%")
    if cfg.sprt:
        llr = sprt_llr(w, d, l, cfg.elo0, cfg.elo1)
        lower, upper = sprt_bounds(cfg.alpha, cfg.beta)
        if llr >= upper:
            verdict = f"H1 accepted: elo >= {cfg.elo1}"
        elif llr <= lower:
            verdict = f"H0 accepted: elo <= {cfg.elo0}"
        else:
            verdict = "inconclusive (max games reached)"
        p(f"SPRT:  llr {llr:.2f} in [{lower:.2f}, {upper:.2f}] -> {verdict}")
    p("")

    p("Per-opening breakdown (A's points / 2):")
    by_opening = {}
    for rec in records:
        by_opening.setdefault(rec.opening.name, []).append(rec)
    sym = {1.0: "W", 0.5: "D", 0.0: "L"}
    for name, recs in sorted(by_opening.items()):
        pts = sum(r.a_score for r in recs)
        marks = "".join(sym[r.a_score] for r in recs)
        p(f"  {name:<40} {marks:<6} {pts:g}/{len(recs)}")
    p("")

    if incidents:
        p(f"Incidents ({len(incidents)}): see incidents.log")
        p("")

    p("CAVEAT: ~100 games gives a 95% error bar of roughly +/-50-70 Elo, so only")
    p("differences of ~40+ Elo are reliably detectable at this sample size.")
    p("Time-based search is nondeterministic: re-run borderline results, run more")
    p("games, use --sprt, or use --depth N for reproducible comparisons.")

    report = "\n".join(lines)
    with open(out_dir / "summary.txt", "w", encoding="utf-8") as f:
        f.write(report + "\n")

    print("\n" + "=" * 72)
    print(report)
    print("=" * 72)
    print(f"outputs: {out_dir / 'games.pgn'}, {out_dir / 'summary.txt'}, "
          f"{out_dir / 'incidents.log'}")

    if not cfg.no_history:
        append_history(match, cfg, out_dir, elapsed_s)


def main():
    ap = argparse.ArgumentParser(description="Paired-opening A/B match between two UCI engine builds.")
    ap.add_argument("engine_a", help="path to engine A (the new build)")
    ap.add_argument("engine_b", help="path to engine B (the baseline build)")
    ap.add_argument("--label-a", default=None, help="label for engine A (default: exe name)")
    ap.add_argument("--label-b", default=None, help="label for engine B (default: exe name)")
    ap.add_argument("--openings", default=str(Path(__file__).parent / "openings.txt"))
    ap.add_argument("--movetime", type=int, default=1000, help="ms per move (default 1000)")
    ap.add_argument("--margin", type=int, default=300,
                    help="ms of grace beyond movetime before a time forfeit (default 300)")
    ap.add_argument("--depth", type=int, default=None,
                    help="use 'go depth N' instead of movetime (reproducible mode)")
    ap.add_argument("--move-timeout", type=float, default=120.0,
                    help="hard per-move timeout in seconds for --depth mode (default 120)")
    ap.add_argument("--games", type=int, default=None,
                    help="total games, rounded down to pairs (default: 2 x openings)")
    ap.add_argument("--ply-cap", type=int, default=300,
                    help="adjudicate a draw beyond this many plies (default 300)")
    ap.add_argument("--concurrency", type=int, default=1,
                    help="games in parallel; keep <= physical cores / 2 for fair timing")
    ap.add_argument("--seed", type=int, default=42, help="shuffles opening order only")
    ap.add_argument("--out", default=None, help="output directory (default: testbed/results/<timestamp>)")
    ap.add_argument("--no-history", action="store_true",
                    help="do not append this match to testbed/history.json (smoke/test runs)")
    ap.add_argument("--sprt", action="store_true",
                    help="sequential test instead of a fixed number of games")
    ap.add_argument("--elo0", type=float, default=0.0)
    ap.add_argument("--elo1", type=float, default=10.0)
    ap.add_argument("--alpha", type=float, default=0.05)
    ap.add_argument("--beta", type=float, default=0.05)
    ap.add_argument("--max-games", type=int, default=1000, help="SPRT game cap (default 1000)")
    cfg = ap.parse_args()

    cfg.label_a = cfg.label_a or Path(cfg.engine_a).stem + "-A"
    cfg.label_b = cfg.label_b or Path(cfg.engine_b).stem + "-B"

    openings = load_openings(Path(cfg.openings))
    games_explicit = cfg.games is not None
    if cfg.games is None:
        cfg.games = 2 * len(openings)
    if cfg.games % 2:
        print(f"note: rounding --games down to {cfg.games - 1} to keep color-reversed pairs")
        cfg.games -= 1
    if cfg.sprt and games_explicit:
        # an explicit --games acts as the SPRT cap instead of being ignored
        cfg.max_games = min(cfg.max_games, cfg.games)
        print(f"note: SPRT capped at --games {cfg.max_games}")

    print(f"openings: {len(openings)} loaded from {cfg.openings} (all validated)")
    preflight(cfg.engine_a, cfg.label_a)
    preflight(cfg.engine_b, cfg.label_b)
    print(f"preflight OK: {cfg.label_a}, {cfg.label_b}")

    out_dir = Path(cfg.out) if cfg.out else (
        Path(__file__).parent / "results" /
        datetime.datetime.now().strftime("match_%Y%m%d_%H%M%S"))

    match = Match(cfg, openings)
    t0 = time.monotonic()
    try:
        match.run()
    except KeyboardInterrupt:
        # join the workers before reporting: write_outputs must not race _tally
        print("\ninterrupted: waiting for in-flight games to finish "
              "(Ctrl-C again to abort without a report)")
        match.stop.set()
        for t in match.threads:
            t.join()
        print("reporting completed games only")
    write_outputs(match, cfg, out_dir, time.monotonic() - t0)


if __name__ == "__main__":
    main()
