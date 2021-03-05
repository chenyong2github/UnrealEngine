// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	HairRendering.h: Hair rendering implementation.
=============================================================================*/

#pragma once

#include "CoreMinimal.h"
#include "RendererInterface.h"
#include "Shader.h"

BEGIN_GLOBAL_SHADER_PARAMETER_STRUCT(FHairStrandsViewUniformParameters, )	
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D<uint4>, HairCategorizationTexture)
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, HairOnlyDepthTexture)
END_GLOBAL_SHADER_PARAMETER_STRUCT()

struct FHairStrandsViewData
{
	//FRDGTextureRef CategorizationTexture = nullptr;
	//FRDGTextureRef HairOnlyDepthTexture = nullptr;
	TRDGUniformBufferRef<FHairStrandsViewUniformParameters> UniformBuffer;
	bool bIsValid = false;
};

namespace HairStrands
{
	TRDGUniformBufferRef<FHairStrandsViewUniformParameters> CreateDefaultHairStrandsViewUniformBuffer(FRDGBuilder& GraphBuilder, FViewInfo& View);
	TRDGUniformBufferRef<FHairStrandsViewUniformParameters> BindHairStrandsViewUniformParameters(const FViewInfo& View);
	bool HasViewHairStrandsData(const FViewInfo& View);
}