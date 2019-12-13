// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "InstallBundleManagerInterface.h"

//Handles calculating the bundle status by combining progress from all of its
//Prerequisites. Allows you to display one progress percent that is weighted based on all
//bundles' values.
class INSTALLBUNDLEMANAGER_API FBundlePrereqCombinedStatusHelper
{
public:
	//provide all our needed combined status information in 1 struct
	struct FCombinedBundleStatus
	{
		//Collapses all the bundle manager states into one of a few states so that you can show simple text based on this enum
		enum class ECombinedBundleStateEnum : int32
		{
			Unknown
			,Initializing
			,Updating
			,Finishing
			,Finished
		};
		
		float ProgressPercent = 0.0f;
		ECombinedBundleStateEnum CombinedState = ECombinedBundleStateEnum::Unknown;
		EInstallBundlePauseFlags CombinedPauseFlags = EInstallBundlePauseFlags::None;
		bool bIsPaused = false;
		bool bDoesCurrentStateSupportPausing = false;
		bool bBundleRequiresUpdate = false;
	};
	
public:
	FBundlePrereqCombinedStatusHelper();
	~FBundlePrereqCombinedStatusHelper();
	
	FBundlePrereqCombinedStatusHelper(const FBundlePrereqCombinedStatusHelper& Other);
	FBundlePrereqCombinedStatusHelper(FBundlePrereqCombinedStatusHelper&& Other);
	
	FBundlePrereqCombinedStatusHelper& operator=(const FBundlePrereqCombinedStatusHelper& Other);
	FBundlePrereqCombinedStatusHelper& operator=(FBundlePrereqCombinedStatusHelper&& Other);
	
	//Setup tracking for all bundles required in the supplied BundleContentState
	void SetBundlesToTrackFromContentState(const FInstallBundleCombinedContentState& BundleContentState, TArrayView<FName> BundlesToTrack);
	
	//Get current CombinedBundleStatus for everything setup to track
	const FCombinedBundleStatus& GetCurrentCombinedState() const;
	
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
	
	FCombinedBundleStatus CurrentCombinedStatus;
	
	bool bBundleNeedsUpdate = false;
	
	IInstallBundleManager* InstallBundleManager = nullptr;
	FDelegateHandle TickHandle;
};
