// <ai_context> This file defines the StationInfo struct and related station logic </ai_context>

#pragma once

#include <string>
#include <vector>
#include <winsock2.h>

// We'll have up to 7 group "slots" plus a "no group" slot:
enum {
    GROUP_NONE = 0,
    GROUP_1,
    GROUP_2,
    GROUP_3,
    GROUP_4,
    GROUP_5,
    GROUP_6,
    GROUP_7
};

// For coloring the list-view rows, each StationInfo stores its groupId
struct StationInfo
{
    SOCKET      sock;
    std::string stationName;
    std::string ip;
    unsigned short port;
    bool sessionActive;
    int  timeLeftSec;
    int  groupId;
    // Position for bubble UI
    int  xPos;
    int  yPos;
    bool isSelected;
};

extern std::vector<StationInfo> g_stations;

// Station utilities
int FindStationIndexBySock(SOCKET sock);