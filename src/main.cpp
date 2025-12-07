#include "ChessEngine.h"
#include "GUI.h"
  
#include <iostream>
#include <memory> 
 
int main(int argc, char* argv[])
{   
    // start engine before interface
    auto engine = std::make_unique<ChessEngine>();
    engine->start();
  
    // hook up gui  
    auto gui = std::make_unique<GUI>(engine.get());
    if (!gui->initialize()) 
    { 
        std::cerr << "How did we get here?  \n";
        return 1;
    } 
 
    // main loop 
    gui->run();

    return 0;
}
