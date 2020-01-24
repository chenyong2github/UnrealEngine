// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "HAL/Thread.h"

class IAnalyticsProvider;
struct FAnalyticsEventAttribute;

/**
 * The public interface for the game studio to gather information about internal development metrics.
 */
class FStudioAnalytics : FNoncopyable
{
public:
	static ENGINE_API void SetProvider(TSharedRef<IAnalyticsProvider> InAnalytics);

	/**
	 * Return the provider instance. Not valid outside of Initialize/Shutdown calls.
	 * Note: must check IsAvailable() first else this code will assert if the provider is not valid.
	 */
	static ENGINE_API IAnalyticsProvider& GetProvider();

	/** Helper function to determine if the provider is valid. */
	static ENGINE_API bool IsAvailable() { return Analytics.IsValid(); }

	static ENGINE_API double GetAnalyticSeconds();

	static ENGINE_API void Tick(float DeltaSeconds);

	static ENGINE_API void Shutdown();

	/** General report event function. */
	static ENGINE_API void ReportEvent(const FString& EventName);
	static ENGINE_API void ReportEvent(const FString& EventName, const TArray<FAnalyticsEventAttribute>& Attributes);

	/** An event for reporting load time that blocks the editor. */
	static ENGINE_API void FireEvent_Loading(const FString& LoadingName, double SecondsSpentLoading, const TArray<FAnalyticsEventAttribute>& Attributes = TArray<FAnalyticsEventAttribute>());

private:
	static void RunTimer_Concurrent();

private:
	static bool bInitialized;
	static TSharedPtr<IAnalyticsProvider> Analytics;
	static FThread TimerThread;
	static volatile double TimeEstimation;
};

