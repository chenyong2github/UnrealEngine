// Copyright Epic Games, Inc. All Rights Reserved.

#include "Builtin/ActorInMapFilter.h"

#include "Engine/Level.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"

namespace ActorInMapFilter
{
	static bool IsActorInMap(const AActor* Actor, const FString& MapNameToCheck)
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

	static EFilterResult::Type IsActorAllowed(const AActor* Actor, const TArray<TSoftObjectPtr<UWorld>>& AllowedLevels)
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
	return ActorInMapFilter::IsActorAllowed(Params.LevelActor, AllowedLevels);
}

EFilterResult::Type UActorInMapFilter::IsAddedActorValid(const FIsAddedActorValidParams& Params) const
{
	return ActorInMapFilter::IsActorAllowed(Params.NewActor, AllowedLevels);
}


