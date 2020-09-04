// Copyright Epic Games, Inc. All Rights Reserved.

#include "MagicLeapImageTrackerRunnable.h"
#include "IMagicLeapPlugin.h"
#include "HeadMountedDisplayFunctionLibrary.h"
#include "MagicLeapHandle.h"

DEFINE_LOG_CATEGORY(LogMagicLeapImageTracker);

FMagicLeapImageTrackerRunnable::FMagicLeapImageTrackerRunnable()
: FMagicLeapRunnable({ EMagicLeapPrivilege::CameraCapture }, TEXT("FMLImageTrackerRunnable"), EThreadPriority::TPri_AboveNormal)
#if WITH_MLSDK
, ImageTracker(ML_INVALID_HANDLE)
#endif // WITH_MLSDK
{
#if WITH_MLSDK
	FMemory::Memset(&TrackerSettings, 0, sizeof(TrackerSettings));
#endif // WITH_MLSDK
};

void FMagicLeapImageTrackerRunnable::Stop()
{
#if WITH_MLSDK
	FMagicLeapRunnable::Stop();

	{
		FScopeLock Lock(&TargetsMutex);
		for (const auto& TargetKeyVal : TrackedImageTargets)
		{
			const FImageTargetData& TargetData = TargetKeyVal.Value;
			if (MLHandleIsValid(TargetData.TargetHandle))
			{
				checkf(MLHandleIsValid(GetHandle()), TEXT("ImageTracker weak pointer is invalid!"));
				MLResult Result = MLImageTrackerRemoveTarget(GetHandle(), TargetData.TargetHandle);
				UE_CLOG(Result != MLResult_Ok, LogMagicLeapImageTracker, Error, TEXT("MLImageTrackerRemoveTarget '%s' failed with error '%s'!"), *TargetKeyVal.Key, UTF8_TO_TCHAR(MLGetResultString(Result)));
			}
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

void FMagicLeapImageTrackerRunnable::SetTargetAsync(const FMagicLeapImageTargetSettings& ImageTarget, const FMagicLeapSetImageTargetCompletedStaticDelegate& SucceededDelegate, const FMagicLeapSetImageTargetCompletedStaticDelegate& FailedDelegate)
{
	FMagicLeapImageTrackerTask SetTargetMsg;
	SetTargetMsg.Type = FMagicLeapImageTrackerTask::EType::TryCreateTarget;
	SetTargetMsg.Target = ImageTarget;
	SetTargetMsg.SuccessStaticDelegate = SucceededDelegate;
	SetTargetMsg.FailureStaticDelegate = FailedDelegate;

	PushNewTask(SetTargetMsg);
}

void FMagicLeapImageTrackerRunnable::SetTargetAsync(const FMagicLeapImageTargetSettings& ImageTarget, const FMagicLeapSetImageTargetSucceededMulti& SucceededDelegate, const FMagicLeapSetImageTargetFailedMulti& FailedDelegate)
{
	FMagicLeapImageTrackerTask SetTargetMsg;
	SetTargetMsg.Type = FMagicLeapImageTrackerTask::EType::TryCreateTarget;
	SetTargetMsg.Target = ImageTarget;
	SetTargetMsg.SuccessDynamicDelegate = SucceededDelegate;
	SetTargetMsg.FailureDynamicDelegate = FailedDelegate;

	PushNewTask(SetTargetMsg);
}

bool FMagicLeapImageTrackerRunnable::RemoveTargetAsync(const FString& TargetName)
{
#if WITH_MLSDK
	FScopeLock Lock(&TargetsMutex);
	if (TrackedImageTargets.Contains(TargetName))
	{
		FMagicLeapImageTrackerTask RemoveTargetMsg;
		RemoveTargetMsg.Type = FMagicLeapImageTrackerTask::EType::TryRemoveTarget;
		RemoveTargetMsg.TargetName = TargetName;

		PushNewTask(RemoveTargetMsg);
		return true;
	}

	UE_LOG(LogMagicLeapImageTracker, Error, TEXT("RemoveTargetAsync failed to resolve target '%s'!"), *TargetName);
#endif // WITH_MLSDK
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
		RemoveTarget(CurrentTask.TargetName, true);
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
	return static_cast<uint32>(FPlatformAtomics::AtomicRead(reinterpret_cast<const volatile int32*>(&TrackerSettings.max_simultaneous_targets)));
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

		{
			FScopeLock Lock(&TrackerMutex);
			if (AppEventHandler.GetPrivilegeStatus(EMagicLeapPrivilege::CameraCapture) == MagicLeap::EPrivilegeState::Granted)
			{
				TrackerSettings.max_simultaneous_targets = NewNumTargets;
				MLResult Result = MLImageTrackerUpdateSettings(ImageTracker, &TrackerSettings);
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
	});
#else
	(void)NewNumTargets;
#endif // WITH_MLSDK
}

bool FMagicLeapImageTrackerRunnable::GetImageTrackerEnabled() const
{
#if WITH_MLSDK
	return static_cast<bool>(FPlatformAtomics::AtomicRead(reinterpret_cast<const volatile int32*>(&TrackerSettings.enable_image_tracking))) && MLHandleIsValid(GetHandle());
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
				TrackerSettings.enable_image_tracking = bEnabled;
				MLResult Result = MLImageTrackerUpdateSettings(ImageTracker, &TrackerSettings);
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

void FMagicLeapImageTrackerRunnable::GetTargetState(const FString& TargetName, bool bProvideTransformInTrackingSpace, FMagicLeapImageTargetState& TargetState) const
{
#if WITH_MLSDK
	FScopeLock Lock(&TargetsMutex);

	const FImageTargetData* TargetData = TrackedImageTargets.Find(TargetName);
	if (TargetData != nullptr)
	{
		MLImageTrackerTargetResult TrackingStatus;
		MLResult Result = MLImageTrackerGetTargetResult(GetHandle(), TargetData->TargetHandle, &TrackingStatus);
		if (Result == MLResult_Ok)
		{
			switch (TrackingStatus.status)
			{
				case MLImageTrackerTargetStatus_Tracked:
				{
					TargetState.TrackingStatus = EMagicLeapImageTargetStatus::Tracked;
					break;
				}
				case MLImageTrackerTargetStatus_Unreliable:
				{
					TargetState.TrackingStatus = EMagicLeapImageTargetStatus::Unreliable;
					break;
				}
				case MLImageTrackerTargetStatus_NotTracked:
				{
					TargetState.TrackingStatus = EMagicLeapImageTargetStatus::NotTracked;
					break;
				}
			}

			if (TargetState.TrackingStatus != EMagicLeapImageTargetStatus::NotTracked)
			{
				EMagicLeapTransformFailReason FailReason = EMagicLeapTransformFailReason::None;
				FTransform Pose = FTransform::Identity;
				if (IMagicLeapPlugin::Get().GetTransform(TargetData->TargetData.coord_frame_target, Pose, FailReason))
				{
					Pose.ConcatenateRotation(FQuat(FVector(0, 0, 1), PI));
					TargetState.Location = Pose.GetTranslation();
					TargetState.Rotation = Pose.Rotator();

					if (!bProvideTransformInTrackingSpace)
					{
						const FTransform TrackingToWorld = UHeadMountedDisplayFunctionLibrary::GetTrackingToWorldTransform(nullptr);
						TargetState.Location = TrackingToWorld.TransformPosition(TargetState.Location);
						TargetState.Rotation = TrackingToWorld.TransformRotation(TargetState.Rotation.Quaternion()).Rotator();
					}
				}
				else
				{
					UE_CLOG(FailReason == EMagicLeapTransformFailReason::NaNsInTransform, LogMagicLeapImageTracker, Error, TEXT("NaNs in image tracker target transform."));
					TargetState.TrackingStatus = EMagicLeapImageTargetStatus::NotTracked;
				}
			}
		}
		else
		{
			UE_LOG(LogMagicLeapImageTracker, Error, TEXT("MLImageTrackerGetTargetResult failed with error '%s'."), UTF8_TO_TCHAR(MLGetResultString(Result)));
			TargetState.TrackingStatus = EMagicLeapImageTargetStatus::NotTracked;
		}
	}
	else
	{
		TargetState.TrackingStatus = EMagicLeapImageTargetStatus::NotTracked;
	}
#else
	TargetState.TrackingStatus = EMagicLeapImageTargetStatus::NotTracked;
#endif // WITH_MLSDK
}

FGuid FMagicLeapImageTrackerRunnable::GetTargetHandle(const FString& TargetName) const
{
	FScopeLock Lock(&TargetsMutex);

#if WITH_MLSDK
	const FImageTargetData* TargetData = TrackedImageTargets.Find(TargetName);
	if (TargetData != nullptr)
	{
		return MagicLeap::MLHandleToFGuid(TargetData->TargetHandle);
	}
#endif // WITH_MLSDK

	return MagicLeap::INVALID_FGUID;
}

#if WITH_MLSDK
bool FMagicLeapImageTrackerRunnable::CreateTracker()
{
	if (MagicLeap::EPrivilegeState::Granted != AppEventHandler.GetPrivilegeStatus(EMagicLeapPrivilege::CameraCapture))
	{
		return false;
	}

	MLImageTrackerSettings TempSettings;
	// don't allow atomic reads of TrackerSettings while we are creating the tracker
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

	{
		FScopeLock Lock(&TrackerMutex);
		TrackerSettings = TempSettings;
		ImageTracker = TempHandle;
	}

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

	const FMagicLeapImageTargetSettings& Target = CurrentTask.Target;

	// Remove target if it already exists with this name
	RemoveTarget(Target.Name);

	UE_LOG(LogMagicLeapImageTracker, Display, TEXT("SetTarget for %s"), *Target.Name);

	FMagicLeapImageTrackerTask TargetCreateTask;
	TargetCreateTask.TargetName = Target.Name;
	// copy delegates so that they can be fired by the game thread again
	TargetCreateTask.SuccessStaticDelegate = CurrentTask.SuccessStaticDelegate;
	TargetCreateTask.FailureStaticDelegate = CurrentTask.FailureStaticDelegate;
	TargetCreateTask.SuccessDynamicDelegate = CurrentTask.SuccessDynamicDelegate;
	TargetCreateTask.FailureDynamicDelegate = CurrentTask.FailureDynamicDelegate;

	MLImageTrackerTargetSettings Settings;
	Settings.name = TCHAR_TO_UTF8(*Target.Name);
	Settings.longer_dimension = Target.LongerDimension / IMagicLeapPlugin::Get().GetWorldToMetersScale();
	Settings.is_stationary = Target.bIsStationary;
	Settings.is_enabled = Target.bIsEnabled;

	if (Target.ImageTexture->PlatformData->Mips.Num() == 0)
	{
		UE_LOG(LogMagicLeapImageTracker, Error, TEXT("Texture for target %s has no mip maps."), *Target.Name);
		TargetCreateTask.Type = FMagicLeapImageTrackerTask::EType::TargetCreateFailed;
		PushCompletedTask(TargetCreateTask);
		return;
	}

	FTexture2DMipMap& Mip = Target.ImageTexture->PlatformData->Mips[0];
	const unsigned char* PixelData = static_cast<const unsigned char*>(Mip.BulkData.Lock(LOCK_READ_ONLY));

	uint32_t Width = static_cast<uint32_t>(Target.ImageTexture->GetSurfaceWidth());
	uint32_t Height = static_cast<uint32_t>(Target.ImageTexture->GetSurfaceHeight());

	if (Target.ImageTexture->GetPixelFormat() == EPixelFormat::PF_B8G8R8A8)
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

	FImageTargetData TargetData;
	MLResult Result = MLImageTrackerAddTargetFromArray(GetHandle(), &Settings, PixelData, Width, Height, MLImageTrackerImageFormat_RGBA, &TargetData.TargetHandle);
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
	Result = MLImageTrackerGetTargetStaticData(GetHandle(), TargetData.TargetHandle, &TargetData.TargetData);
	if (Result != MLResult_Ok)
	{
		UE_LOG(LogMagicLeapImageTracker, Error, TEXT("MLImageTrackerGetTargetStaticData failed with error '%s'!"), UTF8_TO_TCHAR(MLGetResultString(Result)));
		TargetCreateTask.Type = FMagicLeapImageTrackerTask::EType::TargetCreateFailed;
		PushCompletedTask(TargetCreateTask);
		return;
	}

	{
		FScopeLock Lock(&TargetsMutex);
		TrackedImageTargets.Add(Target.Name, TargetData);
	}

	UE_LOG(LogMagicLeapImageTracker, Log, TEXT("SetTarget successfully set for %s"), *Target.Name);
	TargetCreateTask.Type = FMagicLeapImageTrackerTask::EType::TargetCreateSucceeded;
	PushCompletedTask(TargetCreateTask);
}

void FMagicLeapImageTrackerRunnable::RemoveTarget(const FString& TargetName, bool bCanDestroyTracker)
{
	MLHandle TargetHandle;
	bool bNoTargetsLeft = false;

	{
		// lock for min possible time because this lock is also used on the game thread tick to get target state
		FScopeLock Lock(&TargetsMutex);
		FImageTargetData* TargetData = TrackedImageTargets.Find(TargetName);
		if (TargetData == nullptr)
		{
			return;
		}

		TargetHandle = TargetData->TargetHandle;
		TrackedImageTargets.Remove(TargetName);
		bNoTargetsLeft = (TrackedImageTargets.Num() == 0);
	}

	checkf(MLHandleIsValid(GetHandle()), TEXT("ImageTracker weak pointer is invalid!"));
	MLResult Result = MLImageTrackerRemoveTarget(GetHandle(), TargetHandle);
	UE_CLOG(Result != MLResult_Ok, LogMagicLeapImageTracker, Error, TEXT("MLImageTrackerRemoveTarget '%s' failed with error '%s'!"), *TargetName, UTF8_TO_TCHAR(MLGetResultString(Result)));

	if (bCanDestroyTracker && bNoTargetsLeft)
	{
		DestroyTracker();
	}
}

MLHandle FMagicLeapImageTrackerRunnable::GetHandle() const
{
	return FPlatformAtomics::AtomicRead(reinterpret_cast<const volatile int64*>(&ImageTracker));
}
#endif // WITH_MLSDK
