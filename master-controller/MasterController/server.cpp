// <ai_context> This file implements the server logic, including the server thread and client handling </ai_context>

#include "globals.h"  // This brings in all Windows headers
#include "server.h"
#include <thread>
#include "station.h"
#include "util.h"
#include "ui.h"

/////////////////////////////////////////////////////////
// Start/Stop Server
/////////////////////////////////////////////////////////
DWORD WINAPI ServerThreadProc(LPVOID lpParam)
{
    WSADATA wsaData;
    int iRes = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (iRes != 0)
    {
        LogMessage("WSAStartup failed: " + std::to_string(iRes));
        return 0;
    }

    g_listenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (g_listenSocket == INVALID_SOCKET)
    {
        LogMessage("socket() failed: " + std::to_string(WSAGetLastError()));
        WSACleanup();
        return 0;
    }

    unsigned short port = (unsigned short)(ULONG_PTR)lpParam;
    sockaddr_in srv;
    ZeroMemory(&srv, sizeof(srv));
    srv.sin_family = AF_INET;
    srv.sin_addr.s_addr = INADDR_ANY;
    srv.sin_port = htons(port);

    iRes = bind(g_listenSocket, (sockaddr*)&srv, sizeof(srv));
    if (iRes == SOCKET_ERROR)
    {
        LogMessage("bind failed: " + std::to_string(WSAGetLastError()));
        closesocket(g_listenSocket);
        g_listenSocket = INVALID_SOCKET;
        WSACleanup();
        return 0;
    }

    iRes = listen(g_listenSocket, SOMAXCONN);
    if (iRes == SOCKET_ERROR)
    {
        LogMessage("listen failed: " + std::to_string(WSAGetLastError()));
        closesocket(g_listenSocket);
        g_listenSocket = INVALID_SOCKET;
        WSACleanup();
        return 0;
    }

    LogMessage("Server listening on port " + std::to_string(port) + ".");

    while (g_serverRunning)
    {
        sockaddr_in clientAddr;
        int clientSize = sizeof(clientAddr);
        SOCKET clientSock = accept(g_listenSocket, (sockaddr*)&clientAddr, &clientSize);
        if (clientSock == INVALID_SOCKET)
        {
            if (!g_serverRunning) break;
            int err = WSAGetLastError();
            LogMessage("accept failed: " + std::to_string(err));
            break;
        }

        char ipBuf[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &clientAddr.sin_addr, ipBuf, sizeof(ipBuf));
        unsigned short cPort = ntohs(clientAddr.sin_port);

        std::thread(HandleClient, clientSock, std::string(ipBuf), cPort).detach();
    }

    closesocket(g_listenSocket);
    g_listenSocket = INVALID_SOCKET;
    WSACleanup();
    LogMessage("ServerThreadProc exiting.");
    return 0;
}

void StartServer(unsigned short port)
{
    if (g_serverRunning)
    {
        LogMessage("Server already running.");
        return;
    }
    g_serverRunning = true;

    g_serverThreadHandle = CreateThread(nullptr, 0, ServerThreadProc, (LPVOID)(ULONG_PTR)port, 0, &g_serverThreadId);
    if (!g_serverThreadHandle)
    {
        LogMessage("CreateThread failed!");
        g_serverRunning = false;
        return;
    }

    LogMessage("Server thread started.");
    UpdateUI();
}

void StopServer()
{
    if (!g_serverRunning) return;

    g_serverRunning = false;
    if (g_listenSocket != INVALID_SOCKET)
    {
        closesocket(g_listenSocket);
        g_listenSocket = INVALID_SOCKET;
    }

    for (auto& st : g_stations)
    {
        if (st.sock != INVALID_SOCKET)
        {
            closesocket(st.sock);
            st.sock = INVALID_SOCKET;
        }
    }
    g_stations.clear();

    if (g_serverThreadHandle)
    {
        WaitForSingleObject(g_serverThreadHandle, 2000);
        CloseHandle(g_serverThreadHandle);
        g_serverThreadHandle = NULL;
    }

    LogMessage("Server stopped.");
    UpdateUI();
}

/////////////////////////////////////////////////////////
// HandleClient
/////////////////////////////////////////////////////////
void HandleClient(SOCKET clientSock, std::string ipStr, unsigned short port)
{
    StationInfo st;
    st.sock          = clientSock;
    st.ip            = ipStr;
    st.port          = port;
    st.sessionActive = false;
    st.timeLeftSec   = 0;
    st.groupId       = GROUP_NONE;
    st.xPos          = 0;  // Will be initialized later based on saved data or by AddStationRow
    st.yPos          = 0;  // Will be initialized later based on saved data or by AddStationRow
    st.isSelected    = false;

    {
        std::lock_guard<std::mutex> lock(g_logMutex);
        char tmp[32];
        sprintf_s(tmp, "Station #%d", (int)g_stations.size() + 1);
        st.stationName = tmp;

        // Check if we have any stations previously connected with this IP address
        // and restore the name if found - this handles reconnections before STATION_NAME message
        for (const auto& savedStation : g_stationGroups) {
            auto posIt = g_stationPositions.find(savedStation.first);
            if (posIt != g_stationPositions.end()) {
                // Extract IP from stationName (format might vary)
                if (savedStation.first.find(ipStr) != std::string::npos) {
                    st.stationName = savedStation.first;
                    st.groupId = savedStation.second;
                    st.xPos = posIt->second.first;
                    st.yPos = posIt->second.second;
                    LogMessage("Pre-restored station data for " + st.stationName + " based on IP match");
                    break;
                }
            }
        }

        g_stations.push_back(st);
        AddStationRow(g_stations.back());
    }

    LogMessage("Client connected from " + ipStr + ":" + std::to_string(port));

    char buffer[512];
    while (true)
    {
        ZeroMemory(buffer, sizeof(buffer));
        int bytes = recv(clientSock, buffer, sizeof(buffer) - 1, 0);
        if (bytes <= 0)
        {
            LogMessage("Client " + ipStr + " disconnected.");
            
            // Make sure we save the station's data before removing it
            int idx = FindStationIndexBySock(clientSock);
            if (idx >= 0) {
                // Save the station's data - if it has a name (not default)
                std::string stationName = g_stations[idx].stationName;
                if (!stationName.empty() && stationName.find("Station #") != 0) {
                    // Save its current position and group to our maps
                    g_stationGroups[stationName] = g_stations[idx].groupId;
                    g_stationPositions[stationName] = std::make_pair(g_stations[idx].xPos, g_stations[idx].yPos);
                    
                    // Write to disk
                    SaveStationGroups();
                    LogMessage("Saved position and group for " + stationName + " before disconnect");
                }
            }
            
            closesocket(clientSock);
            RemoveStationRow(clientSock);
            return;
        }

        buffer[bytes] = '\0';
        std::string msg(buffer);

        if (msg.rfind("STATION_NAME ", 0) == 0)
        {
            std::string name = msg.substr(13);
            int idx = FindStationIndexBySock(clientSock);
            if (idx >= 0)
            {
                bool nameChanged = (g_stations[idx].stationName != name);
                g_stations[idx].stationName = name;
                
                // Check if this station has a saved group
                auto it = g_stationGroups.find(name);
                if (it != g_stationGroups.end()) {
                    g_stations[idx].groupId = it->second;
                    LogMessage("Assigned station " + name + " to group " + std::to_string(it->second) + " based on saved data");
                }
                
                // Check if this station has a saved position
                auto posIt = g_stationPositions.find(name);
                if (posIt != g_stationPositions.end()) {
                    g_stations[idx].xPos = posIt->second.first;
                    g_stations[idx].yPos = posIt->second.second;
                    LogMessage("Restored position for station " + name);
                }
                
                // Update the row in the ListView without reordering all stations
                UpdateStationRow(idx);
                
                // Save station data if the name changed (to update mappings)
                if (nameChanged) {
                    SaveStationGroups();
                }
                
                // Just redraw to show updated bubble instead of rearranging
                InvalidateRect(g_hMainWnd, NULL, TRUE);
            }
        }
        else if (msg.rfind("SESSION_STARTED", 0) == 0)
        {
            int minutes = 0;
            if (sscanf_s(msg.c_str(), "SESSION_STARTED %d", &minutes) == 1)
            {
                int idx = FindStationIndexBySock(clientSock);
                if (idx >= 0)
                {
                    g_stations[idx].sessionActive = true;
                    g_stations[idx].timeLeftSec   = minutes * 60;
                    UpdateStationRow(idx);
                }
            }
            LogMessage("[From Station] " + msg);
        }
        else if (msg == "SESSION_STOPPED")
        {
            int idx = FindStationIndexBySock(clientSock);
            if (idx >= 0)
            {
                g_stations[idx].sessionActive = false;
                g_stations[idx].timeLeftSec   = 0;
                UpdateStationRow(idx);
            }
            LogMessage("[From Station] SESSION_STOPPED");
        }
        else if (msg.rfind("TIME_LEFT", 0) == 0)
        {
            int sec = 0;
            if (sscanf_s(msg.c_str(), "TIME_LEFT %d", &sec) == 1)
            {
                int idx = FindStationIndexBySock(clientSock);
                if (idx >= 0)
                {
                    g_stations[idx].timeLeftSec   = sec;
                    g_stations[idx].sessionActive = (sec > 0);
                    UpdateStationRow(idx);
                }
            }
        }
        else
        {
            LogMessage("[From Station] " + msg);
        }
    }
}