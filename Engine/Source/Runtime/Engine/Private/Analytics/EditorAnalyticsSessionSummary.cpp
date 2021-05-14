// Copyright Epic Games, Inc. All Rights Reserved.

#include "EditorAnalyticsSessionSummary.h"

#if WITH_EDITOR

#include "Editor.h"
#include "IVREditorModule.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/PlatformTime.h"
#include "Interfaces/IAnalyticsPropertyStore.h"
#include "Kismet2/DebuggerCommands.h"
#include "ProfilingDebugging/StallDetector.h"

namespace EditorAnalyticsProperties
{
	// List of mutable keys.
	static const TAnalyticsProperty<uint32>  UserInteractionCount   = TEXT("UserInteractionCount");
	static const TAnalyticsProperty<int32>   Idle1Min               = TEXT("1MinIdle");
	static const TAnalyticsProperty<int32>   Idle5Min               = TEXT("5MinIdle");
	static const TAnalyticsProperty<int32>   Idle30Min              = TEXT("30MinIdle");
	static const TAnalyticsProperty<bool>    IsInPIE                = TEXT("IsInPIE");
	static const TAnalyticsProperty<bool>    IsInVRMode             = TEXT("IsInVRMode");

	static const TAnalyticsProperty<uint32>  TotalStallCount        = TEXT("TotalStallCount");
	static const TAnalyticsProperty<uint32>  TotalStallReported     = TEXT("TotalStallReported");
	static const TAnalyticsProperty<uint32>  TopStallTriggerCount   = TEXT("TopStallTriggerCount");
	static const TAnalyticsProperty<FString> TopStallName           = TEXT("TopStallName");
	static const TAnalyticsProperty<double>  TopStallBudgetSeconds  = TEXT("TopStallBudgetSeconds");
	static const TAnalyticsProperty<double>  TopStallOverageSeconds = TEXT("TopStallOverageSeconds");
	static const TAnalyticsProperty<uint32>  ProcessDiagnostics     = TEXT("ProcessDiagnostics"); // Whether some profiling/diagnostic tools are enabled, which could slow down the Editor.
	static const TAnalyticsProperty<uint32>  SummaryEventVersion    = TEXT("SummaryEventVersion"); // A version number used identify the key/set used. Can be used to compare before/after some changes too since comparing engine version is not always straingth foward.
}

FEditorAnalyticsSessionSummary::FEditorAnalyticsSessionSummary(TSharedPtr<IAnalyticsPropertyStore> Store, uint32 MonitorProcessId)
	: FEngineAnalyticsSessionSummary(MoveTemp(Store), MonitorProcessId)
	, LastUserActivityTimeSecs(FPlatformTime::Seconds())
	, AccountedUserIdleSecs(0)
{
	EditorAnalyticsProperties::UserInteractionCount.Set(GetStore(), 0);
	EditorAnalyticsProperties::Idle1Min.Set(GetStore(), 0);
	EditorAnalyticsProperties::Idle5Min.Set(GetStore(), 0);
	EditorAnalyticsProperties::Idle30Min.Set(GetStore(), 0);
	EditorAnalyticsProperties::IsInVRMode.Set(GetStore(), IVREditorModule::Get().IsVREditorModeActive());
	EditorAnalyticsProperties::IsInPIE.Set(GetStore(), FPlayWorldCommandCallbacks::IsInPIE());

	EditorAnalyticsProperties::TopStallName.Set(GetStore(), TEXT(""), /*Capacity*/128);
	EditorAnalyticsProperties::TopStallBudgetSeconds.Set(GetStore(), 0.0);
	EditorAnalyticsProperties::TopStallOverageSeconds.Set(GetStore(), 0.0);
	EditorAnalyticsProperties::TopStallTriggerCount.Set(GetStore(), 0);
	EditorAnalyticsProperties::TotalStallCount.Set(GetStore(), 0);
	EditorAnalyticsProperties::TotalStallReported.Set(GetStore(), 0);
	EditorAnalyticsProperties::ProcessDiagnostics.Set(GetStore(), static_cast<uint32>(FPlatformMisc::GetProcessDiagnostics()));

	// The current summary revision number. Identify the key set used and its behavior. If we add/remove keys, we should increment the number. The number can
	// also be incremented when we change some behaviors to be able to compare between versions.
	EditorAnalyticsProperties::SummaryEventVersion.Set(GetStore(), 1);

	// Persist the session to disk.
	GetStore()->Flush();

	FEditorDelegates::PreBeginPIE.AddRaw(this, &FEditorAnalyticsSessionSummary::OnEnterPIE);
	FEditorDelegates::EndPIE.AddRaw(this, &FEditorAnalyticsSessionSummary::OnExitPIE);
	FSlateApplication::Get().GetOnModalLoopTickEvent().AddRaw(this, &FEditorAnalyticsSessionSummary::Tick);
	FSlateApplication::Get().GetLastUserInteractionTimeUpdateEvent().AddRaw(this, &FEditorAnalyticsSessionSummary::OnSlateUserInteraction);
}

void FEditorAnalyticsSessionSummary::ShutdownInternal()
{
	FEditorDelegates::PreBeginPIE.RemoveAll(this);
	FEditorDelegates::EndPIE.RemoveAll(this);
	FSlateApplication::Get().GetOnModalLoopTickEvent().RemoveAll(this);
	FSlateApplication::Get().GetLastUserInteractionTimeUpdateEvent().RemoveAll(this);
}

bool FEditorAnalyticsSessionSummary::UpdateSessionProgressInternal(bool bCrashing)
{
	bool bShouldPersist = UpdateUserIdleTime(FPlatformTime::Seconds(), /*bReset*/false);

	// In case of crash, don't save anything else.
	if (bCrashing)
	{
		return true; // Yes, should persist.
	}

	EditorAnalyticsProperties::IsInVRMode.Set(GetStore(), IVREditorModule::Get().IsVREditorModeActive());
	EditorAnalyticsProperties::IsInPIE.Set(GetStore(), FPlayWorldCommandCallbacks::IsInPIE());

	// Accumulate stall stats
#if STALL_DETECTOR
	TArray<UE::FStallDetectorStats::TabulatedResult> StallResults;
	UE::FStallDetectorStats::TabulateStats(StallResults);
	if (!StallResults.IsEmpty())
	{
		UE::FStallDetectorStats::TabulatedResult TopResult(StallResults[0]);
		EditorAnalyticsProperties::TopStallName.Set(GetStore(), TopResult.Stats->Name, /*Capacity*/128);
		EditorAnalyticsProperties::TopStallBudgetSeconds.Set(GetStore(), TopResult.Stats->BudgetSeconds);
		EditorAnalyticsProperties::TopStallOverageSeconds.Set(GetStore(), TopResult.OverageSeconds);
		EditorAnalyticsProperties::TopStallTriggerCount.Set(GetStore(), TopResult.TriggerCount);
	}

	EditorAnalyticsProperties::TotalStallCount.Set(GetStore(), UE::FStallDetectorStats::TotalTriggeredCount.Get());
	EditorAnalyticsProperties::TotalStallReported.Set(GetStore(), UE::FStallDetectorStats::TotalReportedCount.Get());
#endif // STALL_DETECTOR

	return bShouldPersist;
}

bool FEditorAnalyticsSessionSummary::UpdateUserIdleTime(double CurrTimeSecs, bool bReset)
{
	bool bSessionUpdated = false;

	// How much time elapsed since the last activity.
	double TotalIdleSecs = CurrTimeSecs - LastUserActivityTimeSecs.load();
	if (TotalIdleSecs > 60.0) // Less than a minute is always considered normal interaction delay.
	{
		double LastAccountedIdleSecs = AccountedUserIdleSecs.load();
		double UnaccountedIdleSecs   = TotalIdleSecs - LastAccountedIdleSecs;

		// If one or more minute is unaccounted
		if (UnaccountedIdleSecs >= 60.0)
		{
			double AccountedIdleMins = FMath::FloorToDouble(LastAccountedIdleSecs/60); // Minutes already accounted for.
			double ToAccountIdleMins = FMath::FloorToDouble(UnaccountedIdleSecs/60);   // New minutes to account for (entire minute only)

			// Delta = LatestAccounted - AlreadyAccounted
			double DeltaIdle1Min  = FMath::Max(0.0, AccountedIdleMins + ToAccountIdleMins - 1)  - FMath::Max(0.0, AccountedIdleMins - 1);  // The first minute of this idle sequence is considered 'normal interaction delay' and in not accounted as idle.
			double DeltaIdle5Min  = FMath::Max(0.0, AccountedIdleMins + ToAccountIdleMins - 5)  - FMath::Max(0.0, AccountedIdleMins - 5);  // The 5 first minutes of this idle sequence are considered 'normal interaction delay' and are not accounted for the 5-min timer.
			double DeltaIdle30Min = FMath::Max(0.0, AccountedIdleMins + ToAccountIdleMins - 30) - FMath::Max(0.0, AccountedIdleMins - 30); // The 30 first minutes of this idle sequence are considered 'normal interaction delay' and are not accounted for the 30-min timer.

			// Ensure only one thread adds the current delta time.
			if (AccountedUserIdleSecs.compare_exchange_strong(LastAccountedIdleSecs, LastAccountedIdleSecs + ToAccountIdleMins * 60.0)) // Only add the 'accounted' minutes and keep fraction of minutes running.
			{
				EditorAnalyticsProperties::Idle1Min.Update(GetStore(), [DeltaIdle1Min](int32& InOutValue)
				{
					InOutValue += FMath::RoundToInt(static_cast<float>(DeltaIdle1Min));
					return true;
				});

				EditorAnalyticsProperties::Idle5Min.Update(GetStore(), [DeltaIdle5Min](int32& InOutValue)
				{
					InOutValue += FMath::RoundToInt(static_cast<float>(DeltaIdle5Min));
					return true;
				});

				EditorAnalyticsProperties::Idle30Min.Update(GetStore(), [DeltaIdle30Min](int32& InOutValue)
				{
					InOutValue += FMath::RoundToInt(static_cast<float>(DeltaIdle30Min));
					return true;
				});

				bSessionUpdated = true;
			}
		}
	}

	if (bReset)
	{
		AccountedUserIdleSecs.store(0);
		LastUserActivityTimeSecs.store(CurrTimeSecs);
	}

	return bSessionUpdated; // True if the idle timers were updated.
}

void FEditorAnalyticsSessionSummary::OnSlateUserInteraction(double CurrSlateInteractionTime)
{
	EditorAnalyticsProperties::UserInteractionCount.Update(GetStore(), [](uint32& InOutValue)
	{
		++InOutValue;
		return true;
	});

	// If the user input 'reset' the idle timers.
	double CurrTimeSecs = FPlatformTime::Seconds();
	if (UpdateUserIdleTime(CurrTimeSecs, /*bReset*/true))
	{
		GetStore()->Flush(/*bAsync*/true, FTimespan::Zero());
	}
}

void FEditorAnalyticsSessionSummary::OnEnterPIE(const bool /*bIsSimulating*/)
{
	EditorAnalyticsProperties::IsInPIE.Set(GetStore(), true);
	GetStore()->Flush(/*bAsync*/true, FTimespan::Zero());
}

void FEditorAnalyticsSessionSummary::OnExitPIE(const bool /*bIsSimulating*/)
{
	EditorAnalyticsProperties::IsInPIE.Set(GetStore(), false);
	GetStore()->Flush(/*bAsync*/true, FTimespan::Zero());
}

#endif // WITH_EDITOR
