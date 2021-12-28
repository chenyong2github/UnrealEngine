// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	PostProcessAmbientOcclusionMobile.h
=============================================================================*/

#pragma once
#include "CoreMinimal.h"
#include "RendererInterface.h"

struct FAmbientOcclusionMobileOutputs
{
	TRefCountPtr<IPooledRenderTarget> AmbientOcclusionTexture;
	TRefCountPtr<IPooledRenderTarget> IntermediateBlurTexture;

	bool IsValid()
	{
		return AmbientOcclusionTexture.IsValid() && IntermediateBlurTexture.IsValid();
	}

	void Release()
	{
		AmbientOcclusionTexture.SafeRelease();
		IntermediateBlurTexture.SafeRelease();
	}
};

extern FAmbientOcclusionMobileOutputs GAmbientOcclusionMobileOutputs;