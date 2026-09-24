#ifndef SESSION_H
#define SESSION_H

// <ai_context>
// Declarations for session logic
// </ai_context>

#include "globals.h"

void ResumeSessionIfNeeded(HWND hWnd);
void StopSessionTimer(HWND hWnd);
void StartSessionTimer(HWND hWnd, int totalSeconds);
void StartSession(HWND hWnd);
void AddTimeToSession(int minutes, int seconds = 0);
void AddMoreTimeToSession();
void StopSession(HWND hWnd);
void UpdateTimeLeftDisplay();

#endif