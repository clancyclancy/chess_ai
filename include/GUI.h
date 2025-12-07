#ifndef GUI_H
#define GUI_H

#include "Board.h"
#include "ChessEngine.h"
#include "InputHandler.h"
#include <SDL2/SDL.h>  
#include <SDL2/SDL_ttf.h>  
#include <string>

class GUI {
private:
    SDL_Window* window;
    SDL_Renderer* renderer;
    TTF_Font* font;
    
    const int WINDOW_WIDTH   = 900;
    const int WINDOW_HEIGHT  = 600;  
    const int BOARD_SIZE     = 512;   
    const int SQUARE_SIZE    = 64; 
    const int BOARD_OFFSET_X = 20;  
    const int BOARD_OFFSET_Y = 20;  
    const int INFO_PANEL_X   = 560;     //inc if text offscreen
         
    SDL_Color LIGHT_SQUARE         = {240, 217, 181, 255};  //lichess colors 
    SDL_Color DARK_SQUARE          = {181, 136,  99, 255};  //lichess colors 
    SDL_Color SELECTED_SQUARE      = {  0,  63, 127, 128};
    SDL_Color LEGAL_MOVE_HIGHLIGHT = {  0,  63, 127, 128};  //inv color combo
    SDL_Color CHECK_HIGHLIGHT      = {255, 192, 128, 128};  //inv color combo
    SDL_Color LAST_MOVE_COLOR      = {255, 128, 128, 128}; 
      
    SDL_Texture* pieceSprites;
 
    SDL_Rect whiteButton = {INFO_PANEL_X, 500, 200, 40};
    SDL_Rect blackButton = {INFO_PANEL_X, 550, 200, 40};      
    SDL_Rect flipButton  = {INFO_PANEL_X, 450, 200, 40};


    // game state recieved from engine
    Board currentBoard;
    PieceColor currentTurn;  
    bool isCheck;   
    bool isCheckmate;
    bool isStalemate;
    bool isGameOver = false;
    std::string statusMessage;

    bool whiteIsAI = false;
    bool blackIsAI = true;

    bool aiResponsePending = false;

    bool isBoardFlipped = false; // she flip on my board   

    Move pendingPromotionMove = Move(0,0,0,0);
  
    bool showingPromotionMenu = false;



    SDL_Rect promoQueenButton;
    SDL_Rect promoRookButton;
    SDL_Rect promoBishopButton;
    SDL_Rect promoKnightButton;  

    InputHandler inputHandler;  
    
    // only ref
    ChessEngine* engine;
       
    bool running;
  
    Move lastBestEngineMove = Move(-1,-1,-1,-1);
    int  lastEngineEval = 0;     
    std::vector<Move> lastEngineLine; 

    int evalMultiplyFactor;   

    Move lastMove;
    bool hasLastMove = false;

public:

    GUI(ChessEngine* engine);      

    ~GUI();

    bool initialize();
      
    void run();

    bool isRunning() const { return running; }

private:
  
    void handleEvents();      
  
    void update();
 
    void render();
  
    void renderBoard();      
 
    void renderPieces();    

    void renderPiece(const Piece& piece, int row, int col);
     
    void renderHighlights();

    void renderInfoPanel();  
   
    void renderText(const std::string& text, int x, int y, SDL_Color color);   

    void renderPromotionMenu();
         
    void handleMoveInput(const Move& move);   

    void processEngineResponse(const ResponseData& response);

    std::string getPieceSymbol(const Piece& piece) const;  
 
    void setStatusMessage(const std::string& message);
         
    SDL_Rect getPieceSrcRect(const Piece& piece);
  
};   

#endif
