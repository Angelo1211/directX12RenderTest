#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <D3d12.h>
#include <dxgi1_6.h>
#include <d3dcompiler.h>
#include <DirectXMath.h>

//Globals
HWND window_handle = NULL;
LPCTSTR window_name = "Michael Scott";
LPCTSTR window_title = "DirectX12 Demo Window";
int width = 800;
int height = 600;
bool fullscreen = false;

//D3D declarations
const int framebuffer_count = 3;                                // triple buffering
ID3D12Device *render_device;
IDXGISwapChain3 *render_swapChain;                              // Switching between render targets
ID3D12CommandQueue *command_queue;                              // container for command lists
ID3D12DescriptorHeap *descriptorheap_rtv;                       // Holds resources like render targets
ID3D12Resource *render_targets[framebuffer_count];           
ID3D12CommandAllocator *command_allocators[framebuffer_count];  // One per each framebuffer * thread (we only have one thread for now)
ID3D12GraphicsCommandList *command_list;                        // A command list we can record commands into and then execute them to render a frame
ID3D12Fence1 *fence[framebuffer_count];                         // An object that is locked while 

//User made functions
bool window_init(HINSTANCE hInstance, int showWnd, int width, int height, bool fullscreen);
void window_loop();
LRESULT CALLBACK window_Callback(HWND window_handle, UINT message, WPARAM WParam, LPARAM LParam);

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
                     LPSTR lpCmdLine,         //Command line for program, basically replaces argv and argc
                     int nShowCmd)            //No idea what this does seems to always be  10
{
    //Initialize and create the window
    if (!window_init(hInstance, nShowCmd, width, height, fullscreen))
    {
        //If for whatever reason we could not initialize correctly show a message box with error and make used press ok
        MessageBox(0, "Window Initialization - failed!",
                   "Error", MB_OK);
        return 0;
    }

    //Run main render loop
    window_loop();

    return 0;
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
bool window_init(HINSTANCE hInstance, int showWnd, int width, int height, bool fullscreen)
{

    //Check if we want the program to run fullscreen or not
    if (fullscreen)
    {
        //getting monitor information
        HMONITOR monitor = MonitorFromWindow(window_handle, MONITOR_DEFAULTTONEAREST);
        MONITORINFO mi = {sizeof(mi)};
        GetMonitorInfo(monitor, &mi);

        width = mi.rcMonitor.right - mi.rcMonitor.left;
        height = mi.rcMonitor.bottom - mi.rcMonitor.top;
    }
    //0. Create winclass object
    WNDCLASSEX window_struct = {0};
    window_struct.cbSize = sizeof(WNDCLASSEX);
    window_struct.style = CS_VREDRAW | CS_HREDRAW;         // The window will redraw on vertical or horizontal movement or size adjustment
    window_struct.lpfnWndProc = window_Callback;           // A pointer to the window procedure: function that handles window messages
    window_struct.cbClsExtra = 0;                          // Number of extra bytes to allocate after the window class struct? Why would you want to do this?
    window_struct.cbWndExtra = 0;                          // Number of extra bytes to allocate following window instance? Again why would you even want this?
    window_struct.hInstance = hInstance;                   // A handle to the program instance that is in charge of the window can use get module handle to get it
    window_struct.hIcon = LoadIcon(NULL, IDI_APPLICATION); // A handle to the icon, if the member is null the system provides a default
    window_struct.hCursor = NULL;                          // A handle to the cursor
    window_struct.lpszMenuName = NULL;                     // A menu name
    window_struct.hbrBackground = NULL;                    // A handle to background brush?? No idea what this is but if set to null the application must paint its own background
    window_struct.lpszClassName = window_name;             // The name of the class
    window_struct.hIconSm = LoadIcon(NULL, IDI_APPLICATION);

    //1. Registering the winclass object technically returns a value if the class has not been registered correctly, but only does it if the program is not being run on windows NT
    if (!RegisterClassEx(&window_struct))
    {
        MessageBox(0, "Error registering class!",
                   "Error", MB_OK | MB_ICONERROR);
        return false;
    }

    //void CreateWindowA(lpClassName, lpWindowName, dwStyle,  x, y, nWidth, nHeight, hWndParent, hMenu, hInstance, lpParam );
    //2. Creating the window
    window_handle = CreateWindowExA(0,
                                    window_name,
                                    window_title,
                                    WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                                    CW_USEDEFAULT,
                                    CW_USEDEFAULT,
                                    width,
                                    height,
                                    0,
                                    0,
                                    hInstance,
                                    0);

    //I have no idea how this could fail, but it might. So this checks for that
    if (!window_handle)
    {
        MessageBox(0, "Error creating window!",
                   "Error", MB_OK | MB_ICONERROR);
        return false;
    }

    //If we want it to be a fullscreen window we gotta get rid of the style at the top
    if (fullscreen)
    {
        SetWindowLong(window_handle, GWL_STYLE, 0);
    }

    ShowWindow(window_handle, showWnd);
    UpdateWindow(window_handle);

    //The window is complete
    return true;
}

//3. Manage messages
/*
Windows does not start sending messages by default, it needs someone to start pulling them off a queue. Every tiem you have an application in windows a queue is created.
We need to loop through those messages and extract them.
If bot of the filter values are zero then windows returns all of the messages
// We let it write to our message address
// Get messages from all of our windows
// Filter for specific kinds of messages
// Also a filter
*/
void window_loop()
{
    MSG msg;

    while (true)
    {
        if(PeekMessage(&msg, NULL, 0,0, PM_REMOVE))
        {
            if(msg.message == WM_QUIT) break;

            TranslateMessage(&msg); // Translate keyboard messages into more pproper messages ????
            DispatchMessage(&msg);  // Actually deal with message
        }
        else{
        //Render loop
        OutputDebugStringA("YEEET\n");
        }
    }
}

/*
    This function handles messages sent by the OS
*/
LRESULT CALLBACK window_Callback(HWND window_handle, //Essentially like a pointer to the window
                                 UINT message,       //Message sent by the OS
                                 WPARAM WParam,      //Additional message info if needed
                                 LPARAM LParam)      //Additional message info again but no idea what difference there is between this and the previous system
{
    LRESULT Result = 0; //Output code to tell windows how everyhthing was handled

    switch (message)
    { //React to all these different messages

    case WM_KEYDOWN:
        if (WParam == VK_ESCAPE)
        {
            if (MessageBox(0, "Are you sure you want to exit?", "Really?", MB_YESNO | MB_ICONQUESTION) == IDYES)
            {
                DestroyWindow(window_handle);
            }
        }
        break;

    case WM_DESTROY: // After the window is destroyed
        PostQuitMessage(0);
        break;

    default: // For Everything else we run the default windows procedure that takes care of all other messages
        Result = DefWindowProc(window_handle, message, WParam, LParam);
        break;
    }
    return Result;
}