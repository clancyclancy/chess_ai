
#include "Board.h"
#include <iostream> // only for printing debug info
#include <sstream>

Board::Board() : 
            currentTurn(PieceColor::WHITE), 
            moveCount(0), 
            halfMoveClock(0),
            whiteCanCastleKingside(true), 
            whiteCanCastleQueenside(true),
            blackCanCastleKingside(true), 
            blackCanCastleQueenside(true),
            enPassantTargetRow(-1),
            enPassantTargetCol(-1),
            whiteKingRow(-1),
            whiteKingCol(-1),
            blackKingRow(-1),
            blackKingCol(-1)
            {
                setupInitialPosition();
            }

void Board::resetBoard() 
{
    currentTurn             = PieceColor::WHITE;
    moveCount               = 0;
    halfMoveClock           = 0;
    whiteCanCastleKingside  = true;
    whiteCanCastleQueenside = true;
    blackCanCastleKingside  = true;
    blackCanCastleQueenside = true;
    enPassantTargetRow      = -1;
    enPassantTargetCol      = -1;
    
    setupInitialPosition();
}

void Board::setupInitialPosition() 
{
    //TODO: FEN string interpreter

    // Clear board
    for (int row = 0; row < 8; row++) 
    {
        for (int col = 0; col < 8; col++) 
        {
            board[row][col] = Piece();
        }
    }

    loadFromFEN("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
    //loadFromFEN("8/8/5K2/2p5/1p2k2B/P7/8/8 w - - 0 1"); // engine doesnt see deep enough to find a3b4 is drawing and everything else is losing
    
    //loadFromFEN("8/8/8/4k3/8/8/R7/3K4 w - - 0 1");    // test endgame: KR vs k
    //loadFromFEN("8/8/8/1K6/4k3/4p3/R7/8 w - - 0 1");  // test endgame: KR vs kp

}

bool Board::loadFromFEN(const std::string& fen)
{
    std::istringstream ss(fen);
    std::string boardPart, turnPart, castlingPart, epPart;
    int halfmove, fullmove;

    ss >> boardPart >> turnPart >> castlingPart >> epPart >> halfmove >> fullmove;

    for (int r = 0; r < 8; r++)
        for (int c = 0; c < 8; c++)
            board[r][c] = Piece();

    int row = 0, col = 0;
    for (char ch : boardPart)
    {
        if (ch == '/')
        {
            row++;
            col = 0;
            continue;
        }

        if (std::isdigit(ch))
        {
            col += ch - '0';
            continue;
        }

        PieceColor color = std::isupper(ch) ? PieceColor::WHITE : PieceColor::BLACK;
        PieceType type;

        switch (std::tolower(ch))
        {
            case 'p': type = PieceType::PAWN; break;
            case 'n': type = PieceType::KNIGHT; break;
            case 'b': type = PieceType::BISHOP; break;
            case 'r': type = PieceType::ROOK; break;
            case 'q': type = PieceType::QUEEN; break;
            case 'k': type = PieceType::KING; break;
            default: return false;
        }

        board[row][col] = Piece(type, color, row, col);

        if (type == PieceType::KING)
        {
            if (color == PieceColor::WHITE)
            {
                whiteKingRow = row;
                whiteKingCol = col;
            }
            else
            {
                blackKingRow = row;
                blackKingCol = col;
            }
        }

        col++;
    }

    currentTurn = (turnPart == "w") ? PieceColor::WHITE : PieceColor::BLACK;

    whiteCanCastleKingside  = castlingPart.find('K') != std::string::npos;
    whiteCanCastleQueenside = castlingPart.find('Q') != std::string::npos;
    blackCanCastleKingside  = castlingPart.find('k') != std::string::npos;
    blackCanCastleQueenside = castlingPart.find('q') != std::string::npos;

    if (epPart != "-")
    {
        int file = epPart[0] - 'a';
        int rank = 8 - (epPart[1] - '0');
        enPassantTargetRow = rank;
        enPassantTargetCol = file;
    }  
    else
    {  
        enPassantTargetRow = -1; 
        enPassantTargetCol = -1;
    }


    halfMoveClock = halfmove;
    moveCount = fullmove;

    return true;
}
// const Piece& Board::getPieceConst(int row, int col) const 
// {
//     return board[row][col];
// }

bool Board::isValidPosition(int row, int col) const 
{
    return row >= 0 && row < 8 && col >= 0 && col < 8;
}

bool Board::makeMove(const Move& move, bool switchTurn) 
{

    // // print debug info about promotion
    // if (move.getIsPromotion() )
    // {
    //     std::cout << "makeMove received promotion move: "
    //             << move.getFromRow() << "," << move.getFromCol()
    //             << " -> " << move.getToRow() << "," << move.getToCol()
    //             << " to " << (int)move.getPromotionType() << "\n";
    // }


    int fromRow = move.getFromRow();
    int fromCol = move.getFromCol();

    int toRow = move.getToRow();
    int toCol = move.getToCol();
    
    // Prescreen positions
    if (!isValidPosition(fromRow, fromCol) || !isValidPosition(toRow, toCol)) 
    {
        std::cout << "ERROR: makeUncheckedMove received invalid move: "
                << fromRow << "," << fromCol
                << " -> " << toRow << "," << toCol << std::endl;        
        return false;
    }
    
    Piece& movingPiece = board[fromRow][fromCol];
    


    // Determine if the move is a promotion move
    bool isPromotionMove = false;
    if (movingPiece.getType() == PieceType::PAWN) 
    {
        int promotionRow = (movingPiece.getColor() == PieceColor::WHITE) ? 0 : 7;
        if (toRow == promotionRow) 
        {
            isPromotionMove = true;
        }
    }
    
    // Generate legal moves
    std::vector<Move> legalMoves = getLegalMoves(fromRow, fromCol);

    // Check if the requested move is in the legal list
    auto it = std::find_if(legalMoves.begin(), legalMoves.end(),
        [&](const Move& m) {
            return m.getFromRow() == fromRow &&
                   m.getFromCol() == fromCol &&
                   m.getToRow()   == toRow &&
                   m.getToCol()   == toCol &&
                   m.getIsPromotion() == isPromotionMove;
        });

    if (it == legalMoves.end()) {
        return false; 
    }

    UndoInfo undo;

    Board::makeUncheckedMove(move, undo, true);

    return true;
}


void Board::makeUncheckedMove(const Move& move, UndoInfo& undo, bool switchTurn) {
    

    
    const int fromRow = move.getFromRow();
    const int fromCol = move.getFromCol();
    const int toRow   = move.getToRow();
    const int toCol   = move.getToCol();


    // Save global state
    undo.whiteCanCastleKingside  = whiteCanCastleKingside;
    undo.whiteCanCastleQueenside = whiteCanCastleQueenside;
    undo.blackCanCastleKingside  = blackCanCastleKingside;
    undo.blackCanCastleQueenside = blackCanCastleQueenside;
    undo.enPassantTargetRow      = enPassantTargetRow;
    undo.enPassantTargetCol      = enPassantTargetCol;
    undo.previousTurn            = currentTurn;
    undo.previousHalfMoveClock   = halfMoveClock;
    undo.previousMoveCount       = moveCount;

    Piece& moving = board[fromRow][fromCol];
    Piece& target = board[toRow][toCol];
    
    
    undo.wasCapture = !target.isEmpty();
    undo.wasPawnMove = (moving.getType() == PieceType::PAWN);
    undo.wasPromotion = move.getIsPromotion();

    // TODO: might be unncessary?
    undo.previousWhiteKingRow = whiteKingRow;
    undo.previousWhiteKingCol = whiteKingCol;
    undo.previousBlackKingRow = blackKingRow;
    undo.previousBlackKingCol = blackKingCol;

    // if (move.getIsPromotion()) {
    //     std::cout << "Promotion move: " << fromRow << "," << fromCol
    //             << " -> " << toRow << "," << toCol
    //             << " to " << (int)move.getPromotionType() << "\n";
    // }

    // Handle en passant capture
    bool enPassantCapture = false;

    if (moving.getType() == PieceType::PAWN &&
        fromCol != toCol &&
        target.isEmpty() &&
        toRow == enPassantTargetRow &&
        toCol == enPassantTargetCol)    
    {
        // Diagonal pawn move to empty square → en passant
        enPassantCapture = true;
        

        int epRow = (moving.getColor() == PieceColor::WHITE)
            ? toRow + 1   // white captures a pawn BELOW the target square
            : toRow - 1;  // black captures a pawn ABOVE the target square

        undo.capturedRow = epRow;
        undo.capturedCol = toCol;
        undo.captured = board[epRow][toCol];
        board[epRow][toCol] = Piece(); // remove the captured pawn

        undo.wasCapture = true;
    } 
    else if (!enPassantCapture)
    {
        // Normal capture or move to empty square
        undo.captured  = target;                 // empty Piece() if none
        undo.wasCapture = !target.isEmpty();
    }

    // Clear en passant by default; may be set below
    enPassantTargetRow = -1;
    enPassantTargetCol = -1;

    // Handle castling: king moves two squares horizontally
    undo.didCastle = false;
    if (moving.getType() == PieceType::KING && std::abs(toCol - fromCol) == 2) {
        undo.didCastle = true;
        int rookRow = fromRow;
        if (toCol == 6) 
        { 
            // kingside
            undo.rookFromRow = rookRow; undo.rookFromCol = 7;
            undo.rookToRow   = rookRow; undo.rookToCol   = 5;
        } else 
        {          
            // queenside
            undo.rookFromRow = rookRow; undo.rookFromCol = 0;
            undo.rookToRow   = rookRow; undo.rookToCol   = 3;
        }
        undo.rookOriginal = board[undo.rookFromRow][undo.rookFromCol];
        board[undo.rookToRow][undo.rookToCol] = undo.rookOriginal;
        board[undo.rookToRow][undo.rookToCol].setPosition(undo.rookToRow, undo.rookToCol);
        board[undo.rookFromRow][undo.rookFromCol] = Piece();
    }

    // // Promotion
    // if (move.getIsPromotion()) {
    //     board[toRow][toCol] = Piece(move.getPromotionType(), moving.getColor(), toRow, toCol);
    // }

    // Update castling rights on king/rook moves or rook capture
    auto revokeCastlingForColor = [&](PieceColor color, bool kingMoved, int rookFromRow, int rookFromCol) 
    {
        if (color == PieceColor::WHITE) 
        {
            if (kingMoved) 
            {
                whiteCanCastleKingside = false;
                whiteCanCastleQueenside = false;
            }
            if (rookFromRow == 7 && rookFromCol == 0) whiteCanCastleQueenside = false;
            if (rookFromRow == 7 && rookFromCol == 7) whiteCanCastleKingside = false;
        } 
        else 
        {
            if (kingMoved) 
            {
                blackCanCastleKingside = false;
                blackCanCastleQueenside = false;
            }
            if (rookFromRow == 0 && rookFromCol == 0) blackCanCastleQueenside = false;
            if (rookFromRow == 0 && rookFromCol == 7) blackCanCastleKingside = false;
        }
    };

    // King moved
    if (moving.getType() == PieceType::KING) 
    {

        // Handle king position tracking
        if (moving.getColor() == PieceColor::WHITE) 
        {
            whiteKingRow = toRow;
            whiteKingCol = toCol;
        }
        else 
        {
            blackKingRow = toRow;
            blackKingCol = toCol;
        }

        // Handle caslting rights 
        revokeCastlingForColor(moving.getColor(), true, -1, -1);
    }
    // Rook moved from its original square
    if (moving.getType() == PieceType::ROOK) {
        revokeCastlingForColor(moving.getColor(), false, fromRow, fromCol);
    }
    // Rook captured on its original square
    if (!undo.captured.isEmpty() && undo.captured.getType() == PieceType::ROOK) 
    {
        revokeCastlingForColor(undo.captured.getColor(), false, toRow, toCol);
    }

    // Set en passant target on double pawn push

    if (moving.getType() == PieceType::PAWN && std::abs(toRow - fromRow) == 2) 
    {
        enPassantTargetRow = (fromRow + toRow) / 2;
        enPassantTargetCol = fromCol;
       // std::cout << "En passant target set at (" << enPassantTargetRow << ", " << enPassantTargetCol << ")\n";
    }

    // Half-move clock
    if (undo.wasPawnMove || undo.wasCapture) 
    {
        halfMoveClock = 0;
    } 
    else 
    {
        ++halfMoveClock;
    }

    // Move count and turn
    if (switchTurn) 
    {
        currentTurn = (currentTurn == PieceColor::WHITE) ? PieceColor::BLACK : PieceColor::WHITE;
    }

    // Move the piece or handle promotion
    if (move.getIsPromotion() && moving.getType() == PieceType::PAWN) 
    {
        // Place promoted piece on target square
        board[toRow][toCol] = Piece(move.getPromotionType(), moving.getColor(), toRow, toCol);
        board[toRow][toCol].setHasMoved(true);
    } 
    else 
    {
        // Normal move
        board[toRow][toCol] = moving;
        board[toRow][toCol].setPosition(toRow, toCol);
    }

    board[fromRow][fromCol] = Piece(); // clear origin

    ++moveCount;
}

void Board::undoMove(const Move& move, const UndoInfo& undo)
{
    const int fromRow = move.getFromRow();
    const int fromCol = move.getFromCol();
    const int toRow   = move.getToRow();
    const int toCol   = move.getToCol();

    // Undo promotion: restore mover as a pawn (or original) is implicit when moving back
    Piece& moved = board[toRow][toCol];

    // If this was a promotion, restore a pawn on the from-square
    if (undo.wasPromotion && moved.getType() != PieceType::PAWN) 
    {
        PieceColor color = moved.getColor();
        board[fromRow][fromCol] = Piece(PieceType::PAWN, color, fromRow, fromCol);
    } 
    else 
    {
        // Move back
        board[fromRow][fromCol] = moved;
        board[fromRow][fromCol].setPosition(fromRow, fromCol);
    }

    // If this was an en passant capture, the target square should be empty,
    // and the captured pawn belongs on (capturedRow, capturedCol).
    if (undo.capturedRow != -1 && undo.capturedCol != -1) {
        // Clear the square the capturing pawn moved to
        board[toRow][toCol] = Piece();
        // Restore the captured pawn on its original square
        board[undo.capturedRow][undo.capturedCol] = undo.captured;
    } else {
        // Normal move/capture: restore captured piece (or empty) on target square
        board[toRow][toCol] = undo.captured;
    }

    // Undo castling rook movement
    if (undo.didCastle) 
    {
        board[undo.rookFromRow][undo.rookFromCol] = undo.rookOriginal;
        board[undo.rookFromRow][undo.rookFromCol].setPosition(undo.rookFromRow, undo.rookFromCol);
        board[undo.rookToRow][undo.rookToCol] = Piece();
    }

    // Restore game state
    whiteCanCastleKingside  = undo.whiteCanCastleKingside;
    whiteCanCastleQueenside = undo.whiteCanCastleQueenside;
    blackCanCastleKingside  = undo.blackCanCastleKingside;
    blackCanCastleQueenside = undo.blackCanCastleQueenside;
    enPassantTargetRow      = undo.enPassantTargetRow;
    enPassantTargetCol      = undo.enPassantTargetCol;
    currentTurn             = undo.previousTurn;
    halfMoveClock           = undo.previousHalfMoveClock;
    moveCount               = undo.previousMoveCount;
    whiteKingRow            = undo.previousWhiteKingRow;
    whiteKingCol            = undo.previousWhiteKingCol;    
    blackKingRow            = undo.previousBlackKingRow;
    blackKingCol            = undo.previousBlackKingCol;
}    


std::vector<Move> Board::getLegalMoves(int row, int col) 
{

    std::lock_guard<std::mutex> lock(boardMutex);

    std::vector<Move> moves;
    
    if (!isValidPosition(row, col)) 
    {
        return moves;
    }
    
    const Piece& piece = board[row][col];
    if (piece.isEmpty() || piece.getColor() != currentTurn) 
    {
        return moves;
    }
    
    // Generate pseudo-legal moves based on piece type
    moves = generatePseudoLegalMoves(row, col);
    // // print out moves
    // std::cout << "Generated " << moves.size() << " pseudo-legal moves for piece at (" 
    //           << row << "," << col << "): " << piece.toString() << std::endl;

    // Only add castling candidates for the current king square
    if (piece.getType() == PieceType::KING) 
    {
        appendCastlingMoves(moves, piece.getColor());
    }

    // Only add en passant captures if applicable
    if (piece.getType() == PieceType::PAWN) 
    {
        if (enPassantTargetRow != -1 && enPassantTargetCol != -1) 
        {
            int direction = (piece.getColor() == PieceColor::WHITE) ? -1 : 1;
            if (row + direction == enPassantTargetRow &&
                (col - 1 == enPassantTargetCol || col + 1 == enPassantTargetCol)) 
            {
                Move m = Move(row, col, enPassantTargetRow, enPassantTargetCol);
                m.setIsEnPassant(true); 
                moves.push_back(m);
            }
        }
    }
    
    // TODO: Filter out moves that would leave king in check
    removeIllegalMoves(&moves);

    // print out legal moves
    // std::cout << "After removing illegal moves, " << moves.size() 
    //           << " legal moves remain for piece at (" 
    //           << row << "," << col << "): " << piece.toString();
    // //sleep for 10 milliseconds to allow debug output to flush
    // std::this_thread::sleep_for(std::chrono::milliseconds(1*1000));
    return moves;
}
// I am losing my mind 
// this is to compare slow vs fast illegal move removal
// and find bugs in the fast version
// the slow version is verfied correct against a half dozen perft positions
// the fast version has a minor bug somewhere, perft counts drift slightly
std::vector<Move> Board::getLegalMoves_DEBUG(int row, int col)
{
    // simulate every move
    std::vector<Move> slow = generatePseudoLegalMoves(row, col);
    {
        std::vector<Move> tmp = slow;
        removeIllegalMoves_slowButCorrect(&tmp);   
        slow = tmp;
    }
   
    // pin detection and isSquareUnderAttack based removal with simulate for en passant and checks
    std::vector<Move> fast = generatePseudoLegalMoves(row, col);
    {
        std::vector<Move> tmp = fast;
        removeIllegalMoves(&tmp);  
        fast = tmp;
    }
  
    //size of moves of both 
    std::cout << "DEBUG: getLegalMoves_DEBUG at (" << row << "," << col << "): "
              << "slow size = " << slow.size()   
              << ", fast size = " << fast.size() << "\n";
    //Compare
    bool mismatch = false;

    // Moves slow has but fast does NOT
    for (const Move& mSlow : slow)
    {
        bool found = false;
        for (const Move& mFast : fast)
        {
            if (mSlow.toString() == mFast.toString())
            {
                found = true;
                break;
            }
        }
        if (!found)
        {
            mismatch = true;
            std::cout << "FAST MISSING MOVE: " << mSlow.toString()
                      << " at (" << row << "," << col << ")\n";
        }
    }

    // Moves fast has but slow does NOT
    for (const Move& mFast : fast)
    {
        bool found = false;
        for (const Move& mSlow : slow)
        {
            if (mFast.toString() == mSlow.toString())
            {
                found = true;
                break;
            }
        }
        if (!found)
        {
            mismatch = true;
            std::cout << "FAST ADDED ILLEGAL MOVE: " << mFast.toString()
                      << " at (" << row << "," << col << ")\n";
            // print info about move
            std::cout << "    From piece: " 
                      << board[mFast.getFromRow()][mFast.getFromCol()].toString()
                      << "\n";
            std::cout << "    To piece: " 
                      << board[mFast.getToRow()][mFast.getToCol()].toString()
                      << "\n";
            std::cout << "    isInCheck: " << isInCheck() << "\n";
        }
    }
    // If mismatch, print FEN and context 
    if (mismatch)
    {
        std::cout << "=== POSITION WITH MISMATCH ===\n";
        std::cout << "Square: (" << row << "," << col << ")\n";
        std::cout << "FEN: " << toFEN() << "\n";
        std::cout << "==============================\n";
    }    

    return slow; // or slow, idc
}


std::vector<Move> Board::generatePseudoLegalMoves(int row, int col) const 
{
    const Piece& piece = board[row][col];

    const PieceColor color = piece.getColor();
    const PieceType type   = piece.getType();

    std::vector<Move> moves;

    // From Visual Studio profiling, std::vector<Move>::_Emplace_reallocate<Move>, was using 18% of self CPU time
    // I think its just from rampant reallocation
    // Preallocate space for moves based on maximal pseudolegal moves for each piece type    
    switch (type) 
    {
        case PieceType::PAWN:   moves.reserve(4); break;
        case PieceType::KNIGHT: moves.reserve(8); break;
        case PieceType::BISHOP: moves.reserve(13); break;
        case PieceType::ROOK:   moves.reserve(14); break;
        case PieceType::QUEEN:  moves.reserve(28); break;
        case PieceType::KING:   moves.reserve(8); break;
    }
    
    
    // Basic move generation 
    switch (type) 
    {
        case PieceType::PAWN: 
        {
            int direction;
            if (color == PieceColor::WHITE) 
            {
                direction = -1; // white pawns move "up" board
            } else 
            {
                direction = 1;  // black pawns move "down" board
            }

            int newRow = row + direction;

            std::vector<Move> tempMoves;
            
            // Forward move
            if (isValidPosition(newRow, col) && board[newRow][col].isEmpty()) 
            {
                //emplace_back is faster than push_back for -> does not require a preconstructed Move object
                tempMoves.emplace_back(row, col, newRow, col);
            }

            // Double move from starting position
            if ((color == PieceColor::WHITE && row == 6) ||
                (color == PieceColor::BLACK && row == 1)) 
            {

                int doubleRow = row + 2 * direction;

                if (isValidPosition(doubleRow, col) && board[doubleRow][col].isEmpty() &&
                    board[newRow][col].isEmpty()) 
                {
                    tempMoves.emplace_back(row, col, doubleRow, col);
                }
            }
            
            // Captures
            if (isValidPosition(newRow, col - 1) && !board[newRow][col - 1].isEmpty() &&
                board[newRow][col - 1].getColor() != color) 
            {
                tempMoves.emplace_back(row, col, newRow, col - 1);
            }

            if (isValidPosition(newRow, col + 1) && !board[newRow][col + 1].isEmpty() &&
                board[newRow][col + 1].getColor() != color)     
            {
                tempMoves.emplace_back(row, col, newRow, col + 1);
            }

            // Promotions
            int promotionRow = (color == PieceColor::WHITE) ? 0 : 7;
            std::vector<PieceType> promotionTypes = {PieceType::QUEEN, PieceType::ROOK, PieceType::BISHOP, PieceType::KNIGHT};
            
            for (const Move& m : tempMoves) 
            {
                if (m.getToRow() == promotionRow) 
                {
                    for (PieceType pt : promotionTypes) 
                    {
                        Move promoMove = m;
                        promoMove.setPromotion(pt);
                        moves.push_back(promoMove);
                    }
                }
                else 
                {
                    moves.push_back(m);
                }
            }

            break;
        }
        
        case PieceType::KNIGHT: 
        {
            int knightMoves[8][2] = {{-2,-1}, {-2,1}, {-1,-2}, {-1,2}, {1,-2}, {1,2}, {2,-1}, {2,1}};
            for (int (&offset)[2] : knightMoves) 
            {
                int newRow = row + offset[0];
                int newCol = col + offset[1];
                if (isValidPosition(newRow, newCol)) 
                {
                    const Piece& target = board[newRow][newCol];
                    if (target.isEmpty() || target.getColor() != color) 
                    {
                        moves.emplace_back(row, col, newRow, newCol);
                    }
                }
            }
            break;
        }

        case PieceType::BISHOP: 
        {
            // sliding moves diagonally
            int directions[4][2] = {{1,1}, {1,-1}, {-1,1}, {-1,-1}};
            for (int (&dir)[2] : directions) 
            {
                int newRow = row + dir[0];
                int newCol = col + dir[1];

                while (isValidPosition(newRow, newCol)) 
                {
                    const Piece& target = board[newRow][newCol];
                    if (target.isEmpty()) 
                    {
                        moves.emplace_back(row, col, newRow, newCol);
                    } else 
                    {
                        if (target.getColor() != color) 
                        {
                            moves.emplace_back(row, col, newRow, newCol);
                        }
                        break; // blocked
                    }
                    newRow += dir[0];
                    newCol += dir[1];
                }
            }
            break;
        }

        case PieceType::ROOK: 
        {
            // sliding moves orthogonally
            int directions[4][2] = {{1,0}, {-1,0}, {0,1}, {0,-1}};
            for (int (&dir)[2] : directions) 
            {
                int newRow = row + dir[0];
                int newCol = col + dir[1];

                while (isValidPosition(newRow, newCol)) 
                {
                    const Piece& target = board[newRow][newCol];
                    if (target.isEmpty()) 
                    {
                        moves.emplace_back(row, col, newRow, newCol);
                    } else 
                    {
                        if (target.getColor() != color) 
                        {
                            moves.emplace_back(row, col, newRow, newCol);
                        }
                        break; // blocked
                    }
                    newRow += dir[0];
                    newCol += dir[1];
                }
            }
            break;
        }

        case PieceType::QUEEN: 
        {
            // Combine rook and bishop moves
            int directions[8][2] = {{1,0}, {-1,0}, {0,1}, {0,-1}, {1,1}, {1,-1}, {-1,1}, {-1,-1}};
            for (int (&dir)[2] : directions) 
            {
                int newRow = row + dir[0];
                int newCol = col + dir[1];

                while (isValidPosition(newRow, newCol)) 
                {
                    const Piece& target = board[newRow][newCol];
                    if (target.isEmpty()) 
                    {
                        moves.emplace_back(row, col, newRow, newCol);
                    } else 
                    {
                        if (target.getColor() != color) 
                        {
                            moves.emplace_back(row, col, newRow, newCol);
                        }
                        break; // blocked
                    }
                    newRow += dir[0];
                    newCol += dir[1];
                }
            }
            break;
        }

        case PieceType::KING: 
        {
            // single-square moves in all directions
            int kingMoves[8][2] = {{-1,-1}, {-1,0}, {-1,1}, {0,-1}, {0,1}, {1,-1}, {1,0}, {1,1}};
            for (int (&offset)[2] : kingMoves) 
            {
                int newRow = row + offset[0];
                int newCol = col + offset[1];
                if (isValidPosition(newRow, newCol)) 
                {
                    const Piece& target = board[newRow][newCol];
                    if (target.isEmpty() || target.getColor() != color) 
                    {
                        moves.emplace_back(row, col, newRow, newCol);
                    }
                }
            }
            break;
        }

        default:
            break;
    }
    
    return moves;
}


std::vector<Move> Board::generateAllPseudoLegalMoves() const
{
    std::vector<Move> moves;

    for (int row = 0; row < 8; row++) 
    {
        for (int col = 0; col < 8; col++) 
        {
            const Piece& p = board[row][col];
            if (!p.isEmpty() && p.getColor() == currentTurn) 
            {
                std::vector<Move> pieceMoves = generatePseudoLegalMoves(row, col);
                
                moves.insert(moves.end(), pieceMoves.begin(), pieceMoves.end());

                // Only add castling candidates for the current king square
                if (p.getType() == PieceType::KING) {
                    appendCastlingMoves(moves, p.getColor());
                }

                // Only add en passant captures if applicable
                if (p.getType() == PieceType::PAWN) {
                    if (enPassantTargetRow != -1 && enPassantTargetCol != -1) {
                        int direction = (p.getColor() == PieceColor::WHITE) ? -1 : 1;
                        if (row + direction == enPassantTargetRow &&
                            (col - 1 == enPassantTargetCol || col + 1 == enPassantTargetCol)) 
                        {
                            Move m = Move(row, col, enPassantTargetRow, enPassantTargetCol);
                            m.setIsEnPassant(true); 
                            moves.push_back(m);
                        }
                    }
                }

            }
        }
    }


    

    return moves; 
}


// Use pseudolegal moves because its alot faster than legal moves, and allegedly its fine. still not convinced
// TODO: see if using pseudolegal moves here is causing problems
std::vector<Move> Board::generateQSearchMoves() const 
{
    std::vector<Move> captures;

    for (int row = 0; row < 8; row++) 
    {
        for (int col = 0; col < 8; col++) 
        {
            const Piece& p = board[row][col];
            if (!p.isEmpty() && p.getColor() == currentTurn) 
            {
                std::vector<Move> pieceMoves = generatePseudoLegalMoves(row, col);

                // Only add en passant captures if applicable
                if (p.getType() == PieceType::PAWN) {
                    if (enPassantTargetRow != -1 && enPassantTargetCol != -1) {
                        int direction = (p.getColor() == PieceColor::WHITE) ? -1 : 1;
                        if (row + direction == enPassantTargetRow &&
                            (col - 1 == enPassantTargetCol || col + 1 == enPassantTargetCol)) 
                        {
                            Move m = Move(row, col, enPassantTargetRow, enPassantTargetCol);
                            m.setIsEnPassant(true); 
                            captures.push_back(m);
                        }
                    }
                }

                for (const Move& m : pieceMoves) 
                {
                    const Piece& target = board[m.getToRow()][m.getToCol()];

                    // promotions, captures
                    // TODO: add checks later if move gen is faster
                    bool isCapture = !target.isEmpty();
                    bool isPromotion = m.getIsPromotion();                    
                    if (isCapture || isPromotion) 
                    {
                        captures.push_back(m);
                    }
                }
            }
        }
    }

    return captures;
}


void Board::appendCastlingMoves(std::vector<Move>& moves, PieceColor color) const 
{
    int row = (color == PieceColor::WHITE) ? 7 : 0;

    // Kingside castling
    if ((color == PieceColor::WHITE && whiteCanCastleKingside) ||
        (color == PieceColor::BLACK && blackCanCastleKingside)) 
    {
        if (board[row][5].isEmpty() && board[row][6].isEmpty() &&
            !isSquareUnderAttack(row, 4, (color == PieceColor::WHITE) ? PieceColor::BLACK : PieceColor::WHITE) &&
            !isSquareUnderAttack(row, 5, (color == PieceColor::WHITE) ? PieceColor::BLACK : PieceColor::WHITE) &&
            !isSquareUnderAttack(row, 6, (color == PieceColor::WHITE) ? PieceColor::BLACK : PieceColor::WHITE)) 
        {
            moves.emplace_back(row, 4, row, 6); // kingside
            moves.back().setIsCastling(true);
        }
    }

    // Queenside castling
    if ((color == PieceColor::WHITE && whiteCanCastleQueenside) ||
        (color == PieceColor::BLACK && blackCanCastleQueenside)) 
    {
        if (board[row][1].isEmpty() && board[row][2].isEmpty() && board[row][3].isEmpty() &&
            !isSquareUnderAttack(row, 4, (color == PieceColor::WHITE) ? PieceColor::BLACK : PieceColor::WHITE) &&
            !isSquareUnderAttack(row, 3, (color == PieceColor::WHITE) ? PieceColor::BLACK : PieceColor::WHITE) &&
            !isSquareUnderAttack(row, 2, (color == PieceColor::WHITE) ? PieceColor::BLACK : PieceColor::WHITE)) 
        {          
            moves.emplace_back(row, 4, row, 2); // queenside
            moves.back().setIsCastling(true);
        }
    }
}

// Visual Studio Profiler is unhappy. 10% self CPU.
// TODO: pin detection logic, and only run when king is moving or a pinned piece is moved/captured
// precompute pins
// loop over all moves
// if king move, check isSquareUnderAttack for destination square
// if pinned piece move, check if move is along pin direction
// else accept all mvoes
void Board::removeIllegalMoves(std::vector<Move>* moves)
{
    std::vector<Move> legal;
    legal.reserve(moves->size());

    // for all posible moves, are we currently in check
    bool inCheck = isInCheck();  
  
    // Precompute pins
    PinInfo pins[8][8];  
    computePins(currentTurn, pins);  
   
    PieceColor opponent = (currentTurn == PieceColor::WHITE)
                            ? PieceColor::BLACK   
                            : PieceColor::WHITE;
    
    for (const Move& move : *moves)   
    {
        int fr = move.getFromRow();
        int fc = move.getFromCol();
        int tr = move.getToRow();  
        int tc = move.getToCol(); 
 
        const Piece& p = board[fr][fc]; 
        PieceType type = p.getType();
     
        // Originally I wanted king moves to be checked using isSquareUnderAttack only, an analytical and fast solution.
        // However, I now realize King moves should be simulated.
        // checking isSquareUnderAttack on the target square is by definition done before the king moves
        // the king can actually be protecting the square it is moving to
        // so moving the king from a check can leave the king still in check.
        /* {FAST ADDED ILLEGAL MOVE: d2e1 at (6,3)
            === POSITION WITH MISMATCH ===
            Square: (6,3)
            FEN: rnb1kbnr/pp1ppppp/2p5/q7/8/3P4/PPPKPPPP/RNBQ1BNR w kq - 2 5}} 
        */
        // a more comphrehensive isSqaureUnderAttack can be created to remove the king from the board, check if the 
        // desired square is attacked, then restore the king
        // but at that point, its basically just simulating the move anyway
        // tldr: simulating king moves is simpler and marginally slower if at all
        if (type == PieceType::KING)  
        {
            UndoInfo undo;  
            makeUncheckedMove(move, undo, false);  
            if (!isInCheck())  
                legal.push_back(move);  
            undoMove(move, undo);
            continue;   
        }

        // Simulate moves in check
        if (inCheck)
        {            
            UndoInfo undo;
            makeUncheckedMove(move, undo, false);
            if (!isInCheck())  
                legal.push_back(move); 
            undoMove(move, undo);
            continue;        
        } 
      

        // Simulate pins 
        //TODO: i dont actually this it has to be simulated, analytical solution should be possible
        if (pins[fr][fc].isPinned)
        {
            
            //mustSimulate = true;
            const PinInfo& pin = pins[fr][fc];

            // pinned pieces can only move along the pin direction        
            
            //Move direction
            int dRow = tr - fr;
            int dCol = tc - fc; 
         
            // pin direction
            int pinRow = pin.pinDirectionRow;    
            int pinCol = pin.pinDirectionCol; 

            // (a,b) and (c,d) are in the same direction if a*d == b*c    
            if (dRow * pinCol != dCol * pinRow)   
            {
                // if not in same direction, moving off pin direction
                continue;
            }

            legal.push_back(move);   
            continue;  
        }
   
        // Simulate en passant captures    
        // can do some two pawns next to each other logic here instead of simulating, i think thats what i did in the python abomination awhile ago
        // but en passants are so rare its not worth it    
        if (move.isEnPassantMove())
        {   
            UndoInfo undo;
            makeUncheckedMove(move, undo, false);
            if (!isInCheck())
                legal.push_back(move);
            undoMove(move, undo);
            continue;
        }

        // all other moves are accepted as auto legal
        legal.push_back(move);
    }

    *moves = std::move(legal);
}

// This method works but is slow because it simulates every move
void Board::removeIllegalMoves_slowButCorrect(std::vector<Move>* moves)
{
    // Move legality filtering
    // Simulate each move and check if king remains safe
    std::vector<Move> legalMoves;
    for (const Move& move : *moves) 
    {
        UndoInfo undo;

        makeUncheckedMove(move, undo, false);

        if (!isInCheck()) {
            legalMoves.push_back(move);
        }
        undoMove(move, undo);
    }
    *moves = legalMoves;
}

std::string Board::toFEN() const
{
    std::string fen;

    // 1. Piece placement
    for (int r = 0; r < 8; ++r) 
    {
        int empty = 0;  
        for (int c = 0; c < 8; ++c) 
        {
            const Piece& p = board[r][c];  
            if (p.isEmpty())
            {
                empty++;
            }
            else
            {
                if (empty > 0)
                {
                    fen += std::to_string(empty);
                    empty = 0;
                }

                char ch;
                switch (p.getType())
                {
                    case PieceType::PAWN:   ch = 'p'; break;
                    case PieceType::KNIGHT: ch = 'n'; break;
                    case PieceType::BISHOP: ch = 'b'; break;
                    case PieceType::ROOK:   ch = 'r'; break;
                    case PieceType::QUEEN:  ch = 'q'; break;
                    case PieceType::KING:   ch = 'k'; break;
                    default: ch = '?'; break;
                }

                if (p.getColor() == PieceColor::WHITE)
                    ch = std::toupper(ch);

                fen += ch;
            }
        }
        if (empty > 0)
            fen += std::to_string(empty);

        if (r != 7)
            fen += '/';
    }

    // 2. Side to move
    fen += ' ';
    fen += (currentTurn == PieceColor::WHITE ? 'w' : 'b');

    // 3. Castling rights
    fen += ' ';
    std::string cast;
    if (whiteCanCastleKingside)  cast += 'K';
    if (whiteCanCastleQueenside) cast += 'Q';
    if (blackCanCastleKingside)  cast += 'k';
    if (blackCanCastleQueenside) cast += 'q';
    fen += (cast.empty() ? "-" : cast);

    // 4. En passant
    fen += ' ';
    if (enPassantTargetRow == -1)
    {
        fen += "-";
    }
    else
    {
        fen += char('a' + enPassantTargetCol);
        fen += char('1' + (7 - enPassantTargetRow));
    }

    // 5. Halfmove clock
    fen += ' ';
    fen += std::to_string(halfMoveClock);

    // 6. Fullmove number
    fen += ' ';
    fen += std::to_string(moveCount);

    return fen;
}

bool Board::isCheckmate()
{

    // in check and no legal moves
    if (isInCheck()) 
    {
        for (int r = 0; r < 8; r++) 
        {
            for (int c = 0; c < 8; c++) 
            {
                const Piece& piece = board[r][c];
                if (!piece.isEmpty() && piece.getColor() == currentTurn) 
                {
                    std::vector<Move> moves = getLegalMoves(r, c);

                    if (!moves.empty()) 
                    {
                        return false; 
                    }
                }
            }
        }
        return true; // No legal moves found
    }

    return false;
}

bool Board::isStalemate()
 {

    if (!isInCheck()) 
    {
        // Stalemate from lack of material
        // Check if there is at least one pawn, queen, rook on the board for either side

        bool sufficientMaterial = false;
        bool legalMovesAvailable = false;
        bool fiftyMoveRule = halfMoveClock >= 100;

        // Check for any legal moves for current player
        for (int r = 0; r < 8; r++) 
        {
            for (int c = 0; c < 8; c++) 
            {
                const Piece& piece = board[r][c];
                if (!piece.isEmpty() && piece.getColor() == currentTurn) 
                {
                    std::vector<Move> moves = getLegalMoves(r, c);
                    if (!moves.empty()) 
                    {
                        legalMovesAvailable = true;
                    }
                }
            }
        }

        int minorPiecesWhite = 0;
        int minorPiecesBlack = 0;
        for (int r = 0; r < 8; r++) 
        {
            for (int c = 0; c < 8; c++) 
            {
                const Piece& piece = board[r][c];
                if (!piece.isEmpty()) 
                {
                    PieceType type = piece.getType();
                    if (type == PieceType::PAWN || type == PieceType::QUEEN || type == PieceType::ROOK) 
                    {
                        // Found a piece that prevents stalemate by insufficient material
                        sufficientMaterial = true;
                    }
                    if (type == PieceType::BISHOP || type == PieceType::KNIGHT) 
                    {
                        if (piece.getColor() == PieceColor::WHITE)
                        {
                            minorPiecesWhite++;
                        }
                        else
                        {
                            minorPiecesBlack++;
                        }
                        if (minorPiecesWhite > 1 || minorPiecesBlack > 1) 
                        {
                            // More than one minor piece means sufficient material
                            sufficientMaterial = true;
                        }
                    }
                }
            }
        }

        // Stalemate conditions
        if (!legalMovesAvailable || (legalMovesAvailable && (!sufficientMaterial || fiftyMoveRule))) 
        {
            return true;
        }
    }
    return false;


}

bool Board::isLegalMoveAvailable() 
{
    
        for (int r = 0; r < 8; r++) 
        {
            for (int c = 0; c < 8; c++) 
            {
                const Piece& piece = board[r][c];
                if (!piece.isEmpty() && piece.getColor() == currentTurn) 
                {
                    //std::vector<Move> moves = getLegalMoves(r, c);
                    std::vector<Move> moves = generatePseudoLegalMoves(r, c);

                    if (!moves.empty()) 
                    {
                        // make move and check if king is in check 
                        for (const Move& move : moves) 
                        {
                            Board::UndoInfo undo;

                            makeUncheckedMove(move, undo, true);
                            
                            PieceColor movedSide = undo.previousTurn;
                            
                            if (isInCheck(movedSide))
                            {
                                undoMove(move, undo);
                                continue; // illegal move
                            }                   
                            else  
                            {  
                                undoMove(move, undo);  
                                return true; // legal move found
                            }          
                        }             
                    }
                }
            }
        }

    return false;
}

bool Board::isSufficientMaterial() const
{
    int minorPiecesWhite = 0;
    int minorPiecesBlack = 0;

    for (int r = 0; r < 8; r++) 
    {
        for (int c = 0; c < 8; c++) 
        {
            const Piece& piece = board[r][c];
            if (!piece.isEmpty()) 
            {
                PieceType type = piece.getType();
                if (type == PieceType::PAWN || type == PieceType::QUEEN || type == PieceType::ROOK) 
                {
                    return true; // sufficient material 
                } 
                if (type == PieceType::BISHOP || type == PieceType::KNIGHT)  
                {
                    if (piece.getColor() == PieceColor::WHITE) 
                    {
                        minorPiecesWhite++;
                    }  
                    else
                    {  
                        minorPiecesBlack++;
                    }
                    if (minorPiecesWhite > 1 || minorPiecesBlack > 1) 
                    {
                        return true; // sufficient material 
                    }
                } 
            } 
        }
    } 

    return false; // insufficient material 
}
// TODO: update with directional attack logic
// VS Profiler is unhappy with generatePseudoLegalMoves and checking each move for attacking the row,col square
// directional attack logic is about an order of magnitude faster... hot damn
bool Board::isSquareUnderAttack(int row, int col, PieceColor attackingColor) const 
{

    // // Slow and simple way to check if a square is under attack 
    // for (int r = 0; r < 8; r++) 
    // {
    //     for (int c = 0; c < 8; c++) 
    //     {
    //         const Piece& piece = board[r][c];
    //         if (!piece.isEmpty() && piece.getColor() == attackingColor) 
    //         {
    //             std::vector<Move> moves = generatePseudoLegalMoves(r, c);
    //             for (const Move& move : moves) 
    //             {
    //                 if (move.getToRow() == row && move.getToCol() == col) 
    //                 {
    //                     return true;
    //                 }
    //             }
    //         }
    //     }
    // }
    // return false;    


    const PieceColor defender = (attackingColor == PieceColor::WHITE) ? PieceColor::BLACK : PieceColor::WHITE;

    // Pawn attacks
    int pawnDir = (attackingColor == PieceColor::WHITE) ? -1 : 1;

    // White attacks from row-1; Black from row+1
    int attackerRow = row - pawnDir;

    if (isValidPosition(attackerRow, col - 1)) {
        const Piece& p = board[attackerRow][col - 1];
        if (!p.isEmpty() && p.getColor() == attackingColor && p.getType() == PieceType::PAWN)
            return true;
    }

    if (isValidPosition(attackerRow, col + 1)) {
        const Piece& p = board[attackerRow][col + 1];
        if (!p.isEmpty() && p.getColor() == attackingColor && p.getType() == PieceType::PAWN)
            return true;
    }

    // knight
    static const int knightOffsets[8][2] = {
        {-2,-1}, {-2,1}, {-1,-2}, {-1,2},
        {1,-2},  {1,2},  {2,-1},  {2,1}
    };

    for (auto& off : knightOffsets) {
        int r = row + off[0];
        int c = col + off[1];
        if (isValidPosition(r, c)) {
            const Piece& p = board[r][c];
            if (!p.isEmpty() && p.getColor() == attackingColor && p.getType() == PieceType::KNIGHT)
                return true;
        }
    }

    // king
    static const int kingOffsets[8][2] = {
        {-1,-1}, {-1,0}, {-1,1},
        {0,-1},          {0,1},
        {1,-1},  {1,0},  {1,1}
    };

    for (auto& off : kingOffsets) {
        int r = row + off[0];
        int c = col + off[1];
        if (isValidPosition(r, c)) {
            const Piece& p = board[r][c];
            if (!p.isEmpty() && p.getColor() == attackingColor && p.getType() == PieceType::KING)
                return true;
        }
    }

    // horizontal
    static const int rookDirs[4][2] = {
        {1,0}, {-1,0}, {0,1}, {0,-1}
    };

    for (auto& d : rookDirs) {
        int r = row + d[0];
        int c = col + d[1];
        while (isValidPosition(r, c)) {
            const Piece& p = board[r][c];
            if (!p.isEmpty()) {
                if (p.getColor() == attackingColor &&
                    (p.getType() == PieceType::ROOK || p.getType() == PieceType::QUEEN))
                    return true;
                break;
            }
            r += d[0];
            c += d[1];
        }
    }

    // diagonal 
    static const int bishopDirs[4][2] = {
        {1,1}, {1,-1}, {-1,1}, {-1,-1}
    };

    for (auto& d : bishopDirs) {
        int r = row + d[0];
        int c = col + d[1];
        while (isValidPosition(r, c)) {
            const Piece& p = board[r][c];
            if (!p.isEmpty()) {
                if (p.getColor() == attackingColor &&
                    (p.getType() == PieceType::BISHOP || p.getType() == PieceType::QUEEN))
                    return true;
                break;
            }
            r += d[0];
            c += d[1];
        }
    }

    return false;
}

 
// Walk rays outwards from king to find pins
void Board::computePins(PieceColor color, PinInfo pinnys[8][8]) const
{
    for (int r = 0; r < 8; ++r)
    for (int c = 0; c < 8; ++c)
        pinnys[r][c] = PinInfo{};

    int kingRow = (color == PieceColor::WHITE) ? whiteKingRow : blackKingRow;
    int kingCol = (color == PieceColor::WHITE) ? whiteKingCol : blackKingCol;

    bool firstFriendlyFound;

    // Directions from king to look for potential pins
    static const int directions[8][2] = 
    {
        { 1, 0}, {-1, 0}, {0, 1}, {0,-1}, // cardinal
        { 1, 1}, { 1,-1}, {-1, 1}, {-1,-1} // diag
    };

    for (auto& d : directions) 
    {
        int dr = d[0];
        int dc = d[1];

        int r = kingRow + dr;
        int c = kingCol + dc;

        int pinnedRow = -1;
        int pinnedCol = -1;

        // If two friendly pieces are found before an enemy piece, no pin is possible
        firstFriendlyFound = false;

        // Move outwards in dr,dc direction until edge of board or friendly piece found
        while (isValidPosition(r, c)) 
        {
            const Piece& p = board[r][c];

            if (!p.isEmpty()) 
            {
                if (p.getColor() == color) 
                {        
                    if (firstFriendlyFound)
                    {
                        // second friendly piece found, no pin possible
                        break;
                    }

                    // friendlies hold your fire
                    firstFriendlyFound = true;
                    // only first friendly piece can be pinned
                    pinnedRow = r;
                    pinnedCol = c;
                }
                else
                {
                    // enemy piece
                    // check if it can attack along dr,dc direction
                    PieceType ep = p.getType();
                    
                    // classify direction
                    bool horz = (dr == 0 || dc == 0);
                    bool diag = (std::abs(dr) == std::abs(dc));                        
                    
                    bool canPin = (
                        (horz && (ep == PieceType::ROOK   || ep == PieceType::QUEEN)) ||
                        (diag && (ep == PieceType::BISHOP || ep == PieceType::QUEEN))
                    );

                    if (canPin && pinnedRow != -1 && pinnedCol != -1) 
                    {
                        // Mark the pinned piece
                        pinnys[pinnedRow][pinnedCol].isPinned        = true;
                        pinnys[pinnedRow][pinnedCol].pinDirectionRow = dr;
                        pinnys[pinnedRow][pinnedCol].pinDirectionCol = dc;
                        pinnys[pinnedRow][pinnedCol].attackerRow     = r;
                        pinnys[pinnedRow][pinnedCol].attackerCol     = c;
                    }
                    break; // stop searching in this direction
                }

            }

            // Continue outwards
            r = r + dr;
            c = c + dc;
        }
    }
}

void Board::makeNullMove(UndoInfo& undo)
{
    // flip
    // clear ep

    undo.previousTurn = currentTurn;

    undo.enPassantTargetRow = enPassantTargetRow;
    undo.enPassantTargetCol = enPassantTargetCol;

    currentTurn = (currentTurn == PieceColor::WHITE ? PieceColor::BLACK : PieceColor::WHITE);

    enPassantTargetRow = -1;
    enPassantTargetCol = -1;
}

void Board::undoNullMove(const UndoInfo& undo)
{
    currentTurn = undo.previousTurn;

    enPassantTargetRow = undo.enPassantTargetRow;
    enPassantTargetCol = undo.enPassantTargetCol;
}
