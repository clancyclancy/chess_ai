// uci_main.cpp
// minimal UCI front-end wrapping ChessEngine, so any build of the engine can be driven
// by the testbed match runner (testbed/run_match.py) or a normal chess GUI.
//
// supported subset:
//   uci, isready, ucinewgame,
//   position [startpos | fen <FEN>] [moves <m1> <m2> ...],
//   go [movetime <ms> | depth <n>], stop, quit
//
// flags:
//   --selftest      run notation round-trip + board application tests and exit
//   --verbose       do NOT suppress the engine's std::cout debug spew
//   --name <id>     the name reported to "uci" (label builds, e.g. --name lmr-v2)
//
// notation: UCI long algebraic. board mapping is row 0 = rank 8, col 0 = file a
// (see Move::toString), so square = ('a'+col, '1'+(7-row)).

#include "ChessEngine.h"
#include "Board.h"
#include "Move.h"

#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <deque>
#include <thread>
#include <mutex>
#include <atomic>
#include <chrono>
#include <memory>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <timeapi.h>
#endif

namespace {

// ---------------------------------------------------------------------------
// engine debug output suppression
// the engine prints search info to std::cout from its own thread. a UCI driver
// must only ever see protocol lines on stdout, so std::cout gets redirected to
// this sink and protocol output goes through a separate ostream bound to the
// real stdout buffer.
// ---------------------------------------------------------------------------
class NullBuf : public std::streambuf
{
protected:
    int overflow(int c) override { return c == EOF ? '\0' : c; }
};

// ---------------------------------------------------------------------------
// square / move <-> UCI notation
// ---------------------------------------------------------------------------
bool squareFromUci(char fileCh, char rankCh, int& row, int& col)
{
    if (fileCh < 'a' || fileCh > 'h' || rankCh < '1' || rankCh > '8')
        return false;
    col = fileCh - 'a';
    row = 7 - (rankCh - '1');
    return true;
}

std::string squareToUci(int row, int col)
{
    std::string s;
    s += static_cast<char>('a' + col);
    s += static_cast<char>('1' + (7 - row));
    return s;
}

bool promoCharToType(char c, PieceType& t)
{
    switch (c)
    {
        case 'q': t = PieceType::QUEEN;  return true;
        case 'r': t = PieceType::ROOK;   return true;
        case 'b': t = PieceType::BISHOP; return true;
        case 'n': t = PieceType::KNIGHT; return true;
        default: return false;
    }
}

char promoTypeToChar(PieceType t)
{
    switch (t)
    {
        case PieceType::QUEEN:  return 'q';
        case PieceType::ROOK:   return 'r';
        case PieceType::BISHOP: return 'b';
        case PieceType::KNIGHT: return 'n';
        default: return '\0';
    }
}

// "e2e4" / "e7e8q" -> Move. only coordinates + promotion; Board::makeMove and
// makeUncheckedMove derive castling / en passant from the coordinates.
bool uciToMove(const std::string& s, Move& out)
{
    if (s.size() != 4 && s.size() != 5)
        return false;

    int fr, fc, tr, tc;
    if (!squareFromUci(s[0], s[1], fr, fc) || !squareFromUci(s[2], s[3], tr, tc))
        return false;

    out = Move(fr, fc, tr, tc);

    if (s.size() == 5)
    {
        PieceType t;
        if (!promoCharToType(s[4], t))
            return false;
        out.setPromotion(t);
    }
    return true;
}

// Move -> "e2e4" / "e7e8q". NOTE: Move::toString() prints the promotion piece in
// UPPERCASE which is not valid UCI, hence this separate formatter.
std::string moveToUci(const Move& m)
{
    std::string s = squareToUci(m.getFromRow(), m.getFromCol())
                  + squareToUci(m.getToRow(), m.getToCol());
    if (m.getIsPromotion())
    {
        char p = promoTypeToChar(m.getPromotionType());
        if (p) s += p;
    }
    return s;
}

// apply one UCI move token to a board through the validating makeMove() path
bool applyUciMove(Board& b, const std::string& token)
{
    Move m;
    if (!uciToMove(token, m))
        return false;
    return b.makeMove(m);
}

// first legal move for the side to move, used as a fallback if the search
// returns no move (e.g. "stop" before iteration 1 finished). returns false if
// the side to move has no legal moves at all.
bool anyLegalMove(Board& b, Move& out)
{
    for (int r = 0; r < 8; r++)
    {
        for (int c = 0; c < 8; c++)
        {
            std::vector<Move> moves = b.getLegalMoves(r, c); // empty unless piece of side to move
            if (!moves.empty())
            {
                out = moves[0];
                return true;
            }
        }
    }
    return false;
}

// ---------------------------------------------------------------------------
// stdin reader thread: lets "stop" / "isready" / "quit" be seen while a search
// is running (the go handler blocks the main loop until bestmove).
// ---------------------------------------------------------------------------
struct LineQueue
{
    std::mutex m;
    std::deque<std::string> q;
    std::atomic<bool> eof{false};

    void push(const std::string& s)
    {
        std::lock_guard<std::mutex> lock(m);
        q.push_back(s);
    }

    bool pop(std::string& out)
    {
        std::lock_guard<std::mutex> lock(m);
        if (q.empty()) return false;
        out = q.front();
        q.pop_front();
        return true;
    }

    bool peek(std::string& out)
    {
        std::lock_guard<std::mutex> lock(m);
        if (q.empty()) return false;
        out = q.front();
        return true;
    }

    void dropFront()
    {
        std::lock_guard<std::mutex> lock(m);
        if (!q.empty()) q.pop_front();
    }
};

// strip UTF-8 BOM (e.g. PowerShell prepends one when piping) and CR/LF tails
// so token matching sees clean commands
void sanitizeLine(std::string& s)
{
    if (s.size() >= 3 && static_cast<unsigned char>(s[0]) == 0xEF
                      && static_cast<unsigned char>(s[1]) == 0xBB
                      && static_cast<unsigned char>(s[2]) == 0xBF)
        s.erase(0, 3);
    while (!s.empty() && (s.back() == '\r' || s.back() == '\n'))
        s.pop_back();
}

// ---------------------------------------------------------------------------
// selftest
// ---------------------------------------------------------------------------
int g_failCount = 0;

void check(bool cond, const std::string& what, std::ostream& out)
{
    if (!cond)
    {
        g_failCount++;
        out << "FAIL: " << what << "\n";
    }
}

int runSelfTest(std::ostream& out)
{
    // square mapping anchors (row 0 = rank 8, a1 = row 7 col 0)
    check(squareToUci(7, 0) == "a1", "square (7,0) -> a1", out);
    check(squareToUci(0, 0) == "a8", "square (0,0) -> a8", out);
    check(squareToUci(7, 7) == "h1", "square (7,7) -> h1", out);
    check(squareToUci(0, 7) == "h8", "square (0,7) -> h8", out);
    check(squareToUci(6, 4) == "e2", "square (6,4) -> e2", out);

    // square round trip, both directions, all 64 squares
    for (int r = 0; r < 8; r++)
    {
        for (int c = 0; c < 8; c++)
        {
            std::string s = squareToUci(r, c);
            int rr = -1, cc = -1;
            check(squareFromUci(s[0], s[1], rr, cc) && rr == r && cc == c,
                  "square round trip " + s, out);
        }
    }

    // move round trip: string -> Move -> string
    const char* moveStrings[] = { "e2e4", "e7e5", "e1g1", "e8c8", "a7a8q",
                                  "h2h1n", "b7b8r", "g2g1b", "e7e8n" };
    for (const char* ms : moveStrings)
    {
        Move m;
        check(uciToMove(ms, m) && moveToUci(m) == ms,
              std::string("move round trip ") + ms, out);
    }

    // move round trip: Move -> string -> Move, all from/to square pairs
    for (int i = 0; i < 64; i++)
    {
        for (int j = 0; j < 64; j++)
        {
            Move m(i / 8, i % 8, j / 8, j % 8);
            Move back;
            bool ok = uciToMove(moveToUci(m), back) && back == m;
            if (!ok)
            {
                check(false, "move round trip from=" + std::to_string(i) + " to=" + std::to_string(j), out);
            }
        }
    }

    // cross-check against the engine's own notation for non-promotion moves
    // (Move::toString uses the same squares but uppercase promo letters)
    {
        Move m(6, 4, 4, 4); // e2e4
        check(m.toString() == moveToUci(m), "moveToUci agrees with Move::toString for e2e4", out);
    }

    // promotion move parsing keeps the piece type
    {
        Move m;
        check(uciToMove("a7a8n", m) && m.getIsPromotion()
                  && m.getPromotionType() == PieceType::KNIGHT,
              "a7a8n parses as knight promotion", out);
    }

    // reject garbage
    {
        Move m;
        check(!uciToMove("e2e9", m), "reject e2e9", out);
        check(!uciToMove("i2e4", m), "reject i2e4", out);
        check(!uciToMove("e2e4x", m), "reject promo char x", out);
        check(!uciToMove("e2", m), "reject short token", out);
    }

    // board application: castling detected from king two-square move
    {
        Board b;
        const char* line[] = { "e2e4", "e7e5", "g1f3", "b8c6", "f1c4", "f8c5" };
        bool ok = true;
        for (const char* t : line) ok = ok && applyUciMove(b, t);
        check(ok, "apply italian opening moves", out);

        ok = applyUciMove(b, "e1g1"); // white castles short
        check(ok, "e1g1 accepted as castling", out);
        check(b.getPieceConst(7, 6).getType() == PieceType::KING, "king landed on g1", out);
        check(b.getPieceConst(7, 5).getType() == PieceType::ROOK, "rook landed on f1", out);
        check(b.getPieceConst(7, 7).isEmpty(), "h1 rook square emptied", out);
    }

    // board application: en passant capture removes the passed pawn
    {
        Board b;
        const char* line[] = { "e2e4", "g8f6", "e4e5", "d7d5" };
        bool ok = true;
        for (const char* t : line) ok = ok && applyUciMove(b, t);
        ok = ok && applyUciMove(b, "e5d6"); // en passant
        check(ok, "e5d6 en passant accepted", out);
        check(b.getPieceConst(2, 3).getType() == PieceType::PAWN, "white pawn on d6", out);
        check(b.getPieceConst(3, 3).isEmpty(), "black d5 pawn removed by en passant", out);
    }

    // board application: promotion places the chosen piece
    {
        Board b;
        b.loadFromFEN("8/P6k/8/8/8/8/7K/8 w - - 0 1");
        check(applyUciMove(b, "a7a8q"), "a7a8q accepted", out);
        check(b.getPieceConst(0, 0).getType() == PieceType::QUEEN
                  && b.getPieceConst(0, 0).getColor() == PieceColor::WHITE,
              "white queen on a8", out);
    }
    {
        Board b;
        b.loadFromFEN("8/7k/8/8/8/8/p6K/8 b - - 0 1");
        check(applyUciMove(b, "a2a1n"), "a2a1n accepted", out);
        check(b.getPieceConst(7, 0).getType() == PieceType::KNIGHT
                  && b.getPieceConst(7, 0).getColor() == PieceColor::BLACK,
              "black knight on a1", out);
    }

    // board application: illegal moves rejected
    {
        Board b;
        check(!applyUciMove(b, "e2e5"), "reject e2e5 from startpos", out);
        check(!applyUciMove(b, "e7e5"), "reject black move when white to play", out);
        check(!applyUciMove(b, "e1g1"), "reject castling through pieces", out);
    }

    if (g_failCount == 0)
    {
        out << "selftest: all checks passed\n";
        return 0;
    }
    out << "selftest: " << g_failCount << " check(s) FAILED\n";
    return 1;
}

} // namespace

// ---------------------------------------------------------------------------
// main UCI loop
// ---------------------------------------------------------------------------
int main(int argc, char* argv[])
{
    std::string engineName = "ChessAI";
    bool verbose = false;
    bool selftest = false;

    for (int i = 1; i < argc; i++)
    {
        std::string arg = argv[i];
        if (arg == "--selftest") selftest = true;
        else if (arg == "--verbose") verbose = true;
        else if (arg == "--name" && i + 1 < argc) engineName = argv[++i];
    }

    // protocol output goes to the real stdout; std::cout (engine debug spew)
    // gets swallowed unless --verbose
    NullBuf nullBuf;
    std::streambuf* realStdout = std::cout.rdbuf();
    std::ostream uciOut(realStdout);

    if (selftest)
        return runSelfTest(uciOut);

#ifdef _WIN32
    // default windows timer granularity is ~15.6ms, which turns the 1-2ms polling
    // sleeps below into ~16ms and delays every bestmove by that much
    timeBeginPeriod(1);
#endif

    if (!verbose)
        std::cout.rdbuf(&nullBuf);

    auto send = [&](const std::string& line)
    {
        uciOut << line << "\n" << std::flush;
    };

    // heap-allocated like the GUI's main(): ChessEngine holds multi-MB arrays
    // (pv tables, history) that overflow the default stack as a local
    auto enginePtr = std::make_unique<ChessEngine>();
    ChessEngine& engine = *enginePtr;
    engine.start();

    Board position;              // authoritative position, rebuilt on every "position" command
    engine.setBoard(position);

    LineQueue lines;
    std::thread reader([&lines]()
    {
        std::string line;
        while (std::getline(std::cin, line))
        {
            sanitizeLine(line);
            lines.push(line);
        }
        lines.eof = true;
    });

    bool quit = false;

    // handles commands that must work even while a search is running
    // returns true if the token was consumed
    auto handleAnytime = [&](const std::string& cmd, bool searching) -> bool
    {
        if (cmd == "isready")
        {
            send("readyok");
            return true;
        }
        if (cmd == "stop")
        {
            if (searching)
                engine.setMaxThinkingTimeMs(0); // trips timeExceeded(), search unwinds
            return true;
        }
        if (cmd == "quit")
        {
            quit = true;
            if (searching)
                engine.setMaxThinkingTimeMs(0);
            return true;
        }
        return false;
    };

    while (!quit)
    {
        std::string line;
        if (!lines.pop(line))
        {
            if (lines.eof)
                break; // stdin closed: treat as quit so a killed referee can't leave us hanging
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            continue;
        }

        std::istringstream iss(line);
        std::string cmd;
        iss >> cmd;

        if (cmd.empty())
            continue;

        if (handleAnytime(cmd, false))
            continue;

        if (cmd == "uci")
        {
            send("id name " + engineName);
            send("id author clancy");
            send("uciok");
        }
        else if (cmd == "ucinewgame")
        {
            position = Board();
            engine.setBoard(position);
        }
        else if (cmd == "position")
        {
            std::string tok;
            iss >> tok;

            Board b;
            bool ok = true;

            if (tok == "startpos")
            {
                iss >> tok; // "moves" or nothing
            }
            else if (tok == "fen")
            {
                // collect fen fields until "moves" or end; pad missing clocks so
                // loadFromFEN never reads uninitialized ints
                std::vector<std::string> fenParts;
                while (iss >> tok && tok != "moves")
                    fenParts.push_back(tok);
                while (fenParts.size() < 5) fenParts.push_back("0");
                while (fenParts.size() < 6) fenParts.push_back("1");

                std::string fen;
                for (size_t i = 0; i < fenParts.size(); i++)
                    fen += (i ? " " : "") + fenParts[i];

                ok = b.loadFromFEN(fen);
                if (!ok)
                    send("info string invalid fen: " + fen);
            }
            else
            {
                send("info string malformed position command");
                ok = false;
            }

            if (ok && tok == "moves")
            {
                std::string mv;
                while (iss >> mv)
                {
                    if (!applyUciMove(b, mv))
                    {
                        send("info string illegal move in position command: " + mv);
                        break;
                    }
                }
            }

            if (ok)
            {
                position = b;
                engine.setBoard(position);
            }
        }
        else if (cmd == "go")
        {
            int movetime = -1;
            int depth = -1;

            std::string tok;
            while (iss >> tok)
            {
                if (tok == "movetime") iss >> movetime;
                else if (tok == "depth") iss >> depth;
                // wtime/btime/winc/binc/infinite unsupported: fall through to default budget
            }

            if (depth > 0 && movetime <= 0)
            {
                // fixed-depth mode (reproducible): depth is the binding constraint
                engine.setSearchDepth(depth);
                engine.setMaxThinkingTimeMs(1 << 30);
            }
            else
            {
                // movetime mode: raise the depth cap so time is the binding constraint
                if (movetime <= 0) movetime = 1000;
                engine.setSearchDepth(64);
                engine.setMaxThinkingTimeMs(movetime);
            }

            // sync engine to the authoritative position (the engine auto-applies
            // its move after every search, so never trust its internal board)
            engine.setBoard(position);
            engine.sendCommand(CommandData(EngineCommand::COMPUTE_AI_MOVE));

            // wait for the AI_MOVE response, servicing isready/stop/quit meanwhile
            ResponseData resp;
            bool gotMove = false;
            while (!gotMove)
            {
                ResponseData r;
                while (engine.getResponse(r))
                {
                    if (r.response == EngineResponse::AI_MOVE)
                    {
                        resp = r;
                        gotMove = true;
                    }
                }
                if (gotMove)
                    break;

                // only consume isready/stop/quit mid-search; anything else stays
                // queued in order and is processed after bestmove (batch input)
                std::string pending;
                if (lines.peek(pending))
                {
                    std::istringstream piss(pending);
                    std::string pcmd;
                    piss >> pcmd;
                    if (pcmd == "isready" || pcmd == "stop" || pcmd == "quit")
                    {
                        lines.dropFront();
                        handleAnytime(pcmd, true);
                    }
                }
                else if (lines.eof)
                {
                    // stdin closed with nothing queued: let the current search
                    // finish (piped batch input hits EOF instantly), then exit
                    quit = true;
                }

                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }

            Move best = resp.move;
            bool valid = resp.success && best.getFromRow() >= 0;

            if (!valid)
            {
                // search produced nothing (stopped before iteration 1, or no legal
                // moves). fall back to any legal move rather than forfeiting.
                Board scratch = position;
                Move fallback;
                if (anyLegalMove(scratch, fallback))
                {
                    best = fallback;
                    valid = true;
                    send("info string search returned no move, playing fallback");
                }
            }

            if (valid)
            {
                if (resp.eval != 69420) // engine's "no complete iteration" sentinel
                {
                    std::string info = "info score cp " + std::to_string(resp.eval);
                    if (!resp.line.empty())
                    {
                        info += " pv";
                        for (const Move& m : resp.line)
                            info += " " + moveToUci(m);
                    }
                    send(info);
                }
                send("bestmove " + moveToUci(best));
            }
            else
            {
                send("bestmove 0000"); // mate/stalemate: no legal move exists
            }
        }
        else
        {
            // unknown commands are ignored per the UCI convention (setoption, ponderhit, ...)
        }
    }

    engine.stop();

    // unblock the reader thread if stdin is still open (it usually exited with us)
    if (reader.joinable())
    {
        if (!lines.eof)
            reader.detach(); // getline is blocking on stdin; process exit cleans it up
        else
            reader.join();
    }

    return 0;
}
