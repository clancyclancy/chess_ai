
#ifndef MOVE_H
#define MOVE_H

#include "Piece.h"

class Board; // Forward declaration, dont need to know what Board is here
 
class Move 
{
private:
    int fromRow;
    int fromCol;
    int toRow;
    int toCol;
    
    PieceType capturedPieceType;
    
 
    // Special move flags
    bool isCastling;
    bool isEnPassant;   
    bool isPromotion;  
    PieceType promotionType;

public:    

    Move(int fromRow, int fromCol, int toRow, int toCol) :
        fromRow(fromRow),  
        fromCol(fromCol),
        toRow(toRow),
        toCol(toCol),
        capturedPieceType(PieceType::EMPTY),
        isCastling(false),
        isEnPassant(false),
        isPromotion(false),
        promotionType(PieceType::EMPTY)
    {

    }

    Move() :
        fromRow(0),
        fromCol(0),
        toRow(0),
        toCol(0),
        capturedPieceType(PieceType::EMPTY),
        isCastling(false),
        isEnPassant(false),
        isPromotion(false),
        promotionType(PieceType::EMPTY)
    {        
    }


    // custom == and != ops
    // TODO: i was doing manual comparisons somewhere, find and replace with these
    bool operator==(const Move& other) const  
    {
        return fromRow == other.fromRow &&  
            fromCol == other.fromCol &&
            toRow   == other.toRow &&
            toCol   == other.toCol &&
            isPromotion == other.isPromotion &&
            promotionType == other.promotionType;
    }

    bool operator!=(const Move& other) const   
    {
        return !(*this == other);   
    }

 
    // Getters
    int getFromRow() const { return fromRow; }
    int getFromCol() const { return fromCol; }
    int getToRow() const { return toRow; }
    int getToCol() const { return toCol; }

    bool getIsPromotion() const { return isPromotion; }
    PieceType getPromotionType() const { return promotionType; }

    void setPromotion(PieceType type);
    void setIsEnPassant(bool val) { isEnPassant = val; }
    void setIsCastling(bool val) { isCastling = val; }
    //void setCapturedPieceType(PieceType type) { capturedPieceType = type; }

    bool isPawnPromotion(const Board& board) const;
    bool isEnPassantMove() const { return isEnPassant; }
    bool isCastlingMove() const { return isCastling; }
    //PieceType getCapturedPieceType() const { return capturedPieceType; }

    std::string toString() const;   
};

#endif 
