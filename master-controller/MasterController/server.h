// <ai_context> This file declares all server-related functions, including StartServer/StopServer </ai_context>

#pragma once

#include <windows.h>
#include <string>

DWORD WINAPI ServerThreadProc(LPVOID lpParam);
void StartServer(unsigned short port);
void StopServer();
void HandleClient(SOCKET clientSock, std::string ipStr, unsigned short port);