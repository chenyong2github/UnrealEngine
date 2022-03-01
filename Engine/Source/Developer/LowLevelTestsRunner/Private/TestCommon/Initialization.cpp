// Copyright Epic Games, Inc. All Rights Reserved.

#include "TestCommon/Initialization.h"


void InitAllThreadPoolsEditorEx(bool MultiThreaded)
{
#if WITH_EDITOR
	InitEditorThreadPools();
#endif // WITH_EDITOR
	InitAllThreadPools(MultiThreaded);
}

void InitOutputDevicesEx()
{
#if WITH_APPLICATION_CORE
	InitOutputDevicesAppCore();
#else
	InitOutputDevices();
#endif
}

void InitStats()
{
#if STATS
	FThreadStats::StartThread();
#endif // #if STATS
}

void InitAll(bool bAllowLogging, bool bMultithreaded)
{
	InitCommandLine(bAllowLogging);
	InitAllThreadPools(bMultithreaded);
#if WITH_ENGINE
	InitAsyncQueues();
#endif // WITH_ENGINE
	InitTaskGraph();
	InitOutputDevices();
#if WITH_ENGINE
	InitRendering();
#endif // WITH_ENGINE
#if WITH_EDITOR
	InitDerivedDataCache();
	InitSlate();
	InitForWithEditorOnlyData();
	InitEditor();
#endif // WITH_EDITOR
#if WITH_COREUOBJECT
	InitCoreUObject();
#endif
}

void CleanupAll()
{
#if WITH_COREUOBJECT
	CleanupCoreUObject();
#endif
	CleanupAllThreadPools();
	CleanupTaskGraph();
}