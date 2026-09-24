#include "steam_api.h"
#include "logging.h"
#include <winhttp.h>
#include <sstream>
#include <vector>
#include "json.hpp" // nlohmann::json for JSON cache
#include <fstream>      // For std::ifstream, std::ofstream
#include <algorithm>    // For std::replace used in GetCachePathForImage
#include "globals.h"    // For g_cacheDirFullPath and StringToWString/WStringToString if used within steam_api.cpp

#pragma comment(lib, "Winhttp.lib")

using json = nlohmann::json;

// Helper to replace "\\/" with "/" in JSON strings
void FixEscapedSlashes(std::string& s)
{
    const std::string from = "\\/";
    const std::string to = "/";
    size_t pos = 0;
    while ((pos = s.find(from, pos)) != std::string::npos)
    {
        s.replace(pos, from.size(), to);
        pos += to.size();
    }
}

// Add this helper function (or put it in a common utilities header)
static void trim_quotes_and_spaces(std::string& s) {
    // Remove leading/trailing quotes if present
    if (!s.empty() && s.front() == '"') s.erase(0, 1);
    if (!s.empty() && s.back() == '"') s.pop_back();
    // Remove leading/trailing whitespace
    s.erase(0, s.find_first_not_of(" \t\r\n"));
    s.erase(s.find_last_not_of(" \t\r\n") + 1);
}

// Parses description and header image URL from Steam store API JSON response
bool ParseSteamApiResponse(const std::string& jsonResponse, SteamGame& game)
{
    bool successFound = false;
    bool dataFound = false;

    // Minimal check for success and data presence
    if (jsonResponse.find("\"success\":true") == std::string::npos)
    {
         Log("Warning: JSON response for appid " + game.appid + " missing '\"success\":true'.");
         // Continue parsing anyway, might be partially valid
    } else {
        successFound = true;
    }

    // Find the block for this specific appid: "appid": { ... }
    std::string appid_key = "\"" + game.appid + "\":";
    size_t appid_block_start = jsonResponse.find(appid_key);
    if (appid_block_start == std::string::npos) {
        Log("Error: Cannot find appid block key '" + appid_key + "' in JSON response.");
        return false;
    }

    // Find the start of the data object for this app
    size_t data_start = jsonResponse.find("\"data\":{", appid_block_start);
    if (data_start == std::string::npos) {
        Log("Warning: Cannot find '\"data\":{' block for appid " + game.appid + " in JSON response.");
        // It might be a DLC or other non-standard app type without full details
        return false; // Treat as failure if no data block
    }
     data_start += 7; // Move past "\"data\":{"

    // Find the end of the data object (tricky, look for matching '}')
    int brace_level = 1;
    size_t data_end = data_start;
    while(data_end < jsonResponse.length() && brace_level > 0) {
        if (jsonResponse[data_end] == '{') brace_level++;
        else if (jsonResponse[data_end] == '}') brace_level--;
        data_end++;
    }
    if (brace_level != 0) {
         Log("Error: Unmatched braces finding data block end for appid " + game.appid);
         return false;
    }
    // data_end points one char *after* the closing brace, adjust if needed or use substr length
    std::string data_block = jsonResponse.substr(data_start, data_end - data_start -1);


    // Parse short_description within the data_block
    {
        std::string key = "\"short_description\":\"";
        size_t pos = data_block.find(key);
        if (pos != std::string::npos)
        {
            size_t start = pos + key.size();
            size_t end = data_block.find("\"", start);
            if (end != std::string::npos && end > start)
            {
                game.description = data_block.substr(start, end - start);
                FixEscapedSlashes(game.description);
                trim_quotes_and_spaces(game.description);
                dataFound = true;
            } else {
                 Log("Warning: Found short_description key but couldn't parse value for " + game.appid);
            }
        } else {
             Log("Warning: No short_description found for " + game.appid);
        }
    }

    // Parse header_image within the data_block
    {
        std::string key = "\"header_image\":\"";
        size_t pos = data_block.find(key);
        if (pos != std::string::npos)
        {
            size_t start = pos + key.size();
            size_t end = data_block.find("\"", start);
            if (end != std::string::npos && end > start)
            {
                game.headerImage = data_block.substr(start, end - start);
                FixEscapedSlashes(game.headerImage);
                trim_quotes_and_spaces(game.headerImage);
                dataFound = true;
            } else {
                Log("Warning: Found header_image key but couldn't parse value for " + game.appid);
            }
        } else {
             Log("Warning: No header_image found for " + game.appid);
        }
    }

    // Optional: Parse screenshots/trailers here using the logic from ParseScreenshotsAndTrailers
    // You would adapt that function to work on the 'data_block' string.

    if (dataFound) {
        Log("Successfully parsed some data (description/header) for appid=" + game.appid);
    } else {
         Log("Warning: Parsed JSON for appid=" + game.appid + " but found no usable data fields.");
    }

    return dataFound; // Return true if we found *any* data
}

// --- Caching Function Implementations ---

// Helper to get the cache path for a game's metadata JSON
std::string GetMetadataCachePath(const std::string& appid) {
    return g_cacheDirFullPath + "\\" + appid + ".json";
}

// Helper to extract filename from URL (basic implementation)
std::string GetFilenameFromUrl(const std::string& url) {
    size_t lastSlash = url.find_last_of('/');
    if (lastSlash == std::string::npos) {
        return "header.jpg"; // Default or handle error
    }
    std::string filenameWithQuery = url.substr(lastSlash + 1);
    size_t queryPos = filenameWithQuery.find('?');
    if (queryPos != std::string::npos) {
        return filenameWithQuery.substr(0, queryPos);
    }
    return filenameWithQuery; // Return full part if no query string
}

// Helper to get the full cache path for an image
std::string GetCachePathForImage(const std::string& appid, const std::string& imageUrl) {
    std::string filename = GetFilenameFromUrl(imageUrl);
    // Sanitize filename slightly (replace invalid chars - basic example)
    std::replace(filename.begin(), filename.end(), ':', '_');
    std::replace(filename.begin(), filename.end(), '*', '_');
    // Add more replacements if needed...
    if (filename.empty()) filename = "header.img"; // Fallback

    return g_cacheDirFullPath + "\\" + appid + "_" + filename;
}

bool LoadMetadataFromCache(const std::string& appid, SteamGame& game) {
    std::string cacheFilePath = GetMetadataCachePath(appid);
    std::ifstream ifs(cacheFilePath);
    if (!ifs.is_open()) {
        // Log("LoadMetadataFromCache: Cache file not found: " + cacheFilePath); // Optional log
        return false;
    }

    try {
        json j;
        ifs >> j;
        ifs.close(); // Close file after reading

        // Check if keys exist before accessing
        if (j.contains("description") && j["description"].is_string()) {
             game.description = j.value("description", ""); // Use value() for safety
        } else {
             Log("LoadMetadataFromCache: 'description' missing or not a string in " + cacheFilePath);
             game.description = ""; // Ensure default value
        }

        if (j.contains("headerImage") && j["headerImage"].is_string()) {
            game.headerImage = j.value("headerImage", "");
        } else {
             Log("LoadMetadataFromCache: 'headerImage' missing or not a string in " + cacheFilePath);
             game.headerImage = ""; // Ensure default value
        }

        // Basic validation - did we load anything useful?
        if (game.description.empty() && game.headerImage.empty()) {
             Log("LoadMetadataFromCache: Cache file loaded but contained no useful data: " + cacheFilePath);
             return false; // Treat as failure if no data loaded
        }

        Log("LoadMetadataFromCache: Successfully loaded metadata from cache for appid " + appid);
        return true;

    } catch (json::parse_error& e) {
        Log("LoadMetadataFromCache: Failed to parse JSON cache file: " + cacheFilePath + ". Error: " + e.what());
        ifs.close(); // Ensure file is closed on error too
        // Consider deleting the corrupt cache file here
        DeleteFileA(cacheFilePath.c_str());
        return false;
    } catch (const std::exception& e) {
        Log("LoadMetadataFromCache: Exception reading cache file: " + cacheFilePath + ". Error: " + e.what());
         if(ifs.is_open()) ifs.close();
        // Consider deleting the corrupt cache file here
        DeleteFileA(cacheFilePath.c_str());
        return false;
    }
}

bool SaveMetadataToCache(const SteamGame& game) {
    if (game.appid.empty()) {
        Log("SaveMetadataToCache: Cannot save cache for game with empty appid.");
        return false;
    }
     // Don't save if essential data is missing (prevents saving empty cache)
    if (game.description.empty() && game.headerImage.empty()) {
         Log("SaveMetadataToCache: Skipping save for appid " + game.appid + " - no data to cache.");
         return false;
    }

    std::string cacheFilePath = GetMetadataCachePath(game.appid);
    json j;
    j["description"] = game.description;
    j["headerImage"] = game.headerImage;
    // Add timestamp? j["cached_at"] = time(nullptr);

    std::ofstream ofs; // Declare outside try
    try {
        ofs.open(cacheFilePath); // Open inside try
        if (!ofs.is_open()) {
             Log("SaveMetadataToCache: Failed to open cache file for writing: " + cacheFilePath + ". Error code: " + std::to_string(GetLastError()));
            return false;
        }
        ofs << j.dump(4); // Write pretty-printed JSON
        if (!ofs.good()) { // Check stream state after writing
             Log("SaveMetadataToCache: Failed during writing to cache file: " + cacheFilePath);
             ofs.close(); // Attempt to close
             DeleteFileA(cacheFilePath.c_str()); // Delete potentially corrupt file
             return false;
        }
        ofs.close(); // Close file after writing
        Log("SaveMetadataToCache: Successfully saved metadata cache for appid " + game.appid);
        return true;
    } catch (const json::exception& e) { // Catch nlohmann::json specific exceptions
        Log("SaveMetadataToCache: JSON Exception writing cache file: " + cacheFilePath + ". Error: " + e.what());
        if (ofs.is_open()) ofs.close();
         DeleteFileA(cacheFilePath.c_str()); // Delete potentially corrupt file
        return false;
    } catch (const std::exception& e) { // Catch standard exceptions (e.g., std::ofstream errors)
        Log("SaveMetadataToCache: Standard Exception writing cache file: " + cacheFilePath + ". Error: " + e.what());
         if (ofs.is_open()) ofs.close();
         DeleteFileA(cacheFilePath.c_str()); // Delete potentially corrupt file
        return false;
    }
}

bool LoadImageBytesFromCache(const std::string& cachePath, std::vector<uint8_t>& outBytes) {
    outBytes.clear();
    std::ifstream ifs(cachePath, std::ios::binary | std::ios::ate); // Open in binary mode and position at end
    if (!ifs.is_open()) {
         // Log("LoadImageBytesFromCache: Image cache file not found: " + cachePath); // Optional log
        return false;
    }

    std::streamsize size = ifs.tellg();
    if (size <= 0) {
         Log("LoadImageBytesFromCache: Image cache file is empty or invalid size: " + cachePath);
         ifs.close();
         // Consider deleting the corrupt cache file here
         DeleteFileA(cachePath.c_str());
         return false; // Empty file
    }
    ifs.seekg(0, std::ios::beg); // Go back to the beginning

    outBytes.resize(size);
    if (ifs.read(reinterpret_cast<char*>(outBytes.data()), size)) {
        ifs.close();
        Log("LoadImageBytesFromCache: Successfully loaded " + std::to_string(size) + " bytes from image cache: " + cachePath);
        return true;
    } else {
        Log("LoadImageBytesFromCache: Failed to read bytes from image cache file: " + cachePath);
        ifs.close();
        outBytes.clear(); // Clear partial data on read error
        // Consider deleting the corrupt cache file here
        DeleteFileA(cachePath.c_str());
        return false;
    }
}

bool SaveImageBytesToCache(const std::string& cachePath, const std::vector<uint8_t>& bytes) {
    if (bytes.empty()) {
        Log("SaveImageBytesToCache: Attempted to save empty byte vector to: " + cachePath);
        return false; // Don't save empty files
    }
    std::ofstream ofs; // Declare outside try
    try {
        ofs.open(cachePath, std::ios::binary | std::ios::trunc); // Open inside try
        if (!ofs.is_open()) {
            Log("SaveImageBytesToCache: Failed to open image cache file for writing: " + cachePath + ". Error code: " + std::to_string(GetLastError()));
            return false;
        }

        ofs.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        if (!ofs.good()) { // Check stream state after writing
            Log("SaveImageBytesToCache: Failed during writing bytes to image cache file: " + cachePath);
            ofs.close();
             DeleteFileA(cachePath.c_str()); // Delete potentially corrupt file
            return false;
        }

        ofs.close();
        Log("SaveImageBytesToCache: Successfully saved " + std::to_string(bytes.size()) + " bytes to image cache: " + cachePath);
        return true;
     } catch (const std::exception& e) { // Catch standard exceptions (e.g., std::ofstream errors)
        Log("SaveImageBytesToCache: Standard Exception writing cache file: " + cachePath + ". Error: " + e.what());
         if (ofs.is_open()) ofs.close();
         DeleteFileA(cachePath.c_str()); // Delete potentially corrupt file
        return false;
    }
} 