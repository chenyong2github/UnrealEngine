// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

class FWorldPartitionActorDesc;

class ENGINE_API FWorldPartitionActorDescFactory
{
public:
	virtual ~FWorldPartitionActorDescFactory() = default;

#if WITH_EDITOR
	virtual FWorldPartitionActorDesc* Create();
#endif
};