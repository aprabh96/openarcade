#include "logging.h"
#include <iostream>
#include <fstream>
#include <vector>
#include <deque>
#include <mutex>
#include <string>
#include <windows.h>
#include "window_proc.h"

// <ai_context>
// Implementation of Log function
// </ai_context>

// Maximum number of lines to keep in the log window
const int MAX_LOG_LINES = 100;  // Changed to 100 lines as requested
// Maximum log file size (50MB in bytes)
const size_t MAX_LOG_FILE_SIZE = 50 * 1024 * 1024;

// Store recent log lines in memory for efficient window updates (accessed only by UI thread now)
std::deque<std::string> recent_logs;

std::mutex g_logWindowMutex;

void Log(const std::string& msg)
{
    time_t now = time(nullptr);
    struct tm t;
    localtime_s(&t, &now);
    char timeBuf[64];
    strftime(timeBuf, sizeof(timeBuf), "[%Y-%m-%d %H:%M:%S] ", &t);

    std::string formatted_line = std::string(timeBuf) + msg + "\r\n";
    
    // Handle log file with size limit (thread-safe with g_netMutex)
    {
        std::lock_guard<std::mutex> lock(g_netMutex);
        logFile.flush(); 
        size_t currentSize = logFile.tellp();
        if (currentSize > MAX_LOG_FILE_SIZE) {
            logFile.close();
            logFile.open(g_logFilePath, std::ios::out | std::ios::trunc);
            logFile << "[" << timeBuf << "] Log file truncated (exceeded 50MB limit)\r\n";
        }
        logFile << formatted_line;
        logFile.flush();
    }

    // For UI logging, post a message to the main thread
    if (g_hEditLog)
    {
        HWND hMainWnd = GetParent(g_hEditLog); 
        if (!hMainWnd) {
            hMainWnd = FindWindowW(L"QuitVRAppClass", NULL);
        }
        if (hMainWnd) {
            std::string* logLineCopy = new std::string(formatted_line);
            if (!PostMessage(hMainWnd, WM_APP_LOG_MESSAGE, 0, (LPARAM)logLineCopy)) {
                delete logLineCopy;
                OutputDebugStringA(("Failed to post log message: " + formatted_line).c_str());
            }
        } else {
            OutputDebugStringA(("Cannot find main window for UI log: " + formatted_line).c_str());
        }
    }
}