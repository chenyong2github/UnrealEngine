// Copyright Epic Games, Inc. All Rights Reserved.

#include "MassLWIRepresentationActorManagement.h"
#include "MassLWIRepresentationSubsystem.h"


//-----------------------------------------------------------------------------
// UMassLWIRepresentationActorManagement
//-----------------------------------------------------------------------------
AActor* UMassLWIRepresentationActorManagement::GetOrSpawnActor(UMassRepresentationSubsystem& RepresentationSubsystem
	, FMassEntityManager& EntityManager, const FMassEntityHandle MassAgent, FMassActorFragment& ActorInfo
	, const FTransform& Transform, const int16 TemplateActorIndex, FMassActorSpawnRequestHandle& SpawnRequestHandle, const float Priority) const
{
	if (UMassLWIRepresentationSubsystem* LWIRepresentationSubsystem = Cast<UMassLWIRepresentationSubsystem>(&RepresentationSubsystem))
	{
		return LWIRepresentationSubsystem->GetOrSpawnActorFromTemplate(MassAgent, Transform, TemplateActorIndex, SpawnRequestHandle, Priority,
			FMassActorPreSpawnDelegate::CreateUObject(this, &UMassRepresentationActorManagement::OnPreActorSpawn, &EntityManager),
			FMassActorPostSpawnDelegate::CreateUObject(this, &UMassRepresentationActorManagement::OnPostActorSpawn, &EntityManager));
	}

	return Super::GetOrSpawnActor(RepresentationSubsystem, EntityManager, MassAgent, ActorInfo, Transform, TemplateActorIndex, SpawnRequestHandle, Priority);
}