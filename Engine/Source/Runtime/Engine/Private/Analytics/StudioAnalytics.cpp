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
TSharedPtr<IAnalyticsProviderET> FStudioAnalytics::Analytics;
TArray<FAnalyticsEventAttribute> FStudioAnalytics::DefaultAttributes;

void FStudioAnalytics::SetProvider(TSharedRef<IAnalyticsProviderET> InAnalytics)
{
	checkf(!Analytics.IsValid(), TEXT("FStudioAnalytics::SetProvider called more than once."));

	bInitialized = true;

	Analytics = InAnalytics;

	ApplyDefaultEventAttributes();

	TimeEstimation = FPlatformTime::Seconds();

	if (FPlatformProcess::SupportsMultithreading())
	{
		TimerThread = FThread(TEXT("Studio Analytics Timer Thread"), []() { RunTimer_Concurrent(); });
	}
}

void FStudioAnalytics::ApplyDefaultEventAttributes()
{
	if (Analytics.IsValid())
	{
		// Get the current attributes
		TArray<FAnalyticsEventAttribute> CurrentDefaultAttributes = Analytics->GetDefaultEventAttributesSafe();

		// Append any new attributes to our current ones
		CurrentDefaultAttributes += MoveTemp(DefaultAttributes);
		DefaultAttributes.Reset();

		// Set the new default attributes in the provider
		Analytics->SetDefaultEventAttributes(MoveTemp(CurrentDefaultAttributes));
	}	
}

void FStudioAnalytics::AddDefaultEventAttribute(const FAnalyticsEventAttribute& Attribute)
{
	// Append a single default attribute to the existing list
	DefaultAttributes.Emplace(Attribute);
}

void FStudioAnalytics::AddDefaultEventAttributes(TArray<FAnalyticsEventAttribute>&& Attributes)
{
	// Append attributes list to the existing list
	DefaultAttributes += MoveTemp(Attributes);
}

IAnalyticsProviderET& FStudioAnalytics::GetProvider()
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

void FStudioAnalytics::RecordEvent(const FString& EventName)
{
	RecordEvent(EventName, TArray<FAnalyticsEventAttribute>());
}

void FStudioAnalytics::RecordEvent(const FString& EventName, const TArray<FAnalyticsEventAttribute>& Attributes)
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
	if (!ensureMsgf(SecondsSpentLoading < 36000, TEXT("The loading event shouldn't be over 10 hours, perhaps an uninitialized bit of memory?")))
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