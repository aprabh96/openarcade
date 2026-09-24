#ifndef WINDOW_PROC_H
#define WINDOW_PROC_H

// <ai_context>
// Declaration of WndProc
// </ai_context>

#include "globals.h"

#define WM_APP_GAME_DATA_READY (WM_APP + 100)
#define WM_APP_CACHE_SAVE_DONE (WM_APP + 101)
#define WM_APP_LOG_MESSAGE     (WM_APP + 102)
#define WM_APP_ENSURE_OVERLAY_VISIBLE (WM_APP + 103)
#define WM_APP_HANDLE_RECREATE_TARGET (WM_APP + 104)

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);

// Function declarations for category management functions used across files
void UpdateGameMembershipList(HWND hWndParent, int gameVectorIndex);
void UpdateCategoryControlsState(HWND hWndParent);

#endif