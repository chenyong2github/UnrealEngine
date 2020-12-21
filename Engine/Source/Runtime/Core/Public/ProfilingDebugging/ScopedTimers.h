// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "Containers/UnrealString.h"
#include "CoreGlobals.h"
#include "HAL/PlatformTime.h"
#include "HAL/ThreadSafeCounter.h"
#include "Misc/OutputDeviceRedirector.h"
#include "HAL/ThreadSafeCounter64.h"

/**
 * Utility stopwatch class for tracking the duration of some action (tracks 
 * time in seconds and adds it to the specified variable on destruction).
 */
class FDurationTimer
{
public:
	explicit FDurationTimer(double& AccumulatorIn)
		: StartTime(FPlatformTime::Seconds())
		, Accumulator(AccumulatorIn)
	{}

	double Start()
	{
		StartTime = FPlatformTime::Seconds();
		return StartTime;
	}

	double Stop()
	{
		double StopTime = FPlatformTime::Seconds();
		Accumulator += (StopTime - StartTime);
		StartTime = StopTime;
			
		return StopTime;
	}

protected:
	/** Start time, captured in ctor. */
	double StartTime;
	/** Time variable to update. */
	double& Accumulator;
};

/**
 * Utility class for tracking the duration of a scoped action (the user 
 * doesn't have to call Start() and Stop() manually).
 */
class FScopedDurationTimer : public FDurationTimer
{
public:
	explicit FScopedDurationTimer(double& AccumulatorIn)
		: FDurationTimer(AccumulatorIn)
	{
	}

	/** Dtor, updating seconds with time delta. */
	~FScopedDurationTimer()
	{
		Stop();
	}
};

/**
 * Same as FScopedDurationTimer, except that it tracks the time value internally so you don't have to
 * pass in a double to accumulate.  Call GetTime() to get the total time since starting.
 */
class FAutoScopedDurationTimer : public FScopedDurationTimer
{
public:
	FAutoScopedDurationTimer()
		: FScopedDurationTimer(AccumulatorValue)
		, AccumulatorValue(0)
	{
	}

	double GetTime()
	{
		Stop();
		return AccumulatorValue;
	}

private:
	double AccumulatorValue;
};

/**
 * Utility class for tracking the duration of a scoped action to an accumulator in a thread-safe fashion.
 * Can accumulate into a 32bit or 64bit counter.
 * 
 * ThreadSafeCounterClass is expected to be a thread-safe type with a non-static member Add(uint32) that will work correctly if called from multiple threads simultaneously.
 */
template <typename ThreadSafeCounterClass>
class TScopedDurationThreadSafeTimer
{
public:
	explicit TScopedDurationThreadSafeTimer(ThreadSafeCounterClass& InCounter)
		:Counter(InCounter)
		, StartCycles(FPlatformTime::Cycles())
	{
	}
	~TScopedDurationThreadSafeTimer()
	{
		Counter.Add(FPlatformTime::Cycles() - StartCycles);
	}
private:
	ThreadSafeCounterClass& Counter;
	int32 StartCycles;
};

typedef TScopedDurationThreadSafeTimer<FThreadSafeCounter>   FScopedDurationThreadSafeTimer;
typedef TScopedDurationThreadSafeTimer<FThreadSafeCounter64> FScopedDurationThreadSafeTimer64;

/**
 * Utility class for logging the duration of a scoped action (the user 
 * doesn't have to call Start() and Stop() manually).
 */
class FScopedDurationTimeLogger
{
public:
	explicit FScopedDurationTimeLogger(FString InMsg = TEXT("Scoped action"), FOutputDevice* InDevice = GLog)
		: Msg        (MoveTemp(InMsg))
		, Device     (InDevice)
		, Accumulator(0.0)
		, Timer      (Accumulator)
	{
		Timer.Start();
	}

	~FScopedDurationTimeLogger()
	{
		Timer.Stop();
		Device->Logf(TEXT("%s: %f secs"), *Msg, Accumulator);
	}

private:
	FString        Msg;
	FOutputDevice* Device;
	double         Accumulator;
	FDurationTimer Timer;
};

/**
* Utility stopwatch class for tracking the duration of some action (tracks
* time in seconds and adds it to the specified variable on destruction).
* useful for timing that only wants to occur when a feature is optionally turned on 
*/
class FScopedSwitchedDurationTimer
{
public:
	explicit FScopedSwitchedDurationTimer(double& AccumulatorIn, bool bDoFunctionalityIn)
		: StartTime(bDoFunctionalityIn ? FPlatformTime::Seconds() : 0)
		, Accumulator(AccumulatorIn)
		, bDoFunctionality(bDoFunctionalityIn)
	{
	}

	~FScopedSwitchedDurationTimer()
	{
		if (bDoFunctionality)
		{
			Accumulator += (FPlatformTime::Seconds() - StartTime);
		}
	}

	double Start()
	{
		StartTime = FPlatformTime::Seconds();
		return StartTime;
	}

protected:
	/** Start time, captured in ctor. */
	double StartTime;
	/** Time variable to update. */
	double& Accumulator;
	const bool bDoFunctionality;
};

/**
* Utility stopwatch class for tracking the duration of some action (tracks
* time in seconds and adds it to the specified variable on destruction).
* useful for timing that only wants to occur when a feature is optionally turned on
* Also counts the number of timings
*/
class FScopedSwitchedCountedDurationTimer : FScopedSwitchedDurationTimer
{
public:
	explicit FScopedSwitchedCountedDurationTimer(double& TimeAccumulatorIn, int32& CountAccumlatorIn, bool bDoFunctionalityIn)
		: FScopedSwitchedDurationTimer(TimeAccumulatorIn, bDoFunctionalityIn)
	{
		if (bDoFunctionalityIn)
		{
			++CountAccumlatorIn;
		}
	}
};

/**
 * Utility class for logging the duration of a scoped action (the user 
 * doesn't have to call Start() and Stop() manually) using a custom
 * output function.
 */
class FScopedDurationTimeCustomLogger
{
public:
	explicit FScopedDurationTimeCustomLogger(const TCHAR* InMsg, const FLogCategoryBase& InLogCategory)
		: Msg(InMsg)
		, LogCategory(InLogCategory)
		, Accumulator(0.0)
		, Timer(Accumulator)
	{
		UE_LOG_REF(LogCategory, Display, TEXT("%s"), *Msg);
		Timer.Start();
	}

	~FScopedDurationTimeCustomLogger()
	{
		Timer.Stop();
		UE_LOG_REF(LogCategory, Display, TEXT("%s took %s"), *Msg, *SecondsToString(Accumulator));
	}

private:
	inline FString SecondsToString(double InSeconds)
	{
		if (InSeconds < 0.001)
		{
			uint64 MicroSeconds = (uint64)(InSeconds * 1000000.0);
			return FString::Printf(TEXT("%lluus"), MicroSeconds);
		}
		else if (InSeconds < 1.0)
		{
			uint64 MilliSeconds = (uint64)(InSeconds * 1000.0);
			return FString::Printf(TEXT("%llums"), MilliSeconds);
		}
		else if (InSeconds < 60.0)
		{
			return FString::Printf(TEXT("%.2fs"), InSeconds);
		}

		const int32 Hours = (int32)(InSeconds / 3600.0);
		InSeconds -= Hours * 3600;

		const int32 Minutes = (int32)(InSeconds / 60.0);
		InSeconds -= Minutes * 60;

		const int32 Seconds = (int32)InSeconds;
		InSeconds -= Seconds;

		if (Hours)
		{
			return FString::Printf(TEXT("%02dh %02dm %02ds"), Hours, Minutes, Seconds);
		}

		return FString::Printf(TEXT("%02dm %02ds"), Minutes, Seconds);
	}

	FString Msg;
	const FLogCategoryBase& LogCategory;
	double Accumulator;
	FDurationTimer Timer;
};

#if NO_LOGGING
#define UE_SCOPED_TIMER(Title, Category)
#else
#define UE_SCOPED_TIMER(Title, Category) \
	FScopedDurationTimeCustomLogger BODY_MACRO_COMBINE(Scoped,Timer,_,__LINE__)(Title, Category)
#endif
