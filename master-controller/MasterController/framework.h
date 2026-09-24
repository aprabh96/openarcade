// header.h : include file for standard system include files,
// or project specific include files
//

#pragma once

#include "targetver.h"

// This define must come before any Windows headers
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX  // Add this to prevent min/max macro conflicts

// Windows Socket API
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")

// Windows API
#include <windows.h>
#include <windowsx.h> // For GET_X_LPARAM and GET_Y_LPARAM

// C RunTime Header Files
#include <stdlib.h>
#include <malloc.h>
#include <memory.h>
#include <tchar.h>
