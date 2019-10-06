// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	HairRendering.h: Hair rendering implementation.
=============================================================================*/

#pragma once

#include "CoreMinimal.h"
#include "RendererInterface.h"
#include "Shader.h"
#include "HairStrandsUtils.h"
#include "HairStrandsCluster.h"
#include "HairStrandsLUT.h"
#include "HairStrandsDeepShadow.h"
#include "HairStrandsVoxelization.h"
#include "HairStrandsVisibility.h"
#include "HairStrandsTransmittance.h"
#include "HairStrandsEnvironment.h"
#include "HairStrandsComposition.h"
#include "HairStrandsDebug.h"
#include "HairStrandsInterface.h"

/// Hold all the hair strands data
struct FHairStrandsDatas
{
	FHairStrandsDeepShadowViews DeepShadowViews;
	FHairStrandsVisibilityViews HairVisibilityViews;
	FHairStrandsClusterViews HairClusterPerViews;
};

enum class EHairStrandsInterpolationType
{
	RenderStrands,
	SimulationStrands
};

void RunHairStrandsInterpolation(
	FRHICommandListImmediate& RHICmdList, 
	EWorldType::Type WorldType, 
	TShaderMap<FGlobalShaderType>* ShaderMap, 
	EHairStrandsInterpolationType Type);