// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	LumenTranslucencyVolumeLighting.h
=============================================================================*/

#pragma once

#include "RHIDefinitions.h"
#include "RendererInterface.h"
#include "ShaderParameterMacros.h"

class FLumenTranslucencyGIVolume
{
public:

	FLumenTranslucencyGIVolume()
	{
		GridZParams = FVector::ZeroVector;
		GridPixelSizeShift = 0;
		GridSize = FIntVector::ZeroValue;
	}

	TRefCountPtr<IPooledRenderTarget> Texture0;
	TRefCountPtr<IPooledRenderTarget> Texture1;
	TRefCountPtr<IPooledRenderTarget> HistoryTexture0;
	TRefCountPtr<IPooledRenderTarget> HistoryTexture1;
	FVector GridZParams;
	uint32 GridPixelSizeShift;
	FIntVector GridSize;
};

BEGIN_SHADER_PARAMETER_STRUCT(FLumenTranslucencyLightingParameters, )
	SHADER_PARAMETER_TEXTURE(Texture3D, TranslucencyGIVolume0)
	SHADER_PARAMETER_TEXTURE(Texture3D, TranslucencyGIVolume1)
	SHADER_PARAMETER_TEXTURE(Texture3D, TranslucencyGIVolumeHistory0)
	SHADER_PARAMETER_TEXTURE(Texture3D, TranslucencyGIVolumeHistory1)
	SHADER_PARAMETER_SAMPLER(SamplerState, TranslucencyGIVolumeSampler)
	SHADER_PARAMETER(FVector, TranslucencyGIGridZParams)
	SHADER_PARAMETER(uint32, TranslucencyGIGridPixelSizeShift)
	SHADER_PARAMETER(FIntVector, TranslucencyGIGridSize)
END_SHADER_PARAMETER_STRUCT()

extern FLumenTranslucencyLightingParameters GetLumenTranslucencyLightingParameters(const FLumenTranslucencyGIVolume& LumenTranslucencyGIVolume);