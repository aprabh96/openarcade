#ifndef CONFIG_H
#define CONFIG_H

// <ai_context>
// INI-based config declarations
// </ai_context>

#include "globals.h"

void SetupINIPath();
void EnsureIniExists();
void LoadSettings();
void SaveSettings();
void SaveMasterIP(const std::string& ip);
std::string LoadMasterIP();
void SaveStationName(const std::string& name);
std::string LoadStationName();
void SaveSessionEndTime(time_t endTime);
time_t LoadSessionEndTime();
void SetupCachePath();
void EnsureCacheDirExists();

// Category management
void LoadCategories();
void SaveCategories();

#endif