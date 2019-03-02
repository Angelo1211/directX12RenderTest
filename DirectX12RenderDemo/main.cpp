#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d12.h>
#include <dxgi.h>
#include <dxgi1_6.h>
#include <d3dcompiler.h>
#include <DirectXMath.h>
#include "d3dx12.h"
#include <string>

#define SAFE_RELEASE(p) { if ( (p) ) { (p)->Release(); (p) = 0; } }

using namespace DirectX;

//Globals
HWND window_handle = NULL;
LPCTSTR window_name = "Michael Scott";
LPCTSTR window_title = "DirectX12 Demo Window";
int width = 800;
int height = 600;
bool fullscreen = false;
bool running = true; // exit when this becomes false

//D3D declarations
const int framebuffer_count = 3; // triple buffering
ID3D12Device *renderer_device;
IDXGISwapChain3 *renderer_swapchain;      // Switching between render targets
ID3D12CommandQueue *command_queue;        // container for command lists
ID3D12DescriptorHeap *descriptorheap_rtv; // Holds resources like render targets
ID3D12Resource *renderer_targets[framebuffer_count];
ID3D12CommandAllocator *command_allocators[framebuffer_count]; // One per each framebuffer * thread (we only have one thread for now)
ID3D12GraphicsCommandList *command_list;                       // A command list we can record commands into and then execute them to render a frame
ID3D12Fence1 *renderer_fence[framebuffer_count];               // An object that is locked while command list is executed by the gpu. We need as many as we have allocators
HANDLE renderer_fence_event;                                   // A handle to our event for when the fence is unlocked by the gpu
UINT64 renderer_fence_value[framebuffer_count];                // This value is incremented each frame. Each fence has their own value
ID3D12PipelineState *renderer_pipeline;                        // Pso containing our default pipeline state
ID3D12RootSignature *renderer_rootsig;                         // We use it to say that the Input Assembler will be used, which means we will bind a vertex buffer containing info about each vertex
D3D12_VIEWPORT renderer_viewport;                              // We only have one viewport because it will be drawing to a whole render target
D3D12_RECT renderer_scissorRect;                               // Says where to draw and hwere not to draw.
ID3D12Resource *renderer_vertexBuffer;                         // Where we store our vertices
D3D12_VERTEX_BUFFER_VIEW renderer_vertexBuffer_view;           // Describes the address, stride and total siez of our vertex buffer and a pointer to the vertex data in gpu memory
int frame_index;                                               // Current rtv we are on
int descriptorSize_rtv;                                        // Size of the rtv descriptor on the device  (all front and back buffers will be the same size)

struct Vertex
{
    XMFLOAT3 pos;
};

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

    //we want to ait for hte gpu to finish executing commands before we release everything
    renderer_wait();

    //close the fence event
    CloseHandle(renderer_fence_event);

    //clean up after ourselves
    renderer_cleanup();

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

    while (running)
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
            //run game code
            general_update(); //Update engine logic
            //Execute the commandqueue (rendering the scene is the result oft he gpu executing the command lists)
            renderer_render();
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
        running = false;
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
    renderer_swapchain = static_cast<IDXGISwapChain3 *>(swapchain_temp);
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
    D3D12_DESCRIPTOR_HEAP_DESC heap_desc = {};
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
    CD3DX12_CPU_DESCRIPTOR_HANDLE handle_rtv(descriptorheap_rtv->GetCPUDescriptorHandleForHeapStart());

    // Create a RTV for each buffer (triple buffering will make 3 RTV)
    for (int i = 0; i < framebuffer_count; ++i)
    {
        //First we get the nth buffer in the swap chain and store it in the n position of the resource array
        result = renderer_swapchain->GetBuffer(i, IID_PPV_ARGS(&renderer_targets[i]));
        if (FAILED(result))
        {
            return false;
        }

        //Then we create arender target view which binds the swap chain buffer to the rtv handle
        renderer_device->CreateRenderTargetView(renderer_targets[i], nullptr, handle_rtv);

        handle_rtv.Offset(1, descriptorSize_rtv);
    }

    // -- Creating Command Allocators -- //
    /*
        Command allocators allocate memory on the GPU for the commands we want to execute by calling execute on the command queue and providing a command list with the command we want to
        execute. We are using triple buffering, so we need to create 3 command allocators. We need three because we cannot reset a command allocator while the GPU is executign a command list
        that is associated with it. To create a command allocator we use the createcommandallocator of hte device interface. 
        1. The type of the allocator. We can have either a direct command, or a bndle command allocator. The direct command allocater can be associated with direct command lists, which are executed on the GPU
            by callign execute on a command queue with the command list. A bundle command allocator stores commands for bundles. Bundles are used multiple times for many frames, so we do not want bundles to be
            on the same command allocator as direct commands, because direct command allocators are reset every fra,e. We do not want to reset bundles, otherwise thta is wasteful
        2. the tyupe id oft he interface we will be using
        3. pointer to a poitner to a command allocator interface
    */

    for (int i = 0; i < framebuffer_count; ++i)
    {
        result = renderer_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&command_allocators[i]));
        if (FAILED(result))
        {
            return false;
        }
    }

    // -- Creating the command lists -- //
    /*
        You want as many command lists as you have threads recording commands. We are not making a multi threaded app, so we only need one. 
        Command lists can be reset immediately after we call execute on a command queue with taht command lists. This is why we only need one command list but 3 allocators. 
        To create a command list we can call the createCommandlist method.
        1. This specifies which GPU to use
        2. A struct which specifies which type of command list we want to create
        3. We need to specify a command allocator that will store the commands on the gpu made by the command list
        4. Starting pipeline state object for the command list. It is a pointer to the pipelinestate interface. 
            At least need to speficy a vertex shader. But right now we are only clearing the render target so we can leave it to null. 
        5,6 the same id and pointer to pointer thign we have seen so many times now

        We need to use an enum to determine which kind of command list type we want
        1. A Direct command list is one where commands can be executed by the GPU. This is what we want to create.
        2. A bundle is a list of commands that are used often. Cannot be exectued directly by a command queue, a direct command list must execute the bundles. It inherits all pipeline state except  
            for the current set PSO. 
        3. A compute command list is for compute shaders
        4. A copy command list option

        We need to create a command list so that we can execute our clear render target command. 
        We do this by specifying the D3D12 command list type direct
        Since we only need one command list which is reset each frame when we specify a command allocator, we jsut create this command list with the first command allocator
        When a command list is created, it is created in the "recording " state. We do not want to record the command list yet so we close the command list after creating it.
    */

    //Create the command list with the first allocator
    result = renderer_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, command_allocators[0], NULL, IID_PPV_ARGS(&command_list));
    if (FAILED(result))
    {
        return false;
    }

    // Command lists are created in a recording state. Our main loop will set up for recording again so let's close it for now.

    // -- Creating a Fence and a & Fence Event -- //
    /*
        The final initialization (woohoo!!)
        Requires creating a fence and a fence event
        We are only using a single thread, so we only need one fence event, but since we are triple buffering, we need three fences, one for each frame buffer. We also have three current fence value
        reprecented in the fence value array. 

        The first thing we will do is create 3 fences by using the createfence function from the device interface (for each fence)
        1. initial value we want the fence to start with
        2. The flag is for a shared fence, we are not sharing fence, so we can set it to none.
        3 & 4 typical id type and pointer to pointer stuff 

        Once we create all three fences and initialize the fence value array we create a fence event using the windows createevent function:
        1. This is a pointer toa security attribute structure, setting this to null will use the default security structure
        2. If this is set to true we will have to atuomatically reset the even to not triggered by using the resetevent function 
            setting this to false will cause the even to automaticall reset to not tirggered after we have waited for hte fence event
        3. setting this to true will cause the initial state to be signaled, we dont want this
        4. setting this will make the event be created without a name
    */

    //Creating the fence
    for (int i = 0; i < framebuffer_count; ++i)
    {
        result = renderer_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&renderer_fence[i]));
        if (FAILED(result))
        {
            return false;
        }
        renderer_fence_value[i] = 0;
    }

    //create a handle to the fence event
    renderer_fence_event = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    if (renderer_fence_event == nullptr)
    {
        return false;
    }

    // -- Creating Root signature -- //
    /*
        we need to create a root signature usinc the root signature desc struct
        1. This is the number of slots our root signature will have. A slot is a root parameter, might be a constant, a descriptor or a descriptor table.
        2. An array of root parameter structure which define each of the root parameters this root signature contains
        3. This is the number of static smaplers the root signature will contain
        4. An array of static sampler desc struct these struct define static samplers
        5. A combination of flaps ored together

        Theres are flags taht may be used when creating a root signature. In this tutorial we are only using the root signature that tells the pipeline to use the input assembler so we can pass the 
        vertex buffer through the pipeline. The root signature will not have any root parameters for now.
        
        To tell the pipeline to use the input assembler we must create a root signature with the root signature flag allow inpu assembler inptu layour flag. Without this flag the input assembler will
        not be used, and calling draw number of vertices will call the vertex shader number of vertices times with empty vertices. Basically we can create the vertices in teh vertex shader using the 
        vertex index. We can pass these to the geometry shader to create more geometry. BY not using the flag we same one slot in the root signature that can be used for a root constant, root descriptor 
        or descriptor table. The optimization ism inimal

        If we use the layout flag we must create and use an input layout
        In this tutorial we are going to pass a vertex buffer through the pipeline so we specify the d3d12 flag
        The other flags are used to deny stages of the pipeline access to the root signature. When using resources or the root signature you want to deny any shaders taht do not need access so that the
        GPU can optimize. Allowing all shaders access to everythign slows down performance. A lot of d312 functions allow to you pass an id3d blob pointer to store error messages in. You can pass a 
        nullptr if you do not care to read the error. You can get a null terminated char array from the blob returned from these functions by calling the getbufferpointer method of the blobl.

        We will define and create the root signature in code at runtime. IT could also be defined in hlsl instead 
        The first thing we will do is fill ut a root signature desc  struct. We wnat the input assembler so we will specify that flag. 
        once we have that description we will serialize it into bytecode. We will use that bytecode to create a root signature object
    */

    //create the root signature
    CD3DX12_ROOT_SIGNATURE_DESC rootSig_desc;
    rootSig_desc.Init(0, nullptr, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

    ID3D10Blob *signature;
    result = D3D12SerializeRootSignature(&rootSig_desc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, nullptr);
    if (FAILED(result))
    {
        return false;
    }

    result = renderer_device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&renderer_rootsig));
    if (FAILED(result))
    {
        return false;
    }

    // -- Compiling vertex and pixel shaders -- //
    /*
        When creating a shader you msut provide a pointer to an ID3DBlob containing the sahder bytecode. When debugging, you want to compile the shaader files during runtime to catch any errors
        in the shader. We can compile shader code at runtime using the td3dcompilefromfile function. This function compiles the shader code to shader bytecode and stores it in an Id3dblob object.
        When you release you want to compile the shader to to compiled shader object files and load those raather than compiling shader code during runtime at initialization.
        1. filename that contains the shader code
        2. an array of shader macro structures that define shader macros. set to nullptr if not shaders are used
        3. a pointer to an include interface which is used to handle #includes in the shader code. 
        4. name of the shader function, we keep it to main
        5. this is the shader model that we will like to use. We will use shader model 5.0
        6. compile option flags ored together
        7. more compile flags used for effect files, this will be ignored for now
        8. poiinter to an id3dblocb that will pint to the compiled shader bytecode
        9. pointer to another blob that will hold any errors that occur while compiling the shader code

        If we get an erro we can access it with taht last parameter. The parameter is a null terminated string. We can cast the return of the gebufferpointer of the blob containt the error. 
        When we create the pso we need to provide a d3d12 shader bytecode structure which contains the shader bytecode and the size of the shader bytecode
        We get a pointer to the shader bytecode with thet getbuffer pointer method of the id3dblob we passed to d3dcompilefromfile we can get the size of the bytecode with the getbuffersize method of id3dblob

    */
    // create vertex and pixel shaders

    //compile vertex shader

    char buf[256];
    GetCurrentDirectoryA(256, buf);
    OutputDebugStringA(buf);

    ID3DBlob *shader_vertex; //vertex shader bytecode
    ID3DBlob *shader_error;  //vertex shader bytecode
    result = D3DCompileFromFile(L"DirectX12RenderDemo/vertex.hlsl",
                                nullptr,
                                nullptr,
                                "main",
                                "vs_5_0",
                                D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION,
                                0,
                                &shader_vertex,
                                &shader_error);
    if (FAILED(result))
    {
        return false;
    }

    //Fill out the shader bytecode structure which is just a pointer to the shader bytecode and the size of the shader bytecode
    D3D12_SHADER_BYTECODE shader_vertex_bytecode = {};
    shader_vertex_bytecode.BytecodeLength = shader_vertex->GetBufferSize();
    shader_vertex_bytecode.pShaderBytecode = shader_vertex->GetBufferPointer();

    //compile pixel shader
    ID3DBlob *shader_pixel; //vertex shader bytecode
    result = D3DCompileFromFile(L"DirectX12RenderDemo/pixel.hlsl",
                                nullptr,
                                nullptr,
                                "main",
                                "ps_5_0",
                                D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION,
                                0,
                                &shader_pixel,
                                &shader_error);
    if (FAILED(result))
    {
        return false;
    }

    //Fill out the shader bytecode structure which is just a pointer to the shader bytecode and the size of the shader bytecode
    D3D12_SHADER_BYTECODE shader_pixel_bytecode = {};
    shader_pixel_bytecode.BytecodeLength = shader_pixel->GetBufferSize();
    shader_pixel_bytecode.pShaderBytecode = shader_pixel->GetBufferPointer();

    // -- Creating an Input layout-- //
    /*
        An input layout describes the vertices inside the vertex buffer that are passed to the input assembler. 
        The input assembler will use the inpute layout to organize and pass vertices to the stages of the pipeline
        To create an input layout we fill out an array of input element desc structure sone for each attribute of the vertex structure such  as position texture coordinates or color
        1. Name of the parameter. The input assembler will associate the attribute to an input with the same semantic name in the shaders Can be anything as long as it matches shader
        2. semantic index, only needed if you have more than one element with the same semantic name, kind of like color1 and color2
        3. a DXGI format enumeration. The format the attribute is in. For example a float of 3 positions xyz with 4 bytes per value maps to a 32 bit argument and is mapped to a float3 parameter in the shader.
        4. you can bind multiple vertex buffers to the input assembler each vertex buffer is boudn to a slot, we are only binding one vertex buffer at a time so we set it to zero
        5. This is the offest in bytes from the beginning of the vertex structure to the start of this attribute. The first attribute will always be zero. WE only have one attribute, position. so we set this to zero.
            when we get color we weill have a second attribute color which we will need to set to 12. 
        6. Specifies if the element is per vertex or per instance. More important when we get to instancing. For now we are not instancing.
        7. NUmber of instances to draw before going to the next element. If we set the d3d12 input classification per vertex data we must use 0

        Once we create the input element array we fill out a a input layer desc struct. This structure will be passed as an argument when we create a pso
        If we are not using the input assembler as defined in the root signature there would be no need for an input layout for any pso that are associated with the root signature.
        We can get the size / number of elements of an array in c++ by using the sizeof(array) and divide that by the sizeof element to get the elemetns isnsde. 
    */

    //Creating the input layout
    D3D12_INPUT_ELEMENT_DESC rendererer_layout[] =
        {
            {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0}};

    // fill out an input layout description structure
    D3D12_INPUT_LAYOUT_DESC renderer_input_layout_Desc = {};

    // We get the number of elemeents in an array by sizeof(array) / sizeof(arrayelementtype)
    renderer_input_layout_Desc.NumElements = sizeof(rendererer_layout) / sizeof(D3D12_INPUT_ELEMENT_DESC);
    renderer_input_layout_Desc.pInputElementDescs = rendererer_layout;

    // -- Creating a Pipeline State Object PSO -- //
    /*
        For most applications you have many PSO's. For now we only need one
        We need to fill the D3D12 graphics pipeline state desc structure
        01. REQUIRED: a pointer to the root signature
        02. REQUIRED: a pointer to the vertex shader bytecode
        03. NOT REQUIRED: a pointer to the pixel shader bytecode
        04. NOT REQURIED: a pointer to the domain shader bytecode
        05. NOT REQUIRED: a pointer to the hull shader bytecode
        06. NOT REQUIRED: a pointer to the geometry shader bytecode
        07. NOT REQUIRED: stream output, used to send data from the pipeline after geometry or vertez to the app
        08. REQUIRED: blend state, used for blending or transparency for now we havea default blend state.
        09. REQUIRED: sample mask multisampling stuff 
        10. REQUIRED: state of the rasterizer. We will use default
        11. NOT REQUIRED: state of the depth/stencil buffer. 
        12. NOT REQUIRED: a d3d12 input layout desc struct defining layout of a vertex
        13. NOT REQUIRED: a buffer strip cut value enum used when a triangle strip topology is defined
        14. REQUIRED: a topology type struct defining the way vertices are put together
        15. REQUIRED: number of render target formats in the RTV parameter
        16. REQUIRED: an array of enumerations explaining the format of each render target
        17. NOT REQUIRED: an array of dxgi format enums explaining the format of each depth/stencil buffer. must be the same format as the depth stencil buffers used
        18. REQUIRED: the sample coutn and quality for multi-sampling 
        19. NOT REQUIRED: a bit mask saying which gpu adapter to use, we are only using one gpu so this is zero
        20. NOT REQUIRED: a way to cache PSO's into files
        21. NOT REQUIRED: a way to put debug info into the pipeline stat object
    */

    // Create a pipeline state object

    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso_desc = {};
    pso_desc.InputLayout = renderer_input_layout_Desc;
    pso_desc.pRootSignature = renderer_rootsig;
    pso_desc.VS = shader_vertex_bytecode;
    pso_desc.PS = shader_pixel_bytecode;
    pso_desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pso_desc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    pso_desc.SampleDesc = buffer_sample;
    pso_desc.SampleMask = 0xffffffff;
    pso_desc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    pso_desc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    pso_desc.NumRenderTargets = 1;

    //create the pso
    result = renderer_device->CreateGraphicsPipelineState(&pso_desc, IID_PPV_ARGS(&renderer_pipeline));
    if (FAILED(result))
    {
        return false;
    }

    // -- Creating a vertex Buffer -- //
    /*
        Vertex buffers are a list of vertex structures. To use a vertex structure we must get it to the GPUthen bind that vertex buffer to the input assembler.
        To get a vertex buffer to the GPU there are two options. The first one is to use an upload heap and upload the vertez buffer to the GPU each frame. This is slow since we need
        to copy teh vertex buffer from ram to video memory every frame. Thbe second option is to us an upload heap to upload the vertex buffer to the gpu then compy the data from the upload heap
        to the default heap. Teh default heap will stay in memory until we overwrite or release it. Teh second approach is preferable as you only need to copy the data once when you need it for a while
        and it is the way we will do it.

        We create a list of vertices and store them in the vList array. Here we reate 3 vertices, defined already in view sapce which make up a triangle
        to create a resource heap we use the create commited resource method of the device interface
        1. A structure defining the heap properties we will use a helper struc to create the type of heap we want
        2. A heap flag enumeration. WE will not have any flags
        3. A structure describing the heap. we will use another helper structure
        4. A resource state enum. This is the intiial state teh heap will be in. For the upload bugfger we want it to be in the read state. For the default heap we want it to be a copy destination
            once we copy the vertex buffer to the default heap we will use ar esource abrrier ro transition the default heap from a copy destionation state to a vertex constant buffer state
        5. A clear value structure. If this was ar ender target or depth stencil we coudl set this vcalue to the value the depth stencil buffer orrender target would usually get cleared to 
            the gpu can do some optimizations to increase the performance of clearing a resource. Our resource is a vertex buffer, so we set thsi value to nullptr
        6. Unique identifier for the type of the resulting resource interface
        7. A pointer to a pointeer to the resoruce inter face object


        We can set the name of the heap usign the setname method of the interface. This is useful for graphics debugging.
        Once we create a vertex buffer (list of vertices ) we create an upload it to the default heap. The upload heap is used to upload the vertex buffer to the gpu so we can copy the data
        to the default heap which will stay in memory until we either overwrite or release it
        
        WE can copy the data from the upload heap to the default heap using the update subresources function
        1. This is the command list we will use to creat thsi command which will copy the contents oft he upload heap to the defualt heap
        2. This is the destination of the coy command. in our case it will be the default heap but it could bea readback hap
        3. This is where we will copy the data from. here the upload heap but could also be default heap
        4. number of bytes we want to offeset the start from. We want the whole vertex buffer to be copied so we will not offset at all
        5. The index of the first subresource to start copying. Only have one so this will be zero
        6. number of subresources we want to copy. We only have one so we set this to 1
        7. Pointer to a d3d12 subresource data structue. This struc contains a pointer to the memory where our data is and the size in bytes of the resource

        Once we create a copy command our command list stores it in its command allocator. waiting to be executed. Before we can use the vertex buffer stored in the default heap we must
        make sure it is finished uploading and copying to the default heap
        we close the command list tehn execuite it with the command queue we increment the fence value for this frame and tell the command queue to increment the fence on the gpu side.
        Incrementing a fence is again a command which will ge exectued once the command list finish executing

        After we execute the copy command and set the fence we need to fill out the vertex buffer view. This isa d3d12 vertex buffer view. 
    */

    // Create the vertex buffer

    //a triangle
    Vertex vertex_list[] = {
        {{0.0f, 0.5f, 0.5f}},
        {{0.5f, -0.5f, 0.5f}},
        {{-0.5f, -0.5f, 0.5f}},
    };

    int vertex_buffer_size = sizeof(vertex_list);

    //create default heap
    //default heapa is memory on the gpu only the gpu has accessto this memory.
    //to get data into this heap we will have to upload the data using an upload heap
    renderer_device ->CreateCommittedResource(&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
                                              D3D12_HEAP_FLAG_NONE,
                                              &CD3DX12_RESOURCE_DESC::Buffer(vertex_buffer_size),
                                              D3D12_RESOURCE_STATE_COPY_DEST,
                                              nullptr,
                                              IID_PPV_ARGS(&renderer_vertexBuffer));

    renderer_vertexBuffer->SetName(L"Vertex Buffer Resource Heap");

    //create upload heap
    //used to upload data to teh gpu, cpu can write and gpu can read
    //we upload the vertex buffer using this heap
    ID3D12Resource *buffer_vertex_upload_heap;
    renderer_device->CreateCommittedResource(&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
                                             D3D12_HEAP_FLAG_NONE,
                                             &CD3DX12_RESOURCE_DESC::Buffer(vertex_buffer_size),
                                             D3D12_RESOURCE_STATE_GENERIC_READ,
                                             nullptr,
                                             IID_PPV_ARGS(&buffer_vertex_upload_heap));
    
    buffer_vertex_upload_heap->SetName(L"Vertex Buffer Upload Resource Heap");

    //Store vertex buffer in upload heap[
    D3D12_SUBRESOURCE_DATA vertex_data = {};
    vertex_data.pData       = reinterpret_cast<BYTE*>(vertex_list);
    vertex_data.RowPitch    = vertex_buffer_size;
    vertex_data.SlicePitch  = vertex_buffer_size;

    //We now create a command with the command list to copy data from.
    UpdateSubresources(command_list, renderer_vertexBuffer, buffer_vertex_upload_heap, 0, 0, 1, &vertex_data);

    //transition the vertex buffer dat from copy destination state to vertex buffer state
    command_list->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(renderer_vertexBuffer, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER));

    //now we execute the command list to upload the initial assets
    command_list->Close();
    ID3D12CommandList *p_command_lists[] = { command_list };
    command_queue->ExecuteCommandLists(_countof(p_command_lists), p_command_lists);

    // increment the fence value now, otherwise the buffer might not be uploaded by the time we start drawing
    renderer_fence_value[frame_index]++;
    result = command_queue->Signal(renderer_fence[frame_index], renderer_fence_value[frame_index]);
    if (FAILED(result)){
        running = false;
    }

    command_list->Close();
    // create a vertex buffer view for the triangle
    renderer_vertexBuffer_view.BufferLocation = renderer_vertexBuffer->GetGPUVirtualAddress();
    renderer_vertexBuffer_view.StrideInBytes  = sizeof(Vertex);
    renderer_vertexBuffer_view.SizeInBytes    = vertex_buffer_size;

    //Fill out viewport
    // The viewport will cover our entire  render target. commonly depth is between 0.0 and 1.0 in the screen space. The viewport will stretch the scene from viewpsace to screen space
    renderer_viewport.TopLeftX = 0;
    renderer_viewport.TopLeftY = 0;
    renderer_viewport.Width    = width;
    renderer_viewport.Height   = height;
    renderer_viewport.MinDepth = 0.0f;
    renderer_viewport.MaxDepth = 1.0f;

    //Filling out a scissor rect. The scissor rect is defined in screen space, anything outside the scissor rect will not make it to the pixel shader.
    renderer_scissorRect.left   = 0;
    renderer_scissorRect.top    = 0;
    renderer_scissorRect.right  = width;
    renderer_scissorRect.left   = height;

    return true;
}

//Currently does nothing, but we will add logic to this function that can run while the gpu is executign a command queue. We could have changed the render target color here if we wanted to change each frame
void general_update()
{
}

//This function is where we will add command to the command list.
//Which include changing the state of the render target
//Setting the root signature
//Clearing the render target
//Here we will be setting vertex buffers and calling draw in this function
void pipeline_update()
{
    HRESULT result;

    /*
        Command allocators cannot be reset while the GPU is executing commands from a command list associated with it. This is why we have fences and the fence event
        The first thing we do before we set this frames command allocator is make sure the GPU is finished executing the command list that was associated with this command allocator

        In the render function, after we call execute on the command queue we call signal on the command queue. This will insert a command after the command list we jsut executed that willi increment
        this frames fence value. We call wait fro previous frame whichc will check the value of the fence and see if it has been incremented. If it has, we know the command list that frame has been executed and it is safe
        to reset the command allocator.

        After we have rest this frames command allocator we want to reset the command list. Unlike command allocator, once we call execute on a command queue we can immediately reset the command list and reuse it.
        So if we reset the command list here giving it this frames command allocator and a null PSO.
        Resettign a command list put its on the recording state
    */

    //We have to wait fro the gpu to finish with the command allocator before we reset it
    renderer_wait();

    //We can only reset an allocator once the gpu is done with it
    //Resetting an allocator frees the memory that the command list was stored in
    result = command_allocators[frame_index]->Reset();
    if (FAILED(result))
    {
        running = false;
    }

    /*
        Reset the command list

        By resetting it we put it into a recording mode so we can record it to the command allocator
        the command allocator that we reference might have multiple command lists associated with it
        only one can be recording at a given time
        Make sure all other command lists associated with this command allocator are in the closed state
        Here you will pass an initial pipeline state object as the second parameter
        In the tutorial we are onyl clearing the rtv and do not need anything but an initial default pipeline, which is what we get by setting the second parameter to null
    */
    result = command_list->Reset(command_allocators[frame_index], renderer_pipeline);
    if (FAILED(result))
    {
        running = false;
    }

    /*
        Recording command with the command list

        We will only record commands to change state of the previous and current render target resources, and clearing the render target to a certain color.
        Render target resources must be in the render target state for hte output merger to output on. We can change the state of a resource using a resource barrier.
        This is done with the command list's interface resource barrier command. 
        We need a transition barrier because we are transitionting the state of the render target from the presetn state which it needs to be in for the swap chain to preseint it 
        to the render target state which it needs to be in for the output merger to ouput on  
        1. the number of barrier descriptions we are submitting
        2. the pointer to an array of d3d12 resource barrier descriptions. 

        This is where the helper library is useful again
        We can use a transition resource barrier  to apss to the render target source our current state and what we wnat to transition to. 
        Here we are transitioning the current render target from the present state to the render target state, so we can clear it with a color. 
        AFter we have finishing with our commands for this render target we want to transition it's state again, but this time from render target state to present state.
        So the swap chain can present it
        We want to clear the render target
        So we get a handle to the render target
        We use the descriptor handle structure and provide it with the first description in the rtv heap the index of the current frame and the size of each rtv descriptor
        basically get a pointer to the beginning of the descriptor heap and then increment taht pointer frame index times rtv descriptor size
        Once we hava descriptor handle we need to set the current render target to be the output of the ouput merger, 
        1. number of redner target descriptor handles
        2. a pointer to an array of render target descriptor handles
        3. If this is true then the previous pointer is a pointer to the begining of a contiguous chunk of descriptors in the descriptor heap.
            When getting the next descriptor D3D offsets the current descriptor handle by the size of the descriptor typw. 
            When setting it to false pRenderTargetDescriptors is a pointer to an array of render target descriptor handles. 
            This is less efficient than when setting this to true because to get the next descriptor D3D needs to dereference the handle in the array to get the render target. 
            Since we only have one render target, we set this to false because we are passing a reference to a handle to the only descriptor handle we are using.
        4. A pointer to a depth/stencil descriptor handle. We set this to null in this tutorial because we do not have depth/stencil yet. 

        Finally, to clear the render target we use the clear render target view command:
        1. Rendertarget view: a descriptor handle to the render target we want to clear
        2. An array of 4 floats representing red green blue and alpha
        3. number of rects to clear, 0 clears the entire render target
        4. pointer to an array of rect structures representing the rectangles on the render target you want to clear.
            nice if you don't want to lcear the entire render target, but we do so we set it to null

        Once we are finished with recording our commands, we need to close the command list. If we do not close it before we try to execute it the application will break. 
        Another note on closing: if you do something ilegal during hte command list your program will continue to run until you call close where it will fail. You must enable the debug layer
        in order to see what exactly failed when calling close. 
    */

    // Here we start recordign commands into the commandlist (which all the commands will be stored in the command allocator

    //transition the frame index render target from the present state to the render target state, so the command list draws it starting from here
    command_list->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(renderer_targets[frame_index], D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET));

    // here we again get the handle to our current render target view so we can set it as the render target in the output merger state of the pipeline
    CD3DX12_CPU_DESCRIPTOR_HANDLE handle_rtv(descriptorheap_rtv->GetCPUDescriptorHandleForHeapStart(), frame_index, descriptorSize_rtv);

    // Set the render target for the output merger stage (the ouput of the pipeline)
    command_list->OMSetRenderTargets(1, &handle_rtv, FALSE, nullptr);

    //Clear the render target by using the ClearRenderTargetView command
    const float clearColor[] = {0.0f, 0.2f, 0.4f, 1.0f};
    command_list->ClearRenderTargetView(handle_rtv, clearColor, 0, nullptr);

    //Drawing a triangle
    command_list->SetGraphicsRootSignature(renderer_rootsig);
    command_list->RSSetViewports(1, &renderer_viewport);
    command_list->RSSetScissorRects(1, &renderer_scissorRect);
    command_list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    command_list->IASetVertexBuffers(0, 1, &renderer_vertexBuffer_view);
    command_list->DrawInstanced(3,1,0,0);

    //Transition the frameindex render target from the render target state to the present state.
    //If the debug layer is enabled you receive a warning if present is called on a render target taht is not in the present state
    command_list->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(renderer_targets[frame_index], D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT));

    result = command_list->Close();
    if (FAILED(result))
    {
        running = false;
    }
}

void renderer_render()
{
    /*
        First thing we do is update the pipeline, that is record the command list by calling the update pipeline function 
        once the command list has been recorded we create an array of our command lists
        we conly have one command list but if we had multiple threads we would have a command list for each thread here we would organize our command list in the array in the order
        we want to execute them

        we can execute command lists by calling execute command lists on the command queue and provide the number of command lists to execute and a pointer to the command list array
        1. number of command lists to execute
        2. an array of command lists to execute 

        After we instruct the GPU to execute our command list we want to insert a command into the command queue to set the fence for this frame.
        The signal method basically inserts another command that sets a fence to a speccific value and signals a fence event
        we do this when we get back to this frame buffer so we can check to see if the gpu has finished executing the ocmmand list
        we know when it has finished because the signal command will have been executed and the fence will have been set to the fvalue we told it to set it to
        Finally we present the next back buffer by calling present method of the swapchain
    */
    HRESULT result;

    //Update the pipeline by sending commands to the commandQueue
    pipeline_update();

    //Create an array of command lists (only one for us sadly)
    ID3D12CommandList *command_temp_list[] = {command_list};

    //execute the array of command lists
    command_queue->ExecuteCommandLists(_countof(command_temp_list), command_temp_list);

    //This command goes in at the end of our command queue. we will know when our command queeu has finished becasuse the fence value will be set to fenceValue from the GPU since the command
    //queue is being executed on the GPU
    result = command_queue->Signal(renderer_fence[frame_index], renderer_fence_value[frame_index]);
    if (FAILED(result))
    {
        running = false;
    }

    //present the current backbuffer
    result = renderer_swapchain->Present(0, 0);
    if (FAILED(result))
    {
        running = false;
    }
}

//Just releases all the interface objects we have claimed. Before we release we want to make sure that the gpu has finished with everything before we start releasing things
void renderer_cleanup()
{
    //Wait for the gpu to finish all frames
    for (int i = 0; i < framebuffer_count; ++i)
    {
        frame_index = i;
        renderer_wait();
    }

    //Get swapchain out of fullscreen before exiting
    BOOL fs = false;
    if (renderer_swapchain->GetFullscreenState(&fs, NULL))
    {
        renderer_swapchain->SetFullscreenState(false, NULL);
    }

    SAFE_RELEASE(renderer_device);
    SAFE_RELEASE(renderer_swapchain);
    SAFE_RELEASE(command_queue);
    SAFE_RELEASE(descriptorheap_rtv);
    SAFE_RELEASE(command_list);

    for (int i = 0; i < framebuffer_count; ++i)
    {
        SAFE_RELEASE(renderer_targets[i]);
        SAFE_RELEASE(command_allocators[i]);
        SAFE_RELEASE(renderer_fence[i]);
    }

    SAFE_RELEASE(renderer_pipeline);
    SAFE_RELEASE(renderer_rootsig);
    SAFE_RELEASE(renderer_vertexBuffer);
}

void renderer_wait()
{
    /*
        Finally we have the wait for previous frame function
        This function is where the fencer and fence event are needed 
        The first thing we do is check the current value of the current frames fence
        If the current value is less than the value we wanted it to be we know the GPU is still executing commands for htis frame and we must enter the if block where we set the fence event which will
        get triggered once the fence value equals what we wnat it to equal

        We do this with the setevent on completion method of the fence interface
        1. this is the fvalue we want the fence to equal
        2. This is the event we want triggered when the fence equals value

        After we set up the even, we wait for it to be triggered. We do this with the windows wait for single object function
        1. this is the fence event we wnat to be triggered. IF it happens to be triggered in the very small amount of time between this funcion call and the time we set the fence event this function will trigger immediately
        2. this is the number of milliseconds we want to wait for hte fence event to be triggered. We can use the infinite macro which means this emthod will block forever or until the fence event is triggered.

        Once the GPU has finished executing the frames command list we increment our fence value for this frame and set the current back buffer in the swap chain and continue
    */

    HRESULT result;

    //swap the current rtv buffer index so we draw on the correct buffer
    frame_index = renderer_swapchain->GetCurrentBackBufferIndex();

    // if the current fence value is still less than "fence value", then we know that the gpu has not finished executing
    // the command queue since it has not reached the command que command
    if (renderer_fence[frame_index]->GetCompletedValue() < renderer_fence_value[frame_index])
    {
        //we have the fence create an event which is signaled once the fence's current value is the fencevalue
        result = renderer_fence[frame_index]->SetEventOnCompletion(renderer_fence_value[frame_index], renderer_fence_event);
        if (FAILED(result))
        {
            running = false;
        }

        //We will wiat until the fence has triggered the event that it's current value has reached "fenceValue". once it's value has
        //reached the fence value we know the command queue has finished executing
        WaitForSingleObject(renderer_fence_event, INFINITE);
    }

    //increment fencevalue for next frame
    ++renderer_fence_value[frame_index];
}
