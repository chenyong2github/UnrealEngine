// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DisplayClusterEnums.generated.h"


/**
 * Display cluster operation mode
 */
UENUM(BlueprintType)
enum class EDisplayClusterOperationMode : uint8
{
	Cluster = 0,
	Standalone,
	Editor,
	Disabled
};


/**
 * Display cluster synchronization groups
 */
UENUM(BlueprintType)
enum class EDisplayClusterSyncGroup : uint8
{
	PreTick = 0,
	Tick,
	PostTick
};
