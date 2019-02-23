#include <windows.h>

/*
    Main entry point for windows functions:
    The goal is to initialize the application, display the main window and enter message retrieval
*/
int WinMain(
  HINSTANCE hInstance,     //Handle to current program
  HINSTANCE hPrevInstance, //Handle to previous program instance? If you want to detect if another exists
  LPSTR     lpCmdLine ,    //Command line for program
  int       nShowCmd       //No idea what this does seems to always be  10
)
{

    OutputDebugStringA("Hello World!");
    return 0;
}