#include "InputHandler.h"

InputHandler::InputHandler(int boardOffsetX, int boardOffsetY, int squareSize): 
                            boardOffsetX(boardOffsetX), 
                            boardOffsetY(boardOffsetY), 
                            squareSize(squareSize),
                            squareSelected(false), 
                            selectedRow(-1), 
                            selectedCol(-1) 
                            {
                            }

bool InputHandler::handleMouseClick(int mouseX, int mouseY, Move& outMove) 
{
    int clickedRow, clickedCol;
    bool insideBoard = false;

    insideBoard = screenToBoard(mouseX, mouseY, &clickedRow, &clickedCol);
    
    if (!insideBoard) 
    {
        return false;  
    }
    
    if (!squareSelected) 
    {
        // First click - select a piece
        selectedRow = clickedRow;
        selectedCol = clickedCol;
        squareSelected = true;
        return false;  // No move yet
    } 
    else 
    {
        // Second click - attempt to move
        outMove = Move(selectedRow, selectedCol, clickedRow, clickedCol);
        
        // Clear selection
        squareSelected = false;
        selectedRow = -1;
        selectedCol = -1;
        currentLegalMoves.clear();
        
        return true;  // schmove was made
    }
}
 
bool InputHandler::screenToBoard(int mouseX, int mouseY, int* outRow, int*outCol) const 
{
    // Calculate which square was clicked
    int relativeX = mouseX - boardOffsetX;
    int relativeY = mouseY - boardOffsetY;
    
    // Check if within board bounds
    if (relativeX < 0 || relativeX >= squareSize * 8 ||
        relativeY < 0 || relativeY >= squareSize * 8) 
    {
        return false;
    }
    
    *outCol = relativeX / squareSize;
    *outRow = relativeY / squareSize;

    if (isBoardFlipped) 
    {
        *outCol = 7 - *outCol;
        *outRow = 7 - *outRow;
    }
    
    return true;
}

void InputHandler::setLegalMoves(const std::vector<Move>& moves) 
{
    currentLegalMoves = moves;
}

void InputHandler::getSelectedPosition(int& outRow, int& outCol) const 
{
    outRow = selectedRow;
    outCol = selectedCol;
}

void InputHandler::clearSelection() 
{
    squareSelected = false;
    selectedRow = -1;
    selectedCol = -1;
    currentLegalMoves.clear();
}