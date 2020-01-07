// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "PicpPostProcessing.generated.h"

UENUM(BlueprintType)
enum class EPicpBlurPostProcessShaderType: uint8
{
	Gaussian = 0,
	Dilate,
};
