// Copyright Epic Games, Inc. All Rights Reserved.

#include "BundlePrereqCombinedStatusHelper.h"
#include "Containers/Ticker.h"
#include "InstallBundleManagerPrivatePCH.h"

FBundlePrereqCombinedStatusHelper::FBundlePrereqCombinedStatusHelper()
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
	IInstallBundleManager::PausedBundleDelegate.AddRaw(this, &FBundlePrereqCombinedStatusHelper::OnBundleInstallPauseChanged);
	TickHandle = FTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateRaw(this, &FBundlePrereqCombinedStatusHelper::Tick));
}

void FBundlePrereqCombinedStatusHelper::CleanUpDelegates()
{
	IInstallBundleManager::InstallBundleCompleteDelegate.RemoveAll(this);
	IInstallBundleManager::PausedBundleDelegate.RemoveAll(this);
	if (TickHandle.IsValid())
	{
		FTicker::GetCoreTicker().RemoveTicker(TickHandle);
		TickHandle.Reset();
	}
}

void FBundlePrereqCombinedStatusHelper::SetBundlesToTrackFromContentState(const FInstallBundleCombinedContentState& BundleContentState, TArrayView<FName> BundlesToTrack)
{
	RequiredBundleNames.Empty();
	CachedBundleWeights.Empty();
	BundleStatusCache.Empty();
	
	bBundleNeedsUpdate = false;
	float TotalWeight = 0.0f;
	for (const FName& Bundle : BundlesToTrack)
	{
		const FInstallBundleContentState* BundleState = BundleContentState.IndividualBundleStates.Find(Bundle);
		if (ensureAlwaysMsgf(BundleState, TEXT("Trying to track unknown bundle %s"), *Bundle.ToString()))
		{
			//Track if we need any kind of bundle updates
			if (BundleState->State == EInstallBundleContentState::NotInstalled || BundleState->State == EInstallBundleContentState::NeedsUpdate)
			{
				bBundleNeedsUpdate = true;
			}

			//Save required bundles and their weights
			RequiredBundleNames.Add(Bundle);
			CachedBundleWeights.FindOrAdd(Bundle) = BundleState->Weight;
			TotalWeight += BundleState->Weight;
		}
	}

	CurrentCombinedStatus.bBundleRequiresUpdate = bBundleNeedsUpdate;
	
	if (TotalWeight > 0.0f)
	{
		for (TPair<FName, float>& BundleWeightPair : CachedBundleWeights)
		{
			BundleWeightPair.Value /= TotalWeight;
		}
	}

	// If no bundles to track, we are done
	if (RequiredBundleNames.Num() == 0)
	{
		CurrentCombinedStatus.ProgressPercent = 1.0f;
		CurrentCombinedStatus.CombinedState = FCombinedBundleStatus::ECombinedBundleStateEnum::Finished;
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
			TOptional<FInstallBundleProgress> BundleProgress = InstallBundleManager->GetBundleProgress(BundleName);
			
			//Copy progress to the cache as long as we have progress to copy.
			if (BundleProgress.IsSet())
			{
				BundleStatusCache.Add(BundleName, BundleProgress.GetValue());
			}
		}
	}
}

void FBundlePrereqCombinedStatusHelper::UpdateCombinedStatus()
{
	if (RequiredBundleNames.Num() == 0)
		return;

	CurrentCombinedStatus.ProgressPercent = GetCombinedProgressPercent();
	
	EInstallBundleStatus EarliestBundleState = EInstallBundleStatus::Count;
	EInstallBundlePauseFlags CombinedPauseFlags = EInstallBundlePauseFlags::None;
	bool bIsAnythingPaused = false;
	bool bIsAnythingFinishing = false;
	
	//if we don't yet have a bundle status cache entry for a particular requirement
	//then we can't yet tell what work is required on that bundle yet. We need to go ahead and make sure we don't
	//show a status like "Installed" before we know what state that bundle is in. Make sure we show at LEAST
	//updating in that case, so start with Downloading since that is the first Updating case
	if ((BundleStatusCache.Num() < RequiredBundleNames.Num())
		&& (BundleStatusCache.Num() > 0))
	{
		EarliestBundleState = EInstallBundleStatus::Updating;
	}
	
	float EarliestFinishingPercent = 1.0f;
	for (const TPair<FName,FInstallBundleProgress>& BundlePair : BundleStatusCache)
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
		CombinedPauseFlags |= BundlePair.Value.PauseFlags;
	}
	
	//if we have any paused bundles, and we have any bundle that isn't finished installed, we are Paused
	//if everything is installed ignore the pause flags as we completed after pausing the bundles
	CurrentCombinedStatus.bIsPaused = (bIsAnythingPaused && (EarliestBundleState < EInstallBundleStatus::Ready));
	if(CurrentCombinedStatus.bIsPaused)
	{
		CurrentCombinedStatus.CombinedPauseFlags = CombinedPauseFlags;
	}
	else
	{
		CurrentCombinedStatus.CombinedPauseFlags = EInstallBundlePauseFlags::None;
	}
	
	//if the bundle does not need an update, all the phases we go through don't support pausing (Mounting ,Compiling Shaders, etc)
	//Otherwise start with True and override those specific cases bellow
	CurrentCombinedStatus.bDoesCurrentStateSupportPausing = bBundleNeedsUpdate;
	
	if ((EarliestBundleState == EInstallBundleStatus::Requested) || (EarliestBundleState == EInstallBundleStatus::Count))
	{
		CurrentCombinedStatus.CombinedState = FCombinedBundleStatus::ECombinedBundleStateEnum::Initializing;
	}
	else if (EarliestBundleState <= EInstallBundleStatus::Updating)
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
	else if (EarliestBundleState == EInstallBundleStatus::Ready)
	{
		CurrentCombinedStatus.CombinedState = FCombinedBundleStatus::ECombinedBundleStateEnum::Finished;
		CurrentCombinedStatus.bDoesCurrentStateSupportPausing = false;
	}
	else
	{
		CurrentCombinedStatus.CombinedState = FCombinedBundleStatus::ECombinedBundleStateEnum::Unknown;
	}
}

float FBundlePrereqCombinedStatusHelper::GetCombinedProgressPercent() const
{
	float AllBundleProgressPercent = 0.f;
	
	ensureAlwaysMsgf((CachedBundleWeights.Num() >= BundleStatusCache.Num()), TEXT("Missing Cache Entries for BundleWeights!Any missing bundles will have 0 for their progress!"));
	
	for (const TPair<FName,FInstallBundleProgress>& BundleStatusPair : BundleStatusCache)
	{
		const float* FoundWeight = CachedBundleWeights.Find(BundleStatusPair.Key);
		if (ensureAlwaysMsgf((nullptr != FoundWeight), TEXT("Found missing entry for BundleWeight! Bundle %s does not have a weight entry!"), *(BundleStatusPair.Key.ToString())))
		{
			AllBundleProgressPercent += ((*FoundWeight) * BundleStatusPair.Value.Install_Percent);
		}
	}
	
	FMath::Clamp(AllBundleProgressPercent, 0.f, 1.0f);
	return AllBundleProgressPercent;
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

void FBundlePrereqCombinedStatusHelper::OnBundleInstallComplete(FInstallBundleRequestResultInfo CompletedBundleInfo)
{
	const FName CompletedBundleName = CompletedBundleInfo.BundleName;
	const bool bBundleCompletedSuccessfully = (CompletedBundleInfo.Result == EInstallBundleResult::OK);
	const bool bWasRequiredBundle = RequiredBundleNames.Contains(CompletedBundleInfo.BundleName);
	
	if (bBundleCompletedSuccessfully && bWasRequiredBundle)
	{
		//Make sure our BundleCache shows this as finished all the way
		FInstallBundleProgress& BundleCacheInfo = BundleStatusCache.FindOrAdd(CompletedBundleName);
		BundleCacheInfo.Status = EInstallBundleStatus::Ready;
		
		TOptional<FInstallBundleProgress> BundleProgress = InstallBundleManager->GetBundleProgress(CompletedBundleName);
		if (ensureAlwaysMsgf(BundleProgress.IsSet(), TEXT("Expected to find BundleProgress for completed bundle, but did not. Leaving old progress values")))
		{
			BundleCacheInfo = BundleProgress.GetValue();
		}
	}
}

// It's not really necessary to have this, but it allows for a fallback if GetBundleProgress() returns null.
// Normally that shouldn't happen, but right now its handy while I refactor bundle progress.
void FBundlePrereqCombinedStatusHelper::OnBundleInstallPauseChanged(FInstallBundlePauseInfo PauseInfo)
{
	const bool bWasRequiredBundle = RequiredBundleNames.Contains(PauseInfo.BundleName);
	if (bWasRequiredBundle)
	{
		FInstallBundleProgress& BundleCacheInfo = BundleStatusCache.FindOrAdd(PauseInfo.BundleName);
		BundleCacheInfo.PauseFlags = PauseInfo.PauseFlags;
	}
}
