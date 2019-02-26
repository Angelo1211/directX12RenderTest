#include <windows.h>
/*
    This function handles messages sent by the OS
*/
LRESULT CALLBACK Main_Window_Callback(HWND   window_handle, //Essentially like a pointer to the window
                                      UINT   message,       //Message sent by the OS
                                      WPARAM WParam,        //Additional message info if needed
                                      LPARAM LParam)        //Additional message info again but no idea what difference there is between this and the previous system
{
    LRESULT Result = 0; //Output code to tell windows how everyhthing was handled

    switch(message){ //React to all these different messages
        case WM_SIZE: // Any change in window size
            OutputDebugStringA("WM_SIZE\n");
        break;

        case WM_DESTROY: // After the window is destroyed
            OutputDebugStringA("WM_DESTROY\n");
        break;

        case WM_CLOSE:  // When you click the X to close
            OutputDebugStringA("WM_CLOSE\n");
        break;

        case WM_ACTIVATEAPP: // When the window is made active again
            OutputDebugStringA("WM_ACTIVATEAPP\n");
        break;

        case WM_PAINT: // We clear the window
        {
            PAINTSTRUCT paint; // This struct contains the info we need to paint a window
            HDC device_context = BeginPaint(window_handle, &paint); // We're about to update our window
            int X = paint.rcPaint.left;
            int Y = paint.rcPaint.top;
            int height = paint.rcPaint.bottom - paint.rcPaint.top; 
            int width  = paint.rcPaint.right - paint.rcPaint.left; 
            static DWORD Operation = WHITENESS;
            PatBlt(device_context,X, Y, width, height, Operation);
            if(Operation == WHITENESS){
                Operation = BLACKNESS;
            }
            else{
                Operation = WHITENESS;
            }
            EndPaint(window_handle, &paint); // We're done updating the window

        }break; 

        default: // For Everything else we run the default windows procedure that takes care of all other messages
            Result = DefWindowProc(window_handle, message, WParam, LParam );
        break;
    }
    return Result;
}
/*
    Main entry point for windows functions:
    The goal is to initialize the application, display the main window and enter message retrieval

    HANDLE: Unsigned 32 bit unique identifier used by the kernel to keep track of things
    HINSTANCE: Are handles to a program. Each program gets a single HINSTANCE 
    STR: String data type with already allocated storage
    LPSTR: Long pointer to a STR

    WinMain has a number of jobs:
    1. Register window classes used by the program
    2. Create a window used by the program
    3. Run the message loop

    Each graphical detail on screen is a window. 
    Each window has an associated window class that needs to be registered with the system
    1. Fill in the fields of the WNDCLASS data object
    2. Pass the WNDCLASS object to the registerClass function
    Once registered you can discard the structure

*/
int CALLBACK WinMain(HINSTANCE hInstance,     //Handle to current program
                    HINSTANCE hPrevInstance, //Handle to previous program instance? If you want to detect if another exists. IS ALWAYS NULL
                    LPSTR     lpCmdLine,    //Command line for program, basically replaces argv and argc
                    int       nShowCmd)      //No idea what this does seems to always be  10
{
    //0. Create winclass object
    WNDCLASS window_Struct      = {0};
    window_Struct.style         = CS_VREDRAW | CS_HREDRAW | CS_OWNDC;   // The window will redraw on vertical or horizontal movement or size adjustment
    window_Struct.lpfnWndProc   = Main_Window_Callback;                   // A pointer to the window procedure??
    window_Struct.cbClsExtra    = 0;                         // Number of extra bytes to allocate after the window class struct? Why would you want to do this?
    window_Struct.cbWndExtra    = 0;                         // Number of extra bytes to allocate following window instance? Again why would you even want this?
    window_Struct.hInstance     = hInstance;                 // A handle to the program instance that is in charge of the window can use get module handle to get it
    window_Struct.hIcon         = NULL;                      // A handle to the icon, if the member is null the system provides a default
    window_Struct.hCursor       = NULL;                      // A handle to the cursor
    window_Struct.lpszMenuName  = NULL;                      // A menu name
    window_Struct.hbrBackground = NULL;                      // A handle to background brush?? No idea what this is but if set to null the application must paint its own background
    window_Struct.lpszClassName = "DirectX12 Demo";         // The name of the class 

    //1. Registering the winclass object 
    RegisterClass(&window_Struct); //technically returns a value if the class has not been registered correctly, but only does it if the program is not being run on windows NT, which is not going to be the case I believe

    //void CreateWindowA(lpClassName, lpWindowName, dwStyle,  x, y, nWidth, nHeight, hWndParent, hMenu, hInstance, lpParam );
    //2. Creating the window
    HWND Window_Handle = CreateWindowExA(0,
        window_Struct.lpszClassName,
        "Demo",
        WS_OVERLAPPEDWINDOW | WS_VISIBLE,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        0,
        0,
        hInstance,
        0);

    //3. Manage messages
    /*
    Windows does not start sending messages by defualt, it needs someone to start pulling them off a queue. Every tiem you have an application in windows a queue is created.
    We need to loop through those messages and extract them.
    If bot of the filter values are zero then windows returns all of the messages
    // We let it write to our message address
    // Get messages from all of our windows
    // Filter for specific kinds of messages
    // Also a filter
    */
    MSG msg;
    BOOL bRet;
    while ((bRet = GetMessage(&msg, Window_Handle, 0, 0)) != 0)
    {
        if (bRet == -1)
        {
            break;
        }
        else
        {
            TranslateMessage(&msg); // Translate keyboard messages into more pproper messages ????
            DispatchMessage(&msg);  // Actually deal with message
        }
    }

    return 0;
}
