// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	AmbientCubemapParameters.h: Shared shader parameters for ambient cubemap
=============================================================================*/

#pragma once

#include "CoreMinimal.h"
#include "FinalPostProcessSettings.h"
#include "ShaderParameters.h"

class FShaderParameterMap;

/** Pixel shader parameters needed for deferred passes. */
class FCubemapShaderParameters
{
public:

	void Bind(const FShaderParameterMap& ParameterMap);

	void SetParameters(FRHICommandList& RHICmdList, FRHIPixelShader* ShaderRHI, const FFinalPostProcessSettings::FCubemapEntry& Entry) const;
	void SetParameters(FRHICommandList& RHICmdList, FRHIComputeShader* ShaderRHI, const FFinalPostProcessSettings::FCubemapEntry& Entry) const;

	friend FArchive& operator<<(FArchive& Ar, FCubemapShaderParameters& P);

private:
	FShaderParameter AmbientCubemapColor;
	FShaderParameter AmbientCubemapMipAdjust;
	FShaderResourceParameter AmbientCubemap;
	FShaderResourceParameter AmbientCubemapSampler;

	template<typename TRHIShader>
	void SetParametersTemplate(FRHICommandList& RHICmdList, TRHIShader* ShaderRHI, const FFinalPostProcessSettings::FCubemapEntry& Entry) const;
};

