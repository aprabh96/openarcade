#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <winhttp.h> // <-- Added this include
#pragma comment(lib, "winhttp.lib") // <-- Added this linker comment
#include <commdlg.h> // Make sure this is included at the top
#include <shlobj.h>  // Make sure this is included at the top
#include <fstream>   // Make sure this is included for ifstream

#include "window_proc.h"
#include "session.h"
#include "overlay.h"
#include "logging.h"
#include "network.h"
#include "config.h"
#include "steam_games.h"
#include "steam_api.h"
#include "html_exporter.h" // <-- ADD THIS
#include <vector>

// Forward declarations for helper functions within window_proc.cpp
int GetSelectedGameListIndex(int& outListBoxIndex);
void PopulateCategoriesListBox(HWND hWndParent);
void UpdateGameMembershipList(HWND hWndParent, int gameVectorIndex);
void UpdateCategoryControlsState(HWND hWndParent);

// ===================== Category Management Helpers =====================
void PopulateCategoriesListBox(HWND hWndParent) {
    if (!g_hListCategories) return;
    SendMessageW(g_hListCategories, LB_RESETCONTENT, 0, 0);
    std::lock_guard<std::mutex> lock(g_categoriesMutex);

    // The following sort is the problem. It re-sorts the vector alphabetically
    // every time the listbox is drawn, undoing your manual re-ordering.
    // std::sort(g_categories.begin(), g_categories.end(), [](const std::unique_ptr<GameCategory>& a, const std::unique_ptr<GameCategory>& b) {
    //     return *a < *b;
    // }); // <-- DELETE THIS ENTIRE BLOCK

    for (size_t i = 0; i < g_categories.size(); ++i) {
        if (g_categories[i]) {
            std::wstring wCategoryName = StringToWString(g_categories[i]->name);
            LRESULT lb_idx = SendMessageW(g_hListCategories, LB_ADDSTRING, 0, (LPARAM)wCategoryName.c_str());
            SendMessageW(g_hListCategories, LB_SETITEMDATA, lb_idx, (LPARAM)g_categories[i]->id);
        }
    }
    Log("Populated categories list box with " + std::to_string(g_categories.size()) + " items.");
}

void UpdateGameMembershipList(HWND hWndParent, int gameVectorIndex) {
    if (!g_hListGameMembership || !g_hStaticGameCategoriesLabel) return;
    SendMessageW(g_hListGameMembership, LB_RESETCONTENT, 0, 0);
    SetWindowTextW(g_hStaticGameCategoriesLabel, L"Selected game is in categories:");
    if (gameVectorIndex < 0) {
        EnableWindow(g_hListGameMembership, FALSE);
        return;
    }
    EnableWindow(g_hListGameMembership, TRUE);
    std::lock_guard<std::mutex> games_lock(g_gamesMutex);
    if (static_cast<size_t>(gameVectorIndex) >= g_games.size() || !g_games[gameVectorIndex]) {
        Log("UpdateGameMembershipList: gameVectorIndex " + std::to_string(gameVectorIndex) + " is out of bounds for g_games (size: " + std::to_string(g_games.size()) + ") or game pointer is null.");
        SetWindowTextW(g_hStaticGameCategoriesLabel, L"Error: Game data not found for selection.");
        EnableWindow(g_hListGameMembership, FALSE); // Disable if error
        return;
    }
    const std::string& selectedGameAppId = g_games[gameVectorIndex]->appid;
    std::lock_guard<std::mutex> cat_lock(g_categoriesMutex);
    int membershipCount = 0;
    for (size_t i = 0; i < g_categories.size(); ++i) {
        if (g_categories[i]) {
            const auto& appIds = g_categories[i]->gameAppIds;
            if (std::find(appIds.begin(), appIds.end(), selectedGameAppId) != appIds.end()) {
                std::wstring wCategoryName = StringToWString(g_categories[i]->name);
                LRESULT lb_idx = SendMessageW(g_hListGameMembership, LB_ADDSTRING, 0, (LPARAM)wCategoryName.c_str());
                SendMessageW(g_hListGameMembership, LB_SETITEMDATA, lb_idx, (LPARAM)g_categories[i]->id);
                membershipCount++;
            }
        }
    }
    if (membershipCount == 0) {
        SetWindowTextW(g_hStaticGameCategoriesLabel, L"Selected game is not in any category.");
    }
}

void UpdateCategoryControlsState(HWND hWndParent) {
    bool gameSelected = (SendMessageW(g_hListGames, LB_GETCURSEL, 0, 0) != LB_ERR);
    int catListIdx = (int)SendMessageW(g_hListCategories, LB_GETCURSEL, 0, 0);
    int catListCount = (int)SendMessageW(g_hListCategories, LB_GETCOUNT, 0, 0); // <-- ADD THIS
    bool categorySelected = (catListIdx != LB_ERR);
    LPARAM catItemData = categorySelected ? SendMessageW(g_hListCategories, LB_GETITEMDATA, catListIdx, 0) : -1;
    int categoryId = categorySelected ? (int)catItemData : -1;
    EnableWindow(g_hButtonDeleteCategory, categorySelected);
    EnableWindow(g_hButtonRenameCategory, categorySelected && !GetWindowTextLength(g_hEditCategoryName));
    EnableWindow(g_hButtonAddGameToSelectedCat, gameSelected && categorySelected);
    
    // --- ADD/MODIFY THIS BLOCK ---
    EnableWindow(g_hButtonMoveCategoryUp, categorySelected && catListIdx > 0);
    EnableWindow(g_hButtonMoveCategoryDown, categorySelected && catListIdx < catListCount - 1);
    // --- END BLOCK ---
    bool enableRemoveButton = false;
    if (gameSelected && categorySelected && categoryId >= 0) {
        int lbGameIdx;
        int gameVecIdx = GetSelectedGameListIndex(lbGameIdx);
        if (gameVecIdx != -1) {
            std::lock_guard<std::mutex> games_lock(g_gamesMutex);
            const std::string& selectedGameAppId = g_games[gameVecIdx]->appid;
            std::lock_guard<std::mutex> cat_lock(g_categoriesMutex);
            auto it = std::find_if(g_categories.begin(), g_categories.end(), [categoryId](const std::unique_ptr<GameCategory>& u) { return u && u->id == categoryId; });
            if (it != g_categories.end() && *it) {
                const auto& appIds = (*it)->gameAppIds;
                if (std::find(appIds.begin(), appIds.end(), selectedGameAppId) != appIds.end()) {
                    enableRemoveButton = true;
                }
            }
        }
    }
    EnableWindow(g_hButtonRemoveGameFromSelectedCat, enableRemoveButton);
}

void PopulateGamesInCategoryList(int categoryId) {
    if (!g_hListGamesInCategory) return;

    SendMessageW(g_hListGamesInCategory, LB_RESETCONTENT, 0, 0);

    if (categoryId < 0) {
        EnableWindow(g_hListGamesInCategory, FALSE);
        // Update counter for no category selected
        if (g_hStaticCategoryGameCount) {
            SetWindowTextW(g_hStaticCategoryGameCount, L"Category games: (no category selected)");
        }
        return;
    }

    EnableWindow(g_hListGamesInCategory, TRUE);

    std::vector<std::string> appIds;
    std::string categoryName;
    // Get the list of appIds for the selected category
    {
        std::lock_guard<std::mutex> lock(g_categoriesMutex);
        auto it = std::find_if(g_categories.begin(), g_categories.end(), [categoryId](const std::unique_ptr<GameCategory>& u) { return u && u->id == categoryId; });
        if (it != g_categories.end() && *it) {
            appIds = (*it)->gameAppIds; // Make a copy of the AppIDs
            categoryName = (*it)->name; // Get category name for counter display
        }
    }

    if (appIds.empty()) {
        SendMessageW(g_hListGamesInCategory, LB_ADDSTRING, 0, (LPARAM)L"(This category is empty)");
        // Update counter for empty category
        if (g_hStaticCategoryGameCount) {
            std::string countText = "Category games: 0 ('" + categoryName + "' is empty)";
            SetWindowTextW(g_hStaticCategoryGameCount, StringToWString(countText).c_str());
        }
        return;
    }
    
    int gamesAddedCount = 0;
    // Now find the names for these appIds
    for (const auto& appId : appIds) {
        SteamGame* game = findGameByAppId(appId); // This function handles its own lock
        if (game) {
            std::wstring wGameName = StringToWString(game->name);
            SendMessageW(g_hListGamesInCategory, LB_ADDSTRING, 0, (LPARAM)wGameName.c_str());
            gamesAddedCount++;
        }
        else {
             std::wstring wMissingName = StringToWString("(Unknown game: AppID " + appId + ")");
             SendMessageW(g_hListGamesInCategory, LB_ADDSTRING, 0, (LPARAM)wMissingName.c_str());
             gamesAddedCount++;
        }
    }
    
    // Update the counter display
    if (g_hStaticCategoryGameCount) {
        std::string countText = "Category games: " + std::to_string(gamesAddedCount) + " (in '" + categoryName + "')";
        SetWindowTextW(g_hStaticCategoryGameCount, StringToWString(countText).c_str());
    }
}

// ===================== End Category Management Helpers =====================

#define WM_APP_GAME_DATA_READY (WM_APP + 100)
#define WM_APP_CACHE_SAVE_DONE (WM_APP + 101)
#define WM_APP_LOG_MESSAGE (WM_APP + 102)

// <ai_context>
// Implementation of WndProc logic
// </ai_context>

// Helper function to get selected game index (avoids repetition)
int GetSelectedGameListIndex(int& outListBoxIndex) {
    outListBoxIndex = (int)SendMessageW(g_hListGames, LB_GETCURSEL, 0, 0);
    if (outListBoxIndex == LB_ERR) {
        return -1; // No selection in listbox
    }
    LRESULT itemData = SendMessageW(g_hListGames, LB_GETITEMDATA, outListBoxIndex, 0);
    if (itemData == LB_ERR) {
        return -1; // Error getting item data
    }
    int gameVectorIndex = (int)itemData;
    // Validate index against the actual vector size under mutex protection
    std::lock_guard<std::mutex> lock(g_gamesMutex);
    if (gameVectorIndex < 0 || static_cast<size_t>(gameVectorIndex) >= g_games.size()) {
        Log("Error: ListBox item data index " + std::to_string(gameVectorIndex) + " is out of bounds for g_games vector.");
        return -1;
    }
    return gameVectorIndex; // Return index within g_games vector
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_CREATE:
    {
        Log("WndProc: WM_CREATE started.");
        g_hButtonQuit = CreateWindowW(L"BUTTON", L"Quit Running VR App",
            WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
            50, 20, 300, 30,
            hWnd, (HMENU)1, g_hInst, NULL);

        g_hButtonLaunchVR = CreateWindowW(L"BUTTON", L"Launch SteamVR",
            WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
            50, 60, 300, 30,
            hWnd, (HMENU)2, g_hInst, NULL);

        g_hButtonShowOverlay = CreateWindowW(L"BUTTON", L"Show Overlay (Continuous)",
            WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
            50, 100, 300, 30,
            hWnd, (HMENU)3, g_hInst, NULL);

        g_hButtonHideOverlay = CreateWindowW(L"BUTTON", L"Hide Overlay",
            WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
            50, 140, 300, 30,
            hWnd, (HMENU)4, g_hInst, NULL);

        g_hButtonTestContinuous = CreateWindowW(L"BUTTON", L"Test Continuous Overlay",
            WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
            50, 180, 300, 30,
            hWnd, (HMENU)7, g_hInst, NULL);

        g_hButtonStopTestContinuous = CreateWindowW(L"BUTTON", L"Stop Test Continuous Overlay",
            WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
            50, 220, 300, 30,
            hWnd, (HMENU)8, g_hInst, NULL);

        // Add Test Video button
        CreateWindowW(L"BUTTON", L"Test Video Playback",
            WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
            50, 260, 300, 30,
            hWnd, (HMENU)14, g_hInst, NULL);
        Log("WndProc: Basic buttons created.");

        // Steam Games related UI
        CreateWindowW(L"STATIC", L"Steam Games:",
            WS_CHILD | WS_VISIBLE,
            450, 20, 150, 20, // Shortened the label width to make space
            hWnd, nullptr, g_hInst, nullptr);

        g_hChkFilterGamesInCategories = CreateWindowW(L"BUTTON", L"Show only categorized",
            WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
            600, 20, 150, 20, // Positioned next to the label
            hWnd, (HMENU)ID_CHK_FILTER_GAMES_IN_CATEGORIES, g_hInst, NULL);
        Log("WndProc: Steam Games UI created.");

        g_hListGames = CreateWindowW(L"LISTBOX", L"",
            WS_CHILD | WS_VISIBLE | LBS_NOTIFY | WS_VSCROLL | WS_BORDER,
            450, 40, 300, 400, // Reduced height by 20px to make room for count
            hWnd, (HMENU)ID_GAMES_LIST, g_hInst, nullptr);

        g_hStaticGameCount = CreateWindowW(L"STATIC", L"Games: 0",
            WS_CHILD | WS_VISIBLE,
            450, 445, 300, 15, // Positioned just below the list
            hWnd, nullptr, g_hInst, nullptr);

        g_hButtonLaunchGame = CreateWindowW(L"BUTTON", L"Launch Selected",
            WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON | WS_DISABLED,
            450, 465, 95, 30, // Adjusted width
            hWnd, (HMENU)ID_LAUNCH_GAME, g_hInst, NULL);
        
        g_hButtonAddCustomGame = CreateWindowW(L"BUTTON", L"Add Custom",
            WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
            555, 465, 95, 30, // Positioned next to launch
            hWnd, (HMENU)ID_BUTTON_ADD_CUSTOM_GAME, g_hInst, NULL);
        
        g_hButtonDeleteCustomGame = CreateWindowW(L"BUTTON", L"Delete Custom",
            WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON | WS_DISABLED,
            660, 465, 95, 30, // Positioned next to add
            hWnd, (HMENU)ID_BUTTON_DELETE_CUSTOM_GAME, g_hInst, NULL);

        // Add Simulation Buttons for VR List Navigation
        CreateWindowW(L"BUTTON", L"Simulate List UP",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            450, 505, 145, 30, // Adjusted position
            hWnd, (HMENU)ID_SIMULATE_UP, g_hInst, NULL);

        CreateWindowW(L"BUTTON", L"Simulate List DOWN",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            605, 505, 145, 30, // Adjusted position
            hWnd, (HMENU)ID_SIMULATE_DOWN, g_hInst, NULL);

        CreateWindowW(L"BUTTON", L"Simulate List SELECT",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            450, 545, 300, 30, // Adjusted position
            hWnd, (HMENU)ID_SIMULATE_SELECT, g_hInst, NULL);

        // --- ADD THIS BLOCK ---
        CreateWindowW(L"STATIC", L"Game Name:",
            WS_CHILD | WS_VISIBLE,
            450, 580, 100, 20,
            hWnd, NULL, g_hInst, nullptr);

        g_hEditGameName = CreateWindowW(L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL | WS_DISABLED,
            450, 600, 300, 25,
            hWnd, (HMENU)ID_EDIT_GAME_NAME_DETAIL, g_hInst, nullptr);
        // --- END ADD ---

        // --- Add Manual Cache Editing UI ---
        CreateWindowW(L"STATIC", L"Description:",
            WS_CHILD | WS_VISIBLE,
            450, 635, 100, 20, // Adjusted Y position
            hWnd, (HMENU)ID_LABEL_DESC, g_hInst, nullptr);
        Log("WndProc: Manual Cache Editing UI created.");

        g_hEditGameDesc = CreateWindowW(L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | WS_BORDER | ES_MULTILINE | WS_VSCROLL | ES_AUTOVSCROLL | WS_DISABLED,
            450, 655, 300, 60, // Adjusted Y position
            hWnd, (HMENU)ID_EDIT_GAME_DESC, g_hInst, nullptr);

        CreateWindowW(L"STATIC", L"Header Image:",
            WS_CHILD | WS_VISIBLE,
            450, 725, 100, 20, // Adjusted Y position
            hWnd, (HMENU)ID_LABEL_IMAGE, g_hInst, nullptr);

        g_hEditImagePath = CreateWindowW(L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | WS_BORDER | ES_READONLY | WS_DISABLED, // Read-only
            450, 745, 215, 25, // Adjusted Y position
            hWnd, (HMENU)ID_EDIT_IMAGE_PATH, g_hInst, nullptr);

        g_hButtonBrowseImage = CreateWindowW(L"BUTTON", L"Browse...",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_DISABLED,
            675, 745, 75, 25, // Adjusted Y position
            hWnd, (HMENU)ID_BUTTON_BROWSE, g_hInst, NULL);

        // --- ADD THIS BLOCK right after g_hButtonBrowseImage is created ---
        CreateWindowW(L"STATIC", L"Executable Path (for custom games):",
            WS_CHILD | WS_VISIBLE,
            450, 780, 300, 20, // Adjusted Y position
            hWnd, (HMENU)ID_LABEL_EXE_PATH, g_hInst, nullptr);

        g_hEditExecutablePath = CreateWindowW(L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL | WS_DISABLED,
            450, 805, 215, 25, // Adjusted Y position
            hWnd, (HMENU)ID_EDIT_EXE_PATH, g_hInst, nullptr);

        g_hButtonBrowseExe = CreateWindowW(L"BUTTON", L"Browse...",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_DISABLED,
            675, 805, 75, 25, // Adjusted Y position
            hWnd, (HMENU)ID_BUTTON_BROWSE_EXE, g_hInst, NULL);

        g_hButtonSaveCache = CreateWindowW(L"BUTTON", L"Save Game Details", // <-- Text changed
            WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON | WS_DISABLED,
            770, 805, 300, 30, // Adjusted Y position
            hWnd, (HMENU)ID_BUTTON_SAVE_CACHE, g_hInst, NULL);
        // --- End Manual Cache Editing UI ---

        g_hComboSessionTime = CreateWindowW(L"COMBOBOX", L"",
            CBS_DROPDOWNLIST | WS_CHILD | WS_VISIBLE,
            50, 300, 150, 150,
            hWnd, nullptr, g_hInst, nullptr);
        Log("WndProc: Session Time UI created.");

        SendMessageW(g_hComboSessionTime, CB_ADDSTRING, 0, (LPARAM)L"30 min");
        SendMessageW(g_hComboSessionTime, CB_ADDSTRING, 0, (LPARAM)L"1 hr");
        SendMessageW(g_hComboSessionTime, CB_ADDSTRING, 0, (LPARAM)L"1.5 hrs");
        SendMessageW(g_hComboSessionTime, CB_ADDSTRING, 0, (LPARAM)L"2 hrs");
        SendMessageW(g_hComboSessionTime, CB_ADDSTRING, 0, (LPARAM)L"2.5 hrs");
        SendMessageW(g_hComboSessionTime, CB_ADDSTRING, 0, (LPARAM)L"3 hrs");
        SendMessageW(g_hComboSessionTime, CB_ADDSTRING, 0, (LPARAM)L"Custom");
        SendMessageW(g_hComboSessionTime, CB_SETCURSEL, 0, 0);

        g_hEditCustomMinutes = CreateWindowW(L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | WS_BORDER | ES_NUMBER,
            210, 300, 50, 20,
            hWnd, nullptr, g_hInst, nullptr);
        EnableWindow(g_hEditCustomMinutes, FALSE);

        g_hButtonStartSession = CreateWindowW(L"BUTTON", L"Start Session",
            WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
            50, 330, 120, 30,
            hWnd, (HMENU)9, g_hInst, NULL);

        g_hButtonAddTime = CreateWindowW(L"BUTTON", L"Add More Time (+30min)",
            WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
            180, 330, 170, 30,
            hWnd, (HMENU)10, g_hInst, NULL);

        g_hButtonStopSession = CreateWindowW(L"BUTTON", L"Stop Session",
            WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
            50, 370, 300, 30,
            hWnd, (HMENU)11, g_hInst, NULL);

        g_hStaticTimeLeft = CreateWindowW(L"STATIC", L"Time Left: 0:00",
            WS_CHILD | WS_VISIBLE,
            50, 410, 300, 20,
            hWnd, nullptr, g_hInst, nullptr);

        g_hEditLog = CreateWindowW(L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY,
            50, 440, 380, 200,
            hWnd, nullptr, g_hInst, nullptr);
        Log("WndProc: Other settings UI created.");

        g_hChkAutoOverlay = CreateWindowW(L"BUTTON", L"Auto Enable Overlay",
            WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
            50, 650, 300, 20,
            hWnd, (HMENU)12, g_hInst, nullptr);

        g_hEditIP = CreateWindowW(L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | WS_BORDER,
            50, 680, 200, 25,
            hWnd, nullptr, g_hInst, nullptr);

        {
            std::wstring wIP(g_masterIP.begin(), g_masterIP.end());
            SetWindowTextW(g_hEditIP, wIP.c_str());
        }

        g_hButtonConnect = CreateWindowW(L"BUTTON", L"Connect to Master",
            WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
            270, 680, 150, 25,
            hWnd, (HMENU)13, g_hInst, nullptr);

        CreateWindowW(L"STATIC", L"Station Name:",
            WS_CHILD | WS_VISIBLE,
            50, 720, 100, 20,
            hWnd, nullptr, g_hInst, nullptr);

        g_hEditStationName = CreateWindowW(L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | WS_BORDER,
            160, 720, 150, 25,
            hWnd, nullptr, g_hInst, nullptr);

        {
            std::wstring wStation(g_stationName.begin(), g_stationName.end());
            SetWindowTextW(g_hEditStationName, wStation.c_str());
        }

        if (g_AutoEnableOverlay)
        {
            SendMessageW(g_hChkAutoOverlay, BM_SETCHECK, BST_CHECKED, 0);
            EnableWindow(g_hButtonHideOverlay, FALSE);
        }
        else
        {
            SendMessageW(g_hChkAutoOverlay, BM_SETCHECK, BST_UNCHECKED, 0);
            EnableWindow(g_hButtonHideOverlay, TRUE);
        }

        // Load Steam games
        Log("Loading Steam games...");
        LoadSteamGames(hWnd);

        // Start the persistent SteamVR check timer
        // --- Category Management UI (New Column) ---
        Log("WndProc: Creating Category Management UI...");
        // Position these to the right of the Game List & Cache UI, or rearrange as needed.
        // Assuming window width is now ~1200. Game list + cache UI ends around x=750.
        // Start new column at x = 770.

        CreateWindowW(L"STATIC", L"Categories:",
            WS_CHILD | WS_VISIBLE,
            770, 20, 200, 20,
            hWnd, nullptr, g_hInst, nullptr);

        g_hListCategories = CreateWindowW(L"LISTBOX", L"",
            WS_CHILD | WS_VISIBLE | LBS_NOTIFY | WS_VSCROLL | WS_BORDER,
            770, 40, 200, 100,
            hWnd, (HMENU)ID_LIST_CATEGORIES, g_hInst, nullptr);

        g_hEditCategoryName = CreateWindowW(L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | WS_BORDER,
            770, 150, 200, 25,
            hWnd, (HMENU)ID_EDIT_CATEGORY_NAME, g_hInst, nullptr);

        g_hButtonCreateCategory = CreateWindowW(L"BUTTON", L"Create Category",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            770, 180, 200, 30,
            hWnd, (HMENU)ID_BUTTON_CREATE_CATEGORY, g_hInst, NULL);
        
        g_hButtonRenameCategory = CreateWindowW(L"BUTTON", L"Rename Selected",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            770, 215, 200, 30,
            hWnd, (HMENU)ID_BUTTON_RENAME_CATEGORY, g_hInst, NULL);

        g_hButtonDeleteCategory = CreateWindowW(L"BUTTON", L"Delete Selected Category",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            770, 250, 200, 30,
            hWnd, (HMENU)ID_BUTTON_DELETE_CATEGORY, g_hInst, NULL);

        // --- ADD THIS NEW BLOCK ---
        g_hButtonMoveCategoryUp = CreateWindowW(L"BUTTON", L"Move Up",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            980, 40, 95, 30, // Positioned to the right of the category list
            hWnd, (HMENU)ID_BUTTON_MOVE_CAT_UP, g_hInst, NULL);

        g_hButtonMoveCategoryDown = CreateWindowW(L"BUTTON", L"Move Down",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            980, 75, 95, 30, // Positioned below Move Up
            hWnd, (HMENU)ID_BUTTON_MOVE_CAT_DOWN, g_hInst, NULL);
        // --- END OF NEW BLOCK ---

        // --- Add Simulate Category Buttons ---
        CreateWindowW(L"STATIC", L"Simulate Category VR:",
            WS_CHILD | WS_VISIBLE,
            770, 285, 200, 20,
            hWnd, nullptr, g_hInst, nullptr);

        g_hButtonSimulateCatUp = CreateWindowW(L"BUTTON", L"Sim Cat UP",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            770, 305, 95, 30,
            hWnd, (HMENU)ID_SIMULATE_CAT_UP, g_hInst, NULL);

        g_hButtonSimulateCatDown = CreateWindowW(L"BUTTON", L"Sim Cat DOWN",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            875, 305, 95, 30,
            hWnd, (HMENU)ID_SIMULATE_CAT_DOWN, g_hInst, NULL);

        CreateWindowW(L"STATIC", L"Selected Game (from left list):",
            WS_CHILD | WS_VISIBLE,
            770, 375, 300, 20,
            hWnd, nullptr, g_hInst, nullptr);

        g_hStaticGameCategoriesLabel = CreateWindowW(L"STATIC", L"Game is in categories:",
            WS_CHILD | WS_VISIBLE,
            770, 395, 300, 20,
            hWnd, nullptr, g_hInst, nullptr);

        g_hListGameMembership = CreateWindowW(L"LISTBOX", L"",
            WS_CHILD | WS_VISIBLE | LBS_NOSEL | WS_VSCROLL | WS_BORDER,
            770, 415, 200, 100,
            hWnd, (HMENU)ID_LIST_GAME_MEMBERSHIP, g_hInst, nullptr);

        g_hButtonAddGameToSelectedCat = CreateWindowW(L"BUTTON", L"Add Game to Selected Category",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            770, 525, 300, 30,
            hWnd, (HMENU)ID_BUTTON_ADD_GAME_TO_CAT, g_hInst, NULL);

        g_hButtonRemoveGameFromSelectedCat = CreateWindowW(L"BUTTON", L"Remove Game from Selected Category",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            770, 560, 300, 30,
            hWnd, (HMENU)ID_BUTTON_REMOVE_GAME_FROM_CAT, g_hInst, NULL);
            
        // --- ADD THIS NEW BLOCK ---
        g_hStaticGamesInCategoryLabel = CreateWindowW(L"STATIC", L"Games in selected category:",
            WS_CHILD | WS_VISIBLE,
            770, 600, 300, 20, // Positioned below other category controls
            hWnd, (HMENU)NULL, g_hInst, nullptr);

        g_hListGamesInCategory = CreateWindowW(L"LISTBOX", L"",
            WS_CHILD | WS_VISIBLE | LBS_NOSEL | WS_VSCROLL | WS_BORDER | LBS_SORT, // LBS_SORT will auto-sort alphabetically
            770, 620, 300, 85, // Reduced height by 15px to make room for count
            hWnd, (HMENU)ID_LIST_GAMES_IN_CATEGORY, g_hInst, nullptr);

        g_hStaticCategoryGameCount = CreateWindowW(L"STATIC", L"Category games: 0",
            WS_CHILD | WS_VISIBLE,
            770, 710, 300, 15, // Positioned just below the category games list
            hWnd, nullptr, g_hInst, nullptr);

        EnableWindow(g_hListGamesInCategory, FALSE);
        // --- END OF NEW BLOCK ---

        // --- ADD HTML EXPORT BUTTON ---
        g_hButtonExportHTML = CreateWindowW(L"BUTTON", L"Export Game List (HTML)",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            770, 730, 300, 20, // Positioned above the Save Game Details button
            hWnd, (HMENU)ID_BUTTON_EXPORT_HTML, g_hInst, NULL);

        // After loading games and ALL Category UI in WM_CREATE:
        Log("WndProc: All core UI created. Now populating category list and updating controls.");
        PopulateCategoriesListBox(hWnd);
        UpdateCategoryControlsState(hWnd);
        // int lbGameIdx;
        // int gameVecIdx = GetSelectedGameListIndex(lbGameIdx);
        // UpdateGameMembershipList(hWnd, gameVecIdx);

        // Start the persistent SteamVR check timer
        SetTimer(hWnd, STEAMVR_PERSISTENT_CHECK_TIMER_ID, STEAMVR_PERSISTENT_CHECK_INTERVAL, NULL);
        Log("WndProc: Persistent SteamVR Check Timer (ID=" + std::to_string(STEAMVR_PERSISTENT_CHECK_TIMER_ID) + ") started.");

        break;
    }
    case WM_COMMAND:
    {
        if ((HWND)lParam == g_hComboSessionTime && HIWORD(wParam) == CBN_SELCHANGE)
        {
            int sel = (int)SendMessageW(g_hComboSessionTime, CB_GETCURSEL, 0, 0);
            if (sel == 6)
                EnableWindow(g_hEditCustomMinutes, TRUE);
            else
                EnableWindow(g_hEditCustomMinutes, FALSE);
        }
        else if ((HWND)lParam == g_hChkAutoOverlay && HIWORD(wParam) == BN_CLICKED)
        {
            LRESULT state = SendMessageW(g_hChkAutoOverlay, BM_GETCHECK, 0, 0);
            g_AutoEnableOverlay = (state == BST_CHECKED);
            SaveSettings();
            Log(std::string("User toggled AutoOverlay -> ") + (g_AutoEnableOverlay ? "ON" : "OFF"));

            if (g_AutoEnableOverlay)
            {
                EnableWindow(g_hButtonHideOverlay, FALSE);
            }
            else
            {
                if (hWnd)
                {
                    KillTimer(hWnd, STEAMVR_CHECK_TIMER_ID);
                }
                EnableWindow(g_hButtonHideOverlay, TRUE);
            }
        }
        else if ((HWND)lParam == g_hListCategories && HIWORD(wParam) == LBN_SELCHANGE) {
            wchar_t categoryNameBuffer[256];
            int currentSel = (int)SendMessageW(g_hListCategories, LB_GETCURSEL, 0, 0);
            if (currentSel != LB_ERR) {
                SendMessageW(g_hListCategories, LB_GETTEXT, currentSel, (LPARAM)categoryNameBuffer);
                SetWindowTextW(g_hEditCategoryName, categoryNameBuffer);
            } else {
                SetWindowTextW(g_hEditCategoryName, L"");
            }
            UpdateCategoryControlsState(hWnd);
            // --- ADD THESE TWO LINES ---
            int categoryId = (currentSel != LB_ERR) ? (int)SendMessageW(g_hListCategories, LB_GETITEMDATA, currentSel, 0) : -1;
            PopulateGamesInCategoryList(categoryId);
        }
        else if ((HWND)lParam == g_hListGames && HIWORD(wParam) == LBN_SELCHANGE)
        {
            int currentListBoxIndex = -1; // Will be filled by GetSelectedGameListIndex
            int gameActualVectorIndex = GetSelectedGameListIndex(currentListBoxIndex);

            if (gameActualVectorIndex >= 0) // Correctly check the returned vector index
            {
                std::string gameAppIdForUIThread;
                std::string gameNameForUIThread; // For logging
                std::string gameDescriptionForUIThread;
                std::string gameHeaderImageForUIThread; // URL or cache marker
                bool needsDataFetch = false;
                bool isValidGameSelection = false;

                { // Scope for g_gamesMutex to read initial data and check validity
                    std::lock_guard<std::mutex> lock(g_gamesMutex);
                    if (static_cast<size_t>(gameActualVectorIndex) < g_games.size() && g_games[gameActualVectorIndex]) {
                        SteamGame& selectedGame = *g_games[gameActualVectorIndex];
                        gameAppIdForUIThread = selectedGame.appid;
                        gameNameForUIThread = selectedGame.name;
                        gameDescriptionForUIThread = selectedGame.description;
                        gameHeaderImageForUIThread = selectedGame.headerImage;
                        needsDataFetch = !selectedGame.storeDataFetched;
                        isValidGameSelection = true;
                    } else {
                        Log("Error: LBN_SELCHANGE: gameActualVectorIndex " + std::to_string(gameActualVectorIndex) + " is out of bounds or unique_ptr is null during initial read.");
                    }
                } // g_gamesMutex is released

                if (isValidGameSelection) {
                    EnableWindow(g_hButtonLaunchGame, TRUE);
                    // --- ADD THIS ---
                    EnableWindow(g_hEditGameName, TRUE);
                    // --- END ADD ---
                    EnableWindow(g_hEditGameDesc, TRUE);
                    EnableWindow(g_hEditImagePath, TRUE);
                    EnableWindow(g_hButtonBrowseImage, TRUE);
                    EnableWindow(g_hButtonSaveCache, TRUE);

                    // --- NEW LOGIC FOR CUSTOM GAMES ---
                    bool is_custom_game = false;
                    std::string exe_path;
                    // --- ADD THIS ---
                    std::string game_name;
                    // --- END ADD ---
                    {
                        // Re-lock briefly to get custom game status
                        std::lock_guard<std::mutex> lock(g_gamesMutex);
                        is_custom_game = g_games[gameActualVectorIndex]->isCustom;
                        exe_path = g_games[gameActualVectorIndex]->executablePath;
                        // --- ADD THIS ---
                        game_name = g_games[gameActualVectorIndex]->name;
                        // --- END ADD ---
                    }

                    // --- ADD/MODIFY THIS BLOCK ---
                    SetWindowTextW(g_hEditGameName, StringToWString(game_name).c_str());
                    SendMessageW(g_hEditGameName, EM_SETREADONLY, (WPARAM)!is_custom_game, 0);

                    if (is_custom_game) {
                        EnableWindow(g_hEditExecutablePath, TRUE);
                        EnableWindow(g_hButtonBrowseExe, TRUE);
                        // --- ADD THIS ---
                        EnableWindow(g_hButtonDeleteCustomGame, TRUE);
                        // --- END ADD ---
                        SetWindowTextW(g_hEditExecutablePath, StringToWString(exe_path).c_str());
                    } else {
                        EnableWindow(g_hEditExecutablePath, FALSE);
                        EnableWindow(g_hButtonBrowseExe, FALSE);
                        // --- ADD THIS ---
                        EnableWindow(g_hButtonDeleteCustomGame, FALSE);
                        // --- END ADD ---
                        SetWindowTextW(g_hEditExecutablePath, L"");
                    }
                    // --- END NEW LOGIC ---

                    SetWindowTextW(g_hEditGameDesc, StringToWString(gameDescriptionForUIThread).c_str());
                    if (!gameHeaderImageForUIThread.empty()) {
                        if (gameHeaderImageForUIThread.rfind("cache://", 0) == 0) {
                            std::string actualFilename = gameHeaderImageForUIThread.substr(8);
                            std::string displayPath = "(Using cached image: " + GetCachePathForImage(gameAppIdForUIThread, actualFilename) + ")";
                            SetWindowTextW(g_hEditImagePath, StringToWString(displayPath).c_str());
                        } else {
                            std::string displayPath = "(Using URL: " + gameHeaderImageForUIThread + ")";
                            SetWindowTextW(g_hEditImagePath, StringToWString(displayPath).c_str());
                        }
                    } else {
                        SetWindowTextW(g_hEditImagePath, L"(No image set)");
                    }

                    if (needsDataFetch) {
                        Log("Selected game data not fetched for '" + gameNameForUIThread + "' (AppID: " + gameAppIdForUIThread + "), fetching in background.");
                        HWND mainHwnd = hWnd;

                        std::thread([appId = gameAppIdForUIThread, name = gameNameForUIThread, mainHwnd]() {
                            GameDataResult* result = new GameDataResult();
                            result->appId = appId;
                            result->success = false;
                            SteamGame* gameToFetchPtr = nullptr;
                            { // Scope for g_gamesMutex
                                std::lock_guard<std::mutex> games_lock(g_gamesMutex);
                                for(const auto& game_ptr_iter : g_games) {
                                    if (game_ptr_iter && game_ptr_iter->appid == appId) {
                                        gameToFetchPtr = game_ptr_iter.get();
                                        break;
                                    }
                                }
                            } // g_gamesMutex released by thread

                            if (gameToFetchPtr) {
                                FetchStoreDataForGame(*gameToFetchPtr); // Modifies gameToFetchPtr members

                                { // Lock game's dataMutex to safely read recently fetched data for the result
                                    std::lock_guard<std::mutex> game_data_lock(gameToFetchPtr->dataMutex);
                                    result->description = gameToFetchPtr->description;
                                    result->headerUrl = gameToFetchPtr->headerImage; // URL or cache marker
                                    result->success = gameToFetchPtr->storeDataFetched; // Should be true after FetchStoreDataForGame
                                }

                                if (result->success && !result->headerUrl.empty()) {
                                    FixEscapedSlashes(result->headerUrl);
                                    std::string imageCachePathToUse;
                                    bool isManualCacheMarker = (result->headerUrl.rfind("cache://", 0) == 0);

                                    if (isManualCacheMarker) {
                                        std::string actualFilename = result->headerUrl.substr(8);
                                        imageCachePathToUse = GetCachePathForImage(appId, actualFilename);
                                    } else {
                                        imageCachePathToUse = GetCachePathForImage(appId, result->headerUrl);
                                    }

                                    if (LoadImageBytesFromCache(imageCachePathToUse, result->imageBytes)) {
                                        Log("Background Thread: Image bytes for " + name + " loaded from cache: " + imageCachePathToUse);
                                    } else if (!isManualCacheMarker) { // Not in cache and not a manual marker, try download
                                        Log("Background Thread: Image for " + name + " not in cache, attempting download from: " + result->headerUrl);
                                        std::wstring wImageUrl = StringToWString(result->headerUrl);
                                        URL_COMPONENTSW urlComp = {0};
                                        urlComp.dwStructSize = sizeof(urlComp);
                                        const DWORD buffSize = 1024;
                                        wchar_t szScheme[buffSize], szHostName[buffSize], szUrlPath[buffSize], szExtraInfo[buffSize];
                                        urlComp.lpszScheme = szScheme; urlComp.dwSchemeLength = buffSize;
                                        urlComp.lpszHostName = szHostName; urlComp.dwHostNameLength = buffSize;
                                        urlComp.lpszUrlPath = szUrlPath; urlComp.dwUrlPathLength = buffSize;
                                        urlComp.lpszExtraInfo = szExtraInfo; urlComp.dwExtraInfoLength = buffSize;

                                        if (WinHttpCrackUrl(wImageUrl.c_str(), (DWORD)wImageUrl.length(), 0, &urlComp)) {
                                            bool bSecure = (urlComp.nScheme == INTERNET_SCHEME_HTTPS);
                                            std::wstring server(urlComp.lpszHostName, urlComp.dwHostNameLength);
                                            std::wstring path = std::wstring(urlComp.lpszUrlPath, urlComp.dwUrlPathLength) +
                                                                std::wstring(urlComp.lpszExtraInfo, urlComp.dwExtraInfoLength);
                                            result->imageBytes = HttpDownloadToVector(server, path, bSecure);
                                            if (!result->imageBytes.empty()) {
                                                Log("Background Thread: Image for " + name + " downloaded (" + std::to_string(result->imageBytes.size()) + " bytes).");
                                                SaveImageBytesToCache(imageCachePathToUse, result->imageBytes);
                                            } else {
                                                Log("Background Thread: Failed to download image for " + name + " from URL.");
                                            }
                                        } else {
                                            Log("Background Thread: Failed to crack image URL for " + name + ": " + WStringToString(wImageUrl));
                                        }
                                    } else if (isManualCacheMarker && result->imageBytes.empty()){
                                        Log("Background Thread: Manual cache marker for " + name + " (" + result->headerUrl + ") but failed to load bytes from " + imageCachePathToUse);
                                    }
                                } else if (result->success) { // Metadata fetched but no header image URL
                                    Log("Background Thread: Metadata for " + name + " fetched, but no header image URL was found.");
                                }
                                Log("Background fetch for " + name + " (AppID: " + appId + ") data preparation complete. Posting WM_APP_GAME_DATA_READY.");
                            } else {
                                Log("Background Thread: Game with AppID " + appId + " not found after attempting to re-fetch from g_games.");
                                result->success = false; // Indicate failure to find/fetch
                            }
                            PostMessage(mainHwnd, WM_APP_GAME_DATA_READY, 0, (LPARAM)result);
                        }).detach();
                    }
                } else { // isValidGameSelection was false
                    // Disable game-specific controls if game data is invalid from the start
                    EnableWindow(g_hButtonLaunchGame, FALSE);
                    EnableWindow(g_hEditGameName, FALSE);
                    EnableWindow(g_hEditGameDesc, FALSE);
                    EnableWindow(g_hEditImagePath, FALSE);
                    EnableWindow(g_hButtonBrowseImage, FALSE);
                    EnableWindow(g_hButtonSaveCache, FALSE);
                    EnableWindow(g_hEditExecutablePath, FALSE);
                    EnableWindow(g_hButtonBrowseExe, FALSE);
                    // --- ADD THIS ---
                    EnableWindow(g_hButtonDeleteCustomGame, FALSE);
                    // --- END ADD ---
                    SetWindowTextW(g_hEditGameName, L"");
                    SetWindowTextW(g_hEditGameDesc, L"");
                    SetWindowTextW(g_hEditImagePath, L"(Error loading game details)");
                    SetWindowTextW(g_hEditExecutablePath, L"");
                }

                UpdateGameMembershipList(hWnd, gameActualVectorIndex); // Pass the vector index
                UpdateCategoryControlsState(hWnd);
            } else { // gameActualVectorIndex < 0 (no selection or error from GetSelectedGameListIndex)
                Log("LBN_SELCHANGE: No valid game selected or error retrieving index from GetSelectedGameListIndex.");
                EnableWindow(g_hButtonLaunchGame, FALSE);
                EnableWindow(g_hEditGameName, FALSE);
                EnableWindow(g_hEditGameDesc, FALSE);
                EnableWindow(g_hEditImagePath, FALSE);
                EnableWindow(g_hButtonBrowseImage, FALSE);
                EnableWindow(g_hButtonSaveCache, FALSE);
                EnableWindow(g_hEditExecutablePath, FALSE);
                EnableWindow(g_hButtonBrowseExe, FALSE);
                // --- ADD THIS ---
                EnableWindow(g_hButtonDeleteCustomGame, FALSE);
                // --- END ADD ---
                SetWindowTextW(g_hEditGameName, L"");
                SetWindowTextW(g_hEditGameDesc, L"");
                SetWindowTextW(g_hEditImagePath, L"");
                SetWindowTextW(g_hEditExecutablePath, L"");
                UpdateGameMembershipList(hWnd, -1);
                UpdateCategoryControlsState(hWnd);
            }
        }

        int wmId = LOWORD(wParam);
        switch (wmId)
        {
        case 1:  QuitVRApp();                   break;
        case 2:  LaunchSteamVR();               break;
        case 3:  ShowOverlayContinuous();       break;
        case 4:  HideOverlayContinuous();       break;
        case 7:  StartContinuousOverlayTest(); break;
        case 8:  StopContinuousOverlayTest();  break;
        case 9:  StartSession(hWnd);            break;
        case 10: AddMoreTimeToSession();        break;
        case 11: StopSession(hWnd);             break;
        case 14: TestVideoPlayback(hWnd);       break; // Handle Test Video button
        case ID_CHK_FILTER_GAMES_IN_CATEGORIES:
        {
            LRESULT state = SendMessageW(g_hChkFilterGamesInCategories, BM_GETCHECK, 0, 0);
            g_filterGamesInCategories = (state == BST_CHECKED);
            Log("User toggled 'Filter Games in Categories' -> " + std::string(g_filterGamesInCategories ? "ON" : "OFF"));
            // Repopulate the games list based on the new filter state
            PopulateGamesListBox();
            break;
        }
        case ID_LAUNCH_GAME:
        {
            // Handle launching the selected game
            int sel = (int)SendMessageW(g_hListGames, LB_GETCURSEL, 0, 0);
            if (sel != LB_ERR)
            {
                size_t idx = (size_t)SendMessageW(g_hListGames, LB_GETITEMDATA, sel, 0);
                if (idx < g_games.size())
                {
                    Log("Launching game: " + g_games[idx]->name);
                    LaunchGame(*g_games[idx]);
                }
            }
            break;
        }
        case ID_SIMULATE_UP:
        {
            Log("Simulate UP pressed.");
            std::unique_lock<std::mutex> overlayLock(g_overlayStateMutex); // For g_selectedGameIndex, g_isLoadingGameData, etc.
            std::unique_lock<std::mutex> vrListLock(g_VROverlayGameListMutex); // For g_currentCategoryGameAppIds_VR

            if (g_currentCategoryGameAppIds_VR.empty()) {
                Log("Simulate UP: VR category game list is empty.");
                g_isLoadingGameData = false; // Ensure this is reset if necessary
                vrListLock.unlock();
                overlayLock.unlock();
                break;
            }

            if (g_selectedGameIndex <= 0) {
                g_selectedGameIndex = static_cast<int>(g_currentCategoryGameAppIds_VR.size()) - 1;
            } else {
                g_selectedGameIndex--;
            }
            Log("Simulated selection index (for current VR list): " + std::to_string(g_selectedGameIndex));

            // Ensure g_selectedGameIndex is valid
            if (g_selectedGameIndex < 0 || static_cast<size_t>(g_selectedGameIndex) >= g_currentCategoryGameAppIds_VR.size()) {
                 Log("Simulate UP: Invalid g_selectedGameIndex after operation: " + std::to_string(g_selectedGameIndex) + ". Resetting.");
                 if (g_currentCategoryGameAppIds_VR.empty()) { // Should not happen if we passed the first check
                     g_selectedGameIndex = -1;
                 } else {
                    g_selectedGameIndex = 0; // Default to first item, or handle as error
                 }
            }
            
            if (g_selectedGameIndex == -1) { // If list became effectively empty or an error state
                g_isLoadingGameData = false;
                vrListLock.unlock();
                overlayLock.unlock();
                break;
            }

            std::string appIdToLoad = g_currentCategoryGameAppIds_VR[g_selectedGameIndex];
            vrListLock.unlock(); // Unlock g_VROverlayGameListMutex before starting thread

            g_isLoadingGameData = true;
            SAFE_RELEASE(g_pSelectedGameHeader);
            SAFE_RELEASE(g_pDescTextLayout); 
            g_descScrollOffsetPx = 0;
            g_currentLayoutAppId = "";      

            overlayLock.unlock(); 

            HWND hwndMain = hWnd; 
            std::thread([appIdToLoad, hwndMain]() { 
                Log("Simulate UP (Thread): Starting data fetch for AppID " + appIdToLoad);
                GameDataResult* result = new GameDataResult();
                result->appId = appIdToLoad;
                result->success = false;

                SteamGame* gamePtr = findGameByAppId(appIdToLoad); 
                if (gamePtr) {
                    FetchStoreDataForGame(*gamePtr); 

                    std::lock_guard<std::mutex> gameDataLock(gamePtr->dataMutex); // Lock game's own data mutex
                    result->description = gamePtr->description;
                    result->headerUrl = gamePtr->headerImage; 
                    result->success = gamePtr->storeDataFetched; 

                    if (result->success && !result->headerUrl.empty()) {
                        FixEscapedSlashes(result->headerUrl);
                        std::string imageCachePathToUse;
                        bool isManualCacheMarker = (result->headerUrl.rfind("cache://", 0) == 0);

                        if (isManualCacheMarker) {
                            std::string actualFilename = result->headerUrl.substr(8);
                            imageCachePathToUse = GetCachePathForImage(appIdToLoad, actualFilename);
                        } else {
                            imageCachePathToUse = GetCachePathForImage(appIdToLoad, result->headerUrl);
                        }

                        if (LoadImageBytesFromCache(imageCachePathToUse, result->imageBytes)) {
                            Log("Simulate UP (Thread): Image bytes for " + gamePtr->name + " loaded from cache: " + imageCachePathToUse);
                        } else if (!isManualCacheMarker) {
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
                                   Log("Simulate UP (Thread): Image for " + gamePtr->name + " downloaded (" + std::to_string(result->imageBytes.size()) + " bytes).");
                                   SaveImageBytesToCache(imageCachePathToUse, result->imageBytes);
                                } else {
                                   Log("Simulate UP (Thread): Failed to download image bytes for " + gamePtr->name + ".");
                                }
                            } else {
                                Log("Simulate UP (Thread): Failed to crack image URL for " + gamePtr->name + ".");
                            }
                        }
                    }
                } else {
                    Log("Simulate UP (Thread): Game with AppID " + appIdToLoad + " not found in g_games.");
                    result->success = false;
                }
                PostMessage(hwndMain, WM_APP_GAME_DATA_READY, 0, (LPARAM)result);
            }).detach();
            break;
        }
        case ID_SIMULATE_DOWN:
        {
            Log("Simulate DOWN pressed.");
            std::unique_lock<std::mutex> overlayLock(g_overlayStateMutex);
            std::unique_lock<std::mutex> vrListLock(g_VROverlayGameListMutex);

            if (g_currentCategoryGameAppIds_VR.empty()) {
                Log("Simulate DOWN: VR category game list is empty.");
                g_isLoadingGameData = false;
                vrListLock.unlock();
                overlayLock.unlock();
                break;
            }

            if (g_selectedGameIndex < 0 || g_selectedGameIndex >= static_cast<int>(g_currentCategoryGameAppIds_VR.size()) - 1) {
                g_selectedGameIndex = 0;
            } else {
                g_selectedGameIndex++;
            }
            Log("Simulated selection index (for current VR list): " + std::to_string(g_selectedGameIndex));

            if (g_selectedGameIndex < 0 || static_cast<size_t>(g_selectedGameIndex) >= g_currentCategoryGameAppIds_VR.size()) {
                Log("Simulate DOWN: Invalid g_selectedGameIndex after operation: " + std::to_string(g_selectedGameIndex) + ". Resetting.");
                if (g_currentCategoryGameAppIds_VR.empty()) {
                    g_selectedGameIndex = -1;
                } else {
                    g_selectedGameIndex = 0;
                }
            }

            if (g_selectedGameIndex == -1) {
                g_isLoadingGameData = false;
                vrListLock.unlock();
                overlayLock.unlock();
                break;
            }

            std::string appIdToLoad = g_currentCategoryGameAppIds_VR[g_selectedGameIndex];
            vrListLock.unlock();

            g_isLoadingGameData = true;
            SAFE_RELEASE(g_pSelectedGameHeader);
            SAFE_RELEASE(g_pDescTextLayout);
            g_descScrollOffsetPx = 0;
            g_currentLayoutAppId = "";

            overlayLock.unlock();

            HWND hwndMain = hWnd;
            std::thread([appIdToLoad, hwndMain]() {
                Log("Simulate DOWN (Thread): Starting data fetch for AppID " + appIdToLoad);
                GameDataResult* result = new GameDataResult();
                result->appId = appIdToLoad;
                result->success = false;

                SteamGame* gamePtr = findGameByAppId(appIdToLoad);
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
                            std::string actualFilename = result->headerUrl.substr(8);
                            imageCachePathToUse = GetCachePathForImage(appIdToLoad, actualFilename);
                        } else {
                            imageCachePathToUse = GetCachePathForImage(appIdToLoad, result->headerUrl);
                        }

                        if (LoadImageBytesFromCache(imageCachePathToUse, result->imageBytes)) {
                            Log("Simulate DOWN (Thread): Image bytes for " + gamePtr->name + " loaded from cache: " + imageCachePathToUse);
                        } else if (!isManualCacheMarker) {
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
                                   Log("Simulate DOWN (Thread): Image for " + gamePtr->name + " downloaded (" + std::to_string(result->imageBytes.size()) + " bytes).");
                                   SaveImageBytesToCache(imageCachePathToUse, result->imageBytes);
                                } else {
                                   Log("Simulate DOWN (Thread): Failed to download image bytes for " + gamePtr->name + ".");
                                }
                            } else {
                                Log("Simulate DOWN (Thread): Failed to crack image URL for " + gamePtr->name + ".");
                            }
                        }
                    }
                } else {
                    Log("Simulate DOWN (Thread): Game with AppID " + appIdToLoad + " not found in g_games.");
                    result->success = false;
                }
                PostMessage(hwndMain, WM_APP_GAME_DATA_READY, 0, (LPARAM)result);
            }).detach();
            break;
        }
        case ID_SIMULATE_SELECT:
        {
            Log("Simulate SELECT pressed.");
            std::string gameAppIdToLaunch;
            bool gameFoundInVRList = false;

            // Scope for overlay state and VR list mutexes
            {
                std::lock_guard<std::mutex> overlayLock(g_overlayStateMutex); // For g_selectedGameIndex
                std::lock_guard<std::mutex> vrListLock(g_VROverlayGameListMutex); // For g_currentCategoryGameAppIds_VR

                if (g_selectedGameIndex >= 0 &&
                    static_cast<size_t>(g_selectedGameIndex) < g_currentCategoryGameAppIds_VR.size())
                {
                    gameAppIdToLaunch = g_currentCategoryGameAppIds_VR[g_selectedGameIndex];
                    gameFoundInVRList = true;
                } else {
                    Log("Simulate SELECT: No valid game selected in the VR overlay's current list (g_selectedGameIndex=" + std::to_string(g_selectedGameIndex) + ", VR list size=" + std::to_string(g_currentCategoryGameAppIds_VR.size()) + ").");
                }
            } // Mutexes g_overlayStateMutex and g_VROverlayGameListMutex are released here

            if (gameFoundInVRList && !gameAppIdToLaunch.empty()) {
                // findGameByAppId handles its own g_gamesMutex internally
                SteamGame* gameToLaunchPtr = findGameByAppId(gameAppIdToLaunch);

                if (gameToLaunchPtr) {
                    Log("Simulating launch for game: " + gameToLaunchPtr->name + " (AppID: " + gameAppIdToLaunch + ")");
                    // LaunchGame itself doesn't require external locks for ShellExecuteA
                    LaunchGame(*gameToLaunchPtr);
                } else {
                    Log("Simulate SELECT: Error - Game with AppID '" + gameAppIdToLaunch + "' (selected in VR list) not found in the master game list (g_games).");
                }
            } else if (!gameFoundInVRList) {
                // Log message already printed if selection was invalid
                MessageBoxW(hWnd, L"No game is currently selected in the simulated VR overlay list.", L"Simulate Launch Error", MB_OK | MB_ICONWARNING);
            } else { // gameAppIdToLaunch was empty but gameFoundInVRList was true (should not happen with current logic)
                Log("Simulate SELECT: Game AppID to launch is empty despite being found in VR list. This is unexpected.");
                MessageBoxW(hWnd, L"Internal error: Selected game AppID is empty.", L"Simulate Launch Error", MB_OK | MB_ICONERROR);
            }
            break;
        }
        case 13:
        {
            wchar_t ipBuf[256];
            GetWindowTextW(g_hEditIP, ipBuf, 256);
            std::wstring ws(ipBuf);
            g_masterIP = WStringToString(ws);
            SaveMasterIP(g_masterIP);

            wchar_t stBuf[256];
            GetWindowTextW(g_hEditStationName, stBuf, 256);
            std::wstring wst(stBuf);
            g_stationName = WStringToString(wst);
            SaveStationName(g_stationName);

            Log(std::string("User clicked Connect -> IP='") + g_masterIP + "', Station='" + g_stationName + "'");
            ConnectToMasterAsync(g_masterIP, 12345);
            break;
        }
        case ID_BUTTON_BROWSE: // Handle Browse button click
        {
            wchar_t szFile[MAX_PATH] = { 0 };
            OPENFILENAMEW ofn = { 0 };
            ofn.lStructSize = sizeof(ofn);
            ofn.hwndOwner = hWnd;
            ofn.lpstrFile = szFile;
            ofn.nMaxFile = sizeof(szFile) / sizeof(wchar_t);
            ofn.lpstrFilter = L"Image Files\0*.JPG;*.JPEG;*.PNG;*.BMP\0All Files\0*.*\0";
            ofn.nFilterIndex = 1;
            ofn.lpstrFileTitle = NULL;
            ofn.nMaxFileTitle = 0;
            ofn.lpstrInitialDir = NULL;
            ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_EXPLORER;

            if (GetOpenFileNameW(&ofn) == TRUE)
            {
                SetWindowTextW(g_hEditImagePath, ofn.lpstrFile); // Display selected path
                Log("User selected image file: " + WStringToString(ofn.lpstrFile));
            }
            break;
        }
        case ID_BUTTON_ADD_CUSTOM_GAME:
        {
            Log("WndProc: ID_BUTTON_ADD_CUSTOM_GAME clicked.");
            std::string new_appid = "custom_" + std::to_string(time(nullptr));
            
            auto newGame = std::make_unique<SteamGame>();
            newGame->isCustom = true;
            newGame->name = "[New Custom Game]";
            newGame->appid = new_appid;
            newGame->storeDataFetched = true; // No need to fetch from API

            {
                std::lock_guard<std::mutex> lock(g_gamesMutex);
                g_games.push_back(std::move(newGame));
            }

            SaveCustomGames();
            PopulateGamesListBox();

            // Find and select the new game in the listbox
            int count = (int)SendMessageA(g_hListGames, LB_GETCOUNT, 0, 0);
            for (int i = 0; i < count; i++) {
                int gameVecIdx = (int)SendMessageA(g_hListGames, LB_GETITEMDATA, i, 0);
                if (g_games[gameVecIdx]->appid == new_appid) {
                    SendMessageA(g_hListGames, LB_SETCURSEL, i, 0);
                    // Manually trigger a selection change message to update all controls
                    PostMessage(hWnd, WM_COMMAND, MAKEWPARAM(ID_GAMES_LIST, LBN_SELCHANGE), (LPARAM)g_hListGames);
                    break;
                }
            }
            break;
        }

        case ID_BUTTON_BROWSE_EXE:
        {
            wchar_t szFile[MAX_PATH] = { 0 };
            OPENFILENAMEW ofn = { 0 };
            ofn.lStructSize = sizeof(ofn);
            ofn.hwndOwner = hWnd;
            ofn.lpstrFile = szFile;
            ofn.nMaxFile = sizeof(szFile) / sizeof(wchar_t);
            ofn.lpstrFilter = L"Executables\0*.exe\0All Files\0*.*\0";
            ofn.nFilterIndex = 1;
            ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_EXPLORER;

            if (GetOpenFileNameW(&ofn) == TRUE) {
                SetWindowTextW(g_hEditExecutablePath, ofn.lpstrFile);
                Log("User selected executable file: " + WStringToString(ofn.lpstrFile));
            }
            break;
        }
        case ID_BUTTON_DELETE_CUSTOM_GAME:
        {
            int lbIndex = -1;
            int gameVectorIdx = GetSelectedGameListIndex(lbIndex);
            if (gameVectorIdx == -1) {
                break; // Should not happen as button should be disabled
            }

            std::string gameNameToDelete;
            std::string gameAppIdToDelete;
            bool is_custom = false;
            {
                std::lock_guard<std::mutex> lock(g_gamesMutex);
                if (static_cast<size_t>(gameVectorIdx) < g_games.size() && g_games[gameVectorIdx]) {
                    gameNameToDelete = g_games[gameVectorIdx]->name;
                    gameAppIdToDelete = g_games[gameVectorIdx]->appid;
                    is_custom = g_games[gameVectorIdx]->isCustom;
                }
            }

            if (!is_custom || gameAppIdToDelete.empty()) {
                break; // Safeguard
            }

            std::wstring confirmMsg = L"Are you sure you want to delete the custom game '" + StringToWString(gameNameToDelete) + L"'?\nThis cannot be undone.";
            if (MessageBoxW(hWnd, confirmMsg.c_str(), L"Confirm Delete", MB_YESNO | MB_ICONQUESTION) == IDYES) {
                Log("User confirmed deletion of custom game: " + gameNameToDelete + " (AppID: " + gameAppIdToDelete + ")");
                
                // Remove from g_games vector
                {
                    std::lock_guard<std::mutex> lock(g_gamesMutex);
                    auto it = std::remove_if(g_games.begin(), g_games.end(), [&](const std::unique_ptr<SteamGame>& game) {
                        return game && game->appid == gameAppIdToDelete;
                    });
                    g_games.erase(it, g_games.end());
                }

                // Save the updated custom games list
                SaveCustomGames();

                // Refresh the UI
                PopulateGamesListBox();
                
                Log("Custom game deleted and UI refreshed.");
            }
            break;
        }
        case ID_BUTTON_SAVE_CACHE: // Handle Save Game Details button click
        {
            int lbIndex = -1;
            int gameVectorIdx = GetSelectedGameListIndex(lbIndex);
            if (gameVectorIdx == -1) {
                MessageBoxW(hWnd, L"Please select a game from the list first.", L"Save Error", MB_OK | MB_ICONWARNING);
                break;
            }

            // --- Data Gathering (on UI thread) ---
            std::string appIdToSave;
            // --- ADD THIS ---
            std::string nameToSave;
            // --- END ADD ---
            std::string descToSave;
            std::wstring imagePathToProcessW; // Full path to original image OR empty/marker
            std::string currentHeaderSetting;
            bool game_is_custom = false; // <-- ADD
            std::string exe_path_to_save; // <-- ADD

            { // Scope for mutex
                std::lock_guard<std::mutex> lock(g_gamesMutex);
                if (gameVectorIdx >= g_games.size()) { // Double check index validity
                     Log("Save Cache Error: gameIndex out of bounds.");
                     break;
                }
                SteamGame& game = *g_games[gameVectorIdx];
                appIdToSave = game.appid; // Copy necessary data
                currentHeaderSetting = game.headerImage;
                game_is_custom = game.isCustom; // <-- ADD

                // Get description from UI
                wchar_t descBuf[1024];
                GetWindowTextW(g_hEditGameDesc, descBuf, 1024);
                descToSave = WStringToString(descBuf);

                // Get image path from UI
                wchar_t pathBuf[MAX_PATH];
                GetWindowTextW(g_hEditImagePath, pathBuf, MAX_PATH);
                imagePathToProcessW = pathBuf;

                // --- ADD THIS BLOCK to get the name from the UI ---
                if (game_is_custom) {
                    wchar_t nameBuf[256];
                    GetWindowTextW(g_hEditGameName, nameBuf, 256);
                    nameToSave = WStringToString(nameBuf);
                    if (nameToSave.empty()) {
                        MessageBoxW(hWnd, L"Custom game name cannot be empty.", L"Save Error", MB_OK | MB_ICONERROR);
                        break;
                    }
                }
                // --- END ADD ---

                // Get exe path if it's a custom game
                if (game_is_custom) {
                    wchar_t exePathBuf[MAX_PATH];
                    GetWindowTextW(g_hEditExecutablePath, exePathBuf, MAX_PATH);
                    exe_path_to_save = WStringToString(exePathBuf);
                }
            } // Mutex released

            // --- Launch Background Thread ---
            Log("Save Cache: Launching background thread for processing...");
            auto saveThread = std::thread([hWnd, appIdToSave, nameToSave, descToSave, imagePathToProcessW, currentHeaderSetting, game_is_custom, exe_path_to_save]() {
                bool imageProcessed = false;
                bool imageSaveSuccess = false;
                bool metadataSaveSuccess = false;
                std::string finalHeaderSetting = currentHeaderSetting; // Start with existing setting

                std::string imagePathA = WStringToString(imagePathToProcessW);
                // Check if a *new* file path was provided in the edit box
                bool isFilePath = !imagePathA.empty() &&
                                  (imagePathA.find('\\') != std::string::npos || imagePathA.find('/') != std::string::npos) &&
                                  imagePathA.rfind("cache://", 0) != 0 &&
                                  imagePathA.rfind("(", 0) != 0; // Exclude "(Using...)" strings

                if (isFilePath) {
                    Log("Save Cache Thread: Processing selected image file: " + imagePathA);
                    std::ifstream imgFile(imagePathToProcessW, std::ios::binary | std::ios::ate);
                    if (imgFile) {
                        std::streamsize size = imgFile.tellg();
                        if (size > 0) {
                            imgFile.seekg(0, std::ios::beg);
                            std::vector<uint8_t> imgBytes(size);
                            if (imgFile.read(reinterpret_cast<char*>(imgBytes.data()), size)) {
                                imageProcessed = true; // Mark that we tried to process an image
                                std::string cacheFilename = "header.jpg";
                                std::string imageCachePath = GetCachePathForImage(appIdToSave, cacheFilename);
                                if (SaveImageBytesToCache(imageCachePath, imgBytes)) {
                                    finalHeaderSetting = "cache://" + cacheFilename; // Update setting ONLY on successful save
                                    imageSaveSuccess = true;
                                    Log("Save Cache Thread: Image saved to cache: " + imageCachePath);
                                } else {
                                    Log("Save Cache Thread: Error saving image bytes to cache: " + imageCachePath);
                                }
                            } else { Log("Save Cache Thread: Error reading image file: " + imagePathA); }
                        } else { Log("Save Cache Thread: Selected image file is empty: " + imagePathA); }
                        imgFile.close();
                    } else { Log("Save Cache Thread: Error opening image file: " + imagePathA); }
                } else {
                    Log("Save Cache Thread: No new image file selected, using existing setting: " + finalHeaderSetting);
                     // If it wasn't a new file path, we don't need to process image bytes.
                     // We'll just save the description and whatever header setting was already there.
                     imageProcessed = false; // Didn't process a new image
                     imageSaveSuccess = true; // No image operation needed, so it didn't fail
                }

                // --- Save Metadata (always save description, save potentially updated header) ---
                // Construct a temporary SteamGame object for saving metadata
                SteamGame gameToSaveMeta;
                gameToSaveMeta.appid = appIdToSave;
                gameToSaveMeta.description = descToSave;
                gameToSaveMeta.headerImage = finalHeaderSetting; // Use the final determined setting

                if (SaveMetadataToCache(gameToSaveMeta)) {
                    metadataSaveSuccess = true;
                    Log("Save Cache Thread: Metadata cache saved successfully for appid " + appIdToSave);
                    // Update the actual game object in the main list ONLY AFTER successful metadata save
                     std::lock_guard<std::mutex> lock(g_gamesMutex);
                     for (auto& gamePtr : g_games) {
                         if (gamePtr->appid == appIdToSave) {
                             gamePtr->description = descToSave;
                             gamePtr->headerImage = finalHeaderSetting;
                             gamePtr->storeDataFetched = true; // Mark as fetched/updated
                             if (gamePtr->isCustom) {
                                 // --- ADD/MODIFY THIS ---
                                 gamePtr->name = nameToSave; // Update the name
                                 gamePtr->executablePath = exe_path_to_save;
                                 // --- END ADD/MODIFY ---
                             }
                             break;
                         }
                     }
                } else {
                    Log("Save Cache Thread: Error saving metadata cache for appid " + appIdToSave);
                }

                // If it's a custom game, we must also re-save the main custom games file
                if (game_is_custom && metadataSaveSuccess) {
                    SaveCustomGames();
                }

                // --- Post Result Back to Main Thread ---
                // We pass success/failure info via wParam
                // Bit 0: Image processed? (0=No, 1=Yes)
                // Bit 1: Image save successful? (0=Fail, 1=Success/NotNeeded)
                // Bit 2: Metadata save successful? (0=Fail, 1=Success)
                WPARAM saveResult = (imageProcessed ? 1 : 0) |
                                    (imageSaveSuccess ? 2 : 0) |
                                    (metadataSaveSuccess ? 4 : 0);
                PostMessage(hWnd, WM_APP_CACHE_SAVE_DONE, saveResult, (LPARAM)(new std::string(finalHeaderSetting))); // Pass final setting for UI update
            });
            saveThread.detach(); // Detach the thread to run independently

            // Optionally disable the save button here to prevent double-clicks
             EnableWindow(g_hButtonSaveCache, FALSE);
             SetWindowTextW(g_hEditImagePath, L"Saving cache..."); // Give visual feedback
             // --- ADD THIS ---
             SetWindowTextW(g_hEditGameName, L"Saving...");
             // --- END ADD ---

            break;
        }
        case ID_BUTTON_CREATE_CATEGORY:
        {
            Log("WndProc: ID_BUTTON_CREATE_CATEGORY clicked.");
            wchar_t categoryNameBuffer[256];
            GetWindowTextW(g_hEditCategoryName, categoryNameBuffer, 256);
            std::string newCategoryName = WStringToString(std::wstring(categoryNameBuffer));

            if (newCategoryName.empty()) {
                MessageBoxW(hWnd, L"Category name cannot be empty.", L"Create Category Error", MB_OK | MB_ICONWARNING);
                Log("Create Category: Name was empty.");
                break;
            }

            // Check for duplicate category name (case-insensitive)
            bool duplicateFound = false;
            {
                std::lock_guard<std::mutex> lock(g_categoriesMutex);
                std::string newNameLower = newCategoryName;
                std::transform(newNameLower.begin(), newNameLower.end(), newNameLower.begin(), ::tolower);
                for (const auto& category : g_categories) {
                    if (category) {
                        std::string existingNameLower = category->name;
                        std::transform(existingNameLower.begin(), existingNameLower.end(), existingNameLower.begin(), ::tolower);
                        if (existingNameLower == newNameLower) {
                            duplicateFound = true;
                            break;
                        }
                    }
                }
            }

            if (duplicateFound) {
                MessageBoxW(hWnd, L"A category with this name already exists.", L"Create Category Error", MB_OK | MB_ICONWARNING);
                Log("Create Category: Duplicate name found: " + newCategoryName);
                break;
            }

            Log("Creating new category: " + newCategoryName);
            int newId = 0;
            {
                std::lock_guard<std::mutex> lock(g_categoriesMutex);
                if (!g_categories.empty()) {
                    for(const auto& cat : g_categories) {
                        if (cat && cat->id >= newId) {
                            newId = cat->id + 1;
                        }
                    }
                }
                g_categories.push_back(std::make_unique<GameCategory>(newCategoryName, newId));
                Log("New category '" + newCategoryName + "' (ID: " + std::to_string(newId) + ") added to vector.");
            }

            SaveCategories();
            PopulateCategoriesListBox(hWnd);
            SetWindowTextW(g_hEditCategoryName, L"");
            UpdateCategoryControlsState(hWnd);
            Log("Category created and UI updated.");
            break;
        }
        case ID_BUTTON_ADD_GAME_TO_CAT:
        {
            Log("WndProc: ID_BUTTON_ADD_GAME_TO_CAT clicked.");
            int lbGameIdx = -1;
            int gameVecIdx = GetSelectedGameListIndex(lbGameIdx);
            int lbCatIdx = (int)SendMessageW(g_hListCategories, LB_GETCURSEL, 0, 0);

            if (gameVecIdx == -1) {
                MessageBoxW(hWnd, L"Please select a game from the game list.", L"Add to Category Error", MB_OK | MB_ICONWARNING);
                break;
            }
            if (lbCatIdx == LB_ERR) {
                MessageBoxW(hWnd, L"Please select a category from the categories list.", L"Add to Category Error", MB_OK | MB_ICONWARNING);
                break;
            }
            LPARAM catItemData = SendMessageW(g_hListCategories, LB_GETITEMDATA, lbCatIdx, 0);
            int catId = (int)catItemData;

            std::string gameAppIdToAdd;
            { // Scope for g_gamesMutex
                std::lock_guard<std::mutex> games_lock(g_gamesMutex);
                if (static_cast<size_t>(gameVecIdx) >= g_games.size() || !g_games[gameVecIdx]) {
                    Log("Add to Category: Invalid game index or null game pointer.");
                    MessageBoxW(hWnd, L"Error accessing game data.", L"Add to Category Error", MB_OK | MB_ICONERROR);
                    break;
                }
                gameAppIdToAdd = g_games[gameVecIdx]->appid;
            }

            bool changed = false;
            { // Scope for g_categoriesMutex
                std::lock_guard<std::mutex> cat_lock(g_categoriesMutex);
                auto it = std::find_if(g_categories.begin(), g_categories.end(), [catId](const std::unique_ptr<GameCategory>& u) { return u && u->id == catId; });
                if (it == g_categories.end() || !*it) {
                    Log("Add to Category: Invalid category id or null category pointer.");
                    MessageBoxW(hWnd, L"Error accessing category data.", L"Add to Category Error", MB_OK | MB_ICONERROR);
                    break;
                }
                GameCategory* category = it->get();
                // Check if game already in category
                if (std::find(category->gameAppIds.begin(), category->gameAppIds.end(), gameAppIdToAdd) == category->gameAppIds.end()) {
                    category->gameAppIds.push_back(gameAppIdToAdd);
                    std::sort(category->gameAppIds.begin(), category->gameAppIds.end());
                    changed = true;
                    Log("Added game " + gameAppIdToAdd + " to category " + category->name);
                } else {
                    MessageBoxW(hWnd, L"Game is already in this category.", L"Add to Category", MB_OK | MB_ICONINFORMATION);
                }
            }

            if (changed) {
                SaveCategories();
                UpdateGameMembershipList(hWnd, gameVecIdx);
                UpdateCategoryControlsState(hWnd);
                PopulateGamesInCategoryList(catId); // <-- Add this line
            }
            break;
        }
        case ID_BUTTON_REMOVE_GAME_FROM_CAT:
        {
            Log("WndProc: ID_BUTTON_REMOVE_GAME_FROM_CAT clicked.");
            int lbGameIdx = -1;
            int gameVecIdx = GetSelectedGameListIndex(lbGameIdx);
            int lbCatIdx = (int)SendMessageW(g_hListCategories, LB_GETCURSEL, 0, 0);

            if (gameVecIdx == -1) {
                MessageBoxW(hWnd, L"Please select a game from the game list.", L"Remove from Category Error", MB_OK | MB_ICONWARNING);
                break;
            }
            if (lbCatIdx == LB_ERR) {
                MessageBoxW(hWnd, L"Please select a category from which to remove the game.", L"Remove from Category Error", MB_OK | MB_ICONWARNING);
                break;
            }
            LPARAM catItemData = SendMessageW(g_hListCategories, LB_GETITEMDATA, lbCatIdx, 0);
            int catId = (int)catItemData;

            std::string gameAppIdToRemove;
            { // Scope for g_gamesMutex
                std::lock_guard<std::mutex> games_lock(g_gamesMutex);
                if (static_cast<size_t>(gameVecIdx) >= g_games.size() || !g_games[gameVecIdx]) {
                    Log("Remove from Category: Invalid game index or null game pointer.");
                    MessageBoxW(hWnd, L"Error accessing game data.", L"Remove from Category Error", MB_OK | MB_ICONERROR);
                    break;
                }
                gameAppIdToRemove = g_games[gameVecIdx]->appid;
            }

            bool changed = false;
            { // Scope for g_categoriesMutex
                std::lock_guard<std::mutex> cat_lock(g_categoriesMutex);
                auto it = std::find_if(g_categories.begin(), g_categories.end(), [catId](const std::unique_ptr<GameCategory>& u) { return u && u->id == catId; });
                if (it == g_categories.end() || !*it) {
                    Log("Remove from Category: Invalid category id or null category pointer.");
                    MessageBoxW(hWnd, L"Error accessing category data.", L"Remove from Category Error", MB_OK | MB_ICONERROR);
                    break;
                }
                GameCategory* category = it->get();
                auto& appIds = category->gameAppIds;
                auto itApp = std::find(appIds.begin(), appIds.end(), gameAppIdToRemove);
                if (itApp != appIds.end()) {
                    appIds.erase(itApp);
                    changed = true;
                    Log("Removed game " + gameAppIdToRemove + " from category " + category->name);
                } else {
                    MessageBoxW(hWnd, L"Game is not in this category.", L"Remove from Category", MB_OK | MB_ICONINFORMATION);
                }
            }

            if (changed) {
                SaveCategories();
                UpdateGameMembershipList(hWnd, gameVecIdx);
                UpdateCategoryControlsState(hWnd);
                PopulateGamesInCategoryList(catId); // <-- Add this line
            }
            break;
        }
        case ID_BUTTON_DELETE_CATEGORY:
        {
            Log("WndProc: ID_BUTTON_DELETE_CATEGORY clicked.");
            int lbCatIdx = (int)SendMessageW(g_hListCategories, LB_GETCURSEL, 0, 0);
            if (lbCatIdx == LB_ERR) {
                MessageBoxW(hWnd, L"Please select a category to delete.", L"Delete Category", MB_OK | MB_ICONWARNING);
                break;
            }

            LPARAM catItemData = SendMessageW(g_hListCategories, LB_GETITEMDATA, lbCatIdx, 0);
            int catIdToDelete = (int)catItemData;

            std::string categoryNameToDelete;
            {
                std::lock_guard<std::mutex> lock(g_categoriesMutex);
                auto it = std::find_if(g_categories.begin(), g_categories.end(), [catIdToDelete](const std::unique_ptr<GameCategory>& u) { return u && u->id == catIdToDelete; });
                if (it != g_categories.end() && (*it)) {
                    categoryNameToDelete = (*it)->name;
                }
            }

            if (categoryNameToDelete.empty()) {
                MessageBoxW(hWnd, L"Could not find the selected category data.", L"Error", MB_OK | MB_ICONERROR);
                break;
            }

            std::wstring confirmMsg = L"Are you sure you want to delete the category '" + StringToWString(categoryNameToDelete) + L"'?\nThis cannot be undone.";
            if (MessageBoxW(hWnd, confirmMsg.c_str(), L"Confirm Deletion", MB_YESNO | MB_ICONQUESTION) == IDYES) {
                Log("User confirmed deletion of category: " + categoryNameToDelete);
                bool changed = false;
                {
                    std::lock_guard<std::mutex> lock(g_categoriesMutex);
                    auto it = std::remove_if(g_categories.begin(), g_categories.end(), [catIdToDelete](const std::unique_ptr<GameCategory>& u) { return u && u->id == catIdToDelete; });
                    if (it != g_categories.end()) {
                        g_categories.erase(it, g_categories.end());
                        changed = true;
                    }
                }

                if (changed) {
                    SaveCategories();
                    PopulateCategoriesListBox(hWnd);
                    UpdateCategoryControlsState(hWnd);
                }
            }
            break;
        }

        // --- ADD THESE NEW CASES ---
        case ID_BUTTON_MOVE_CAT_UP:
        case ID_BUTTON_MOVE_CAT_DOWN:
        {
            int lbCatIdx = (int)SendMessageW(g_hListCategories, LB_GETCURSEL, 0, 0);
            if (lbCatIdx == LB_ERR) {
                break; // Should be disabled, but check anyway
            }

            LPARAM catItemData = SendMessageW(g_hListCategories, LB_GETITEMDATA, lbCatIdx, 0);
            int catIdToMove = (int)catItemData;

            int catVecIdx = -1;
            {
                std::lock_guard<std::mutex> lock(g_categoriesMutex);
                for (int i = 0; i < g_categories.size(); ++i) {
                    if (g_categories[i] && g_categories[i]->id == catIdToMove) {
                        catVecIdx = i;
                        break;
                    }
                }

                if (catVecIdx != -1) {
                    if (wmId == ID_BUTTON_MOVE_CAT_UP && catVecIdx > 0) {
                        std::swap(g_categories[catVecIdx], g_categories[catVecIdx - 1]);
                    } else if (wmId == ID_BUTTON_MOVE_CAT_DOWN && catVecIdx < g_categories.size() - 1) {
                        std::swap(g_categories[catVecIdx], g_categories[catVecIdx + 1]);
                    }
                }
            }

            SaveCategories();
            PopulateCategoriesListBox(hWnd);

            // Re-select the moved item
            int newLbIdx = -1;
            int count = (int)SendMessageW(g_hListCategories, LB_GETCOUNT, 0, 0);
            for (int i = 0; i < count; i++) {
                if ((int)SendMessageW(g_hListCategories, LB_GETITEMDATA, i, 0) == catIdToMove) {
                    newLbIdx = i;
                    break;
                }
            }
            if (newLbIdx != -1) {
                SendMessageW(g_hListCategories, LB_SETCURSEL, newLbIdx, 0);
            }

            UpdateCategoryControlsState(hWnd);
            break;
        }
        // --- ADD THIS NEW CASE ---
        case ID_BUTTON_EXPORT_HTML:
        {
            ExportCategorizedGamesToHTML(hWnd);
            break;
        }
        // --- END OF NEW CASES ---

        case ID_SIMULATE_CAT_UP:
        {
            Log("Simulate Category UP pressed.");
            std::unique_lock<std::mutex> cat_lock(g_categoriesMutex);
            if (g_categories.empty()) {
                Log("Simulate Category UP: No categories available.");
                cat_lock.unlock();
                break;
            }

            if (g_selectedCategoryIndexVR <= 0) {
                g_selectedCategoryIndexVR = static_cast<int>(g_categories.size()) - 1;
            } else {
                g_selectedCategoryIndexVR--;
            }
            Log("Simulated category selection index: " + std::to_string(g_selectedCategoryIndexVR) + " (" + (g_selectedCategoryIndexVR >= 0 && static_cast<size_t>(g_selectedCategoryIndexVR) < g_categories.size() ? g_categories[g_selectedCategoryIndexVR]->name : "INVALID") + ")");
            cat_lock.unlock(); // Unlock before calling other functions that might lock

            UpdateCurrentCategoryGameList_VR(); // This updates g_currentCategoryGameAppIds_VR and resets g_selectedGameIndex

            // Now, auto-select the first game in the new category for the VR overlay
            std::string appIdForInitialVRLoad = "";
            HWND hwndMainForSim = hWnd; 

            { // Scope for VR list and overlay state mutexes
                std::lock_guard<std::mutex> vrListLock(g_VROverlayGameListMutex);
                std::lock_guard<std::mutex> overlayLock(g_overlayStateMutex);

                if (!g_currentCategoryGameAppIds_VR.empty()) {
                    g_selectedGameIndex = 0; // Select first game in the new category
                    appIdForInitialVRLoad = g_currentCategoryGameAppIds_VR[0];
                    
                    g_isLoadingGameData = true;
                    SAFE_RELEASE(g_pSelectedGameHeader);
                    SAFE_RELEASE(g_pDescTextLayout);
                    g_descScrollOffsetPx = 0;
                    g_currentLayoutAppId = "";
                    Log("Simulate Category UP: Auto-selecting first game for VR overlay: AppID " + appIdForInitialVRLoad);
                } else {
                    g_selectedGameIndex = -1; 
                    g_isLoadingGameData = false;
                    // Clear existing game details if category is empty
                    SAFE_RELEASE(g_pSelectedGameHeader);
                    SAFE_RELEASE(g_pDescTextLayout);
                    g_descScrollOffsetPx = 0;
                    g_currentLayoutAppId = "";
                    Log("Simulate Category UP: New category is empty. Clearing VR game details.");
                }
            } // Mutexes released

            if (!appIdForInitialVRLoad.empty() && hwndMainForSim) {
                std::thread([appId = appIdForInitialVRLoad, hwndMain = hwndMainForSim]() {
                    Log("Simulate Category UP (Thread): Starting data fetch for AppID " + appId);
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
                                Log("Simulate Category UP (Thread): Image bytes for " + gamePtr->name + " loaded from cache.");
                            } else if (!isManualCacheMarker) {
                                Log("Simulate Category UP (Thread): Image for " + gamePtr->name + " not in cache, download from: " + result->headerUrl);
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
                                       Log("Simulate Category UP (Thread): Image for " + gamePtr->name + " downloaded.");
                                       SaveImageBytesToCache(imageCachePathToUse, result->imageBytes);
                                    } else {
                                       Log("Simulate Category UP (Thread): Failed to download image for " + gamePtr->name + ".");
                                    }
                                } else {
                                    Log("Simulate Category UP (Thread): Failed to crack image URL for " + gamePtr->name + ".");
                                }
                            }
                        }
                    } else {
                        Log("Simulate Category UP (Thread): Game with AppID " + appId + " not found.");
                        result->success = false;
                    }
                    PostMessage(hwndMain, WM_APP_GAME_DATA_READY, 0, (LPARAM)result);
                }).detach();
            }
            break;
        }
        case ID_SIMULATE_CAT_DOWN:
        {
            Log("Simulate Category DOWN pressed.");
            std::unique_lock<std::mutex> cat_lock(g_categoriesMutex);
            if (g_categories.empty()) {
                Log("Simulate Category DOWN: No categories available.");
                cat_lock.unlock();
                break;
            }

            if (g_selectedCategoryIndexVR < 0 || g_selectedCategoryIndexVR >= static_cast<int>(g_categories.size()) - 1) {
                g_selectedCategoryIndexVR = 0;
            } else {
                g_selectedCategoryIndexVR++;
            }
            Log("Simulated category selection index: " + std::to_string(g_selectedCategoryIndexVR) + " (" + (g_selectedCategoryIndexVR >= 0 && static_cast<size_t>(g_selectedCategoryIndexVR) < g_categories.size() ? g_categories[g_selectedCategoryIndexVR]->name : "INVALID") + ")");
            cat_lock.unlock(); 

            UpdateCurrentCategoryGameList_VR(); 

            std::string appIdForInitialVRLoad = "";
            HWND hwndMainForSim = hWnd;

            { 
                std::lock_guard<std::mutex> vrListLock(g_VROverlayGameListMutex);
                std::lock_guard<std::mutex> overlayLock(g_overlayStateMutex);

                if (!g_currentCategoryGameAppIds_VR.empty()) {
                    g_selectedGameIndex = 0; 
                    appIdForInitialVRLoad = g_currentCategoryGameAppIds_VR[0];
                    
                    g_isLoadingGameData = true;
                    SAFE_RELEASE(g_pSelectedGameHeader);
                    SAFE_RELEASE(g_pDescTextLayout);
                    g_descScrollOffsetPx = 0;
                    g_currentLayoutAppId = "";
                    Log("Simulate Category DOWN: Auto-selecting first game for VR overlay: AppID " + appIdForInitialVRLoad);
                } else {
                    g_selectedGameIndex = -1; 
                    g_isLoadingGameData = false;
                    SAFE_RELEASE(g_pSelectedGameHeader);
                    SAFE_RELEASE(g_pDescTextLayout);
                    g_descScrollOffsetPx = 0;
                    g_currentLayoutAppId = "";
                    Log("Simulate Category DOWN: New category is empty. Clearing VR game details.");
                }
            } 

            if (!appIdForInitialVRLoad.empty() && hwndMainForSim) {
                std::thread([appId = appIdForInitialVRLoad, hwndMain = hwndMainForSim]() {
                    Log("Simulate Category DOWN (Thread): Starting data fetch for AppID " + appId);
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
                                Log("Simulate Category DOWN (Thread): Image bytes for " + gamePtr->name + " loaded from cache.");
                            } else if (!isManualCacheMarker) {
                                Log("Simulate Category DOWN (Thread): Image for " + gamePtr->name + " not in cache, download from: " + result->headerUrl);
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
                                       Log("Simulate Category DOWN (Thread): Image for " + gamePtr->name + " downloaded.");
                                       SaveImageBytesToCache(imageCachePathToUse, result->imageBytes);
                                    } else {
                                       Log("Simulate Category DOWN (Thread): Failed to download image for " + gamePtr->name + ".");
                                    }
                                } else {
                                    Log("Simulate Category DOWN (Thread): Failed to crack image URL for " + gamePtr->name + ".");
                                }
                            }
                        }
                    } else {
                        Log("Simulate Category DOWN (Thread): Game with AppID " + appId + " not found.");
                        result->success = false;
                    }
                    PostMessage(hwndMain, WM_APP_GAME_DATA_READY, 0, (LPARAM)result);
                }).detach();
            }
            break;
        }
        }
        break;
    }
    case WM_TIMER:
    {
        if (wParam == SESSION_TIMER_ID)
        {
            if (g_SessionRunning)
            {
                time_t now = time(nullptr);
                if (g_sessionEndTime <= now)
                {
                    StopSessionTimer(hWnd);
                    QuitVRApp();
                    ShowOverlayContinuous(); // Show the waiting overlay after session ends

                    if (g_connected)
                    {
                        SendToMaster("SESSION_STOPPED");
                    }
                }
                else
                {
                    g_SessionTimeSeconds = (int)(g_sessionEndTime - now);
                    UpdateTimeLeftDisplay();

                    if (g_connected)
                    {
                        // Send time left update less frequently to avoid flooding master?
                        // static int updateCounter = 0;
                        // if (++updateCounter % 5 == 0) { // Send every 5 seconds
                            char buf[64];
                            sprintf_s(buf, "TIME_LEFT %d", g_SessionTimeSeconds);
                            SendToMaster(buf);
                        // }
                    }

                    // Update overlay with time left - This is now handled by OVERLAY_TIMER_ID timer
                    // The OVERLAY_TIMER_ID will call RefreshOverlayTexture which reads g_SessionTimeSeconds
                }
            }
        }
        else if (wParam == OVERLAY_TIMER_ID)
        {
            // Log timestamp to track timer firing frequency (Optional: Keep or remove)
            // static DWORD lastOverlayTimerCheck = 0;
            // DWORD now = GetTickCount();
            // if (lastOverlayTimerCheck != 0)
            // {
            //     DWORD interval = now - lastOverlayTimerCheck;
            //     if (interval < (OVERLAY_REFRESH_INTERVAL - 5) || interval > (OVERLAY_REFRESH_INTERVAL + 5)) { // Adjust tolerance based on interval
            //         Log("Overlay timer (WM_TIMER) interval: " + std::to_string(interval) + "ms");
            //     }
            // }
            // lastOverlayTimerCheck = now;

            // Poll for input events *before* drawing the next frame
             if (g_MainOverlay != 0)
             {
                 UpdateContinuousScroll();
                 PollOverlayEvents();
                 RefreshOverlayTexture(g_MainOverlay);
             }

             // Add check to ensure our overlay stays front and center (Optional: Keep or remove)
             // This might interfere with game overlays, use with caution or only when needed
             /*if (continuousOverlayRunning && !g_SessionRunning && g_MainOverlay != vr::k_ulOverlayHandleInvalid) {
                 vr::IVROverlay *pOverlay = vr::VROverlay();
                 if (pOverlay && !pOverlay->IsActiveDashboardOverlay(g_MainOverlay)) {
                     Log("Overlay lost active status, calling ShowDashboard again.");
                     pOverlay->ShowDashboard("arcade.station.overlay");
                 }
             }*/
        }
        else if (wParam == STEAMVR_CHECK_TIMER_ID) // Temporary check timer
        {
            std::thread tempCheckThread([hWnd](){
                if (IsSteamVRRunning())
                {
                    Log("Temporary SteamVR check: Detected running, killing timer.");
                    PostMessage(hWnd, WM_APP + 2, 0, 0); // Post message to kill this timer

                    bool shouldShowOverlay = false;
                    if (g_SessionRunning) {
                        Log("Temporary SteamVR check: Session is active. Triggering overlay show.");
                        shouldShowOverlay = true;
                    } else if (g_AutoEnableOverlay) { // g_SessionRunning is false here
                        Log("Temporary SteamVR check: AutoEnableOverlay is ON (no session). Triggering overlay show.");
                        shouldShowOverlay = true;
                    } else if (continuousOverlayRunning) { // g_SessionRunning is false here
                        // This case implies the overlay was manually shown, SteamVR restarted, and we want it back.
                        Log("Temporary SteamVR check: Overlay was manually started (no session). Triggering overlay show.");
                        shouldShowOverlay = true;
                    }

                    if (shouldShowOverlay) {
                        Log("Temporary SteamVR check: Posting WM_COMMAND, 3 to show overlay.");
                        PostMessageW(hWnd, WM_COMMAND, 3, 0); // 3 is ID for ShowOverlayContinuous
                    }
                }
                else
                {
                    Log("Temporary SteamVR check: Still waiting...");
                }
            });
            tempCheckThread.detach();
        }
        else if (wParam == 4) // Reconnection timer for master server
        {
             if (!g_connected)
             {
                 Log("Reconnection timer: Attempting to reconnect to master...");
                 ConnectToMasterAsync(g_masterIP, 12345);
             }

            // Also handle the overlay restoration here if needed (e.g., if connection loss hid it)
             std::thread overlayCheckThread([hWnd]() {
                if (!continuousOverlayRunning && !g_SessionRunning)
                {
                    if (IsSteamVRRunning() && g_AutoEnableOverlay)
                    {
                        Log("Reconnection timer: Overlay not running, attempting to restore it via PostMessage");
                        PostMessageW(hWnd, WM_COMMAND, 3, 0); // Post message to show overlay
                    }
                }
             });
             overlayCheckThread.detach();
        }
        else if (wParam == STEAMVR_PERSISTENT_CHECK_TIMER_ID)
        {
            std::thread steamVRStateUpdateThread([hWnd]() {
                bool steamVRIsCurrentlyRunning = IsSteamVRRunning();
                bool dashboardIsCurrentlyVisible = false;

                if (steamVRIsCurrentlyRunning) {
                    // Ensure OpenVR is initialized to be able to call VROverlay functions
                    if (InitializeOpenVRForOverlay() && vr::VROverlay()) {
                        dashboardIsCurrentlyVisible = vr::VROverlay()->IsDashboardVisible();
                    }

                    // Check this *before* other logic that might reset flags.
                    if (g_awaitingDashboardHide) {
                        if (!dashboardIsCurrentlyVisible) {
                            // Success! Dashboard closed.
                            Log("Persistent Check: Dashboard successfully hidden after game launch.");
                            g_awaitingDashboardHide = false;
                            g_gameLaunchTime = 0;
                        } else { // Dashboard is still visible
                            // Check for timeout.
                            if (g_gameLaunchTime != 0 && (GetTickCount() - g_gameLaunchTime > 5000)) { // 5 second timeout
                                Log("Persistent Check: Dashboard did not hide automatically after 5s. Re-issuing HideOverlay command as a fallback.");
                                if (vr::VROverlay()) { // Check only for VROverlay system
                                    if (g_MainOverlay != 0) { // Check for our specific overlay handles
                                        vr::VROverlay()->HideOverlay(g_MainOverlay);
                                    }
                                    if (g_ThumbnailOverlay != 0) {
                                        vr::VROverlay()->HideOverlay(g_ThumbnailOverlay);
                                    }
                                }
                                // Stop trying after this one attempt.
                                g_awaitingDashboardHide = false;
                                g_gameLaunchTime = 0;
                            }
                        }
                    }

                    if (!g_steamVRWasRunning) {
                        // SteamVR just started
                        Log("Persistent Check: SteamVR has started. Initiating 3-second delay for overlay.");
                        g_steamVRWasRunning = true;
                        g_steamVRDetectedTime = GetTickCount(); // Start 3s timer for SteamVR settling
                        // ***** NEW CODE START *****
                        if (!g_initialDashboardToggleDone) { // Only set this if we haven't done the dashboard toggle yet
                            g_steamVRStartTimeForDashboardToggle = GetTickCount(); // For new 5s dashboard toggle logic
                            Log("Persistent Check: Initialized 5s timer for dashboard toggle.");
                        }
                        // ***** NEW CODE END *****
                        // Don't attempt to show/hide overlay yet based on dashboard; wait for 3s.
                    } else {
                        // SteamVR was already running
                        // ***** NEW CODE BLOCK START for 5-second dashboard toggle *****
                        if (!g_initialDashboardToggleDone && g_steamVRStartTimeForDashboardToggle != 0 && (GetTickCount() - g_steamVRStartTimeForDashboardToggle >= 5000))
                        {
                            Log("Persistent Check: 5-second delay for dashboard toggle complete. Attempting to toggle dashboard ONCE.");
                            if (InitializeOpenVRForOverlay() && vr::VROverlay()) {
                                vr::VROverlay()->ShowDashboard(nullptr); // Show the main SteamVR dashboard
                                Log("Persistent Check: vr::VROverlay()->ShowDashboard(nullptr) called.");
                            } else {
                                Log("Persistent Check: Could not toggle dashboard - OpenVR/VROverlay not ready at the moment of 5s check.");
                            }
                            g_initialDashboardToggleDone = true; // Mark as done for this app session
                            // g_steamVRStartTimeForDashboardToggle = 0; // Resetting here means it would re-trigger if g_initialDashboardToggleDone was ever reset.
                                                                // Keeping it non-zero but relying on g_initialDashboardToggleDone ensures one-time action.
                                                                // It will be reset to 0 if SteamVR stops.
                            Log("Persistent Check: Initial dashboard toggle action complete. Flag set to true.");
                        }
                        // ***** NEW CODE BLOCK END *****
                        bool canAttemptShowOverlayActions = false;
                        if (g_steamVRDetectedTime != 0) { // Check if we are in the 3-second delay period
                            if (GetTickCount() - g_steamVRDetectedTime >= 3000) { // 3-second delay passed
                                Log("Persistent Check: 3-second delay after SteamVR start complete.");
                                g_steamVRDetectedTime = 0; // Clear the detection time, delay is over
                                canAttemptShowOverlayActions = true;
                            } else {
                                // Still waiting for the 3-second delay
                                // Log("Persistent Check: Waiting for 3-second delay to complete..."); // Optional: for debugging delay
                            }
                        } else { // Not in the 3-second delay period (it either passed, or SteamVR was running long before)
                            canAttemptShowOverlayActions = true;
                        }

                        if (canAttemptShowOverlayActions) {
                            if (g_bOverlayShutdownRequested && !g_awaitingDashboardHide) {
                                if (!dashboardIsCurrentlyVisible) {
                                    // User has successfully closed the dashboard after our request.
                                    // Keep the flag true until they manually open it again.
                                } else {
                                    // The dashboard is visible again, which means the user likely opened it.
                                    // We can now reset our flag and resume normal persistent behavior.
                                    Log("Persistent Check: Dashboard is visible again, resetting shutdown request flag.");
                                    g_bOverlayShutdownRequested = false;
                                }
                            }

                            if (dashboardIsCurrentlyVisible && !g_bOverlayShutdownRequested) {
                                // Conditions met: SteamVR running, 3s delay (if any) passed, dashboard open,
                                // AND we haven't requested a shutdown.
                                // Log("Persistent Check: Conditions met. Ensuring overlay is visible."); // Optional: for debugging
                                PostMessageW(hWnd, WM_APP_ENSURE_OVERLAY_VISIBLE, 0, 0);
                            } else if (!dashboardIsCurrentlyVisible) {
                                // Dashboard is closed (or not visible). We need to PAUSE rendering, not destroy.
                                if (continuousOverlayRunning) {
                                     // Only log if we weren't the ones who requested the shutdown
                                    if (!g_bOverlayShutdownRequested) {
                                        Log("Persistent Check: SteamVR running, dashboard closed/not visible. Triggering pause rendering.");
                                    }
                                    PostMessageW(hWnd, WM_APP_PAUSE_OVERLAY_RENDERING, 0, 0);
                                }
                            }
                        }
                    }
                } else {
                    // SteamVR is NOT running
                    if (g_steamVRWasRunning) { // SteamVR just stopped
                        Log("Persistent Check: SteamVR has stopped.");
                        g_steamVRWasRunning = false;
                        g_steamVRDetectedTime = 0; // Reset 3s timer state, as SteamVR is no longer starting
                        // ***** NEW CODE START *****
                        g_steamVRStartTimeForDashboardToggle = 0; // Reset the 5s dashboard toggle timer state
                        Log("Persistent Check: Reset 5s dashboard toggle timer timestamp as SteamVR stopped.");
                        // Note: g_initialDashboardToggleDone remains true.
                        // ***** NEW CODE END *****
                        if (continuousOverlayRunning) { // If overlay was running, hide it
                            PostMessageW(hWnd, WM_COMMAND, 4, 0); // ID 4 is HideOverlayContinuous
                        }
                    }
                    // else: SteamVR was not running and is still not running.

                    // Attempt to relaunch SteamVR if it's desired (e.g., AutoEnableOverlay or active session)
                    bool shouldLaunchSteamVR = (g_AutoEnableOverlay && !g_SessionRunning) || g_SessionRunning;
                    if (shouldLaunchSteamVR) {
                        Log("Persistent Check: SteamVR not running but is desired. Requesting launch.");
                        PostMessageW(hWnd, WM_APP + 1, 0, 0); // Request SteamVR launch (WM_APP + 1 handler calls LaunchSteamVR)
                    }
                }
            });
            steamVRStateUpdateThread.detach();
            break;
        }
        break;
    }
    case WM_SIZE:
    {
        // int width = LOWORD(lParam);
        // int height = HIWORD(lParam);
        // You might resize/reposition controls here if needed, but it's a fixed layout now.
        break;
    }
    case WM_CLOSE:
    {
        // Explicit disconnect from master when window is closing
        if (g_connected) {
            Log("Window closing, disconnecting from master...");
            DisconnectFromMaster();
        }

        DestroyWindow(hWnd);
        break;
    }
    case WM_DESTROY:
    {
        // Kill all timers
        KillTimer(hWnd, OVERLAY_TIMER_ID);
        KillTimer(hWnd, SESSION_TIMER_ID);
        KillTimer(hWnd, STEAMVR_CHECK_TIMER_ID);
        KillTimer(hWnd, 4); // Kill reconnection timer
        KillTimer(hWnd, STEAMVR_PERSISTENT_CHECK_TIMER_ID); // Kill persistent check timer

        // Reset overlay state
        if (continuousOverlayRunning)
        {
            HideOverlayContinuous();
        }

        // Make sure to clean up all threads and connections
        if (g_runRecvThread)
        {
            g_runRecvThread = false;
            if (g_recvThread.joinable())
                g_recvThread.join();
        }

        if (g_clientSocket != INVALID_SOCKET)
        {
             shutdown(g_clientSocket, SD_BOTH); // Ensure proper socket shutdown
             closesocket(g_clientSocket);
             g_clientSocket = INVALID_SOCKET;
        }

        // Make sure we exit completely
        Log("WM_DESTROY received, posting quit message...");
        PostQuitMessage(0);
        break;
    }
    case WM_APP + 1: // Custom message to launch SteamVR
    {
        Log("Received custom message WM_APP + 1: Launching SteamVR.");
        LaunchSteamVR();
        return 0; // Indicate message processed
    }
    case WM_APP_GAME_DATA_READY:
    {
        GameDataResult* result = reinterpret_cast<GameDataResult*>(lParam); // Cast lParam
        if (!result) {
            Log("Error: Received null GameDataResult pointer in WM_APP_GAME_DATA_READY.");
            return 0;
        }
        Log("WM_APP_GAME_DATA_READY: Received data for app ID " + result->appId);

        SteamGame* gameToUpdate = nullptr;
        // int gameIndexInMainList = -1; // This variable isn't strictly needed for the VR overlay update

        std::unique_lock<std::mutex> gamesLock(g_gamesMutex); // Use unique_lock for early unlock
        for (size_t i = 0; i < g_games.size(); ++i) {
            if (g_games[i] && g_games[i]->appid == result->appId) {
                gameToUpdate = g_games[i].get();
                // gameIndexInMainList = static_cast<int>(i); 
                break;
            }
        }
        gamesLock.unlock(); // Unlock g_gamesMutex as soon as gameToUpdate is found (or not)

        if (gameToUpdate) {
            Log("Processing UI updates for game: " + gameToUpdate->name + " (AppID: " + result->appId + ")");

            // Update the game object's data (under its own mutex if FetchStoreDataForGame didn't already do it fully)
            // FetchStoreDataForGame was called in the thread, so gameToUpdate should have fresh data.
            // We might just need to save to cache here if it was a successful API fetch.
            std::lock_guard<std::mutex> gameDataUpdateLock(gameToUpdate->dataMutex);
            gameToUpdate->description = result->description; // Update from what the thread determined
            gameToUpdate->headerImage = result->headerUrl;   // Update from what the thread determined
            gameToUpdate->storeDataFetched = result->success; // Reflect the success of the fetch operation

            // Save metadata to cache if the fetch was successful and resulted in new data
            if (result->success) { // Only save if the entire operation in the thread was successful
                SaveMetadataToCache(*gameToUpdate);
            }
            // gameDataUpdateLock.unlock(); // Not needed, lock_guard

            // Now handle D2D bitmap creation for the overlay, if this AppID is the currently selected one for VR
            std::unique_lock<std::mutex> overlayStateLock(g_overlayStateMutex);
            bool isCurrentlySelectedForVROverlay = false;
            if (g_selectedGameIndex != -1) { // Check if any game is selected in VR
                 std::unique_lock<std::mutex> vrListLock(g_VROverlayGameListMutex);
                 if (static_cast<size_t>(g_selectedGameIndex) < g_currentCategoryGameAppIds_VR.size() &&
                     g_currentCategoryGameAppIds_VR[g_selectedGameIndex] == result->appId) {
                     isCurrentlySelectedForVROverlay = true;
                 }
                 vrListLock.unlock();
            }

            ID2D1Bitmap* pNewBitmap = nullptr;
            if (isCurrentlySelectedForVROverlay) {
                if (result->success && !result->imageBytes.empty() && g_pWICFactory && g_pD2DContext) {
                    // WIC Decoding and D2D Bitmap Creation
                    IWICStream* pIWICStream = nullptr;
                    IWICBitmapDecoder* pIDecoder = nullptr;
                    IWICBitmapFrameDecode* pIDecoderFrame = nullptr;
                    IWICFormatConverter* pIFormatConverter = nullptr;
                    HRESULT hr = S_OK;

                    hr = g_pWICFactory->CreateStream(&pIWICStream);
                    if (SUCCEEDED(hr)) hr = pIWICStream->InitializeFromMemory(result->imageBytes.data(), (DWORD)result->imageBytes.size());
                    if (SUCCEEDED(hr)) hr = g_pWICFactory->CreateDecoderFromStream(pIWICStream, NULL, WICDecodeMetadataCacheOnDemand, &pIDecoder);
                    if (SUCCEEDED(hr)) hr = pIDecoder->GetFrame(0, &pIDecoderFrame);
                    if (SUCCEEDED(hr)) hr = g_pWICFactory->CreateFormatConverter(&pIFormatConverter);
                    if (SUCCEEDED(hr)) hr = pIFormatConverter->Initialize(pIDecoderFrame, GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, NULL, 0.f, WICBitmapPaletteTypeCustom);
                    if (SUCCEEDED(hr)) hr = g_pD2DContext->CreateBitmapFromWicBitmap(pIFormatConverter, NULL, &pNewBitmap);

                    if (SUCCEEDED(hr) && pNewBitmap) {
                        Log("Successfully created D2D bitmap on main thread for VR overlay (AppID: " + result->appId + ")");
                    } else {
                        Log("Failed to create D2D bitmap on main thread for VR overlay (AppID: " + result->appId + "). HR=" + std::to_string(hr));
                        SAFE_RELEASE(pNewBitmap); 
                    }
                    SAFE_RELEASE(pIWICStream);
                    SAFE_RELEASE(pIDecoder);
                    SAFE_RELEASE(pIDecoderFrame);
                    SAFE_RELEASE(pIFormatConverter);
                }
                SAFE_RELEASE(g_pSelectedGameHeader); 
                g_pSelectedGameHeader = pNewBitmap; 

                SAFE_RELEASE(g_pDescTextLayout); 
                g_descScrollOffsetPx = 0;
                // g_currentLayoutAppId will be updated in DrawOverlayContentD2D when it rebuilds.

                g_isLoadingGameData = false; 
                Log("Updated g_pSelectedGameHeader and g_isLoadingGameData for VR overlay (AppID: " + result->appId + ")");
            } else if (!result->imageBytes.empty() && pNewBitmap){
                 // Bitmap was created but not for current VR selection, release it.
                 SAFE_RELEASE(pNewBitmap);
            }
            overlayStateLock.unlock();

            // Update Desktop UI if this game is selected in the desktop list
            int currentDesktopListIndex = -1;
            int gameActualVectorIndex = GetSelectedGameListIndex(currentDesktopListIndex);
            if (gameActualVectorIndex != -1 && g_games[gameActualVectorIndex] && g_games[gameActualVectorIndex]->appid == result->appId) {
                Log("Updating desktop UI for AppID: " + result->appId);
                SetWindowTextW(g_hEditGameDesc, StringToWString(result->description).c_str());
                if (!result->headerUrl.empty()) {
                     if (result->headerUrl.rfind("cache://", 0) == 0) {
                         std::string displayPath = "(Using cached image: " + GetCachePathForImage(result->appId, result->headerUrl.substr(8)) + ")";
                         SetWindowTextW(g_hEditImagePath, StringToWString(displayPath).c_str());
                     } else {
                         std::string displayPath = "(Using URL: " + result->headerUrl + ")";
                         SetWindowTextW(g_hEditImagePath, StringToWString(displayPath).c_str());
                     }
                } else {
                    SetWindowTextW(g_hEditImagePath, L"(No image set)");
                }
            }

        } else {
            Log("WM_APP_GAME_DATA_READY: Game with AppID " + result->appId + " not found in g_games. Discarding.");
        }

        delete result; // Clean up the dynamically allocated result object
        break;
    }
    case WM_APP_CACHE_SAVE_DONE: // Handle completion of background cache save
    {
        Log("WM_APP_CACHE_SAVE_DONE received.");
        // Re-enable the save button regardless of outcome
        EnableWindow(g_hButtonSaveCache, TRUE);

        WPARAM resultFlags = wParam;
        std::string* pFinalHeader = reinterpret_cast<std::string*>(lParam);
        std::string finalHeaderSetting = pFinalHeader ? *pFinalHeader : "";
        delete pFinalHeader; // Clean up the string passed via LPARAM

        bool imageProcessed = (resultFlags & 1);
        bool imageSuccess = (resultFlags & 2);
        bool metaSuccess = (resultFlags & 4);

        std::wstring messageText;
        UINT messageIcon = MB_ICONERROR; // Default to error

        // Construct message based on success flags
        if (metaSuccess) {
            if (imageProcessed) {
                if (imageSuccess) {
                    messageText = L"Manual cache data (description and image) saved successfully.";
                    messageIcon = MB_ICONINFORMATION;
                    // Update image path display only if metadata save was successful and image was processed
                    int lbIndex = -1;
                    int gameIndex = GetSelectedGameListIndex(lbIndex);
                    if(gameIndex != -1) {
                       std::lock_guard<std::mutex> lock(g_gamesMutex);
                       if (!finalHeaderSetting.empty() && finalHeaderSetting.rfind("cache://", 0) == 0) {
                           std::string displayPath = "(Using cached image: " + GetCachePathForImage(g_games[gameIndex]->appid, finalHeaderSetting) + ")";
                           SetWindowTextW(g_hEditImagePath, StringToWString(displayPath).c_str());
                       } else {
                           // It might be empty or a URL if image processing wasn't done/failed but metadata saved
                            SetWindowTextW(g_hEditImagePath, StringToWString(finalHeaderSetting).c_str());
                       }
                    }

                } else {
                    messageText = L"Metadata (description) saved, but failed to process or save the selected image file to cache.";
                }
            } else {
                messageText = L"Metadata (description) saved successfully. No new image file was processed.";
                messageIcon = MB_ICONINFORMATION;
                // Update image path display based on finalHeaderSetting
                 int lbIndex = -1;
                 int gameIndex = GetSelectedGameListIndex(lbIndex);
                 if(gameIndex != -1) {
                    std::lock_guard<std::mutex> lock(g_gamesMutex); // Needed for GetCachePathForImage
                     if (!finalHeaderSetting.empty() && finalHeaderSetting.rfind("cache://", 0) == 0) {
                         std::string displayPath = "(Using cached image: " + GetCachePathForImage(g_games[gameIndex]->appid, finalHeaderSetting) + ")";
                         SetWindowTextW(g_hEditImagePath, StringToWString(displayPath).c_str());
                     } else if (!finalHeaderSetting.empty()) {
                          std::string displayPath = "(Using URL: " + finalHeaderSetting + ")";
                         SetWindowTextW(g_hEditImagePath, StringToWString(displayPath).c_str());
                     } else {
                         SetWindowTextW(g_hEditImagePath, L"(No image set)");
                     }
                 }
            }
        } else {
             messageText = L"Failed to save metadata cache.";
             if (imageProcessed && !imageSuccess) {
                  messageText += L"\nImage processing/saving also failed.";
             } else if (imageProcessed && imageSuccess) {
                 messageText += L"\nImage was successfully cached, but metadata failed to save. Cache might be inconsistent.";
             }
              // Don't update the image path display if metadata failed
              // Get current path text to reset it if it said "Saving..."
              wchar_t currentPathText[MAX_PATH];
              GetWindowTextW(g_hEditImagePath, currentPathText, MAX_PATH);
              if (wcscmp(currentPathText, L"Saving cache...") == 0) {
                  // Reset to something generic if metadata save failed
                  SetWindowTextW(g_hEditImagePath, L"(Save failed)");
              }
        }

        // --- ADD THIS AT THE END OF THE HANDLER ---
        if (metaSuccess) {
            PopulateGamesListBox(); // Repopulate to show the new name
            // Optional: Re-select the item that was just edited
        }
        // --- END ADD ---

        MessageBoxW(hWnd, messageText.c_str(), L"Save Cache Result", MB_OK | messageIcon);
        break;
    }
    case WM_APP_LOG_MESSAGE:
    {
        std::string* logMsgPtr = reinterpret_cast<std::string*>(lParam);
        if (logMsgPtr && g_hEditLog) {
            {
                std::lock_guard<std::mutex> lock(g_logWindowMutex);
                recent_logs.push_back(*logMsgPtr);
                while (recent_logs.size() > 100) {
                    recent_logs.pop_front();
                }
            }
            SendMessageA(g_hEditLog, WM_SETREDRAW, FALSE, 0);
            std::string all_logs_text;
            {
                std::lock_guard<std::mutex> lock(g_logWindowMutex);
                for (const auto& s : recent_logs) {
                    all_logs_text += s;
                }
            }
            SetWindowTextA(g_hEditLog, all_logs_text.c_str());
            SendMessageA(g_hEditLog, EM_SETSEL, (WPARAM)-1, (LPARAM)-1);
            SendMessageA(g_hEditLog, EM_SCROLLCARET, 0, 0);
            SendMessageA(g_hEditLog, WM_SETREDRAW, TRUE, 0);
            InvalidateRect(g_hEditLog, NULL, TRUE);
            delete logMsgPtr;
        } else if (logMsgPtr) {
            delete logMsgPtr;
        }
        return 0;
    }
    case WM_APP_ENSURE_OVERLAY_VISIBLE:
    {
        Log("WndProc: WM_APP_ENSURE_OVERLAY_VISIBLE received.");
        if (!IsSteamVRRunning()) {
            Log("WM_APP_ENSURE_OVERLAY_VISIBLE: SteamVR not running. Action deferred.");
            break; 
        }

        if (!InitializeOpenVRForOverlay() || !vr::VROverlay()) { 
            Log("WM_APP_ENSURE_OVERLAY_VISIBLE: Failed to initialize OpenVR or VROverlay interface not available.");
            break;
        }

        bool needsFullOverlayRecreation = false;

        // Check 1: Is the OpenVR overlay handle itself missing or invalid?
        if (g_MainOverlay == 0) {
            Log("WM_APP_ENSURE_OVERLAY_VISIBLE: g_MainOverlay is 0. Full overlay recreation needed.");
            needsFullOverlayRecreation = true;
        } else {
            vr::VROverlayHandle_t foundOverlay = 0;
            vr::EVROverlayError overlayError = vr::VROverlay()->FindOverlay("arcade.station.overlay", &foundOverlay);
            if (overlayError != vr::VROverlayError_None || foundOverlay != g_MainOverlay) {
                Log("WM_APP_ENSURE_OVERLAY_VISIBLE: Overlay handle is invalid or not found. Full overlay recreation needed.");
                vr::VROverlay()->DestroyOverlay(g_MainOverlay); // Attempt to destroy it
                g_MainOverlay = 0;
                if (g_ThumbnailOverlay != 0) {
                    vr::VROverlay()->DestroyOverlay(g_ThumbnailOverlay);
                    g_ThumbnailOverlay = 0;
                }
                needsFullOverlayRecreation = true;
            }
        }
        
        // Check 2: Are D3D resources missing?
        if (g_pD3DDevice == nullptr) {
            Log("WM_APP_ENSURE_OVERLAY_VISIBLE: g_pD3DDevice is null. D3D resources need re-initialization.");
            needsFullOverlayRecreation = true; // If D3D is gone, we need PrepareOverlay to run which will call InitializeOverlayDirectX
            if (g_MainOverlay != 0) { // If D3D is gone but VR overlay handle still exists
                Log("WM_APP_ENSURE_OVERLAY_VISIBLE: D3D device is null, but OpenVR overlay handle exists. Destroying OpenVR overlay handle to ensure full PrepareOverlay execution.");
                vr::VROverlay()->DestroyOverlay(g_MainOverlay);
                g_MainOverlay = 0;
                if (g_ThumbnailOverlay != 0) {
                    vr::VROverlay()->DestroyOverlay(g_ThumbnailOverlay);
                    g_ThumbnailOverlay = 0;
                }
            }
        }

        if (needsFullOverlayRecreation) {
            Log("WM_APP_ENSURE_OVERLAY_VISIBLE: Setting continuousOverlayRunning to false to trigger full setup path in ShowOverlayContinuous.");
            continuousOverlayRunning = false; 
        }
        
        ShowOverlayContinuous(); // This will now robustly handle re-creation or just re-showing.
        break;
    }
    case WM_APP_PAUSE_OVERLAY_RENDERING:
    {
        Log("WndProc: WM_APP_PAUSE_OVERLAY_RENDERING received.");
        KillTimer(hWnd, OVERLAY_TIMER_ID); // Stop the rendering/refresh timer
        Log("Overlay rendering timer (OVERLAY_TIMER_ID) stopped.");

        // Stop the video thread
        if (g_videoThread.joinable()) {
            Log("WM_APP_PAUSE_OVERLAY_RENDERING: Stopping video thread.");
            g_stopVideoThread = true;
            // Graceful join with timeout
            std::thread safetyThread([] {
                std::this_thread::sleep_for(std::chrono::milliseconds(500)); // Shorter timeout for pause
                if (g_videoThread.joinable()) {
                    Log("WM_APP_PAUSE_OVERLAY_RENDERING: Video thread join timed out - detaching.");
                    g_videoThread.detach();
                }
            });
            // Attempt to join briefly. If it takes too long, the safety thread will detach it.
            auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(250);
            while(g_videoThread.joinable() && std::chrono::steady_clock::now() < deadline) {
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
            }
            if(g_videoThread.joinable()) { // If still joinable after primary attempt
                Log("WM_APP_PAUSE_OVERLAY_RENDERING: Video thread did not join in primary attempt, detaching.");
                g_videoThread.detach();
            } else {
                Log("WM_APP_PAUSE_OVERLAY_RENDERING: Video thread joined or was already not joinable.");
            }
            safetyThread.detach(); // Detach the safety thread itself
        }
        // Do NOT set continuousOverlayRunning = false here.
        // Do NOT destroy g_MainOverlay or D3D resources.
        // g_videoInitialized might be set to false here or when video thread actually exits.
        // For simplicity, let InitializeVideo be called again when ShowOverlayContinuous runs.
        g_videoInitialized = false; 
        break;
    }
    default:
        return DefWindowProcW(hWnd, message, wParam, lParam);
    }
    return 0;
}