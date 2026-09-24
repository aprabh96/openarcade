#ifndef GLOBALS_H
#define GLOBALS_H

// <ai_context>
// Shared global variables and includes for the app
// </ai_context>

#pragma once

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <TlHelp32.h>
#include <shellapi.h>
#include <shlwapi.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <d2d1_1.h>
#include <dwrite.h>
#include <wincodec.h> // For WIC Imaging Factory

// Media Foundation
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>
#include <evr.h>  // Enhanced Video Renderer

#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "windowscodecs.lib")

#include <string>
#include <fstream>
#include <ctime>
#include <thread>
#include <mutex>
#include <vector>
#include <condition_variable>
#include <memory>

// Include openvr
#include "openvr.h"

// Forward declarations
LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
int APIENTRY WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow);

// Global variables (extern)
extern HINSTANCE g_hInst;
extern HWND g_hButtonQuit;
extern HWND g_hButtonLaunchVR;
extern HWND g_hButtonShowOverlay;
extern HWND g_hButtonHideOverlay;
extern HWND g_hButtonTestContinuous;
extern HWND g_hButtonStopTestContinuous;
extern HWND g_hComboSessionTime;
extern HWND g_hEditCustomMinutes;
extern HWND g_hButtonStartSession;
extern HWND g_hButtonAddTime;
extern HWND g_hButtonStopSession;
extern HWND g_hStaticTimeLeft;
extern HWND g_hEditLog;
extern HWND g_hChkAutoOverlay;
extern HWND g_hEditIP;
extern HWND g_hButtonConnect;
extern HWND g_hEditStationName;
// --- ADD THIS ---
extern HWND g_hEditGameName;
// --- END ADD ---

// --- ADD THIS ---
extern DWORD g_customGamePID; // Tracks the Process ID of a running custom game
// --- ADD THIS ---
extern std::string g_runningCustomGameAppId; // Tracks the AppID of the running custom game
// --- END ADD ---
// --- END ADD ---

extern vr::VROverlayHandle_t g_MainOverlay;
extern vr::VROverlayHandle_t g_ThumbnailOverlay;

extern std::ofstream logFile;
extern std::string g_logFilePath;  // Path to the log file
extern std::mutex g_netMutex;

// Flags
extern bool continuousOverlayRunning;
extern bool g_AutoEnableOverlay;
extern bool g_filterGamesInCategories;
extern const wchar_t* STEAMVR_PATH;
extern bool g_SessionRunning;
extern bool g_bOverlayShutdownRequested; // New flag for controlling overlay visibility
extern bool g_awaitingDashboardHide; // Flag to check if we're waiting for the dashboard to hide after a game launch
extern DWORD g_gameLaunchTime;       // Timestamp for when the game was launched

// Timer IDs
#define OVERLAY_TIMER_ID          1
#define OVERLAY_REFRESH_INTERVAL  33  // Change to ~30fps for video playback
#define SESSION_TIMER_ID          2
#define SESSION_TICK_INTERVAL     1000
#define STEAMVR_CHECK_TIMER_ID    3
#define STEAMVR_CHECK_INTERVAL    1000
#define STEAMVR_PERSISTENT_CHECK_TIMER_ID 5
#define STEAMVR_PERSISTENT_CHECK_INTERVAL 200

// Session
extern time_t g_sessionEndTime;
extern int    g_SessionTimeSeconds;

// Networking
extern SOCKET g_clientSocket;
extern bool   g_connected;
extern bool   g_runRecvThread;
extern std::thread g_recvThread;
extern std::string g_masterIP;
extern std::string g_stationName;
extern bool   g_wsaInitialized;

// INI-based
extern std::string g_iniFullPath;
static const char* INI_SECTION = "OverlaySettings";

// Forward declarations for Steam games functionality (from steam_games.h)
struct SteamGame;

// ===================== Game Category Management =====================
#include <vector>
#include <string>
#include <memory> // For std::unique_ptr
#include <algorithm> // For std::sort, std::transform

struct GameCategory {
    std::string name;
    std::vector<std::string> gameAppIds; // Stores appids of games in this category
    int id; // Unique ID for stable reference, could be simple index if not reordering

    GameCategory(std::string n, int i) : name(std::move(n)), id(i) {}

    // Comparator for sorting by name (case-insensitive)
    bool operator<(const GameCategory& other) const {
        std::string nameLower = name;
        std::string otherNameLower = other.name;
        std::transform(nameLower.begin(), nameLower.end(), nameLower.begin(), ::tolower);
        std::transform(otherNameLower.begin(), otherNameLower.end(), otherNameLower.begin(), ::tolower);
        // Handle numbers first then alphabetical
        bool thisIsNumeric = !nameLower.empty() && std::all_of(nameLower.begin(), nameLower.end(), ::isdigit);
        bool otherIsNumeric = !otherNameLower.empty() && std::all_of(otherNameLower.begin(), otherNameLower.end(), ::isdigit);

        if (thisIsNumeric && !otherIsNumeric) return true;
        if (!thisIsNumeric && otherIsNumeric) return false;
        // If both are numeric or both are non-numeric, standard comparison
        return nameLower < otherNameLower;
    }
};

// Category Management Globals
extern std::vector<std::unique_ptr<GameCategory>> g_categories;
extern std::mutex g_categoriesMutex;
extern int g_selectedCategoryIndexVR; // Index for the category selected in the VR overlay
extern std::vector<std::string> g_currentCategoryGameAppIds_VR; // AppIDs of games in the currently selected VR category
extern std::mutex g_VROverlayGameListMutex; // Mutex for g_currentCategoryGameAppIds_VR and related VR list state

// HWNDs for new UI elements for category management on Desktop
extern HWND g_hListCategories;
extern HWND g_hEditCategoryName; // For creating/renaming
extern HWND g_hButtonCreateCategory;
extern HWND g_hButtonDeleteCategory;
extern HWND g_hButtonRenameCategory; // Optional: if renaming is desired
extern HWND g_hButtonMoveCategoryUp;   // <-- ADD THIS
extern HWND g_hButtonMoveCategoryDown; // <-- ADD THIS
extern HWND g_hButtonExportHTML;       // <-- ADD THIS
extern HWND g_hButtonAddGameToSelectedCat;
extern HWND g_hButtonRemoveGameFromSelectedCat;
extern HWND g_hStaticGameCategoriesLabel; // Shows "Game is in categories:"
extern HWND g_hListGameMembership;      // Lists categories the selected game belongs to
extern HWND g_hStaticGamesInCategoryLabel; // NEW
extern HWND g_hListGamesInCategory;      // NEW
extern HWND g_hStaticCategoryGameCount;  // Counter for games in selected category
// Simulate Category VR navigation buttons
extern HWND g_hButtonSimulateCatUp;
extern HWND g_hButtonSimulateCatDown;
extern HWND g_hButtonSimulateCatSelect; // Optional
extern HWND g_hChkFilterGamesInCategories;

// Category Control IDs
#define ID_LIST_CATEGORIES          200
#define ID_EDIT_CATEGORY_NAME       201
#define ID_BUTTON_CREATE_CATEGORY   202
#define ID_BUTTON_DELETE_CATEGORY   203
#define ID_BUTTON_RENAME_CATEGORY   204 // Optional
#define ID_BUTTON_ADD_GAME_TO_CAT   205
#define ID_BUTTON_REMOVE_GAME_FROM_CAT 206
#define ID_LIST_GAME_MEMBERSHIP     207
#define ID_LIST_GAMES_IN_CATEGORY   208 // NEW
#define ID_BUTTON_MOVE_CAT_UP       209 // <-- ADD THIS
#define ID_BUTTON_MOVE_CAT_DOWN     210 // <-- ADD THIS
#define ID_BUTTON_EXPORT_HTML       212 // <-- ADD THIS
#define ID_CHK_FILTER_GAMES_IN_CATEGORIES 211
// Simulate Category VR navigation buttons
#define ID_SIMULATE_CAT_UP      31
#define ID_SIMULATE_CAT_DOWN    32
#define ID_SIMULATE_CAT_SELECT  33 // Optional: if you want to simulate category selection too
// No ID needed for g_hStaticGameCategoriesLabel if it's just a static label
// ===================== End Game Category Management =====================

extern HWND g_hListGames;
extern HWND g_hButtonLaunchGame;
// --- ADD THIS BLOCK ---
extern HWND g_hButtonAddCustomGame;
// --- ADD THIS ---
extern HWND g_hButtonDeleteCustomGame;
// --- END ADD ---
extern HWND g_hEditExecutablePath;
extern HWND g_hButtonBrowseExe;
// --- END ADD ---
extern HWND g_hStaticGameCount;
extern std::vector<std::unique_ptr<SteamGame>> g_games;
extern std::mutex g_gamesMutex;
void LoadSteamGames(HWND hWnd);
void LaunchGame(const SteamGame& game);

// Functions used across multiple files
std::string WStringToString(const std::wstring& wstr);
bool IsSteamVRRunning();
uint32_t GetCurrentVRAppProcessId();
std::string GetProcessNameFromPID(uint32_t pid);
bool KillProcessByPID(uint32_t pid);
void QuitVRApp();
void Log(const std::string& msg);

// Add this to globals.h
extern std::mutex g_steamVRCheckMutex;
extern bool g_steamVRWasRunning; // Tracks the previous state of SteamVR
extern DWORD g_steamVRDetectedTime; // Timestamp for when SteamVR running state was detected
extern bool g_initialDashboardToggleDone; // Tracks if the initial dashboard toggle has been done
extern DWORD g_steamVRStartTimeForDashboardToggle; // Timestamp for 5-sec dashboard toggle delay
bool IsSteamVRRunningAsync();

// Add D3D/D2D declarations
extern ID3D11Device*            g_pD3DDevice;
extern ID3D11DeviceContext*     g_pImmediateContext;
extern IDXGISwapChain1*         g_pSwapChain;
extern ID3D11Texture2D*         g_pOverlayTexture;
extern ID3D11RenderTargetView*  g_pOverlayRenderTargetView; // Optional, maybe render directly?
extern IDXGISurface*            g_pDXGISurface;
extern ID2D1Factory1*           g_pD2DFactory;
extern ID2D1Device*             g_pD2DDevice;
extern ID2D1DeviceContext*      g_pD2DContext;
extern ID2D1Bitmap1*            g_pD2DTargetBitmap;
extern ID2D1SolidColorBrush*    g_pWhiteBrush;
extern IDWriteFactory*          g_pDWriteFactory;
extern IDWriteTextFormat*       g_pTextFormatStatus;
extern IDWriteTextFormat*       g_pTextFormatTime;
extern IDWriteTextFormat*       g_pTextFormatSessionTime;

// Video playback globals
extern IMFSourceReader* g_pSourceReader;
extern ID2D1Bitmap*     g_pVideoFrameBitmap;
extern std::wstring     g_videoPath;
extern bool             g_videoInitialized;
extern float            g_videoFrameRate; // Frame rate of the video file (FPS)

// Add thread-related globals for video processing
extern std::thread g_videoThread;
extern std::mutex g_videoFrameMutex;
extern std::condition_variable g_videoFrameCV;
extern bool g_stopVideoThread;

// --- Game Launcher Overlay State ---
extern int g_selectedGameIndex;
extern ID2D1Bitmap* g_pSelectedGameHeader;
extern bool g_isLoadingGameData;
extern std::string g_launchingAppId; // Tracks the AppID of the game currently being launched
extern std::mutex g_overlayStateMutex;
extern IDWriteTextFormat* g_pTextFormatGameList;
extern IDWriteTextFormat* g_pTextFormatGameDesc;
extern IDWriteTextFormat* g_pTextFormatQuitButton;
extern D2D1_RECT_F g_rectGameList;
extern D2D1_RECT_F g_rectHeaderImage;
extern D2D1_RECT_F g_rectDescription;
extern D2D1_RECT_F g_rectStartButton;
extern D2D1_RECT_F g_rectQuitButton;
extern ID2D1SolidColorBrush* pBlackBrush;
extern ID2D1SolidColorBrush* g_pRedBrush;

// Overlay layout rectangles for VR category list
extern D2D1_RECT_F g_rectCategoryList;
extern D2D1_RECT_F g_rectCatScrollUpButton;
extern D2D1_RECT_F g_rectCatScrollDownButton;
extern IDWriteTextFormat* g_pTextFormatCategoryList; // For category names

// Overlay state variables
extern int g_categoryListScrollOffset; // Scroll offset for the VR category list
extern int g_gameListScrollOffset; // Scroll offset for game list
extern D2D1_RECT_F g_rectScrollUpButton;
extern D2D1_RECT_F g_rectScrollDownButton;
extern ID2D1Bitmap* g_pUpArrowBitmap;   // Bitmap for the up arrow image
extern ID2D1Bitmap* g_pDownArrowBitmap; // Bitmap for the down arrow image
extern int g_lastVisibleGameCount; // Number of items drawn in the last frame

// Overlay state variables for hold-to-scroll functionality
extern bool g_isGameListUpButtonHeld;
extern bool g_isGameListDownButtonHeld;
extern DWORD g_timeGameListButtonHeld;
extern DWORD g_timeOfLastAutoScroll;

extern ID2D1SolidColorBrush* g_pArrowBrush;
extern IDWriteTextLayout* g_pDescTextLayout;
extern D2D1_RECT_F g_rectDescScrollUpButton;
extern D2D1_RECT_F g_rectDescScrollDownButton;
extern int g_descScrollOffsetPx;
extern std::string g_currentLayoutAppId;
extern bool g_initialGameDataLoaded; // Flag to track if the first game's data has been fetched

// WIC Factory
extern IWICImagingFactory* g_pWICFactory;

// Helper function to convert UTF-8 string to wide string
extern std::wstring StringToWString(const std::string& str);

extern std::string g_cacheDirFullPath;

extern HWND g_hEditGameDesc;     // Edit box for description
extern HWND g_hEditImagePath;    // Edit box for image path (read-only)
extern HWND g_hButtonBrowseImage; // Button to browse for image
extern HWND g_hButtonSaveCache;  // Button to save manual cache data

extern std::vector<uint8_t> g_rawVideoFrameBuffer;
extern UINT g_rawVideoFrameWidth;
extern UINT g_rawVideoFrameHeight;
extern bool g_rawVideoFrameReady;

#define SAFE_RELEASE(p) { if(p) { (p)->Release(); (p)=nullptr; } }

#define WM_APP_ENSURE_OVERLAY_VISIBLE (WM_APP + 103)
#define WM_APP_HANDLE_RECREATE_TARGET (WM_APP + 104)
#define WM_APP_PAUSE_OVERLAY_RENDERING (WM_APP + 105)

#endif