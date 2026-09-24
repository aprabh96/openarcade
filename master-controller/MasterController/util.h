// <ai_context> This file declares general utility functions </ai_context>

#pragma once

#include <string>

void LogMessage(const std::string& msg);
std::string FormatTime(int sec);
void KillExistingProcesses(const std::wstring& exeName);
void SaveStationGroups();
void LoadStationGroups();