#include "Piece.h"

Piece::Piece() : 
                type(PieceType::EMPTY), 
                color(PieceColor::NONE), 
                row(0), 
                col(0), 
                hasMoved(false) 
                {
                }

Piece::Piece(PieceType type, PieceColor color, int row, int col): 
                                                                type(type), 
                                                                color(color), 
                                                                row(row), 
                                                                col(col), 
                                                                hasMoved(false) 
                                                                {                                                                
                                                                }


void Piece::setPosition(int newRow, int newCol) 
{
    row = newRow;
    col = newCol;
    hasMoved = true;
}

std::string Piece::toString() const 
{
    if (isEmpty()) return "  ";
    
    std::string result;
    
    result += (color == PieceColor::WHITE) ? "W" : "B";
    
    switch (type) 
    {
        case PieceType::PAWN:   result += "P"; break;
        case PieceType::KNIGHT: result += "N"; break;
        case PieceType::BISHOP: result += "B"; break;
        case PieceType::ROOK:   result += "R"; break;
        case PieceType::QUEEN:  result += "Q"; break;
        case PieceType::KING:   result += "K"; break;
        default: result += "?"; break;
    }
    
    return result;
}
