#include "globals.h"
#include "logging.h" // Include logging for potential use inside the async function
#include "steam_games.h" // Include definition for SteamGame struct
#include <memory>

// <ai_context>
// Definition of extern globals
// </ai_context>

// Define the mutex declared in globals.h
std::mutex g_steamVRCheckMutex;
bool g_steamVRWasRunning = false;
DWORD g_steamVRDetectedTime = 0;
bool g_initialDashboardToggleDone = false;
DWORD g_steamVRStartTimeForDashboardToggle = 0;

// ===================== Game Category Management =====================
std::vector<std::unique_ptr<GameCategory>> g_categories;
std::mutex g_categoriesMutex;
int g_selectedCategoryIndexVR = -1;
std::vector<std::string> g_currentCategoryGameAppIds_VR;
std::mutex g_VROverlayGameListMutex;

// Category list rectangles for overlay UI
D2D1_RECT_F g_rectCategoryList = {};
D2D1_RECT_F g_rectCatScrollUpButton = {};
D2D1_RECT_F g_rectCatScrollDownButton = {};
IDWriteTextFormat* g_pTextFormatCategoryList = nullptr;
int g_categoryListScrollOffset = 0;

HWND g_hListCategories = nullptr;
HWND g_hEditCategoryName = nullptr;
HWND g_hButtonCreateCategory = nullptr;
HWND g_hButtonDeleteCategory = nullptr;
HWND g_hButtonRenameCategory = nullptr;
HWND g_hButtonMoveCategoryUp = nullptr;   // <-- ADD THIS
HWND g_hButtonExportHTML = nullptr;       // <-- ADD THIS
HWND g_hButtonMoveCategoryDown = nullptr; // <-- ADD THIS
// --- ADD THIS ---
DWORD g_customGamePID = 0; // Initialize to 0 (no process)
// --- ADD THIS ---
std::string g_runningCustomGameAppId = ""; // Initialize to empty
// --- END ADD ---
// --- END ADD ---
HWND g_hButtonAddGameToSelectedCat = nullptr;
HWND g_hButtonRemoveGameFromSelectedCat = nullptr;
HWND g_hStaticGameCategoriesLabel = nullptr;
HWND g_hListGameMembership = nullptr;
HWND g_hStaticGamesInCategoryLabel = nullptr; // NEW
HWND g_hListGamesInCategory = nullptr;      // NEW
HWND g_hStaticCategoryGameCount = nullptr;  // Counter for games in selected category
// Simulate Category VR navigation buttons
HWND g_hButtonSimulateCatUp = nullptr;
HWND g_hButtonSimulateCatDown = nullptr;
HWND g_hButtonSimulateCatSelect = nullptr; // Optional
HWND g_hChkFilterGamesInCategories = nullptr;
// ===================== End Game Category Management =====================

// 1) Globals
HINSTANCE g_hInst = nullptr;
HWND g_hButtonQuit = nullptr;
HWND g_hButtonLaunchVR = nullptr;
HWND g_hButtonShowOverlay = nullptr;
HWND g_hButtonHideOverlay = nullptr;
HWND g_hButtonTestContinuous = nullptr;
HWND g_hButtonStopTestContinuous = nullptr;
HWND g_hComboSessionTime = nullptr;
HWND g_hEditCustomMinutes = nullptr;
HWND g_hButtonStartSession = nullptr;
HWND g_hButtonAddTime = nullptr;
HWND g_hButtonStopSession = nullptr;
HWND g_hStaticTimeLeft = nullptr;
HWND g_hEditLog = nullptr;
HWND g_hChkAutoOverlay = nullptr;
HWND g_hEditIP = nullptr;
HWND g_hButtonConnect = nullptr;
HWND g_hEditStationName = nullptr;
// --- ADD THIS ---
HWND g_hEditGameName = NULL;
// --- END ADD ---
HWND g_hEditGameDesc = NULL;     // Edit box for description
HWND g_hEditImagePath = NULL;    // Edit box for image path (read-only)
HWND g_hButtonBrowseImage = NULL; // Button to browse for image
HWND g_hButtonSaveCache = NULL;  // Button to save manual cache data

vr::VROverlayHandle_t g_MainOverlay = 0;
vr::VROverlayHandle_t g_ThumbnailOverlay = 0;

std::string g_logFilePath = "log.txt";
std::ofstream logFile("log.txt", std::ios::app);
std::mutex g_netMutex;

bool continuousOverlayRunning = false;
bool g_AutoEnableOverlay = false;
bool g_filterGamesInCategories = false;
bool g_SessionRunning = false;
bool g_bOverlayShutdownRequested = false; // Initialized to false
bool g_awaitingDashboardHide = false;
DWORD g_gameLaunchTime = 0;
const wchar_t* STEAMVR_PATH = L"C:\\Program Files (x86)\\Steam\\steamapps\\common\\SteamVR\\bin\\win64\\vrstartup.exe";

time_t g_sessionEndTime = 0;
int    g_SessionTimeSeconds = 0;

SOCKET g_clientSocket = INVALID_SOCKET;
bool   g_connected = false;
bool   g_runRecvThread = false;
std::thread g_recvThread;
std::string g_masterIP = "127.0.0.1";
std::string g_stationName = "Station1";
bool g_wsaInitialized = false;

std::string g_iniFullPath;
std::string g_cacheDirFullPath;

HWND g_hListGames = NULL;
HWND g_hButtonLaunchGame = NULL;
// --- ADD THIS BLOCK ---
HWND g_hButtonAddCustomGame = NULL;
// --- ADD THIS ---
HWND g_hButtonDeleteCustomGame = NULL;
// --- END ADD ---
HWND g_hEditExecutablePath = NULL;
HWND g_hButtonBrowseExe = NULL;
// --- END ADD ---
HWND g_hStaticGameCount = NULL;
std::vector<std::unique_ptr<SteamGame>> g_games;
std::mutex g_gamesMutex;

// Add D3D/D2D definitions
ID3D11Device*            g_pD3DDevice = nullptr;
ID3D11DeviceContext*     g_pImmediateContext = nullptr;
IDXGISwapChain1*         g_pSwapChain = nullptr; // Likely not needed for overlay only
ID3D11Texture2D*         g_pOverlayTexture = nullptr;
ID3D11RenderTargetView*  g_pOverlayRenderTargetView = nullptr;
IDXGISurface*            g_pDXGISurface = nullptr;
ID2D1Factory1*           g_pD2DFactory = nullptr;
ID2D1Device*             g_pD2DDevice = nullptr;
ID2D1DeviceContext*      g_pD2DContext = nullptr;
ID2D1Bitmap1*            g_pD2DTargetBitmap = nullptr;
ID2D1SolidColorBrush*    g_pWhiteBrush = nullptr;
IDWriteFactory*          g_pDWriteFactory = nullptr;
IDWriteTextFormat*       g_pTextFormatStatus = nullptr;
IDWriteTextFormat*       g_pTextFormatTime = nullptr;
IDWriteTextFormat*       g_pTextFormatSessionTime = nullptr;

// Video playback globals
IMFSourceReader* g_pSourceReader = nullptr;
ID2D1Bitmap*     g_pVideoFrameBitmap = nullptr;
std::wstring     g_videoPath;
bool             g_videoInitialized = false;
float            g_videoFrameRate = 30.0f; // Frame rate of the video file (FPS), default 30

// Video thread globals
std::thread g_videoThread;
std::mutex g_videoFrameMutex;
std::condition_variable g_videoFrameCV;
bool g_stopVideoThread = false;

// --- Game Launcher Overlay State definitions ---
int g_selectedGameIndex = -1;             // Initialize to -1 (no selection)
std::string g_launchingAppId = ""; // Initialize to empty
ID2D1Bitmap* g_pSelectedGameHeader = nullptr;
bool g_isLoadingGameData = false;
std::mutex g_overlayStateMutex;           // Mutexes are default constructed
IDWriteTextFormat* g_pTextFormatGameList = nullptr;
IDWriteTextFormat* g_pTextFormatGameDesc = nullptr;
IDWriteTextFormat* g_pTextFormatQuitButton = nullptr;
D2D1_RECT_F g_rectGameList = {};
D2D1_RECT_F g_rectHeaderImage = {};
D2D1_RECT_F g_rectDescription = {};
D2D1_RECT_F g_rectStartButton = {};
D2D1_RECT_F g_rectQuitButton = {};
ID2D1SolidColorBrush* pBlackBrush = nullptr;
ID2D1SolidColorBrush* g_pRedBrush = nullptr;
int g_gameListScrollOffset = 0;         // Initialize scroll offset
D2D1_RECT_F g_rectScrollUpButton = {};
D2D1_RECT_F g_rectScrollDownButton = {};
ID2D1Bitmap* g_pUpArrowBitmap = nullptr;   // Bitmap for the up arrow image
ID2D1Bitmap* g_pDownArrowBitmap = nullptr; // Bitmap for the down arrow image
int g_lastVisibleGameCount = 0;

// Overlay state variables for hold-to-scroll functionality
bool g_isGameListUpButtonHeld = false;
bool g_isGameListDownButtonHeld = false;
DWORD g_timeGameListButtonHeld = 0;
DWORD g_timeOfLastAutoScroll = 0;

ID2D1SolidColorBrush* g_pArrowBrush = nullptr; // Will be created in InitializeOverlayDirectX
// Add definitions for description scrolling variables
IDWriteTextLayout* g_pDescTextLayout = nullptr;
D2D1_RECT_F g_rectDescScrollUpButton = {};
D2D1_RECT_F g_rectDescScrollDownButton = {};
int g_descScrollOffsetPx = 0;
std::string g_currentLayoutAppId = "";
bool g_initialGameDataLoaded = false;

// WIC Factory definition
IWICImagingFactory* g_pWICFactory = nullptr;

// Helper function definition (UTF-8 std::string to std::wstring)
std::wstring StringToWString(const std::string& str)
{
    if (str.empty()) return std::wstring();
    int size_needed = MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), NULL, 0);
    std::wstring wstrTo(size_needed, 0);
    MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), &wstrTo[0], size_needed);
    return wstrTo;
}

// Shared utility functions moved here so they can be used in multiple files
std::string WStringToString(const std::wstring& wstr)
{
    if (wstr.empty()) return std::string();
    int size_needed = WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), NULL, 0, NULL, NULL);
    std::string strTo(size_needed, 0);
    WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), &strTo[0], size_needed, NULL, NULL);
    return strTo;
}

bool IsSteamVRRunning()
{
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap == INVALID_HANDLE_VALUE)
    {
        Log("CreateToolhelp32Snapshot failed in IsSteamVRRunning.");
        return false;
    }

    PROCESSENTRY32 pe;
    pe.dwSize = sizeof(pe);

    bool found = false;
    if (Process32First(hSnap, &pe))
    {
        do
        {
#ifdef UNICODE
            std::wstring wProcessName = pe.szExeFile;
            std::string processName = WStringToString(wProcessName);
#else
            std::string processName = pe.szExeFile;
#endif
            if (_stricmp(processName.c_str(), "vrserver.exe") == 0)
            {
                found = true;
                break;
            }
        } while (Process32Next(hSnap, &pe));
    }

    CloseHandle(hSnap);
    Log(std::string("IsSteamVRRunning -> ") + (found ? "Yes" : "No"));
    return found;
}

uint32_t GetCurrentVRAppProcessId()
{
    vr::IVRApplications* pApps = vr::VRApplications();
    if (!pApps)
    {
        Log("IVRApplications not available.");
        return 0;
    }
    uint32_t pid = pApps->GetCurrentSceneProcessId();
    Log("Current VR App PID -> " + std::to_string(pid));
    return pid;
}

std::string GetProcessNameFromPID(uint32_t pid)
{
    std::string processName;
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap == INVALID_HANDLE_VALUE)
    {
        Log("CreateToolhelp32Snapshot failed (GetProcessNameFromPID).");
        return processName;
    }

    PROCESSENTRY32 pe;
    pe.dwSize = sizeof(pe);

    if (Process32First(hSnap, &pe))
    {
        do
        {
#ifdef UNICODE
            std::wstring wProcessName = pe.szExeFile;
            processName = WStringToString(wProcessName);
#else
            processName = pe.szExeFile;
#endif
            if (pe.th32ProcessID == pid)
            {
                // Found
                break;
            }
        } while (Process32Next(hSnap, &pe));
    }

    CloseHandle(hSnap);
    Log("GetProcessNameFromPID(" + std::to_string(pid) + ") -> " + processName);
    return processName;
}

bool KillProcessByPID(uint32_t pid)
{
    HANDLE hProcess = OpenProcess(PROCESS_TERMINATE, FALSE, pid);
    if (hProcess == NULL)
    {
        Log("OpenProcess failed in KillProcessByPID(" + std::to_string(pid) + ").");
        return false;
    }

    BOOL result = TerminateProcess(hProcess, 0);
    CloseHandle(hProcess);

    Log("TerminateProcess(" + std::to_string(pid) + ") -> " + (result ? "Success" : "Fail"));
    return (result != 0);
}

void QuitVRApp()
{
    if (!vr::VRCompositor()) // Minimal check to see if VR is initialized
    {
        Log("QuitVRApp -> OpenVR not initialized, skipping.");
        return;
    }

    uint32_t scenePID = GetCurrentVRAppProcessId();
    if (scenePID == 0)
    {
        Log("No VR app is running. Nothing to quit.");
        return;
    }

    std::string processName = GetProcessNameFromPID(scenePID);
    bool result = KillProcessByPID(scenePID);

    std::string logMsg = "[QuitVRApp] Terminating VR app with PID: " + std::to_string(scenePID);
    if (!processName.empty())
        logMsg += " (ProcessName=" + processName + ")";
    if (!result)
        logMsg += " -> FAILED to terminate!";
    else
        logMsg += " -> Terminated successfully.";
    Log(logMsg);
}

// Thread-safe wrapper for IsSteamVRRunning (asynchronous check)
bool IsSteamVRRunningAsync()
{
    std::thread checkThread([]() {
        // Lock the mutex before accessing shared resources or performing the check
        // Although IsSteamVRRunning might be thread-safe itself, 
        // locking ensures atomicity if we were to interact with shared state based on the result.
        std::lock_guard<std::mutex> lock(g_steamVRCheckMutex); 
        
        bool result = IsSteamVRRunning();
        
        // Log the result from the background thread
        if (result) {
            Log("Async Check: SteamVR is running.");
        } else {
            Log("Async Check: SteamVR is NOT running.");
        }
        
        // Note: The result is not directly returned to the caller of IsSteamVRRunningAsync.
        // If the result is needed, a different mechanism like PostMessage or futures would be required.
    });
    
    // Detach the thread to let it run independently.
    checkThread.detach();
    
    // Immediately return true as per the example. 
    // The caller should not rely on this return value for the actual status.
    return true; 
}

std::vector<uint8_t> g_rawVideoFrameBuffer;
UINT g_rawVideoFrameWidth = 0;
UINT g_rawVideoFrameHeight = 0;
bool g_rawVideoFrameReady = false;