
#include "Move.h"
#include "Board.h"

void Move::setPromotion(PieceType type) 
{
    isPromotion = true;
    promotionType = type;
}

std::string Move::toString() const 
{
    std::string result;
    if (fromCol < 0 || fromCol > 7 || fromRow < 0 || fromRow > 7 ||
       toCol < 0 || toCol > 7 || toRow < 0 || toRow > 7)   
    {  
        return " ";   
    }   
    
    result += static_cast<char>('a' + fromCol);
    result += static_cast<char>('1' + (7 - fromRow)); 
    
    result += static_cast<char>('a' + toCol);
    result += static_cast<char>('1' + (7 - toRow));
    
    if (isPromotion) {
        switch (promotionType) {
            case PieceType::QUEEN:  result += "Q"; break;
            case PieceType::ROOK:   result += "R"; break;
            case PieceType::BISHOP: result += "B"; break;
            case PieceType::KNIGHT: result += "N"; break;
            default: break;
        }
    }
    
    return result;
}

bool Move::isPawnPromotion(const Board& board) const 
{  
    
    const Piece& movingPiece = board.getPieceConst(fromRow, fromCol);
    if (movingPiece.getType() != PieceType::PAWN) 
    {
        return false;
    }
    
    int promotionRow = (movingPiece.getColor() == PieceColor::WHITE) ? 0 : 7;
    return toRow == promotionRow;
}
