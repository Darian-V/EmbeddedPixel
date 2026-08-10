#pragma once

// NET_LOG_LEVEL: 0=off, 1=error only, 2=info (default), 3=debug (verbose)
#ifndef NET_LOG_LEVEL
#define NET_LOG_LEVEL 2
#endif

#include <stdio.h>

#define LOG_ERR(fmt, ...)  do { if (NET_LOG_LEVEL >= 1) printf("[NET ERR] " fmt, ##__VA_ARGS__); } while(0)
#define LOG_INFO(fmt, ...) do { if (NET_LOG_LEVEL >= 2) printf("[NET] "     fmt, ##__VA_ARGS__); } while(0)
#define LOG_DBG(fmt, ...)  do { if (NET_LOG_LEVEL >= 3) printf("[NET DBG] " fmt, ##__VA_ARGS__); } while(0)
