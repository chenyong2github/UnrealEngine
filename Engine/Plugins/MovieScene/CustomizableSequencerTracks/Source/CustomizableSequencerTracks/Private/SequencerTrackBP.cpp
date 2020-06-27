// Copyright Epic Games, Inc. All Rights Reserved.

#include "SequencerTrackBP.h"
#include "SequencerSectionBP.h"

#include "EntitySystem/MovieSceneEntitySystemLinker.h"
#include "EntitySystem/MovieSceneInstanceRegistry.h"


UMovieSceneSection* USequencerTrackBP::CreateNewSection()
{
	if (UClass* Class = DefaultSectionType.Get())
	{
		return NewObject<USequencerSectionBP>(this, Class);
	}

	for (TSubclassOf<USequencerSectionBP> SupportedSection : SupportedSections)
	{
		if (UClass* Class = SupportedSection.Get())
		{
			return NewObject<USequencerSectionBP>(this, Class);
		}
	}

	ensureMsgf(false, TEXT("Track does not have any supported section types. Returning a base class to avoid crashing."));
	return NewObject<UMovieSceneSection>(this);
}

void USequencerTrackBP::GetAssetRegistryTags(TArray<FAssetRegistryTag>& OutTags) const
{
	Super::GetAssetRegistryTags(OutTags);
}