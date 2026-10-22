#pragma once

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#ifndef DEBUG_ASSERTLOG
#define DEBUG_ASSERTLOG(condition, message) ((void)0)
#endif

#ifndef DEBUG_LOG
#define DEBUG_LOG(message) ((void)0)
#endif

#ifndef DEBUG_CRASH
#define DEBUG_CRASH(message) ((void)0)
#endif
