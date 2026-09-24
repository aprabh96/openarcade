// <ai_context> This file holds the global station array and any station-related functions </ai_context>

#include "station.h"
// Remove winsock2.h include since it comes from station.h -> globals.h chain
// #include <winsock2.h>

std::vector<StationInfo> g_stations;

int FindStationIndexBySock(SOCKET sock)
{
    for (int i = 0; i < (int)g_stations.size(); i++)
    {
        if (g_stations[i].sock == sock)
        {
            return i;
        }
    }
    return -1;
}