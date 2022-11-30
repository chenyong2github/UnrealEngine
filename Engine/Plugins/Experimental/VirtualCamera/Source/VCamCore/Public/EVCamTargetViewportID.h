// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EVCamTargetViewportID.generated.h"

UENUM(BlueprintType, meta=(DisplayName = "VCam Target Viewport ID"))
enum class EVCamTargetViewportID : uint8
{
	CurrentlySelected = 0,
	Viewport1 = 1,
	Viewport2 = 2,
	Viewport3 = 3,
	Viewport4 = 4,

	Count UMETA(Hidden)
};