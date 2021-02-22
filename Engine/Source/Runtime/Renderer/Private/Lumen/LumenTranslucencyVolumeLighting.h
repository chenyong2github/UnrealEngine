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
	FRDGTextureRef Texture0        = nullptr;
	FRDGTextureRef Texture1        = nullptr;
	FRDGTextureRef HistoryTexture0 = nullptr;
	FRDGTextureRef HistoryTexture1 = nullptr;
	FVector GridZParams            = FVector::ZeroVector;
	uint32 GridPixelSizeShift      = 0;
	FIntVector GridSize            = FIntVector::ZeroValue;
};

BEGIN_SHADER_PARAMETER_STRUCT(FLumenTranslucencyLightingParameters, )
	SHADER_PARAMETER_RDG_TEXTURE(Texture3D, TranslucencyGIVolume0)
	SHADER_PARAMETER_RDG_TEXTURE(Texture3D, TranslucencyGIVolume1)
	SHADER_PARAMETER_RDG_TEXTURE(Texture3D, TranslucencyGIVolumeHistory0)
	SHADER_PARAMETER_RDG_TEXTURE(Texture3D, TranslucencyGIVolumeHistory1)
	SHADER_PARAMETER_SAMPLER(SamplerState, TranslucencyGIVolumeSampler)
	SHADER_PARAMETER(FVector, TranslucencyGIGridZParams)
	SHADER_PARAMETER(uint32, TranslucencyGIGridPixelSizeShift)
	SHADER_PARAMETER(FIntVector, TranslucencyGIGridSize)
END_SHADER_PARAMETER_STRUCT()

extern FLumenTranslucencyLightingParameters GetLumenTranslucencyLightingParameters(FRDGBuilder& GraphBuilder, const FLumenTranslucencyGIVolume& LumenTranslucencyGIVolume);