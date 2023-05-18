// Copyright Epic Games, Inc. All Rights Reserved.

#include "MassLWIRepresentationSubsystem.h"
#include "Engine/Note.h"
#include "Engine/World.h"


//-----------------------------------------------------------------------------
// UMassLWIRepresentationSubsystem
//-----------------------------------------------------------------------------
AActor* UMassLWIRepresentationSubsystem::GetOrSpawnActorFromTemplate(const FMassEntityHandle MassAgent, const FTransform& Transform, const int16 TemplateActorIndex
	, FMassActorSpawnRequestHandle& SpawnRequestHandle, float Priority, FMassActorPreSpawnDelegate ActorPreSpawnDelegate
	, FMassActorPostSpawnDelegate ActorPostSpawnDelegate)
{
	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return nullptr;
	}
	else if (World->IsNetMode(NM_Client))
	{
		// this is where we'd sent a request to the server to spawn the actor
		//return SpawnedActor;
	}

	// @todo notify OnActorSpawnedDelegate

	return Super::GetOrSpawnActorFromTemplate(MassAgent, Transform, TemplateActorIndex
		, SpawnRequestHandle, Priority, ActorPreSpawnDelegate
		, ActorPostSpawnDelegate);
}
