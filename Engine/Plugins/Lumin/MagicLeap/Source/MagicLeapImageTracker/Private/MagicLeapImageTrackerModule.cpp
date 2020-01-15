// Copyright Epic Games, Inc. All Rights Reserved.

#include "MagicLeapImageTrackerModule.h"
#include "MagicLeapImageTrackerRunnable.h"

FMagicLeapImageTrackerModule::FMagicLeapImageTrackerModule()
: Runnable(nullptr)
{
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

bool FMagicLeapImageTrackerModule::Tick(float DeltaTime)
{
	FMagicLeapImageTrackerTask CompletedTask;
	if (Runnable->TryGetCompletedTask(CompletedTask))
	{
		FMagicLeapImageTrackerTarget& ImageTrackerTarget = CompletedTask.Target;
		switch (CompletedTask.Type)
		{
		case FMagicLeapImageTrackerTask::EType::TargetCreateFailed:
		{
			ImageTrackerTarget.OnSetImageTargetFailed.Broadcast();
		}
		break;

		case FMagicLeapImageTrackerTask::EType::TargetCreateSucceeded:
		{
			ImageTrackerTarget.OnSetImageTargetSucceeded.Broadcast();
		}
		break;
		}
	}

	Runnable->UpdateTargetsMainThread();

	return true;
}

void FMagicLeapImageTrackerModule::SetTargetAsync(const FMagicLeapImageTrackerTarget& ImageTarget)
{
	Runnable->SetTargetAsync(ImageTarget);
	if (!TickDelegateHandle.IsValid())
	{
		TickDelegateHandle = FTicker::GetCoreTicker().AddTicker(TickDelegate);
	}
}

bool FMagicLeapImageTrackerModule::RemoveTargetAsync(const FString& TargetName)
{
	return Runnable->RemoveTargetAsync(TargetName);
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

bool FMagicLeapImageTrackerModule::TryGetRelativeTransform(const FString& TargetName, FVector& OutLocation, FRotator& OutRotation)
{
	return Runnable->TryGetRelativeTransformMainThread(TargetName, OutLocation, OutRotation);
}


IMPLEMENT_MODULE(FMagicLeapImageTrackerModule, MagicLeapImageTracker);
