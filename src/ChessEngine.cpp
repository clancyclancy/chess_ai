/*
cmake .. -DCMAKE_TOOLCHAIN_FILE=C:\vcpkg\scripts\buildsystems\vcpkg.cmake
 


GUI -> Engine Command:: MAKE_MOVE -> Engine Thread      
- Engine processes move
- Validates legality
- Updates board state
          
GUI <- Engine Response:: MOVE_RESULT <- Engine Thread   
- Update            
- Render   
- GUI requests AI move

GUI -> Engine Command:: COMPUTE_AI_MOVE-> Engine Thread    
- Engine calculates best move  
                                            
GUI <- Engine Response:: AI_MOVE <- Engine Thread   
       
 */

/*TODO:

BUGS:

engine sometimes misses checkmates for the opponent 
engine sometimes misevaluates capture sequences horribly - check SEE
engine misevaluates pawn pushes - i think disable pruning at a higher material count
engine sometimes plays moves that cause a bishop to get trapped 
engine sometimes repeats moves in a winning position 


white capture a3b4 is a draw. white plays kg7 at depth 8 which is losing immediately 
8/8/5K2/2p5/1p2k2B/P7/8/8 w - - 0 1
- the engine simply cannot see far enough ahead to realize its losing. need to speed up move gen


black is one square away from promoting and checkmating white. white thinks they are winning by 0.58 pawns
- dont htink promotions are being evaluated properly
8/2k3Pp/B3b1P1/p1p5/P7/RPp5/2Prp3/2K5 w - - 0 1    
- RESOLVED. bug: PV was not being updated properly in root search. now properly updates 
- RESOLVED. q search now has promotions. this solved the problem but was not the root cause  


plays great opening. hangs pieces in midgame. then crashes 
-TODO: look at SEE for memory corruption - found nothing
-TODO: might be how getOrderMoves is sorting and copying moves.. idk. test getOrderedMoves, only copy the moves into a new vector and then replace them in moves.  - found nothing
- ran perft test with getOrderedMoves and had no problems
- getOrderedMoves must be exposing a bug somewhere else   
- rnb1kb1r/pp3pp1/2p4p/3q4/4p3/5N2/PPPPBPPP/R1BQR1K1 b kq - 0 1  // selects e4f3 at depth 1 (which hangs a queen) then never picks a better move at later depths then crashes instantly before making the move
- RESOLVED bug: not picking better move. aspiration windows were not handling both fail-low and fail-high properly.
r1bqkb1r/ppp1ppp1/2n2n1p/3P4/8/2N2N2/PPPPBPPP/R1BQK2R b KQkq - 0 1 //causes a crash on blacks ai move
- NMR and FP do not affect crash
- turning off aspiration windows removes crash. but i suspect its due to a different search tree not AW causing the crash
- RESOLVED crashes. bug: it was a bug in SEE but specifically the getAllAttackersOfSquare call. was accessing out of bounds board positions for pawn attacks. 


hangs queen and engine line does not match played move
- RESOLVED. bug: did not account for a new depth not finding a new pv. (need lastCompleteBestMove vars that are only updated when a new pv is found)
r1bq1rk1/1pp3pp/1b2p3/p4p2/2Q3P1/P6P/1PPP1PB1/R1B2RK1 w KQkq - 0 1


move ordering crash due to out of bounds index of MVV_LVA
- RESOLVED. bug: MVV_LVA was incorrect, now use SEE for move ordering


futility move pruning causes wack moves to be played.  
- RESOLVED. bug


aspiration windows causing bad moves 
- RESOLVED. removed fail low fail high handling. if outside of window. remove all aspiration windows and research at full depth


ENGINE:  
not handling timeouts in qsearch - resolved
isEmpty() is responsible for 9% of cpu time in profile. it is already inlined. 
dont think can get much more speed from move generation without a complete rewrite using bitboards.

EVALUATION:  

endgame
-incentivize bringing king and pieces towards enemy king
-incentivize restricting enemy king movement


optimize piece mobility eval 

pawn structure bonus for connected pawns
penalize rh1g1 in piece square tables 
less reward for developing knight
more reward for pawn e4,d4 
penalize random h pawn pushes
-RESOLVED





GUI:  
gui highlight to and from squares after move made 
add 100ms delay before ai move to when ai vs ai. endgames resolve like 5 moves nearly instantly   
improve how the gui info panel looks


*/
#include "ChessEngine.h"
#include <algorithm>
#include <iostream> // only for printing debug info, can be removed later before pushed
#include <iomanip>  // only for printing debug info, can be removed later before pushed

#include <chrono>   // timing

#include "Board.h"

#include <assert.h>
    


ChessEngine::ChessEngine(): 
    running(false), 
    aiSearchDepth(12) 
    {
    }

ChessEngine::~ChessEngine() 
{
    stop();
}

void ChessEngine::start() 
{
    if (running) return;
    
    running = true;
    engineThread = std::thread(&ChessEngine::engineLoop, this);
}

void ChessEngine::stop() 
{
    if (!running) return;
    
    // Send shutdown command
    CommandData cmd(EngineCommand::SHUTDOWN);
    sendCommand(cmd);
    
    // this is necessary for ~Safe Synchronization~, it leaves hanging threads otherwise and causes crashes :/
    // .joinable checks if thread is running
    // .join blocks the calling thread until the thread finishes execution
    // prevents dangling threads
    if (engineThread.joinable()) 
    {
        engineThread.join();
    }
    
    running = false;
}

void ChessEngine::sendCommand(const CommandData& command) 
{
    std::lock_guard<std::mutex> lock(commandMutex);
    commandQueue.push(command);
    commandCV.notify_one();  // wakey wakey
}

bool ChessEngine::hasResponse() 
{
    std::lock_guard<std::mutex> lock(responseMutex);
    return !responseQueue.empty();
}

bool ChessEngine::getResponse(ResponseData& outResponse) 
{
    std::lock_guard<std::mutex> lock(responseMutex);
    
    if (responseQueue.empty()) 
    {
        return false;
    }
    
    outResponse = responseQueue.front();
    responseQueue.pop();
    return true;
}

const Board ChessEngine::getBoardCopy() const 
{
    //TODO: this ensures a safe copy of the board is returned but it might cause bottlenecking
    // revisit here first if search is slowing down
    std::lock_guard<std::mutex> lock(boardMutex);

    //TODO: revisit if parallelizing searches
    return board;
}

void ChessEngine::setBoard(const Board& newBoard) 
{
    std::lock_guard<std::mutex> lock(boardMutex);

    board = newBoard;
}

void ChessEngine::engineLoop() 
{
    // Main engine loop - runs in separate thread
    while (running) 
    {

        CommandData command(EngineCommand::SHUTDOWN);

        // Local scope block {} is necessary, otherwise the lock is still in place when calling processCommand()
        // double lock on mutex causes deadlock and crash 
        {
            std::unique_lock<std::mutex> lock(commandMutex);

            // Wait until there's a command to process
            // Stay asleep until commandCV.notify_one() is called
            commandCV.wait(lock, [this] { return !commandQueue.empty(); });
            
            // Calling .front() on empty queue causes vague crash. ask me how i know :/
            if (!commandQueue.empty()) 
            {
                command = commandQueue.front();
                commandQueue.pop();
            }
        }
        
        // Process the command
        if (command.command == EngineCommand::SHUTDOWN) 
        {
            break;
        }
        
        processCommand(command);
    }
}

void ChessEngine::processCommand(const CommandData& command) 
{
    //TODO: I think logging the responses and sendResponse() can be called outside of the switch case but would take some time to refactor
    // 

    ResponseData response;
    
    switch (command.command) {
        case EngineCommand::MAKE_MOVE: {

            std::lock_guard<std::mutex> lock(boardMutex);

            // Attempt move
            bool success = board.makeMove(command.move);
            
            // Log response
            response.response    = EngineResponse::MOVE_RESULT;
            response.success     = success;
            response.move        = command.move;
            response.currentTurn = board.getCurrentTurn();
            response.isCheck     = board.isInCheck();
            response.isCheckmate = board.isCheckmate();
            response.isStalemate = board.isStalemate();
            
            if (success) 
            {
                response.message = "Move executed: " + command.move.toString();
                std::cout << "Engine: Requested move executed: " << command.move.toString() << std::endl;
            } 
            else 
            {
                response.message = "Invalid move";
            }
            
            // Send response back to GUI
            sendResponse(response);
            break;
        }
        
        case EngineCommand::COMPUTE_AI_MOVE: 
        {
            // //print out
            // std::cout << "Computing AI move..." << std::endl;
            // Lock board
            std::lock_guard<std::mutex> lock(boardMutex);

            // Calculate best move
            Move bestMove = calculateBestMove();
            
            // // 
            std::cout << "AI selected move: " << bestMove.toString() << std::endl;

            if (bestMove.getFromRow() == -1) 
            {
                response.response = EngineResponse::AI_MOVE;
                response.success = false;
                response.message = "AI has no legal moves";
                sendResponse(response);
                return;
            }

            // Attempt move
            bool success = board.makeMove(bestMove);
            
            // Log move response
            response.response    = EngineResponse::AI_MOVE;
            response.success     = success;
            response.move        = bestMove;
            response.currentTurn = board.getCurrentTurn();
            response.isCheck     = board.isInCheck();
            response.isCheckmate = board.isCheckmate();
            response.isStalemate = board.isStalemate();

            // engine data
            response.bestMove     = sendToGuiBestMove;
            response.eval         = sendToGuiBestEval;
            response.line         = sendToGuiBestLine;
            
            response.message     = "AI played: " + bestMove.toString();
            
            // engine to gui response
            sendResponse(response);
            break;
        }
        
        case EngineCommand::GET_LEGAL_MOVES: 
        {
            std::lock_guard<std::mutex> lock(boardMutex);

            // Get legal moves for selected piece
            std::vector<Move> moves = board.getLegalMoves(command.pieceRow, command.pieceCol);
            
            // Log response
            response.response = EngineResponse::LEGAL_MOVES;
            response.success = true;
            response.legalMoves = moves;
            response.message = "Found " + std::to_string(moves.size()) + " legal moves";
            
            // engine to gui response
            sendResponse(response);
            break;
        }
        
        case EngineCommand::RESET_GAME: 
        {
            std::lock_guard<std::mutex> lock(boardMutex);

            board.resetBoard();
            
            // Log response
            response.response    = EngineResponse::GAME_RESET;
            response.success     = true;
            response.currentTurn = board.getCurrentTurn(); // specifing turn color here is overkill, currentTurn is reset by board.resetBoard() 
            response.isCheck     = false;
            response.isCheckmate = false;
            response.isStalemate = false;
            response.message     = "Game reset";
            
            // engine to gui response
            sendResponse(response);
            break;
        }
        
        default:
            std::cout << "Uknown Engine Command: break" << std::endl;
            break;
    }
}

void ChessEngine::sendResponse(const ResponseData& response) 
{
    std::lock_guard<std::mutex> lock(responseMutex);

    responseQueue.push(response);
}

Move ChessEngine::calculateBestMove() 
{
    // calling mutex lock here causes deadlock and the most vague crash 
    // the board is already locked by caller, processCommand(), methods called from processCommand() should not relock it

    // timing 
    searchStart = std::chrono::steady_clock::now();
    searchTimeout = false;

      
    // Last fully completed iteration                  
    Move              lastCompleteBestMove(-1, -1, -1, -1);  
    int               lastCompleteBestEval = 0;  
    std::vector<Move> lastCompleteBestLine;     
    bool              hasCompleteIteration = false;  

  
    
    nodesPerDepth.assign(aiSearchDepth + 1, 0);

    // member variable reset
    moveOrderingPreviousBestMove = Move(-1, -1, -1, -1);

    // // loop over depths for iterative deepening
    int startDepth, endDepth;
    if (USE_ITERATIVE_DEEPENING)
    {
        startDepth = 1;
        endDepth   = aiSearchDepth + 1;
    }
    else 
    {
        startDepth = aiSearchDepth;
        endDepth   = aiSearchDepth + 1;
    }

    if (QUICK_PRINTOUT)
        std::cout << "Starting AI search to depth " << aiSearchDepth << "========================================================================================" << std::endl;

    for ( int numPlyLeft = startDepth; numPlyLeft < endDepth; numPlyLeft++)
    {
        if (searchTimeout)
            break;
                
        int thisIterBestScore = -99999;
        Move thisIterBestMove(-1, -1, -1, -1);
        std::vector<Move> thisIterBestLine;

        resetCounters();

        for (int i = 0; i < MAX_PLY; i++)
            pvLength[i] = 0;

        int alpha, beta;
        
        if (numPlyLeft == startDepth || !USE_ASPIRATION_WINDOWS)  
        {
            // reset alpha and beta each iteration
            // this causes reeally funky bugs if not reset. ask me how i know            
            alpha = -1000000; 
            beta  =  1000000;          
        }
        else 
        {  
            // aspiration window  
            alpha = lastCompleteBestEval - ASPIRATION_WINDOW_MARGIN_AMOUNT;
            beta  = lastCompleteBestEval + ASPIRATION_WINDOW_MARGIN_AMOUNT; 
        }  
  
        auto start = std::chrono::high_resolution_clock::now();     

        std::vector<Move> moves = board.generateAllPseudoLegalMoves();

        
        if (USE_MOVE_ORDERING)
            getOrderedMoves(moves, 0);  // ply is 0 at root nodes              


        for (const Move& move : moves)
        {
           
            Board::UndoInfo undo;                    
            board.makeUncheckedMove(move, undo, true);

            // generating pseudolegal so have to check is move that was made resulted in the king being exposed to check
            PieceColor movedSide = undo.previousTurn;        
            if (board.isInCheck(movedSide))
            {
                board.undoMove(move, undo);
                continue; // illegal move  
            }

            if (FULL_PRINTOUT_OF_NODES_PER_DEPTH)
                std::cout << "Root move candidate: " << move.toString();   

            // ply now 1 after first move was made
            // using aspiration windows on this search
            int score = -negamax(numPlyLeft - 1, -beta, -alpha, 1);                       
            

            // logic for aspiration window check incase of fail-low or fail-high. just full search instead of modifying windows
            bool failLow = (score <= alpha);
            bool failHigh = (score >= beta);
            if ((failLow || failHigh) && 
                 USE_ASPIRATION_WINDOWS)          
            {
                if (FULL_PRINTOUT_OF_NODES_PER_DEPTH)  
                    std::cout << " (AW fail) ";
                // re-search with no clamps
                score = -negamax(numPlyLeft - 1, -1000000, 1000000, 1);
            }     

                      
            board.undoMove(move, undo); // undo after all searches

            
            if (searchTimeout)       
                break;  // only break out after undoing move   


            if (FULL_PRINTOUT_OF_NODES_PER_DEPTH)              
                std::cout << " -> score " << score << " (alpha before update=" << alpha << ")\n";
   
               
            if (score > thisIterBestScore)  
            { 
                thisIterBestScore = score;  
                    
                pv[0][0] = move;  
                for (int i = 0; i < pvLength[1]; i++)   
                    pv[0][i+1] = pv[1][i];  
                pvLength[0] = pvLength[1] + 1;          
            }

            if (score > alpha)
                alpha = score; //new better move has been found. update lower bound of what engine can guarantee       
                           
        }
        
        if (searchTimeout)  
            break;   

        // // store best of iteration
        // // necessary as search can get cutoff once betIterEval is reset
        if (!searchTimeout && pvLength[0] > 0)
        {
            hasCompleteIteration = true;                  
            
            lastCompleteBestEval = thisIterBestScore;  
            lastCompleteBestMove = pv[0][0];             // the move actually leading the PV

            //print out best move
            std::cout << lastCompleteBestMove.toString() << " ";

            lastCompleteBestLine.clear();
            for (int i = 0; i < pvLength[0]; i++)
                lastCompleteBestLine.push_back(pv[0][i]);

            // hint to move ordering for next iteration 
            moveOrderingPreviousBestMove = lastCompleteBestMove;                
        }

       

        // printout info per depth  
        if (!searchTimeout && QUICK_PRINTOUT)
        {  
    
            if (!(pvLength[0] > 0))   
            {  
                std::cout << "Plys Searched=" << numPlyLeft  
                << " No new pv found========================================================================="   
                << std::endl; 
            } 
 
            auto end = std::chrono::high_resolution_clock::now();
            auto ms  = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

            std::cout << "Plys Searched=" << numPlyLeft
                    << "  time="     << std::setw(5) << std::setfill('0') << ms << "ms"  
                    << "  best="     << lastCompleteBestMove.toString()
                    << "  eval="     << std::setw(4) << std::setfill('0') << lastCompleteBestEval
                    << "  normN="    << std::setw(7) << std::setfill('0') << totalNodes
                    << "  qN="       << std::setw(7) << std::setfill('0') << qNodes
                    << "  (N+qN)PS=" << std::setw(5) << std::setfill('0') << ((totalNodes + qNodes) / (ms + 1)) << "Knps"
                    << "  cN="       << std::setw(7) << std::setfill('0') << cutNodes
                    << "  nmcN="     << std::setw(7) << std::setfill('0') << nullMoveCutNodes
                    << "  fpcN="     << std::setw(7) << std::setfill('0') << futilityPruneNodes
                    << "  cqN="      << std::setw(7) << std::setfill('0') << seeCutqNodes
                    << std::endl;
            
            if (FULL_PRINTOUT_OF_NODES_PER_DEPTH)
            {
                std::cout << "totalNPD=";
                for (int d = 0; d <= numPlyLeft; d++)
                    std::cout << " " << d << "=" << nodesPerDepth[d] << " "; 
                std::cout << std::endl;

                std::cout << "cutNPD=  ";
                for (int d = 0; d <= numPlyLeft; d++)
                    std::cout << " " << d << "=" << cutNodesPerDepth[d] << " ";
                std::cout << std::endl;

                // std::cout << "qNPD=    ";
                // for (int d = 0; d <= numPlyLeft; d++)
                //     std::cout << " " << d << "=" << qNodesPerDepth[d] << " ";
                // std::cout << std::endl;

                std::cout << "nmcNPD=  ";
                for (int d = 0; d <= numPlyLeft; d++)
                    std::cout << " " << d << "=" << nullMoveCutNodesPerDepth[d] << " ";
                std::cout << std::endl;

                std::cout << "fpnNPD=  ";
                for (int d = 0; d <= numPlyLeft; d++)
                    std::cout << " " << d << "=" << futilityPruneNodesPerDepth[d] << " ";
                std::cout << std::endl;

                std::cout << "Engine line: ";
                for (int i = 0; i < pvLength[0]; i++)
                    std::cout << pv[0][i].toString() << " ";    
                std::cout << std::endl;            
            }
        }
        

        if (searchTimeout)
            break;

        // stop if mate found
        if (std::abs(lastCompleteBestEval) >= 9000)
            break;
    
    }

    // On timeout or full completion, choose move from last fully completed iteration   
    Move moveToPlay(-1, -1, -1, -1);      
    
    if (hasCompleteIteration)
    {  
    moveToPlay = lastCompleteBestMove;  
    sendToGuiBestMove = lastCompleteBestMove;   
    sendToGuiBestEval = lastCompleteBestEval;
    sendToGuiBestLine = lastCompleteBestLine;

    }  
    else
    {
        moveToPlay = Move(-1, -1, -1, -1); // no legal moves found or search didnt complete any iterations      
        sendToGuiBestMove = moveToPlay; 
        sendToGuiBestEval = 69420; 
        sendToGuiBestLine.clear();  
    }        


    return moveToPlay;
};







int ChessEngine::evaluateBoard() const 
{
    // calling mutex lock here causes deadlock and the most vague crash 
    // the board is already locked by caller, processCommand(), methods called from processCommand() should not relock it
    // duh 
    
    // raw material eval
    // square heuristics 
    // pawn structure
    // king safety
    // piece activity
    // check checkmate and stalemate
    int score = 0;
    

    // track state of game
    // track phase of game by piece count
    // don't care about pawns
    // 62 = opening, 0 = endgame
    int pieceCount = nonPawnMaterialCount();

    PieceColor sideToMove = board.getCurrentTurn();


    // pure material count 
    // TODO: modify piece values based on game state
    score += rawPieceTotal(sideToMove);

    // piece-square heuristics 
    // this is purely based on vibes
    // mid and end game tables for pawn and king
    // only midgame table for everything else
    score += evaluatePieceSquareTables(sideToMove, pieceCount);

    // pawn structure
    // connected pawns
    // isolated
    // doubled
    // passed pawns
    // connected passed pawns 
    score += evaluatePawnStructure(sideToMove);

      
    // count number of moves a piece has
    // knights & bishops significantly benefit from mobility 
    // queens and rooks benefit less  
    // king should be not have much mobility in opening and mid, but prior it in end  
    // TODO: make cheaper. currently too expensive         
    //score += evaluatePieceMobility(sideToMove, pieceCount);                
  

    // king safety 
    // open files near king is very bad
    // open diagonal near king is bad
    // pawn shield is very good
    // consider exponential penalty for every pawn missing in front of king
    // i.e. -50 for 1, -200 for 2, -500 for no pawns at all
    score += evaluateKingSafety(sideToMove, pieceCount);

    // end game specifics
    // restrict enemy king movement in endgame
    // incentivize bringing own king towards enemy king in endgame
    score += evaluateKingRestriction(sideToMove, pieceCount);

    score += evaluateKingToKingDistance(sideToMove, pieceCount);

    // specifically KPK endgame
    // if engine gets to a KPK endgame, even if depth is maxed. it should be able to evaluate a win or draw
    score += evaluateKPKEndgame(sideToMove, pieceCount);

    return score;
};

int ChessEngine::rawPieceTotal(PieceColor sideToMove) const
{

    int score = 0;
    
    const int PAWN_VALUE   = 100;
    const int KNIGHT_VALUE = 300;
    const int BISHOP_VALUE = 300;
    const int ROOK_VALUE   = 500;
    const int QUEEN_VALUE  = 900;
    
    for (int row = 0; row < 8; row++) 
    {
        for (int col = 0; col < 8; col++) 
        {    
            const Piece& piece = board.getPieceConst(row, col); 

            if (piece.isEmpty()) continue; 
            
            int pieceValue = 0; 
            switch (piece.getType())  
            {
                case PieceType::PAWN:   pieceValue = PAWN_VALUE;   break; 
                case PieceType::KNIGHT: pieceValue = KNIGHT_VALUE; break; 
                case PieceType::BISHOP: pieceValue = BISHOP_VALUE; break; 
                case PieceType::ROOK:   pieceValue = ROOK_VALUE;   break;
                case PieceType::QUEEN:  pieceValue = QUEEN_VALUE;  break;  
                default: break;
            }
            
            if (piece.getColor() == sideToMove) //sideToMove 
            {
                score += pieceValue;
            } else 
            {
                score -= pieceValue;
            }
        }
    }
    
    return score;
}

int ChessEngine::evaluatePieceSquareTables(PieceColor sideToMove, int pieceCount) const
{
    // based entirely on vibes
    // special end game tables for pawns and kings.
    // TODO: end game for light and heavy pieces, but like, idk?
    // TODO: only update changed squares

    // push center pawns, keep pawns around castling area back
    const int PAWN_TABLE_MID[8][8] = 
    {
        { 0,  0,  0,   0,   0,   0,   0,   0},
        {20, 20, 25,  30,  30,  25,  20,  20},
        {15, 15, 20,  20,  20,  20,  15,  15},
        {10, 10, 10,  15,  15,  10,  10,  10},
        {0,   0,  0,  15,  15,   0,   0,   0},
        {0,   5,  5,   0,   0,   5,   5,   0},
        {5,  10, 10, -10, -10,  10,  10,   5},
        {0,   0,  0,   0,   0,   0,   0,   0}
    };

    // PUSH
    const int PAWN_TABLE_END[8][8] = 
    {
        {  0,    0,   0,   0,   0,   0,   0,   0},
        {  60,  60,  60,  60,  60,  60,  60,  60},
        {  40,  40,  40,  40,  40,  40,  40,  40},
        {  25,  25,  25,  25,  25,  25,  25,  25},
        {  15,  15,  15,  20,  20,  15,  15,  15},
        {  0,    0,   0,   0,   0,   0,   0,   0},
        {-10,  -10, -10, -10, -10, -10, -10, -10},
        {  0,    0,   0,   0,   0,   0,   0,   0},
    };

    // guide bishops to center
    const int BISHOP_TABLE_MID[8][8] = 
    {
        { -5, -5, -5, -5, -5, -5, -5, -5},
        { -5,  0,  0,  0,  0,  0,  0, -5},
        { -5,  0,  5, 10, 10,  5,  0, -5},
        { -5, 10, 10, 15, 15, 10, 10, -5},
        { -5,  0, 15, 15, 15, 15,  0, -5},
        { -5,  0,  5, 10, 10,  5,  0, -5},
        { -5, 10,  0,  0,  0,  0, 10, -5},
        { -5, -5, -5, -5, -5, -5, -5, -5}
    };
    
    // guide knights to develop
    const int KNIGHT_TABLE_MID[8][8] = 
    {
        {-10,-10,-10,-10,-10,-10,-10,-10},
        {-10,  0,  0,  0,  0,  0,  0,-10},
        {-10,  0, 10, 10, 10, 10,  0,-10},
        {-10,  0, 10, 20, 20, 10,  0,-10},
        {-10,  0, 10, 20, 20, 10,  0,-10},
        {-10,  0, 10, 10, 10, 10,  0,-10},
        {-10,  0,  0,  0,  0,  0,  0,-10},
        {-10,-10,-10,-10,-10,-10,-10,-10}
    };

    // TODO: make this better?
    const int ROOK_TABLE_MID[8][8] = 
    {
        {10, 10, 10, 10, 10, 10, 10, 10},
        {20, 20, 20, 20, 20, 20, 20, 20},
        { 0,  0,  0,  0,  0,  0,  0,  0},
        { 0,  0,  0,  0,  0,  0,  0,  0},
        { 0,  0,  0,  0,  0,  0,  0,  0},
        { 0,  0,  0,  0,  0,  0,  0,  0},
        { 0,  0,  0,  0,  0,  0,  0,  0},
        { 0,  0,  0, 10, 10,  5,-20, -5}
    };

    // queen in centerish
    const int QUEEN_TABLE_MID[8][8] = 
    {
        { -5, -5, -5, -5, -5, -5, -5, -5},
        { -5,  0,  0,  0,  0,  0,  0, -5},
        { -5,  0,  5,  5,  5,  5,  0, -5},
        { -5,  0,  5, 10, 10,  5,  0, -5},
        { -5,  0,  5, 10, 10,  5,  0, -5},
        { -5,  5,  5,  5,  5,  5,  0, -5},
        { -5,  0,  5,  0,  0,  0,  0, -5},
        { -5, -5, -5, -5, -5, -5, -5, -5}
    };

    // for the love of god castle. do not move your king in the middle
    const int KING_TABLE_MID[8][8] = 
    {
        {-40,-40,-40,-40,-40,-40,-40,-40},
        {-40,-40,-40,-50,-50,-40,-40,-40},
        {-40,-40,-40,-50,-50,-40,-40,-40},
        {-30,-30,-30,-40,-40,-30,-30,-30},
        {-20,-20,-20,-30,-30,-20,-20,-20},
        {-10,-10,-10,-20,-20,-10,-10,-10},
        { 10, 10,  0,-10,-10,  0, 10, 10},
        { 10, 20, 10,-20,-10,  0, 30, 10}
    };


    // guide king to center and up board
    const int KING_TABLE_END[8][8] = 
    {
        {-10,  10, -10, -10, -10, -10, -10, -10},
        {  0,  10,  10,  30,  30,  10,  10,   0},
        {-10,   0,  20,  40,  40,  20,   0, -10},
        {-10,   0,  30,  40,  40,  20,   0, -10},
        {-10,   0,  20,  20,  20,  20,   0, -10},
        {-20, -10,   0,  10,  10,   0, -10, -20},
        {-30, -20, -10,   0,   0, -10, -20, -30},
        {-40, -30, -20, -10, -10, -20, -30, -40},
    };


    int mgScore = 0;
    int egScore = 0;  

    for (int row = 0; row < 8; row++)
    {
        for (int col = 0; col < 8; col++)
        {
            const Piece& piece = board.getPieceConst(row, col);
            if (piece.isEmpty()) continue;

            // Mirror for black
            int r = (piece.getColor() == PieceColor::WHITE) ? row : 7 - row;
            int c = col;

            int mg = 0;
            int eg = 0;

            switch (piece.getType())
            {
                case PieceType::PAWN:
                    mg = PAWN_TABLE_MID[r][c];
                    eg = PAWN_TABLE_END[r][c];
                    break;

                case PieceType::KNIGHT:
                    mg = KNIGHT_TABLE_MID[r][c];
                    eg = mg;
                    break;

                case PieceType::BISHOP:
                    mg = BISHOP_TABLE_MID[r][c];
                    eg = mg;
                    break;

                case PieceType::ROOK:
                    mg = ROOK_TABLE_MID[r][c];
                    eg = mg;
                    break;

                case PieceType::QUEEN:
                    mg = QUEEN_TABLE_MID[r][c];
                    eg = mg;
                    break;

                case PieceType::KING:
                    mg = KING_TABLE_MID[r][c];
                    eg = KING_TABLE_END[r][c];
                    break;
            }

            if (piece.getColor() == sideToMove)
            {
                mgScore += mg;
                egScore += eg;
            }
            else
            {
                mgScore -= mg;
                egScore -= eg;
            }
        }
    }

    // linear game phase blend 
    int blended =
        (
         mgScore * pieceCount + 
         egScore * (MAX_PHASE - pieceCount)
        ) / MAX_PHASE;

    return blended;
}

int ChessEngine::evaluatePawnStructure(PieceColor sideToMove) const
{
 
    // Store all pawns 
    // store pawns by file to check for isolated and doubled pawns
    // store all pawn positions to check for passed pawns
    int score = 0;

    int wpCols[8] = {0,0,0,0,0,0,0,0};
    int bpCols[8] = {0,0,0,0,0,0,0,0};

    std::vector<std::pair<int, int>> wps;
    std::vector<std::pair<int, int>> bps;

    for (int row = 0; row < 8; row++)
    {
        for (int col = 0; col < 8; col++)
        {
            const Piece& piece = board.getPieceConst(row, col);
            if (piece.isEmpty()) continue;

            if (piece.getType() == PieceType::PAWN)
            {
                if (piece.getColor() == PieceColor::WHITE)
                {
                    wpCols[col]++;
                    wps.emplace_back(row, col);
                }
                else
                {
                    bpCols[col]++;
                    bps.emplace_back(row, col);
                }
            }
        }
    }


    // doubled pawns
    for (int col = 0; col < 8; col++)
    {
        if (wpCols[col] > 1)
            score -= 30;
        if (bpCols[col] > 1)
            score += 30;
    }

    // isolated pawns
    for (int col = 0; col < 8; col++)
    {
        if (wpCols[col] > 0)
        {
            if ((col == 0 || wpCols[col - 1] == 0) &&
                (col == 7 || wpCols[col + 1] == 0))
            {
                score -= 20;

                // doubled isolated pawns extra extra penalty
                if (wpCols[col] > 1)
                {
                    score -= 30;
                }
            }
        }

        if (bpCols[col] > 0)
        {
            if ((col == 0 || bpCols[col - 1] == 0) &&
                (col == 7 || bpCols[col + 1] == 0))
            {
                score += 20;

                // doubled isolated pawns extra extra penalty
                if (bpCols[col] > 1)
                {
                    score += 30;
                }                
            }
        }
    }

    // doubled isolated pawns 
    // we need more penalty

    // passed pawns
    // no enemy pawn in left, center, right col

    // white 
    for (auto [row, col] : wps)
    {
        bool passed = true;

        
        int colLeft    = std::max(0, col - 1);
        int colRight   = std::min(7, col + 1);

        
        for (int f = colLeft; f <= colRight; f++)
        {
            if (!passed) break;

            // white pawn starts from row and moves up towards row 0
            for (int r = row; r > 0; r--)
            {
                const Piece& p = board.getPieceConst(r, f);
                if (!p.isEmpty() &&
                    p.getType() == PieceType::PAWN &&
                    p.getColor() == PieceColor::BLACK)
                {
                    passed = false;
                    break;
                }
            }
        }

        if (passed)
        {
            score += 50;
        }
    }

    // Black passed pawns
    for (auto [row, col] : bps)
    {
        bool passed = true;

        int colLeft = std::max(0, col - 1);
        int colRight = std::min(7, col + 1);

        for (int f = colLeft; f <= colRight; f++)
        {
            if (!passed) break;

            // black pawn starts from row and moves down towards row 7
            for (int r = row + 1; r < 8; r++)
            {
                const Piece& p = board.getPieceConst(r, f);
                if (!p.isEmpty() &&
                    p.getType()  == PieceType::PAWN &&
                    p.getColor() == PieceColor::WHITE)
                {
                    passed = false;
                    break;
                }
            }
        }

        if (passed)
        {
            score -= 50;
        }
    }   


    // protected pawn structure
    // if pawn is off the starting row, and is protected by another pawn, small bonus  

    // white pawns  
    for (auto [row, col] : wps) 
    { 
        if (row >= 6) 
            continue; // starting row or behind

        // check left diagonal
        if (col > 0 && row < 7)
        {
            const Piece& p = board.getPieceConst(row + 1, col - 1);
            if (!p.isEmpty() &&
                p.getType() == PieceType::PAWN &&
                p.getColor() == PieceColor::WHITE)
            {
                score += 15;
                continue;
            }
        }

        // check right diagonal
        if (col < 7 && row < 7)
        {
            const Piece& p = board.getPieceConst(row + 1, col + 1);
            if (!p.isEmpty() &&
                p.getType() == PieceType::PAWN &&
                p.getColor() == PieceColor::WHITE)
            {
                score += 15;
                continue;
            }
        }
    }

    // black pawns  
    for (auto [row, col] : bps) 
    { 
        if (row <= 1) 
            continue; 

        // check left diagonal
        if (col > 0 && row > 0)
        {
            const Piece& p = board.getPieceConst(row - 1, col - 1);
            if (!p.isEmpty() &&
                p.getType() == PieceType::PAWN &&
                p.getColor() == PieceColor::BLACK)
            {
                score -= 15;
                continue;
            }
        }

        // check right diagonal
        if (col < 7 && row > 0)
        {
            const Piece& p = board.getPieceConst(row - 1, col + 1);
            if (!p.isEmpty() &&
                p.getType() == PieceType::PAWN &&
                p.getColor() == PieceColor::BLACK)
            {
                score -= 15;
                continue;
            }
        }
    }




    // Idk how else to when pawns are involved
    // score is positive if white is better. if sideToMove is white. dont change
    // score is negative if black is better. if sideToMove is black, flip
    score = (sideToMove == PieceColor::WHITE) ? score : -score;
    return score;
}

int ChessEngine::evaluatePieceMobility(PieceColor sideToMove, int pieceCount) const
{
    int score = 0;

    std::vector<Move> moves;
    moves.reserve(27);  // Queen can have a max of 27   


    for (int row = 0; row < 8; row++)
    {
        for (int col = 0; col < 8; col++)
        {
            const Piece& piece = board.getPieceConst(row, col);

            if (piece.isEmpty()) continue;

            auto moves = board.generatePseudoLegalMoves(row, col);

            int mobility = static_cast<int>(moves.size());

            // weight by type
            switch (piece.getType())
            {
                case PieceType::KNIGHT: mobility *= 4; break;
                case PieceType::BISHOP: mobility *= 3; break;
                case PieceType::ROOK:   mobility *= 2; break;
                case PieceType::QUEEN:  mobility *= 1; break;
                case PieceType::KING:  
                    
                    int blended;
                    // linear game phase blend 
                    blended =
                    (
                    -2 * pieceCount + 
                     4 * (MAX_PHASE - pieceCount)
                    ) / MAX_PHASE;
                            
                    mobility *= blended; 
                    break;

                default: break;
            }



            if (piece.getColor() == sideToMove)
            {
                score += mobility;
            }
            else
            {
                score -= mobility;
            }
        }
    }

    return score;
}

int ChessEngine::evaluateKingSafety(PieceColor sideToMove, int pieceCount) const
{

    int score = 0;

    int wkRow = board.getWhiteKingRow();
    int wkCol = board.getWhiteKingCol();
    int bkRow = board.getBlackKingRow();
    int bkCol = board.getBlackKingCol();


    // white king pawn shield
    // no matter where the king is, want to have a pawn shield in midgame
    for (int dc = -1; dc <= 1; dc++)
    {
        int c = wkCol + dc;
        if (c < 0 || c > 7) continue;

        // white pawns in front of king are "up" the board ... lesser index
        int r = wkRow - 1;
        if (r < 0 || r > 7) continue;

        const Piece& p = board.getPieceConst(r, c);
        if (!p.isEmpty()  &&
             p.getType()  == PieceType::PAWN &&
             p.getColor() == PieceColor::WHITE)
        {
            score += 20;
        }
    }

    // black king pawn shield
    for (int dc = -1; dc <= 1; dc++)
    {
        int c = bkCol + dc;
        if (c < 0 || c > 7) continue;

        // black pawns in front of king are "down" the board ... greater index
        int r = bkRow + 1;
        if (r < 0 || r > 7) continue;   

        const Piece& p = board.getPieceConst(r, c);
        if (!p.isEmpty() &&
            p.getType() == PieceType::PAWN &&
            p.getColor() == PieceColor::BLACK)
        {
            score -= 20;
        }
    }

    // open files 
    // check rook and queen lines 

    // white  
    for (int dc = -1; dc <= 1; dc++)
    {
        int c = wkCol + dc;
        if (c < 0 || c > 7) continue;

        // open file is no pawns, semi open is no white pawn
        bool wp = false;
        bool bp = false;

        // upwards for white king
        for (int r = wkRow - 1; r >= 0; r--)
        {
            const Piece& p = board.getPieceConst(r, c);
            
            if (p.isEmpty())
            {
                continue;
            }
            if (p.getType() == PieceType::PAWN)
            {
                if (p.getColor() == PieceColor::WHITE)
                    wp = true;
                else
                    bp = true;
            }
        }

        if (!wp && !bp)
        {
            score -= 50;
        }
        else if (!wp)
        {
            score -= 25;
        }
    }

    // black  
    for (int dc = -1; dc <= 1; dc++)
    {
        int c = bkCol + dc;
        if (c < 0 || c > 7) continue;

        // open file is no pawns, semi open is no black pawn
        bool wp = false;
        bool bp = false;

        // downwards for black king
        for (int r = bkRow + 1; r < 8; r++)
        {
            const Piece& p = board.getPieceConst(r, c);
            
            if (p.isEmpty())
            {
                continue;
            }
            if (p.getType() == PieceType::PAWN)
            {
                if (p.getColor() == PieceColor::WHITE)
                    wp = true;
                else
                    bp = true;
            }
        }

        if (!wp && !bp)
        {
            score += 50;
        }
        else if (!bp)
        {
            score += 25;
        }
    }

      
    // Idk how else to when pawns are involved
    // score is positive if white is better. if sideToMove is white. dont change
    // score is negative if black is better. if sideToMove is black, flip
    score = (sideToMove == PieceColor::WHITE) ? score : -score;

    // multiply before dividing: (pieceCount / MAX_PHASE) is integer math and
    // truncates to 0 for anything below full material, zeroing the whole term
    return score * pieceCount / MAX_PHASE; // scale by game phase
}

int ChessEngine::evaluateKingRestriction(PieceColor sideToMove, int pieceCount) const
{
    // restrict enemy king movement in endgame
    // incentivize reducing the enemy kings available squares
    // incentivize the enemy king being on the edge of the board 
    int egScore = 0;

    int wkRow = board.getWhiteKingRow();
    int wkCol = board.getWhiteKingCol();
    int bkRow = board.getBlackKingRow();
    int bkCol = board.getBlackKingCol();

    int enemyKingRow, enemyKingCol;
    enemyKingRow = (sideToMove == PieceColor::WHITE) ? bkRow : wkRow;
    enemyKingCol = (sideToMove == PieceColor::WHITE) ? bkCol : wkCol;

    PieceColor enemyColor = (sideToMove == PieceColor::WHITE) ? PieceColor::BLACK : PieceColor::WHITE;

    // Generate king moves 
    static const int dirs[8][2] = 
    { 
      {1, 0},{-1, 0},{0, 1},{ 0,-1}, 
      {1, 1},{ 1,-1},{-1,1},{-1,-1} 
    };


    int legalMoves = 0;

    for (auto dir: dirs)
    {
        int newRow = enemyKingRow + dir[0];
        int newCol = enemyKingCol + dir[1];

        if (newRow < 0 || newRow > 7 || newCol < 0 || newCol > 7)
            continue;

        const Piece& target = board.getPieceConst(newRow, newCol);
        
        if (!target.isEmpty() && target.getColor() == enemyColor)
            continue;


        // see if square is attacked
        if (!board.isSquareUnderAttack(newRow, newCol, sideToMove))
        {
            legalMoves++;
        }


    }

    egScore = (8 - legalMoves) * 20;

     // guide king to edges 
    const int ENEMY_KING_TABLE_END[8][8] = 
    {
        {100,  60,  60,  60,  60,  60,  80, 100},
        { 80,  20,  20,  20,  20,  20,  20,  80},
        { 60,  20, -20, -20, -20, -20,  20,  60},
        { 60,  20, -20, -60, -60, -20,  20,  60},
        { 60,  20, -20, -60, -60, -20,  20,  60},
        { 60,  20, -20, -20, -20, -20,  20,  60},
        { 80,  20,  20,  20,  20,  20,  20,  80},
        {100,  60,  60,  60,  60,  60,  80, 100},
    };

    egScore += ENEMY_KING_TABLE_END[enemyKingRow][enemyKingCol];


    // only applies in endgame
    int mgScore = 0;


    // linear game phase blend 
    // all pieces on board: score = mgScore
    // no pieces on board:  score = egScore
    int score =
        (
         mgScore * pieceCount + 
         egScore * (MAX_PHASE - pieceCount)
        ) / MAX_PHASE;  


    return score;

}

int ChessEngine::evaluateKingToKingDistance(PieceColor sideToMove, int pieceCount) const
{
    // incentivize bringing own king closer to enemy king in endgame    
    int score = 0;  

    int wkRow = board.getWhiteKingRow(); 
    int wkCol = board.getWhiteKingCol();
    int bkRow = board.getBlackKingRow();    
    int bkCol = board.getBlackKingCol();
  
    int rowDiff = std::abs(wkRow - bkRow);
    int colDiff = std::abs(wkCol - bkCol);


    // LERP  A * (1-t) + B * t
    // kings two squares away: k2kScore = 250, 
    // kings opposite corners: k2kScore = -350    
    int distance = rowDiff + colDiff;
    int const MAX_DIST = 14;
    int const A = 25;
    int const B = -25;
    int egScore = A * (MAX_DIST - distance) + B * distance;
    
    
    // only applies in endgame
    int mgScore = 0;


    // linear game phase blend 
    // all pieces on board: score = mgScore
    // no pieces on board:  score = egScore
    score =
        (
         mgScore * pieceCount + 
         egScore * (MAX_PHASE - pieceCount)
        ) / MAX_PHASE;    
    
    return score;
}

int ChessEngine::evaluateKPKEndgame(PieceColor sideToMove, int pieceCount) const
{
    // check for king and pawn vs king endgame
    // 1. can the defender catch the pawn
    // 2. is the defender in front of the pawn
    // 3. is the friendly king close enough to help 
    // 4. otherwise win.

    // TODO: i think the only checks that matter are 1. can the defender catch the pawn ? draw : win
    
    if (pieceCount > 0) 
        return 0; // don't care about non KPK endgames

    // pawn count 
    if (getPawnCount() > 1) 
        return 0; // TODO: maybe evaluate KPPK?

    // find pawn
    int pawnRow = -1;
    int pawnCol = -1;
    for (int row = 0; row < 8; row++)
    {
        for (int col = 0; col < 8; col++)
        {
            const Piece& piece = board.getPieceConst(row, col);
            if (!piece.isEmpty() &&
                piece.getType() == PieceType::PAWN)
            {
                pawnRow = row;
                pawnCol = col;
                break;
            }
        }
        if (pawnRow != -1) 
            break;
    }

    if (pawnRow == -1 || pawnCol == -1) 
    {
        std::cout << "Error in evaluateKPKEndgame: no pawn found\n";  
        return 0; // shouldnt get here 
    }    
    
    
    Piece pawnPiece = board.getPieceConst(pawnRow, pawnCol);

    // only care about friendly pawn 
    if (pawnPiece.getColor() != sideToMove) 
        return 0;

    int friendlyKingRow = (sideToMove == PieceColor::WHITE) ? board.getWhiteKingRow() : board.getBlackKingRow();
    int friendlyKingCol = (sideToMove == PieceColor::WHITE) ? board.getWhiteKingCol() : board.getBlackKingCol();

    int enemyKingRow = (sideToMove == PieceColor::WHITE) ? board.getBlackKingRow() : board.getWhiteKingRow();
    int enemyKingCol = (sideToMove == PieceColor::WHITE) ? board.getBlackKingCol() : board.getWhiteKingCol();

    
    int pawnMovesToPromotion = (sideToMove == PieceColor::WHITE) ? pawnRow : (7 - pawnRow);
    int pawnPromotionCol = pawnCol;
    int pawnPromotionRow = (sideToMove == PieceColor::WHITE) ? 0 : 7;

    int enemyKingToPawnDist = std::max(std::abs(enemyKingRow - pawnRow), std::abs(enemyKingCol - pawnCol));
    

    // 1. can the defender catch the pawn  
    if (enemyKingToPawnDist <= pawnMovesToPromotion)
        return 0; // defender can catch the pawn


    // 2. is the defender in front of the pawn
    if (abs(pawnCol - enemyKingCol) <= 1) // enemy king close to pawn file
    {
        // same file
        if (sideToMove == PieceColor::WHITE && enemyKingRow <= pawnRow)
            return 0; // defender is in front of pawn  
    
        else if (enemyKingRow >= pawnRow)
            return 0; // defender is in front of pawn
    }

    // 3. is friendly king is closer than enemy king, can probably promote
    int friendlyKingToPawnDist = std::max(std::abs(friendlyKingRow - pawnRow), std::abs(friendlyKingCol - pawnCol));
    if (friendlyKingToPawnDist < enemyKingToPawnDist)
    {
        return WINNING_KPK_EVAL; // likely win
    }

    // 4. just win
    return WINNING_KPK_EVAL;
}


void ChessEngine::getOrderedMoves(std::vector<Move>& moves, int ply) const
{
    // replaced MVV-LVA with SEE
    // order
    // previous best / hash
    // good SEE captures 
    //  promotions
    // equal SEE captures 
    // killer 
    // any history heuristic    
    // score 0
    // bad captures


    std::vector<std::pair<Move, int>> moveScores;                 
    moveScores.reserve(moves.size()); //preallosaurus 

    int score = 0;  

    for (const Move& move : moves)  
    {
        score = 0;  
   

        bool isCapture   = !board.getPieceConst(move.getToRow(),move.getToCol()).isEmpty();  
        bool isEnPassant = move.isEnPassantMove();
        bool isPromotion = move.isPawnPromotion(board);
   
        // If there is a capture, use MVV-LVA
        // otherwise use killer moves   
        // then history heuristic   
        if (move == moveOrderingPreviousBestMove)
        {
            score = MOVE_ORDERING_PREVIOUS_BEST_MOVE;
        }
        else if (isPromotion) 
        { 
            score = MOVE_ORDERING_PROMOTION;
        }    
        else if (isEnPassant) 
        {     
            score = MOVE_ORDERING_SEE_EQUAL_CAPTURE;      
        }          
        else if (isCapture)  
        {
            int seeGain = SEE(move);
            if (seeGain > 0)
            {
                score = MOVE_ORDERING_SEE_GOOD_CAPTURE + seeGain;
            }
            else if (seeGain == 0)
            {
                score = MOVE_ORDERING_SEE_EQUAL_CAPTURE;
            }
            else
            {
                score = MOVE_ORDERING_SEE_BAD_CAPTURE + seeGain;
            }
             
        }     
        else
        {
            //killer moves
            if (move == killerMove1[ply] && USE_KILLER_MOVE_HEURISTIC)
            {
                score = MOVE_ORDERING_KILLER_MOVE_1;
            } 
            else if (move == killerMove2[ply] && USE_KILLER_MOVE_HEURISTIC) 
            {
                score = MOVE_ORDERING_KILLER_MOVE_2; 
            }   
            else if (USE_HISTORY_HEURISTIC)
            {
                // the history heuristic hysteresis habanero
                // she history on my heuristic till i hysteresis
                int fromSquare = move.getFromRow() * 8 + move.getFromCol();
                int toSquare   = move.getToRow()   * 8 + move.getToCol();
                int side = (board.getCurrentTurn() == PieceColor::WHITE) ? 0 : 1; 

                score += historyHeuristicsHysteresis[side][fromSquare][toSquare]; 
            }
        }

        moveScores.emplace_back(move, score);
    }

    // // DEBUG ==============
    // // DEBUG ==============

    
    // std::vector<std::pair<Move, int>> moveScores;                 
    // moveScores.reserve(moves.size());  

    // int debugScore = 100;
    // for (const Move& move : moves)  
    // {
    //     debugScore++;
    //     moveScores.emplace_back(move, debugScore);
    // }

    // // DEBUG ==============
    // // DEBUG ==============


    // sort moves
    std::sort(moveScores.begin(), moveScores.end(),
              [](const std::pair<Move, int>& a, 
                 const std::pair<Move, int>& b)
              {
                  return a.second > b.second;
              });

    // reorder moves
    for (size_t i = 0; i < moves.size(); i++)
    {
        moves[i] = moveScores[i].first;
    }

}

int ChessEngine::qSearch(int alpha, int beta, int qPly)
{
    assert(qPly >= 0 && qPly < QUIESCENCE_SEARCH_MAX_PLY);


    //timeout
    if (searchTimeout)  
    { 
        return 0;
    } 
 
    if (timeExceeded())  
    {   
        std::cout << "Search timeout at qSearch ply " << qPly << "\n";          
        searchTimeout = true;   
        return 0;      
    } 
           

    qNodes++;
    // evaluate board
    // if eval is too good and exceeds beta, return beta, enemy wont pick
    // if eval is better than alpha, raise alpha
    // generate just captures (at 6M NPS not fast enough to do checks too)
    
    // evaluateBoard() is too slow for quiencescence search
    // just use raw material to make sure capture chains arent missed


    // Endings 
        if (!board.isLegalMoveAvailable())
        {
            if (board.isInCheck(board.getCurrentTurn()))
            {
                // + ply to end sooner
                return -10000 + qPly;
            }
            else
            {
                // stalemate
                return 0;
            }
        }
        if (!board.isSufficientMaterial() || board.getFiftyMoveClock() >= 100)    
        {
            // stalemate
            // TODO: add 3 fold repetition    
            return 0;
        }



    int currentEval = evaluateBoard();
    // PieceColor sideToMove = board.getCurrentTurn();
    // int currentEval = rawPieceTotal(sideToMove);

    // cut off because otherwise search blows up to 30+seconds
    if (qPly >= QUIESCENCE_SEARCH_MAX_PLY)
        return currentEval;


    // beta cutoff
    if (currentEval >= beta)
        return currentEval;

    // alpha update
    if (currentEval > alpha)
        alpha = currentEval;

    // Generate only captures
    std::vector<Move> moves = board.generateQSearchMoves();

    for (const Move& move : moves)
    {
        
        // SEE pruning (a "better" version of delta pruning) aka nightmare implementation
        if (USE_SEE_EVALUATION)
        {
            int seeGain = SEE(move);
            if (seeGain < 0)
                {
                    seeCutqNodes++;
                    continue; // bad capture chain for us
                }
        }
        Board::UndoInfo undo;
        
        board.makeUncheckedMove(move, undo, true);
       

        // legality check, same as in negamax 
        PieceColor movedSide = undo.previousTurn;      
        if (board.isInCheck(movedSide)) 
        {   
            board.undoMove(move, undo);
            continue; // illegal capture 
        }


        int score = -qSearch(-beta, -alpha, qPly + 1);

        board.undoMove(move, undo);

        if (score >= beta)
            return beta;

        if (score > alpha)
            alpha = score;
    }

    return alpha;
}

int ChessEngine::negamax(int numPlyLeft, int alpha, int beta, int plyCount)
{
    assert(plyCount < MAX_PLY - 1);

    //timeout
    if (searchTimeout)  
    { 
        return 0;
    } 
 
    if (timeExceeded())  
    {   
        std::cout << "Search timeout at negamax ply " << plyCount << " of " << numPlyLeft + plyCount << "\n";    
        searchTimeout = true; 
        return 0;     
    } 
   
    // count nodes  
    totalNodes++;  
    nodesPerDepth[plyCount]++;


    // Endings 
    if (!board.isLegalMoveAvailable())
    {
        if (board.isInCheck(board.getCurrentTurn()))
        {
            // + ply to end sooner
            return -10000 + plyCount;
        }
        else
        {
            // stalemate
            return 0;
        }
    }
    if (!board.isSufficientMaterial() || board.getFiftyMoveClock() >= 100)    
    {
        // stalemate
        // TODO: add 3 fold repetition    
        return 0;
    }

    // Determine phase of game
    int pieceCount = nonPawnMaterialCount();
    bool isEndgamePhase = false;
    if (pieceCount < ENDGAME_PHASE_MATERIAL_THRESHOLD)
    {
        inEndgamePhase = true;
    }



    if (numPlyLeft == 0)
    {
        if (USE_QUIESCENCE_SEARCH)
        {
            //int qScore = qSearch(alpha, beta, 0);  
            //qNodesPerDepth[numPlyLeft] = qNodes;
            return qSearch(alpha, beta, 0);
        }
        else
        {
            //PieceColor sideToMove = board.getCurrentTurn();   
            return evaluateBoard();  // evaluate all raw material on from side to move perspective  
        }    
    }

    int bestScore = -99999;

    Move bestMove(-1, -1, -1, -1);

    bool foundLegalMove = false;
    

    // null move pruning -------------------------------------
    // if doing nothing is pretty good, dont search this node
    // only run when depth > 2, not in check, not pawn endgame
    // zugzwang issues if only pawns             
    if (USE_NULL_MOVE_PRUNING && !inEndgamePhase)
    {
        if (numPlyLeft >= NULL_MOVE_PRUNE_MIN_DEPTH && 
            !board.isInCheck(board.getCurrentTurn()))
        {
            // make null move
            Board::UndoInfo undoNullMove;
            board.makeNullMove(undoNullMove);

            // -1 reduction for standard minimax depth reduction
            // NULL_MOVE_DEPTH_REDUCTION, kind of arbitrary
            int score = -negamax(numPlyLeft - 1 - NULL_MOVE_DEPTH_REDUCTION, -beta, -beta + 1, plyCount + 1);     
   
            // undo null move  
            board.undoNullMove(undoNullMove);  
  
            if (score >= beta)    
            {
                // track cut nodes     
                cutNodes++;    
                cutNodesPerDepth[plyCount]++;      
                nullMoveCutNodes++;         
                nullMoveCutNodesPerDepth[plyCount]++;               
              
                return score;  
            }
        }
    }
    // null move pruning -------------------------------------




    // i cant believe modern engines generate only pseudolegalmoves
    // on first glance this is insane but 
    // the search tree must grow
    std::vector<Move> moves = board.generateAllPseudoLegalMoves();
    
    if (USE_MOVE_ORDERING)   
        getOrderedMoves(moves, plyCount); 

    // LMR move index
    int moveIndex = 0;  
    for (const Move& move : moves)
    {        



        bool isCapture = (board.getPieceConst(move.getToRow(), move.getToCol()).getType() != PieceType::EMPTY) || move.isEnPassantMove();  // for LMR       


        Board::UndoInfo undo;
        board.makeUncheckedMove(move, undo, true);
        
        // generating pseudolegal so have to check is move that was made resulted in the king being exposed to check
        PieceColor movedSide = undo.previousTurn;
        if (board.isInCheck(movedSide))
        {
            board.undoMove(move, undo);
            continue; // illegal move
        }
        foundLegalMove = true;

        // futility move pruning  -------------------------------------
        // at a depth of 1, a quiet move cannot reasonably raise alpha 
        // if board state + some margin is still less than alpha        
        // dont prune in endgame with low material, can miss a quiet pawn push that is really good
        if (USE_FUTILITY_PRUNING && !inEndgamePhase)
        {
            // get board state info
            int boardEvaluationBeforeMove = evaluateBoard();                 
            int nonPawnMaterialBeforeMove = nonPawnMaterialCount();            
            bool futilityPruneCandidate   = isFutilityPruneCandidate(move);

            if (futilityPruneCandidate &&
                numPlyLeft == 1 && 
                boardEvaluationBeforeMove + FUTILITY_PRUNE_MARGIN <= alpha &&       
                nonPawnMaterialBeforeMove > FUTILITY_PRUNE_MIN_NON_PAWN_MATERIAL)       
            {  
                // print info about 
                // log          
                cutNodes++;  
                cutNodesPerDepth[plyCount]++;          
                futilityPruneNodes++;         
                futilityPruneNodesPerDepth[plyCount]++;                             

                // undo 
                board.undoMove(move, undo);              
                continue; 
                
            }
        }
         // futility move pruning  -------------------------------------                
 



        
        int score;

        if (USE_LATE_MOVE_REDUCTION && !isEndgamePhase)   
        {             

            // late move reductions        
            bool isKillerMove = (move == killerMove1[plyCount] || move == killerMove2[plyCount]);        

            if (numPlyLeft >= LATE_MOVE_REDUCTION_MIN_DEPTH && !isCapture && !isKillerMove && moveIndex >= LATE_MOVE_REDUCTION_MIN_MOVE_ORDER)  
            {
                // LMR  
                // first do a reduced search     
                score = -negamax(numPlyLeft - 1 - LATE_MOVE_REDUCTION_REDUCTION_AMOUNT, -alpha - 1, -alpha, plyCount + 1);     
                
                // if it fails high, do a full depth search 
                if (score > alpha)
                {  
                    score = -negamax(numPlyLeft - 1, -beta, -alpha, plyCount + 1);  
                }                             
            }        
            else
            {
                score = -negamax(numPlyLeft - 1, -beta, -alpha, plyCount + 1);  
            }             
        }
        else
        {
            score = -negamax(numPlyLeft - 1, -beta, -alpha, plyCount + 1);  
        }

        bool wasCapture = (undo.captured.getType() != PieceType::EMPTY);

        board.undoMove(move, undo);

        // record info about this iteration
        if (score > bestScore)
        {
            bestScore = score;
            bestMove  = move;
        }


        // found better move that increases alpha
        if (score > alpha)
        {
            alpha = score;

            // PV update at interior node
            updatePvLine(plyCount, move);

        }

        if (alpha >= beta)
        {
            cutNodes++;
            cutNodesPerDepth[plyCount]++;
            // killer move update for non captures
            if (isQuietMove(move))
            {
                if (killerMove1[plyCount] != move && 
                    USE_KILLER_MOVE_HEURISTIC)
                {
                    killerMove2[plyCount] = killerMove1[plyCount];
                    killerMove1[plyCount] = move;
                }
                if (USE_HISTORY_HEURISTIC)
                {
                    int fromSquare = move.getFromRow() * 8 + move.getFromCol();
                    int toSquare   = move.getToRow()   * 8 + move.getToCol();
                    
                    int side = (board.getCurrentTurn() == PieceColor::WHITE) ? 0 : 1;

                    historyHeuristicsHysteresis[side][fromSquare][toSquare] += numPlyLeft * numPlyLeft;
                }

            }

            break; // beta cutoff

        }
        moveIndex++; // LMR  
    }

    // fall back 
    if (!foundLegalMove)
    {
        std::cout << "WARNING: No legal moves found at depth " << numPlyLeft << "\n";
        return evaluateBoard(); 
    }    

    return bestScore;
}


int ChessEngine::SEE(const Move& move) const
{

    // initial move info
    int toRow   = move.getToRow();
    int toCol   = move.getToCol();
    int fromRow = move.getFromRow();
    int fromCol = move.getFromCol();

    assert(toRow >=0 && toRow <8 && toCol >=0 && toCol <8);
    assert(fromRow >=0 && fromRow <8 && fromCol >=0 && fromCol <8);


    const Piece& movingPiece   = board.getPieceConst(fromRow, fromCol);
    const Piece& capturedPiece = board.getPieceConst(toRow, toCol);

    PieceColor color = movingPiece.getColor();
    PieceColor enemyColor = (color == PieceColor::WHITE) ? PieceColor::BLACK : PieceColor::WHITE;

    if (capturedPiece.getType() == PieceType::EMPTY)
        return 0; // TODO: do something about en passant

    // Track gains 
    int gain[32]; // max of 32 exchanges. after that just give up
    int depth = 0;


    // Temporary board copy for minisim
    Board tempBoard = board;    
    PieceColor tempSideToMove = board.getCurrentTurn();

    // simulate the initial capture
    // initial gain is value of captured piece
    gain[0] = pieceValue(capturedPiece.getType());

    tempBoard[toRow][toCol] = movingPiece; // move attacker to capture square    
    tempBoard[fromRow][fromCol] = Piece(); // empty the from square  
    tempSideToMove = (tempSideToMove == PieceColor::WHITE) ? PieceColor::BLACK : PieceColor::WHITE; //flip  
    
    PieceType pieceToBeCaptured = movingPiece.getType();    

        
    // list of attackers and defenders
    // whiteAttackPieces and blackAttackPieces is easier to keep track of 
    std::vector<Piece> whiteAttackPieces;
    std::vector<Piece> blackAttackPieces;
    getAllAttackersOfSquare(toRow, toCol, tempBoard, whiteAttackPieces, blackAttackPieces); // i am steadily losing my mind

    auto cmp = [this](const Piece& a, const Piece& b) {
        return pieceValue(a.getType()) < pieceValue(b.getType());  
    };    
        
    std::sort(whiteAttackPieces.begin(), whiteAttackPieces.end(), cmp);
    std::sort(blackAttackPieces.begin(), blackAttackPieces.end(), cmp);
  
 
           
    while (true) 
    {
       
        // get current side's attackers
        // get least valuable attacking piece
        // calculate gain
        // simulate capture
        // flip side        
        // get new attackers

        // get current side's attackers        
        std::vector<Piece>& currentAttackers = (tempSideToMove == PieceColor::WHITE) ? whiteAttackPieces : blackAttackPieces;
        
        if (currentAttackers.empty())
            break; // no more attackers

        // get least valuable attacking piece
        Piece attacker = currentAttackers.front();
        currentAttackers.erase(currentAttackers.begin());

        // calculate gain
        depth++;

        if (depth >= 16)
        {
            depth--; // gain[depth] not written this iteration; unwind starts at the last written entry
            break;
        }

        gain[depth] = pieceValue(pieceToBeCaptured) - gain[depth - 1];

        // simulate capture
        pieceToBeCaptured = attacker.getType();

        int attackerFromRow = attacker.getRow();
        int attackerFromCol = attacker.getCol();

        assert(attackerFromRow >=0 && attackerFromRow <8 && attackerFromCol >=0 && attackerFromCol <8);

        // add attacker to capture square  
        tempBoard[toRow][toCol] = attacker; // move attacker to capture square
        // update attacker internal postion
        tempBoard[toRow][toCol].setPosition(toRow, toCol); // update attacker position. thid might not matter but see is broken atm

        // empty atttacker from square     
        if (attackerFromRow < 0 || attackerFromRow >= 8 || attackerFromCol < 0 || attackerFromCol >= 8)
        {
            // should never happen
            std::cout << "SEE attacker from square invalid\n"
                      << "attacker type: " << pieceTypeToIndex[(int)(attacker.getType())] << "\n"
                      << "attacker color: " << static_cast<int>(attacker.getColor()) << "\n"
                      << "attacker from row: " << attackerFromRow << "\n"
                      << "attacker from col: " << attackerFromCol << "\n";

        }
        tempBoard[attackerFromRow][attackerFromCol] = Piece(); // empty the from square



        // flip side        
        tempSideToMove = (tempSideToMove == PieceColor::WHITE) ? PieceColor::BLACK : PieceColor::WHITE; //flip

        // get new attackers
        whiteAttackPieces.clear();
        blackAttackPieces.clear();   

        getAllAttackersOfSquare(toRow, toCol, tempBoard, whiteAttackPieces, blackAttackPieces);  

        std::sort(whiteAttackPieces.begin(), whiteAttackPieces.end(), cmp);  
        std::sort(blackAttackPieces.begin(), blackAttackPieces.end(), cmp);    
            
           
    } 
    // no more attackers
    // either side may stop the exchange early
    // determine stopping point

    // for (int d = depth - 1; d >= 0; d--)
    // {
    //     // gain[i]    is the value if side stops after move i
    //     // -gain[i+1] is the value if the continues and lets the opponent reply
    //     gain[d] = std::max( gain[d], -gain[d + 1]);
    // }
    // fold from the deepest exchange down: gain[d-1] = min(gain[d-1], -gain[d])
    // the loop must start at the last written entry (depth), otherwise the
    // final recapture never counts and e.g. QxP defended by a pawn scores +100
    while (depth > 0)
    {
        if (gain[depth-1] > -1*gain[depth])
        {
            gain[depth-1] = -1*gain[depth];
        }
        depth--;
    }

    return gain[0];
}


void ChessEngine::getAllAttackersOfSquare(int toRow, int toCol, Board passedInBoard, std::vector<Piece>& whiteAttackPieces,  std::vector<Piece>& blackAttackPieces ) const
{
    
    // from the capture square, look for all attackers and defenders
    // simulate all exchanges starting from least valuable pieces first
    //

    // Pawn attacks 
    // white pawn attacks 
    // from the perspective of the to square. a white pawn attacks from below left and below right    
    if (toRow + 1 <= 7) // even tho a white pawn will never be on the 8th rank, 7 index, in a legal position. we are not looking for legal posiitons here.  
    {
        if (toCol + 1 <= 7)
        {
            const Piece& p = passedInBoard.getPieceConst(toRow + 1, toCol + 1); 
            if (!p.isEmpty() && p.getType() == PieceType::PAWN && p.getColor() == PieceColor::WHITE)
                whiteAttackPieces.push_back(p);
        }
        if (toCol - 1 >= 0)
        {  
            const Piece& p = passedInBoard.getPieceConst(toRow + 1, toCol - 1);
            if (!p.isEmpty() && p.getType() == PieceType::PAWN && p.getColor() == PieceColor::WHITE)
                whiteAttackPieces.push_back(p);
        }
    }

    // black pawn attacks
    // from the perspective of the to square. a black pawn attacks from above left and above right 
    if (toRow - 1 >= 0)  // same thing here.            
    { 
        if (toCol - 1 >= 0) 
        {         
            const Piece& p = passedInBoard.getPieceConst(toRow - 1, toCol - 1);  
            if (!p.isEmpty() && p.getType() == PieceType::PAWN && p.getColor() == PieceColor::BLACK) 
                blackAttackPieces.push_back(p);  
        }  
        if (toCol + 1 <= 7)  
        {                    
            const Piece& p = passedInBoard.getPieceConst(toRow - 1, toCol + 1);  
            if (!p.isEmpty() && p.getType() == PieceType::PAWN && p.getColor() == PieceColor::BLACK)  
                blackAttackPieces.push_back(p);  
        }
    }

    // knight attacks 
    static const int knightMoveDirections[8][2] =   
    {
        {-2, -1}, {-2,  1}, {-1, -2}, {-1,  2},
        { 1, -2}, { 1,  2}, { 2, -1}, { 2,  1}
    };   
       
    for (const auto& dir : knightMoveDirections)  
    {
        int r = toRow + dir[0];
        int c = toCol + dir[1];
       
        if (r < 0 || r > 7 || c < 0 || c > 7) continue;               
            
        const Piece& p = passedInBoard.getPieceConst(r, c);         
        if (!p.isEmpty() && p.getType() == PieceType::KNIGHT)      
        {    
            if (p.getColor() == PieceColor::WHITE)      
                whiteAttackPieces.push_back(p);          
            else
                blackAttackPieces.push_back(p);     
        }    
    } 
   
    // sliders    
    static const int bishopMoveDirections[4][2] =     
    {
        {-1, -1}, {-1,  1},{ 1, -1}, { 1,  1}
    };        
    static const int rookMoveDirections[4][2] = 
    {
        {-1,  0}, { 1,  0}, { 0, -1}, { 0,  1}
    };    
    

    // just keep searching to end of board or until blocked    
    // the x-ray attacks will be potentially out of order but whatever
    // TODO: fix x-ray attacks order later         
    // TODO: SEE now does sim so x-rays are ok. but sim is slow. fix later         
    for (const auto& dir : bishopMoveDirections)     
    {     
        int r = toRow;  
        int c = toCol;   
        // PieceColor pieceFoundColor = PieceColor::NONE;    
        
        while (true)  
        {  
            r += dir[0]; 
            c += dir[1]; 

            if (r < 0 || r > 7 || c < 0 || c > 7) break;    
  
            const Piece& p = passedInBoard.getPieceConst(r, c);    
            if (!p.isEmpty())    
            { 
                if (p.getType() == PieceType::BISHOP || p.getType() == PieceType::QUEEN)
                {     
                    if (p.getColor() == PieceColor::WHITE)
                        {
                        whiteAttackPieces.push_back(p);
                        // pieceFoundColor = p.getColor();     
                        }
                    else  
                        {
                        blackAttackPieces.push_back(p);
                        // pieceFoundColor = p.getColor();   
                        }                      
                }
                break; // blocked. only get first bishop or queen
            }  
        } 
    }  

   
    // rooks and queens
    for (const auto& dir : rookMoveDirections)     
    { 
        int r = toRow; 
        int c = toCol;   
  
        while (true)    
        {   
            r += dir[0];   
            c += dir[1];   

            if (r < 0 || r > 7 || c < 0 || c > 7) break;    
     
            const Piece& p = passedInBoard.getPieceConst(r, c);      
            if (!p.isEmpty())      
            { 
                if (p.getType() == PieceType::ROOK || p.getType() == PieceType::QUEEN)
                {
                    if (p.getColor() == PieceColor::WHITE)
                    {
                        whiteAttackPieces.push_back(p);     
                    }
                    else  
                    {
                        blackAttackPieces.push_back(p); 
                    }                      
                }  
                break; // blocked     
            }
        }
    }
 
    // king attacks    
    for (int dr = -1; dr <= 1; dr++)          
    {  
        for (int dc = -1; dc <= 1; dc++)
        {
            if (dr || dc) // not (0,0) 
            {
                int r = toRow + dr, c = toCol + dc;
                if (r >= 0 && r < 8 && c >= 0 && c < 8)
                {
                    const Piece& p = passedInBoard.getPieceConst(r, c);
                    if (!p.isEmpty() && p.getType() == PieceType::KING)
                    {
                        if (p.getColor() == PieceColor::WHITE)
                            whiteAttackPieces.push_back(p);
                        else
                            blackAttackPieces.push_back(p);
                    }
                }
            }    
        }   
    } 


}

void ChessEngine::resetCounters()
{
    totalNodes         = 0;
    cutNodes           = 0;
    qNodes             = 0;
    seeCutqNodes       = 0;
    nullMoveCutNodes   = 0;
    futilityPruneNodes = 0; 
    nodesPerDepth.assign(aiSearchDepth + 1, 0);
    cutNodesPerDepth.assign(aiSearchDepth + 1, 0);
    //qNodesPerDepth.assign(aiSearchDepth + 1, 0);
    nullMoveCutNodesPerDepth.assign(aiSearchDepth + 1, 0);
    futilityPruneNodesPerDepth.assign(aiSearchDepth + 1, 0);
}

void ChessEngine::updatePvLine(int ply, const Move& move)  
{ 
    assert(ply >= 0 && ply + 1 < MAX_PLY); 
    assert(pvLength[ply + 1] >= 0 && pvLength[ply + 1] < MAX_PLY);

  
    // First move in the PV line at this ply   
    pv[ply][0] = move;   
   
    // New length = 1 (this move) + child PV length
    pvLength[ply] = pvLength[ply + 1] + 1;  

    assert(pvLength[ply] <= MAX_PLY);  
                       
    // Copy child PV from ply+1
    for (int i = 0; i < pvLength[ply + 1]; ++i) {
        pv[ply][i + 1] = pv[ply + 1][i];
    }  
}


int ChessEngine::nonPawnMaterialCount() const
{
    // track state of game
    // track phase of game by piece count
    // don't care about pawns
    // 62 = opening, 0 = endgame
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

    return pieceCount;
    
}

int ChessEngine::getPawnCount() const
{
    int pawnCount = 0;

    for (int r = 0; r < 8; r++)
    {
        for (int c = 0; c < 8; c++)
        {
            const Piece& p = board.getPieceConst(r, c);
            if (p.isEmpty()) continue;

            if (p.getType() == PieceType::PAWN)
                pawnCount += 1;
        }
    }

    return pawnCount;
}


        
bool ChessEngine::isQuietMove(const Move& move) const
{      
    const Piece& captured = board.getPieceConst(move.getToRow(), move.getToCol());
   
    // not a capture 
    if (!captured.isEmpty()) 
        return false;
    if (move.isEnPassantMove()) 
        return false;
  
    // not a promotion
    if (move.getIsPromotion()) 
        return false; 

    // TODO: can add checks? idk
  
    return true;
}          

bool ChessEngine::isFutilityPruneCandidate(const Move& move) const
{   
    bool quiet                  = isQuietMove(move);    
    bool inCheckBeforeMove      = board.isInCheck(board.getCurrentTurn());  
    bool castleMove             = move.isCastlingMove();
    bool passedPawnPush         = isPassedPawnPush(move);
    
    // dont prune passed pawn push 

    return (quiet && !inCheckBeforeMove && !castleMove && !passedPawnPush);
}


bool ChessEngine::isPassedPawnPush(const Move& move) const 
{  
    int fromRow = move.getFromRow();   
    int fromCol = move.getFromCol(); 
 
    int toRow = move.getToRow();
    int toCol = move.getToCol(); 
   
    // if it is a pawn, then its a pawn capture. no passed pawn push
    if (fromCol != toCol)       
        return false; // casptture. sure

    const Piece& p = board.getPieceConst(fromRow, fromCol); 
  
    if (p.getType() != PieceType::PAWN)  
        return false;
          

    PieceColor color = p.getColor();

    // check all enemy pawns ahead on same file and adjacent files
    // no friendly pawns directly ahead on same file
    int direction = (color == PieceColor::WHITE) ? -1 : 1; // white moves up, black moves down     
          
    for (int r = toRow + direction; r >= 0 && r <= 7; r += direction)
    {
        for (int c = toCol - 1; c <= toCol + 1; c++)
        {
            if (c < 0 || c > 7)
                continue;

            const Piece& otherPawn = board.getPieceConst(r, c);
                    
            if (otherPawn.isEmpty())
                continue;
   
            if (otherPawn.getType() == PieceType::PAWN)   
            {
                if (otherPawn.getColor() != color)
                {
                    return false; // enemy pawn blocking path 
                } 
                               
                else if (c == toCol)
                {  
                    return false; // friendly pawn directly ahead  
                }  
            } 
        } 
    }    
    return true; // passed pawn push  
}