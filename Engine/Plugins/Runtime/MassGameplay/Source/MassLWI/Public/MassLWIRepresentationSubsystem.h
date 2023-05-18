// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MassRepresentationSubsystem.h"
#include "MassExternalSubsystemTraits.h"
#include "MassLWIRepresentationSubsystem.generated.h"


UCLASS()
class MASSLWI_API UMassLWIRepresentationSubsystem : public UMassRepresentationSubsystem
{
	GENERATED_BODY()

public:
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnActorSpawned, AActor&);

	AActor* GetOrSpawnActorFromTemplate(const FMassEntityHandle MassAgent, const FTransform& Transform, const int16 TemplateActorIndex
		, FMassActorSpawnRequestHandle& SpawnRequestHandle, float Priority, FMassActorPreSpawnDelegate ActorPreSpawnDelegate
		, FMassActorPostSpawnDelegate ActorPostSpawnDelegate);

	FOnActorSpawned& GetOnActorSpawnedDelegate() { return OnActorSpawnedDelegate; }

protected:
	FOnActorSpawned OnActorSpawnedDelegate;
};

template<>
struct TMassExternalSubsystemTraits<UMassLWIRepresentationSubsystem> final
{
	enum
	{
		GameThreadOnly = true
	};
};
