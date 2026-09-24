#include "html_exporter.h"
#include "globals.h"
#include "logging.h"
#include "steam_games.h"
#include "steam_api.h"
#include <commdlg.h>
#include <shlobj.h>
#include <map>
#include <vector>
#include <sstream>
#include <set>
#include <algorithm>

// Helper function to escape special HTML characters to prevent rendering issues.
std::string escapeHtml(const std::string& data) {
    std::string buffer;
    buffer.reserve(data.size());
    for(size_t pos = 0; pos != data.size(); ++pos) {
        switch(data[pos]) {
            case '&':  buffer.append("&amp;");       break;
            case '\"': buffer.append("&quot;");      break;
            case '\'': buffer.append("&#39;");       break;
            case '<':  buffer.append("&lt;");        break;
            case '>':  buffer.append("&gt;");        break;
            default:   buffer.append(&data[pos], 1); break;
        }
    }
    return buffer;
}

void ExportCategorizedGamesToHTML(HWND hWnd) {
    Log("ExportCategorizedGamesToHTML: Starting export process.");

    // 1. Gather all games, grouped by the categories they appear in, and also a unique list.
    std::map<std::string, std::vector<SteamGame*>> gamesByCategory;
    std::set<SteamGame*> uniqueCategorizedGames;

    { // Scope for mutexes
        std::lock_guard<std::mutex> cat_lock(g_categoriesMutex);
        
        for (const auto& category_ptr : g_categories) {
            if (!category_ptr || category_ptr->gameAppIds.empty()) continue;

            std::vector<SteamGame*> gamesInThisCategory;
            for (const auto& appId : category_ptr->gameAppIds) {
                SteamGame* game = findGameByAppId(appId); // This locks g_gamesMutex internally
                if (game) {
                    gamesInThisCategory.push_back(game);
                    uniqueCategorizedGames.insert(game);
                }
            }
            if (!gamesInThisCategory.empty()) {
                std::sort(gamesInThisCategory.begin(), gamesInThisCategory.end(), [](const SteamGame* a, const SteamGame* b) {
                    std::string nameA_lower = a->name;
                    std::string nameB_lower = b->name;
                    std::transform(nameA_lower.begin(), nameA_lower.end(), nameA_lower.begin(), ::tolower);
                    std::transform(nameB_lower.begin(), nameB_lower.end(), nameB_lower.begin(), ::tolower);
                    return nameA_lower < nameB_lower;
                });
                gamesByCategory[category_ptr->name] = gamesInThisCategory;
            }
        }
    }

    if (gamesByCategory.empty()) {
        MessageBoxW(hWnd, L"No games found in any category. Nothing to export.", L"Export HTML", MB_OK | MB_ICONINFORMATION);
        Log("ExportCategorizedGamesToHTML: No categorized games found.");
        return;
    }

    // 2. Check for and fetch any uncached game data.
    std::vector<SteamGame*> gamesToFetch;
    for (SteamGame* game : uniqueCategorizedGames) {
        std::lock_guard<std::mutex> lock(game->dataMutex);
        if (!game->storeDataFetched) {
            gamesToFetch.push_back(game);
        }
    }

    if (!gamesToFetch.empty()) {
        Log("ExportHTML: Need to fetch API data for " + std::to_string(gamesToFetch.size()) + " games.");
        std::wstring waitMsg = L"Fetching store data for " + std::to_wstring(gamesToFetch.size()) + L" games before exporting.\n\nThis may take a moment and the application will be unresponsive. Please wait.";
        MessageBoxW(hWnd, waitMsg.c_str(), L"Fetching Data...", MB_OK | MB_ICONINFORMATION);
        
        for (SteamGame* game : gamesToFetch) {
            Log("ExportHTML: Fetching data for: " + game->name + " (AppID: " + game->appid + ")");
            FetchStoreDataForGame(*game);
        }
        Log("ExportHTML: All necessary game data has been fetched.");
    } else {
        Log("ExportHTML: All categorized games already have cached data. Proceeding directly to export.");
    }

    // 3. Prompt for save location
    wchar_t szFile[MAX_PATH] = L"game_library.html";
    OPENFILENAMEW ofn = { 0 };
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hWnd;
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = sizeof(szFile) / sizeof(wchar_t);
    ofn.lpstrFilter = L"HTML Files\0*.html\0All Files\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.lpstrTitle = L"Save Game List As HTML";
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT | OFN_EXPLORER;

    if (GetSaveFileNameW(&ofn) != TRUE) {
        Log("ExportCategorizedGamesToHTML: User cancelled save dialog.");
        return;
    }

    std::wstring htmlFilePathW = ofn.lpstrFile;
    std::wstring exportDirW = htmlFilePathW.substr(0, htmlFilePathW.find_last_of(L'\\'));
    std::wstring imagesDirW = exportDirW + L"\\images";

    // 4. Create the 'images' subdirectory.
    if (SHCreateDirectoryExW(NULL, imagesDirW.c_str(), NULL) != ERROR_SUCCESS) {
        DWORD attrs = GetFileAttributesW(imagesDirW.c_str());
        if (attrs == INVALID_FILE_ATTRIBUTES || !(attrs & FILE_ATTRIBUTE_DIRECTORY)) {
             MessageBoxW(hWnd, L"Failed to create the 'images' sub-directory for the export.", L"Export Error", MB_OK | MB_ICONERROR);
             Log("ExportCategorizedGamesToHTML: Failed to create images directory.");
             return;
        }
    }

    // 5. Generate the HTML content.
    std::stringstream html;

    // HTML Header and CSS
    html << R"(<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Game Library</title>
    <style>
        body { font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, Helvetica, Arial, sans-serif; background-color: #121212; color: #e0e0e0; margin: 0; padding: 20px; }
        .container { max-width: 1200px; margin: auto; }
        h1 { text-align: center; color: #4CAF50; }
        .search-bar { width: 100%; padding: 12px; margin-bottom: 25px; background-color: #2a2a2a; border: 1px solid #444; border-radius: 5px; color: #e0e0e0; font-size: 1.1em; box-sizing: border-box; }
        .accordion { background-color: #333; color: #eee; cursor: pointer; padding: 18px; width: 100%; border: none; text-align: left; outline: none; font-size: 1.2em; font-weight: bold; transition: background-color 0.4s; border-radius: 5px; margin-top: 10px; }
        .accordion.active, .accordion:hover { background-color: #4CAF50; }
        .panel { display: none; overflow: hidden; padding-top: 15px; }
        .category-grid { display: grid; grid-template-columns: repeat(auto-fill, minmax(250px, 1fr)); gap: 20px; }
        .game-card { background-color: #1e1e1e; border-radius: 8px; overflow: hidden; box-shadow: 0 4px 8px rgba(0,0,0,0.3); transition: transform 0.2s; display: flex; flex-direction: column; }
        .game-card:hover { transform: scale(1.03); }
        .game-card img { width: 100%; height: 120px; object-fit: cover; display: block; }
        .game-card-content { padding: 15px; flex-grow: 1; }
        .game-card h3 { margin-top: 0; margin-bottom: 10px; color: #fff; }
        .game-card p { font-size: 0.9em; color: #b0b0b0; line-height: 1.5; }
    </style>
</head>
<body>
    <div class="container">
        <h1>Game Library</h1>
)";
    html << "<input type=\"text\" id=\"searchInput\" class=\"search-bar\" onkeyup=\"searchGames()\" placeholder=\"Search for games...\">\n";

    // Create a sorted vector from the unique set for the "All Games" section
    std::vector<SteamGame*> allGamesSorted(uniqueCategorizedGames.begin(), uniqueCategorizedGames.end());
    std::sort(allGamesSorted.begin(), allGamesSorted.end(), [](const SteamGame* a, const SteamGame* b) {
        std::string nameA_lower = a->name;
        std::string nameB_lower = b->name;
        std::transform(nameA_lower.begin(), nameA_lower.end(), nameA_lower.begin(), ::tolower);
        std::transform(nameB_lower.begin(), nameB_lower.end(), nameB_lower.begin(), ::tolower);
        return nameA_lower < nameB_lower;
    });

    // "All Games" Accordion Section
    html << "<div class=\"category-section\">\n";
    html << "<button class=\"accordion\">All Games (" << allGamesSorted.size() << ")</button>\n";
    html << "<div class=\"panel\">\n<div class=\"category-grid\">\n";
    for (SteamGame* game : allGamesSorted) {
        // This is duplicated code. A lambda would be nice here.
        std::string imageRelativePath = "";
        if (!game->headerImage.empty()) {
            std::string actualCachePath;
            if (game->headerImage.rfind("cache://", 0) == 0) {
                 actualCachePath = GetCachePathForImage(game->appid, game->headerImage.substr(8));
            } else {
                 actualCachePath = GetCachePathForImage(game->appid, game->headerImage);
            }
            std::wstring sourcePathW = StringToWString(actualCachePath);
            std::string destFilename = game->appid + "_" + GetFilenameFromUrl(game->headerImage);
            std::wstring destPathW = imagesDirW + L"\\" + StringToWString(destFilename);
            if (CopyFileW(sourcePathW.c_str(), destPathW.c_str(), FALSE)) {
                imageRelativePath = "images/" + destFilename;
            }
        }
        html << "  <div class=\"game-card\">\n";
        if (!imageRelativePath.empty()) {
            html << "    <img src=\"" << escapeHtml(imageRelativePath) << "\" alt=\"" << escapeHtml(game->name) << " header image\">\n";
        }
        html << "    <div class=\"game-card-content\">\n";
        html << "      <h3>" << escapeHtml(game->name) << "</h3>\n";
        html << "      <p>" << escapeHtml(game->description) << "</p>\n";
        html << "    </div>\n";
        html << "  </div>\n";
    }
    html << "</div>\n</div>\n</div>\n";


    // Individual Category Accordion Sections
    for (const auto& pair : gamesByCategory) {
        const std::string& categoryName = pair.first;
        const std::vector<SteamGame*>& games = pair.second;
        
        html << "<div class=\"category-section\">\n";
        html << "<button class=\"accordion\">" << escapeHtml(categoryName) << " (" << games.size() << ")</button>\n";
        html << "<div class=\"panel\">\n<div class=\"category-grid\">\n";

        for (SteamGame* game : games) {
            std::string imageRelativePath = "";
            if (!game->headerImage.empty()) {
                std::string actualCachePath;
                if (game->headerImage.rfind("cache://", 0) == 0) {
                     actualCachePath = GetCachePathForImage(game->appid, game->headerImage.substr(8));
                } else {
                     actualCachePath = GetCachePathForImage(game->appid, game->headerImage);
                }
                std::wstring sourcePathW = StringToWString(actualCachePath);
                std::string destFilename = game->appid + "_" + GetFilenameFromUrl(game->headerImage);
                std::wstring destPathW = imagesDirW + L"\\" + StringToWString(destFilename);
                if (CopyFileW(sourcePathW.c_str(), destPathW.c_str(), FALSE)) {
                    imageRelativePath = "images/" + destFilename;
                }
            }

            html << "  <div class=\"game-card\">\n";
            if (!imageRelativePath.empty()) {
                html << "    <img src=\"" << escapeHtml(imageRelativePath) << "\" alt=\"" << escapeHtml(game->name) << " header image\">\n";
            }
            html << "    <div class=\"game-card-content\">\n";
            html << "      <h3>" << escapeHtml(game->name) << "</h3>\n";
            html << "      <p>" << escapeHtml(game->description) << "</p>\n";
            html << "    </div>\n";
            html << "  </div>\n";
        }
        
        html << "</div>\n</div>\n</div>\n";
    }

    // JavaScript for interactivity
    html << "\n    </div>\n    <script>\n";
    html << "        // Accordion Logic\n";
    html << "        var acc = document.getElementsByClassName(\"accordion\");\n";
    html << "        for (var i = 0; i < acc.length; i++) {\n";
    html << "            acc[i].addEventListener(\"click\", function() {\n";
    html << "                this.classList.toggle(\"active\");\n";
    html << "                var panel = this.nextElementSibling;\n";
    html << "                if (panel.style.display === \"block\") {\n";
    html << "                    panel.style.display = \"none\";\n";
    html << "                } else {\n";
    html << "                    panel.style.display = \"block\";\n";
    html << "                }\n";
    html << "            });\n";
    html << "        }\n\n";
    html << "        // Store original category names and counts for restoration\n";
    html << "        var originalAccordionTexts = [];\n";
    html << "        var accordions = document.getElementsByClassName(\"accordion\");\n";
    html << "        for (var a = 0; a < accordions.length; a++) {\n";
    html << "            originalAccordionTexts[a] = accordions[a].textContent;\n";
    html << "        }\n\n";
    html << "        // Search Logic\n";
    html << "        function searchGames() {\n";
    html << "            var input = document.getElementById('searchInput');\n";
    html << "            var filter = input.value.toUpperCase();\n";
    html << "            var sections = document.getElementsByClassName('category-section');\n\n";
    html << "            for (var s = 0; s < sections.length; s++) {\n";
    html << "                var section = sections[s];\n";
    html << "                var accordion = section.getElementsByClassName('accordion')[0];\n";
    html << "                var cards = section.getElementsByClassName('game-card');\n";
    html << "                var visibleCardsInSection = 0;\n\n";
    html << "                for (var i = 0; i < cards.length; i++) {\n";
    html << "                    var h3 = cards[i].getElementsByTagName(\"h3\")[0];\n";
    html << "                    var p = cards[i].getElementsByTagName(\"p\")[0];\n";
    html << "                    var matchFound = false;\n\n";
    html << "                    // Search in title\n";
    html << "                    if (h3) {\n";
    html << "                        var titleValue = h3.textContent || h3.innerText;\n";
    html << "                        if (titleValue.toUpperCase().indexOf(filter) > -1) {\n";
    html << "                            matchFound = true;\n";
    html << "                        }\n";
    html << "                    }\n\n";
    html << "                    // Search in description if no match found in title\n";
    html << "                    if (!matchFound && p) {\n";
    html << "                        var descValue = p.textContent || p.innerText;\n";
    html << "                        if (descValue.toUpperCase().indexOf(filter) > -1) {\n";
    html << "                            matchFound = true;\n";
    html << "                        }\n";
    html << "                    }\n\n";
    html << "                    if (matchFound) {\n";
    html << "                        cards[i].style.display = \"\";\n";
    html << "                        visibleCardsInSection++;\n";
    html << "                    } else {\n";
    html << "                        cards[i].style.display = \"none\";\n";
    html << "                    }\n";
    html << "                }\n\n";
    html << "                // Update accordion text with current visible count\n";
    html << "                if (accordion) {\n";
    html << "                    var originalText = originalAccordionTexts[s];\n";
    html << "                    var categoryNameMatch = originalText.match(/^(.+?)\\s*\\(/);\n";
    html << "                    var categoryName = categoryNameMatch ? categoryNameMatch[1] : originalText;\n\n";
    html << "                    if (filter.length > 0) {\n";
    html << "                        accordion.textContent = categoryName + \" (\" + visibleCardsInSection + \")\";\n";
    html << "                    } else {\n";
    html << "                        accordion.textContent = originalText;\n";
    html << "                    }\n";
    html << "                }\n\n";
    html << "                // Hide category header if no cards are visible in it during a search\n";
    html << "                if (filter.length > 0 && visibleCardsInSection === 0) {\n";
    html << "                    section.style.display = \"none\";\n";
    html << "                } else {\n";
    html << "                    section.style.display = \"\";\n";
    html << "                }\n";
    html << "            }\n";
    html << "        }\n";
    html << "    </script>\n";
    html << "</body>\n";
    html << "</html>\n";

    // 6. Write the generated HTML to the chosen file.
    std::ofstream ofs(htmlFilePathW);
    if (ofs.is_open()) {
        ofs << html.str();
        ofs.close();
        Log("Successfully exported HTML to " + WStringToString(htmlFilePathW));
        std::wstring successMsg = L"Export complete!\n\nHTML file and 'images' folder saved to:\n" + exportDirW;
        MessageBoxW(hWnd, successMsg.c_str(), L"Export Successful", MB_OK | MB_ICONINFORMATION);
    } else {
        MessageBoxW(hWnd, L"Failed to write to the selected HTML file.", L"Export Error", MB_OK | MB_ICONERROR);
        Log("ExportCategorizedGamesToHTML: Failed to open file for writing: " + WStringToString(htmlFilePathW));
    }
} 