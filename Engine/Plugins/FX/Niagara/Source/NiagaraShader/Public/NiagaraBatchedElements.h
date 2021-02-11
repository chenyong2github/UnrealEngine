// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "BatchedElements.h"

/**
 * Batched element parameters for gathering attributes from different slices into a single color
 */
class NIAGARASHADER_API FBatchedElementNiagara2DArrayAttribute : public FBatchedElementParameters
{
public:
	typedef TFunction<void(FRHITexture*&, FRHISamplerState*&)> FGetTextureAndSamplerDelegate;

	FBatchedElementNiagara2DArrayAttribute(const FIntVector4& InAttributeSlices, FGetTextureAndSamplerDelegate InGetTextureAndSampler = nullptr)
		: AttributeSlices(InAttributeSlices)
		, GetTextureAndSampler(InGetTextureAndSampler)
	{
	}

	/** Binds vertex and pixel shaders for this element */
	virtual void BindShaders(FRHICommandList& RHICmdList, FGraphicsPipelineStateInitializer& GraphicsPSOInit, ERHIFeatureLevel::Type InFeatureLevel, const FMatrix& InTransform, const float InGamma, const FMatrix& ColorWeights, const FTexture* Texture) override;

private:
	FIntVector4 AttributeSlices;
	FGetTextureAndSamplerDelegate GetTextureAndSampler;
};
