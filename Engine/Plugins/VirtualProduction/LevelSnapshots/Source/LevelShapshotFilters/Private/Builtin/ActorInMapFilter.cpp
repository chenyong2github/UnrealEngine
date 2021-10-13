// Copyright Epic Games, Inc. All Rights Reserved.

#include "ActorInMapFilter.h"

#include "Engine/Level.h"
#include "GameFramework/Actor.h"

namespace
{
	bool IsActorInMap(const AActor* Actor, const FString& MapNameToCheck)
	{
		const ULevel* Level = Actor->GetLevel();

		if (!ensure(Level))
		{
			return false;
		}

		const UWorld* World = Level->GetTypedOuter<UWorld>();

		if (!ensure(World))
		{
			return false;
		}

		return World->GetName() == MapNameToCheck;
	}

	EFilterResult::Type IsActorAllowed(const AActor* Actor, const TArray<TSoftObjectPtr<UWorld>>& AllowedLevels)
	{
		for (const TSoftObjectPtr<UWorld>& AllowedLevel : AllowedLevels)
		{
			if (IsActorInMap(Actor, AllowedLevel.GetAssetName()))
			{
				return EFilterResult::Include;
			}
		}

		return EFilterResult::Exclude;
	}
};

EFilterResult::Type UActorInMapFilter::IsActorValid(const FIsActorValidParams& Params) const
{
	return IsActorAllowed(Params.LevelActor, AllowedLevels);
}

EFilterResult::Type UActorInMapFilter::IsAddedActorValid(const FIsAddedActorValidParams& Params) const
{
	return IsActorAllowed(Params.NewActor, AllowedLevels);
}


