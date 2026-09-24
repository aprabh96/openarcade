#include "config.h"
#include "logging.h"
#include <fstream>
#include <ShlObj.h> // For SHGetFolderPath, CreateDirectory, etc.
#include <algorithm> // For std::sort
#include <sstream>   // For std::stringstream
#include <vector>
#include "steam_games.h" // <--- ADD THIS INCLUDE for findGameByAppId and SteamGame struct

// <ai_context>
// Implementation for INI-based config
// </ai_context>

void LoadCategories() {
    Log("LoadCategories: Function entered.");
    std::lock_guard<std::mutex> lock(g_categoriesMutex);
    g_categories.clear();
    Log("LoadCategories -> Loading categories sequentially from INI: " + g_iniFullPath);

    int categoryIndex = 0;
    while (true) {
        std::string catSectionName = "Category_" + std::to_string(categoryIndex);

        // Check if the section exists by trying to read the 'Name' key.
        char catNameBuf[256];
        GetPrivateProfileStringA(catSectionName.c_str(), "Name", "", catNameBuf, 256, g_iniFullPath.c_str());
        std::string categoryName = catNameBuf;

        if (categoryName.empty()) {
            // If the name is empty, we assume this is the end of the category list.
            Log("LoadCategories: No category found in section " + catSectionName + ". Assuming end of list.");
            break;
        }

        Log("LoadCategories: Processing INI section: " + catSectionName);
        auto newCategory = std::make_unique<GameCategory>(categoryName, categoryIndex); // Use index as ID for now

        char gameAppIdsBuf[4096];
        GetPrivateProfileStringA(catSectionName.c_str(), "GameAppIds", "", gameAppIdsBuf, 4096, g_iniFullPath.c_str());
        std::string gameAppIdsStr = gameAppIdsBuf;
        std::stringstream ss(gameAppIdsStr);
        std::string appId;

        while (std::getline(ss, appId, ',')) {
            if (!appId.empty()) {
                newCategory->gameAppIds.push_back(appId);
            }
        }
        g_categories.push_back(std::move(newCategory));
        Log("LoadCategories -> Loaded category '" + categoryName + "' with " + std::to_string(g_categories.back()->gameAppIds.size()) + " games.");
        categoryIndex++;
    }

    Log("LoadCategories -> Loaded " + std::to_string(g_categories.size()) + " categories in file order.");

    if (!g_categories.empty()) {
        g_selectedCategoryIndexVR = 0;
    } else {
        g_selectedCategoryIndexVR = -1;
    }
    Log("LoadCategories: Function finished.");
}

void SaveCategories() {
    std::lock_guard<std::mutex> lock(g_categoriesMutex);
    Log("SaveCategories -> Saving " + std::to_string(g_categories.size()) + " categories to INI: " + g_iniFullPath);
    
    // The following sort is what we need to remove to preserve manual order.
    // std::sort(g_categories.begin(), g_categories.end(), [](const std::unique_ptr<GameCategory>& a, const std::unique_ptr<GameCategory>& b) {
    //     return *a < *b;
    // });  // <-- DELETE THIS BLOCK

    int categoryIndex = 0;
    for (const auto& category : g_categories) {
        if (!category || category->name.empty()) continue;
        std::string sectionName = "Category_" + std::to_string(categoryIndex);
        WritePrivateProfileStringA(sectionName.c_str(), "Name", category->name.c_str(), g_iniFullPath.c_str());
        // --- New sorting logic for gameAppIds ---
        std::vector<std::pair<std::string, std::string>> gameAppIdNamePairs;
        for (const std::string& appId : category->gameAppIds) {
            SteamGame* game = findGameByAppId(appId);
            if (game) {
                gameAppIdNamePairs.push_back({appId, game->name});
            } else {
                gameAppIdNamePairs.push_back({appId, ""});
                Log("SaveCategories: Warning - Game with AppID " + appId + " not found in g_games while sorting for category '" + category->name + "'. It will be sorted based on AppID or empty name.");
            }
        }
        std::sort(gameAppIdNamePairs.begin(), gameAppIdNamePairs.end(), [](const auto& a, const auto& b) {
            std::string nameA_lower = a.second;
            std::string nameB_lower = b.second;
            std::transform(nameA_lower.begin(), nameA_lower.end(), nameA_lower.begin(), ::tolower);
            std::transform(nameB_lower.begin(), nameB_lower.end(), nameB_lower.begin(), ::tolower);
            if (nameA_lower.empty() && !nameB_lower.empty()) return false;
            if (!nameA_lower.empty() && nameB_lower.empty()) return true;
            return nameA_lower < nameB_lower;
        });
        std::string gameAppIdsStr;
        for (size_t i = 0; i < gameAppIdNamePairs.size(); ++i) {
            gameAppIdsStr += gameAppIdNamePairs[i].first;
            if (i < gameAppIdNamePairs.size() - 1) {
                gameAppIdsStr += ",";
            }
        }
        // --- End new sorting logic ---
        WritePrivateProfileStringA(sectionName.c_str(), "GameAppIds", gameAppIdsStr.c_str(), g_iniFullPath.c_str());
        Log("SaveCategories -> Saved category '" + category->name + "' to section " + sectionName + " with " + std::to_string(gameAppIdNamePairs.size()) + " games (sorted by name).");
        categoryIndex++;
    }
    char oldSectionName[256];
    int oldCategoryCount = GetPrivateProfileIntA("CategoriesInfo", "Count", 0, g_iniFullPath.c_str());
    for (int i = categoryIndex; i < oldCategoryCount; ++i) {
        sprintf_s(oldSectionName, "Category_%d", i);
        WritePrivateProfileSectionA(oldSectionName, NULL, g_iniFullPath.c_str());
        Log("SaveCategories -> Cleared old INI section: " + std::string(oldSectionName));
    }
    WritePrivateProfileStringA("CategoriesInfo", "Count", std::to_string(categoryIndex).c_str(), g_iniFullPath.c_str());
    Log("SaveCategories -> Finished saving categories.");
}


void SetupINIPath()
{
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(NULL, exePath, MAX_PATH);
    PathRemoveFileSpecW(exePath);
    PathAppendW(exePath, L"settings.ini");

    std::wstring wFullPath(exePath);
    g_iniFullPath = WStringToString(wFullPath);

    char buf[512];
    sprintf_s(buf, "SetupINIPath -> Using INI path: %s", g_iniFullPath.c_str());
    Log(buf);
}

void EnsureIniExists()
{
    DWORD attrs = GetFileAttributesA(g_iniFullPath.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES)
    {
        Log("EnsureIniExists -> no ini found, creating default file.");
        std::ofstream ofs(g_iniFullPath);
        ofs << "[OverlaySettings]\n";
        ofs << "MasterIP=127.0.0.1\n";
        ofs << "StationName=StationDefault\n";
        ofs << "AutoEnableOverlay=0\n";
        ofs << "SessionEndTime=0\n";
        ofs.close();
    }
    else
    {
        Log("EnsureIniExists -> INI found, no need to create.");
    }
}

void LoadSettings()
{
    int val = GetPrivateProfileIntA(INI_SECTION, "AutoEnableOverlay", 0, g_iniFullPath.c_str());
    g_AutoEnableOverlay = (val == 1);
    Log(std::string("LoadSettings -> AutoEnableOverlay=") + (g_AutoEnableOverlay ? "1" : "0"));
}

void SaveSettings()
{
    const char* val = g_AutoEnableOverlay ? "1" : "0";
    WritePrivateProfileStringA(INI_SECTION, "AutoEnableOverlay", val, g_iniFullPath.c_str());
    Log(std::string("SaveSettings -> wrote AutoEnableOverlay=") + val);
}

void SaveMasterIP(const std::string& ip)
{
    WritePrivateProfileStringA(INI_SECTION, "MasterIP", ip.c_str(), g_iniFullPath.c_str());
    Log("SaveMasterIP -> wrote MasterIP=" + ip);
}

std::string LoadMasterIP()
{
    char buf[256];
    GetPrivateProfileStringA(INI_SECTION, "MasterIP", "127.0.0.1", buf, 256, g_iniFullPath.c_str());
    std::string result(buf);
    Log("LoadMasterIP -> read from INI: " + result);
    return result;
}

void SaveStationName(const std::string& name)
{
    WritePrivateProfileStringA(INI_SECTION, "StationName", name.c_str(), g_iniFullPath.c_str());
    Log("SaveStationName -> wrote StationName=" + name);
}

std::string LoadStationName()
{
    char buf[256];
    GetPrivateProfileStringA(INI_SECTION, "StationName", "StationDefault", buf, 256, g_iniFullPath.c_str());
    std::string result(buf);
    Log("LoadStationName -> read from INI: " + result);
    return result;
}

void SaveSessionEndTime(time_t endTime)
{
    char buf[64];
    sprintf_s(buf, "%lld", (long long)endTime);
    WritePrivateProfileStringA(INI_SECTION, "SessionEndTime", buf, g_iniFullPath.c_str());
    Log(std::string("SaveSessionEndTime -> wrote ") + buf);
}

time_t LoadSessionEndTime()
{
    char buf[64];
    GetPrivateProfileStringA(INI_SECTION, "SessionEndTime", "0", buf, 64, g_iniFullPath.c_str());
    long long v = _atoi64(buf);
    Log(std::string("LoadSessionEndTime -> read ") + buf);
    return (time_t)v;
}

void SetupCachePath()
{
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(NULL, exePath, MAX_PATH);
    PathRemoveFileSpecW(exePath);
    PathAppendW(exePath, L"game_cache"); // Append the cache folder name

    std::wstring wFullPath(exePath);
    g_cacheDirFullPath = WStringToString(wFullPath); // Use the global defined in globals.cpp

    Log("SetupCachePath -> Using cache path: " + g_cacheDirFullPath);
}

void EnsureCacheDirExists()
{
    DWORD attrs = GetFileAttributesA(g_cacheDirFullPath.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES)
    {
        Log("EnsureCacheDirExists -> Cache directory not found, creating: " + g_cacheDirFullPath);
        if (CreateDirectoryA(g_cacheDirFullPath.c_str(), NULL)) {
            Log("EnsureCacheDirExists -> Cache directory created successfully.");
        } else {
            Log("EnsureCacheDirExists -> ERROR: Failed to create cache directory. Error code: " + std::to_string(GetLastError()));
            // Consider notifying the user or disabling caching if creation fails
        }
    }
    else if (!(attrs & FILE_ATTRIBUTE_DIRECTORY))
    {
        Log("EnsureCacheDirExists -> ERROR: Cache path exists but is not a directory: " + g_cacheDirFullPath);
        // Handle error: maybe try deleting the file and creating the directory?
    }
    else
    {
        Log("EnsureCacheDirExists -> Cache directory found: " + g_cacheDirFullPath);
    }
}