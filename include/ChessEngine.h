#ifndef CHESS_ENGINE_H
#define CHESS_ENGINE_H

#include "Board.h"
#include "Move.h"
#include <thread>
#include <mutex>
#include <queue>
#include <condition_variable>
#include <chrono>


// gui command to engine
enum class EngineCommand 
{
    MAKE_MOVE,
    COMPUTE_AI_MOVE,
    GET_LEGAL_MOVES,
    RESET_GAME,
    SHUTDOWN
};

// Engine response to gui
// GAME_STATE_UPDATE is currently unused but I want to use it to provide what the ai thinks the best move is later on
// while player is thinking, have best move and evaluation score updated in the background and displayed in the GUI
enum class EngineResponse 
{
    MOVE_RESULT,
    AI_MOVE, 
    LEGAL_MOVES,
    GAME_RESET,
    GAME_STATE_UPDATE               
};

// gui to engine data 
struct CommandData   
{
    EngineCommand command;
    Move move;
    int pieceRow;         
    int pieceCol;
    
    CommandData(EngineCommand cmd) :
                command(cmd), 
                move(0, 0, 0, 0),   
                pieceRow(-1), 
                pieceCol(-1)  
    {
    } 
};  

// Engine to gui data 
struct ResponseData 
{ 
    EngineResponse response;
    bool success;                 
    Move move;                    
    std::vector<Move> legalMoves;       
    std::string message;          
    
    bool isCheck;   
    bool isCheckmate;
    bool isStalemate;    
    PieceColor currentTurn;
 
    Move bestMove;  
    int eval; 
    std::vector<Move> line;    
    
    ResponseData() : 
                response(EngineResponse::GAME_STATE_UPDATE), 
                success(false), 
                move(0, 0, 0, 0),   
                isCheck(false), 
                isCheckmate(false), 
                isStalemate(false),         
                currentTurn(PieceColor::WHITE) 
                {        
                }
};
      
struct attackingPieceData 
{
    int row;
    int col;
    PieceType type;
    PieceColor color;
};
   
class ChessEngine 
{
private:
    Board board;
    
    // Threading components
    std::thread engineThread;
    std::atomic<bool> running;
    
    std::queue<CommandData> commandQueue;
    std::queue<ResponseData> responseQueue;
    
    std::mutex commandMutex;
    std::mutex responseMutex;

    // Mutex for board state is not needed atm
    // I want to multithread the search later though
    // Mutex is needed for safe multithreading so i put it in now
    // it has to be mutable since I want getBoardCopy() to be const
    mutable std::mutex boardMutex;

    std::condition_variable commandCV; 
    
    static const int MAX_PLY = 256;

    
    int aiSearchDepth; 

    // move order
    Move killerMove1[MAX_PLY]; // [ply]
    Move killerMove2[MAX_PLY]; // [ply]
    int historyHeuristicsHysteresis[2][MAX_PLY][MAX_PLY] = {0}; // [side][from][to] for current depth

    const int MAX_PHASE = 62;

    // null move pruning constants
    // if doing nothing is so good chances are the opponent will not let it happen
    const int NULL_MOVE_PRUNE_MIN_DEPTH = 3;
    const int NULL_MOVE_DEPTH_REDUCTION = 2; // idk

    // futility pruning constants
    // if the search is almost over, and the eval is way below alpha (222 below in this case) stop
    const int FUTILITY_PRUNE_MARGIN = 200; // idk
    const int FUTILITY_PRUNE_MIN_NON_PAWN_MATERIAL = 3;

    // late move reduction constants 
    // moves farther down the move order are less likely to be good moves
    // search at lower depth, if they prove to be good, search again at full depth  
    const int LATE_MOVE_REDUCTION_MIN_DEPTH        = 3; // idk
    const int LATE_MOVE_REDUCTION_MIN_MOVE_ORDER   = 3; // idk
    const int LATE_MOVE_REDUCTION_REDUCTION_AMOUNT = 2; // idk

    // aspiration window lims
    // let alpha-beta pruning be really agro and start them within a margin of the previous best eval
    const int ASPIRATION_WINDOW_MARGIN_AMOUNT = 50; // idk

        
    // quiescence search constants
    const int QUIESCENCE_SEARCH_MAX_PLY = 32; // idk
    
    const int MAX_THINKING_TIME_MS = 5000; // idk
         

    // end game disable pruning 
    // TODO: implement
    const int ENDGAME_PHASE_MATERIAL_THRESHOLD = 10; // R+R 
    bool inEndgamePhase = false;     


    // count nodes searched    
    const bool QUICK_PRINTOUT = true;
    const bool FULL_PRINTOUT_OF_NODES_PER_DEPTH = true;

    // total search  
    std::vector<uint64_t> nodesPerDepth;     
    uint64_t totalNodes = 0; 
                
    // normal alpha-beta nodes    
    std::vector<uint64_t> cutNodesPerDepth;
    uint64_t cutNodes   = 0;   

    // qsearch nodes 
    //std::vector<uint64_t> qNodesPerDepth; //yeah idk how to track this 
    uint64_t qNodes     = 0; 

    // null move pruning nodes
    std::vector<uint64_t> nullMoveCutNodesPerDepth;
    uint64_t nullMoveCutNodes = 0;

    // futility pruning nodes
    std::vector<uint64_t> futilityPruneNodesPerDepth;
    uint64_t futilityPruneNodes = 0;

    // late move reduction
    // TODO: figure out a way to track lmr nodes 

    // SEE 
    uint64_t seeCutqNodes = 0;

    //end game 
    int WINNING_KPK_EVAL = 500; 

     
  
    // flags  
    const bool USE_NULL_MOVE_PRUNING     = true;  // good  
    const bool USE_FUTILITY_PRUNING      = true;  // good  
    const bool USE_LATE_MOVE_REDUCTION   = true;  // not really tested but nothing appears to be breaking 
    const bool USE_ASPIRATION_WINDOWS    = true;  // good 
    const bool USE_QUIESCENCE_SEARCH     = true;  // good   
    const bool USE_SEE_EVALUATION        = true;  // good    
           
    const bool USE_MOVE_ORDERING         = true;  // good 
    const bool USE_HISTORY_HEURISTIC     = true;  // not really tested but nothing appears to be breaking  
    const bool USE_KILLER_MOVE_HEURISTIC = true;  // not really tested but nothing appears to be breaking 

    const bool USE_ITERATIVE_DEEPENING   = true;   // good    
    const bool LIMIT_SEARCH_TIME         = true;   // good  



    const int MOVE_ORDERING_PREVIOUS_BEST_MOVE     = 10000;
    const int MOVE_ORDERING_SEE_GOOD_CAPTURE       = 9000;
    const int MOVE_ORDERING_PROMOTION              = 8000;  
    const int MOVE_ORDERING_SEE_EQUAL_CAPTURE      = 7000; 
    const int MOVE_ORDERING_KILLER_MOVE_1          = 6000;
    const int MOVE_ORDERING_KILLER_MOVE_2          = 5000; 
    const int MOVE_ORDERING_HISTORY_HEURISTIC_BASE = 1;    
    const int MOVE_ORDERING_SEE_BAD_CAPTURE        = -1000;  

    Move moveOrderingPreviousBestMove = Move(-1,-1,-1,-1);

 
    // timing
    std::chrono::time_point<std::chrono::steady_clock> searchStart;
    bool searchTimeout = false; 

    inline bool timeExceeded() const {
        if (!LIMIT_SEARCH_TIME)
            return false;
        auto now = std::chrono::steady_clock::now();
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - searchStart).count();
        return ms >= MAX_THINKING_TIME_MS;
    };
       
    // engine line storage     
    Move pv[MAX_PLY][MAX_PLY];  // [depth][move]                
    int pvLength[MAX_PLY]; // [depth]     
   
public:

    ChessEngine();

    ~ChessEngine();

    // expose engine 
    Move sendToGuiBestMove = Move(0,0,0,0);
    int  sendToGuiBestEval = 0;
    std::vector<Move> sendToGuiBestLine;     

    void start();

    void stop();

    void sendCommand(const CommandData& command);

    bool hasResponse();

    bool getResponse(ResponseData& outResponse);

    // Returns a deep copy of the board so the GUI can't break anything.
    // required writing a custom copy instructor since board is mutex locked
    // using a pointer kind of worked and kind of didn't
    const Board getBoardCopy() const;

    int evaluateBoard() const;

    int rawPieceTotal(PieceColor sideToMove) const;

    int evaluatePieceSquareTables(PieceColor sideToMove, int pieceCount) const;

    int evaluatePawnStructure(PieceColor sideToMove) const;

    int evaluatePieceMobility(PieceColor sideToMove, int pieceCount) const;

    int evaluateKingSafety(PieceColor sideToMove, int pieceCount) const;

    int evaluateKingRestriction(PieceColor sideToMove, int pieceCount) const;

    int evaluateKingToKingDistance(PieceColor sideToMove, int pieceCount) const;

    int evaluateKPKEndgame(PieceColor sideToMove, int pieceCount) const;

    void getOrderedMoves(std::vector<Move>& moves, int depth) const;

    void setBoard(const Board& newBoard);

private:

    void engineLoop();

    void processCommand(const CommandData& command);

    void sendResponse(const ResponseData& response);

    Move calculateBestMove();

    int negamax(int numPlyLeft, int alpha, int beta, int ply);

    int qSearch(int alpha, int beta, int qPly);

    inline int pieceValue(PieceType t) const
    {
        switch (t)
        {
            case PieceType::PAWN:   return 100;
            case PieceType::KNIGHT: return 300;
            case PieceType::BISHOP: return 300;
            case PieceType::ROOK:   return 500;
            case PieceType::QUEEN:  return 900;
            case PieceType::KING:   return 2000;
            default: return 0;
        }
    };

    int SEE(const Move& move) const;

    void getAllAttackersOfSquare(int toRow, int toCol, Board passedInBoard, std::vector<Piece>& whiteAttackPieces,  std::vector<Piece>& blackAttackPieces ) const;   

    void resetCounters();

    void updatePvLine(int ply, const Move& move);
   
    int nonPawnMaterialCount() const;    
        
    int getPawnCount() const;

    bool isQuietMove(const Move& move) const;

    bool isFutilityPruneCandidate(const Move& move) const;

    bool isPassedPawnPush(const Move& move) const;    

    std::string getPieceSymbol(const Piece& piece) const 
    {
        // TODO: make a sprite sheet
        
        std::string symbol;
        switch (piece.getType()) {
            case PieceType::PAWN:   symbol = "P"; break;
            case PieceType::KNIGHT: symbol = "N"; break;
            case PieceType::BISHOP: symbol = "B"; break;
            case PieceType::ROOK:   symbol = "R"; break;
            case PieceType::QUEEN:  symbol = "Q"; break;
            case PieceType::KING:   symbol = "K"; break;
            default: symbol = "?"; break;
        }
        
        return symbol;
    }    


};              


#endif
