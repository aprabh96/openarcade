// <ai_context> This file implements UI logic, including WndProc, controls, and ListView handling </ai_context>

#include "globals.h"  // This includes <winsock2.h> and <windows.h> in correct order
#include "ui.h"
#include "station.h"
#include "server.h"
#include "util.h"

#include <set>
#include <algorithm>

// Tooltip support
HWND g_hToolTip = NULL;
TOOLINFOW g_toolInfo = { 0 };
int g_lastHoverStationIndex = -1;

/////////////////////////////////////////////////////////
// For coloring the list-view rows
/////////////////////////////////////////////////////////
static COLORREF g_groupColors[8] = {
    RGB(255,255,255),  // GROUP_NONE => White
    RGB(255, 170, 170),// GROUP_1   => Light Red
    RGB(170, 255, 170),// GROUP_2   => Light Green
    RGB(170, 170, 255),// GROUP_3   => Light Blue
    RGB(255, 255, 170),// GROUP_4   => Light Yellow
    RGB(170, 255, 255),// GROUP_5   => Light Cyan
    RGB(255, 170, 255),// GROUP_6   => Light Magenta
    RGB(255, 200, 150) // GROUP_7   => Light Orange
};

/////////////////////////////////////////////////////////
// Bubble UI Implementation
/////////////////////////////////////////////////////////
void DrawStationBubbles(HDC hdc)
{
    // Double buffering to prevent flickering
    HDC memDC = CreateCompatibleDC(hdc);
    RECT clientRect;
    GetClientRect(g_hMainWnd, &clientRect);
    HBITMAP memBitmap = CreateCompatibleBitmap(hdc, clientRect.right, clientRect.bottom);
    HBITMAP oldBitmap = (HBITMAP)SelectObject(memDC, memBitmap);
    
    // Paint the background
    HBRUSH bgClientBrush = CreateSolidBrush(GetSysColor(COLOR_BTNFACE));
    FillRect(memDC, &clientRect, bgClientBrush);
    DeleteObject(bgClientBrush);
    
    // Create background region
    RECT bubbleArea = { BUBBLE_AREA_X, BUBBLE_AREA_Y, 
                        BUBBLE_AREA_X + BUBBLE_AREA_WIDTH,
                        BUBBLE_AREA_Y + BUBBLE_AREA_HEIGHT };
    
    HBRUSH bgBrush = CreateSolidBrush(RGB(240, 240, 240));
    FillRect(memDC, &bubbleArea, bgBrush);
    DeleteObject(bgBrush);
    
    // Draw border
    HPEN borderPen = CreatePen(PS_SOLID, 1, RGB(180, 180, 180));
    HPEN oldPen = (HPEN)SelectObject(memDC, borderPen);
    Rectangle(memDC, bubbleArea.left, bubbleArea.top, bubbleArea.right, bubbleArea.bottom);
    SelectObject(memDC, oldPen);
    DeleteObject(borderPen);

    // Create a larger font for station names
    HFONT bigFont = CreateFont(
        18,                     // Height
        0,                      // Width
        0,                      // Escapement
        0,                      // Orientation
        FW_BOLD,                // Weight
        FALSE,                  // Italic
        FALSE,                  // Underline
        FALSE,                  // StrikeOut
        DEFAULT_CHARSET,        // CharSet
        OUT_OUTLINE_PRECIS,     // OutPrecision
        CLIP_DEFAULT_PRECIS,    // ClipPrecision
        CLEARTYPE_QUALITY,      // Quality
        DEFAULT_PITCH | FF_SWISS, // PitchAndFamily
        L"Arial");              // Face Name
        
    // Create a normal font for other text
    HFONT normalFont = CreateFont(
        14,                     // Height
        0,                      // Width
        0,                      // Escapement
        0,                      // Orientation
        FW_NORMAL,              // Weight
        FALSE,                  // Italic
        FALSE,                  // Underline
        FALSE,                  // StrikeOut
        DEFAULT_CHARSET,        // CharSet
        OUT_OUTLINE_PRECIS,     // OutPrecision
        CLIP_DEFAULT_PRECIS,    // ClipPrecision
        CLEARTYPE_QUALITY,      // Quality
        DEFAULT_PITCH | FF_SWISS, // PitchAndFamily
        L"Arial");              // Face Name

    // Draw each station bubble
    for (size_t i = 0; i < g_stations.size(); i++) {
        const StationInfo& station = g_stations[i];
        
        // Calculate bubble rect
        RECT bubbleRect = { 
            station.xPos, 
            station.yPos, 
            station.xPos + BUBBLE_WIDTH, 
            station.yPos + BUBBLE_HEIGHT
        };
        
        // Background color
        HBRUSH fillBrush = CreateSolidBrush(g_groupColors[station.groupId]);
        FillRect(memDC, &bubbleRect, fillBrush);
        DeleteObject(fillBrush);
        
        // Selection highlight
        if (station.isSelected) {
            HPEN selPen = CreatePen(PS_SOLID, 3, RGB(0, 120, 215));
            HGDIOBJ oldPen = SelectObject(memDC, selPen);
            HBRUSH nullBrush = (HBRUSH)GetStockObject(NULL_BRUSH);
            HGDIOBJ oldBrush = SelectObject(memDC, nullBrush);
            
            RoundRect(memDC, bubbleRect.left, bubbleRect.top, 
                     bubbleRect.right, bubbleRect.bottom, 10, 10);
            
            SelectObject(memDC, oldPen);
            SelectObject(memDC, oldBrush);
            DeleteObject(selPen);
        } else {
            // Normal border
            HPEN borderPen = CreatePen(PS_SOLID, 1, RGB(100, 100, 100));
            HGDIOBJ oldPen = SelectObject(memDC, borderPen);
            HBRUSH nullBrush = (HBRUSH)GetStockObject(NULL_BRUSH);
            HGDIOBJ oldBrush = SelectObject(memDC, nullBrush);
            
            RoundRect(memDC, bubbleRect.left, bubbleRect.top, 
                     bubbleRect.right, bubbleRect.bottom, 10, 10);
            
            SelectObject(memDC, oldPen);
            SelectObject(memDC, oldBrush);
            DeleteObject(borderPen);
        }
        
        // Station text
        SetBkMode(memDC, TRANSPARENT);
        
        // Station name (larger font)
        RECT nameRect = bubbleRect;
        nameRect.top += 5;
        nameRect.left += 5;
        
        HFONT oldFont = (HFONT)SelectObject(memDC, bigFont);
        wchar_t stationNameW[128];
        MultiByteToWideChar(CP_UTF8, 0, station.stationName.c_str(), -1, stationNameW, 128);
        DrawTextW(memDC, stationNameW, -1, &nameRect, DT_LEFT);
        
        // Switch to normal font for remaining text
        SelectObject(memDC, normalFont);
        
        // Session status
        RECT sessionRect = bubbleRect;
        sessionRect.top += 30;  // Adjusted from 40 for more compact layout
        sessionRect.left += 5;
        
        std::wstring sessionStatus = station.sessionActive ? L"Running Session" : L"No Session";
        DrawTextW(memDC, sessionStatus.c_str(), -1, &sessionRect, DT_LEFT);
        
        // Time left
        RECT timeRect = bubbleRect;
        timeRect.top += 48;  // Adjusted from 65 for more compact layout
        timeRect.left += 5;
        
        std::string formattedTime = FormatTime(station.timeLeftSec);
        std::string timeStr = station.sessionActive ? formattedTime + " left" : "Not active";
        wchar_t timeW[128];
        MultiByteToWideChar(CP_UTF8, 0, timeStr.c_str(), -1, timeW, 128);
        DrawTextW(memDC, timeW, -1, &timeRect, DT_LEFT);
        
        // Restore original font
        SelectObject(memDC, oldFont);
    }
    
    // Draw selection rectangle if active
    if (g_isSelecting) {
        // Create a dashed pen for the selection rectangle
        HPEN selPen = CreatePen(PS_DASH, 1, RGB(0, 120, 215));
        HGDIOBJ oldPen = SelectObject(memDC, selPen);
        // Using a solid color instead of semi-transparent (alpha not supported by RGB)
        HBRUSH selBrush = CreateSolidBrush(RGB(200, 220, 240));
        HGDIOBJ oldBrush = SelectObject(memDC, selBrush);
        
        RECT selRect = {
            std::min(g_selectionStart.x, g_selectionEnd.x),
            std::min(g_selectionStart.y, g_selectionEnd.y),
            std::max(g_selectionStart.x, g_selectionEnd.x),
            std::max(g_selectionStart.y, g_selectionEnd.y)
        };
        
        // Draw filled rectangle
        Rectangle(memDC, selRect.left, selRect.top, selRect.right, selRect.bottom);
        
        SelectObject(memDC, oldPen);
        SelectObject(memDC, oldBrush);
        DeleteObject(selPen);
        DeleteObject(selBrush);
    }
    
    // Copy the buffer to the screen
    BitBlt(hdc, 0, 0, clientRect.right, clientRect.bottom, memDC, 0, 0, SRCCOPY);
    
    // Clean up
    SelectObject(memDC, oldBitmap);
    DeleteObject(memBitmap);
    DeleteObject(bigFont);
    DeleteObject(normalFont);
    DeleteDC(memDC);
}

void InitializeStationBubblePositions()
{
    int x = BUBBLE_AREA_X + BUBBLE_MARGIN;
    int y = BUBBLE_AREA_Y + BUBBLE_MARGIN;
    
    // Arrange bubbles in a grid
    for (size_t i = 0; i < g_stations.size(); i++) {
        g_stations[i].xPos = x;
        g_stations[i].yPos = y;
        g_stations[i].isSelected = false;
        
        // Move to next position
        x += BUBBLE_WIDTH + BUBBLE_MARGIN;
        
        // Wrap to next row if needed
        if (x + BUBBLE_WIDTH > BUBBLE_AREA_X + BUBBLE_AREA_WIDTH) {
            x = BUBBLE_AREA_X + BUBBLE_MARGIN;
            y += BUBBLE_HEIGHT + BUBBLE_MARGIN;
        }
    }
}

int FindStationBubbleAt(int x, int y)
{
    for (int i = 0; i < (int)g_stations.size(); i++) {
        const StationInfo& station = g_stations[i];
        
        if (x >= station.xPos && x <= station.xPos + BUBBLE_WIDTH &&
            y >= station.yPos && y <= station.yPos + BUBBLE_HEIGHT) {
            return i;
        }
    }
    
    return -1; // No station found
}

void HandleStationSelection(int x, int y, bool isCtrlPressed)
{
    int stationIndex = FindStationBubbleAt(x, y);
    
    if (!isCtrlPressed) {
        // Clear existing selection if Ctrl not pressed
        for (auto& station : g_stations) {
            station.isSelected = false;
        }
    }
    
    if (stationIndex >= 0) {
        // Toggle selection for clicked station
        g_stations[stationIndex].isSelected = !g_stations[stationIndex].isSelected || !isCtrlPressed;
    }
    
    // Redraw
    InvalidateRect(g_hMainWnd, NULL, TRUE);
}

void MoveSelectedStations(int deltaX, int deltaY)
{
    // Check if move would place any station outside the bubble area
    bool canMove = true;
    
    for (auto& station : g_stations) {
        if (!station.isSelected) continue;
        
        int newX = station.xPos + deltaX;
        int newY = station.yPos + deltaY;
        
        if (newX < BUBBLE_AREA_X || 
            newY < BUBBLE_AREA_Y || 
            newX + BUBBLE_WIDTH > BUBBLE_AREA_X + BUBBLE_AREA_WIDTH ||
            newY + BUBBLE_HEIGHT > BUBBLE_AREA_Y + BUBBLE_AREA_HEIGHT) {
            canMove = false;
            break;
        }
    }
    
    if (canMove) {
        for (auto& station : g_stations) {
            if (station.isSelected) {
                station.xPos += deltaX;
                station.yPos += deltaY;
            }
        }
    }
}

void StartBubbleMultiSelection(int x, int y)
{
    g_isSelecting = true;
    g_selectionStart.x = x;
    g_selectionStart.y = y;
    g_selectionEnd = g_selectionStart;
}

void UpdateBubbleMultiSelection(int x, int y)
{
    if (!g_isSelecting) return;
    
    g_selectionEnd.x = x;
    g_selectionEnd.y = y;
    
    // Redraw
    InvalidateRect(g_hMainWnd, NULL, TRUE);
}

void EndBubbleMultiSelection()
{
    if (!g_isSelecting) return;
    
    // Create selection rectangle
    RECT selRect = {
        std::min(g_selectionStart.x, g_selectionEnd.x),
        std::min(g_selectionStart.y, g_selectionEnd.y),
        std::max(g_selectionStart.x, g_selectionEnd.x),
        std::max(g_selectionStart.y, g_selectionEnd.y)
    };
    
    // Select all stations inside the selection rectangle
    for (auto& station : g_stations) {
        // Check if station bubble intersects selection rectangle
        RECT bubbleRect = {
            station.xPos, station.yPos,
            station.xPos + BUBBLE_WIDTH, station.yPos + BUBBLE_HEIGHT
        };
        
        RECT intersectRect;
        if (IntersectRect(&intersectRect, &selRect, &bubbleRect)) {
            station.isSelected = true;
        }
    }
    
    g_isSelecting = false;
    
    // Redraw
    InvalidateRect(g_hMainWnd, NULL, TRUE);
}

/////////////////////////////////////////////////////////
// Custom Time Functions
/////////////////////////////////////////////////////////
void SetCustomTimeForSelectedStations()
{
    // Get the time from the edit control
    wchar_t buffer[32];
    GetWindowTextW(g_hCustomTimeEdit, buffer, 32);
    
    if (buffer[0] == 0) {
        MessageBoxW(g_hMainWnd, L"Please enter a time value in minutes", L"Input Required", MB_OK | MB_ICONINFORMATION);
        return;
    }
    
    // Convert to integer
    int minutes = _wtoi(buffer);
    
    if (minutes <= 0) {
        MessageBoxW(g_hMainWnd, L"Please enter a valid positive number of minutes", L"Invalid Input", MB_OK | MB_ICONINFORMATION);
        return;
    }
    
    // Create the command
    char cmd[64];
    sprintf_s(cmd, "SET_TIME %d", minutes);
    
    // Send the command to selected stations
    SendCommandToSelectedStation(cmd);
}

/////////////////////////////////////////////////////////
// WndProc
/////////////////////////////////////////////////////////
LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);
        
        // Draw the station bubbles
        DrawStationBubbles(hdc);
        
        EndPaint(hWnd, &ps);
        return 0;
    }
    case WM_LBUTTONDOWN:
    {
        int xPos = GET_X_LPARAM(lParam);
        int yPos = GET_Y_LPARAM(lParam);
        
        // Check if click is inside bubble area
        if (xPos >= BUBBLE_AREA_X && xPos <= BUBBLE_AREA_X + BUBBLE_AREA_WIDTH &&
            yPos >= BUBBLE_AREA_Y && yPos <= BUBBLE_AREA_Y + BUBBLE_AREA_HEIGHT) {
            
            int stationIndex = FindStationBubbleAt(xPos, yPos);
            
            if (stationIndex >= 0) {
                // Station was clicked - handle selection
                g_isDragging = true;
                g_draggedStationIndex = stationIndex;
                g_dragStart.x = xPos;
                g_dragStart.y = yPos;
                g_lastMousePos = g_dragStart;
                
                // Handle selection (with Ctrl for multi-select)
                bool isCtrlPressed = (GetKeyState(VK_CONTROL) & 0x8000);
                HandleStationSelection(xPos, yPos, isCtrlPressed);
                
                // Only start dragging if this station is now selected
                if (g_stations[stationIndex].isSelected) {
                    g_isDragging = true;
                    g_draggedStationIndex = stationIndex;
                } else {
                    g_isDragging = false;
                }
                
                // Capture mouse
                SetCapture(hWnd);
            } else {
                // Empty area clicked - start selection rectangle
                StartBubbleMultiSelection(xPos, yPos);
                
                // If Ctrl not pressed, clear existing selection
                if (!(GetKeyState(VK_CONTROL) & 0x8000)) {
                    for (auto& station : g_stations) {
                        station.isSelected = false;
                    }
                    InvalidateRect(hWnd, NULL, TRUE);
                }
                
                // Capture mouse
                SetCapture(hWnd);
            }
        }
        break;
    }
    case WM_MOUSEMOVE:
    {
        int xPos = GET_X_LPARAM(lParam);
        int yPos = GET_Y_LPARAM(lParam);
        
        if (g_isDragging) {
            // Calculate delta movement
            int deltaX = xPos - g_lastMousePos.x;
            int deltaY = yPos - g_lastMousePos.y;
            
            if (deltaX != 0 || deltaY != 0) {
                // Move selected stations
                MoveSelectedStations(deltaX, deltaY);
                
                // Update last position
                g_lastMousePos.x = xPos;
                g_lastMousePos.y = yPos;
                
                // Redraw
                InvalidateRect(hWnd, NULL, TRUE);
            }
        } else if (g_isSelecting) {
            // Update selection rectangle
            UpdateBubbleMultiSelection(xPos, yPos);
        } else {
            // Check for tooltip if not dragging or selecting
            if (xPos >= BUBBLE_AREA_X && xPos <= BUBBLE_AREA_X + BUBBLE_AREA_WIDTH &&
                yPos >= BUBBLE_AREA_Y && yPos <= BUBBLE_AREA_Y + BUBBLE_AREA_HEIGHT) {
                
                UpdateStationTooltip(xPos, yPos);
            }
        }
        break;
    }
    case WM_LBUTTONUP:
    {
        if (g_isDragging) {
            g_isDragging = false;
            
            // Check for collision with other bubbles
            int draggedIndex = g_draggedStationIndex;
            int collidedIndex = CheckBubbleCollision(draggedIndex);
            
            if (collidedIndex >= 0) {
                // Collision detected, handle grouping
                StationInfo& draggedStation = g_stations[draggedIndex];
                StationInfo& collidedStation = g_stations[collidedIndex];
                
                int targetGroupId;
                
                // Determine which group ID to use
                if (collidedStation.groupId != GROUP_NONE) {
                    // If collided station already has a group, use that group
                    targetGroupId = collidedStation.groupId;
                } else if (draggedStation.groupId != GROUP_NONE) {
                    // If dragged station has a group, use that group
                    targetGroupId = draggedStation.groupId;
                } else {
                    // Neither station has a group, find an unused group ID
                    targetGroupId = FindFirstUnusedGroupId();
                }
                
                // Assign both stations to the same group
                draggedStation.groupId = targetGroupId;
                collidedStation.groupId = targetGroupId;
                
                // Update all currently selected stations to the same group too
                for (auto& station : g_stations) {
                    if (station.isSelected && &station != &draggedStation) {
                        station.groupId = targetGroupId;
                    }
                }
                
                // Save the updated group assignments
                SaveStationGroups();
                
                // Provide feedback
                LogMessage("Grouped stations with color group " + std::to_string(targetGroupId));
                
                // Redraw
                InvalidateRect(g_hMainWnd, NULL, TRUE);
            }
            
            ReleaseCapture();
        } else if (g_isSelecting) {
            EndBubbleMultiSelection();
            ReleaseCapture();
        }
        break;
    }
    case WM_NOTIFY:
    {
        if (wParam == 200) // Our ListView's ID
        {
            LPNMHDR pnmh = (LPNMHDR)lParam;
            if (pnmh->code == NM_CUSTOMDRAW)
            {
                LRESULT lr = OnListViewCustomDraw((LPNMLVCUSTOMDRAW)lParam);
                return lr;
            }
        }
        break;
    }
    case WM_CREATE:
    {
        // Initialize tooltip
        InitializeTooltip(hWnd);
        
        // Get window dimensions for better layout
        RECT windowRect;
        GetClientRect(hWnd, &windowRect);
        int windowWidth = windowRect.right - windowRect.left;
        
        // Server start/stop
        g_hBtnStartServer = CreateWindowW(L"BUTTON", L"Start Server",
            WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
            20, 20, 120, 30,
            hWnd, reinterpret_cast<HMENU>(101), g_hInst, nullptr);

        g_hBtnStopServer = CreateWindowW(L"BUTTON", L"Stop Server",
            WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
            160, 20, 120, 30,
            hWnd, reinterpret_cast<HMENU>(102), g_hInst, nullptr);

        g_hLblServerStatus = CreateWindowW(L"STATIC", L"Server: Stopped",
            WS_CHILD | WS_VISIBLE,
            300, 25, 200, 20,
            hWnd, nullptr, g_hInst, nullptr);

        // Main ListView (now hidden but kept for compatibility)
        g_hListView = CreateWindowW(WC_LISTVIEW, L"",
            WS_CHILD | LVS_REPORT | WS_BORDER, // removed WS_VISIBLE
            20, 70, 800, 550,  // Match the new bubble area size
            hWnd, reinterpret_cast<HMENU>(200), g_hInst, nullptr);

        ListView_SetExtendedListViewStyle(g_hListView, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
        InitListViewColumns();

        // Right side controls - reorganized layout
        int rightPanelX = 850;  // Moved further right to accommodate larger bubble area
        int currentY = 70;      // Starting Y position

        // Session duration controls
        CreateWindowW(L"STATIC", L"Session Duration:",
            WS_CHILD | WS_VISIBLE,
            rightPanelX, currentY, 150, 20,
            hWnd, nullptr, g_hInst, nullptr);
        currentY += 25;

        g_hComboSessionMin = CreateWindowW(L"COMBOBOX", L"",
            WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
            rightPanelX, currentY, 80, 200,
            hWnd, (HMENU)999, g_hInst, nullptr);
        currentY += 30;

        for (int i = 1; i <= 60; i++)
        {
            wchar_t buf[32];
            swprintf_s(buf, L"%d min", i);
            SendMessageW(g_hComboSessionMin, CB_ADDSTRING, 0, (LPARAM)buf);
        }
        SendMessageW(g_hComboSessionMin, CB_SETCURSEL, 29, 0); // default 30

        g_hBtnStartSession = CreateWindowW(L"BUTTON", L"Start Session",
            WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
            rightPanelX, currentY, 120, 25,
            hWnd, (HMENU)103, g_hInst, nullptr);
        currentY += 30;

        g_hBtnStopSession = CreateWindowW(L"BUTTON", L"Stop Session",
            WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
            rightPanelX, currentY, 120, 25,
            hWnd, (HMENU)104, g_hInst, nullptr);
        currentY += 40;  // Add some spacing

        // Quick time add buttons
        CreateWindowW(L"STATIC", L"Add Time:",
            WS_CHILD | WS_VISIBLE,
            rightPanelX, currentY, 100, 20,
            hWnd, nullptr, g_hInst, nullptr);
        currentY += 25;

        g_hBtnAdd1 = CreateWindowW(L"BUTTON", L"+1",
            WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
            rightPanelX, currentY, 40, 25,
            hWnd, (HMENU)110, g_hInst, nullptr);

        g_hBtnAdd5 = CreateWindowW(L"BUTTON", L"+5",
            WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
            rightPanelX + 50, currentY, 40, 25,
            hWnd, (HMENU)111, g_hInst, nullptr);

        g_hBtnAdd10 = CreateWindowW(L"BUTTON", L"+10",
            WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
            rightPanelX + 100, currentY, 40, 25,
            hWnd, (HMENU)112, g_hInst, nullptr);
        currentY += 30;

        g_hBtnAdd15 = CreateWindowW(L"BUTTON", L"+15",
            WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
            rightPanelX, currentY, 40, 25,
            hWnd, (HMENU)113, g_hInst, nullptr);

        g_hBtnAdd30 = CreateWindowW(L"BUTTON", L"+30",
            WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
            rightPanelX + 50, currentY, 40, 25,
            hWnd, (HMENU)114, g_hInst, nullptr);

        g_hBtnAdd60 = CreateWindowW(L"BUTTON", L"+60",
            WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
            rightPanelX + 100, currentY, 40, 25,
            hWnd, (HMENU)115, g_hInst, nullptr);
        currentY += 40;  // Add some spacing

        // Custom time input 
        CreateWindowW(L"STATIC", L"Custom Time (minutes):",
            WS_CHILD | WS_VISIBLE,
            rightPanelX, currentY, 150, 20,
            hWnd, nullptr, g_hInst, nullptr);
        currentY += 25;

        g_hCustomTimeEdit = CreateWindowW(L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | WS_BORDER | ES_NUMBER,
            rightPanelX, currentY, 80, 22,
            hWnd, (HMENU)116, g_hInst, nullptr);

        g_hBtnSetCustomTime = CreateWindowW(L"BUTTON", L"Set Time",
            WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
            rightPanelX + 90, currentY, 80, 22,
            hWnd, (HMENU)117, g_hInst, nullptr);
        currentY += 40;  // Add some spacing

        // Group information label
        g_hGroupInfoLabel = CreateWindowW(L"STATIC", 
            L"Note: Stations with the same color will have\r\ntime-related events applied to them as a group.",
            WS_CHILD | WS_VISIBLE,
            rightPanelX, currentY, 250, 40,
            hWnd, nullptr, g_hInst, nullptr);
        
        // Group buttons on far right
        int groupX = rightPanelX + 220;  // Place group buttons far to the right
        int groupY = 70;  // Start at the same Y height
        
        // Group buttons
        static const wchar_t* groupLabels[7] = {
            L"Group 1 (Red)", L"Group 2 (Green)", L"Group 3 (Blue)",
            L"Group 4 (Yellow)", L"Group 5 (Cyan)", L"Group 6 (Magenta)",
            L"Group 7 (Orange)"
        };
        for (int i = 0; i < 7; i++)
        {
            g_hBtnGroupColor[i] = CreateWindowW(
                L"BUTTON",
                groupLabels[i],
                WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
                groupX, groupY + i * 30, 150, 25,
                hWnd, (HMENU)(130 + i),
                g_hInst, nullptr);
        }

        g_hBtnRemoveGroup = CreateWindowW(
            L"BUTTON",
            L"Remove from Group",
            WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
            groupX, groupY + 7 * 30, 150, 25,
            hWnd, (HMENU)137,
            g_hInst, nullptr);

        UpdateUI();
        
        // Auto-start the server when the app launches
        PostMessage(hWnd, WM_COMMAND, 101, 0);
        
        break;
    }
    case WM_COMMAND:
    {
        int id = LOWORD(wParam);
        switch (id)
        {
        case 101: // Start server
            StartServer(12345);
            break;
        case 102: // Stop server
            // Show warning dialog before stopping the server
            if (MessageBoxW(hWnd, L"Are you sure you want to stop the server?\nAll station connections will be closed.", 
                           L"Warning", MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) == IDYES)
            {
                StopServer();
            }
            break;
        case 103: // Start Session
        {
            int sel = (int)SendMessageW(g_hComboSessionMin, CB_GETCURSEL, 0, 0);
            if (sel < 0) sel = 29; // default to 30
            int minutes = sel + 1;

            char buf[64];
            sprintf_s(buf, "START_SESSION %d", minutes);
            SendCommandToSelectedStation(buf);
            break;
        }
        case 104: // Stop Session
            SendCommandToSelectedStation("STOP_SESSION");
            break;

        // +time: "ADD_OR_START X"
        case 110:
            SendCommandToSelectedStation("ADD_OR_START 1");
            break;
        case 111:
            SendCommandToSelectedStation("ADD_OR_START 5");
            break;
        case 112:
            SendCommandToSelectedStation("ADD_OR_START 10");
            break;
        case 113:
            SendCommandToSelectedStation("ADD_OR_START 15");
            break;
        case 114:
            SendCommandToSelectedStation("ADD_OR_START 30");
            break;
        case 115:
            SendCommandToSelectedStation("ADD_OR_START 60");
            break;
        case 117: // Custom time set button
            SetCustomTimeForSelectedStations();
            break;

        // Group color buttons
        case 130: SetSelectedStationsGroup(GROUP_1); break;
        case 131: SetSelectedStationsGroup(GROUP_2); break;
        case 132: SetSelectedStationsGroup(GROUP_3); break;
        case 133: SetSelectedStationsGroup(GROUP_4); break;
        case 134: SetSelectedStationsGroup(GROUP_5); break;
        case 135: SetSelectedStationsGroup(GROUP_6); break;
        case 136: SetSelectedStationsGroup(GROUP_7); break;
        case 137: RemoveSelectedStationsGroup();      break;
        }
        break;
    }
    case WM_CLOSE:
    {
        // Show confirmation dialog before closing
        if (g_serverRunning) {
            if (MessageBoxW(hWnd, L"Server is still running. Are you sure you want to exit?\nAll station connections will be closed.",
                L"Warning", MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES) {
                // User chose "No", prevent the window from closing
                return 0;
            }
            // User chose "Yes", let the window close normally
        }
        
        // Default processing to destroy the window
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
    case WM_DESTROY:
    {
        SaveStationGroups(); // Save station groups before exiting
        StopServer(); // Always stop the server before exiting
        PostQuitMessage(0);
        break;
    }
    }
    return DefWindowProc(hWnd, message, wParam, lParam);
}

/////////////////////////////////////////////////////////
// OnListViewCustomDraw
/////////////////////////////////////////////////////////
LRESULT OnListViewCustomDraw(LPNMLVCUSTOMDRAW nmcd)
{
    switch (nmcd->nmcd.dwDrawStage)
    {
    case CDDS_PREPAINT:
        return CDRF_NOTIFYITEMDRAW;

    case CDDS_ITEMPREPAINT:
    {
        int itemIndex = (int)nmcd->nmcd.dwItemSpec;
        if (itemIndex >= 0 && itemIndex < (int)g_stations.size())
        {
            int groupId = g_stations[itemIndex].groupId;
            nmcd->clrTextBk = g_groupColors[groupId];
        }
        return CDRF_DODEFAULT;
    }
    }
    return CDRF_DODEFAULT;
}

/////////////////////////////////////////////////////////
// InitListViewColumns
/////////////////////////////////////////////////////////
void InitListViewColumns()
{
    LVCOLUMNW col;
    ZeroMemory(&col, sizeof(col));
    col.mask = LVCF_TEXT | LVCF_WIDTH;

    col.cx = 100;
    col.pszText = (LPWSTR)L"Station";
    ListView_InsertColumn(g_hListView, 0, &col);

    col.cx = 120;
    col.pszText = (LPWSTR)L"IP:Port";
    ListView_InsertColumn(g_hListView, 1, &col);

    col.cx = 100;
    col.pszText = (LPWSTR)L"Session";
    ListView_InsertColumn(g_hListView, 2, &col);

    col.cx = 70;
    col.pszText = (LPWSTR)L"TimeLeft";
    ListView_InsertColumn(g_hListView, 3, &col);
}

int AddStationRow(const StationInfo& st)
{
    // Check if we need to initialize bubble position
    // Only initialize if position is 0,0 (not yet positioned or restored from saved data)
    if (st.xPos == 0 && st.yPos == 0) {
        // Find a vacant spot for this station instead of resetting all stations
        int x = BUBBLE_AREA_X + BUBBLE_MARGIN;
        int y = BUBBLE_AREA_Y + BUBBLE_MARGIN;
        bool found = false;
        
        // Try to find an empty position in a grid pattern
        while (!found && y + BUBBLE_HEIGHT <= BUBBLE_AREA_Y + BUBBLE_AREA_HEIGHT) {
            bool overlaps = false;
            
            // Check if this position overlaps with any existing station
            for (const auto& existingStation : g_stations) {
                if (&st == &existingStation) continue; // Skip comparing with self
                
                // Collision detection
                if (x < existingStation.xPos + BUBBLE_WIDTH && 
                    x + BUBBLE_WIDTH > existingStation.xPos &&
                    y < existingStation.yPos + BUBBLE_HEIGHT &&
                    y + BUBBLE_HEIGHT > existingStation.yPos) {
                    overlaps = true;
                    break;
                }
            }
            
            if (!overlaps) {
                // Found a vacant spot
                found = true;
                const_cast<StationInfo&>(st).xPos = x;
                const_cast<StationInfo&>(st).yPos = y;
            } else {
                // Move to next position
                x += BUBBLE_WIDTH + BUBBLE_MARGIN;
                
                // Wrap to next row if needed
                if (x + BUBBLE_WIDTH > BUBBLE_AREA_X + BUBBLE_AREA_WIDTH) {
                    x = BUBBLE_AREA_X + BUBBLE_MARGIN;
                    y += BUBBLE_HEIGHT + BUBBLE_MARGIN;
                }
            }
        }
        
        // If we couldn't find a vacant spot, just use the default position
        if (!found) {
            const_cast<StationInfo&>(st).xPos = BUBBLE_AREA_X + BUBBLE_MARGIN;
            const_cast<StationInfo&>(st).yPos = BUBBLE_AREA_Y + BUBBLE_MARGIN;
        }
    }
    
    // Now update the ListView (kept for compatibility)
    LVITEMW it;
    ZeroMemory(&it, sizeof(it));
    it.mask = LVIF_TEXT;
    it.iItem = ListView_GetItemCount(g_hListView);
    it.iSubItem = 0;

    std::wstring ws(st.stationName.begin(), st.stationName.end());
    it.pszText = (LPWSTR)ws.c_str();
    int row = ListView_InsertItem(g_hListView, &it);

    {
        std::string ipPort = st.ip + ":" + std::to_string(st.port);
        std::wstring wip(ipPort.begin(), ipPort.end());
        LVITEMW sub;
        ZeroMemory(&sub, sizeof(sub));
        sub.mask = LVIF_TEXT;
        sub.iItem = row;
        sub.iSubItem = 1;
        sub.pszText = (LPWSTR)wip.c_str();
        ListView_SetItem(g_hListView, &sub);
    }
    {
        std::wstring sessionStr = st.sessionActive ? L"Running" : L"Not Running";
        LVITEMW sub;
        ZeroMemory(&sub, sizeof(sub));
        sub.mask = LVIF_TEXT;
        sub.iItem = row;
        sub.iSubItem = 2;
        sub.pszText = (LPWSTR)sessionStr.c_str();
        ListView_SetItem(g_hListView, &sub);
    }
    {
        std::string t = FormatTime(st.timeLeftSec);
        std::wstring wt(t.begin(), t.end());
        LVITEMW sub;
        ZeroMemory(&sub, sizeof(sub));
        sub.mask = LVIF_TEXT;
        sub.iItem = row;
        sub.iSubItem = 3;
        sub.pszText = (LPWSTR)wt.c_str();
        ListView_SetItem(g_hListView, &sub);
    }
    
    // Redraw to update bubbles
    InvalidateRect(g_hMainWnd, NULL, TRUE);
    
    return row;
}

void UpdateStationRow(int index)
{
    if (index < 0 || index >= (int)g_stations.size()) return;
    const StationInfo& st = g_stations[index];

    // Update the ListView (kept for compatibility)
    {
        std::wstring sessionStr = st.sessionActive ? L"Running" : L"Not Running";
        LVITEMW sub;
        ZeroMemory(&sub, sizeof(sub));
        sub.mask = LVIF_TEXT;
        sub.iItem = index;
        sub.iSubItem = 2;
        sub.pszText = (LPWSTR)sessionStr.c_str();
        ListView_SetItem(g_hListView, &sub);
    }
    {
        std::string t = FormatTime(st.timeLeftSec);
        std::wstring wt(t.begin(), t.end());
        LVITEMW sub;
        ZeroMemory(&sub, sizeof(sub));
        sub.mask = LVIF_TEXT;
        sub.iItem = index;
        sub.iSubItem = 3;
        sub.pszText = (LPWSTR)wt.c_str();
        ListView_SetItem(g_hListView, &sub);
    }
    
    // Redraw to update bubbles
    InvalidateRect(g_hMainWnd, NULL, TRUE);
}

void RemoveStationRow(SOCKET sock)
{
    int idx = FindStationIndexBySock(sock);
    if (idx < 0) return;
    
    // Save position and group before removing
    std::string stationName = g_stations[idx].stationName;
    
    // Remove from vector
    g_stations.erase(g_stations.begin() + idx);
    
    // Remove from ListView
    ListView_DeleteItem(g_hListView, idx);
    
    // Update the ListView to reflect the removal
    // but WITHOUT reinitializing positions or changing the grid
    ListView_DeleteAllItems(g_hListView);
    for (auto& s : g_stations)
    {
        AddStationToListViewOnly(s);
    }
    
    // Redraw without modifying positions
    InvalidateRect(g_hMainWnd, NULL, TRUE);
}

void UpdateUI()
{
    if (g_hLblServerStatus)
    {
        if (g_serverRunning)
            SetWindowTextW(g_hLblServerStatus, L"Server: Running");
        else
            SetWindowTextW(g_hLblServerStatus, L"Server: Stopped");
    }
    if (g_hBtnStartServer && g_hBtnStopServer)
    {
        EnableWindow(g_hBtnStartServer, !g_serverRunning);
        EnableWindow(g_hBtnStopServer, g_serverRunning);
    }
}

void ReorderStationsAscending()
{
    // We need to preserve positions during sorting
    std::vector<std::pair<int, int>> savedPositions;
    for (const auto& station : g_stations) {
        savedPositions.push_back(std::make_pair(station.xPos, station.yPos));
    }
    
    // Sort stations by name
    std::sort(g_stations.begin(), g_stations.end(),
        [](const StationInfo& a, const StationInfo& b) {
            return a.stationName < b.stationName;
        });
        
    // Restore positions in sorted order
    for (size_t i = 0; i < g_stations.size() && i < savedPositions.size(); i++) {
        g_stations[i].xPos = savedPositions[i].first;
        g_stations[i].yPos = savedPositions[i].second;
    }
    
    // Update ListView
    ListView_DeleteAllItems(g_hListView);
    for (auto& station : g_stations) {
        AddStationToListViewOnly(station);
    }
    
    // Redraw without reinitializing positions
    InvalidateRect(g_hMainWnd, NULL, TRUE);
}

void SetSelectedStationsGroup(int groupId)
{
    bool anySelected = false;
    
    // For the bubble UI, look at the isSelected flag
    for (auto& station : g_stations) {
        if (station.isSelected) {
            station.groupId = groupId;
            anySelected = true;
        }
    }
    
    if (!anySelected) {
        // Legacy ListView selection support
        int sel = -1;
        while (true) {
            sel = ListView_GetNextItem(g_hListView, sel, LVNI_SELECTED);
            if (sel == -1) break;
            anySelected = true;
            
            if (sel >= 0 && sel < (int)g_stations.size()) {
                g_stations[sel].groupId = groupId;
            }
        }
    }

    if (!anySelected) {
        LogMessage("No station(s) selected for grouping.");
    } else {
        InvalidateRect(g_hMainWnd, NULL, TRUE);
        LogMessage("SetSelectedStationsGroup -> groupId = " + std::to_string(groupId));
        SaveStationGroups(); // Save the updated group assignments
    }
}

void RemoveSelectedStationsGroup()
{
    bool anySelected = false;
    
    // For the bubble UI, look at the isSelected flag
    for (auto& station : g_stations) {
        if (station.isSelected) {
            station.groupId = GROUP_NONE;
            anySelected = true;
        }
    }
    
    if (!anySelected) {
        // Legacy ListView selection support
        int sel = -1;
        while (true) {
            sel = ListView_GetNextItem(g_hListView, sel, LVNI_SELECTED);
            if (sel == -1) break;
            anySelected = true;
            
            if (sel >= 0 && sel < (int)g_stations.size()) {
                g_stations[sel].groupId = GROUP_NONE;
            }
        }
    }

    if (!anySelected) {
        LogMessage("No station(s) selected to remove group.");
    } else {
        InvalidateRect(g_hMainWnd, NULL, TRUE);
        LogMessage("RemoveSelectedStationsGroup -> done.");
        SaveStationGroups(); // Save the updated group assignments
    }
}

// Find the first available group ID that's not currently in use
int FindFirstUnusedGroupId()
{
    std::set<int> usedGroups;
    
    // Collect all used group IDs
    for (const auto& station : g_stations) {
        if (station.groupId != GROUP_NONE) {
            usedGroups.insert(station.groupId);
        }
    }
    
    // Find the first unused group ID (1-7)
    for (int i = GROUP_1; i <= GROUP_7; i++) {
        if (usedGroups.find(i) == usedGroups.end()) {
            return i;
        }
    }
    
    // If all groups are used, return GROUP_1 as a fallback
    return GROUP_1;
}

// Check if a station bubble collides with any other bubble
int CheckBubbleCollision(int stationIndex)
{
    if (stationIndex < 0 || stationIndex >= (int)g_stations.size()) {
        return -1;
    }
    
    const StationInfo& draggedStation = g_stations[stationIndex];
    
    // Create the rect for the dragged station
    RECT draggedRect = {
        draggedStation.xPos,
        draggedStation.yPos,
        draggedStation.xPos + BUBBLE_WIDTH,
        draggedStation.yPos + BUBBLE_HEIGHT
    };
    
    // Check for collision with any other station
    for (int i = 0; i < (int)g_stations.size(); i++) {
        if (i == stationIndex) continue; // Skip the dragged station itself
        
        const StationInfo& otherStation = g_stations[i];
        
        // Create the rect for the other station
        RECT otherRect = {
            otherStation.xPos,
            otherStation.yPos,
            otherStation.xPos + BUBBLE_WIDTH,
            otherStation.yPos + BUBBLE_HEIGHT
        };
        
        // Check if the rectangles intersect
        RECT intersection;
        if (IntersectRect(&intersection, &draggedRect, &otherRect)) {
            return i; // Return the index of the colliding station
        }
    }
    
    return -1; // No collision
}

void SendCommandToSelectedStation(const std::string& rawCmd)
{
    std::set<int> selectedGroupIds;
    std::vector<int> selectedNoGroupIdx;
    bool anySelected = false;
    
    // First check bubble UI selections
    for (int i = 0; i < (int)g_stations.size(); i++) {
        if (g_stations[i].isSelected) {
            anySelected = true;
            if (g_stations[i].groupId != GROUP_NONE) {
                selectedGroupIds.insert(g_stations[i].groupId);
            } else {
                selectedNoGroupIdx.push_back(i);
            }
        }
    }
    
    // If no bubble selections, check ListView (legacy support)
    if (!anySelected) {
        int sel = -1;
        while (true) {
            sel = ListView_GetNextItem(g_hListView, sel, LVNI_SELECTED);
            if (sel == -1) break;

            if (sel >= 0 && sel < (int)g_stations.size()) {
                if (g_stations[sel].groupId != GROUP_NONE)
                    selectedGroupIds.insert(g_stations[sel].groupId);
                else
                    selectedNoGroupIdx.push_back(sel);
            }
        }
    }

    if (selectedGroupIds.empty() && selectedNoGroupIdx.empty()) {
        LogMessage("No station(s) selected.");
        return;
    }

    auto interpretAddOrStart = [&](const std::string& cmd, StationInfo& st) -> std::string {
        if (cmd.rfind("ADD_OR_START", 0) == 0) {
            int x = 0;
            if (sscanf_s(cmd.c_str(), "ADD_OR_START %d", &x) != 1)
                return cmd;

            if (!st.sessionActive) {
                char tmp[64];
                sprintf_s(tmp, "START_SESSION %d", x);
                return std::string(tmp);
            } else {
                char tmp[64];
                sprintf_s(tmp, "ADD_TIME %d", x);
                return std::string(tmp);
            }
        } else if (cmd.rfind("SET_TIME", 0) == 0) {
            int minutes = 0;
            if (sscanf_s(cmd.c_str(), "SET_TIME %d", &minutes) != 1)
                return cmd;
                
            // Always stop any existing session first
            if (st.sessionActive) {
                // Send stop command
                send(st.sock, "STOP_SESSION", 12, 0);
                Sleep(100); // Give a little time for the client to process
            }
            
            // Then start with exact time
            char tmp[64];
            sprintf_s(tmp, "START_SESSION %d", minutes);
            return std::string(tmp);
        }
        
        return cmd;
    };

    // Groups
    for (auto grp : selectedGroupIds) {
        for (int i = 0; i < (int)g_stations.size(); i++) {
            StationInfo& st = g_stations[i];
            if (st.groupId == grp && st.sock != INVALID_SOCKET) {
                std::string finalCmd = interpretAddOrStart(rawCmd, st);
                int r = send(st.sock, finalCmd.c_str(), (int)finalCmd.size(), 0);
                if (r == SOCKET_ERROR) {
                    LogMessage("send() failed to station: " + std::to_string(WSAGetLastError()));
                    closesocket(st.sock);
                    st.sock = INVALID_SOCKET;
                    st.sessionActive = false;
                    st.timeLeftSec = 0;
                    UpdateStationRow(i);
                } else {
                    LogMessage("[To " + st.stationName + "] " + finalCmd);
                }
            }
        }
    }

    // No group
    for (int idx : selectedNoGroupIdx) {
        StationInfo& st = g_stations[idx];
        if (st.sock == INVALID_SOCKET) continue;

        std::string finalCmd = interpretAddOrStart(rawCmd, st);
        int r = send(st.sock, finalCmd.c_str(), (int)finalCmd.size(), 0);
        if (r == SOCKET_ERROR) {
            LogMessage("send() failed to station: " + std::to_string(WSAGetLastError()));
            closesocket(st.sock);
            st.sock = INVALID_SOCKET;
            st.sessionActive = false;
            st.timeLeftSec = 0;
            UpdateStationRow(idx);
        } else {
            LogMessage("[To " + st.stationName + "] " + finalCmd);
        }
    }
}

// Initialize tooltip control
void InitializeTooltip(HWND hWnd)
{
    if (g_hToolTip != NULL) {
        DestroyWindow(g_hToolTip);
    }
    
    g_hToolTip = CreateWindowEx(
        WS_EX_TOPMOST,
        TOOLTIPS_CLASS,
        NULL,
        WS_POPUP | TTS_ALWAYSTIP | TTS_BALLOON,
        CW_USEDEFAULT, CW_USEDEFAULT,
        CW_USEDEFAULT, CW_USEDEFAULT,
        hWnd, NULL,
        g_hInst, NULL
    );
    
    SetWindowPos(g_hToolTip, HWND_TOPMOST, 0, 0, 0, 0,
                SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    
    // Set up tooltip info
    ZeroMemory(&g_toolInfo, sizeof(TOOLINFOW));
    g_toolInfo.cbSize = sizeof(TOOLINFOW);
    g_toolInfo.uFlags = TTF_SUBCLASS | TTF_IDISHWND;
    g_toolInfo.hwnd = hWnd;
    g_toolInfo.hinst = g_hInst;
    g_toolInfo.uId = (UINT_PTR)hWnd;
    g_toolInfo.lpszText = NULL;
    
    // Add tooltip
    SendMessageW(g_hToolTip, TTM_ADDTOOL, 0, (LPARAM)&g_toolInfo);
    
    // Set delay times
    SendMessageW(g_hToolTip, TTM_SETDELAYTIME, TTDT_INITIAL, 500);
    SendMessageW(g_hToolTip, TTM_SETDELAYTIME, TTDT_AUTOPOP, 5000);
}

// Show a tooltip for the station at the mouse position
void UpdateStationTooltip(int x, int y)
{
    int stationIndex = FindStationBubbleAt(x, y);
    
    if (stationIndex != g_lastHoverStationIndex) {
        g_lastHoverStationIndex = stationIndex;
        
        if (stationIndex >= 0 && stationIndex < (int)g_stations.size()) {
            // Create tooltip text with IP information
            static wchar_t tooltipText[256];
            std::string ipInfo = g_stations[stationIndex].ip + ":" + std::to_string(g_stations[stationIndex].port);
            MultiByteToWideChar(CP_UTF8, 0, ipInfo.c_str(), -1, tooltipText, 256);
            
            // Update tooltip
            g_toolInfo.lpszText = tooltipText;
            SendMessageW(g_hToolTip, TTM_UPDATETIPTEXTW, 0, (LPARAM)&g_toolInfo);
            
            // Force tooltip to show immediately
            POINT pt = { x, y };
            ClientToScreen(g_hMainWnd, &pt);
            SendMessageW(g_hToolTip, TTM_TRACKPOSITION, 0, MAKELONG(pt.x, pt.y));
            SendMessageW(g_hToolTip, TTM_TRACKACTIVATE, TRUE, (LPARAM)&g_toolInfo);
        } else {
            // Hide tooltip if not over a station
            SendMessageW(g_hToolTip, TTM_TRACKACTIVATE, FALSE, (LPARAM)&g_toolInfo);
        }
    }
}

// Just update the ListView without touching bubble positions
int AddStationToListViewOnly(const StationInfo& st)
{
    // Update the ListView only
    LVITEMW it;
    ZeroMemory(&it, sizeof(it));
    it.mask = LVIF_TEXT;
    it.iItem = ListView_GetItemCount(g_hListView);
    it.iSubItem = 0;

    std::wstring ws(st.stationName.begin(), st.stationName.end());
    it.pszText = (LPWSTR)ws.c_str();
    int row = ListView_InsertItem(g_hListView, &it);

    {
        std::string ipPort = st.ip + ":" + std::to_string(st.port);
        std::wstring wip(ipPort.begin(), ipPort.end());
        LVITEMW sub;
        ZeroMemory(&sub, sizeof(sub));
        sub.mask = LVIF_TEXT;
        sub.iItem = row;
        sub.iSubItem = 1;
        sub.pszText = (LPWSTR)wip.c_str();
        ListView_SetItem(g_hListView, &sub);
    }
    {
        std::wstring sessionStr = st.sessionActive ? L"Running" : L"Not Running";
        LVITEMW sub;
        ZeroMemory(&sub, sizeof(sub));
        sub.mask = LVIF_TEXT;
        sub.iItem = row;
        sub.iSubItem = 2;
        sub.pszText = (LPWSTR)sessionStr.c_str();
        ListView_SetItem(g_hListView, &sub);
    }
    {
        std::string t = FormatTime(st.timeLeftSec);
        std::wstring wt(t.begin(), t.end());
        LVITEMW sub;
        ZeroMemory(&sub, sizeof(sub));
        sub.mask = LVIF_TEXT;
        sub.iItem = row;
        sub.iSubItem = 3;
        sub.pszText = (LPWSTR)wt.c_str();
        ListView_SetItem(g_hListView, &sub);
    }
    
    return row;
}