// Copyright Epic Games, Inc. All Rights Reserved.

#include "MagicLeapImageTrackerModule.h"
#include "MagicLeapImageTrackerRunnable.h"

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
	if (!Runnable->IsRunning()) return true;

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
			ImageTrackerTarget.SetImageTargetSucceededDelegate.ExecuteIfBound(ImageTrackerTarget);
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
	return Runnable->IsRunning() ? Runnable->RemoveTargetAsync(TargetName) : true;
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

bool FMagicLeapImageTrackerModule::IsTracked(const FString& TargetName) const
{
	return Runnable->IsTracked(TargetName);
}

bool FMagicLeapImageTrackerModule::TryGetRelativeTransform(const FString& TargetName, FVector& OutLocation, FRotator& OutRotation)
{
	return Runnable->TryGetRelativeTransformMainThread(TargetName, OutLocation, OutRotation);
}

IMPLEMENT_MODULE(FMagicLeapImageTrackerModule, MagicLeapImageTracker);
