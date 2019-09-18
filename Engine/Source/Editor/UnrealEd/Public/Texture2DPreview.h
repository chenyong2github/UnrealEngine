// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

/*==============================================================================
	Texture2DPreview.h: Definitions for previewing 2d textures.
==============================================================================*/

#pragma once

#include "CoreMinimal.h"
#include "BatchedElements.h"

/**
 * Batched element parameters for previewing 2d textures.
 */
class UNREALED_API FBatchedElementTexture2DPreviewParameters : public FBatchedElementParameters
{
public:
	FBatchedElementTexture2DPreviewParameters(float InMipLevel, float InLayerIndex, bool bInIsNormalMap, bool bInIsSingleChannel, bool bInIsSingleVTPhysicalSpace, bool bInIsVirtualTexture, bool bInIsTextureArray)
		: MipLevel(InMipLevel)
		, LayerIndex(InLayerIndex)
		, bIsNormalMap( bInIsNormalMap )
		, bIsSingleChannelFormat( bInIsSingleChannel )
		, bIsSingleVTPhysicalSpace(bInIsSingleVTPhysicalSpace)
		, bIsVirtualTexture(bInIsVirtualTexture)
		, bIsTextureArray(bInIsTextureArray)
	{
	}

	/** Binds vertex and pixel shaders for this element */
	virtual void BindShaders(FRHICommandList& RHICmdList, FGraphicsPipelineStateInitializer& GraphicsPSOInit, ERHIFeatureLevel::Type InFeatureLevel, const FMatrix& InTransform, const float InGamma, const FMatrix& ColorWeights, const FTexture* Texture) override;

private:

	/** Parameters that need to be passed to the shader */
	float MipLevel;
	float LayerIndex;
	bool bIsNormalMap;
	bool bIsSingleChannelFormat;
	bool bIsSingleVTPhysicalSpace;

	/** Parameters that are used to select a shader permutation */
	bool bIsVirtualTexture;
	bool bIsTextureArray;
};
