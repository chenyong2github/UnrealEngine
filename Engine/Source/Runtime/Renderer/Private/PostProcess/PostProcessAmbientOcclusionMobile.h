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

	bool IsValid()
	{
		return AmbientOcclusionTexture.IsValid();
	}

	void Release()
	{
		AmbientOcclusionTexture.SafeRelease();
	}
};

extern FAmbientOcclusionMobileOutputs GAmbientOcclusionMobileOutputs;

bool IsMobileAmbientOcclusionEnabled(EShaderPlatform ShaderPlatform);
bool IsUsingMobileAmbientOcclusion(EShaderPlatform ShaderPlatform);
