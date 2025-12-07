
#ifndef INPUT_HANDLER_H
#define INPUT_HANDLER_H

#include "Move.h"
#include <SDL2/SDL.h>
#include <vector>


class InputHandler {
private:
    int boardOffsetX;
    int boardOffsetY;
    int squareSize;
    
    bool squareSelected;
    int  selectedRow;
    int  selectedCol;

    bool isBoardFlipped = false; // she flip on my board
    
    std::vector<Move> currentLegalMoves;

public:

    InputHandler(int boardOffsetX, int boardOffsetY, int squareSize);

    void setFlipped(bool f) { isBoardFlipped = f; }

    bool handleMouseClick(int mouseX, int mouseY, Move& outMove);

    bool screenToBoard(int mouseX, int mouseY, int* outRow, int* outCol) const;

    void setLegalMoves(const std::vector<Move>& moves);

    bool isPieceSelected() const { return squareSelected; }

    void getSelectedPosition(int& outRow, int& outCol) const;

    void clearSelection();

    const std::vector<Move>& getLegalMoves() const { return currentLegalMoves; }
};

#endif
