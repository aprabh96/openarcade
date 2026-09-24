// <ai_context> This file is the application's entry point (wWinMain) and handles main window creation </ai_context>

#include "globals.h"  // This brings in all Windows headers
#include <commctrl.h>
#include "ui.h"
#include "util.h"

int APIENTRY wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int nCmdShow)
{
    g_hInst = hInstance;

    // Create a named mutex to ensure only one instance of the app can run
    HANDLE hMutex = CreateMutex(NULL, TRUE, L"ArcadeMasterControllerMutex");
    DWORD lastError = GetLastError();
    
    // If mutex already exists, another instance is running
    if (lastError == ERROR_ALREADY_EXISTS || hMutex == NULL)
    {
        MessageBox(NULL, L"Another instance of the application is already running.", 
                  L"Master Controller", MB_OK | MB_ICONINFORMATION);
        
        // Close the mutex handle and exit
        if (hMutex) CloseHandle(hMutex);
        return 1;
    }

    INITCOMMONCONTROLSEX icex;
    icex.dwSize = sizeof(icex);
    icex.dwICC = ICC_LISTVIEW_CLASSES;
    InitCommonControlsEx(&icex);

    const wchar_t CLASS_NAME[] = L"MasterControllerClass";
    WNDCLASSEX wc = { 0 };
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = CLASS_NAME;

    if (!RegisterClassEx(&wc))
    {
        MessageBox(nullptr, L"RegisterClassEx failed!", L"Error", MB_OK | MB_ICONERROR);
        CloseHandle(hMutex);
        return 1;
    }

    g_hMainWnd = CreateWindowEx(
        0,
        CLASS_NAME,
        L"Master Controller",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 1300, 700,  // Increased window size further
        nullptr, nullptr, hInstance, nullptr
    );

    if (!g_hMainWnd)
    {
        MessageBox(nullptr, L"CreateWindowEx failed!", L"Error", MB_OK | MB_ICONERROR);
        CloseHandle(hMutex);
        return 1;
    }

    // Load saved station groups
    LoadStationGroups();

    ShowWindow(g_hMainWnd, nCmdShow);
    UpdateWindow(g_hMainWnd);

    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    // Release the mutex when exiting
    CloseHandle(hMutex);
    
    return (int)msg.wParam;
}