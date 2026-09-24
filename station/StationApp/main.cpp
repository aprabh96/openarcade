#include "globals.h"
#include "window_proc.h"
#include "session.h"
#include "config.h"
#include "logging.h"
#include "overlay.h"
#include "network.h"
#include "steam_games.h"
#include "steam_api.h"
#include <objidl.h> // For GDI+ / COM
#include <gdiplus.h> // For GDI+
#pragma comment(lib, "gdiplus.lib") // Link GDI+
#include <winhttp.h>
#pragma comment(lib, "winhttp.lib")

// <ai_context>
// Contains WinMain, basic initialization, message loop
// </ai_context>

// Unique mutex name for single instance check
#define SINGLE_INSTANCE_MUTEX_NAME L"StationApp_SingleInstance"

// Define the global token for GDI+
ULONG_PTR g_gdiplusToken = 0;

// Helper function to find and kill existing instances
void KillExistingProcesses() 
{
    DWORD currentPID = GetCurrentProcessId();
    
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap == INVALID_HANDLE_VALUE) {
        return;
    }
    
    PROCESSENTRY32 pe32;
    pe32.dwSize = sizeof(PROCESSENTRY32);
    
    // Get current exe filename
    WCHAR currentProcessPath[MAX_PATH];
    if (GetModuleFileNameW(NULL, currentProcessPath, MAX_PATH) == 0) {
        CloseHandle(hSnap);
        return;
    }
    
    // Extract filename only
    WCHAR* currentExeName = wcsrchr(currentProcessPath, L'\\');
    if (currentExeName == NULL) {
        currentExeName = currentProcessPath;
    } else {
        currentExeName++; // Skip the backslash
    }
    
    if (Process32First(hSnap, &pe32)) {
        do {
            if (_wcsicmp(pe32.szExeFile, currentExeName) == 0 && 
                pe32.th32ProcessID != currentPID) {
                // Found another instance, terminate it
                HANDLE hProcess = OpenProcess(PROCESS_TERMINATE, FALSE, pe32.th32ProcessID);
                if (hProcess != NULL) {
                    if (TerminateProcess(hProcess, 0)) {
                        OutputDebugStringW(L"Terminated existing process\n");
                    }
                    CloseHandle(hProcess);
                }
            }
        } while (Process32Next(hSnap, &pe32));
    }
    
    CloseHandle(hSnap);
    
    // Give the terminated processes time to clean up
    Sleep(500);
}

int APIENTRY WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow)
{
    Log("WinMain: Entered.");
    // First, kill any existing instances of this process
    KillExistingProcesses();
    Log("WinMain: KillExistingProcesses completed.");
    
    // Create/check for single instance mutex
    HANDLE hSingleInstanceMutex = CreateMutexW(NULL, TRUE, SINGLE_INSTANCE_MUTEX_NAME);
    Log("WinMain: Single instance mutex created.");
    if (GetLastError() == ERROR_ALREADY_EXISTS)
    {
        // Find the existing instance window
        HWND existingWindow = FindWindowW(L"QuitVRAppClass", L"Station Overlay (Client)");
        if (existingWindow)
        {
            // Bring the existing window to front
            ShowWindow(existingWindow, SW_SHOW);
            SetForegroundWindow(existingWindow);
        }
        else
        {
            // If we can't find the window but the mutex exists, close any orphaned instances
            HWND otherWindow = FindWindowW(L"QuitVRAppClass", NULL);
            if (otherWindow)
            {
                PostMessageW(otherWindow, WM_CLOSE, 0, 0);
                // Wait a moment for it to close
                Sleep(500);
            }
        }
        
        // Close our mutex handle and exit
        if (hSingleInstanceMutex)
            CloseHandle(hSingleInstanceMutex);
        Log("WinMain: Single instance check completed.");
        return 0;
    }
    
    g_hInst = hInstance;

    // Initialize GDI+
    Gdiplus::GdiplusStartupInput gdiplusStartupInput;
    Gdiplus::GdiplusStartup(&g_gdiplusToken, &gdiplusStartupInput, NULL);
    Log("GDI+ Initialized."); // Already present

    // Also good practice to initialize COM for WIC later
    HRESULT hrCom = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    Log("WinMain: COM initialization attempted.");
    if (SUCCEEDED(hrCom)) {
        Log("COM Initialized.");
        // Initialize WIC Factory right after COM is initialized
        HRESULT hrWic = CoCreateInstance(
            CLSID_WICImagingFactory,
            NULL,
            CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(&g_pWICFactory) // Use the global variable
        );
        if (SUCCEEDED(hrWic)) {
            Log("WIC Imaging Factory Initialized.");
        } else {
            Log("Error: Failed to create WIC Imaging Factory! HRESULT: " + std::to_string(hrWic));
            // Handle error appropriately - image loading won't work
            // Maybe show a message box?
        }
    } else {
        Log("Warning: Failed to initialize COM. HRESULT: " + std::to_string(hrCom));
    }
    Log("WinMain: COM and WIC Factory initialized checks completed.");

    // Initialize Media Foundation early
    HRESULT hr = MFStartup(MF_VERSION);
    if (FAILED(hr)) {
        Log("WARNING: Failed to initialize Media Foundation. HRESULT: " + std::to_string(hr));
        Log("Video playback will not be available.");
        MessageBoxW(NULL, L"Failed to initialize Media Foundation.\nVideo playback will not be available.", 
                   L"Media Foundation Error", MB_OK | MB_ICONWARNING);
    } else {
        Log("Media Foundation initialized successfully.");
    }
    Log("WinMain: Media Foundation initialized checks completed.");

    SetupINIPath();
    Log("WinMain: SetupINIPath completed.");
    EnsureIniExists();
    Log("WinMain: EnsureIniExists completed.");
    SetupCachePath();
    Log("WinMain: SetupCachePath completed.");
    EnsureCacheDirExists();
    Log("WinMain: EnsureCacheDirExists completed.");

    // --- ADD THIS BLOCK ---
    {
        std::string customGamesPath = g_iniFullPath.substr(0, g_iniFullPath.find_last_of("\\/")) + "\\custom_games.json";
        DWORD attrs = GetFileAttributesA(customGamesPath.c_str());
        if (attrs == INVALID_FILE_ATTRIBUTES) {
            Log("Creating empty custom_games.json file.");
            std::ofstream ofs(customGamesPath);
            ofs << "[]"; // Write an empty JSON array
            ofs.close();
        }
    }
    // --- END ADD ---

    LoadSettings();
    Log("WinMain: LoadSettings completed.");
    g_masterIP = LoadMasterIP();
    Log("WinMain: LoadMasterIP completed.");
    g_stationName = LoadStationName();
    Log("WinMain: LoadStationName completed.");
    g_sessionEndTime = LoadSessionEndTime();
    Log("WinMain: LoadSessionEndTime completed.");
    Log("WinMain: Attempting to load categories...");
    LoadCategories(); // Load game categories from INI
    Log("WinMain: LoadCategories completed.");

    const wchar_t CLASS_NAME[] = L"QuitVRAppClass";
    WNDCLASSEXW wcex;
    ZeroMemory(&wcex, sizeof(wcex));
    wcex.cbSize = sizeof(WNDCLASSEXW);
    wcex.lpfnWndProc = WndProc;
    wcex.hInstance = hInstance;
    wcex.lpszClassName = CLASS_NAME;
    wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wcex.hCursor = LoadCursor(NULL, IDC_ARROW);

    if (!RegisterClassExW(&wcex))
    {
        MessageBoxW(NULL, L"Failed to register window class!", L"Error", MB_OK | MB_ICONERROR);
        Log("RegisterClassExW failed.");
        if (hSingleInstanceMutex)
            CloseHandle(hSingleInstanceMutex);
        Log("WinMain: RegisterClassExW completed (or logged failure).");
        return 1;
    }
    Log("WinMain: RegisterClassExW completed.");

    HWND hWnd = CreateWindowW(CLASS_NAME, L"Station Overlay (Client)",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 1200, 970, // width increased from 800 to 1200 for category UI
        NULL, NULL, hInstance, NULL);

    if (!hWnd)
    {
        MessageBoxW(NULL, L"Failed to create window!", L"Error", MB_OK | MB_ICONERROR);
        Log("CreateWindowW failed.");
        if (hSingleInstanceMutex)
            CloseHandle(hSingleInstanceMutex);
        Log("WinMain: CreateWindowW completed (or logged failure).");
        return 1;
    }
    Log("WinMain: CreateWindowW completed.");

    ShowWindow(hWnd, nCmdShow);
    UpdateWindow(hWnd);
    Log("WinMain: Window shown and updated.");

    // Initialize VR overlay game list based on the default category
    if (g_selectedCategoryIndexVR != -1 && static_cast<size_t>(g_selectedCategoryIndexVR) < g_categories.size()) {
        Log("WinMain: Default category is selected (" + g_categories[g_selectedCategoryIndexVR]->name + "). Updating VR game list.");
        UpdateCurrentCategoryGameList_VR();

        // The old, premature data fetch logic that caused the race condition has been removed.
        // The new logic in WndProc now handles the initial data load reliably.
    }

    {
        char buf[512];
        sprintf_s(buf, "INI path: %s\nLoaded IP='%s', Station='%s', AutoOverlay=%d, sessionEnd=%lld",
            g_iniFullPath.c_str(), g_masterIP.c_str(), g_stationName.c_str(),
            (g_AutoEnableOverlay ? 1 : 0), (long long)g_sessionEndTime);
        Log(buf);
    }
    
    // Create media folder if it doesn't exist
    {
        wchar_t exePath[MAX_PATH];
        GetModuleFileNameW(NULL, exePath, MAX_PATH);
        PathRemoveFileSpecW(exePath);
        wcscat_s(exePath, MAX_PATH, L"\\media");
        
        DWORD attrs = GetFileAttributesW(exePath);
        if (attrs == INVALID_FILE_ATTRIBUTES) {
            Log("Creating media directory for video files...");
            if (CreateDirectoryW(exePath, NULL)) {
                Log("Media directory created: " + WStringToString(std::wstring(exePath)));
                
                // Inform user they need to place the video file in this directory
                std::string message = "ATTENTION: A media directory has been created at:\n" + 
                                       WStringToString(std::wstring(exePath)) + 
                                       "\n\nPlease place your logo-loop.mp4 video file in this directory.";
                MessageBoxA(NULL, message.c_str(), "Media Directory Created", MB_OK | MB_ICONINFORMATION);
            } else {
                Log("Failed to create media directory. Error: " + std::to_string(GetLastError()));
                MessageBoxW(NULL, L"Failed to create the media directory for video files.\nThe video background will not be available.", 
                           L"Media Directory Error", MB_OK | MB_ICONWARNING);
            }
        } else {
            Log("Media directory exists: " + WStringToString(std::wstring(exePath)));
            
            // Check if the video file exists
            std::wstring videoPath = std::wstring(exePath) + L"\\logo-loop.mp4";
            DWORD videoAttrs = GetFileAttributesW(videoPath.c_str());
            if (videoAttrs == INVALID_FILE_ATTRIBUTES) {
                Log("WARNING: Video file not found at: " + WStringToString(videoPath));
                
                // Inform user they need to place the video file in this directory
                std::string message = "ATTENTION: The video file was not found at:\n" + 
                                     WStringToString(videoPath) + 
                                     "\n\nPlease place your logo-loop.mp4 video file in the media directory.";
                MessageBoxA(NULL, message.c_str(), "Video File Missing", MB_OK | MB_ICONWARNING);
            } else {
                Log("Video file found: " + WStringToString(videoPath));
            }
        }
        Log("WinMain: Media folder check/creation completed.");
    }

    // 1) Resume session
    ResumeSessionIfNeeded(hWnd);
    Log("WinMain: ResumeSessionIfNeeded completed.");

    // 2) Auto overlay if needed
    // (Removed: persistent timer now handles this logic)

    Log("Station overlay started. Window displayed.");

    // 3) Auto-connect if IP/station are set
    if (!g_masterIP.empty() && !g_stationName.empty())
    {
        Log("WinMain: Attempting initial connection to master.");
        // Try to connect once initially
        ConnectToMaster(g_masterIP, 12345);
        
        // Set up a timer to try reconnecting every few seconds if not connected
        // This will handle both the initial connection and reconnection attempts
    }

    Log("WinMain: Entering message loop.");
    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    Log("Application exiting.");

    // Explicitly notify the master we're disconnecting
    if (g_connected) {
        SendToMaster("STATION_DISCONNECTING");
        // Give a moment for the message to be sent
        Sleep(100);
    }

    // Cleanup
    if (g_runRecvThread)
    {
        g_runRecvThread = false;
        if (g_recvThread.joinable())
            g_recvThread.join();
    }

    if (g_clientSocket != INVALID_SOCKET)
    {
        shutdown(g_clientSocket, SD_BOTH);
        closesocket(g_clientSocket);
        g_clientSocket = INVALID_SOCKET;
    }
    g_connected = false;

    // Only cleanup WSA once at program exit
    if (g_wsaInitialized)
    {
        WSACleanup();
        g_wsaInitialized = false;
    }
    
    vr::VR_Shutdown();
    
    logFile.close();
    
    // Release the single instance mutex
    if (hSingleInstanceMutex)
        CloseHandle(hSingleInstanceMutex);
    
    // Shutdown Media Foundation
    MFShutdown();
    Log("Media Foundation shutdown.");

    // Shutdown GDI+
    if (g_gdiplusToken != 0) {
        Gdiplus::GdiplusShutdown(g_gdiplusToken);
        Log("GDI+ Shutdown.");
    }

    // Release WIC Factory before CoUninitialize
    if (g_pWICFactory) {
        g_pWICFactory->Release();
        g_pWICFactory = nullptr;
        Log("WIC Factory Released.");
    }

    // Shutdown COM
    if (SUCCEEDED(hrCom)) { // Only uninitialize if initialization succeeded
        CoUninitialize();
        Log("COM Uninitialized.");
    }
    
    return (int)msg.wParam;
}