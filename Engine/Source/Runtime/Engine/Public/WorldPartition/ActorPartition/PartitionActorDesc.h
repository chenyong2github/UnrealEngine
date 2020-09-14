// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Containers/Map.h"
#include "WorldPartition/WorldPartitionActorDesc.h"

class ENGINE_API FPartitionActorDesc : public FWorldPartitionActorDesc
{
public:
	uint32 GridSize;
	int64 GridIndexX;
	int64 GridIndexY;
	int64 GridIndexZ;

#if WITH_EDITOR
protected:
	FPartitionActorDesc() = delete;
	FPartitionActorDesc(const FWorldPartitionActorDescData& DescData, uint32 InGridSize, int64 InGridIndexX, int64 InGridIndexY, int64 InGridIndexZ);
	FPartitionActorDesc(AActor* InActor);

	friend class FPartitionActorDescFactory;
#endif
};
