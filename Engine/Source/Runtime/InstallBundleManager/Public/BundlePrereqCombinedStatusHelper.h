// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "InstallBundleManagerInterface.h"
#include "InstallBundleUtils.h"

//Handles calculating the bundle status by combining progress from all of its
//Prerequisites. Allows you to display one progress percent that is weighted based on all
//bundles' values.
class INSTALLBUNDLEMANAGER_API FInstallBundleCombinedProgressTracker
{
public:

	//Collapses all the bundle manager states into one of a few states so that you can show simple text based on this enum
	enum class ECombinedBundleStatus : int32
	{
		Unknown, 
		Initializing, 
		Updating, 
		Finishing, 
		Finished,
		Count
	};
	friend const TCHAR* LexToString(ECombinedBundleStatus Status)
	{
		static const TCHAR* Strings[] =
		{
			TEXT("Unknown"),
			TEXT("Initializing"),
			TEXT("Updating"),
			TEXT("Finishing"),
			TEXT("Finished"),
			TEXT("Count")
		};

		static_assert(InstallBundleUtil::CastToUnderlying(ECombinedBundleStatus::Count) == UE_ARRAY_COUNT(Strings) - 1, "");
		return Strings[InstallBundleUtil::CastToUnderlying(Status)];
	}

	//provide all our needed combined status information in 1 struct
	struct FCombinedProgress
	{
		float ProgressPercent = 0.0f;
		ECombinedBundleStatus CombinedStatus = ECombinedBundleStatus::Unknown;
		EInstallBundlePauseFlags CombinedPauseFlags = EInstallBundlePauseFlags::None;
		bool bIsPaused = false;
		bool bDoesCurrentStateSupportPausing = false;
		bool bBundleRequiresUpdate = false;
	};
	
public:
	FInstallBundleCombinedProgressTracker();
	~FInstallBundleCombinedProgressTracker();
	
	FInstallBundleCombinedProgressTracker(const FInstallBundleCombinedProgressTracker& Other);
	FInstallBundleCombinedProgressTracker(FInstallBundleCombinedProgressTracker&& Other);
	
	FInstallBundleCombinedProgressTracker& operator=(const FInstallBundleCombinedProgressTracker& Other);
	FInstallBundleCombinedProgressTracker& operator=(FInstallBundleCombinedProgressTracker&& Other);
	
	//Setup tracking for all bundles required in the supplied BundleContentState
	void SetBundlesToTrackFromContentState(const FInstallBundleCombinedContentState& BundleContentState, TArrayView<FName> BundlesToTrack);
	
	//Get current CombinedBundleStatus for everything setup to track
	const FCombinedProgress& GetCurrentCombinedProgress() const;
	
	//Useful for resolving tick order issue
	void ForceTick() { Tick(0); }

private:
	bool Tick(float dt);
	void UpdateBundleCache();
	void UpdateCombinedStatus();
	
	void SetupDelegates();
	void CleanUpDelegates();
	
	//Called so we can track when a bundle is finished
	void OnBundleInstallComplete(FInstallBundleRequestResultInfo CompletedBundleInfo);
	void OnBundleInstallPauseChanged(FInstallBundlePauseInfo PauseInfo);
	
	float GetCombinedProgressPercent() const;
	
private:
	//All bundles we need including pre-reqs
	TArray<FName> RequiredBundleNames;
	
	//Internal Cache of all bundle statuses to track progress
	TMap<FName, FInstallBundleProgress> BundleStatusCache;
	
	//Bundle weights that determine what % of the overall install each bundle represents
	TMap<FName, float> CachedBundleWeights;
	
	FCombinedProgress CurrentCombinedProgress;
	
	TWeakPtr<IInstallBundleManager> InstallBundleManager;
	FDelegateHandle TickHandle;
};
