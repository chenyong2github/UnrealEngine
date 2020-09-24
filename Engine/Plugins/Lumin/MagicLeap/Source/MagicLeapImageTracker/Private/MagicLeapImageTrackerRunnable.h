// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Async/Async.h"
#include "Engine/Texture2D.h"
#include "IMagicLeapPlugin.h"
#include "MagicLeapImageTrackerComponent.h"
#include "MagicLeapRunnable.h"

DECLARE_LOG_CATEGORY_EXTERN(LogMagicLeapImageTracker, Verbose, All);

struct FMagicLeapImageTrackerTask : public FMagicLeapTask
{
	enum class EType : uint32
	{
		None,
		TryCreateTarget,
		TryRemoveTarget,
		TargetCreateSucceeded,
		TargetCreateFailed
	};

	EType Type;
	FMagicLeapImageTargetSettings Target;
	FString TargetName;

	FMagicLeapSetImageTargetCompletedStaticDelegate SuccessStaticDelegate;
	FMagicLeapSetImageTargetCompletedStaticDelegate FailureStaticDelegate;

	FMagicLeapSetImageTargetSucceededMulti SuccessDynamicDelegate;
	FMagicLeapSetImageTargetFailedMulti FailureDynamicDelegate;

	FMagicLeapImageTrackerTask()
	: Type(EType::None)
	{}
};

class FMagicLeapImageTrackerRunnable : public FMagicLeapRunnable<FMagicLeapImageTrackerTask>
{
public:
	FMagicLeapImageTrackerRunnable();

	void Stop() override;
	void Pause() override;
	void Resume() override;
	bool ProcessCurrentTask() override;

	void SetTargetAsync(const FMagicLeapImageTargetSettings& ImageTarget, const FMagicLeapSetImageTargetCompletedStaticDelegate& SucceededDelegate, const FMagicLeapSetImageTargetCompletedStaticDelegate& FailedDelegate);
	void SetTargetAsync(const FMagicLeapImageTargetSettings& ImageTarget, const FMagicLeapSetImageTargetSucceededMulti& SucceededDelegate, const FMagicLeapSetImageTargetFailedMulti& FailedDelegate);
	bool RemoveTargetAsync(const FString& TargetName);

	uint32 GetMaxSimultaneousTargets() const;
	void SetMaxSimultaneousTargets(uint32 NewNumTargets);
	bool GetImageTrackerEnabled() const;
	void SetImageTrackerEnabled(bool bEnabled);

	void GetTargetState(const FString& TargetName, bool bProvideTransformInTrackingSpace, FMagicLeapImageTargetState& TargetState) const;
	FGuid GetTargetHandle(const FString& TargetName) const;

private:

	mutable FCriticalSection TargetsMutex;
	mutable FCriticalSection TrackerMutex;
	TArray<uint8> RBGAPixelData;
#if WITH_MLSDK
	struct FImageTargetData
	{
		MLHandle TargetHandle;
		MLImageTrackerTargetStaticData TargetData;
	};

	TMap<FString, FImageTargetData> TrackedImageTargets;

	MLImageTrackerSettings TrackerSettings;
	MLHandle ImageTracker;

	bool CreateTracker();
	void DestroyTracker();
	void SetTarget();
	void RemoveTarget(const FString& TargetName, bool bCanDestroyTracker = false);
	MLHandle GetHandle() const;
#endif // WITH_MLSDK
};
