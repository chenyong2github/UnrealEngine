// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "WorldPartition/WorldPartitionActorDescFactory.h"

class ENGINE_API FHLODActorDescFactory : public FWorldPartitionActorDescFactory
{
public:
#if WITH_EDITOR
	virtual FWorldPartitionActorDesc* Create() override;
#endif
};
