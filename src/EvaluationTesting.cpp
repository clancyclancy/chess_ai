#include <iostream>
#include <string>
#include <Board.h>
#include "ChessEngine.h"
#include <chrono>   // timing

     
template <typename Func>   
auto timeFunction(const std::string& label, Func&& func)      
{                           
    auto start = std::chrono::high_resolution_clock::now();           
    auto result = func();  
    auto end   = std::chrono::high_resolution_clock::now();                              
  
    auto us = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();

    std::cout << label << ": " << result  
              << "   (" << us << " us)\n"; 
  
    return result;     
}  
    
int main() {

    std::cout << "Eval test start\n" << std::flush;; 

    std::cout << "Enter FEN:\n" << std::flush;;

    std::string fen;
    std::getline(std::cin, fen);

    if (fen.empty()) {
        std::cout << "No FEN provided.\n";
        return 1;
    }

    Board board;
    if (!board.loadFromFEN(fen)) {
        std::cout << "Failed to load FEN.\n";
        return 1;
    }

    auto engine = std::make_unique<ChessEngine>();
    //engine->start();

    engine->setBoard(board);


    int pieceCount = 0;

    for (int r = 0; r < 8; r++)
    {
        for (int c = 0; c < 8; c++)
        {
            const Piece& p = board.getPieceConst(r, c);
            if (p.isEmpty()) continue;

            switch (p.getType())
            {
                case PieceType::KNIGHT: pieceCount += 3; break;
                case PieceType::BISHOP: pieceCount += 3; break;
                case PieceType::ROOK:   pieceCount += 5; break;
                case PieceType::QUEEN:  pieceCount += 9; break;
                default: break;
            }
        }
    }
    PieceColor sideToMove = board.getCurrentTurn();


    std::cout << "=============================\n";

    timeFunction("Raw material", [&](){
        return engine->rawPieceTotal(sideToMove);
    });

    timeFunction("Piece-square tables", [&](){
        return engine->evaluatePieceSquareTables(sideToMove, pieceCount);
    });

    timeFunction("Pawn structure", [&](){
        return engine->evaluatePawnStructure(sideToMove);
    });

    timeFunction("Piece mobility", [&](){
        return engine->evaluatePieceMobility(sideToMove, pieceCount);
    });

    timeFunction("King safety", [&](){
        return engine->evaluateKingSafety(sideToMove, pieceCount);
    });

    timeFunction("King restriction", [&](){
        return engine->evaluateKingRestriction(sideToMove, pieceCount);
    });

    timeFunction("King to king distance", [&](){
        return engine->evaluateKingToKingDistance(sideToMove, pieceCount);
    });

    std::cout << "=============================\n";

    timeFunction("Total evaluateBoard()", [&](){
        return engine->evaluateBoard();
    });

    std::cout << "=============================\n";

    return 0;
}
