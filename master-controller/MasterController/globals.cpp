// <ai_context> This file defines all globals for the application </ai_context>

#include "globals.h"

// Server related
bool g_serverRunning = false;
SOCKET g_listenSocket = INVALID_SOCKET;
HANDLE g_serverThreadHandle = NULL;
DWORD g_serverThreadId = 0;

// MainWindow
HINSTANCE g_hInst;
HWND g_hMainWnd;

// For ListView
HWND g_hListView;

// Log
std::mutex g_logMutex;
HWND g_hEditLog;

// Drag support
bool g_isDragging = false;
int g_draggedStationIndex = -1;
POINT g_dragStart = { 0, 0 };
POINT g_lastMousePos = { 0, 0 };

// Selection rectangle
bool g_isSelecting = false;
POINT g_selectionStart = { 0, 0 };
POINT g_selectionEnd = { 0, 0 };

// For controls
HWND g_hBtnStartServer;
HWND g_hBtnStopServer;
HWND g_hLblServerStatus;
HWND g_hComboSessionMin;
HWND g_hBtnStartSession;
HWND g_hBtnStopSession;
HWND g_hBtnAdd1;
HWND g_hBtnAdd5;
HWND g_hBtnAdd10;
HWND g_hBtnAdd15;
HWND g_hBtnAdd30;
HWND g_hBtnAdd60;
HWND g_hBtnGroupColor[7];
HWND g_hBtnRemoveGroup;
HWND g_hCustomTimeEdit;
HWND g_hBtnSetCustomTime;
HWND g_hGroupInfoLabel;

// Group assignments by station name (for persistence)
std::unordered_map<std::string, int> g_stationGroups;

// Station positions by station name (for persistence)
std::unordered_map<std::string, std::pair<int, int>> g_stationPositions;