// Copyright Epic Games, Inc. All Rights Reserved.

#include "WorldPartition/ActorPartition/PartitionActorDesc.h"
#include "ActorPartition/PartitionActor.h"

#if WITH_EDITOR
#include "Engine/Level.h"

void FPartitionActorDesc::InitFrom(const AActor* InActor)
{
	FWorldPartitionActorDesc::InitFrom(InActor);

	const APartitionActor* PartitionActor = CastChecked<APartitionActor>(InActor);

	GridSize = PartitionActor->GridSize;

	const FVector ActorLocation = InActor->GetActorLocation();
	GridIndexX = FMath::FloorToInt(ActorLocation.X / GridSize);
	GridIndexY = FMath::FloorToInt(ActorLocation.Y / GridSize);
	GridIndexZ = FMath::FloorToInt(ActorLocation.Z / GridSize);
}

void FPartitionActorDesc::Serialize(FArchive& Ar)
{
	FWorldPartitionActorDesc::Serialize(Ar);

	Ar << GridSize << GridIndexX << GridIndexY << GridIndexZ;
}
#endif
