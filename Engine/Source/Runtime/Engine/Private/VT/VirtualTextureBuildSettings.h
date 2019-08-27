// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "VirtualTextureBuildSettings.generated.h"

/** Build settings used for virtual textures. */
USTRUCT()
struct FVirtualTextureBuildSettings
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY()
	int32 TileSize;

	UPROPERTY()
	int32 TileBorderSize;

	UPROPERTY()
	bool bEnableCompressCrunch;

	UPROPERTY()
	bool bEnableCompressZlib;

	/** Initialize with default build settings. These are defined by the current project setup. */
	ENGINE_API void Init();
};
