#ifndef BOARD_H
#define BOARD_H

#include "Piece.h"
#include "Move.h"
#include <vector>
#include <memory>
#include <mutex>

class Board {
private:
    // 0,0 is top-left from white's perspective
    Piece board[8][8];

    mutable std::mutex boardMutex; // mutable for const methods TODO: I don't like this but I like my getters being const methods

    
    // Game state
    PieceColor currentTurn;
    int moveCount;
    int halfMoveClock;  // 50-move rule
    
    // Caslting rights
    bool whiteCanCastleKingside;
    bool whiteCanCastleQueenside;
    bool blackCanCastleKingside;
    bool blackCanCastleQueenside;
    
    int enPassantTargetRow;  // default -1 if no en passant possible
    int enPassantTargetCol;

    int whiteKingRow;
    int whiteKingCol;
    int blackKingRow;
    int blackKingCol;

    struct PinInfo 
    {
        bool isPinned;
        int pinDirectionRow;
        int pinDirectionCol;
        int attackerRow;
        int attackerCol;
    };

public:
    // Constructor
    Board();

    //Copy constructor for copying board state but not boardMutex
    Board(const Board& other) 
    {
        
        // Lock
        //std::lock_guard<std::mutex> lock(other.boardMutex);

        // Copy piece layout
        for (int r = 0; r < 8; ++r) 
        {
            for (int c = 0; c < 8; ++c) 
            {
                board[r][c] = other.board[r][c];
            }
        }

        // Copy game state
        currentTurn             = other.currentTurn;
        moveCount               = other.moveCount;
        halfMoveClock           = other.halfMoveClock;
        whiteCanCastleKingside  = other.whiteCanCastleKingside;
        whiteCanCastleQueenside = other.whiteCanCastleQueenside;
        blackCanCastleKingside  = other.blackCanCastleKingside;
        blackCanCastleQueenside = other.blackCanCastleQueenside;
        enPassantTargetRow      = other.enPassantTargetRow;
        enPassantTargetCol      = other.enPassantTargetCol;
        whiteKingRow            = other.whiteKingRow;
        whiteKingCol            = other.whiteKingCol;
        blackKingRow            = other.blackKingRow;
        blackKingCol            = other.blackKingCol;
        
    }

    // Assignment operator
    Board& operator=(const Board& other) 
    {
        if (this == &other)
            return *this;

        // lock 
        //std::lock_guard<std::mutex> lock(other.boardMutex);

        for (int r = 0; r < 8; ++r) 
        {
            for (int c = 0; c < 8; ++c) 
            {
                board[r][c] = other.board[r][c];
            }
        }

        currentTurn             = other.currentTurn;
        moveCount               = other.moveCount;
        halfMoveClock           = other.halfMoveClock;
        whiteCanCastleKingside  = other.whiteCanCastleKingside;
        whiteCanCastleQueenside = other.whiteCanCastleQueenside;
        blackCanCastleKingside  = other.blackCanCastleKingside;
        blackCanCastleQueenside = other.blackCanCastleQueenside;
        enPassantTargetRow      = other.enPassantTargetRow;
        enPassantTargetCol      = other.enPassantTargetCol;
        whiteKingRow            = other.whiteKingRow;
        whiteKingCol            = other.whiteKingCol;
        blackKingRow            = other.blackKingRow;
        blackKingCol            = other.blackKingCol;        

        return *this;
    }

    Piece* operator[](int row) 
    {
        return board[row];
    }

    const Piece* operator[](int row) const 
    {
        return board[row];
    }    


    struct UndoInfo 
    {
        
        Piece captured;                 
        int capturedRow = -1;           // for en passant , captured pawn row
        int capturedCol = -1;

        // Rook movement for castling
        bool didCastle = false;
        int rookFromRow = -1;
        int rookFromCol = -1;
        int rookToRow = -1;
        int rookToCol = -1;
        Piece rookOriginal;

        // Game state
        bool whiteCanCastleKingside;
        bool whiteCanCastleQueenside;
        bool blackCanCastleKingside;
        bool blackCanCastleQueenside;

        int enPassantTargetRow;
        int enPassantTargetCol;

        PieceColor previousTurn;
        int previousHalfMoveClock;
        int previousMoveCount;

        // Move-type bookkeeping
        bool wasPawnMove  = false;
        bool wasCapture   = false;
        bool wasPromotion = false;       
        
        // King position tracking 
        int previousWhiteKingRow;
        int previousWhiteKingCol;
        int previousBlackKingRow;
        int previousBlackKingCol;

        // Default constructor
        UndoInfo()
            : captured(),
            capturedRow(-1),
            capturedCol(-1),
            didCastle(false),
            rookFromRow(-1),
            rookFromCol(-1),
            rookToRow(-1),
            rookToCol(-1),
            rookOriginal(),
            whiteCanCastleKingside(false),
            whiteCanCastleQueenside(false),
            blackCanCastleKingside(false),
            blackCanCastleQueenside(false),
            enPassantTargetRow(-1),
            enPassantTargetCol(-1),
            previousTurn(PieceColor::WHITE),
            previousHalfMoveClock(0),
            previousMoveCount(0),
            wasPawnMove(false),
            wasCapture(false),
            wasPromotion(false),
            previousWhiteKingRow(-1),
            previousWhiteKingCol(-1),
            previousBlackKingRow(-1),
            previousBlackKingCol(-1) 
            {                
            }  
    };

    void resetBoard();

    int getFiftyMoveClock() const { return halfMoveClock; }
    
    bool loadFromFEN(const std::string& fen);

    inline const Piece& getPieceConst(int row, int col) const
    {
        return board[row][col];
    }

    bool isValidPosition(int row, int col) const;

    bool makeMove(const Move& move, bool switchTurn = true);

    void makeUncheckedMove(const Move& move, UndoInfo& undo, bool switchTurn = true);

    void undoMove(const Move& move, const UndoInfo& undo);

    std::vector<Move> getLegalMoves(int row, int col);

    // slowly losing my mind :)
    std::vector<Move> getLegalMoves_DEBUG(int row, int col);


    // bool isInCheck() const;

    inline bool isInCheck() const 
    {
        int kingRow = (currentTurn == PieceColor::WHITE) ? whiteKingRow : blackKingRow;
        int kingCol = (currentTurn == PieceColor::WHITE) ? whiteKingCol : blackKingCol;
        return isSquareUnderAttack(kingRow, kingCol, (currentTurn == PieceColor::WHITE) ? PieceColor::BLACK : PieceColor::WHITE);
    }

    inline bool isInCheck(PieceColor color) const 
    {
        int kingRow = (color == PieceColor::WHITE) ? whiteKingRow : blackKingRow;
        int kingCol = (color == PieceColor::WHITE) ? whiteKingCol : blackKingCol;
        return isSquareUnderAttack(kingRow, kingCol, (color == PieceColor::WHITE) ? PieceColor::BLACK : PieceColor::WHITE);
    }

    bool isCheckmate();

    bool isStalemate();

    bool isLegalMoveAvailable();

    bool isSufficientMaterial() const;

    PieceColor getCurrentTurn() const { return currentTurn; }

    std::vector<Move> generatePseudoLegalMoves(int row, int col) const;

    std::vector<Move> generateAllPseudoLegalMoves() const;

    std::vector<Move> generateQSearchMoves() const;

    int getWhiteKingRow() const { return whiteKingRow; }
    int getWhiteKingCol() const { return whiteKingCol; }
    int getBlackKingRow() const { return blackKingRow; }
    int getBlackKingCol() const { return blackKingCol; }

    //losing it
    std::string toFEN() const;    

    void makeNullMove(UndoInfo& undo);

    void undoNullMove(const UndoInfo& undo);    

    bool isSquareUnderAttack(int row, int col, PieceColor attackingColor) const;


private:

    void setupInitialPosition();


    void appendCastlingMoves(std::vector<Move>& moves, PieceColor color) const;

        // slowly losing my mind :)
    void removeIllegalMoves_slowButCorrect(std::vector<Move>* moves);

    void removeIllegalMoves(std::vector<Move>* moves);


    // void findKing(PieceColor color, int* outRow, int* outCol) const;

    void computePins(PieceColor color, PinInfo pinInfo[8][8]) const;


};

#endif
