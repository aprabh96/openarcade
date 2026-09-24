#include <string>
#include <vector>
#ifndef STEAM_API_H
#define STEAM_API_H

#include "steam_games.h" // Include for SteamGame definition

// Helper to replace "\\/" with "/" in JSON strings
void FixEscapedSlashes(std::string& s);

// Makes an HTTP GET request using WinHTTP and returns the body as a string
std::string HttpGetToString(const std::wstring& host, const std::wstring& path, bool forceTLS12 = true);

// Parses description and header image URL from Steam store API JSON response
// Returns true if successful, false otherwise
bool ParseSteamApiResponse(const std::string& jsonResponse, SteamGame& game);

// --- Caching Functions ---
bool LoadMetadataFromCache(const std::string& appid, SteamGame& game);
bool SaveMetadataToCache(const SteamGame& game);
bool LoadImageBytesFromCache(const std::string& cachePath, std::vector<uint8_t>& outBytes);
bool SaveImageBytesToCache(const std::string& cachePath, const std::vector<uint8_t>& bytes);
std::string GetFilenameFromUrl(const std::string& url);
std::string GetCachePathForImage(const std::string& appid, const std::string& imageUrl);

#endif // STEAM_API_H 