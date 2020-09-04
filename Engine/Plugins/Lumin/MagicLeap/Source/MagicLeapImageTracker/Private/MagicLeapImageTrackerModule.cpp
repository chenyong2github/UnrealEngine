// Copyright Epic Games, Inc. All Rights Reserved.

#include "MagicLeapImageTrackerModule.h"
#include "MagicLeapImageTrackerRunnable.h"
#include "Stats/Stats.h"
#include "MagicLeapHandle.h"

FMagicLeapImageTrackerModule::FMagicLeapImageTrackerModule()
: Runnable(nullptr)
{
	IMagicLeapPlugin::Get().RegisterMagicLeapTrackerEntity(this);
}

FMagicLeapImageTrackerModule::~FMagicLeapImageTrackerModule()
{
	IMagicLeapPlugin::Get().UnregisterMagicLeapTrackerEntity(this);
}

void FMagicLeapImageTrackerModule::StartupModule()
{
	IMagicLeapImageTrackerModule::StartupModule();
	TickDelegate = FTickerDelegate::CreateRaw(this, &FMagicLeapImageTrackerModule::Tick);
	Runnable = new FMagicLeapImageTrackerRunnable;
}

void FMagicLeapImageTrackerModule::ShutdownModule()
{
	if (TickDelegateHandle.IsValid())
	{
		FTicker::GetCoreTicker().RemoveTicker(TickDelegateHandle);
	}
	FMagicLeapImageTrackerRunnable* InRunnable = Runnable;
	Runnable = nullptr;
	AsyncTask(ENamedThreads::AnyBackgroundThreadNormalTask, [InRunnable]()
	{
		delete InRunnable;
	});
	IModuleInterface::ShutdownModule();
}

void FMagicLeapImageTrackerModule::DestroyTracker()
{
	DestroyEntityTracker();
}

void FMagicLeapImageTrackerModule::DestroyEntityTracker()
{
	Runnable->Stop();
}

bool FMagicLeapImageTrackerModule::Tick(float DeltaTime)
{
	QUICK_SCOPE_CYCLE_COUNTER(STAT_FMagicLeapImageTrackerModule_Tick);
	if (!Runnable->IsRunning())
	{
		return true;
	}


	FMagicLeapImageTrackerTask CompletedTask;
	if (Runnable->TryGetCompletedTask(CompletedTask))
	{
		switch (CompletedTask.Type)
		{
		case FMagicLeapImageTrackerTask::EType::TargetCreateFailed:
		{
			CompletedTask.FailureDynamicDelegate.Broadcast();
			CompletedTask.FailureStaticDelegate.ExecuteIfBound(CompletedTask.TargetName);
		}
		break;

		case FMagicLeapImageTrackerTask::EType::TargetCreateSucceeded:
		{
			CompletedTask.SuccessDynamicDelegate.Broadcast();
			CompletedTask.SuccessStaticDelegate.ExecuteIfBound(CompletedTask.TargetName);
		}
		break;
		}
	}

	return true;
}

void FMagicLeapImageTrackerModule::SetTargetAsync(const FMagicLeapImageTargetSettings& ImageTarget, const FMagicLeapSetImageTargetCompletedStaticDelegate& SucceededDelegate, const FMagicLeapSetImageTargetCompletedStaticDelegate& FailedDelegate)
{
	Runnable->SetTargetAsync(ImageTarget, SucceededDelegate, FailedDelegate);
	if (!TickDelegateHandle.IsValid())
	{
		TickDelegateHandle = FTicker::GetCoreTicker().AddTicker(TickDelegate);
	}
}

void FMagicLeapImageTrackerModule::SetTargetAsync(const FMagicLeapImageTargetSettings& ImageTarget, const FMagicLeapSetImageTargetSucceededMulti& SucceededDelegate, const FMagicLeapSetImageTargetFailedMulti& FailedDelegate)
{
	Runnable->SetTargetAsync(ImageTarget, SucceededDelegate, FailedDelegate);
	if (!TickDelegateHandle.IsValid())
	{
		TickDelegateHandle = FTicker::GetCoreTicker().AddTicker(TickDelegate);
	}
}

bool FMagicLeapImageTrackerModule::RemoveTargetAsync(const FString& TargetName)
{
	return Runnable->IsRunning() ? Runnable->RemoveTargetAsync(TargetName) : true;
}

void FMagicLeapImageTrackerModule::GetTargetState(const FString& TargetName, bool bProvideTransformInTrackingSpace, FMagicLeapImageTargetState& TargetState) const
{
	if (Runnable->IsRunning())
	{
		Runnable->GetTargetState(TargetName, bProvideTransformInTrackingSpace, TargetState);
	}
	else
	{
		TargetState.TrackingStatus = EMagicLeapImageTargetStatus::NotTracked;
	}
}

FGuid FMagicLeapImageTrackerModule::GetTargetHandle(const FString& TargetName) const
{
	return (Runnable->IsRunning()) ? Runnable->GetTargetHandle(TargetName) : MagicLeap::INVALID_FGUID;
}

uint32 FMagicLeapImageTrackerModule::GetMaxSimultaneousTargets() const
{
	return Runnable->GetMaxSimultaneousTargets();
}

void FMagicLeapImageTrackerModule::SetMaxSimultaneousTargets(uint32 NewNumTargets)
{
	Runnable->SetMaxSimultaneousTargets(NewNumTargets);
}

bool FMagicLeapImageTrackerModule::GetImageTrackerEnabled() const
{
	return Runnable->GetImageTrackerEnabled();
}

void FMagicLeapImageTrackerModule::SetImageTrackerEnabled(bool bEnabled)
{
	Runnable->SetImageTrackerEnabled(bEnabled);
}

IMPLEMENT_MODULE(FMagicLeapImageTrackerModule, MagicLeapImageTracker);
