// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Containers/Map.h"
#include "WorldPartition/WorldPartitionActorDesc.h"

class ENGINE_API FPartitionActorDesc : public FWorldPartitionActorDesc
{
#if WITH_EDITOR
	friend class FPartitionActorDescFactory;

public:
	uint32 GridSize;
	int64 GridIndexX;
	int64 GridIndexY;
	int64 GridIndexZ;
	FGuid GridGuid;
protected:
	virtual void Init(const AActor* InActor) override;
	virtual void Serialize(FArchive& Ar) override;
#endif
};
