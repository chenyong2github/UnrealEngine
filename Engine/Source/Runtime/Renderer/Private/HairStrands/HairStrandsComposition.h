// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	HairStrandsComposition.h: Hair strands pixel composition implementation.
=============================================================================*/

#pragma once

#include "CoreMinimal.h"
#include "RendererInterface.h"

void RenderHairComposeSubPixel(
	FRHICommandListImmediate& RHICmdList, 
	const TArray<FViewInfo>& Views,
	const struct FHairStrandsDatas* HairDatas);
