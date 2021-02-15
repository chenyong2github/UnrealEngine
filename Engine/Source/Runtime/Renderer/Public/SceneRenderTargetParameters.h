// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "RenderGraph.h"

class FSceneView;

/** A uniform buffer containing common scene textures used by materials or global shaders. */
BEGIN_GLOBAL_SHADER_PARAMETER_STRUCT(FSceneTextureUniformParameters, RENDERER_API)
	// Scene Color / Depth
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SceneColorTexture)
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SceneDepthTexture)

	// GBuffer
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D, GBufferATexture)
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D, GBufferBTexture)
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D, GBufferCTexture)
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D, GBufferDTexture)
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D, GBufferETexture)
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D, GBufferFTexture)
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D, GBufferVelocityTexture)

	// SSAO
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D, ScreenSpaceAOTexture)

	// Custom Depth / Stencil
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D, CustomDepthTexture)
	SHADER_PARAMETER_SRV(Texture2D<uint2>, CustomStencilTexture)

	// Misc
	SHADER_PARAMETER_SAMPLER(SamplerState, PointClampSampler)
END_GLOBAL_SHADER_PARAMETER_STRUCT()

enum class ESceneTextureSetupMode : uint32
{
	None			= 0,
	SceneColor		= 1 << 0,
	SceneDepth		= 1 << 1,
	SceneVelocity	= 1 << 2,
	GBufferA		= 1 << 3,
	GBufferB		= 1 << 4,
	GBufferC		= 1 << 5,
	GBufferD		= 1 << 6,
	GBufferE		= 1 << 7,
	GBufferF		= 1 << 8,
	SSAO			= 1 << 9,
	CustomDepth		= 1 << 10,
	GBuffers		= GBufferA | GBufferB | GBufferC | GBufferD | GBufferE | GBufferF,
	All				= SceneColor | SceneDepth | SceneVelocity | GBuffers | SSAO | CustomDepth
};
ENUM_CLASS_FLAGS(ESceneTextureSetupMode);

/** Fills the shader parameter struct. */
extern RENDERER_API void SetupSceneTextureUniformParameters(
	const FSceneRenderTargets& SceneContext,
	ERHIFeatureLevel::Type FeatureLevel,
	ESceneTextureSetupMode SetupMode,
	FSceneTextureUniformParameters& OutParameters);

extern RENDERER_API void SetupSceneTextureUniformParameters(
	FRDGBuilder& GraphBuilder,
	ERHIFeatureLevel::Type FeatureLevel,
	ESceneTextureSetupMode SetupMode,
	FSceneTextureUniformParameters& OutParameters);

/** Returns RHI scene texture uniform buffer with passthrough RDG resources. */
extern RENDERER_API TUniformBufferRef<FSceneTextureUniformParameters> CreateSceneTextureUniformBuffer(
	FRHIComputeCommandList& RHICmdList,
	ERHIFeatureLevel::Type FeatureLevel,
	ESceneTextureSetupMode SetupMode = ESceneTextureSetupMode::All);

/** Returns RDG scene texture uniform buffer. */
extern RENDERER_API TRDGUniformBufferRef<FSceneTextureUniformParameters> CreateSceneTextureUniformBuffer(
	FRDGBuilder& GraphBuilder,
	ERHIFeatureLevel::Type FeatureLevel,
	ESceneTextureSetupMode SetupMode = ESceneTextureSetupMode::All);

BEGIN_GLOBAL_SHADER_PARAMETER_STRUCT(FMobileSceneTextureUniformParameters, RENDERER_API)
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SceneColorTexture)
	SHADER_PARAMETER_SAMPLER(SamplerState, SceneColorTextureSampler)
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SceneDepthTexture)
	SHADER_PARAMETER_SAMPLER(SamplerState, SceneDepthTextureSampler)
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D, CustomDepthTexture)
	SHADER_PARAMETER_SAMPLER(SamplerState, CustomDepthTextureSampler)
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D, MobileCustomStencilTexture)
	SHADER_PARAMETER_SAMPLER(SamplerState, MobileCustomStencilTextureSampler)
	SHADER_PARAMETER_UAV(RWBuffer<uint>, VirtualTextureFeedbackUAV)
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SceneVelocityTexture)
	SHADER_PARAMETER_SAMPLER(SamplerState, SceneVelocityTextureSampler)
	// GBuffer
	SHADER_PARAMETER_TEXTURE(Texture2D, GBufferATexture)
	SHADER_PARAMETER_TEXTURE(Texture2D, GBufferBTexture)
	SHADER_PARAMETER_TEXTURE(Texture2D, GBufferCTexture)
	SHADER_PARAMETER_TEXTURE(Texture2D, GBufferDTexture)
	SHADER_PARAMETER_TEXTURE(Texture2D, SceneDepthAuxTexture)
	SHADER_PARAMETER_SAMPLER(SamplerState, GBufferATextureSampler)
	SHADER_PARAMETER_SAMPLER(SamplerState, GBufferBTextureSampler)
	SHADER_PARAMETER_SAMPLER(SamplerState, GBufferCTextureSampler)
	SHADER_PARAMETER_SAMPLER(SamplerState, GBufferDTextureSampler)
	SHADER_PARAMETER_SAMPLER(SamplerState, SceneDepthAuxTextureSampler)
END_GLOBAL_SHADER_PARAMETER_STRUCT()

enum class EMobileSceneTextureSetupMode : uint32
{
	None			= 0,
	SceneColor		= 1 << 0,
	CustomDepth		= 1 << 1,
	SceneVelocity	= 1 << 2,
	All = SceneColor | CustomDepth | SceneVelocity
};
ENUM_CLASS_FLAGS(EMobileSceneTextureSetupMode);

/** Fills the scene texture uniform buffer struct. */
extern RENDERER_API void SetupMobileSceneTextureUniformParameters(
	const FSceneRenderTargets& SceneContext,
	EMobileSceneTextureSetupMode SetupMode,
	FMobileSceneTextureUniformParameters& SceneTextureParameters);

extern RENDERER_API void SetupMobileSceneTextureUniformParameters(
	FRDGBuilder& GraphBuilder,
	EMobileSceneTextureSetupMode SetupMode,
	FMobileSceneTextureUniformParameters& SceneTextureParameters);

/** Creates the RHI mobile scene texture uniform buffer with passthrough RDG resources. */
extern RENDERER_API TUniformBufferRef<FMobileSceneTextureUniformParameters> CreateMobileSceneTextureUniformBuffer(
	FRHIComputeCommandList& RHICmdList,
	EMobileSceneTextureSetupMode SetupMode = EMobileSceneTextureSetupMode::All);

/** Creates the RDG mobile scene texture uniform buffer. */
extern RENDERER_API TRDGUniformBufferRef<FMobileSceneTextureUniformParameters> CreateMobileSceneTextureUniformBuffer(
	FRDGBuilder& GraphBuilder,
	EMobileSceneTextureSetupMode SetupMode = EMobileSceneTextureSetupMode::All);

BEGIN_SHADER_PARAMETER_STRUCT(FSceneTextureShaderParameters, RENDERER_API)
	SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FSceneTextureUniformParameters, SceneTextures)
	SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FMobileSceneTextureUniformParameters, MobileSceneTextures)
END_SHADER_PARAMETER_STRUCT()

FORCEINLINE FSceneTextureShaderParameters GetSceneTextureShaderParameters(TRDGUniformBufferRef<FSceneTextureUniformParameters> UniformBuffer)
{
	FSceneTextureShaderParameters Parameters;
	Parameters.SceneTextures = UniformBuffer;
	return Parameters;
}

FORCEINLINE FSceneTextureShaderParameters GetSceneTextureShaderParameters(TRDGUniformBufferRef<FMobileSceneTextureUniformParameters> UniformBuffer)
{
	FSceneTextureShaderParameters Parameters;
	Parameters.MobileSceneTextures = UniformBuffer;
	return Parameters;
}

/** Returns scene texture shader parameters containing the RDG uniform buffer for either mobile or deferred shading. */
extern RENDERER_API FSceneTextureShaderParameters CreateSceneTextureShaderParameters(
	FRDGBuilder& GraphBuilder,
	ERHIFeatureLevel::Type FeatureLevel,
	ESceneTextureSetupMode SetupMode = ESceneTextureSetupMode::All);

/** Returns appropriate scene textures RHI uniform buffer for deferred or mobile path. */
extern RENDERER_API TRefCountPtr<FRHIUniformBuffer> CreateSceneTextureUniformBufferDependentOnShadingPath(
	FRHIComputeCommandList& RHICmdList,
	ERHIFeatureLevel::Type FeatureLevel,
	ESceneTextureSetupMode SetupMode = ESceneTextureSetupMode::All);


extern RENDERER_API bool IsSceneTexturesValid(FRHICommandListImmediate& RHICmdList);


/** Deprecated APIs. */

/** This was renamed to remove the extraneous 's' for consistency. */
UE_DEPRECATED(4.26, "FSceneTexturesUniformParameters has been renamed to FSceneTextureUniformParameters.")
typedef FSceneTextureUniformParameters FSceneTexturesUniformParameters;

UE_DEPRECATED(4.26, "BindSceneTextureUniformBufferDependentOnShadingPath has been removed.")
inline void BindSceneTextureUniformBufferDependentOnShadingPath(
	const FShader::CompiledShaderInitializerType& Initializer,
	FShaderUniformBufferParameter& SceneTexturesUniformBuffer)
{}

UE_DEPRECATED(4.26, "SetupMobileSceneTextureUniformParameters has been refactored to EMobileSceneTextureSetupMode.")
extern void SetupMobileSceneTextureUniformParameters(
	FSceneRenderTargets& SceneContext,
	ERHIFeatureLevel::Type FeatureLevel,
	bool bSceneTexturesValid,
	bool bCustomDepthIsValid,
	FMobileSceneTextureUniformParameters& SceneTextureParameters);