// Copyright Epic Games, Inc. All Rights Reserved.

#include "SequencerTrackFilters.h"
#include "Engine/World.h"

FSequencerTrackFilter_LevelFilter::~FSequencerTrackFilter_LevelFilter()
{
	if (CachedWorld.IsValid())
	{
		CachedWorld.Get()->OnLevelsChanged().RemoveAll(this);
		CachedWorld.Reset();
	}
}

bool FSequencerTrackFilter_LevelFilter::PassesFilter(FTrackFilterType InItem) const
{
	if (!InItem || !InItem->GetOutermost())
	{
		return false;
	}

	// For anything in a level, outermost should refer to the ULevel that contains it
	FString OutermostName = FPackageName::GetShortName(InItem->GetOutermost()->GetName());
	
	// Pass anything that is not in a hidden level
	return !HiddenLevels.Contains(OutermostName);
}

void FSequencerTrackFilter_LevelFilter::ResetFilter()
{
	HiddenLevels.Empty();

	BroadcastChangedEvent();
}

bool FSequencerTrackFilter_LevelFilter::IsLevelHidden(const FString& LevelName) const
{
	return HiddenLevels.Contains(LevelName);
}

void FSequencerTrackFilter_LevelFilter::HideLevel(const FString& LevelName)
{
	HiddenLevels.AddUnique(LevelName);

	BroadcastChangedEvent();
}

void FSequencerTrackFilter_LevelFilter::UnhideLevel(const FString& LevelName)
{
	HiddenLevels.Remove(LevelName);

	BroadcastChangedEvent();
}

void FSequencerTrackFilter_LevelFilter::UpdateWorld(UWorld* World)
{
	if (!CachedWorld.IsValid() || CachedWorld.Get() != World)
	{
		if (CachedWorld.IsValid())
		{
			CachedWorld.Get()->OnLevelsChanged().RemoveAll(this);
		}
		
		CachedWorld.Reset();
	
		if (IsValid(World))
		{
			CachedWorld = World;
			CachedWorld.Get()->OnLevelsChanged().AddRaw(this, &FSequencerTrackFilter_LevelFilter::HandleLevelsChanged);
		}

		HandleLevelsChanged();
	}
}

void FSequencerTrackFilter_LevelFilter::HandleLevelsChanged()
{
	if (!CachedWorld.IsValid())
	{
		ResetFilter();
		return;
	}

	const TArray<ULevel*>& WorldLevels = CachedWorld->GetLevels();
	
	if (WorldLevels.Num() < 2)
	{
		ResetFilter();
		return;
	}

	// Build a list of level names contained in the current world
	TArray<FString> WorldLevelNames;
	for (const ULevel* Level : WorldLevels)
	{
		if (IsValid(Level))
		{
			FString LevelName = FPackageName::GetShortName(Level->GetOutermost()->GetName());
			WorldLevelNames.Add(LevelName);
		}
	}

	// Rebuild our list of hidden level names to only include levels which are still in the world
	TArray<FString> OldHiddenLevels = HiddenLevels;
	HiddenLevels.Empty();
	for (FString LevelName : OldHiddenLevels)
	{
		if (WorldLevelNames.Contains(LevelName))
		{
			HiddenLevels.Add(LevelName);
		}
	}

	if (OldHiddenLevels.Num() != HiddenLevels.Num())
	{
		BroadcastChangedEvent();
	}
}