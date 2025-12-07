
#ifndef PIECE_H
#define PIECE_H

#include <string>

enum class PieceType 
{
    PAWN,
    KNIGHT,
    BISHOP,
    ROOK,
    QUEEN,
    KING,
    EMPTY
};

static const int pieceTypeToIndex[] = 
{
    0, // PAWN  
    1, // KNIGHT   
    2, // BISHOP 
    3, // ROOK 
    4, // QUEEN 
    5, // KING  
   -1  // EMPTY 
}; 


enum class PieceColor 
{
    NONE,
    WHITE,
    BLACK
};

class Piece 

{
private:
    PieceType type;
    PieceColor color;
    int row;
    int col;
    bool hasMoved;

public:
 
    Piece();

    Piece(PieceType type, PieceColor color, int row, int col);

    // Getters
    PieceType  getType()     const { return type;     }  
    PieceColor getColor()    const { return color;    } 
    int        getRow()      const { return row;      }  
    int        getCol()      const { return col;      } 
    bool       getHasMoved() const { return hasMoved; }

    // Setters
    void setPosition(int newRow, int newCol);
    void setHasMoved(bool moved) { hasMoved = moved; } 


    // changed isEmpty() to inline. Visual Studio Profiler showed 2.3% self CPU just on isEmpty() calls.
    // inline is now a single CPU instruction 
    inline bool isEmpty() const noexcept 
    {
        return type == PieceType::EMPTY;
    }


    std::string toString() const;
};

#endif
