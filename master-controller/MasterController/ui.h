// <ai_context> This file declares all UI-related functions, including the WndProc and ListView handling </ai_context>

#pragma once

#include <commctrl.h> // For ListView macros if needed

// We do NOT #include <windows.h> directly here because globals.h already has it.
// If we truly need "windows.h" specifics, we could do #include "globals.h" here or rely on the .cpp that includes both.

struct StationInfo; // forward declaration

// Window procedure
LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);

// Bubble UI constants
#define BUBBLE_WIDTH 160
#define BUBBLE_HEIGHT 70  // Reduced from 90 to make bubbles more compact
#define BUBBLE_MARGIN 10
#define BUBBLE_AREA_X 20
#define BUBBLE_AREA_Y 70
#define BUBBLE_AREA_WIDTH 800   // Increased from 600 for more space
#define BUBBLE_AREA_HEIGHT 550  // Increased from 400 for more space

// Station visualization
void DrawStationBubbles(HDC hdc);
void InitializeStationBubblePositions();
int FindStationBubbleAt(int x, int y);
void HandleStationSelection(int x, int y, bool isCtrlPressed);
void MoveSelectedStations(int deltaX, int deltaY);
void StartBubbleMultiSelection(int x, int y);
void UpdateBubbleMultiSelection(int x, int y);
void EndBubbleMultiSelection();

// Tooltip support
void InitializeTooltip(HWND hWnd);
void UpdateStationTooltip(int x, int y);

// Selection rectangle
extern bool g_isSelecting;
extern POINT g_selectionStart;
extern POINT g_selectionEnd;

// Custom time controls
extern HWND g_hCustomTimeEdit;
extern HWND g_hBtnSetCustomTime;
extern HWND g_hGroupInfoLabel;

// ListView columns / rows (legacy functions)
void InitListViewColumns();
int  AddStationRow(const StationInfo& st);
int  AddStationToListViewOnly(const StationInfo& st);
void UpdateStationRow(int index);
void RemoveStationRow(SOCKET sock);

// UI updates
void UpdateUI();
void ReorderStationsAscending();

// Custom time functions
void SetCustomTimeForSelectedStations();

// Custom draw for list color
LRESULT OnListViewCustomDraw(LPNMLVCUSTOMDRAW nmcd);

// Grouping logic
void SetSelectedStationsGroup(int groupId);
void RemoveSelectedStationsGroup();
int FindFirstUnusedGroupId();
int CheckBubbleCollision(int stationIndex);

// Commands
void SendCommandToSelectedStation(const std::string& cmd);