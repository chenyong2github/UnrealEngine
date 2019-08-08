// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "BundlePrereqCombinedStatusHelper.h"
#include "Containers/Ticker.h"
#include "InstallBundleManagerPrivatePCH.h"

FBundlePrereqCombinedStatusHelper::FBundlePrereqCombinedStatusHelper()
: DownloadWeight(1.f)
, InstallWeight(1.f)
, RequiredBundleNames()
, BundleStatusCache()
, CachedBundleWeights()
, CurrentCombinedStatus()
, bBundleNeedsUpdate(false)
, InstallBundleManager(nullptr)
, TickHandle()
{
	SetupDelegates();
}

FBundlePrereqCombinedStatusHelper::~FBundlePrereqCombinedStatusHelper()
{
	CleanUpDelegates();
}

FBundlePrereqCombinedStatusHelper::FBundlePrereqCombinedStatusHelper(const FBundlePrereqCombinedStatusHelper& Other)
{
	*this = Other;
}

FBundlePrereqCombinedStatusHelper::FBundlePrereqCombinedStatusHelper(FBundlePrereqCombinedStatusHelper&& Other)
{
	*this = MoveTemp(Other);
}

FBundlePrereqCombinedStatusHelper& FBundlePrereqCombinedStatusHelper::operator=(const FBundlePrereqCombinedStatusHelper& Other)
{
	if (this != &Other)
	{
		//Just copy all this data
		DownloadWeight = Other.DownloadWeight;
		InstallWeight = Other.InstallWeight;
		RequiredBundleNames = Other.RequiredBundleNames;
		BundleStatusCache = Other.BundleStatusCache;
		CachedBundleWeights = Other.CachedBundleWeights;
		CurrentCombinedStatus = Other.CurrentCombinedStatus;
		bBundleNeedsUpdate = Other.bBundleNeedsUpdate;
		InstallBundleManager = Other.InstallBundleManager;
		
		//Don't copy TickHandle as we want to setup our own here
		SetupDelegates();
	}
	
	return *this;
}

FBundlePrereqCombinedStatusHelper& FBundlePrereqCombinedStatusHelper::operator=(FBundlePrereqCombinedStatusHelper&& Other)
{
	if (this != &Other)
	{
		//Just copy small data
		DownloadWeight = Other.DownloadWeight;
		InstallWeight = Other.InstallWeight;
		CurrentCombinedStatus = Other.CurrentCombinedStatus;
		bBundleNeedsUpdate = Other.bBundleNeedsUpdate;
		InstallBundleManager = Other.InstallBundleManager;

		//Move bigger data
		RequiredBundleNames = MoveTemp(Other.RequiredBundleNames);
		BundleStatusCache = MoveTemp(Other.BundleStatusCache);
		CachedBundleWeights = MoveTemp(Other.CachedBundleWeights);

		//Prevent other from having callbacks now that its information is gone
		Other.CleanUpDelegates();
	
		//Don't copy TickHandle as we want to setup our own here
		SetupDelegates();
	}
	
	return *this;
}

void FBundlePrereqCombinedStatusHelper::SetupDelegates()
{
	CleanUpDelegates();
	
	IInstallBundleManager::InstallBundleCompleteDelegate.AddRaw(this, &FBundlePrereqCombinedStatusHelper::OnBundleInstallComplete);
	TickHandle = FTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateRaw(this, &FBundlePrereqCombinedStatusHelper::Tick));
}

void FBundlePrereqCombinedStatusHelper::CleanUpDelegates()
{
	IInstallBundleManager::InstallBundleCompleteDelegate.RemoveAll(this);
	if (TickHandle.IsValid())
	{
		FTicker::GetCoreTicker().RemoveTicker(TickHandle);
		TickHandle.Reset();
	}
}

void FBundlePrereqCombinedStatusHelper::SetBundlesToTrackFromContentState(FInstallBundleContentState& BundleContentState)
{
	RequiredBundleNames.Empty();
	CachedBundleWeights.Empty();
	BundleStatusCache.Empty();
	
	//Track if we need any kind of bundle updates
	bBundleNeedsUpdate = false;
	if (BundleContentState.State == EInstallBundleContentState::NotInstalled || BundleContentState.State == EInstallBundleContentState::NeedsUpdate)
	{
		bBundleNeedsUpdate = true;
	}
	CurrentCombinedStatus.bBundleRequiresUpdate = bBundleNeedsUpdate;
	
	//Save required bundles and their weights
	for (TPair<FName, float>& BundleStatePair : BundleContentState.IndividualBundleWeights)
	{
		RequiredBundleNames.Add(BundleStatePair.Key);
		CachedBundleWeights.FindOrAdd(BundleStatePair.Key) = BundleStatePair.Value;
	}
	
	//Go ahead and calculate initial values from the Bundle Cache
	UpdateBundleCache();
}

void FBundlePrereqCombinedStatusHelper::UpdateBundleCache()
{
	//if we haven't already set this up, lets try to set it now
	if (nullptr == InstallBundleManager)
	{
		InstallBundleManager = IInstallBundleManager::GetPlatformInstallBundleManager();
	}
	
	if (ensureAlwaysMsgf((nullptr != InstallBundleManager), TEXT("Invalid InstallBundleManager during UpdateBundleCache! Needs to be valid during run!")))
	{
		for (FName& BundleName : RequiredBundleNames)
		{
			TOptional<FInstallBundleStatus> BundleProgress = InstallBundleManager->GetBundleProgress(BundleName);
			
			//Copy progress to the cache as long as we have progress to copy.
			if (BundleProgress.IsSet())
			{
				FInstallBundleStatus& CachedStatus = BundleStatusCache.FindOrAdd(BundleName);
				CachedStatus = BundleProgress.GetValue();
			}
		}
	}
}

void FBundlePrereqCombinedStatusHelper::UpdateCombinedStatus()
{
	CurrentCombinedStatus.ProgressPercent = GetCombinedProgressPercent();
	
	EInstallBundleStatus EarliestBundleState = EInstallBundleStatus::Count;
	bool bIsAnythingPaused = false;
	bool bIsAnythingFinishing = false;
	
	//if we don't yet have a bundle status cache entry for a particular requirement
	//then we can't yet tell what work is required on that bundle yet. We need to go ahead and make sure we don't
	//show a status like "Installed" before we know what state that bundle is in. Make sure we show at LEAST
	//updating in that case, so start with Downloading since that is the first Updating case
	if ((BundleStatusCache.Num() < RequiredBundleNames.Num())
		&& (BundleStatusCache.Num() > 0))
	{
		EarliestBundleState = EInstallBundleStatus::Downloading;
	}
	
	float EarliestFinishingPercent = 1.0f;
	for (const TPair<FName,FInstallBundleStatus>& BundlePair : BundleStatusCache)
	{
		if (BundlePair.Value.Status < EarliestBundleState)
		{
			EarliestBundleState = BundlePair.Value.Status;
		}

		if (!bIsAnythingFinishing && BundlePair.Value.Status == EInstallBundleStatus::Finishing)
		{
			EarliestFinishingPercent = BundlePair.Value.Finishing_Percent;
			bIsAnythingFinishing = true;
		}
		
		bIsAnythingPaused = bIsAnythingPaused || BundlePair.Value.PauseFlags != EInstallBundlePauseFlags::None;
	}
	
	//if we have any paused bundles, and we have any bundle that isn't finished installed, we are Paused
	//if everything is installed ignore the pause flags as we completed after pausing the bundles
	CurrentCombinedStatus.bIsPaused = (bIsAnythingPaused && (EarliestBundleState < EInstallBundleStatus::Installed));
	
	//if the bundle does not need an update, all the phases we go through don't support pausing (Mounting ,Compiling Shaders, etc)
	//Otherwise start with True and override those specific cases bellow
	CurrentCombinedStatus.bDoesCurrentStateSupportPausing = bBundleNeedsUpdate;
	
	if ((EarliestBundleState == EInstallBundleStatus::Requested) || (EarliestBundleState == EInstallBundleStatus::Count))
	{
		CurrentCombinedStatus.CombinedState = FCombinedBundleStatus::ECombinedBundleStateEnum::Initializing;
	}
	else if (EarliestBundleState <= EInstallBundleStatus::Installing)
	{
		CurrentCombinedStatus.CombinedState = FCombinedBundleStatus::ECombinedBundleStateEnum::Updating;
	}
	else if (EarliestBundleState <= EInstallBundleStatus::Finishing)
	{
		//Handles the case where one of our Bundles was finishing and we have finished everything else.
		//Now just shows our earliest bundle that is finishing.
		if (bIsAnythingFinishing)
		{
			CurrentCombinedStatus.CombinedState = FCombinedBundleStatus::ECombinedBundleStateEnum::Finishing;
			CurrentCombinedStatus.ProgressPercent = EarliestFinishingPercent;
		}
		else
		{
			CurrentCombinedStatus.CombinedState = FCombinedBundleStatus::ECombinedBundleStateEnum::Updating;
		}
	}
	else if (EarliestBundleState == EInstallBundleStatus::Installed)
	{
		CurrentCombinedStatus.CombinedState = FCombinedBundleStatus::ECombinedBundleStateEnum::Finished;
		CurrentCombinedStatus.bDoesCurrentStateSupportPausing = false;
	}
	else
	{
		CurrentCombinedStatus.CombinedState = FCombinedBundleStatus::ECombinedBundleStateEnum::Unknown;
	}
}

float FBundlePrereqCombinedStatusHelper::GetCombinedProgressPercent()
{
	float AllBundleProgressPercent = 0.f;
	
	ensureAlwaysMsgf((CachedBundleWeights.Num() >= BundleStatusCache.Num()), TEXT("Missing Cache Entries for BundleWeights!Any missing bundles will have 0 for their progress!"));
	
	for (TPair<FName,FInstallBundleStatus>& BundleStatusPair : BundleStatusCache)
	{
		float* FoundWeight = CachedBundleWeights.Find(BundleStatusPair.Key);
		if (ensureAlwaysMsgf((nullptr != FoundWeight), TEXT("Found missing entry for BundleWeight! Bundle %s does not have a weight entry!"), *(BundleStatusPair.Key.ToString())))
		{
			AllBundleProgressPercent += ((*FoundWeight) * GetIndividualWeightedProgressPercent(BundleStatusPair.Value));
		}
	}
	
	FMath::Clamp(AllBundleProgressPercent, 0.f, 1.0f);
	return AllBundleProgressPercent;
}

float FBundlePrereqCombinedStatusHelper::GetIndividualWeightedProgressPercent(FInstallBundleStatus& BundleStatus)
{
	const float TotalWeight = DownloadWeight + InstallWeight;
	float CombinedOverallProgressPercent = 0.f;
	
	//If we are done installing then lets just coung our progress as 1.0f. Finishing progress is handled in UpdateCombinedStatus
	if (BundleStatus.Status > EInstallBundleStatus::Installing)
	{
		CombinedOverallProgressPercent = 1.0f;
	}
	else
	{
		if (ensureAlwaysMsgf((TotalWeight > 0), TEXT("Invalid Weights for FBundleStatusTracker! Need to have weights > 0.f!")))
		{
			float DownloadWeightedPercent = 0.f;
			bool bDidHaveBackgroundDownloadProgress = false;
			
			if (DownloadWeight > 0.f)
			{
				//Skip background downloads from contributing to progress if they aren't in use
				bDidHaveBackgroundDownloadProgress = (BundleStatus.BackgroundDownloadProgress.IsSet() && (BundleStatus.BackgroundDownloadProgress->BytesDownloaded > 0));
				if (bDidHaveBackgroundDownloadProgress)
				{
					const float AppliedDownloadWeight = DownloadWeight / TotalWeight;
					
					if (BundleStatus.Status > EInstallBundleStatus::Downloading)
					{
						//if we have moved past the download phase, just consider these
						//downloads as having finished
						DownloadWeightedPercent = 1.0f * AppliedDownloadWeight;
					}
					else
					{
						DownloadWeightedPercent = BundleStatus.BackgroundDownloadProgress->PercentComplete * AppliedDownloadWeight;
					}
				}
			}
			
			float InstallWeightedPercent = 0.f;
			
			//If we didn't have background download progress, just use our InstallWeight as we don't have Download Progress to use
			const float AppliedInstallWeight = bDidHaveBackgroundDownloadProgress ? (InstallWeight / TotalWeight) : 1.0f;
			InstallWeightedPercent = BundleStatus.Install_Percent * AppliedInstallWeight;
			
			CombinedOverallProgressPercent = DownloadWeightedPercent + InstallWeightedPercent;
			FMath::Clamp(CombinedOverallProgressPercent, 0.f, 1.0f);
		}
	}
	
	return CombinedOverallProgressPercent;
}

bool FBundlePrereqCombinedStatusHelper::Tick(float dt)
{
	UpdateBundleCache();
	UpdateCombinedStatus();
	
	//just always keep ticking
	return true;
}

const FBundlePrereqCombinedStatusHelper::FCombinedBundleStatus& FBundlePrereqCombinedStatusHelper::GetCurrentCombinedState() const
{
	return CurrentCombinedStatus;
}

void FBundlePrereqCombinedStatusHelper::OnBundleInstallComplete(FInstallBundleResultInfo CompletedBundleInfo)
{
	const FName CompletedBundleName = CompletedBundleInfo.BundleName;
	const bool bBundleCompletedSuccessfully = (CompletedBundleInfo.Result == EInstallBundleResult::OK);
	const bool bWasRequiredBundle = RequiredBundleNames.Contains(CompletedBundleInfo.BundleName);
	
	if (bBundleCompletedSuccessfully && bWasRequiredBundle)
	{
		//Make sure our BundleCache shows this as finished all the way
		FInstallBundleStatus& BundleCacheInfo = BundleStatusCache.FindOrAdd(CompletedBundleName);
		BundleCacheInfo.Status = EInstallBundleStatus::Installed;
		
		TOptional<FInstallBundleStatus> BundleProgress = InstallBundleManager->GetBundleProgress(CompletedBundleName);
		if (ensureAlwaysMsgf(BundleProgress.IsSet(), TEXT("Expected to find BundleProgress for completed bundle, but did not. Leaving old progress values")))
		{
			BundleCacheInfo = BundleProgress.GetValue();
		}
	}
}
