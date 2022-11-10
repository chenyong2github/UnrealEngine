// Copyright Epic Games, Inc.All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ScreenPass.h"

struct FMobileSeparateTranslucencyInputs
{
	FScreenPassTexture SceneColor;
	FScreenPassTexture SceneDepth;
};

// Returns whether separate translucency is enabled and there primitives to draw in the view
bool IsMobileTranslucencyAfterDOFActive(const FViewInfo& View);
bool IsMobileTranslucencyAfterDOFActive(const FViewInfo* Views, int32 NumViews);
bool IsMobileTranslucencyStandardActive(const FViewInfo& View);
bool IsMobileTranslucencyStandardActive(const FViewInfo* Views, int32 NumViews);