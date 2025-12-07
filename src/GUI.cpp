#include "GUI.h"
#include <iostream>
#include <SDL_image.h>

GUI::GUI(ChessEngine* engine):
         window(nullptr), 
         renderer(nullptr), 
         font(nullptr),
         currentTurn(PieceColor::WHITE), 
         isCheck(false), 
         isCheckmate(false), 
         isStalemate(false),
         inputHandler(BOARD_OFFSET_X, BOARD_OFFSET_Y, SQUARE_SIZE),
         engine(engine), 
         running(false) 
{
    promoQueenButton  = { INFO_PANEL_X, 200, 200, 40 };
    promoRookButton   = { INFO_PANEL_X, 250, 200, 40 };
    promoBishopButton = { INFO_PANEL_X, 300, 200, 40 };
    promoKnightButton = { INFO_PANEL_X, 350, 200, 40 };

}

GUI::~GUI() 
{
    if (font) 
    {
        TTF_CloseFont(font);
    }

    if (renderer) 
    {
        SDL_DestroyRenderer(renderer);
    }

    if (window) 
    {
        SDL_DestroyWindow(window);
    }
    
    if (pieceSprites) 
    {
    SDL_DestroyTexture(pieceSprites);
    }

    TTF_Quit();
    SDL_Quit();
}

bool GUI::initialize() 
{
    // Initialize SDL
    if (SDL_Init(SDL_INIT_VIDEO) < 0) 
    {
        std::cerr << "SDL initialization failed: " << SDL_GetError() << std::endl;
        return false;  
    }
      
    // Initialize SDL_ttf
    if (TTF_Init() < 0) 
    {
        std::cerr << "SDL_ttf initialization failed: " << TTF_GetError() << std::endl;
        return false;
    }
    
    // Create window    
    window = SDL_CreateWindow(
                            "Chess AI",
                            SDL_WINDOWPOS_CENTERED,
                            SDL_WINDOWPOS_CENTERED,
                            WINDOW_WIDTH,
                            WINDOW_HEIGHT,
                            SDL_WINDOW_SHOWN
                            );
    
    if (!window)    
    {
        std::cerr << "Window creation failed: " << SDL_GetError() << std::endl;
        return false;
    }
       
    // Create renderer
    renderer = SDL_CreateRenderer(
                                window, 
                                -1, 
                                SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC
                                );

    if (!renderer) 
    {   
        std::cerr << "Renderer creation failed: " << SDL_GetError() << std::endl;
        return false;
    }

    // global transparency    
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);


    // Load Assets
    char* base = SDL_GetBasePath();
    std::string assetPath = std::string(base) + "assets/";
    SDL_free(base); //delete pointer

    // Load font
    font = TTF_OpenFont((assetPath + "arial.ttf").c_str(), 24);
    //font = TTF_OpenFont("build/assets/wingding.ttf", 24); // this doesn't work how i wanted it to
    if (!font) {
        std::cerr << "Font loading failed: " << TTF_GetError() << std::endl;
        return false;
    }  
      
    // downscaling sprites is poorly done, but the linear filter is better 
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "linear");

    SDL_Surface* spriteSurface = IMG_Load((assetPath + "sprite_sheet_resize2.png").c_str());
    if (!spriteSurface)   
    {
        std::cerr << "Failed to load sprite sheet: " << IMG_GetError() << std::endl;
        return false;  
    }  
    pieceSprites = SDL_CreateTextureFromSurface(renderer, spriteSurface);
    SDL_FreeSurface(spriteSurface);  
  
    // downscaling sprites is poorly done, but blending for anti‑aliased edges helps a bit
    SDL_SetTextureBlendMode(pieceSprites, SDL_BLENDMODE_BLEND);

    // Get initial board state
    currentBoard = engine->getBoardCopy();
    currentTurn = currentBoard.getCurrentTurn();
    statusMessage = (currentTurn == PieceColor::WHITE) ? "White to move" : "Black to move";
    
    return true;
}

void GUI::run() 
{
    running = true;
    
    // Main GUI loop
    while (running) 
    {
        handleEvents();
        update();
        render();
          
        SDL_Delay(17);  // 60   
    }
}

// TODO: make some helper functions, this is disgusting
void GUI::handleEvents() 
{
    // hold input/system events
    SDL_Event event;
    
    // is there a pending event in SDL queue
    while (SDL_PollEvent(&event)) 
    {

        // she switch on my case
        switch (event.type) 
        {
            case SDL_QUIT:
                running = false;
                break;
                
            case SDL_MOUSEBUTTONDOWN:
                if (event.button.button == SDL_BUTTON_LEFT) 
                {
                    
                    // Handle promotion menu clicks
                    if (showingPromotionMenu) 
                    {
                        int mx = event.button.x;
                        int my = event.button.y;

                        auto inside = [&](SDL_Rect r)
                        {
                            return mx >= r.x && mx <= r.x + r.w &&
                                my >= r.y && my <= r.y + r.h;
                        };

                        if (inside(promoQueenButton)) 
                        {
                            pendingPromotionMove.setPromotion(PieceType::QUEEN);
                        }
                        else if (inside(promoRookButton)) 
                        {
                            pendingPromotionMove.setPromotion(PieceType::ROOK);
                        }
                        else if (inside(promoBishopButton)) 
                        {
                            pendingPromotionMove.setPromotion(PieceType::BISHOP);
                        }
                        else if (inside(promoKnightButton)) 
                        {
                            pendingPromotionMove.setPromotion(PieceType::KNIGHT);
                        }
                        else 
                        {
                            return; 
                        }

                        showingPromotionMenu = false;
                        handleMoveInput(pendingPromotionMove);
                        return;
                    }


                    // Handle toggle buttons
                    if (event.button.x >= whiteButton.x && event.button.x <= (whiteButton.x + whiteButton.w) &&
                        event.button.y >= whiteButton.y && event.button.y <= (whiteButton.y + whiteButton.h)) 
                    {
                        whiteIsAI = !whiteIsAI;
                        // std::cout << "White AI toggled to " << (whiteIsAI ? "ON" : "OFF") << std::endl;
                        continue; // skip further processing
                    }
                    
                    if (event.button.x >= blackButton.x && event.button.x <= (blackButton.x + blackButton.w) &&
                        event.button.y >= blackButton.y && event.button.y <= (blackButton.y + blackButton.h)) 
                    {
                        blackIsAI = !blackIsAI;
                        // std::cout << "Black AI toggled to " << (blackIsAI ? "ON" : "OFF") << std::endl;
                        continue; // skip further processing
                    }

                    if (event.button.x >= flipButton.x && event.button.x <= flipButton.x + flipButton.w &&
                        event.button.y >= flipButton.y && event.button.y <= flipButton.y + flipButton.h)
                    {
                        isBoardFlipped = !isBoardFlipped;
                        inputHandler.setFlipped(isBoardFlipped);
                        return;                    
                    }

                    
                    // Handle move input
                    Move move(0, 0, 0, 0);
                    
                    // First click selects, second click moves
                    if (inputHandler.handleMouseClick(event.button.x, event.button.y, move)) 
                    {
                        // A complete move was attempted
                        // Check if promotion is needed
                        if (move.isPawnPromotion(currentBoard)) 
                        {
                            //Bring up pop up for promotion choice 
                            pendingPromotionMove = move;
                            showingPromotionMenu = true;
                            return;                               
                        }
                        handleMoveInput(move);
                    } 
                    else if (inputHandler.isPieceSelected()) 
                    {
                        // A piece was just selected - get its legal moves
                        int selectedRow, selectedCol;
                        inputHandler.getSelectedPosition(selectedRow, selectedCol);

                        std::cout << "Selected piece at " << selectedRow << ", " << selectedCol << std::endl;
                        
                        CommandData cmd(EngineCommand::GET_LEGAL_MOVES);
                        cmd.pieceRow = selectedRow;
                        cmd.pieceCol = selectedCol;
                        engine->sendCommand(cmd);
                    }
                }
                break;
                
            case SDL_KEYDOWN:
                if (event.key.keysym.sym == SDLK_r) 
                {
                    // Reset state via engine command and response
                    // do not reset directly here
                    CommandData cmd(EngineCommand::RESET_GAME);
                    engine->sendCommand(cmd);
                
                }
                break;
        }
    }
}

void GUI::update() 
{
    // Process all available responses from the engine
    ResponseData response;
    while (engine->hasResponse()) 
    {
        if (engine->getResponse(response)) 
        {
            // // Print response message for debugging
            // std::cout << "Engine Response: " << response.message << std::endl;
            processEngineResponse(response);
        }
    }
    


    // //what player is AI
    // std::cout << "black is AI: " << blackIsAI << " white is AI: " << whiteIsAI << std::endl;
    // //print what turn is it
    // std::cout << "Current turn: " << (currentTurn == PieceColor::WHITE ? "White" : "Black") << std::endl;

    // Handle AI move command
    if (((currentTurn == PieceColor::WHITE && whiteIsAI) 
          || (currentTurn == PieceColor::BLACK && blackIsAI)) 
          && !aiResponsePending 
          && !isGameOver) 
    {
        std::cout << "Requesting AI move..." << std::endl;
        aiResponsePending = true;
        CommandData cmd(EngineCommand::COMPUTE_AI_MOVE);
        engine->sendCommand(cmd);
    }
}

void GUI::render() 
{
    // Clear screen with background color  
    SDL_SetRenderDrawColor(renderer, 50, 50, 50, 255);
    SDL_RenderClear(renderer);  
    
    // Render components   
    renderBoard();
    renderHighlights();  
    renderPieces(); 
    renderInfoPanel();
    
    if (showingPromotionMenu) 
    {
        renderPromotionMenu();
    }
   
    if (isGameOver) 
    {
  
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
   
        SDL_Color overlayColor = {0, 0, 0, 128};
        SDL_SetRenderDrawColor(renderer, overlayColor.r, overlayColor.g, overlayColor.b, overlayColor.a);
        SDL_Rect overlay = {BOARD_OFFSET_X, BOARD_OFFSET_Y, SQUARE_SIZE*8, SQUARE_SIZE*8};
        SDL_RenderFillRect(renderer, &overlay);
   
        SDL_Color textColor = {255, 255, 255, 255};
        renderText(statusMessage, BOARD_OFFSET_X + 100, BOARD_OFFSET_Y + 200, textColor);
        renderText("Press R to reset", BOARD_OFFSET_X + 100, BOARD_OFFSET_Y + 250, textColor);
    }  
 
       
    SDL_RenderPresent(renderer);
}
   
void GUI::renderBoard() {
    for (int row = 0; row < 8; row++) 
    {   
        for (int col = 0; col < 8; col++) 
        {  
                      
            SDL_Color squareColor = ((row + col) % 2 == 0) ? LIGHT_SQUARE : DARK_SQUARE;
            
            SDL_SetRenderDrawColor(renderer, squareColor.r, squareColor.g, squareColor.b, squareColor.a);
            
            // TODO: This might not be necessary, but if i add numbering to the squares it will be
            int screenRow = isBoardFlipped ? 7 - row : row;
            int screenCol = isBoardFlipped ? 7 - col : col;

            SDL_Rect square = 
            {  
                BOARD_OFFSET_X + screenCol * SQUARE_SIZE,
                BOARD_OFFSET_Y + screenRow * SQUARE_SIZE,  
                SQUARE_SIZE,
                SQUARE_SIZE   
            };  

            
            SDL_RenderFillRect(renderer, &square);
        }
    }
}

void GUI::renderPieces() 
{
    for (int row = 0; row < 8; row++) 
    {
        for (int col = 0; col < 8; col++) 
        {
            const Piece& piece = currentBoard.getPieceConst(row, col);
            if (!piece.isEmpty())    
            { 
                //renderPiece(piece, row, col);
                int screenRow = isBoardFlipped ? 7 - row : row;
                int screenCol = isBoardFlipped ? 7 - col : col;

                renderPiece(piece, screenRow, screenCol);
 
            }  
        }
    }
}

// TODO: make square sprites and remove this scale logic
void GUI::renderPiece(const Piece& piece, int row, int col) 
{
    if (!pieceSprites) return;

    SDL_Rect src = getPieceSrcRect(piece);  
  
    // Compute scale factors to fit inside SQUARE_SIZE
    float scaleX = static_cast<float>(SQUARE_SIZE) / src.w;  
    float scaleY = static_cast<float>(SQUARE_SIZE) / src.h;  
    float scale  = std::min(scaleX, scaleY); // preserve aspect ratio  

    int destW = static_cast<int>(src.w * scale);
    int destH = static_cast<int>(src.h * scale);

    // Center the piece inside the square
    int destX = BOARD_OFFSET_X + col * SQUARE_SIZE + (SQUARE_SIZE - destW) / 2;
    int destY = BOARD_OFFSET_Y + row * SQUARE_SIZE + (SQUARE_SIZE - destH) / 2;

    SDL_Rect dest = { destX, destY, destW, destH };
    SDL_RenderCopy(renderer, pieceSprites, &src, &dest);
}


//TODO: make work with flipped board state  
void GUI::renderHighlights() 
{
    
    // Highlight last move squares  
    if (hasLastMove)  
    {

        auto drawSquare = [&](int r, int c)
        {
            int screenRow = isBoardFlipped ? 7 - r : r;
            int screenCol = isBoardFlipped ? 7 - c : c;
  
            SDL_Rect rect = {
                BOARD_OFFSET_X + screenCol * SQUARE_SIZE,
                BOARD_OFFSET_Y + screenRow * SQUARE_SIZE,
                SQUARE_SIZE,
                SQUARE_SIZE  
            };      
 
            SDL_SetRenderDrawColor(renderer,
                                LAST_MOVE_COLOR.r,
                                LAST_MOVE_COLOR.g, 
                                LAST_MOVE_COLOR.b, 
                                LAST_MOVE_COLOR.a);

            SDL_RenderFillRect(renderer, &rect);
        };  
  
        drawSquare(lastMove.getFromRow(), lastMove.getFromCol());
        drawSquare(lastMove.getToRow(),   lastMove.getToCol());
    }  
  
    if (inputHandler.isPieceSelected()) 
    {
        // Highlight selected square
        int selectedRow, selectedCol; 
        inputHandler.getSelectedPosition(selectedRow, selectedCol);        
 
        // Board flip adjustment 
        int screenRow = isBoardFlipped ? 7 - selectedRow : selectedRow;
        int screenCol = isBoardFlipped ? 7 - selectedCol : selectedCol;

  
        SDL_SetRenderDrawColor(renderer, 
                               SELECTED_SQUARE.r, 
                               SELECTED_SQUARE.g, 
                               SELECTED_SQUARE.b, 
                               SELECTED_SQUARE.a);
    
        SDL_Rect selectedRect =  
        {
            BOARD_OFFSET_X + screenCol * SQUARE_SIZE,
            BOARD_OFFSET_Y + screenRow * SQUARE_SIZE,
            SQUARE_SIZE,
            SQUARE_SIZE
        };
          
        // DrawRect draws a SINGLE pixel thickness with no way to change so here a foor loop
        for (int i = 0; i < 5; i++) 
        {
            SDL_RenderDrawRect(renderer, &selectedRect);
            selectedRect.x++;
            selectedRect.y++;
            selectedRect.w -= 2;
            selectedRect.h -= 2;
        }
        
  

 
        // Highlight legal move destinations
        const std::vector<Move>& legalMoves = inputHandler.getLegalMoves();
 
        int screenToRow;
        int screenToCol;
        
        for (const Move& move : legalMoves) 
        {

            // Board flip adjustment  
            screenToRow = isBoardFlipped ? 7 - move.getToRow() : move.getToRow();
            screenToCol = isBoardFlipped ? 7 - move.getToCol() : move.getToCol();

            SDL_SetRenderDrawColor(renderer, LEGAL_MOVE_HIGHLIGHT.r, LEGAL_MOVE_HIGHLIGHT.g,
                                   LEGAL_MOVE_HIGHLIGHT.b, LEGAL_MOVE_HIGHLIGHT.a);
            
            SDL_Rect legalMoveRect =   
            {
                BOARD_OFFSET_X + screenToCol * SQUARE_SIZE + SQUARE_SIZE / 4,
                BOARD_OFFSET_Y + screenToRow * SQUARE_SIZE + SQUARE_SIZE / 4,
                SQUARE_SIZE / 2,
                SQUARE_SIZE / 2
            };
            
            SDL_RenderFillRect(renderer, &legalMoveRect);
        }  
    }
}

//TODO: make buttons look better
void GUI::renderInfoPanel() 
{
    if (!font) return;   
    
    SDL_Color textColor = {255, 255, 255, 255};
    
    renderText("Chess AI", INFO_PANEL_X, 20, textColor);
    
     std::string turnText = (currentTurn == PieceColor::WHITE) ? "White's Turn" : "Black's Turn";
    renderText(turnText, INFO_PANEL_X, 60, textColor);
    
    renderText(statusMessage, INFO_PANEL_X, 100, textColor);
      
     if (isCheck) 
    {
        SDL_Color checkColor = {255, 0, 0, 255};
        renderText("CHECK!", INFO_PANEL_X, 140, checkColor);
    }
    
    if (isCheckmate) 
    {
        SDL_Color mateColor = {255, 0, 0, 255};
        renderText("CHECKMATE!", INFO_PANEL_X, 180, mateColor);
    }
    
    if (isStalemate) 
    {
        renderText("STALEMATE!", INFO_PANEL_X, 180, textColor);
    }
 
    renderText("Engine Best Move: " + lastBestEngineMove.toString(),
            INFO_PANEL_X, 220, textColor);
  
    renderText("Engine Eval: " + std::to_string(lastEngineEval),
            INFO_PANEL_X, 260, textColor);

    std::string lastEngineLineString = "line: ";
    for (const Move& m : lastEngineLine)
        lastEngineLineString += m.toString() + " ";

    renderText(lastEngineLineString, INFO_PANEL_X, 320, textColor);
 

    // White AI 
    SDL_Color btnColor = {200, 200, 200, 255};
    SDL_SetRenderDrawColor(renderer, btnColor.r, btnColor.g, btnColor.b, btnColor.a);
    SDL_RenderFillRect(renderer, &whiteButton);

    std::string whiteLabel = whiteIsAI ? "White: AI" : "White: Human";
    renderText(whiteLabel, whiteButton.x + 10, whiteButton.y + 10, textColor);
 
    // Black AI  
    SDL_RenderFillRect(renderer, &blackButton);
 
    std::string blackLabel = blackIsAI ? "Black: AI" : "Black: Human";
    renderText(blackLabel, blackButton.x + 10, blackButton.y + 10, textColor);

    // Flip 
    SDL_SetRenderDrawColor(renderer, 200, 200, 200, 255);
  
    SDL_RenderFillRect(renderer, &flipButton);
    renderText("Flip Board", flipButton.x + 10, flipButton.y + 10, {255,255,255,255});
        
 
    // Instructions 
    renderText("Press R to reset", INFO_PANEL_X, 400, textColor);
}

void GUI::renderText(const std::string& text, int x, int y, SDL_Color color) 
{
    if (!font) return;
    
    SDL_Surface* surface = TTF_RenderText_Solid(font, text.c_str(), color);
    if (!surface) return;
      
    SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer, surface);
    if (!texture) 
    {
        SDL_FreeSurface(surface);
        return; 
    } 
    
    SDL_Rect destRect = {x, y, surface->w, surface->h};

    SDL_RenderCopy(renderer, texture, nullptr, &destRect);
    
    SDL_DestroyTexture(texture);
    SDL_FreeSurface(surface);
}

void GUI::handleMoveInput(const Move& move) 
{
    CommandData cmd(EngineCommand::MAKE_MOVE);
    cmd.move = move;
    engine->sendCommand(cmd); 
}

//TODO: make this use sprites instead of text
//TODO: could probably make this less bad
void GUI::renderPromotionMenu() 
{
    SDL_Color bg = {80, 80, 80, 255};
    SDL_Color text = {255, 255, 255, 255}; 

    SDL_SetRenderDrawColor(renderer, bg.r, bg.g, bg.b, bg.a);

    SDL_Rect panel = { INFO_PANEL_X, 180, 220, 240 };
    SDL_RenderFillRect(renderer, &panel);

    renderText("Promote to:", INFO_PANEL_X + 10, 190, text);

    SDL_RenderFillRect(renderer, &promoQueenButton);
    renderText("Queen", promoQueenButton.x + 10, promoQueenButton.y + 10, text);

    SDL_RenderFillRect(renderer, &promoRookButton);
    renderText("Rook", promoRookButton.x + 10, promoRookButton.y + 10, text);

    SDL_RenderFillRect(renderer, &promoBishopButton);
    renderText("Bishop", promoBishopButton.x + 10, promoBishopButton.y + 10, text);

    SDL_RenderFillRect(renderer, &promoKnightButton);
    renderText("Knight", promoKnightButton.x + 10, promoKnightButton.y + 10, text);
}

  
void GUI::processEngineResponse(const ResponseData& response) 
{
    switch (response.response) 
    {
        case EngineResponse::MOVE_RESULT:
            if (response.success) 
            { 
 
            currentTurn = response.currentTurn;
            isCheck = response.isCheck;
            isCheckmate = response.isCheckmate;
            isStalemate = response.isStalemate;

            // track last move for highlighting
            lastMove = response.move;
            hasLastMove = true;
            }

            setStatusMessage(response.message);
            aiResponsePending = false;
            break;  
                          
        case EngineResponse::AI_MOVE:
            currentTurn = response.currentTurn;  
            isCheck = response.isCheck;
            isCheckmate = response.isCheckmate; 
            isStalemate = response.isStalemate;    
            setStatusMessage(response.message); 
            aiResponsePending = false;

            // store data
            lastBestEngineMove = response.bestMove;
            lastEngineEval     = response.eval; 
            lastEngineLine     = response.line; 

            // adjust eval for GUI display, negamax always from side to move pov (negative is bad for side to move, pos if good)           
            // display in traditional way where positive is good for white, negative good for black  
            evalMultiplyFactor = (currentTurn == PieceColor::WHITE) ? -1 : 1;
            lastEngineEval     = lastEngineEval * evalMultiplyFactor;  
 

            // track last move for highlighting
            lastMove = response.bestMove; 
            hasLastMove = true;    
            
            break; 
            
        case EngineResponse::LEGAL_MOVES:
            inputHandler.setLegalMoves(response.legalMoves);

            // // Debug print legal moves
            // std::cout << "Legal moves received: ";
            // for (const Move& m : response.legalMoves) 
            // {
            //     std::cout << m.toString() << " ";
            // }
            break;
            
        case EngineResponse::GAME_RESET:
            currentTurn = response.currentTurn;
            isCheck = false;
            isCheckmate = false;
            isStalemate = false;
            currentBoard = engine->getBoardCopy();  // refresh GUI board state
            isGameOver = false;       
            inputHandler.clearSelection();     
            aiResponsePending = false;
            setStatusMessage("Game reset - White to move");
            break;
             
        case EngineResponse::GAME_STATE_UPDATE:
            currentTurn = response.currentTurn; 
            isCheck = response.isCheck;
            isCheckmate = response.isCheckmate;
            isStalemate = response.isStalemate;
            break;
    }
     
    // Detect game end states
    if (isCheckmate) 
    { 
        if (currentTurn == PieceColor::WHITE) 
        {
            setStatusMessage("Black wins by checkmate");
        } else 
        {
            setStatusMessage("White wins by checkmate");
        }
        isGameOver = true;
    }

    if (isStalemate)  
    {
        setStatusMessage("Draw by stalemate");
        isGameOver = true;
    }

    currentBoard = engine->getBoardCopy();

} 

std::string GUI::getPieceSymbol(const Piece& piece) const 
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

void GUI::setStatusMessage(const std::string& message) 
{
    statusMessage = message;
}

SDL_Rect GUI::getPieceSrcRect(const Piece& piece) 
{
    // TODO: make a sprite sheet that doesnt need to be scaled and can have width and height 64x64
    const int spriteWidth         = 100;//500;
    const int spriteHeight        = 170;//850;
    const int initialOffsetWidth  = 0;//50;
    const int initialOffsetHeight = 0;//100;
    int row = (piece.getColor() == PieceColor::WHITE) ? 0 : 1;
    int col = 0;
    switch (piece.getType()) 
    {
        case PieceType::PAWN:   col = 0; break;
        case PieceType::KNIGHT: col = 2; break;
        case PieceType::BISHOP: col = 1; break;
        case PieceType::ROOK:   col = 3; break;
        case PieceType::QUEEN:  col = 4; break;
        case PieceType::KING:   col = 5; break;
        default: col = 0; break; 
    }  
    return {
            col * spriteWidth  + initialOffsetWidth, 
            row * spriteHeight + initialOffsetHeight, 
            spriteWidth        + initialOffsetWidth, 
            spriteHeight       + initialOffsetHeight
            };
}

