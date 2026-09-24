#include "network.h"
#include "logging.h"
#include "session.h"
#include "overlay.h" // Added this include to fix HideOverlayContinuous call
#include <winhttp.h>
#pragma comment(lib, "winhttp.lib")
#include <vector>
#include <cstdint> // For uint8_t (often used as BYTE)
#include <string>
#include "steam_games.h"

// <ai_context>
// Implementation of networking code
// </ai_context>

void ConnectToMaster(const std::string& ip, unsigned short port)
{
    if (g_connected)
    {
        Log("ConnectToMaster -> already connected, skip.");
        return;
    }

    // Save current overlay state before attempting connection
    bool wasOverlayRunning = continuousOverlayRunning;

    // If we have an existing socket, close it first
    if (g_clientSocket != INVALID_SOCKET)
    {
        closesocket(g_clientSocket);
        g_clientSocket = INVALID_SOCKET;
    }

    // Initialize WSA once only
    if (!g_wsaInitialized)
    {
        WSADATA wsaData;
        int iResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
        if (iResult != 0)
        {
            Log("WSAStartup failed -> " + std::to_string(iResult));
            return;
        }
        g_wsaInitialized = true;
    }

    g_clientSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (g_clientSocket == INVALID_SOCKET)
    {
        Log("socket() failed -> " + std::to_string(WSAGetLastError()));
        return;
    }

    sockaddr_in srv;
    ZeroMemory(&srv, sizeof(srv));
    srv.sin_family = AF_INET;
    srv.sin_port = htons(port);
    inet_pton(AF_INET, ip.c_str(), &srv.sin_addr);

    int iResult = connect(g_clientSocket, (sockaddr*)&srv, sizeof(srv));
    if (iResult == SOCKET_ERROR)
    {
        Log("connect() failed -> " + std::to_string(WSAGetLastError()));
        closesocket(g_clientSocket);
        g_clientSocket = INVALID_SOCKET;
        return;
    }

    g_connected = true;
    Log("ConnectToMaster -> connected to " + ip + ":" + std::to_string(port));

    // Start the receive thread if it's not already running
    if (!g_runRecvThread)
    {
        g_runRecvThread = true;
        g_recvThread = std::thread(RecvThreadProc);
    }

    if (!g_stationName.empty())
    {
        std::string cmd = "STATION_NAME " + g_stationName;
        SendToMaster(cmd);
    }
    
    // Restore overlay if it was running before
    if (wasOverlayRunning && !continuousOverlayRunning && !g_SessionRunning)
    {
        Log("Restoring overlay state after connection");
        HWND mainWnd = FindWindowW(L"QuitVRAppClass", NULL);
        if (mainWnd)
        {
            // Use PostMessage to avoid blocking the connection thread
            PostMessageW(mainWnd, WM_COMMAND, 3, 0); // 3 is the command ID for ShowOverlayContinuous
        }
    }
}

void DisconnectFromMaster()
{
    if (!g_connected)
        return;

    // Explicitly notify the master we're disconnecting if possible
    if (g_clientSocket != INVALID_SOCKET)
    {
        const char* msg = "STATION_DISCONNECTING";
        send(g_clientSocket, msg, (int)strlen(msg), 0);
        Log("Sent disconnect notification to master");
        
        // Give a brief moment for the message to be sent
        Sleep(100);
    }
    
    g_connected = false;
    
    // Safe shutdown of the socket
    if (g_clientSocket != INVALID_SOCKET)
    {
        shutdown(g_clientSocket, SD_BOTH);
        closesocket(g_clientSocket);
        g_clientSocket = INVALID_SOCKET;
    }
    
    // Stop the receive thread
    g_runRecvThread = false;
    if (g_recvThread.joinable())
        g_recvThread.join();

    Log("Disconnected from master.");
}

void RecvThreadProc()
{
    char buffer[512];
    
    // Track when we started and the overlay state
    bool wasOverlayRunning = continuousOverlayRunning;
    
    while (g_runRecvThread)
    {
        // If socket is invalid but we should be running, try to get a new socket
        if (g_clientSocket == INVALID_SOCKET)
        {
            // Sleep to prevent tight CPU loop
            Sleep(1000);
            continue;
        }
        
        // Only receive if we have a valid socket
        ZeroMemory(buffer, sizeof(buffer));
        int bytes = recv(g_clientSocket, buffer, sizeof(buffer) - 1, 0);
        
        if (bytes <= 0)
        {
            Log("Master connection lost. Closing socket.");
            
            // Remember the overlay state
            wasOverlayRunning = continuousOverlayRunning;
            
            // Socket error or closed - just close the socket
            // Note: Don't attempt to reconnect here - let the timer handle reconnection
            if (g_clientSocket != INVALID_SOCKET)
            {
                closesocket(g_clientSocket);
                g_clientSocket = INVALID_SOCKET;
            }
            
            g_connected = false;
            
            // Check if overlay timer needs to be restarted
            if (wasOverlayRunning && !continuousOverlayRunning && !g_SessionRunning)
            {
                Log("Attempting to restore overlay after connection loss");
                HWND mainWnd = FindWindowW(L"QuitVRAppClass", NULL);
                if (mainWnd)
                {
                    // Use PostMessage to avoid blocking
                    PostMessageW(mainWnd, WM_COMMAND, 3, 0); // 3 is ShowOverlayContinuous
                }
            }
            
            // Continue the loop - don't attempt to reconnect here
            continue;
        }
        
        // Process received data
        buffer[bytes] = '\0';
        std::string cmd(buffer);
        Log("[From Master] " + cmd);
        
        ParseCommand(cmd);
    }
    
    Log("Receive thread exiting.");
}

void ParseCommand(const std::string& cmd)
{
    if (cmd.rfind("START_SESSION", 0) == 0)
    {
        float minutes = 0;
        sscanf_s(cmd.c_str(), "START_SESSION %f", &minutes);

        HWND mainWnd = FindWindowW(L"QuitVRAppClass", NULL);
        if (mainWnd && minutes > 0)
        {
            // Convert minutes to seconds, handling the decimal part
            int seconds = (int)(minutes * 60 + 0.5); // Adding 0.5 to round to nearest second
            StartSessionTimer(mainWnd, seconds);
            Log(">>> MASTER says START_SESSION " + std::to_string(minutes) + " (converted to " + std::to_string(seconds) + " seconds)");
            // Ensure the overlay is shown *after* starting the timer
            PostMessageW(mainWnd, WM_COMMAND, 3, 0); // 3 is ShowOverlayContinuous
        }
    }
    else if (cmd.rfind("STOP_SESSION", 0) == 0)
    {
        HWND mainWnd = FindWindowW(L"QuitVRAppClass", NULL);
        if (mainWnd)
        {
            StopSession(mainWnd);
            Log(">>> MASTER says STOP_SESSION");
        }
    }
    else if (cmd.rfind("ADD_TIME", 0) == 0)
    {
        float addMin = 0;
        sscanf_s(cmd.c_str(), "ADD_TIME %f", &addMin);

        if (addMin > 0 && g_SessionRunning)
        {
            // Convert minutes to seconds, handling the decimal part
            int addSeconds = (int)(addMin * 60 + 0.5); // Adding 0.5 to round to nearest second
            // Add time as minutes and seconds
            AddTimeToSession(addSeconds / 60, addSeconds % 60);
            Log(">>> MASTER says ADD_TIME " + std::to_string(addMin) + " (converted to " + std::to_string(addSeconds) + " seconds)");
        }
        else
        {
            Log(">>> MASTER says ADD_TIME but no session or invalid minutes");
        }
    }
    else
    {
        Log("Unknown MASTER command: " + cmd);
    }
}

void SendToMaster(const std::string& msg)
{
    if (!g_connected || g_clientSocket == INVALID_SOCKET)
    {
        Log("SendToMaster -> not connected, skip.");
        return;
    }

    int iResult = send(g_clientSocket, msg.c_str(), (int)msg.size(), 0);
    if (iResult == SOCKET_ERROR)
    {
        Log("send() failed -> " + std::to_string(WSAGetLastError()));
        
        // Just close the socket - the recv thread or timer will handle reconnection
        if (g_clientSocket != INVALID_SOCKET)
        {
            closesocket(g_clientSocket);
            g_clientSocket = INVALID_SOCKET;
        }
        g_connected = false;
    }
    else
    {
        Log("[To Master] " + msg);
    }
}

void SendMessageToServer(const std::string& message)
{
    // Placeholder function, implement as needed
    Log("SendMessageToServer called with: " + message);
}

// Add new function to network.cpp
void ConnectToMasterAsync(const std::string& ip, unsigned short port)
{
    // Launch a new thread to handle the connection
    std::thread connectThread([ip, port]() {
        ConnectToMaster(ip, port);
    });
    
    // Detach the thread so it runs independently
    connectThread.detach();
    Log("Async connection attempt started");
}

// New function to download binary data (like images) to a vector
std::vector<uint8_t> HttpDownloadToVector(const std::wstring& server, const std::wstring& path, bool secure) {
    std::vector<uint8_t> downloadedData;
    DWORD dwSize = 0;
    DWORD dwDownloaded = 0;
    LPSTR pszOutBuffer;
    BOOL bResults = FALSE;
    HINTERNET hSession = NULL, hConnect = NULL, hRequest = NULL;

    Log("HttpDownloadToVector: Starting download from " + WStringToString(server) + WStringToString(path));

    // Use WinHttpOpen to obtain a session handle.
    hSession = WinHttpOpen(L"ArcadeStation/1.0",
                            WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                            WINHTTP_NO_PROXY_NAME,
                            WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) {
        Log("HttpDownloadToVector: WinHttpOpen failed. Error: " + std::to_string(GetLastError()));
        return downloadedData;
    }

    // Specify an HTTP server.
    USHORT port = secure ? INTERNET_DEFAULT_HTTPS_PORT : INTERNET_DEFAULT_HTTP_PORT;
    hConnect = WinHttpConnect(hSession, server.c_str(), port, 0);
    if (!hConnect) {
        Log("HttpDownloadToVector: WinHttpConnect failed. Error: " + std::to_string(GetLastError()));
        WinHttpCloseHandle(hSession);
        return downloadedData;
    }

    // Create an HTTP request handle.
    hRequest = WinHttpOpenRequest(hConnect, L"GET", path.c_str(),
                                  NULL, WINHTTP_NO_REFERER,
                                  WINHTTP_DEFAULT_ACCEPT_TYPES,
                                  secure ? WINHTTP_FLAG_SECURE : 0);
    if (!hRequest) {
        Log("HttpDownloadToVector: WinHttpOpenRequest failed. Error: " + std::to_string(GetLastError()));
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return downloadedData;
    }

    // Send the request.
    bResults = WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                  WINHTTP_NO_REQUEST_DATA, 0, 0, 0);
    if (!bResults) {
        Log("HttpDownloadToVector: WinHttpSendRequest failed. Error: " + std::to_string(GetLastError()));
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return downloadedData;
    }

    // Ensure request completed.
    bResults = WinHttpReceiveResponse(hRequest, NULL);
    if (!bResults) {
        Log("HttpDownloadToVector: WinHttpReceiveResponse failed. Error: " + std::to_string(GetLastError()));
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return downloadedData;
    }

    // Check HTTP status code
    DWORD dwStatusCode = 0;
    DWORD dwSizeOfStatusCode = sizeof(dwStatusCode);
    WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                        WINHTTP_HEADER_NAME_BY_INDEX, &dwStatusCode,
                        &dwSizeOfStatusCode, WINHTTP_NO_HEADER_INDEX);

    if (dwStatusCode != HTTP_STATUS_OK) {
         Log("HttpDownloadToVector: HTTP request failed with status code: " + std::to_string(dwStatusCode));
         WinHttpCloseHandle(hRequest);
         WinHttpCloseHandle(hConnect);
         WinHttpCloseHandle(hSession);
         return downloadedData; // Return empty vector on non-200 status
    }

    // Keep checking for data until there is nothing left.
    do {
        // Check for available data.
        dwSize = 0;
        if (!WinHttpQueryDataAvailable(hRequest, &dwSize)) {
            Log("HttpDownloadToVector: WinHttpQueryDataAvailable failed. Error: " + std::to_string(GetLastError()));
            bResults = FALSE; // Signal error
            break;
        }

        // If no data available, break loop
        if (dwSize == 0)
            break;

        // Allocate space for the buffer.
        pszOutBuffer = new char[dwSize + 1];
        if (!pszOutBuffer) {
            Log("HttpDownloadToVector: Out of memory allocating buffer.");
            dwSize = 0;
            bResults = FALSE; // Signal error
            break;
        }

        // Read the data.
        ZeroMemory(pszOutBuffer, dwSize + 1);
        if (!WinHttpReadData(hRequest, (LPVOID)pszOutBuffer, dwSize, &dwDownloaded)) {
            Log("HttpDownloadToVector: WinHttpReadData failed. Error: " + std::to_string(GetLastError()));
            delete[] pszOutBuffer;
            bResults = FALSE; // Signal error
            break;
        }

        // Append the downloaded data to our vector
        downloadedData.insert(downloadedData.end(), (uint8_t*)pszOutBuffer, (uint8_t*)pszOutBuffer + dwDownloaded);

        delete[] pszOutBuffer;

    } while (dwSize > 0);


    // Report any errors.
    if (!bResults)
        Log("HttpDownloadToVector: An error occurred during download read loop.");

    // Close any open handles.
    if (hRequest) WinHttpCloseHandle(hRequest);
    if (hConnect) WinHttpCloseHandle(hConnect);
    if (hSession) WinHttpCloseHandle(hSession);

    Log("HttpDownloadToVector: Download finished. Total bytes received: " + std::to_string(downloadedData.size()));
    return downloadedData;
}

// New function to download HTTP content to a string
std::string HttpGetToString(const std::wstring& server, const std::wstring& path, bool secure) {
    std::vector<uint8_t> data = HttpDownloadToVector(server, path, secure);
    if (data.empty()) {
        return "";
    }
    // Convert vector<uint8_t> to string
    return std::string(data.begin(), data.end());
}