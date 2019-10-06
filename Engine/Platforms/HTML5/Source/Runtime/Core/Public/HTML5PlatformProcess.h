// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.


/*=============================================================================================
	HTML5Process.h: KickStart platform Process functions
==============================================================================================*/

#pragma once
#include "GenericPlatform/GenericPlatformProcess.h"

/** Dummy process handle for platforms that use generic implementation. */
struct FProcHandle : public TProcHandle<void*, nullptr>
{
public:
	/** Default constructor. */
	FORCEINLINE FProcHandle()
		: TProcHandle()
	{}

	/** Initialization constructor. */
	FORCEINLINE explicit FProcHandle( HandleType Other )
		: TProcHandle( Other )
	{}
};

/**
 * HTML5 implementation of the Process OS functions
 **/
struct CORE_API FHTML5PlatformProcess : public FGenericPlatformProcess
{
	static const TCHAR* ComputerName();
	static const TCHAR* BaseDir();
	static void Sleep(float Seconds);
	static void SleepNoStats(float Seconds);
	static void SleepInfinite();
	static class FEvent* CreateSynchEvent(bool bIsManualReset = 0);
	static class FRunnableThread* CreateRunnableThread();
	static bool SupportsMultithreading();
	static void LaunchURL( const TCHAR* URL, const TCHAR* Parms, FString* Error );
	static const TCHAR* ExecutableName(bool bRemoveExtension = true);
	static bool SkipWaitForStats()
	{
 		// CreateTask() is still crashing on HTML5 for both single threaded and multi threaded builds
		// TODO: check back after WASM w/multi-threading is in.... and try FPlatformProcess::SupportsMultithreading() again
		return true;
	}
};

typedef FHTML5PlatformProcess FPlatformProcess;
