// <ai_context> This file declares global variables shared across the application </ai_context>

#pragma once

#include "framework.h"  // This brings in all Windows headers in correct order
#include <mutex>
#include <unordered_map>  // Added for station group mappings
#include <utility> // for std::pair

// Forward-declare any global UI controls used throughout the program
extern HINSTANCE g_hInst;
extern HWND      g_hMainWnd;

// UI Controls
extern HWND g_hBtnStartServer;
extern HWND g_hBtnStopServer;
extern HWND g_hComboSessionMin;
extern HWND g_hBtnStartSession;
extern HWND g_hBtnStopSession;
extern HWND g_hBtnAdd1;
extern HWND g_hBtnAdd5;
extern HWND g_hBtnAdd10;
extern HWND g_hBtnAdd15;
extern HWND g_hBtnAdd30;
extern HWND g_hBtnAdd60;
extern HWND g_hBtnGroupColor[7];
extern HWND g_hBtnRemoveGroup;
extern HWND g_hLblServerStatus;
extern HWND g_hListView;

// Custom time controls
extern HWND g_hCustomTimeEdit;
extern HWND g_hBtnSetCustomTime;
extern HWND g_hGroupInfoLabel;

// Bubble UI state
extern bool g_isSelecting;
extern bool g_isDragging;
extern POINT g_selectionStart;
extern POINT g_selectionEnd;
extern POINT g_dragStart;
extern POINT g_lastMousePos;
extern int g_draggedStationIndex;

// Socket/server globals
extern SOCKET g_listenSocket;
extern bool   g_serverRunning;
extern HANDLE g_serverThreadHandle;
extern DWORD  g_serverThreadId;

// Synchronization
extern std::mutex g_logMutex;

// Station group persistence
extern std::unordered_map<std::string, int> g_stationGroups;

// Station positions by station name (for persistence)
extern std::unordered_map<std::string, std::pair<int, int>> g_stationPositions;