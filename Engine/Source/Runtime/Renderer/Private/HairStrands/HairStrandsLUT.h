// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	HairRendering.h: Hair strands LUT generation.
=============================================================================*/

#pragma once

#include "CoreMinimal.h"
#include "RendererInterface.h"
#include "RenderGraphDefinitions.h"

class FViewInfo;

enum FHairLUTType
{
	HairLUTType_DualScattering,
	HairLUTType_MeanEnergy,
	HairLUTType_Coverage,
	HairLUTTypeCount
};

struct FHairLUT
{
	FRDGTextureRef Textures[HairLUTTypeCount] = { nullptr, nullptr, nullptr };
};

/// Returns Hair LUTs. LUTs are generated on demand.
FHairLUT GetHairLUT(FRDGBuilder& GraphBuilder, const FViewInfo& View);