#include <iostream>
#include <string>
#include <chrono>
#include <vector>
#include "Board.h"
#include "ChessEngine.h"

using namespace std;

ChessEngine engine;
 
struct PerftStats {
    uint64_t nodes      = 0;
    uint64_t captures   = 0;
    uint64_t enPassant  = 0;
    uint64_t castles    = 0;
    uint64_t promotions = 0;
    uint64_t checks     = 0;
    uint64_t checkmates = 0;
};
 

void perft(Board& board, int depth, PerftStats& stats)
{
    if (depth == 0) {
        stats.nodes++;
        return;
    }

    PieceColor side = board.getCurrentTurn();

    for (int row = 0; row < 8; row++)
    {
        for (int col = 0; col < 8; col++)
        {
            const Piece& piece = board.getPieceConst(row, col);
            if (piece.isEmpty() || piece.getColor() != side)
                continue;

            vector<Move> moves = board.getLegalMoves(row, col);

                     
            for (const Move& move : moves)
            {
                Board::UndoInfo undo;
                board.makeUncheckedMove(move, undo);

                if (depth == 1)
                {
                    stats.nodes++;

                    if (undo.wasCapture)
                        stats.captures++;

                    if (undo.captured.getType() == PieceType::PAWN &&
                        undo.wasPawnMove == true &&
                        undo.captured.getRow() != move.getToRow())
                    {
                        stats.enPassant++;
                    }

                    if (undo.didCastle)
                        stats.castles++;

                    if (undo.wasPromotion)
                        stats.promotions++;

                    if (board.isInCheck())
                        stats.checks++;

                    if (board.isCheckmate())
                        stats.checkmates++;
                }
                else {
                    perft(board, depth - 1, stats);
                }

                board.undoMove(move, undo);
            }
        }
    }
}
 
int main()
{
    Board board;

    cout << "Enter FEN: ";
    string fen;
    getline(cin, fen);

    if (!board.loadFromFEN(fen))
    {
        cout << "Invalid FEN\n";
        return 1;
    }

    cout << "\nFEN loaded successfully.\n";
    cout << "Current turn: "
         << (board.getCurrentTurn() == PieceColor::WHITE ? "White" : "Black")
         << "\n\n";

 
    cout << "\nDepth\tNodes\tCaptures\tE.p.\tCastles\tPromotions\tChecks\tCheckmates\n";

    for (int depth = 1; depth <= 6; depth++)
    {
        PerftStats stats;

        auto start = chrono::high_resolution_clock::now();
        perft(board, depth, stats);
        auto end = chrono::high_resolution_clock::now();

        double ms = chrono::duration<double, milli>(end - start).count();

        cout << depth << "\t"
                << stats.nodes << "\t"
                << stats.captures << "\t\t"
                << stats.enPassant << "\t"
                << stats.castles << "\t"
                << stats.promotions << "\t\t"
                << stats.checks << "\t"
                << stats.checkmates
                << "   (" << ms << " ms)\n";
    }
  

    return 0;
}
