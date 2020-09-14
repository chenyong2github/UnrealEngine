// Copyright Epic Games, Inc. All Rights Reserved.

#include "WorldPartition/ActorPartition/PartitionActorDesc.h"
#include "ActorPartition/PartitionActor.h"

#if WITH_EDITOR
#include "Engine/Level.h"
FPartitionActorDesc::FPartitionActorDesc(const FWorldPartitionActorDescData& DescData, uint32 InGridSize, int64 InGridIndexX, int64 InGridIndexY, int64 InGridIndexZ)
	: FWorldPartitionActorDesc(DescData)
	, GridSize(InGridSize)
	, GridIndexX(InGridIndexX)
	, GridIndexY(InGridIndexY)
	, GridIndexZ(InGridIndexZ)
{}

FPartitionActorDesc::FPartitionActorDesc(AActor* InActor)
	: FWorldPartitionActorDesc(InActor)
{
	APartitionActor* PartitionActor = CastChecked<APartitionActor>(InActor);

	GridSize = PartitionActor->GridSize;

	const FVector ActorLocation = InActor->GetActorLocation();
	GridIndexX = FMath::FloorToInt(ActorLocation.X / GridSize);
	GridIndexY = FMath::FloorToInt(ActorLocation.Y / GridSize);
	GridIndexZ = FMath::FloorToInt(ActorLocation.Z / GridSize);
}
#endif
