// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	FogRendering.h: 
=============================================================================*/

#pragma once

#include "CoreMinimal.h"
#include "ShaderParameters.h"
#include "SceneRendering.h"
#include "VolumetricFog.h"

BEGIN_GLOBAL_SHADER_PARAMETER_STRUCT(FFogUniformParameters,)
	SHADER_PARAMETER(FVector4, ExponentialFogParameters)
	SHADER_PARAMETER(FVector4, ExponentialFogParameters2)
	SHADER_PARAMETER(FVector4, ExponentialFogColorParameter)
	SHADER_PARAMETER(FVector4, ExponentialFogParameters3)
	SHADER_PARAMETER(FVector4, InscatteringLightDirection) // non negative DirectionalInscatteringStartDistance in .W
	SHADER_PARAMETER(FVector4, DirectionalInscatteringColor)
	SHADER_PARAMETER(FVector2D, SinCosInscatteringColorCubemapRotation)
	SHADER_PARAMETER(FVector, FogInscatteringTextureParameters)
	SHADER_PARAMETER(float, ApplyVolumetricFog)
	SHADER_PARAMETER_TEXTURE(TextureCube, FogInscatteringColorCubemap)
	SHADER_PARAMETER_SAMPLER(SamplerState, FogInscatteringColorSampler)
	SHADER_PARAMETER_RDG_TEXTURE(Texture3D, IntegratedLightScattering)
	SHADER_PARAMETER_SAMPLER(SamplerState, IntegratedLightScatteringSampler)
END_GLOBAL_SHADER_PARAMETER_STRUCT()

extern void SetupFogUniformParameters(FRDGBuilder& GraphBuilder, const FViewInfo& View, FFogUniformParameters& OutParameters);
TRDGUniformBufferRef<FFogUniformParameters> CreateFogUniformBuffer(FRDGBuilder& GraphBuilder, const FViewInfo& View);

extern bool ShouldRenderFog(const FSceneViewFamily& Family);
