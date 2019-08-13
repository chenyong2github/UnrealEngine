// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	PostProcessSubsurfaceCommon.h: Screenspace subsurface common functions
=============================================================================*/

#pragma once

#include "Engine/SubsurfaceProfile.h"
#include "ScreenPass.h"

enum class ESubsurfaceMode : uint32
{
	// Performs a full resolution scattering filter.
	FullRes,

	// Performs a half resolution scattering filter.
	HalfRes,

	// Reconstructs lighting, but does not perform scattering.
	Bypass,

	MAX
};

// Returns the [0, N] clamped value of the 'r.SSS.Scale' CVar.
float GetSubsurfaceRadiusScale();

int32 GetSSSFilter();
int32 GetSSSSampleSet();
int32 GetSSSQuality();

// Returns the SS profile texture with a black fallback texture if none exists yet.
// Actually we do not need this for the burley normalized SSS.
FRHITexture* GetSubsurfaceProfileTexture(FRHICommandListImmediate& RHICmdList);

// Returns the current subsurface mode required by the current view.
ESubsurfaceMode GetSubsurfaceModeForView(const FViewInfo& View);

// Set of common shader parameters shared by all subsurface shaders.
BEGIN_SHADER_PARAMETER_STRUCT(FSubsurfaceParameters, )
	SHADER_PARAMETER(FVector4, SubsurfaceParams)
	SHADER_PARAMETER_STRUCT_REF(FSceneTexturesUniformParameters, SceneUniformBuffer)
	SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, ViewUniformBuffer)
	SHADER_PARAMETER_SAMPLER(SamplerState, BilinearTextureSampler)
	SHADER_PARAMETER_TEXTURE(Texture2D, SSProfilesTexture)
END_SHADER_PARAMETER_STRUCT()

FSubsurfaceParameters GetSubsurfaceCommonParameters(FRHICommandListImmediate& RHICmdList, const FViewInfo& View);

// Base class for a subsurface shader.
class FSubsurfaceShader : public FGlobalShader
{
public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("SUBSURFACE_RADIUS_SCALE"), SUBSURFACE_RADIUS_SCALE);
		OutEnvironment.SetDefine(TEXT("SUBSURFACE_KERNEL_SIZE"), SUBSURFACE_KERNEL_SIZE);
	}

	FSubsurfaceShader() = default;
	FSubsurfaceShader(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{}
};

// A shader parameter struct for a single subsurface input texture.
BEGIN_SHADER_PARAMETER_STRUCT(FSubsurfaceInput, )
	SHADER_PARAMETER_STRUCT_INCLUDE(FScreenPassTextureViewportParameters, Viewport)
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D, Texture)
END_SHADER_PARAMETER_STRUCT()

FSubsurfaceInput GetSubsurfaceInput(FRDGTextureRef Texture, const FScreenPassTextureViewportParameters& ViewportParameters);
