// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	HairRendering.h: Hair strands LUT generation.
=============================================================================*/

#pragma once

#include "CoreMinimal.h"
#include "RendererInterface.h"

enum FHairLUTType
{
	HairLUTType_DualScattering,
	HairLUTType_MeanEnergy,
	HairLUTTypeCount
};

struct FHairLUT
{
	TRefCountPtr<IPooledRenderTarget> Textures[HairLUTTypeCount];
};

/// Returns Hair LUTs. LUTs are generated on demand.
FHairLUT GetHairLUT(FRHICommandListImmediate& RHICmdList, const FViewInfo& View);
