// <ai_context> This file defines the implementations of utility functions </ai_context>

#include "globals.h"  // This brings in all Windows headers
#include "util.h"
#include <tlhelp32.h>
#include <mutex>
#include <cstdio>
#include <fstream>
#include <sstream>
#include "station.h"

/////////////////////////////////////////////////////////
// Helpers & Logging
/////////////////////////////////////////////////////////
void LogMessage(const std::string& msg)
{
    std::lock_guard<std::mutex> lock(g_logMutex);
    OutputDebugStringA((msg + "\n").c_str());
}

std::string FormatTime(int sec)
{
    if (sec <= 0) return "0:00";
    int m = sec / 60;
    int s = sec % 60;
    char buf[32];
    sprintf_s(buf, "%d:%02d", m, s);
    return std::string(buf);
}

/////////////////////////////////////////////////////////
// KillExistingProcesses
/////////////////////////////////////////////////////////
void KillExistingProcesses(const std::wstring& exeName)
{
    DWORD currentPID = GetCurrentProcessId();

    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return;

    PROCESSENTRY32W pe;
    pe.dwSize = sizeof(pe);

    if (Process32FirstW(snap, &pe))
    {
        do
        {
            std::wstring procName = pe.szExeFile;
            if (_wcsicmp(procName.c_str(), exeName.c_str()) == 0)
            {
                if (pe.th32ProcessID != currentPID)
                {
                    HANDLE hProc = OpenProcess(PROCESS_TERMINATE, FALSE, pe.th32ProcessID);
                    if (hProc)
                    {
                        TerminateProcess(hProc, 0);
                        CloseHandle(hProc);
                    }
                }
            }
        } while (Process32NextW(snap, &pe));
    }

    CloseHandle(snap);
}

/////////////////////////////////////////////////////////
// Station Group Persistence
/////////////////////////////////////////////////////////
void SaveStationGroups()
{
    std::ofstream file("station_groups.txt");
    if (!file.is_open()) {
        LogMessage("Failed to open station_groups.txt for writing");
        return;
    }

    for (const auto& station : g_stations) {
        // Save all stations, including positions and group information
        file << station.stationName << "\t" 
             << station.groupId << "\t"
             << station.xPos << "\t" 
             << station.yPos << std::endl;
    }
    
    file.close();
    LogMessage("Saved station groups and positions to station_groups.txt");
}

void LoadStationGroups()
{
    std::ifstream file("station_groups.txt");
    if (!file.is_open()) {
        LogMessage("No saved station groups found");
        return;
    }

    g_stationGroups.clear();
    // Also create a map for positions
    std::unordered_map<std::string, std::pair<int, int>> stationPositions;
    
    std::string line;
    while (std::getline(file, line)) {
        std::istringstream iss(line);
        std::string stationName;
        int groupId, xPos = 0, yPos = 0;
        
        if (std::getline(iss, stationName, '\t') && 
            iss >> groupId && 
            iss.ignore() && iss >> xPos &&
            iss.ignore() && iss >> yPos) {
            
            // Save group
            g_stationGroups[stationName] = groupId;
            
            // Save position
            stationPositions[stationName] = std::make_pair(xPos, yPos);
            
            LogMessage("Loaded group " + std::to_string(groupId) + 
                      " and position (" + std::to_string(xPos) + "," + 
                      std::to_string(yPos) + ") for station " + stationName);
        }
    }
    
    // Store positions in a global for use during reconnection
    g_stationPositions = std::move(stationPositions);
    
    file.close();
    LogMessage("Loaded station groups and positions from station_groups.txt");
}