// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Containers/Map.h"
#include "WorldPartition/WorldPartitionActorDesc.h"

class ENGINE_API FInstancedObjectsActorDesc : public FWorldPartitionActorDesc
{
public:
	int32 GridSize;
	int64 GridIndexX;
	int64 GridIndexY;
	int64 GridIndexZ;

#if WITH_EDITOR
protected:
	FInstancedObjectsActorDesc() = delete;
	FInstancedObjectsActorDesc(const FWorldPartitionActorDescData& DescData, int32 InGridSize, int64 InGridIndexX, int64 InGridIndexY, int64 InGridIndexZ);
	FInstancedObjectsActorDesc(AActor* InActor);

	friend class FInstancedObjectsActorDescFactory;
#endif
};
