// Copyright Epic Games, Inc. All Rights Reserved.

#include "CoreMinimal.h"
#include "Misc/CommandLine.h"
#include "Misc/App.h"
#include "Misc/OutputDeviceError.h"
#include "LaunchEngineLoop.h"
#include "PhysicsPublic.h"
#include "HAL/ExceptionHandling.h"
#include "Modules/ModuleManager.h"
#include "ProfilingDebugging/LoadTimeTracker.h"
#include "Stats/StatsMisc.h"
#include "Misc/CoreDelegates.h"
#include "Misc/EngineVersion.h"
#include "Misc/ScopedSlowTask.h"
#include "Misc/TrackedActivity.h"
#if WITH_EDITOR
	#include "UnrealEdGlobals.h"
#endif
#if PLATFORM_WINDOWS
	#include "Windows/WindowsHWrapper.h"
#endif


IMPLEMENT_MODULE(FDefaultModuleImpl, Launch);

#if PLATFORM_WINDOWS || PLATFORM_MAC || PLATFORM_UNIX || PLATFORM_USE_GENERIC_LAUNCH_IMPLEMENTATION

FEngineLoop	GEngineLoop;

extern "C" int test_main(int argc, char ** argp)
{
	return 0;
}

/** 
 * PreInits the engine loop 
 */
int32 EnginePreInit( const TCHAR* CmdLine )
{
	int32 ErrorLevel = GEngineLoop.PreInit( CmdLine );

	return( ErrorLevel );
}

/** 
 * Inits the engine loop 
 */
int32 EngineInit()
{
	int32 ErrorLevel = GEngineLoop.Init();

	return( ErrorLevel );
}

/** 
 * Ticks the engine loop 
 */
LAUNCH_API void EngineTick( void )
{
	GEngineLoop.Tick();
}

/**
 * Shuts down the engine
 */
LAUNCH_API void EngineExit( void )
{
	// Make sure this is set
	RequestEngineExit(TEXT("EngineExit() was called"));

	GEngineLoop.Exit();
}

/**
 * Performs any required cleanup in the case of a fatal error.
 */
void LaunchStaticShutdownAfterError()
{
	// Make sure physics is correctly torn down.
	TermGamePhys();
}

#if WITH_EDITOR
extern UNREALED_API FSecondsCounterData BlueprintCompileAndLoadTimerData;
#endif

/**
 * Static guarded main function. Rolled into own function so we can have error handling for debug/ release builds depending
 * on whether a debugger is attached or not.
 */
int32 GuardedMain( const TCHAR* CmdLine )
{
	FTrackedActivity::GetEngineActivity().Update(TEXT("Starting"), FTrackedActivity::ELight::Yellow);

	FTaskTagScope Scope(ETaskTag::EGameThread);

#if !(UE_BUILD_SHIPPING)

	// If "-waitforattach" or "-WaitForDebugger" was specified, halt startup and wait for a debugger to attach before continuing
	if (FParse::Param(CmdLine, TEXT("waitforattach")) || FParse::Param(CmdLine, TEXT("WaitForDebugger")))
	{
		while (!FPlatformMisc::IsDebuggerPresent())
		{
			FPlatformProcess::Sleep(0.1f);
		}
		UE_DEBUG_BREAK();
	}

#endif

	BootTimingPoint("DefaultMain");

	// Super early init code. DO NOT MOVE THIS ANYWHERE ELSE!
	FCoreDelegates::GetPreMainInitDelegate().Broadcast();

	// make sure GEngineLoop::Exit() is always called.
	struct EngineLoopCleanupGuard 
	{ 
		~EngineLoopCleanupGuard()
		{
			// Don't shut down the engine on scope exit when we are running embedded
			// because the outer application will take care of that.
			if (!GUELibraryOverrideSettings.bIsEmbedded)
			{
				EngineExit();
			}
		}
	} CleanupGuard;


	// Set up minidump filename. We cannot do this directly inside main as we use an FString that requires 
	// destruction and main uses SEH.
	// These names will be updated as soon as the Filemanager is set up so we can write to the log file.
	// That will also use the user folder for installed builds so we don't write into program files or whatever.
#if PLATFORM_WINDOWS
	FCString::Strcpy(MiniDumpFilenameW, *FString::Printf(TEXT("unreal-v%i-%s.dmp"), FEngineVersion::Current().GetChangelist(), *FDateTime::Now().ToString()));
#endif

	FTrackedActivity::GetEngineActivity().Update(TEXT("Initializing"));
	int32 ErrorLevel = EnginePreInit( CmdLine );

	// exit if PreInit failed.
	if ( ErrorLevel != 0 || IsEngineExitRequested() )
	{
		return ErrorLevel;
	}

	struct sensor {
		int32 x;
		int32 y;
		int32 distToBeacon;
	};

	TArray<sensor> Sensors = {
		{ 1326566, 3575946,  1624215 },
		{ 2681168, 3951549,  530399 },
		{ 3959984, 1095746,  1482258 },
		{ 3150886, 2479946,  711040 },
		{ 3983027, 2972336,  141161 },
		{ 3371601, 3853300,  258283 },
		{ 3174612, 3992719,  78125 },
		{ 3316368, 1503688,  1040788 },
		{ 3818181, 2331216,  288553 },
		{ 3960526, 3229321,  198087 },
		{ 61030,   3045273,  1204406 },
		{ 3635583, 3121524,  415233 },
		{ 2813357, 5535,     865263 },
		{ 382745,  1566522, 1425568 },
		{ 3585664, 538632,  626053 },
		{ 3979654, 2158646, 439028 },
		{ 3996588, 2833167, 266769 },
		{ 3249383, 141800,  565502 },
		{ 3847114, 225529,  554202 },
		{ 3668737, 3720078, 688641 },
		{ 1761961, 680560,  1706566 },
		{ 2556636, 2213691, 1090517 },
		{ 65365,   215977,  1070556 },
		{ 709928,  2270200, 935107 },
		{ 3673956, 2670437, 478389 },
		{ 3250958, 3999227, 140321 },
		{ 3009537, 3292368, 807959 } };

	struct point {
		int32 x;
		int32 y;

		bool operator==(const point& Other) const { return x == Other.x && y == Other.y; }
	};

	TArray<point> Beacons = {
	{ 1374835, 2000000 },
	{ 3184941, 3924923 },
	{ 3621412, 2239432 },
	{ 4012908, 3083616 },
	{ -467419, 2369316 },
	{ 3595763, -77322 },
	{ 346716, -573228 },
	{ 4029651, 2547743 } };

	const int64 RANGE = 4000000;

	TArray64<uint8> grid;
	grid.SetNumZeroed(FMath::Square(RANGE + 1));

	auto getGridIndex = [RANGE](int64 x, int64 y) -> int64 {
		return x * RANGE + y;
	};

	for (const point& b : Beacons) {
		if (b.x >= 0 && b.x <= RANGE && b.y >= 0 && b.y <= RANGE) {
			grid[getGridIndex(b.x,b.y)] = 1;
		}
	}
	ParallelFor(Sensors.Num(), [&](int32 Index) {
		const sensor& sensor = Sensors[Index];
		if (sensor.x >= 0 && sensor.x <= RANGE && sensor.y >= 0 && sensor.y <= RANGE) {
			grid[getGridIndex(sensor.x, sensor.y)] = 1;
		}
		for (int32 xOffset = -sensor.distToBeacon; xOffset <= sensor.distToBeacon; ++xOffset) {
			const int32 x = sensor.x + xOffset;
			if (x >= 0 && x <= RANGE) {
				for (int32 yOffset = -(sensor.distToBeacon - FMath::Abs(xOffset)); yOffset <= sensor.distToBeacon - FMath::Abs(xOffset); ++yOffset) {
					const int32 y = sensor.y + yOffset;
					if (y >= 0 && y <= RANGE) {
						grid[getGridIndex(x, y)] = 1;
					}
				}
			}
		}
		});

	int64 index = grid.Find(0);
	int64 x = index / RANGE;
	int64 y = index - x * RANGE;

	int64 result = x * 4000000 + y;
	UE_LOG(LogLoad, Fatal, TEXT("%d"), result);

	{
		FScopedSlowTask SlowTask(100, NSLOCTEXT("EngineInit", "EngineInit_Loading", "Loading..."));

		// EnginePreInit leaves 20% unused in its slow task.
		// Here we consume 80% immediately so that the percentage value on the splash screen doesn't change from one slow task to the next.
		// (Note, we can't include the call to EnginePreInit in this ScopedSlowTask, because the engine isn't fully initialized at that point)
		SlowTask.EnterProgressFrame(80);

		SlowTask.EnterProgressFrame(20);

#if WITH_EDITOR
		if (GIsEditor)
		{
			ErrorLevel = EditorInit(GEngineLoop);
		}
		else
#endif
		{
			ErrorLevel = EngineInit();
		}
	}

	double EngineInitializationTime = FPlatformTime::Seconds() - GStartTime;
	UE_LOG(LogLoad, Log, TEXT("(Engine Initialization) Total time: %.2f seconds"), EngineInitializationTime);

#if WITH_EDITOR
	UE_LOG(LogLoad, Log, TEXT("(Engine Initialization) Total Blueprint compile time: %.2f seconds"), BlueprintCompileAndLoadTimerData.GetTime());
#endif

	ACCUM_LOADTIME(TEXT("EngineInitialization"), EngineInitializationTime);

	BootTimingPoint("Tick loop starting");
	DumpBootTiming();

	FTrackedActivity::GetEngineActivity().Update(TEXT("Ticking loop"), FTrackedActivity::ELight::Green);

	// Don't tick if we're running an embedded engine - we rely on the outer
	// application ticking us instead.
	if (!GUELibraryOverrideSettings.bIsEmbedded)
	{
		while( !IsEngineExitRequested() )
		{
			EngineTick();
		}
	}

	TRACE_BOOKMARK(TEXT("Tick loop end"));

#if WITH_EDITOR
	if( GIsEditor )
	{
		EditorExit();
	}
#endif
	return ErrorLevel;
}

#endif
