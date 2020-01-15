// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	HairStrandsDebug.h: Hair strands debug display.
=============================================================================*/

#pragma once

#include "CoreMinimal.h"
#include "RendererInterface.h"

void RenderHairStrandsDebugInfo(
	FRHICommandListImmediate& RHICmdList,
	TArray<FViewInfo>& Views,
	const struct FHairStrandsDatas* HairDatas,
	const struct FHairStrandClusterData& HairClusterData);