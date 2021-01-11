// Copyright Epic Games, Inc. All Rights Reserved.

#include "FramePerformanceProvider.h"

#include "EngineGlobals.h"
#include "Features/IModularFeatures.h"
#include "IStageDataProvider.h"
#include "Misc/App.h"
#include "Misc/CoreDelegates.h"
#include "RenderCore.h"
#include "StageDataProviderModule.h"
#include "StageMessages.h"
#include "StageMonitoringSettings.h"
#include "StageMonitorUtils.h"
#include "Stats/StatsData.h"
#include "VPSettings.h"

FFramePerformanceProvider::FFramePerformanceProvider()
{
	//Verify if conditions are met to enable frame performance messages
	FCoreDelegates::OnEndFrame.AddRaw(this, &FFramePerformanceProvider::OnEndFrame);

#if WITH_EDITOR
	GetMutableDefault<UStageMonitoringSettings>()->OnSettingChanged().AddRaw(this, &FFramePerformanceProvider::OnStageSettingsChanged);
#endif //WITH_EDITOR
	EnableHitchDetection(GetDefault<UStageMonitoringSettings>()->ProviderSettings.HitchDetectionSettings.bEnableHitchDetection);
}

FFramePerformanceProvider::~FFramePerformanceProvider()
{
	//Cleanup what could have been registered
	FCoreDelegates::OnEndFrame.RemoveAll(this);
	EnableHitchDetection(false);
}

void FFramePerformanceProvider::OnEndFrame()
{
	UpdateFramePerformance();
}

void FFramePerformanceProvider::CheckHitches(int64 Frame)
{
#if STATS
	// when synced, this time will be the full time of the frame, whereas the above don't include any waits
	const FStatsThreadState& Stats = FStatsThreadState::GetLocalState();
	const float GameThreadTimeWithWaits = (float)FPlatformTime::ToMilliseconds64(Stats.GetFastThreadFrameTime(Frame, EThreadType::Game));
	const float RenderThreadTimeWithWaits = (float)FPlatformTime::ToMilliseconds64(Stats.GetFastThreadFrameTime(Frame, EThreadType::Renderer));
	const float FullFrameTime = FMath::Max(GameThreadTimeWithWaits, RenderThreadTimeWithWaits);

	// check for hitch (if application not backgrounded)
	const float TimeThreshold = CachedHitchSettings.MinimumFrameRate.AsInterval() * 1000.0f;
	if (FullFrameTime > TimeThreshold)
	{
		const float GameThreadTime = FPlatformTime::ToMilliseconds(GGameThreadTime);
		const float RenderThreadTime = FPlatformTime::ToMilliseconds(GRenderThreadTime);
		const float GPUTime = FPlatformTime::ToMilliseconds(GGPUFrameTime);
		float HitchedFPS = CachedHitchSettings.MinimumFrameRate.AsDecimal();
		if (!FMath::IsNearlyZero(FullFrameTime))
		{
			HitchedFPS = 1000.0f / FullFrameTime;
		}

		UE_LOG(LogStageDataProvider, VeryVerbose, TEXT("Hitch detected: FullFrameTime=%f, GameThreadTimeWithWaits=%f, RenderThreadTimeWithWaits=%f, Threshold=%f, GameThreadTime=%f, RenderThreadTime=%f"), FullFrameTime, GameThreadTimeWithWaits, RenderThreadTimeWithWaits, TimeThreshold, GameThreadTime, RenderThreadTime);

		IStageDataProvider::SendMessage<FHitchDetectionMessage>(EStageMessageFlags::None, GameThreadTimeWithWaits, RenderThreadTimeWithWaits, GameThreadTime, RenderThreadTime, GPUTime, TimeThreshold, HitchedFPS);
	}
#endif //STATS
}

void FFramePerformanceProvider::UpdateFramePerformance()
{
	const double CurrentTime = FApp::GetCurrentTime();
	if (CurrentTime - LastFramePerformanceSent >= GetDefault<UStageMonitoringSettings>()->ProviderSettings.FramePerformanceSettings.UpdateInterval)
	{
		LastFramePerformanceSent = CurrentTime;

		const float GameThreadTime = FPlatformTime::ToMilliseconds(GGameThreadTime);
		const float RenderThreadTime = FPlatformTime::ToMilliseconds(GRenderThreadTime);
		const float GPUTime = FPlatformTime::ToMilliseconds(GGPUFrameTime);
		const float IdleTimeMilli = (FApp::GetIdleTime() * 1000.0);
		IStageDataProvider::SendMessage<FFramePerformanceProviderMessage>(EStageMessageFlags::None, GameThreadTime, RenderThreadTime, GPUTime, IdleTimeMilli);
	}
}

#if WITH_EDITOR
void FFramePerformanceProvider::OnStageSettingsChanged(UObject* Object, struct FPropertyChangedEvent& PropertyChangedEvent)
{
	const FName PropertyName = (PropertyChangedEvent.Property != nullptr) ? PropertyChangedEvent.Property->GetFName() : NAME_None;

	if (PropertyName == GET_MEMBER_NAME_CHECKED(FStageHitchDetectionSettings, bEnableHitchDetection))
	{
		EnableHitchDetection(GetDefault<UStageMonitoringSettings>()->ProviderSettings.HitchDetectionSettings.bEnableHitchDetection);
	}
}
#endif //WITH_EDITOR

void FFramePerformanceProvider::EnableHitchDetection(bool bShouldEnable)
{
#if STATS
	if (bShouldEnable != bIsHitchDetectionEnabled)
	{
		if (bShouldEnable)
		{
			CachedHitchSettings = GetDefault<UStageMonitoringSettings>()->ProviderSettings.HitchDetectionSettings;

			// Subscribe to Stats provider to verify hitches
			StatsMasterEnableAdd();
			FStatsThreadState& Stats = FStatsThreadState::GetLocalState();
			Stats.NewFrameDelegate.AddRaw(this, &FFramePerformanceProvider::CheckHitches);
		}
		else
		{
			StatsMasterEnableSubtract();
			FStatsThreadState& Stats = FStatsThreadState::GetLocalState();
			Stats.NewFrameDelegate.RemoveAll(this);
		}
	}

	bIsHitchDetectionEnabled = bShouldEnable;
#endif //STATS
}
