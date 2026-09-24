#ifndef LOGGING_H
#define LOGGING_H

// <ai_context>
// Logging declarations
// </ai_context>

#include "globals.h"
#include <deque>
#include <string>
#include <mutex>

extern std::deque<std::string> recent_logs;
extern std::mutex g_logWindowMutex;

void Log(const std::string& msg);

#endif