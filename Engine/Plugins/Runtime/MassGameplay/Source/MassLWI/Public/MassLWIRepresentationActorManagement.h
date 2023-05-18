// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MassRepresentationActorManagement.h"
#include "MassLWIRepresentationActorManagement.generated.h"


UCLASS()
class MASSLWI_API UMassLWIRepresentationActorManagement : public UMassRepresentationActorManagement
{
	GENERATED_BODY()

public:

protected:
	virtual AActor* GetOrSpawnActor(UMassRepresentationSubsystem& RepresentationSubsystem, FMassEntityManager& EntitySubsystem
		, const FMassEntityHandle MassAgent, FMassActorFragment& ActorInfo, const FTransform& Transform, const int16 TemplateActorIndex
		, FMassActorSpawnRequestHandle& SpawnRequestHandle, const float Priority) const override;
};
