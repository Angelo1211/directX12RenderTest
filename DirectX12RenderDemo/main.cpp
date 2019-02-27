#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <D3d12.h>
#include <dxgi1_6.h>
#include <d3dcompiler.h>
#include <DirectXMath.h>
#include "d3dx12.h"

//Globals
HWND window_handle = NULL;
LPCTSTR window_name = "Michael Scott";
LPCTSTR window_title = "DirectX12 Demo Window";
int width = 800;
int height = 600;
bool fullscreen = false;

//D3D declarations
const int framebuffer_count = 3; // triple buffering
ID3D12Device *renderer_device;
IDXGISwapChain3 *renderer_swapchain;      // Switching between render targets
ID3D12CommandQueue *command_queue;        // container for command lists
ID3D12DescriptorHeap *descriptorheap_rtv; // Holds resources like render targets
ID3D12Resource *renderer_targets[framebuffer_count];
ID3D12CommandAllocator *command_allocators[framebuffer_count]; // One per each framebuffer * thread (we only have one thread for now)
ID3D12GraphicsCommandList *command_list;                       // A command list we can record commands into and then execute them to render a frame
ID3D12Fence1 *fence[framebuffer_count];                        // An object that is locked while command list is executed by the gpu. We need as many as we have allocators
HANDLE fence_event;                                            // A handle to our event for when the fence is unlocked by the gpu
UINT64 fence_value[framebuffer_count];                         // This value is incremented each frame. Each fence has their own value

int frame_index;        // Current rtv we are on
int descriptorSize_rtv; // Size of the rtv descriptor on the device  (all front and back buffers will be the same size)

//User made functions
//Window window's handling
void window_loop();
bool window_init(HINSTANCE hInstance, int showWnd, int width, int height, bool fullscreen);
LRESULT CALLBACK window_Callback(HWND window_handle, UINT message, WPARAM WParam, LPARAM LParam);

//D3D functions
bool renderer_init();    // Init the d3d render context
void general_update();   // Update the engine logic
void pipeline_update();  // update command lists
void renderer_render();  // execute command lists
void renderer_cleanup(); // release objects and clean up memory
void renderer_wait();    // Wait until gpu is doen with command list

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
        return 1;
    }

    //Initialize the renderer
    if (!renderer_init())
    {
        MessageBox(0, "D3D12 Initialization failed!",
                   "Error", MB_OK);
        renderer_cleanup();
        return 1;
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
        if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
        {
            if (msg.message == WM_QUIT)
                break;

            TranslateMessage(&msg); // Translate keyboard messages into more pproper messages ????
            DispatchMessage(&msg);  // Actually deal with message
        }
        else
        {
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

bool renderer_init()
{
    HRESULT result; // Why do we need this??

    // Create the device context //  TODO:: REVIEW THIS FOR THE LOVE OF GOD

    // TODO:: Research wtf is this?? why 4?!
    IDXGIFactory4 *factory;
    // WTF? what does this achieve???
    result = CreateDXGIFactory1(IID_PPV_ARGS(&factory));
    if (FAILED(result))
    {
        return false;
    }

    //These represent the graphics card
    IDXGIAdapter1 *adapter;

    //We'll start looking for DX12 compatible graphics devices starting at index 0;
    int adapter_index = 0;

    //As soon as we get a good one we will set this to true
    bool adapter_found = false;

    //Find the first hardware GPU that supports DX12
    while (factory->EnumAdapters1(adapter_index, &adapter) != DXGI_ERROR_NOT_FOUND)
    {
        DXGI_ADAPTER_DESC1 description;
        adapter->GetDesc1(&description);

        //If we get a software device we skip it
        if (description.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
        {
            ++adapter_index;
            continue;
        }

        // What we want is a DirectX12 compatible device  (that is feature 11 or higher)
        // We run the create device function with a nullptr parameter to make sure the function will succeed before actually creating the device. The 4th parameter is where the pointer to the device would go
        result = D3D12CreateDevice(adapter, D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device), nullptr);
        if (SUCCEEDED(result))
        {
            adapter_found = true;
            break;
        }

        ++adapter_index;
    }

    //If you never found a valid adapter just quit
    if (!adapter_found)
    {
        return false;
    }

    //We now create the device and use a fancy macro to make sure the device inherits from the correct class.
    result = D3D12CreateDevice(adapter, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&renderer_device));

    // If for whatever reason the device was not created succesfully quit;
    if (FAILED(result))
    {
        return false;
    }

    // -- Create command queue --  //

    // Command queues executes the command lists which contain the commands to tell the gpu what to do
    // We use create command queue function to create the the command queue
    // We need to create a command queue description struct before moving forward

    // Use the default parameters
    D3D12_COMMAND_QUEUE_DESC command_description = {};

    // create the command queue
    result = renderer_device->CreateCommandQueue(&command_description, IID_PPV_ARGS(&command_queue));
    if (FAILED(result))
    {
        return false;
    }

    // -- Create swap chain --  //

    /*
     The swap chain is used to present the finished render target
     We will use triple buffering so we will also have to keep track of which render target should be rendering onto.
     The dxgi factory will create a IIDXGISWapChain, but we want an IDXGISwapChain3 in order to get the current backbuffer index. We can easily cast with no issue.

     We begin by filling a DXGI_SWAP_CHAIN_DESC struct in it we define:
        1. The display mode, the width, the height and the format. Uses another struct called DXGI mode desc
        2. The multisampling method we are using, uses another struct
        3. If the swap chain will be a render target or a shader input. 
        4. Number of backbuffers we want, we are triple buffered so three
        5. Output window where we will be displaying our backbuffer
        6. Whether we should display in full screen or windowed mode (kinda tricky what we are supposed to do here for an unlocked fps setting)
        7. The swap effect, which describes how a buffer is handled after it is presented. WE will use the dxgi flip discard effect
        8. Other flags, we will set to zero for now. 

     We fill a DXGI_MODE_DESC struct in it we define:
        1. The width resolution of our backbuffer. Default value is zero. IF zero it will set the width to be the width of the window. We will set it, but it could be zero and still work. So i'll do zero lol. 
        2. Same as above but with width
        3. Refresh rate, takes in a rational structure. If you leave zero then the default is zero. 
        4. The display format of our swap chain, described in the DXGI_FORMAT enumeration. The default is unkown and will cause an error. We will set it it 32 bit unsigned integer rgba format. 
        5. Scanline ordering, the default is unordered. We will leave it like that.
        6. Scaling: Defines what happens if the window is resized. 

     We fill a DXGI_SAMPLE_DESC struct in it we define:
        1. Count, the number of samples taken of each pixel. Default is zero, causes an error. We will use 1. 
        2. Quality of the sample taken. Default is zero. 
    
     Finally, we create the swapchain using the create swapchain method of the DXGI factory. 
        1. Pointer to a direct3d device that will write the images to the swap chains back buffer
        2. Reference to the swap chain description we talked above
        3. pointer to the swap chain interface. In our app we will use a IDXGI swapchain3 but this function returns a swapchain. We must static cast it to swapchain 3 to point to the created swapchain memory. 

    */

    //This describes our display mode and other display properties
    DXGI_MODE_DESC buffer_desc = {};
    buffer_desc.Width = width;
    buffer_desc.Height = height;
    buffer_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM; // Format of the buffer (RGBA with 9 bits per channel 32 bits total)

    //Multisampling description
    DXGI_SAMPLE_DESC buffer_sample = {};
    buffer_sample.Count = 1; //1 sample per pixel (no multisampling)

    //Describe the swap chain
    DXGI_SWAP_CHAIN_DESC swapchain_desc = {};
    swapchain_desc.BufferCount = framebuffer_count;
    swapchain_desc.BufferDesc = buffer_desc;
    swapchain_desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT; // Our pipeline will render to this very swap chain
    swapchain_desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;    // Our swap chain will discard the buffer after we call present
    swapchain_desc.OutputWindow = window_handle;                  // The window we will sample too
    swapchain_desc.SampleDesc = buffer_sample;                    // The description of how will do buffer sampling
    swapchain_desc.Windowed = !fullscreen;                        // Set to true when windowed, if you are in fullscreen god help you

    IDXGISwapChain *swapchain_temp;

    factory->CreateSwapChain(command_queue,    // The queue of commands will be flushed once the swap chain is created
                             &swapchain_desc,  // Give it the swap chain description that we created above
                             &swapchain_temp); // Store the created swap chain in a temp IDXGISwapChain interface

    //We cast it because we want to call the getcurrentbackbufferindex function 
    renderer_swapchain = static_cast<IDXGISwapChain3*>(swapchain_temp);
    frame_index = renderer_swapchain->GetCurrentBackBufferIndex();

    // -- Creating the Back Buffers (render target views ) Description Heap -- //
    /*
        The descriptor heap holds our render targets
        We start by filling the D3D12_DESCRIPTOR_HEAP_DESC structure to describe the descriptor heap we want to create
        1. A descriptor heap type enumerator. There are three types. CBV/SRV/UAV, sampler, RTV, DSV. We are creating an RTV so we will set it to that
        2. Number of descriptors we will store in the heap. We willl be doing triple buffering. So we will need 3 back buffers, which means we will have 3 descriptors.
        3. Flags: describes if the heap is shader visible or not. Shaders do not access RTVs, so we do not need this heap to be shader visible Non-shader visible heaps are not stored on the gpu so they are not cosntrained 
        4. This is a bit field that determines which GPU this heap is stored on, default is zero

        To create the descriptor heap we will call the CreateDescriptorHeap method of the device interface:
        1. This is a pointer to the structure we just filled out that described the heap we wanted to create
        2. The type id of the derscriptor heap we want to create
        3. This is avoid pointer to the RTV interface. 

        Once the RTV is created we need to get hte size of thr RTV type size on the GPU. There is no guarantee that a descriptor type on one GPU is the same as a descriptor size on another GPU
        Which is why we need to ask the device for the siE of a descriptor type size . We need the size of the type so we can iterate over the descriptors in the descriptor heap. 
        We do this by calling the getdescriptorhandleincrementsize method of the device
        1. The only parameter is the type of the descriptor that we want to find the size of 

        Once we have the descriptor type size we want to get a handle to the descriptor in the heap. There are two handles,the CPU and the GPU handles. 
        Because our descriptor is not shader visible it is only described on the CPU so we need a cpu handle
        The handles are basically like pointers, but only directx can use these pointers.
        We can get a handle to the first descriptor in a descriptor heap by calling the GetCPUDescriptorHandleForHeapStart method 
        There are some helper structure sthat will use to get this handle
        We can loop through the RTV descriptors in the heap by ofestting the current handle by the descriptor heap size we got form the getdescriptorhandleincrementsize function
        When we have the descriptor handle to the first RTV descriptor on the heap we point each rtv descriptor to the backbuffers in our swap chain

        We can get a pointer to the buffer in the swap chain by calling the getbuffre method of the swap chain interface. Using that method we can set our render target source to the swap chain buffers
        1. Index to the buffer we want to get
        2. the ype id of the interface we will store the pointer in
        3. A void pointer to a pointer to the interface we want to point to the buffer

        Now that we have 3 resources that point to the swap chain buffers we can create the RTVs using the device interafaces createrendertargetview method. 
        This method will create a descriptor that points to the resource and store it in a descriptor handle.
        1. Pointer to the resource
        2. A pointer to a D3D12 render target view desc this is used if we are using subresources. We can pass a nullptr here
        3. This is a handle to a cpu descriptor in a descriptor heap that will point to the render target resource

        To get to the next descriptor we can offest the descriptor by the type size. There is a helper structure we can call that will help us deal with this. 
    */

    //Describe the Render target views descriptor heap
    D3D12_DESCRIPTOR_HEAP_DESC  heap_desc = {};
    //Number of descriptors for this heap
    heap_desc.NumDescriptors = framebuffer_count;
    //This heap is a renter target view heap
    heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    //This heap will not be directly referenced by shaders, as this will just store the output of the pipeline. 
    //Otherwise we would set the flag  to visible
    heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE; 

    result = renderer_device->CreateDescriptorHeap(&heap_desc, IID_PPV_ARGS(&descriptorheap_rtv));
    if (FAILED(result))
    {
        return false;
    }

    // Get the size of the descriptor in this heap (this is a rtv heap so only the rtv descriptor heaps will be stored in it )
    // descriptor sizes may vary from device to device, which is why there is no set seize and we msut ask the device to give us the size
    // We will use this size to increment a descriptor handle offset
    descriptorSize_rtv = renderer_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    //Get a handle to the first descriptor in the descriptor heap. A handle is basically a pointer,
    //But we cannot literally use it like a c++ pointer. Only directx can handle it like it deserves
    CD3DX12_CPU_DESCRIPTOR_HANDLE rtvhandle(descriptorheap_rtv->GetCPUDescriptorHandleForHeapStart());
    //randomname
   


    return true;
}

void renderer_cleanup()
{
}