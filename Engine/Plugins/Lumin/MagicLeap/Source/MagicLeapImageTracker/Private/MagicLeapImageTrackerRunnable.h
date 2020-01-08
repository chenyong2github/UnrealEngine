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
	FMagicLeapImageTrackerTarget Target;

	FMagicLeapImageTrackerTask()
	: Type(EType::None)
	{}
};

class FMagicLeapImageTrackerRunnable : public FMagicLeapRunnable<FMagicLeapImageTrackerTask>
{
public:
	FMagicLeapImageTrackerRunnable();

	void Exit() override;
	void Pause() override;
	void Resume() override;
	bool ProcessCurrentTask() override;

	void SetTargetAsync(const FMagicLeapImageTrackerTarget& ImageTarget);
	bool RemoveTargetAsync(const FString& InName);
	uint32 GetMaxSimultaneousTargets() const;
	void SetMaxSimultaneousTargets(uint32 NewNumTargets);
	bool GetImageTrackerEnabled() const;
	void SetImageTrackerEnabled(bool bEnabled);
	void UpdateTargetsMainThread();
	bool TryGetRelativeTransformMainThread(const FString& TargetName, FVector& OutLocation, FRotator& OutRotation);

private:
	TMap<FString, FMagicLeapImageTrackerTarget> TrackedImageTargets;
	FCriticalSection TargetsMutex;
	mutable FCriticalSection TrackerMutex;
	TArray<uint8> RBGAPixelData;
#if WITH_MLSDK
	MLImageTrackerSettings Settings;
	MLHandle ImageTracker;

	bool CreateTracker();
	void DestroyTracker();
	void SetTarget();
	void RemoveTarget(const FMagicLeapImageTrackerTarget& Target, bool bCanDestroyTracker = false);
	MLHandle GetHandle() const;
#endif // WITH_MLSDK
};
