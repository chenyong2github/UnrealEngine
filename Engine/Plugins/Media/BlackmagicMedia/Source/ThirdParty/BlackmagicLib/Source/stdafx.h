// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if _WINDOWS
#include "targetver.h"
#include <tchar.h>
#endif
#include "assert.h"

#include <algorithm>
#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#ifdef _DEBUG
#define DO_CHECK 1
#else
#define DO_CHECK 1
#endif

#if DO_CHECK
#if _WINDOWS
#define CHECK(FUNCTION) if (!(FUNCTION)) { *reinterpret_cast<char*>(0) = 0; }
#define COM_CHECK(FUNCTION) if ((FUNCTION) != S_OK) { *reinterpret_cast<char*>(0) = 0; }
#else
#define CHECK(FUNCTION) if (!(FUNCTION)) { __builtin_trap(); }
#define COM_CHECK(FUNCTION) if ((FUNCTION) != S_OK) { __builtin_trap(); }
#endif // _WINDOWS
#else
#define CHECK(FUNCTION) (FUNCTION)
#define COM_CHECK(FUNCTION) (FUNCTION)
#endif

#if _WINDOWS
#include "../DeckLinkAPI_h.h"
#else
#include "Linux/DeckLinkAPI.h"
#include "Linux/LinuxCOM.h"
#endif

#include "Platform/GenericPlatform.h"
#include "BlackmagicLib.h"
#include "BlackmagicLog.h"
