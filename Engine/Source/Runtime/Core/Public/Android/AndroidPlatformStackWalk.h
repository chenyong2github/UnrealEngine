// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.


/*=============================================================================================
	AndroidPlatformStackWalk.h: Android platform stack walk functions
==============================================================================================*/

#pragma once
#include "GenericPlatform/GenericPlatformStackWalk.h"

/**
* Android platform stack walking
*/
struct CORE_API FAndroidPlatformStackWalk : public FGenericPlatformStackWalk
{
	typedef FGenericPlatformStackWalk Parent;

	static void ProgramCounterToSymbolInfo(uint64 ProgramCounter, FProgramCounterSymbolInfo& out_SymbolInfo);
	static uint32 CaptureStackBackTrace(uint64* BackTrace, uint32 MaxDepth, void* Context = nullptr);
	static bool SymbolInfoToHumanReadableString(const FProgramCounterSymbolInfo& SymbolInfo, ANSICHAR* HumanReadableString, SIZE_T HumanReadableStringSize);

	static uint32 CaptureThreadStackBackTrace(uint64 ThreadId, uint64* BackTrace, uint32 MaxDepth);

	static void HandleBackTraceSignal(siginfo* Info, void* Context);
};

#define ANDROID_HAS_THREADBACKTRACE !PLATFORM_LUMIN && PLATFORM_USED_NDK_VERSION_INTEGER >= 21

#if ANDROID_HAS_THREADBACKTRACE

/** Passed in through sigqueue for gathering of a callstack from a signal */
struct ThreadStackUserData
{
	uint64* BackTrace;
	int32 BackTraceCount;
	SIZE_T CallStackSize;
};
#define THREAD_CALLSTACK_GENERATOR SIGRTMIN + 5
#endif // ANDROID_HAS_THREADBACKTRACE

typedef FAndroidPlatformStackWalk FPlatformStackWalk;
