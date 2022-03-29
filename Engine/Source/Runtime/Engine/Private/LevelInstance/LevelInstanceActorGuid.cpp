// Copyright Epic Games, Inc. All Rights Reserved.

#include "LevelInstance/LevelInstanceActorGuid.h"
#include "GameFramework/Actor.h"

#if !WITH_EDITOR
void FLevelInstanceActorGuid::AssignIfInvalid()
{
	if (!ActorGuid.IsValid())
	{
		ActorGuid = FGuid::NewGuid();
	}
}
#endif

const FGuid& FLevelInstanceActorGuid::GetGuid() const
{
	check(Actor);
#if WITH_EDITOR
	const FGuid& Guid = Actor->GetActorGuid();
#else
	const FGuid& Guid = ActorGuid;
#endif
	check(Actor->IsTemplate() || Guid.IsValid());
	return Guid;
}

FArchive& operator<<(FArchive& Ar, FLevelInstanceActorGuid& LevelInstanceActorGuid)
{
	check(LevelInstanceActorGuid.Actor);
#if WITH_EDITOR
	if (Ar.IsSaving() && Ar.IsCooking() && !LevelInstanceActorGuid.Actor->IsTemplate())
	{
		FGuid Guid = LevelInstanceActorGuid.GetGuid();
		Ar << Guid;
	}
#else
	if (Ar.IsLoading())
	{
		if (LevelInstanceActorGuid.Actor->IsTemplate())
		{
			check(!LevelInstanceActorGuid.ActorGuid.IsValid());
		}
		else if (Ar.GetPortFlags() & PPF_Duplicate)
		{
			LevelInstanceActorGuid.ActorGuid = FGuid::NewGuid();
		}
		else if (Ar.IsPersistent())
		{
			Ar << LevelInstanceActorGuid.ActorGuid;
			check(LevelInstanceActorGuid.ActorGuid.IsValid());
		}
	}
#endif
	return Ar;
}