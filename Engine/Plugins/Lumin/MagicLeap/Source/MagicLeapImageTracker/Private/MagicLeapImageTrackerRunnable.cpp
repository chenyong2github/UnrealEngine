// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "MagicLeapImageTrackerRunnable.h"

DEFINE_LOG_CATEGORY(LogMagicLeapImageTracker);

FMagicLeapImageTrackerRunnable::FMagicLeapImageTrackerRunnable()
: FMagicLeapRunnable({ EMagicLeapPrivilege::CameraCapture }, TEXT("FMLImageTrackerRunnable"), EThreadPriority::TPri_AboveNormal)
#if WITH_MLSDK
, ImageTracker(ML_INVALID_HANDLE)
#endif // WITH_MLSDK
{
#if WITH_MLSDK
	FMemory::Memset(&Settings, 0, sizeof(Settings));
#endif // WITH_MLSDK
};

void FMagicLeapImageTrackerRunnable::Exit()
{
#if WITH_MLSDK
	{
		FScopeLock Lock(&TargetsMutex);
		for (auto& Target : TrackedImageTargets)
		{
			RemoveTarget(Target.Value);
		}
		TrackedImageTargets.Empty();
	}

	DestroyTracker();
#endif // WITH_MLSDK
}

void FMagicLeapImageTrackerRunnable::Pause()
{
#if WITH_MLSDK
	AppEventHandler.SetWasSystemEnabledOnPause(GetImageTrackerEnabled());

	if (!AppEventHandler.WasSystemEnabledOnPause())
	{
		UE_LOG(LogMagicLeapImageTracker, Log, TEXT("Image tracking was not enabled at time of application pause."));
	}
	else
	{
		if (!MLHandleIsValid(ImageTracker))
		{
			UE_LOG(LogMagicLeapImageTracker, Error, TEXT("Image tracker was invalid on application pause."));
		}
		else
		{
			SetImageTrackerEnabled(false);
		}
	}
#endif // WITH_MLSDK
}

void FMagicLeapImageTrackerRunnable::Resume()
{
#if WITH_MLSDK
	if (!AppEventHandler.WasSystemEnabledOnPause())
	{
		UE_LOG(LogMagicLeapImageTracker, Log, TEXT("Not resuming image tracker as it was not enabled at time of application pause."));
	}
	else
	{
		if (!MLHandleIsValid(ImageTracker))
		{
			UE_LOG(LogMagicLeapImageTracker, Error, TEXT("Image tracker was invalid on application resume."));
		}
		else
		{
			SetImageTrackerEnabled(true);
		}
	}
#endif // WITH_MLSDK
}

void FMagicLeapImageTrackerRunnable::SetTargetAsync(const FMagicLeapImageTrackerTarget& ImageTarget)
{
	FMagicLeapImageTrackerTask SetTargetMsg;
	SetTargetMsg.Type = FMagicLeapImageTrackerTask::EType::TryCreateTarget;
	SetTargetMsg.Target = ImageTarget;

	PushNewTask(SetTargetMsg);
}

bool FMagicLeapImageTrackerRunnable::RemoveTargetAsync(const FString& InName)
{
	FMagicLeapImageTrackerTarget* Target = TrackedImageTargets.Find(InName);
	if (Target)
	{
		FMagicLeapImageTrackerTask RemoveTargetMsg;
		RemoveTargetMsg.Type = FMagicLeapImageTrackerTask::EType::TryRemoveTarget;
		RemoveTargetMsg.Target = *Target;

		PushNewTask(RemoveTargetMsg);
		return true;
	}

	UE_LOG(LogMagicLeapImageTracker, Warning, TEXT("RemoveTargetAsync failed to resolve target '%s'!"), *InName);
	return false;
}

bool FMagicLeapImageTrackerRunnable::ProcessCurrentTask()
{
	bool bProcessedTask = false;

	switch (CurrentTask.Type)
	{
	case FMagicLeapImageTrackerTask::EType::None: break;

	case FMagicLeapImageTrackerTask::EType::TryCreateTarget:
	{
#if WITH_MLSDK
		SetTarget();
#endif // WITH_MLSDK
		bProcessedTask = true;
	}
	break;

	case FMagicLeapImageTrackerTask::EType::TryRemoveTarget:
	{
#if WITH_MLSDK
		FScopeLock Lock(&TargetsMutex);
		RemoveTarget(CurrentTask.Target, true);
#endif // WITH_MLSDK
		bProcessedTask = true;
	}
	break;
	}

	return bProcessedTask;
}

uint32 FMagicLeapImageTrackerRunnable::GetMaxSimultaneousTargets() const
{
#if WITH_MLSDK
	return static_cast<uint32>(FPlatformAtomics::AtomicRead(reinterpret_cast<const volatile int32*>(&Settings.max_simultaneous_targets)));
#else
	return 0;
#endif // WITH_MLSDK
}

void FMagicLeapImageTrackerRunnable::SetMaxSimultaneousTargets(uint32 NewNumTargets)
{
#if WITH_MLSDK
	// run on first available background thread rather than our worker thread in order
	// to avoid a potential task queue.
	AsyncTask(ENamedThreads::AnyBackgroundThreadNormalTask, [this, NewNumTargets]()
	{
		if (!MLHandleIsValid(GetHandle()))
		{
			CreateTracker();
		}
		FScopeLock Lock(&TrackerMutex);
		if (AppEventHandler.GetPrivilegeStatus(EMagicLeapPrivilege::CameraCapture) == MagicLeap::EPrivilegeState::Granted)
		{
			Settings.max_simultaneous_targets = NewNumTargets;
			MLResult Result = MLImageTrackerUpdateSettings(ImageTracker, &Settings);
			if (Result != MLResult_Ok)
			{
				UE_LOG(LogMagicLeapImageTracker, Error, TEXT("MLImageTrackerUpdateSettings failed with error '%s'!"), UTF8_TO_TCHAR(MLGetResultString(Result)));
			}
		}
		else
		{
			UE_LOG(LogMagicLeapImageTracker, Log, TEXT("Image tracking settings failed to update due to lack of privilege!"));
		}
	});
#else
	(void)NewNumTargets;
#endif // WITH_MLSDK
}

bool FMagicLeapImageTrackerRunnable::GetImageTrackerEnabled() const
{
#if WITH_MLSDK
	return static_cast<bool>(FPlatformAtomics::AtomicRead(reinterpret_cast<const volatile int32*>(&Settings.enable_image_tracking))) && MLHandleIsValid(GetHandle());
#else 
	return false;
#endif // WITH_MLSDK
}

void FMagicLeapImageTrackerRunnable::SetImageTrackerEnabled(bool bEnabled)
{
#if WITH_MLSDK
	// run on first available background thread rather than our worker thread in order
	// to avoid a potential task queue.
	AsyncTask(ENamedThreads::AnyBackgroundThreadNormalTask, [this, bEnabled]()
	{
		if (MLHandleIsValid(GetHandle()))
		{
			FScopeLock Lock(&TrackerMutex);
			if (AppEventHandler.GetPrivilegeStatus(EMagicLeapPrivilege::CameraCapture) == MagicLeap::EPrivilegeState::Granted)
			{
				Settings.enable_image_tracking = bEnabled;
				MLResult Result = MLImageTrackerUpdateSettings(ImageTracker, &Settings);
				if (Result != MLResult_Ok)
				{
					UE_LOG(LogMagicLeapImageTracker, Error, TEXT("MLImageTrackerUpdateSettings failed with error '%s'!"), UTF8_TO_TCHAR(MLGetResultString(Result)));
				}
			}
			else
			{
				UE_LOG(LogMagicLeapImageTracker, Log, TEXT("Image tracking settings failed to update due to lack of privilege!"));
			}
		}
		else if (bEnabled)
		{
			CreateTracker();
		}
	});
#else
	(void)bEnabled;
#endif // WITH_MLSDK
}

void FMagicLeapImageTrackerRunnable::UpdateTargetsMainThread()
{
#if WITH_MLSDK
	if (!(IMagicLeapPlugin::Get().IsMagicLeapHMDValid()))
	{
		return;
	}

	FScopeLock Lock(&TargetsMutex);
	for (auto& TargetEntry : TrackedImageTargets)
	{
		FMagicLeapImageTrackerTarget& Target = TargetEntry.Value;
		checkf(MLHandleIsValid(GetHandle()), TEXT("ImageTracker handle is invalid!"));
		MLImageTrackerTargetResult TrackingStatus;
		MLResult Result = MLImageTrackerGetTargetResult(GetHandle(), Target.Handle, &TrackingStatus);
		if (Result != MLResult_Ok)
		{
			UE_LOG(LogMagicLeapImageTracker, Warning, TEXT("MLImageTrackerGetTargetResult failed with error '%s'."), UTF8_TO_TCHAR(MLGetResultString(Result)));
			TrackingStatus.status = MLImageTrackerTargetStatus_NotTracked;
		}

		if (TrackingStatus.status == MLImageTrackerTargetStatus_NotTracked)
		{
			if (Target.OldTrackingStatus.status != MLImageTrackerTargetStatus_NotTracked)
			{
				Target.OnImageTargetLost.Broadcast();
			}
		}
		else
		{
			EMagicLeapTransformFailReason FailReason = EMagicLeapTransformFailReason::None;
			FTransform Pose = FTransform::Identity;
			if (IMagicLeapPlugin::Get().GetTransform(Target.Data.coord_frame_target, Pose, FailReason))
			{
				Pose.ConcatenateRotation(FQuat(FVector(0, 0, 1), PI));
				if (TrackingStatus.status == MLImageTrackerTargetStatus_Unreliable)
				{
					Target.UnreliableLocation = Pose.GetTranslation();
					Target.UnreliableRotation = Pose.Rotator();
					Target.OnImageTargetUnreliableTracking.Broadcast(Target.Location, Target.Rotation, Target.UnreliableLocation, Target.UnreliableRotation);
				}
				else
				{
					Target.Location = Pose.GetTranslation();
					Target.Rotation = Pose.Rotator();
					if (Target.OldTrackingStatus.status != MLImageTrackerTargetStatus_Tracked)
					{
						Target.OnImageTargetFound.Broadcast();
					}
				}
			}
			else
			{
				if (FailReason == EMagicLeapTransformFailReason::NaNsInTransform)
				{
					UE_LOG(LogMagicLeapImageTracker, Error, TEXT("NaNs in image tracker target transform."));
				}
				TrackingStatus.status = MLImageTrackerTargetStatus_NotTracked;
				if (Target.OldTrackingStatus.status != MLImageTrackerTargetStatus_NotTracked)
				{
					Target.OnImageTargetLost.Broadcast();
				}
			}
		}

		Target.OldTrackingStatus = TrackingStatus;
	}
#endif // WITH_MLSDK
}

bool FMagicLeapImageTrackerRunnable::TryGetRelativeTransformMainThread(const FString& TargetName, FVector& OutLocation, FRotator& OutRotation)
{
#if WITH_MLSDK
	if (!(IMagicLeapPlugin::Get().IsMagicLeapHMDValid()))
	{
		return false;
	}

	FScopeLock Lock(&TargetsMutex);
	FMagicLeapImageTrackerTarget* Target = TrackedImageTargets.Find(TargetName);
	if (Target)
	{
		if (Target->OldTrackingStatus.status == MLImageTrackerTargetStatus_Unreliable)
		{
			if (Target->bUseUnreliablePose)
			{
				OutLocation = Target->UnreliableLocation;
				OutRotation = Target->UnreliableRotation;
				return true;
			}

			return false;
		}
		else if (Target->OldTrackingStatus.status == MLImageTrackerTargetStatus_Tracked)
		{
			OutLocation = Target->Location;
			OutRotation = Target->Rotation;
			return true;
		}
	}
#endif // WITH_MLSDK
	return false;
}


#if WITH_MLSDK
bool FMagicLeapImageTrackerRunnable::CreateTracker()
{
	if (MagicLeap::EPrivilegeState::Granted != AppEventHandler.GetPrivilegeStatus(EMagicLeapPrivilege::CameraCapture))
	{
		return false;
	}

	MLImageTrackerSettings TempSettings;
	// don't allow atomic reads of Settings while we are creating the tracker
	MLResult Result = MLImageTrackerInitSettings(&TempSettings);
	if (Result != MLResult_Ok)
	{
		UE_LOG(LogMagicLeapImageTracker, Error, TEXT("MLImageTrackerInitSettings failed with error '%s'!"), UTF8_TO_TCHAR(MLGetResultString(Result)));
		return false;
	}

	MLHandle TempHandle = ML_INVALID_HANDLE;
	// don't allow atomic reads of the tracker handle while we are creating the tracker
	Result = MLImageTrackerCreate(&TempSettings, &TempHandle);

	if (Result != MLResult_Ok)
	{
		UE_LOG(LogMagicLeapImageTracker, Error, TEXT("MLImageTrackerCreate failed with error '%s'!"), UTF8_TO_TCHAR(MLGetResultString(Result)));
		return false;
	}

	FScopeLock Lock(&TrackerMutex);
	Settings = TempSettings;
	ImageTracker = TempHandle;
	return true;
}

void FMagicLeapImageTrackerRunnable::DestroyTracker()
{
	FScopeLock Lock(&TrackerMutex);
	if (MLHandleIsValid(ImageTracker))
	{
		MLHandle TempHandle = ImageTracker;
		// don't allow atomic reads of the tracker handle while we are destroying the tracker
		ImageTracker = ML_INVALID_HANDLE;
		MLResult Result = MLImageTrackerDestroy(TempHandle);

		if (Result != MLResult_Ok)
		{
			UE_LOG(LogMagicLeapImageTracker, Error, TEXT("MLImageTrackerDestroy failed with error '%s'!"), UTF8_TO_TCHAR(MLGetResultString(Result)));
		}

		ImageTracker = ML_INVALID_HANDLE;
	}
}

void FMagicLeapImageTrackerRunnable::SetTarget()
{
	if (!MLHandleIsValid(ImageTracker))
	{
		if (!CreateTracker())
		{
			return;
		}
	}

	FMagicLeapImageTrackerTarget& Target = CurrentTask.Target;
	{
		FScopeLock Lock(&TargetsMutex);
		FMagicLeapImageTrackerTarget* ExistingTarget = TrackedImageTargets.Find(Target.Name);
		if (ExistingTarget != nullptr && MLHandleIsValid(ExistingTarget->Handle))
		{
			RemoveTarget(*ExistingTarget);
		}
	}

	UE_LOG(LogMagicLeapImageTracker, Warning, TEXT("SetTarget for %s"), *Target.Name);
	FMagicLeapImageTrackerTask TargetCreateTask;
	Target.Settings.name = TCHAR_TO_UTF8(*Target.Name);

	if (Target.Texture->PlatformData->Mips.Num() == 0)
	{
		UE_LOG(LogMagicLeapImageTracker, Error, TEXT("Texture for target %s has no mip maps."), *Target.Name);
		TargetCreateTask.Type = FMagicLeapImageTrackerTask::EType::TargetCreateFailed;
		PushCompletedTask(TargetCreateTask);
		return;
	}

	FTexture2DMipMap& Mip = Target.Texture->PlatformData->Mips[0];
	const unsigned char* PixelData = static_cast<const unsigned char*>(Mip.BulkData.Lock(LOCK_READ_ONLY));

	uint32_t Width = static_cast<uint32_t>(Target.Texture->GetSurfaceWidth());
	uint32_t Height = static_cast<uint32_t>(Target.Texture->GetSurfaceHeight());

	if (Target.Texture->GetPixelFormat() == EPixelFormat::PF_B8G8R8A8)
	{
		const uint32_t NumComponents = Width * Height * 4;
		RBGAPixelData.Reset(NumComponents);
		RBGAPixelData.AddUninitialized(NumComponents);

		for (uint32_t i = 0; i < NumComponents; i += 4)
		{
			RBGAPixelData[i + 0] = PixelData[i + 2];
			RBGAPixelData[i + 1] = PixelData[i + 1];
			RBGAPixelData[i + 2] = PixelData[i + 0];
			RBGAPixelData[i + 3] = PixelData[i + 3];
		}

		PixelData = static_cast<const unsigned char*>(RBGAPixelData.GetData());
	}

	checkf(MLHandleIsValid(GetHandle()), TEXT("ImageTracker handle is invalid!"));
	MLResult Result = MLImageTrackerAddTargetFromArray(GetHandle(), &Target.Settings, PixelData, Width, Height, MLImageTrackerImageFormat_RGBA, &Target.Handle);
	Mip.BulkData.Unlock();

	if (Result != MLResult_Ok)
	{
		UE_LOG(LogMagicLeapImageTracker, Error, TEXT("MLImageTrackerAddTargetFromArray failed with error '%s'!"), UTF8_TO_TCHAR(MLGetResultString(Result)));
		TargetCreateTask.Type = FMagicLeapImageTrackerTask::EType::TargetCreateFailed;
		PushCompletedTask(TargetCreateTask);
		return;
	}

	// [3] Cache all the static data for this target.
	checkf(MLHandleIsValid(GetHandle()), TEXT("ImageTracker weak pointer is invalid!"));
	Result = MLImageTrackerGetTargetStaticData(GetHandle(), Target.Handle, &Target.Data);
	if (Result != MLResult_Ok)
	{
		UE_LOG(LogMagicLeapImageTracker, Error, TEXT("MLImageTrackerGetTargetStaticData failed with error '%s'!"), UTF8_TO_TCHAR(MLGetResultString(Result)));
		TargetCreateTask.Type = FMagicLeapImageTrackerTask::EType::TargetCreateFailed;
		PushCompletedTask(TargetCreateTask);
		return;
	}

	UE_LOG(LogMagicLeapImageTracker, Log, TEXT("SetTarget successfully set for %s"), *Target.Name);
	TargetCreateTask.Type = FMagicLeapImageTrackerTask::EType::TargetCreateSucceeded;
	TargetCreateTask.Target = Target;
	PushCompletedTask(TargetCreateTask);
	{
		FScopeLock Lock(&TargetsMutex);
		TrackedImageTargets.Add(Target.Name, Target);
	}
}

void FMagicLeapImageTrackerRunnable::RemoveTarget(const FMagicLeapImageTrackerTarget& Target, bool bCanDestroyTracker)
{
	if (MLHandleIsValid(Target.Handle))
	{
		checkf(MLHandleIsValid(GetHandle()), TEXT("ImageTracker weak pointer is invalid!"));
		MLResult Result = MLImageTrackerRemoveTarget(GetHandle(), Target.Handle);
		UE_CLOG(Result != MLResult_Ok, LogMagicLeapImageTracker, Error, TEXT("MLImageTrackerRemoveTarget '%s' failed with error '%s'!"), *Target.Name, UTF8_TO_TCHAR(MLGetResultString(Result)));
		TrackedImageTargets.Remove(Target.Name);

		if (bCanDestroyTracker && TrackedImageTargets.Num() == 0)
		{
			DestroyTracker();
		}
	}
}

MLHandle FMagicLeapImageTrackerRunnable::GetHandle() const
{
	return FPlatformAtomics::AtomicRead(reinterpret_cast<const volatile int64*>(&ImageTracker));
}
#endif // WITH_MLSDK
