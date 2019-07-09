// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "GenericPlatform/GenericPlatformInstallBundleManager.h"

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
			,CompilingShaders
			,NoUpdateRequired
			,Finished
		};
		
		float ProgressPercent;
		ECombinedBundleStateEnum CombinedState;
		bool bIsPaused;
		bool bDoesCurrentStateSupportPausing;
		
		FCombinedBundleStatus()
		: ProgressPercent(0.f)
		, CombinedState(ECombinedBundleStateEnum::Unknown)
		, bIsPaused(false)
		, bDoesCurrentStateSupportPausing(false)
		{
		}
	};
	
public:
	FBundlePrereqCombinedStatusHelper();
	~FBundlePrereqCombinedStatusHelper();
	
	//Setup tracking for all bundles required in the supplied BundleContentState
	void SetBundlesToTrackFromContentState(FInstallBundleContentState& BundleContentState);
	
	//Get current CombinedBundleStatus for everything setup to track
	const FCombinedBundleStatus& GetCurrentCombinedState() const;
	
private:
	bool Tick(float dt);
	void UpdateBundleCache();
	void UpdateCombinedStatus();
	
	void SetupDelegates();
	void CleanUpDelegates();
	
	//Called so we can track when a bundle is finished
	void OnBundleInstallComplete(FInstallBundleResultInfo CompletedBundleInfo);
	
	float GetCombinedProgressPercent();
	float GetIndividualWeightedProgressPercent(FInstallBundleStatus& Bundle);
	
private:
	//All bundles we need including pre-reqs
	TArray<FName> RequiredBundleNames;
	
	//Internal Cache of all bundle statuses to track progress
	TMap<FName, FInstallBundleStatus> BundleStatusCache;
	
	//Bundle weights that determine what % of the overall install each bundle represents
	TMap<FName, float> CachedBundleWeights;
	
	FCombinedBundleStatus CurrentCombinedStatus;
	
	//How to weight downloads vs. installs
	float DownloadWeight;
	float InstallWeight;
	
	bool bBundleNeedsUpdate;
	
	IPlatformInstallBundleManager* InstallBundleManager;
	FDelegateHandle TickHandle;
};
