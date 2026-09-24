#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <windows.h>
#include <shlwapi.h>
#include "steam_games.h"
#include "logging.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include "steam_api.h" // Make sure this includes WStringToString if used here, or include globals.h
#include "globals.h" // Include for WStringToString definition
#include "config.h"
#include "window_proc.h" // Include for UpdateGameMembershipList and UpdateCategoryControlsState
#include <shellapi.h>
#include <iostream>
#include <memory>
#include <set> // For std::set to track duplicate App IDs
#include <openvr.h>  // For vr::VROverlay()
#include "overlay.h" // For g_MainOverlay
#include "json.hpp" // For nlohmann::json
#include <ctime>    // For time() to generate unique IDs

// For convenience
using json = nlohmann::json;

// Get Steam installation path from registry
std::string GetSteamInstallPath()
{
    Log("GetSteamInstallPath: checking registry...");
    HKEY hKey;
    if (RegOpenKeyExA(HKEY_CURRENT_USER, "Software\\Valve\\Steam", 0, KEY_READ, &hKey) == ERROR_SUCCESS)
    {
        char buf[1024];
        DWORD dwSize = sizeof(buf);
        if (RegQueryValueExA(hKey, "SteamPath", NULL, NULL, (LPBYTE)buf, &dwSize) == ERROR_SUCCESS)
        {
            RegCloseKey(hKey);
            return std::string(buf);
        }
        RegCloseKey(hKey);
    }
    return {};
}

// Get all Steam library folders
std::vector<std::string> GetAllSteamLibraryFolders(const std::string& steamInstallPath)
{
    std::vector<std::string> libs;

    // Add default library
    std::string defaultLib = steamInstallPath + "\\steamapps";
    DWORD attr = GetFileAttributesA(defaultLib.c_str());
    if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY))
    {
        libs.push_back(defaultLib);
    }

    // Parse libraryfolders.vdf to find additional libraries
    std::string vdfPath = defaultLib + "\\libraryfolders.vdf";
    std::ifstream ifs(vdfPath);
    if (ifs.is_open())
    {
        std::string line;
        while (std::getline(ifs, line))
        {
            auto trim = [](std::string& s) {
                s.erase(0, s.find_first_not_of(" \t\r\n"));
                s.erase(s.find_last_not_of(" \t\r\n") + 1);
                };
            trim(line);

            if (!line.empty() && line[0] == '\"')
            {
                // e.g. "1"  "D:\\SteamLibrary"
                size_t secondQuote = line.find('\"', 1);
                if (secondQuote != std::string::npos)
                {
                    size_t pathStart = line.find('\"', secondQuote + 1);
                    size_t pathEnd = line.find_last_of('\"');
                    if (pathStart != std::string::npos && pathEnd != std::string::npos && pathEnd > pathStart)
                    {
                        std::string path = line.substr(pathStart + 1, pathEnd - (pathStart + 1));

                        // Fix double slashes
                        size_t pos = 0;
                        while ((pos = path.find("\\\\", pos)) != std::string::npos)
                        {
                            path.replace(pos, 2, "\\");
                            pos++;
                        }
                        std::string steamApps = path + "\\steamapps";
                        DWORD attr2 = GetFileAttributesA(steamApps.c_str());
                        if (attr2 != INVALID_FILE_ATTRIBUTES && (attr2 & FILE_ATTRIBUTE_DIRECTORY))
                        {
                            libs.push_back(steamApps);
                        }
                    }
                }
            }
        }
        ifs.close();
    }

    return libs;
}

// Parse a Steam game manifest file and populate output parameters
// Returns true if essential information (name, appid) was found, false otherwise
bool ParseAcfFile(const std::string& acfPath, const std::string& libraryPath,
    std::string& outName, std::string& outAppid, std::string& outInstallDir)
{
    // Clear output parameters at the start
    outName.clear();
    outAppid.clear();
    outInstallDir.clear();
    bool foundName = false;
    bool foundAppid = false;

    std::ifstream ifs(acfPath);
    if (!ifs.is_open())
    {
        Log("ParseAcfFile: cannot open " + acfPath);
        return false; // Return false on failure
    }

    std::string line;
    while (std::getline(ifs, line))
    {
        auto trim = [](std::string& s) {
            s.erase(0, s.find_first_not_of(" \t\r\n"));
            s.erase(s.find_last_not_of(" \t\r\n") + 1);
            };
        trim(line);

        if (!line.empty() && line[0] == '\"')
        {
            size_t keyEnd = line.find('\"', 1);
            if (keyEnd != std::string::npos)
            {
                std::string key = line.substr(1, keyEnd - 1);

                size_t valStart = line.find('\"', keyEnd + 1);
                size_t valEnd = line.find('\"', valStart + 1);
                if (valStart != std::string::npos && valEnd != std::string::npos && valEnd > valStart)
                {
                    std::string val = line.substr(valStart + 1, valEnd - (valStart + 1));
                    // Populate output parameters directly
                    if (key == "name") {
                        outName = val;
                        foundName = true;
                    }
                    else if (key == "appid") {
                        outAppid = val;
                        foundAppid = true;
                    }
                    else if (key == "installdir") {
                        outInstallDir = val;
                    }
                }
            }
        }
    }
    ifs.close(); // Ensure file is closed
    // Return true only if the essential parts (name, appid) were found
    return foundName && foundAppid;
}

// --- Helper: Find game by appId ---
SteamGame* findGameByAppId(const std::string& appIdToFind) { // Renamed parameter to avoid confusion
    if (appIdToFind.empty()) {
        Log("findGameByAppId: Called with empty appIdToFind.");
        return nullptr;
    }
    // Reduced logging - only log entry and result to eliminate spam
    Log("findGameByAppId: Searching for AppID: " + appIdToFind);

    std::lock_guard<std::mutex> lock(g_gamesMutex);

    for (size_t i = 0; i < g_games.size(); ++i) {
        const auto& game_ptr = g_games[i]; // Get the unique_ptr

        if (!game_ptr) {
            continue; // Skip null unique_ptrs (no logging for performance)
        }

        // Direct comparison without excessive logging
        if (game_ptr->appid == appIdToFind) {
            Log("findGameByAppId: Match found for AppID: " + appIdToFind + " at index " + std::to_string(i));
            return game_ptr.get();
        }
    }
    Log("findGameByAppId: No match found for AppID: " + appIdToFind);
    return nullptr;
}

// --- ADD THIS FUNCTION ---
void SaveCustomGames() {
    std::lock_guard<std::mutex> lock(g_gamesMutex);
    Log("SaveCustomGames: Saving custom games to custom_games.json");

    json j = json::array();
    for (const auto& game : g_games) {
        if (game && game->isCustom) {
            json gameObj;
            gameObj["appid"] = game->appid;
            gameObj["name"] = game->name;
            gameObj["executablePath"] = game->executablePath;
            gameObj["description"] = game->description;
            gameObj["headerImage"] = game->headerImage;
            gameObj["isCustom"] = true;
            j.push_back(gameObj);
        }
    }

    std::string customGamesPath = g_iniFullPath.substr(0, g_iniFullPath.find_last_of("\\/")) + "\\custom_games.json";
    std::ofstream ofs(customGamesPath);
    if (ofs.is_open()) {
        ofs << j.dump(4); // Pretty print
        ofs.close();
        Log("SaveCustomGames: Successfully saved " + std::to_string(j.size()) + " custom games.");
    } else {
        Log("SaveCustomGames: ERROR - Failed to open " + customGamesPath + " for writing.");
    }
}

// --- ADD THIS FUNCTION ---
void LoadCustomGames() {
    std::string customGamesPath = g_iniFullPath.substr(0, g_iniFullPath.find_last_of("\\/")) + "\\custom_games.json";
    std::ifstream ifs(customGamesPath);

    if (!ifs.is_open()) {
        Log("LoadCustomGames: custom_games.json not found. This is normal on first run.");
        return;
    }

    try {
        json j;
        ifs >> j;
        if (j.is_array()) {
            std::lock_guard<std::mutex> lock(g_gamesMutex);
            int count = 0;
            for (const auto& item : j) {
                auto customGame = std::make_unique<SteamGame>();
                customGame->appid = item.value("appid", "");
                customGame->name = item.value("name", "Unnamed Custom Game");
                customGame->executablePath = item.value("executablePath", "");
                customGame->description = item.value("description", "");
                customGame->headerImage = item.value("headerImage", "");
                customGame->isCustom = true;
                customGame->storeDataFetched = true; // Custom games don't fetch from Steam API

                // Ensure it has a valid appid and executable path
                if (!customGame->appid.empty() && !customGame->executablePath.empty()) {
                    g_games.push_back(std::move(customGame));
                    count++;
                } else {
                    Log("LoadCustomGames: Skipping invalid custom game entry from JSON.");
                }
            }
            Log("LoadCustomGames: Loaded " + std::to_string(count) + " custom games from custom_games.json.");
        }
    } catch (const json::parse_error& e) {
        Log("LoadCustomGames: ERROR parsing custom_games.json: " + std::string(e.what()));
    }
}

void PopulateGamesListBox() {
    if (!g_hListGames) return;
    HWND hWnd = GetParent(g_hListGames); // Get the main window handle to update dependent controls

    // Reset the UI state for the game list and details panel
    SendMessageW(g_hListGames, LB_RESETCONTENT, 0, 0);
    EnableWindow(g_hButtonLaunchGame, FALSE);
    EnableWindow(g_hEditGameDesc, FALSE);
    EnableWindow(g_hEditImagePath, FALSE);
    EnableWindow(g_hButtonBrowseImage, FALSE);
    EnableWindow(g_hButtonSaveCache, FALSE);
    SetWindowTextW(g_hEditGameDesc, L"");
    SetWindowTextW(g_hEditImagePath, L"");

    // Clear dependent lists and update control states
    if(hWnd) {
        UpdateGameMembershipList(hWnd, -1);
        UpdateCategoryControlsState(hWnd);
    }

    std::lock_guard<std::mutex> games_lock(g_gamesMutex);
    
    // Create a temporary set of all appids that are in at least one category for efficient lookup
    std::vector<std::string> categorized_app_ids;
    bool filter_is_active = g_filterGamesInCategories; // Read the global filter flag

    if (filter_is_active) {
        std::lock_guard<std::mutex> cat_lock(g_categoriesMutex);
        for (const auto& category : g_categories) {
            if (category) {
                // Add all gameAppIds from this category to our temporary vector
                categorized_app_ids.insert(categorized_app_ids.end(), category->gameAppIds.begin(), category->gameAppIds.end());
            }
        }
        // Sort and remove duplicates to create a unique, sorted list for fast searching
        std::sort(categorized_app_ids.begin(), categorized_app_ids.end());
        categorized_app_ids.erase(std::unique(categorized_app_ids.begin(), categorized_app_ids.end()), categorized_app_ids.end());
    }

    // Populate the list box from the g_games vector, applying the filter if active
    int displayedCount = 0;
    for (size_t i = 0; i < g_games.size(); ++i) {
        if (g_games[i]) {
            bool should_add = !filter_is_active; // If filter is not active, we should add the game

            if (filter_is_active) {
                // If filter is active, check if the game's appid is in our set of categorized games
                if (std::binary_search(categorized_app_ids.begin(), categorized_app_ids.end(), g_games[i]->appid)) {
                    should_add = true;
                }
            }

            if (should_add) {
                int lb_idx = (int)SendMessageA(g_hListGames, LB_ADDSTRING, 0, (LPARAM)g_games[i]->name.c_str());
                // IMPORTANT: The item data is the index into the original g_games vector, which preserves all other logic.
                SendMessageA(g_hListGames, LB_SETITEMDATA, lb_idx, (LPARAM)i);
                displayedCount++;
            }
        }
    }
    
    // Update the count display
    if (g_hStaticGameCount) {
        std::string countText;
        if (filter_is_active) {
            countText = "Games: " + std::to_string(displayedCount) + " / " + std::to_string(g_games.size()) + " (filtered)";
        } else {
            countText = "Games: " + std::to_string(displayedCount) + " (unique games found)";
        }
        SetWindowTextW(g_hStaticGameCount, StringToWString(countText).c_str());
    }
    
    Log("PopulateGamesListBox: Finished populating list. Filter was " + std::string(filter_is_active ? "ON" : "OFF") + ". Displayed " + std::to_string(displayedCount) + " games.");
}

// Launch a Steam game
void LaunchGame(const SteamGame& game)
{
    g_launchingAppId = game.appid;

    // UNIVERSAL CHECK: Before launching ANY game, check if a DIFFERENT custom game is running.
    if (g_customGamePID != 0 && !g_runningCustomGameAppId.empty() && game.appid != g_runningCustomGameAppId) {
        Log("LaunchGame: A different custom game (AppID: " + g_runningCustomGameAppId + ", PID: " + std::to_string(g_customGamePID) + ") is running. Terminating it before launching new game '" + game.name + "'.");
        KillProcessByPID(g_customGamePID);
        g_customGamePID = 0;
        g_runningCustomGameAppId = "";
    }

    // --- UNIFIED DASHBOARD HIDING LOGIC ---
    g_bOverlayShutdownRequested = true;
    Log("LaunchGame: Setting overlay shutdown request flag.");
    if (vr::VROverlay()) {
        Log("LaunchGame: Hiding overlay before game launch.");
        if (g_MainOverlay != 0) {
            vr::VROverlay()->HideOverlay(g_MainOverlay);
        }
        if (g_ThumbnailOverlay != 0) {
            vr::VROverlay()->HideOverlay(g_ThumbnailOverlay);
        }
    }
    // --- END UNIFIED LOGIC ---

    // --- GAME-SPECIFIC LAUNCH LOGIC ---
    if (game.isCustom)
    {
        // Check if this specific custom game is already running.
        if (g_customGamePID != 0 && game.appid == g_runningCustomGameAppId) {
            HANDLE hProcess = OpenProcess(SYNCHRONIZE, FALSE, g_customGamePID);
            if (hProcess != NULL) {
                // Process is still running.
                CloseHandle(hProcess);
                Log("LaunchGame: This custom game is already running (PID: " + std::to_string(g_customGamePID) + "). Launch silently blocked.");
                g_bOverlayShutdownRequested = false; // Reset flag as we are not proceeding
                return;
            }
            // Process is not running, so the PID is stale.
            Log("LaunchGame: Stale PID " + std::to_string(g_customGamePID) + " found for this AppID. Clearing it.");
            g_customGamePID = 0;
            g_runningCustomGameAppId = "";
        }

        if (game.executablePath.empty()) {
            Log("LaunchGame Error: Custom game '" + game.name + "' has no executable path set.");
            MessageBoxA(NULL, "This custom game does not have an executable path set.", "Launch Error", MB_OK | MB_ICONERROR);
            g_bOverlayShutdownRequested = false; // Reset flag on error
            return;
        }

        // --- NEW MANIFEST LAUNCH METHOD ---
        vr::IVRApplications *pVRApplications = vr::VRApplications();
        if (!pVRApplications)
        {
            Log("LaunchGame Error: Could not get IVRApplications interface. Cannot launch custom game through SteamVR.");
            MessageBoxA(NULL, "Could not get IVRApplications interface. Custom game launch may not hide dashboard.", "Launch Warning", MB_OK | MB_ICONWARNING);
            g_bOverlayShutdownRequested = false; // Reset flag on error
            return;
        }

        char tempPath[MAX_PATH];
        GetTempPathA(MAX_PATH, tempPath);
        std::string manifestPath = std::string(tempPath) + game.appid + ".vrmanifest";
        Log("LaunchGame: Creating temporary manifest at: " + manifestPath);

        // Prepare paths for the manifest, correctly escaping backslashes
        std::string binaryPath = game.executablePath;
        std::string workingDir = game.executablePath;
        
        // Escape backslashes for the manifest format
        for (size_t i = 0; (i = binaryPath.find('\\', i)) != std::string::npos; ++i) {
            binaryPath.replace(i, 1, "\\\\");
            i++; 
        }
        
        auto const pos = workingDir.find_last_of("\\/");
        if (std::string::npos != pos)
        {
            workingDir = workingDir.substr(0, pos);
        }
        for (size_t i = 0; (i = workingDir.find('\\', i)) != std::string::npos; ++i) {
            workingDir.replace(i, 1, "\\\\");
            i++;
        }

        // Manually build the manifest string in the correct VDF-like format
        std::stringstream ss;
        ss << "{\n";
        ss << "    \"applications\": [\n";
        ss << "        {\n";
        ss << "            \"app_key\": \"" << game.appid << "\",\n";
        ss << "            \"launch_type\": \"binary\",\n";
        ss << "            \"binary_path_windows\": \"" << binaryPath << "\",\n";
        ss << "            \"working_dir_windows\": \"" << workingDir << "\",\n";
        ss << "            \"is_temp\": true,\n";
        ss << "            \"strings\": {\n";
        ss << "                \"en_us\": {\n";
        ss << "                    \"name\": \"VR Lawrence Custom Game\"\n";
        ss << "                }\n";
        ss << "            }\n";
        ss << "        }\n";
        ss << "    ]\n";
        ss << "}\n";

        std::ofstream ofs(manifestPath);
        if (!ofs.is_open())
        {
            Log("LaunchGame Error: Could not write temporary manifest. Cannot launch custom game through SteamVR.");
            MessageBoxA(NULL, "Could not write temporary manifest. Custom game launch may not hide dashboard.", "Launch Warning", MB_OK | MB_ICONWARNING);
            g_bOverlayShutdownRequested = false; // Reset flag on error
            return;
        }
        ofs << ss.str();
        ofs.close();

        vr::EVRApplicationError appError = pVRApplications->AddApplicationManifest(manifestPath.c_str(), true);
        if (appError != vr::VRApplicationError_None)
        {
            Log("LaunchGame Error: AddApplicationManifest failed: " + std::string(pVRApplications->GetApplicationsErrorNameFromEnum(appError)));
            remove(manifestPath.c_str());
            MessageBoxA(NULL, "Failed to register temporary game manifest with SteamVR.", "Launch Error", MB_OK | MB_ICONERROR);
            g_bOverlayShutdownRequested = false; // Reset flag on error
            return;
        }

        Log("LaunchGame: Launching custom game via manifest: " + game.appid);
        appError = pVRApplications->LaunchApplication(game.appid.c_str());

        // Clean up manifest file immediately, regardless of launch success
        pVRApplications->RemoveApplicationManifest(manifestPath.c_str());
        remove(manifestPath.c_str());
        Log("LaunchGame: Cleaned up temporary manifest.");

        if (appError != vr::VRApplicationError_None)
        {
            Log("LaunchGame Error: LaunchApplication failed: " + std::string(pVRApplications->GetApplicationsErrorNameFromEnum(appError)));
            MessageBoxA(NULL, "SteamVR failed to launch the custom game.", "Launch Error", MB_OK | MB_ICONERROR);
            g_bOverlayShutdownRequested = false; // Reset flag on error
            return; // Skip post-launch fallback setup
        }
        else
        {
            // --- PID acquisition logic ---
            uint32_t newPid = 0;
            // It can take a moment for the process ID to become available after launch.
            for (int i = 0; i < 10; ++i) 
            {
                newPid = pVRApplications->GetApplicationProcessId(game.appid.c_str());
                if (newPid != 0) {
                    break;
                }
                Sleep(100); // Wait and retry
            }

            if (newPid != 0) {
                g_customGamePID = newPid;
                g_runningCustomGameAppId = game.appid;
                Log("LaunchGame: Custom game '" + game.name + "' launched. Acquired PID: " + std::to_string(g_customGamePID));
            } else {
                Log("LaunchGame WARNING: Launched custom game but could not retrieve its PID. Multi-launch protection for this instance may not work.");
            }
        }
    }
    // --- Logic for Steam games ---
    else
    {
        if (game.appid.empty())
        {
            Log("LaunchGame Error: Steam Game AppID is empty.");
            g_bOverlayShutdownRequested = false; // Reset flag on error
            return;
        }

        vr::IVRApplications* pVRApplications = vr::VRApplications();
        if (!pVRApplications)
        {
            Log("LaunchGame Error: Could not get IVRApplications interface. Cannot launch Steam game through SteamVR.");
            MessageBoxA(NULL, "Could not get IVRApplications interface. Game launch may not work correctly.", "Launch Warning", MB_OK | MB_ICONWARNING);
            g_bOverlayShutdownRequested = false; // Reset flag on error
            return;
        }

        std::string appKey = "steam.app." + game.appid;
        Log("LaunchGame: Launching Steam game via IVRApplications with key: " + appKey);
        vr::EVRApplicationError appError = pVRApplications->LaunchApplication(appKey.c_str());

        if (appError != vr::VRApplicationError_None)
        {
            std::string errorMsg = "SteamVR failed to launch the game: " + std::string(pVRApplications->GetApplicationsErrorNameFromEnum(appError));
            Log("LaunchGame Error: LaunchApplication failed for key " + appKey + ". Error: " + errorMsg);
            MessageBoxA(NULL, errorMsg.c_str(), "Launch Error", MB_OK | MB_ICONERROR);
            g_bOverlayShutdownRequested = false; // Reset flag on error
            return;
        }
    }
    
    // --- UNIFIED POST-LAUNCH FALLBACK SETUP ---
    g_awaitingDashboardHide = true;
    g_gameLaunchTime = GetTickCount();
    Log("LaunchGame: Set flag to monitor dashboard dismissal.");
}

void LoadSteamGames(HWND hWnd)
{
    Log("LoadSteamGames: Entered function.");
    {
        std::lock_guard<std::mutex> lock(g_gamesMutex); // Lock for thread safety

        // Clear the global games vector before populating
        g_games.clear();

        std::string steamPath = GetSteamInstallPath();
        if (steamPath.empty())
        {
            Log("LoadSteamGames: ERROR - Steam path not found in registry.");
            return;
        }
        Log("LoadSteamGames: Found Steam path: " + steamPath);

        auto libs = GetAllSteamLibraryFolders(steamPath);
        Log("LoadSteamGames: Number of library folders: " + std::to_string(libs.size()));

        // Step 1: Parse all ACF files and populate the g_games vector (with duplicate detection)
        std::set<std::string> seenAppIds; // Track App IDs to prevent duplicates
        int duplicatesSkipped = 0;
        
        for (const auto& lib : libs)
        {
            Log("LoadSteamGames: Scanning library: " + lib);
            std::string pattern = lib + "\\appmanifest_*.acf";
            WIN32_FIND_DATAA fd;
            HANDLE hFind = FindFirstFileA(pattern.c_str(), &fd);
            if (hFind != INVALID_HANDLE_VALUE)
            {
                do
                {
                    if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
                    {
                        std::string acfFull = lib + "\\" + fd.cFileName;
                        std::string gameName, gameAppid, gameInstallDir;
                        if (ParseAcfFile(acfFull, lib, gameName, gameAppid, gameInstallDir))
                        {
                            // Check if we've already seen this App ID
                            if (seenAppIds.find(gameAppid) == seenAppIds.end()) {
                                // New App ID, add it
                                seenAppIds.insert(gameAppid);
                                g_games.emplace_back(std::make_unique<SteamGame>(
                                    std::move(gameName),
                                    std::move(gameAppid),
                                    std::move(gameInstallDir),
                                    lib
                                ));
                            } else {
                                // Duplicate App ID, skip it
                                duplicatesSkipped++;
                                Log("LoadSteamGames: Skipping duplicate game with AppID: " + gameAppid + " (" + gameName + ")");
                            }
                        }
                    }
                } while (FindNextFileA(hFind, &fd));
                FindClose(hFind);
            }
        }
        
        if (duplicatesSkipped > 0) {
            Log("LoadSteamGames: Skipped " + std::to_string(duplicatesSkipped) + " duplicate games.");
        }
    } // The lock on g_gamesMutex is released here.

    // --- MOVE THE SORT TO HERE ---
    Log("LoadSteamGames: Loading custom games from file...");
    LoadCustomGames();
    
    // Step 2: Sort the combined g_games vector alphabetically (case-insensitive)
    {
        std::lock_guard<std::mutex> lock(g_gamesMutex);
        std::sort(g_games.begin(), g_games.end(), [](const std::unique_ptr<SteamGame>& a, const std::unique_ptr<SteamGame>& b) {
            if (!a) return false;
            if (!b) return true;
            std::string nameA_lower = a->name;
            std::string nameB_lower = b->name;
            std::transform(nameA_lower.begin(), nameA_lower.end(), nameA_lower.begin(), ::tolower);
            std::transform(nameB_lower.begin(), nameB_lower.end(), nameB_lower.begin(), ::tolower);
            return nameA_lower < nameB_lower;
        });
    }

    // Step 3: Populate the list box UI from the now-sorted vector. This function handles its own locking.
    PopulateGamesListBox();

    // Re-lock to log the final count
    {
        std::lock_guard<std::mutex> lock(g_gamesMutex);
        if (!g_games.empty())
        {
            Log("LoadSteamGames: Total found and sorted games: " + std::to_string(g_games.size()));
        }
        else
        {
            Log("LoadSteamGames: No Steam games found.");
        }
    }
    Log("LoadSteamGames: Function finished.");
}

// --- Fetch store data for a single game ---
void FetchStoreDataForGame(SteamGame& game)
{
    std::string cachedHeaderImageMarker = ""; // Store potential cache marker

    // --- Check Cache First ---
    if (LoadMetadataFromCache(game.appid, game)) {
        Log("FetchStoreDataForGame: Loaded metadata from cache for " + game.name);
        // Check if the cached image is a manual marker
        if (game.headerImage.rfind("cache://", 0) == 0) {
            cachedHeaderImageMarker = game.headerImage; // Remember the manual marker
            Log("FetchStoreDataForGame: Manual cache marker found: " + cachedHeaderImageMarker);
        }
        game.storeDataFetched = true; // Mark as fetched from cache
        return;
    } else {
        Log("FetchStoreDataForGame: No valid cache found for " + game.name + ", proceeding to fetch from API.");
    }
    // --- End Cache Check ---

    std::lock_guard<std::mutex> lock(game.dataMutex);
    if (game.storeDataFetched && cachedHeaderImageMarker.empty()) {
        // If fetched from cache AND it wasn't a manual image marker, maybe skip API call?
        // return;
    }

    if (game.appid.empty()) {
        Log("FetchStoreDataForGame: Skipping game '" + game.name + "' - no appid.");
        return;
    }

    Log("Attempting to fetch store data from API for game: " + game.name + " (appid=" + game.appid + ")");

    std::wstring path = L"/api/appdetails?appids=" + StringToWString(game.appid) + L"&l=english";
    std::string resp = HttpGetToString(L"store.steampowered.com", path, true);

    if (resp.empty())
    {
        Log("FetchStoreDataForGame: No response from store API for " + game.appid + ".");
        return;
    }

    Log("Received response for appid " + game.appid + ": " + resp.substr(0, 100) + "...");

    std::string originalDesc = game.description;
    std::string originalHeader = game.headerImage;

    if (ParseSteamApiResponse(resp, game)) {
        Log("Successfully parsed store data for " + game.name + ": description='" + game.description.substr(0, 50) + "...', headerImage='" + game.headerImage + "'");
        if (!cachedHeaderImageMarker.empty()) {
            Log("Restoring manual cache marker [" + cachedHeaderImageMarker + "] over API URL [" + game.headerImage + "]");
            game.headerImage = cachedHeaderImageMarker;
        }
        FixEscapedSlashes(game.description);
        FixEscapedSlashes(game.headerImage);
        if (!SaveMetadataToCache(game)) {
            Log("FetchStoreDataForGame: WARNING - Failed to save metadata to cache for appid " + game.appid);
        }
        game.storeDataFetched = true;
    }
    else {
        Log("Failed to parse store data from API for " + game.name);
        if (game.storeDataFetched) {
            game.description = originalDesc;
            game.headerImage = originalHeader;
            Log("Restored pre-API-parse cached values due to parsing failure.");
        }
        game.storeDataFetched = true;
    }
}