#include "session.h"
#include "logging.h"
#include "overlay.h"
#include "network.h"
#include "config.h" // Added this include to fix SaveSessionEndTime
#include <thread>
#include <mutex>
#include "globals.h"
#include "steam_games.h"
#include "steam_api.h"
#include <winhttp.h> // For WinHttpCrackUrl
#include "window_proc.h" // Added for WM_APP_GAME_DATA_READY definition

// Helper macro for safe COM release (Copied from overlay.cpp)
#ifndef SAFE_RELEASE
#define SAFE_RELEASE(p) { if(p) { (p)->Release(); (p)=nullptr; } }
#endif

// <ai_context>
// Implementation for session logic
// </ai_context>

void UpdateTimeLeftDisplay()
{
    if (!g_hStaticTimeLeft) return;

    int secs = g_SessionTimeSeconds;
    if (secs < 0) secs = 0;
    int mins = secs / 60;
    int rema = secs % 60;

    wchar_t buf[64];
    swprintf_s(buf, L"Time Left: %d:%02d", mins, rema);
    SetWindowTextW(g_hStaticTimeLeft, buf);
}

void ResumeSessionIfNeeded(HWND hWnd)
{
    time_t now = time(nullptr);
    if (g_sessionEndTime > now)
    {
        g_SessionRunning = true;
        g_SessionTimeSeconds = (int)(g_sessionEndTime - now);
        SetTimer(hWnd, SESSION_TIMER_ID, SESSION_TICK_INTERVAL, NULL);
        Log("Resuming previous session with " + std::to_string(g_SessionTimeSeconds) + "s left.");
        // The persistent SteamVR check timer will handle showing the overlay
        // once SteamVR is confirmed running and the 3s delay has passed.
        UpdateTimeLeftDisplay();
    }
    else
    {
        g_SessionRunning = false;
        g_sessionEndTime = 0;
        g_SessionTimeSeconds = 0;
        SaveSessionEndTime(0); // Ensure saved state reflects no active session
        UpdateTimeLeftDisplay();
        Log("No active session to resume.");
    }
}

void StopSessionTimer(HWND hWnd)
{
    KillTimer(hWnd, SESSION_TIMER_ID);
    g_SessionRunning = false;
    g_sessionEndTime = 0;
    g_SessionTimeSeconds = 0;
    SaveSessionEndTime(0);

    // Reset game launcher state
    {
        std::lock_guard<std::mutex> lock(g_overlayStateMutex);
        g_selectedGameIndex = -1;
        g_launchingAppId = ""; // Reset launching state
        if (g_pSelectedGameHeader) {
            g_pSelectedGameHeader->Release();
            g_pSelectedGameHeader = nullptr;
        }
        g_isLoadingGameData = false;
    }
    Log("Reset overlay game launcher state after session stop.");

    UpdateTimeLeftDisplay();
    Log("Session stopped (timer).");
}

void StartSessionTimer(HWND hWnd, int totalSeconds)
{
    if (totalSeconds <= 0)
    {
        Log("StartSessionTimer -> invalid totalSeconds.");
        return;
    }
    g_SessionRunning = true;

    // Reset game launcher state
    {
        std::lock_guard<std::mutex> lock(g_overlayStateMutex);
        g_selectedGameIndex = -1;
        g_launchingAppId = ""; // Reset launching state
        if (g_pSelectedGameHeader) {
            g_pSelectedGameHeader->Release();
            g_pSelectedGameHeader = nullptr;
        }
        g_isLoadingGameData = false;
    }
    Log("Reset overlay game launcher state for new session.");

    // --- Reset to the first category on new session start ---
    Log("StartSessionTimer: Resetting VR overlay to the first category.");
    {
        std::lock_guard<std::mutex> lock(g_categoriesMutex);
        if (!g_categories.empty()) {
            g_selectedCategoryIndexVR = 0;
        }
        else {
            g_selectedCategoryIndexVR = -1;
        }
    }
    UpdateCurrentCategoryGameList_VR(); // This updates the VR game list based on the new category and resets the game selection state.
    Log("StartSessionTimer: VR game list updated to reflect the first category.");
    // --- End of new logic ---

    // New auto-selection logic for VR overlay:
    std::string appIdForInitialVRLoad = "";
    HWND hwndMainForSession = FindWindowW(L"QuitVRAppClass", NULL); // Get main window handle for PostMessage

    { // Scope for mutexes
        std::lock_guard<std::mutex> vrListLock(g_VROverlayGameListMutex);
        std::lock_guard<std::mutex> overlayLock(g_overlayStateMutex); 

        if (!g_currentCategoryGameAppIds_VR.empty()) {
            g_selectedGameIndex = 0; // Select first in current VR list
            appIdForInitialVRLoad = g_currentCategoryGameAppIds_VR[0];
            
            g_isLoadingGameData = true;
            SAFE_RELEASE(g_pSelectedGameHeader);
            SAFE_RELEASE(g_pDescTextLayout);
            g_descScrollOffsetPx = 0;
            g_currentLayoutAppId = "";
            Log("StartSessionTimer: Auto-selecting first game for VR overlay: AppID " + appIdForInitialVRLoad);
        } else {
            g_selectedGameIndex = -1; 
            g_isLoadingGameData = false;
            Log("StartSessionTimer: No games in current VR category to auto-select for overlay.");
        }
    } // Mutexes released

    if (!appIdForInitialVRLoad.empty() && hwndMainForSession) {
        std::thread([appId = appIdForInitialVRLoad, hwndMain = hwndMainForSession]() {
            Log("StartSessionTimer (Thread): Starting data fetch for AppID " + appId);
            GameDataResult* result = new GameDataResult();
            result->appId = appId;
            result->success = false;

            SteamGame* gamePtr = findGameByAppId(appId);
            if (gamePtr) {
                FetchStoreDataForGame(*gamePtr); 

                std::lock_guard<std::mutex> gameDataLock(gamePtr->dataMutex);
                result->description = gamePtr->description;
                result->headerUrl = gamePtr->headerImage; 
                result->success = gamePtr->storeDataFetched;

                if (result->success && !result->headerUrl.empty()) {
                    FixEscapedSlashes(result->headerUrl);
                    std::string imageCachePathToUse;
                    bool isManualCacheMarker = (result->headerUrl.rfind("cache://", 0) == 0);

                    if (isManualCacheMarker) {
                        imageCachePathToUse = GetCachePathForImage(appId, result->headerUrl.substr(8));
                    } else {
                        imageCachePathToUse = GetCachePathForImage(appId, result->headerUrl);
                    }

                    if (LoadImageBytesFromCache(imageCachePathToUse, result->imageBytes)) {
                        Log("StartSessionTimer (Thread): Image bytes for " + gamePtr->name + " loaded from cache.");
                    } else if (!isManualCacheMarker) {
                        Log("StartSessionTimer (Thread): Image for " + gamePtr->name + " not in cache, attempting download from: " + result->headerUrl);
                        std::wstring wImageUrl = StringToWString(result->headerUrl);
                        URL_COMPONENTSW urlComp = {0}; urlComp.dwStructSize = sizeof(urlComp);
                        const DWORD buffSize = 1024;
                        wchar_t szScheme[buffSize], szHostName[buffSize], szUrlPath[buffSize], szExtraInfo[buffSize];
                        urlComp.lpszScheme = szScheme; urlComp.dwSchemeLength = buffSize;
                        urlComp.lpszHostName = szHostName; urlComp.dwHostNameLength = buffSize;
                        urlComp.lpszUrlPath = szUrlPath; urlComp.dwUrlPathLength = buffSize;
                        urlComp.lpszExtraInfo = szExtraInfo; urlComp.dwExtraInfoLength = buffSize;

                        if (WinHttpCrackUrl(wImageUrl.c_str(), (DWORD)wImageUrl.length(), 0, &urlComp)) {
                            bool bSecure = (urlComp.nScheme == INTERNET_SCHEME_HTTPS);
                            std::wstring server(urlComp.lpszHostName, urlComp.dwHostNameLength);
                            std::wstring path_ws = std::wstring(urlComp.lpszUrlPath, urlComp.dwUrlPathLength) + std::wstring(urlComp.lpszExtraInfo, urlComp.dwExtraInfoLength);
                            result->imageBytes = HttpDownloadToVector(server, path_ws, bSecure);
                            if (!result->imageBytes.empty()) {
                               Log("StartSessionTimer (Thread): Image for " + gamePtr->name + " downloaded.");
                               SaveImageBytesToCache(imageCachePathToUse, result->imageBytes);
                            } else {
                               Log("StartSessionTimer (Thread): Failed to download image for " + gamePtr->name + ".");
                            }
                        } else {
                            Log("StartSessionTimer (Thread): Failed to crack image URL for " + gamePtr->name + ".");
                        }
                    }
                }
            } else {
                 Log("StartSessionTimer (Thread): Game with AppID " + appId + " not found.");
                 result->success = false;
            }
            PostMessage(hwndMain, WM_APP_GAME_DATA_READY, 0, (LPARAM)result);
        }).detach();
    } else if (appIdForInitialVRLoad.empty()) {
         // No action needed if no game to load
    } else {
        Log("StartSessionTimer: Could not find main window handle to post game data ready message for initial VR load.");
    }

    time_t now = time(nullptr);
    g_sessionEndTime = now + totalSeconds;
    SaveSessionEndTime(g_sessionEndTime);

    g_SessionTimeSeconds = totalSeconds;
    SetTimer(hWnd, SESSION_TIMER_ID, SESSION_TICK_INTERVAL, NULL);
    UpdateTimeLeftDisplay();

    Log("Session timer started for " + std::to_string(totalSeconds) + "s.");

    if (g_connected)
    {
        int minutes = totalSeconds / 60;
        char buf[64];
        sprintf_s(buf, "SESSION_STARTED %d", minutes);
        SendToMaster(buf);
    }
}

void StartSession(HWND hWnd)
{
    int sel = (int)SendMessageW(g_hComboSessionTime, CB_GETCURSEL, 0, 0);
    int minutes = 0;
    switch (sel)
    {
    case 0: minutes = 30;  break;
    case 1: minutes = 60;  break;
    case 2: minutes = 90;  break;
    case 3: minutes = 120; break;
    case 4: minutes = 150; break;
    case 5: minutes = 180; break;
    case 6:
    {
        wchar_t buf[16];
        GetWindowTextW(g_hEditCustomMinutes, buf, 16);
        minutes = _wtoi(buf);
        break;
    }
    default: minutes = 0; break;
    }

    if (minutes <= 0)
    {
        MessageBoxW(NULL, L"Please select a valid session time.", L"Error", MB_OK | MB_ICONERROR);
        Log("StartSession -> invalid minutes: " + std::to_string(minutes));
        return;
    }

    Log("StartSession -> Beginning session start process");
    
    // Use a timeout mechanism when hiding the overlay
    // std::thread hideThread([]{
    //     try {
    //         HideOverlayContinuous();
    //     } catch (const std::exception& e) {
    //         Log("ERROR in HideOverlayContinuous: " + std::string(e.what()));
    //     }
    //     
    //     Log("Overlay hide thread completed");
    // });
    // hideThread.detach();
    
    // Give the hide thread a moment to work
    // Sleep(100);
    
    // Then start the session timer
    Log("StartSession -> Starting session timer");
    StartSessionTimer(hWnd, minutes * 60);
    ShowOverlayContinuous();
    Log("StartSession -> Session started successfully");
}

void AddTimeToSession(int minutes, int seconds)
{
    if (!g_SessionRunning)
    {
        Log("AddTimeToSession -> no session running, skip.");
        return;
    }
    g_sessionEndTime += (minutes * 60) + seconds;
    SaveSessionEndTime(g_sessionEndTime);

    time_t now = time(nullptr);
    g_SessionTimeSeconds = (int)(g_sessionEndTime - now);

    Log("Added " + std::to_string(minutes) + " min " + std::to_string(seconds) + " sec. New leftover=" + std::to_string(g_SessionTimeSeconds) + "s.");
    UpdateTimeLeftDisplay();

    if (g_connected)
    {
        char buf[64];
        sprintf_s(buf, "TIME_LEFT %d", g_SessionTimeSeconds);
        SendToMaster(buf);
    }
}

void AddMoreTimeToSession()
{
    if (g_SessionRunning)
    {
        AddTimeToSession(30, 0);
    }
    else
    {
        Log("AddMoreTimeToSession -> no session running.");
    }
}

void StopSession(HWND hWnd)
{
    if (g_SessionRunning)
    {
        StopSessionTimer(hWnd);

        // --- ADD THIS BLOCK ---
        // If a custom game was launched, terminate it.
        if (g_customGamePID != 0)
        {
            Log("StopSession: Terminating running custom game with PID: " + std::to_string(g_customGamePID));
            KillProcessByPID(g_customGamePID);
            g_customGamePID = 0; // Reset the PID
            // --- ADD THIS LINE ---
            g_runningCustomGameAppId = ""; // Reset the AppID
        }
        // --- END ADD ---

        QuitVRApp();
        // ShowOverlayContinuous();

        if (g_connected)
        {
            SendToMaster("SESSION_STOPPED");
        }
    }
    else
    {
        Log("StopSession -> no session running to stop.");
    }
}