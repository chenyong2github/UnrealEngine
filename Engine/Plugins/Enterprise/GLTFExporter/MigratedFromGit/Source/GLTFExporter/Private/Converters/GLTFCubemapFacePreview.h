// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "BatchedElements.h"

class FGLTFCubemapFacePreview : public FBatchedElementParameters
{

public:

	FGLTFCubemapFacePreview(FRHITexture* TextureCube, int32 CubeFaceIndex)
		: TextureCube(TextureCube)
		, CubeFaceIndex(CubeFaceIndex)
	{
	}

	/** Binds vertex and pixel shaders for this element */
	virtual void BindShaders(
		FRHICommandList& RHICmdList,
		FGraphicsPipelineStateInitializer& GraphicsPSOInit,
		ERHIFeatureLevel::Type InFeatureLevel,
		const FMatrix& InTransform,
		const float InGamma,
		const FMatrix& ColorWeights,
		const FTexture* Texture) override;

private:

	FRHITexture* TextureCube;
	int32 CubeFaceIndex;
};
