// Copyright Epic Games, Inc. All Rights Reserved.

#include "StudioAnalytics.h"
#include "Misc/Guid.h"
#include "Stats/Stats.h"
#include "Misc/ConfigCacheIni.h"
#include "EngineGlobals.h"
#include "Engine/Engine.h"
#include "Misc/EngineBuildSettings.h"
#include "AnalyticsBuildType.h"
#include "AnalyticsEventAttribute.h"
#include "IAnalyticsProviderET.h"
#include "AnalyticsET.h"
#include "GeneralProjectSettings.h"
#include "Misc/EngineVersion.h"
#include "RHI.h"
#include "GenericPlatform/GenericPlatformCrashContext.h"
#include "Interfaces/IAnalyticsProvider.h"
#include "Templates/SharedPointer.h"
#include "HAL/PlatformProcess.h"

bool FStudioAnalytics::bInitialized = false;
volatile double FStudioAnalytics::TimeEstimation = 0;
FThread FStudioAnalytics::TimerThread;
TSharedPtr<IAnalyticsProvider> FStudioAnalytics::Analytics;

void FStudioAnalytics::SetProvider(TSharedRef<IAnalyticsProvider> InAnalytics)
{
	checkf(!Analytics.IsValid(), TEXT("FStudioAnalytics::SetProvider called more than once."));

	bInitialized = true;

	Analytics = InAnalytics;

	TimeEstimation = FPlatformTime::Seconds();

	if (FPlatformProcess::SupportsMultithreading())
	{
		TimerThread = FThread(TEXT("Studio Analytics Timer Thread"), []() { RunTimer_Concurrent(); });
	}
}

IAnalyticsProvider& FStudioAnalytics::GetProvider()
{
	checkf(IsAvailable(), TEXT("FStudioAnalytics::GetProvider called outside of Initialize/Shutdown."));

	return *Analytics.Get();
}

void FStudioAnalytics::RunTimer_Concurrent()
{
	TimeEstimation = FPlatformTime::Seconds();

	const double FixedInterval = 0.0333333333334;
	const double BreakpointHitchTime = 1;

	while (bInitialized)
	{
		const double StartTime = FPlatformTime::Seconds();
		FPlatformProcess::Sleep((float)FixedInterval);
		const double EndTime = FPlatformTime::Seconds();
		const double DeltaTime = EndTime - StartTime;

		if (DeltaTime > BreakpointHitchTime)
		{
			TimeEstimation += FixedInterval;
		}
		else
		{
			TimeEstimation += DeltaTime;
		}
	}
}

void FStudioAnalytics::Tick(float DeltaSeconds)
{

}

void FStudioAnalytics::Shutdown()
{
	ensure(!Analytics.IsValid() || Analytics.IsUnique());
	Analytics.Reset();

	bInitialized = false;

	if (TimerThread.IsJoinable())
	{
		TimerThread.Join();
	}
}

double FStudioAnalytics::GetAnalyticSeconds()
{
	return bInitialized ? TimeEstimation : FPlatformTime::Seconds();
}

void FStudioAnalytics::ReportEvent(const FString& EventName)
{
	ReportEvent(EventName, TArray<FAnalyticsEventAttribute>());
}

void FStudioAnalytics::ReportEvent(const FString& EventName, const TArray<FAnalyticsEventAttribute>& Attributes)
{
	if (FStudioAnalytics::IsAvailable())
	{
		FStudioAnalytics::GetProvider().RecordEvent(EventName, Attributes);
	}
}

void FStudioAnalytics::FireEvent_Loading(const FString& LoadingName, double SecondsSpentLoading, const TArray<FAnalyticsEventAttribute>& InAttributes)
{
	// Ignore anything less than a 1/4th a second.
	if (SecondsSpentLoading < 0.250)
	{
		return;
	}

	// Throw out anything over 10 hours - 
	if (SecondsSpentLoading > 36000)
	{
		return;
	}

	if (FStudioAnalytics::IsAvailable())
	{
		TArray<FAnalyticsEventAttribute> Attributes;
		Attributes.Emplace(TEXT("LoadingName"), LoadingName);
		Attributes.Emplace(TEXT("LoadingSeconds"), SecondsSpentLoading);
		Attributes.Append(InAttributes);

		FStudioAnalytics::GetProvider().RecordEvent(TEXT("Performance.Loading"), Attributes);
	}
}