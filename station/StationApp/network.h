#ifndef NETWORK_H
#define NETWORK_H

// <ai_context>
// Networking declarations
// </ai_context>

#include "globals.h"
#include <vector>
#include <cstdint>

void ConnectToMaster(const std::string& ip, unsigned short port);
void DisconnectFromMaster();
void RecvThreadProc();
void ParseCommand(const std::string& cmd);
void SendToMaster(const std::string& msg);
void SendMessageToServer(const std::string& message);
void ConnectToMasterAsync(const std::string& ip, unsigned short port);

// New function to download binary data (like images)
std::vector<uint8_t> HttpDownloadToVector(const std::wstring& server, const std::wstring& path, bool secure);

// New function to download string data
std::string HttpGetToString(const std::wstring& server, const std::wstring& path, bool secure);

// Function to parse Steam API JSON (declaration)
bool ParseSteamApiResponse(const std::string& response, SteamGame& game);

#endif