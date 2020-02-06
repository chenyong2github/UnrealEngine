// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	AmbientCubemapParameters.h: Shared shader parameters for ambient cubemap
=============================================================================*/

#pragma once

#include "CoreMinimal.h"
#include "FinalPostProcessSettings.h"
#include "ShaderParameters.h"

class FShaderParameterMap;

// DEPRECATED: use FAmbientCubemapParameters instead.
class FCubemapShaderParameters
{
	DECLARE_TYPE_LAYOUT(FCubemapShaderParameters, NonVirtual);
public:

	void Bind(const FShaderParameterMap& ParameterMap);

	void SetParameters(FRHICommandList& RHICmdList, FRHIPixelShader* ShaderRHI, const FFinalPostProcessSettings::FCubemapEntry& Entry) const;
	void SetParameters(FRHICommandList& RHICmdList, FRHIComputeShader* ShaderRHI, const FFinalPostProcessSettings::FCubemapEntry& Entry) const;

	friend FArchive& operator<<(FArchive& Ar, FCubemapShaderParameters& P);

private:
	template<typename TRHIShader>
	void SetParametersTemplate(FRHICommandList& RHICmdList, TRHIShader* ShaderRHI, const FFinalPostProcessSettings::FCubemapEntry& Entry) const;

	
		LAYOUT_FIELD(FShaderParameter, AmbientCubemapColor)
		LAYOUT_FIELD(FShaderParameter, AmbientCubemapMipAdjust)
		LAYOUT_FIELD(FShaderResourceParameter, AmbientCubemap)
		LAYOUT_FIELD(FShaderResourceParameter, AmbientCubemapSampler)
	
};

/** Shader parameters needed for deferred passes sampling the ambient cube map. */
BEGIN_SHADER_PARAMETER_STRUCT(FAmbientCubemapParameters, )
	SHADER_PARAMETER(FLinearColor, AmbientCubemapColor)
	SHADER_PARAMETER(FVector4, AmbientCubemapMipAdjust)
	SHADER_PARAMETER_TEXTURE(TextureCube, AmbientCubemap)
	SHADER_PARAMETER_SAMPLER(SamplerState, AmbientCubemapSampler)
END_SHADER_PARAMETER_STRUCT()

void SetupAmbientCubemapParameters(const FFinalPostProcessSettings::FCubemapEntry& Entry, FAmbientCubemapParameters* OutParameters);
