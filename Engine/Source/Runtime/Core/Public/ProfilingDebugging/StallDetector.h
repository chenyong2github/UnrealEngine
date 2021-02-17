// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"

#include "Containers/UnrealString.h"
#include "Containers/Set.h"
#include "HAL/CriticalSection.h"
#include "HAL/PreprocessorHelpers.h"
#include "Logging/LogMacros.h"
#include "Misc/ScopeLock.h"

#include <atomic>

#ifndef STALL_DETECTOR
 #if WITH_EDITOR && PLATFORM_WINDOWS
  #define STALL_DETECTOR 1
 #else // WITH_EDITOR && PLATFORM_WINDOWS
  #define STALL_DETECTOR 0
 #endif // WITH_EDITOR && PLATFORM_WINDOWS
#endif // STALL_DETECTOR

#if STALL_DETECTOR

CORE_API DECLARE_LOG_CATEGORY_EXTERN(LogStall, Log, All);

/**
* This code is meant to instrument code to send reports for slow pathways that create unresponsive conditions
**/

namespace UE
{
	/**
	* The reporting behavior of the detector
	**/
	enum EStallDetectorReportingMode
	{
		Disabled,
		First,
		Always
	};

	/**
	* Information to track per-callsite (vs. lifetime of a single interval)
	**/
	class CORE_API FStallDetectorStats
	{
	public:
		/**
		* Construct a stats object for use with a stall detector
		*/
		FStallDetectorStats(const TCHAR* InName, const double InBudgetSeconds, const EStallDetectorReportingMode InReportingMode);

		/**
		* Destruct a stats object, just removes it from the instance tracking
		*/
		~FStallDetectorStats();

		/**
		* Stall dettector scope has ended, record the interval
		*/
		void OnStallCompleted(double InOverageSeconds);

		/**
		* Retrieve the critical section (lock) for accessing the instances list
		*/
		static FCriticalSection& GetInstancesSection()
		{
			return InstancesSection;
		}

		/**
		* The container of all constructed instances of this class
		*/
		static const TSet<FStallDetectorStats*>& GetInstances()
		{
			return Instances;
		}

		/**
		* 
		*/
		struct TabulatedResult
		{
			const UE::FStallDetectorStats* Stats;
			uint32 TriggerCount;
			double OverageSeconds;
			
			TabulatedResult()
				: Stats(nullptr)
				, TriggerCount(0)
				, OverageSeconds(0.0)
			{}
		};
		static void TabulateStats(TArray<TabulatedResult>& TabulatedResults);

		// The name to refer to this callsite in the codebase
		const TCHAR* Name;

		// The budgeted time we tolerate without sending a report
		const double BudgetSeconds;

		// Drives the behavior about what to do when a budget is violated (triggered)
		const EStallDetectorReportingMode ReportingMode;

		// Has this been reported yet, this is different than the trigger count for coherency reasons
		bool bReported;

		// The total number of times all callsites have been triggered
		static std::atomic<uint32> TotalTriggeredCount;

		// The total number of reports that have been sent
		static std::atomic<uint32> TotalReportedCount;

	private:
		// The number of times this callsite has been triggered
		uint32 TriggerCount;

		// The cumulative overage time for this callsite
		double OverageSeconds;

		// Guards access to the stats from multiple threads, for coherency
		mutable FCriticalSection StatsSection;

		// Guards access to the instances container from multiple threads
		static FCriticalSection InstancesSection;

		// The pointers to every constructed instance of this class
		static TSet<FStallDetectorStats*> Instances;
	};

	/**
	* FStallDetector is meant to be constructed and destructed across a single scope to measure that specific timespan.
	**/
	class CORE_API FStallDetector
	{
	public:
		/**
		* Special const value for invalid timestamps, used to detect races of startup/shutdown and threads
		*/
		static const double InvalidSeconds;

		/**
		* Construct the stall detector linking it to it's stats object and take a timestamp
		*/
		FStallDetector(FStallDetectorStats& InStats);

		/**
		* Destruct the stall detector and perform a check if appropriate (the default behavior)
		*/
		~FStallDetector();

		/**
		* Check the detector to see if we are over budget, called from both construction and background thread
		* @param bIsComplete			Is this check the one performed at completion of the scope or measurement period?
		* @param InWhenToCheckSeconds	The timestamp at which to compare the start time to
		**/
		void Check(bool bIsComplete, double InWhenToCheckSeconds = InvalidSeconds);

		/**
		* Perform a Check and then reset the timer to the current time.
		* This changes the mode of this class to defer the first check, and skip the destruction check
		* This mode enables this class to used as a global or static, and CheckAndReset() callable periodically
		**/
		void CheckAndReset();

		/**
		* Sample the same clock that the stall detector uses internally
		**/
		static double Seconds();

		/**
		* Retrieve the critical section (lock) for accessing the instances list
		*/
		static FCriticalSection& GetInstancesSection()
		{
			return InstancesSection;
		}

		/**
		* The container of all constructed instances of this class
		*/
		static const TSet<FStallDetector*>& GetInstances()
		{
			return Instances;
		}

		/**
		* Initialize Stall Detector API resources
		**/
		static void Startup();

		/**
		* Cleanup Stall Detector API resources
		**/
		static void Shutdown();

		/**
		* Stall Detector API is running?
		**/
		static bool IsRunning();

	private:
		// The time provided, the deadline
		FStallDetectorStats& Stats;

		// Thread it was constructed on, and to stack trace when budgeted time is elapsed
		uint32 ThreadId;

		// The time of construction of this object
		double StartSeconds;

		// Persistent usage mode
		bool bPersistent;

		// Track the triggered state, atomic to mitigate foreground and background thread checking
		std::atomic<bool> Triggered;

		// The pinch point for tracking counts and emitting a report for a triggered hitch detector
		void OnStallDetected(uint32 InThreadId, const double InElapsedSeconds);

		// Guards access to the instances container from multiple threads
		static FCriticalSection InstancesSection;

		// The pointers to every constructed instance of this class
		static TSet<FStallDetector*> Instances;
	};
}

/**
* Counts, but does not report, a stall.
**/
#define SCOPE_STALL_COUNTER(InName, InBudgetSeconds) \
static UE::FStallDetectorStats PREPROCESSOR_JOIN(StallDetectorStats, __LINE__) (TEXT(#InName), InBudgetSeconds, UE::EStallDetectorReportingMode::Disabled); \
UE::FStallDetector PREPROCESSOR_JOIN(ScopeStallDetector, __LINE__)(PREPROCESSOR_JOIN(StallDetectorStats, __LINE__));

/**
* Counts and sends a report of the first occurence of a stall.
**/
#define SCOPE_STALL_REPORTER(InName, InBudgetSeconds) \
static UE::FStallDetectorStats PREPROCESSOR_JOIN(StallDetectorStats, __LINE__) (TEXT(#InName), InBudgetSeconds, UE::EStallDetectorReportingMode::First); \
UE::FStallDetector PREPROCESSOR_JOIN(ScopeStallDetector, __LINE__)(PREPROCESSOR_JOIN(StallDetectorStats, __LINE__));

/**
* Counts and reports every occurence of a stall.
* Use with extreme caution as you could overload the report backend!
* This mainly exists to help test and exercise the stall detector system.
**/
#define SCOPE_STALL_REPORTER_ALWAYS(InName, InBudgetSeconds) \
static UE::FStallDetectorStats PREPROCESSOR_JOIN(StallDetectorStats, __LINE__) (TEXT(#InName), InBudgetSeconds, UE::EStallDetectorReportingMode::Always); \
UE::FStallDetector PREPROCESSOR_JOIN(ScopeStallDetector, __LINE__)(PREPROCESSOR_JOIN(StallDetectorStats, __LINE__));

#else // STALL_DETECTOR

#define SCOPE_STALL_COUNTER(InName, InBudgetSeconds)
#define SCOPE_STALL_REPORTER(InName, InBudgetSeconds)
#define SCOPE_STALL_REPORTER_ALWAYS(InName, InBudgetSeconds)

#endif // STALL_DETECTOR