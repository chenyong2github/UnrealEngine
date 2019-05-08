// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GameFramework/Info.h"

#include "AROriginActor.generated.h"

/**
 * Simple actor that is spawned at the origin for AR systems that want to hang components on an actor
 * Spawned as a custom class for easier TObjectIterator use
 */
UCLASS(BlueprintType)
class AUGMENTEDREALITY_API AAROriginActor :
	public AInfo
{
	GENERATED_UCLASS_BODY()
public:
};
