// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "OverlayRenderingParameters.generated.h"


UENUM(BlueprintType)
enum class ECameraOverlayRenderMode : uint8
{
	Over = 0,
	Under
};

UENUM(BlueprintType)
enum class EChromakeyMarkerUVSource: uint8
{
	ScreenSpace = 0,
	WarpMesh
};
