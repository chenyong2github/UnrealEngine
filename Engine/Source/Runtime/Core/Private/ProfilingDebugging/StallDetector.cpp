// Copyright Epic Games, Inc. All Rights Reserved.
#include "ProfilingDebugging/StallDetector.h"

#if STALL_DETECTOR

#include "HAL/ExceptionHandling.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformStackWalk.h"
#include "HAL/PlatformTime.h"
#include "HAL/PlatformTLS.h"
#include "HAL/Runnable.h"
#include "HAL/RunnableThread.h"

// counters for sending information into trace system
TRACE_DECLARE_INT_COUNTER(StallCount, TEXT("StallDetector/Count"));
TRACE_DECLARE_FLOAT_COUNTER(StallTimeSeconds, TEXT("StallDetector/TimeSeconds"));

// force normal behavior in the face of debug configuration and debugger attached
#define STALL_DETECTOR_DEBUG 0

// use the heart beat clock to account for process suspend
#define STALL_DETECTOR_HEART_BEAT_CLOCK 1

#if STALL_DETECTOR_DEBUG && _MSC_VER
#pragma optimize( "", off )
#endif // STALL_DETECTOR_DEBUG && _MSC_VER

#if STALL_DETECTOR_HEART_BEAT_CLOCK
 #include "HAL/ThreadHeartBeat.h"
#endif // STALL_DETECTOR_HEART_BEAT_CLOCK

DEFINE_LOG_CATEGORY(LogStall);

/**
* Globals
**/

// The reference count for the resources for this API
static int32 InitCount = 0;

/**
* Stall Detector Thread
**/

namespace UE
{
	class FStallDetectorRunnable : public FRunnable
	{
	public:
		FStallDetectorRunnable();

		// FRunnable implementation
		virtual uint32 Run() override;
		virtual void Stop() override
		{
			StopThread = true;
		}
		virtual void Exit() override
		{
			Stop();
		}

		bool GetStartedThread()
		{
			return StartedThread;
		}
		
#if STALL_DETECTOR_HEART_BEAT_CLOCK
		FThreadHeartBeatClock& GetClock()
		{
			return Clock;
		}
#endif

	private:
		bool StartedThread;
		bool StopThread;

#if STALL_DETECTOR_HEART_BEAT_CLOCK
		FThreadHeartBeatClock Clock;
#endif
	};

	static FStallDetectorRunnable* Runnable = nullptr;
	static FRunnableThread* Thread = nullptr;
}

UE::FStallDetectorRunnable::FStallDetectorRunnable()
	: StartedThread(false)
	, StopThread(false)
#if STALL_DETECTOR_HEART_BEAT_CLOCK
	, Clock(50.0/1000.0) // the clamped time interval that each tick of the clock can possibly advance
#endif
{
}

uint32 UE::FStallDetectorRunnable::Run()
{
	while (!StopThread)
	{
#if STALL_DETECTOR_HEART_BEAT_CLOCK
		Clock.Tick();
#endif

		// Clock has been ticked
		StartedThread = true;

		// Use this timestamp to try to avoid marginal triggering
		double Seconds = FStallDetector::Seconds();

		// Check the detectors
		if (Seconds != FStallDetector::InvalidSeconds)
		{
			FScopeLock ScopeLock(&FStallDetector::GetInstancesSection());
			for (FStallDetector* Detector : FStallDetector::GetInstances())
			{
				Detector->Check(false, Seconds);
			}
		}

		// Sleep an interval, the resolution at which we want to detect an overage
		FPlatformProcess::SleepNoStats(0.005);
	}

	return 0;
}

/**
* Stall Detector Stats
**/

FCriticalSection UE::FStallDetectorStats::InstancesSection;
TSet<UE::FStallDetectorStats*> UE::FStallDetectorStats::Instances;
FCountersTrace::TCounter<std::atomic<int64>, TraceCounterType_Int> UE::FStallDetectorStats::TotalTriggeredCount (TEXT("StallDetector/TotalTriggeredCount"), TraceCounterDisplayHint_None);
FCountersTrace::TCounter<std::atomic<int64>, TraceCounterType_Int> UE::FStallDetectorStats::TotalReportedCount (TEXT("StallDetector/TotalReportedCount"), TraceCounterDisplayHint_None);

UE::FStallDetectorStats::FStallDetectorStats(const TCHAR* InName, const double InBudgetSeconds, const EStallDetectorReportingMode InReportingMode)
	: Name(InName)
	, BudgetSeconds(InBudgetSeconds)
	, ReportingMode(InReportingMode)
	, bReported(false)
	, TriggerCount(
		(
			FCString::Strcat(TriggerCountCounterName, TEXT("StallDetector/")),
			FCString::Strcat(TriggerCountCounterName, InName),
			FCString::Strcat(TriggerCountCounterName, TEXT(" TriggerCount"))
		), TraceCounterDisplayHint_None)
	, OverageSeconds(
		(
			FCString::Strcat(OverageSecondsCounterName, TEXT("StallDetector/")),
			FCString::Strcat(OverageSecondsCounterName, InName),
			FCString::Strcat(OverageSecondsCounterName, TEXT(" OverageSeconds"))
		), TraceCounterDisplayHint_None)
{
	// Add at the end of construction
	FScopeLock ScopeLock(&InstancesSection);
	Instances.Add(this);
}

UE::FStallDetectorStats::~FStallDetectorStats()
{
	// Remove at the beginning of destruction
	FScopeLock ScopeLock(&InstancesSection);
	Instances.Remove(this);
}

void UE::FStallDetectorStats::OnStallCompleted(double InOverageSeconds)
{
	// we sync access around these for coherency reasons, can be polled from another thread (tabulation)
	FScopeLock StatsLock(&StatsSection);
	TriggerCount.Increment();
	OverageSeconds.Add(InOverageSeconds);
}

void UE::FStallDetectorStats::TabulateStats(TArray<TabulatedResult>& TabulatedResults)
{
	TabulatedResults.Empty();

	struct SortableStallStats
	{
		explicit SortableStallStats(const UE::FStallDetectorStats* InStats)
			: StallStats(InStats)
			, OverageRatio(0.0)
		{
			FScopeLock Lock(&InStats->StatsSection);
			if (InStats->TriggerCount.Get() && InStats->BudgetSeconds > 0.0)
			{
				OverageRatio = (InStats->OverageSeconds.Get() / InStats->TriggerCount.Get()) / InStats->BudgetSeconds;
			}
		}

		bool operator<(const SortableStallStats& InRhs) const
		{
			// NOTE THIS IS REVERSED TO PUT THE MOST OVERAGE AT THE FRONT
			return OverageRatio > InRhs.OverageRatio;
		}

		const UE::FStallDetectorStats* StallStats;
		double OverageRatio;
	};

	TArray<SortableStallStats> StatsArray;
	FScopeLock InstancesLock(&UE::FStallDetectorStats::GetInstancesSection());
	for (const UE::FStallDetectorStats* StallStats : UE::FStallDetectorStats::GetInstances())
	{
		if (StallStats->TriggerCount.Get() && StallStats->ReportingMode != UE::EStallDetectorReportingMode::Disabled)
		{
			StatsArray.Emplace(StallStats);
		}
	}

	if (!StatsArray.IsEmpty())
	{
		StatsArray.Sort();

		for (const SortableStallStats& Stat : StatsArray)
		{
			TabulatedResults.Emplace(TabulatedResult());
			TabulatedResult& Result(TabulatedResults.Last());
			Result.Stats = Stat.StallStats;

			// we sync access around these for coherency reasons, can be polled from another thread (detector or scope)
			FScopeLock Lock(&Result.Stats->StatsSection);
			Result.TriggerCount = Result.Stats->TriggerCount.Get();
			Result.OverageSeconds = Result.Stats->OverageSeconds.Get();
		}
	}
}


/**
* Stall Detector
**/

const double UE::FStallDetector::InvalidSeconds = -1.0;
FCriticalSection UE::FStallDetector::InstancesSection;
TSet<UE::FStallDetector*> UE::FStallDetector::Instances;

UE::FStallDetector::FStallDetector(FStallDetectorStats& InStats)
	: Stats(InStats)
	, ThreadId(0)
	, StartSeconds(InvalidSeconds)
	, bPersistent(false)
	, Triggered(false)
{
	if (FStallDetector::IsRunning())
	{
		ThreadId = FPlatformTLS::GetCurrentThreadId();
		StartSeconds = FStallDetector::Seconds();
	}

	// Add at the end of construction
	FScopeLock ScopeLock(&InstancesSection);
	Instances.Add(this);
}

UE::FStallDetector::~FStallDetector()
{
	// Remove at the beginning of destruction
	{
		FScopeLock ScopeLock(&InstancesSection);
		Instances.Remove(this);
	}

	if (FStallDetector::IsRunning())
	{
		if (!bPersistent)
		{
			Check(true);
		}
	}
}

void UE::FStallDetector::Check(bool bIsComplete, double InWhenToCheckSeconds)
{
	// StartSeconds checks that system was started when this detector was constructed
	bool bInitialized = FStallDetector::IsRunning() && StartSeconds != InvalidSeconds;
	if (!bInitialized)
	{
		return;
	}

	double CheckSeconds = InWhenToCheckSeconds;
	if (InWhenToCheckSeconds == InvalidSeconds)
	{
		CheckSeconds = FStallDetector::Seconds();
		if (CheckSeconds == InvalidSeconds)
		{
			return;
		}
	}

	double DeltaSeconds = CheckSeconds - StartSeconds;
	double OverageSeconds = DeltaSeconds - Stats.BudgetSeconds;

	if (Triggered)
	{
		if (bIsComplete)
		{
			Stats.OnStallCompleted(OverageSeconds);

#if STALL_DETECTOR_DEBUG
			FString OverageString = FString::Printf(TEXT("[FStallDetector] [%s] Overage of %f\n"), Stats.Name, OverageSeconds);
			FPlatformMisc::LocalPrint(OverageString.GetCharArray().GetData());
#endif
			if (Stats.ReportingMode != EStallDetectorReportingMode::Disabled)
			{
				UE_LOG(LogStall, Log, TEXT("Stall detector '%s' complete in %fs (%fs overbudget)"), Stats.Name, DeltaSeconds, OverageSeconds);
			}
		}
	}
	else
	{
		if (OverageSeconds > 0.0)
		{
			bool PreviousTriggered = false;
			if (Triggered.compare_exchange_strong(PreviousTriggered, true, std::memory_order_acquire, std::memory_order_relaxed))
			{
#if STALL_DETECTOR_DEBUG
				FString OverageString = FString::Printf(TEXT("[FStallDetector] [%s] Triggered at %f\n"), Stats.Name, CheckSeconds);
				FPlatformMisc::LocalPrint(OverageString.GetCharArray().GetData());
#endif
				OnStallDetected(ThreadId, DeltaSeconds);
			}
		}
	}
}

void UE::FStallDetector::CheckAndReset()
{
	// StartSeconds checks that system was started when this detector was constructed
	bool bInitialized = FStallDetector::IsRunning() && StartSeconds != InvalidSeconds;
	if (!bInitialized)
	{
		return;
	}

	double CheckSeconds = FStallDetector::Seconds();
	if (CheckSeconds == InvalidSeconds)
	{
		return;
	}

	// if this is the first call to CheckAndReset
	if (!bPersistent)
	{
		// never the first call again, because the timespan between construction and the first call isn't valid don't perform a check
		bPersistent = true;
	}
	else
	{
		// only perform the check on the second call
		Check(true, CheckSeconds);
	}

	StartSeconds = CheckSeconds;
	Triggered = false;
}

void UE::FStallDetector::OnStallDetected(uint32 InThreadId, const double InElapsedSeconds)
{
	Stats.TotalTriggeredCount.Increment();

	//
	// Determine if we want to undermine the specified reporting mode
	//

	EStallDetectorReportingMode ReportingMode = Stats.ReportingMode;

	bool bDisableReporting = false;

#if UE_BUILD_DEBUG
	bDisableReporting |= true; // Do not generate a report in debug configurations due to performance characteristics
#endif

#if !STALL_DETECTOR_DEBUG
	bDisableReporting |= FPlatformMisc::IsDebuggerPresent(); // Do not generate a report if we detect the debugger mucking with things
#endif

	if (bDisableReporting)
	{
#if !STALL_DETECTOR_DEBUG
		ReportingMode = EStallDetectorReportingMode::Disabled;
#endif
	}

	//
	// Resolve reporting mode to whether we should send a report for this call
	//

	bool bSendReport = false;
	switch (ReportingMode)
	{
	case EStallDetectorReportingMode::First:
		bSendReport = !Stats.bReported;
		break;

	case EStallDetectorReportingMode::Always:
		bSendReport = true;
		break;

	default:
		break;
	}

	//
	// Send the report
	//

	if (bSendReport)
	{
		Stats.bReported = true;
		Stats.TotalReportedCount.Increment();
		const int NumStackFramesToIgnore = FPlatformTLS::GetCurrentThreadId() == InThreadId ? 2 : 0;
		UE_LOG(LogStall, Log, TEXT("Stall detector '%s' exceeded budget of %fs, reporting..."), Stats.Name, Stats.BudgetSeconds);
		double ReportSeconds = FStallDetector::Seconds();
		ReportStall(Stats.Name, InThreadId);
		ReportSeconds = FStallDetector::Seconds() - ReportSeconds;
		UE_LOG(LogStall, Log, TEXT("Stall detector '%s' report submitted, and took %fs"), Stats.Name, ReportSeconds);
	}
	else
	{
		if (ReportingMode != EStallDetectorReportingMode::Disabled)
		{
			UE_LOG(LogStall, Log, TEXT("Stall detector '%s' exceeded budget of %fs"), Stats.Name, Stats.BudgetSeconds);
		}
	}
}

double UE::FStallDetector::Seconds()
{
	double Result = InvalidSeconds;

	if (FStallDetector::IsRunning())
	{
#if STALL_DETECTOR_HEART_BEAT_CLOCK
		Result = Runnable->GetClock().Seconds();
#else
		Result = FPlatformTime::Seconds();
#endif

#if STALL_DETECTOR_DEBUG
		static double ClockStartSeconds = Result;
		static double PlatformStartSeconds = FPlatformTime::Seconds();
		double ClockDelta = Result - ClockStartSeconds;
		double PlatformDelta = FPlatformTime::Seconds() - PlatformStartSeconds;
		double Drift = PlatformDelta - ClockDelta;
		static double LastDrift = Drift;
		double DriftDelta = Drift - LastDrift;
		if (DriftDelta > 0.001)
		{
			FString ResultString = FString::Printf(TEXT("[FStallDetector] Thread %5d / Platform: %f / Clock: %f / Drift: %f (%f)\n"), FPlatformTLS::GetCurrentThreadId(), PlatformDelta, ClockDelta, Drift, DriftDelta);
			FPlatformMisc::LocalPrint(ResultString.GetCharArray().GetData());
			LastDrift = Drift;
		}
#endif
	}

	return Result;
}

void UE::FStallDetector::Startup()
{
	check(InitCount >= 0);
	if (++InitCount == 1)
	{
		UE_LOG(LogStall, Log, TEXT("Startup..."));

		check(FPlatformTime::GetSecondsPerCycle());

		// Cannot be a global due to clock member
		Runnable = new FStallDetectorRunnable();

		if (Thread == nullptr)
		{
			Thread = FRunnableThread::Create(Runnable, TEXT("StallDetectorThread"));
			check(Thread);

			// Poll until we have ticked the clock
			while (!Runnable->GetStartedThread())
			{
				FPlatformProcess::YieldThread();
			}
		}

		UE_LOG(LogStall, Log, TEXT("Startup complete."));
	}
}

void UE::FStallDetector::Shutdown()
{
	if (--InitCount == 0)
	{
		UE_LOG(LogStall, Log, TEXT("Shutdown..."));

		delete Thread;
		Thread = nullptr;

		delete Runnable;
		Runnable = nullptr;
		
		UE_LOG(LogStall, Log, TEXT("Shutdown complete."));
	}
	check(InitCount >= 0);
}

bool UE::FStallDetector::IsRunning()
{
	return InitCount > 0;
}

#endif // STALL_DETECTOR