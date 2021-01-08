// Copyright Epic Games, Inc. All Rights Reserved.

#include "ActorPartition/PartitionActor.h"

#if WITH_EDITOR
#include "Components/BoxComponent.h"
#include "WorldPartition/WorldPartitionActorDesc.h"
#include "WorldPartition/ActorPartition/PartitionActorDesc.h"
#endif

#define LOCTEXT_NAMESPACE "PartitionActor"

APartitionActor::APartitionActor(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
#if WITH_EDITORONLY_DATA
	, GridSize(1)
#endif
{
	USceneComponent* SceneComponent = CreateDefaultSubobject<USceneComponent>(TEXT("RootComponent0"));
	RootComponent = SceneComponent;
	RootComponent->Mobility = EComponentMobility::Static;
}

#if WITH_EDITOR
TUniquePtr<FWorldPartitionActorDesc> APartitionActor::CreateClassActorDesc() const
{
	return TUniquePtr<FWorldPartitionActorDesc>(new FPartitionActorDesc());
}

bool APartitionActor::CanDeleteSelectedActor(FText& OutReason) const
{
	if (!Super::CanDeleteSelectedActor(OutReason))
	{
		return false;
	}

	check(GetLevel());
	check(GetLevel()->bIsPartitioned);
	OutReason = LOCTEXT("CantDeleteSelectedPartitionActor", "Can't delete Partition Actor (automatically managed).");
	return false;
}
#endif

#undef LOCTEXT_NAMESPACE