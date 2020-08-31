// Copyright Epic Games, Inc. All Rights Reserved.

#include "WorldPartition/ActorPartition/InstancedObjectsActorDesc.h"
#include "ActorPartition/InstancedObjectsActor.h"

#if WITH_EDITOR
#include "Engine/Level.h"
FInstancedObjectsActorDesc::FInstancedObjectsActorDesc(const FWorldPartitionActorDescData& DescData, int32 InGridSize, int64 InGridIndexX, int64 InGridIndexY, int64 InGridIndexZ)
	: FWorldPartitionActorDesc(DescData)
	, GridSize(InGridSize)
	, GridIndexX(InGridIndexX)
	, GridIndexY(InGridIndexY)
	, GridIndexZ(InGridIndexZ)
{}

FInstancedObjectsActorDesc::FInstancedObjectsActorDesc(AActor* InActor)
	: FWorldPartitionActorDesc(InActor)
{
	AInstancedObjectsActor* InstancedObjectsActor = CastChecked<AInstancedObjectsActor>(InActor);

	GridSize = InstancedObjectsActor->GridSize;

	const FVector ActorLocation = InActor->GetActorLocation();
	GridIndexX = FMath::FloorToInt(ActorLocation.X / GridSize);
	GridIndexY = FMath::FloorToInt(ActorLocation.Y / GridSize);
	GridIndexZ = FMath::FloorToInt(ActorLocation.Z / GridSize);
}
#endif
