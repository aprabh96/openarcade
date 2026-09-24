#ifndef STEAM_GAMES_H
#define STEAM_GAMES_H

#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <windows.h>
#include <string>
#include <vector>
#include <mutex>
#include <utility> // Required for std::move
#include <memory>

// Structure to represent a Steam game
struct SteamGame {
    std::string name;           // Game name
    std::string appid;          // Steam App ID or a unique ID for custom games
    std::string installDir;     // Installation directory
    std::string libraryPath;    // Steam library path

    // --- NEW ---
    bool isCustom = false;       // Flag to identify custom games
    std::string executablePath;  // Full path to the executable for custom games
    // --- END NEW ---

    // --- New fields for store data ---
    std::string description;    // Short description from store API
    std::string headerImage;    // URL for the header image
    bool        storeDataFetched = false; // Flag to track if API data has been loaded
    std::mutex  dataMutex;      // Mutex for store data access

    // Default constructor
    SteamGame() = default;

    // Constructor for creating/emplacing
    SteamGame(std::string p_name, std::string p_appid, std::string p_installDir, std::string p_libraryPath) :
        name(std::move(p_name)),             // Move incoming strings
        appid(std::move(p_appid)),
        installDir(std::move(p_installDir)),
        libraryPath(std::move(p_libraryPath)),
        description(""),                     // Default initialize others
        headerImage(""),
        storeDataFetched(false),
        isCustom(false),                     // <-- ADD THIS
        executablePath("")                   // <-- ADD THIS
        // dataMutex is default initialized automatically
    {}

    // Explicitly delete copy constructor and assignment
    SteamGame(const SteamGame&) = delete;
    SteamGame& operator=(const SteamGame&) = delete;

    // Explicitly default move constructor and assignment
    SteamGame(SteamGame&&) = default;
    SteamGame& operator=(SteamGame&&) = default;

    // Optional: Add screenshot/trailer info later if needed
    // std::vector<ScreenshotInfo> screenshots;
    // std::vector<TrailerInfo>    trailers;
};

// Function declarations
std::string GetSteamInstallPath();
std::vector<std::string> GetAllSteamLibraryFolders(const std::string& steamInstallPath);
// Returns true on success, populates output parameters
bool ParseAcfFile(const std::string& acfPath, const std::string& libraryPath,
    std::string& outName, std::string& outAppid, std::string& outInstallDir);
void LaunchGame(const SteamGame& game);
void LoadSteamGames(HWND hWnd); // This will be called at startup to load games
void PopulateGamesListBox();

// --- ADD THIS BLOCK ---
void LoadCustomGames();
void SaveCustomGames();
// --- END ADD ---

// UI elements
extern HWND g_hListGames;      // ListBox for games
extern HWND g_hButtonLaunchGame; // Button to launch selected game
extern std::vector<std::unique_ptr<SteamGame>> g_games; // List of Steam games (now unique_ptrs)

// Constant for control IDs
#define ID_LAUNCH_GAME      20
#define ID_GAMES_LIST       21
#define ID_SIMULATE_UP      22
#define ID_SIMULATE_DOWN    23
#define ID_SIMULATE_SELECT  24
#define ID_EDIT_GAME_DESC   25 // Edit box for description
#define ID_EDIT_IMAGE_PATH  26 // Edit box for image path (read-only)
#define ID_BUTTON_BROWSE    27 // Button to browse for image
#define ID_BUTTON_SAVE_CACHE 28 // Button to save manual cache data
#define ID_LABEL_DESC       29 // Static label for Description
#define ID_LABEL_IMAGE      30 // Static label for Header Image

// --- ADD THIS ---
#define ID_EDIT_GAME_NAME_DETAIL     38
// --- END ADD ---

// --- ADD THIS BLOCK ---
#define ID_BUTTON_ADD_CUSTOM_GAME    34
#define ID_LABEL_EXE_PATH            35
#define ID_EDIT_EXE_PATH             36
#define ID_BUTTON_BROWSE_EXE         37
#define ID_BUTTON_DELETE_CUSTOM_GAME 39
// --- END ADD ---

// --- New Function for fetching store data ---
// Fetches store data for a *single* game. Could be called on demand.
void FetchStoreDataForGame(SteamGame& game);

SteamGame* findGameByAppId(const std::string& appId);

#endif // STEAM_GAMES_H