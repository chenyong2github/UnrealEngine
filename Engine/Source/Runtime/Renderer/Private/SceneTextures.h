// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "SceneView.h"
#include "RenderGraphUtils.h"
#include "CustomDepthRendering.h"
#include "SceneRenderTargetParameters.h"
#include "GBufferInfo.h"

struct FSceneTextures;
class FViewInfo;
class FViewFamilyInfo;

enum class ESceneTextureExtracts : uint32
{
	/** No textures are extracted from the render graph after execution. */
	None = 0,

	/** Extracts scene depth after execution */
	Depth = 1 << 0,

	/** Extracts custom depth after execution. */
	CustomDepth = 1 << 1,

	/** Extracts all available textures after execution. */
	All = Depth | CustomDepth
};

/** Struct containing the scene texture configuration used to create scene textures.  Use InitializeViewFamily to initialize the
 *  SceneTexturesConfig structure in the FViewFamilyInfo.  A global singleton instance is maintained manually with static Set / Get
 *  functions, but will soon be deprecated, in preference of using the structure from the FViewFamilyInfo.
 */
struct RENDERER_API FSceneTexturesConfig
{
	// Sets the persistent global config instance.
	static void Set(const FSceneTexturesConfig& Config);

	// Gets the persistent global config instance. If unset, will return a default constructed instance.
	static const FSceneTexturesConfig& Get();

	// Initializes the SceneTexturesConfig in the target view family.
	static void InitializeViewFamily(FViewFamilyInfo& ViewFamily);

	FORCEINLINE bool IsValid() const
	{
		return ShadingPath != EShadingPath::Num;
	}

	FSceneTexturesConfig()
		: bRequireMultiView{}
		, bIsUsingGBuffers{}
		, bKeepDepthContent{1}
		, bPreciseDepthAux{}
		, bSamplesCustomDepthAndStencil{}
	{}

	// Extractions to queue for after execution of the render graph.
	ESceneTextureExtracts Extracts = ESceneTextureExtracts::All;

	// Enums describing the shading / feature / platform configurations used to construct the config.
	EShadingPath ShadingPath = EShadingPath::Num;
	ERHIFeatureLevel::Type FeatureLevel = ERHIFeatureLevel::SM5;
	EShaderPlatform ShaderPlatform = SP_PCD3D_SM5;

	// Extent of all full-resolution textures.
	FIntPoint Extent = FIntPoint::ZeroValue;

	// Extend of the mobile Pixel Projected Reflection texture
	FIntPoint MobilePixelProjectedReflectionExtent = FIntPoint::ZeroValue;

	// Downsample factors to divide against the full resolution texture extent.
	uint32 SmallDepthDownsampleFactor = 2;

	// Number of MSAA samples used by color and depth targets.
	uint32 NumSamples = 1;

	// Number of MSAA sampled used by the editor primitive composition targets.
	uint32 EditorPrimitiveNumSamples = 1;

	// Pixel format to use when creating scene color.
	EPixelFormat ColorFormat = PF_Unknown;

	// Optimized clear values to use for color / depth textures.
	FClearValueBinding ColorClearValue = FClearValueBinding::Black;
	FClearValueBinding DepthClearValue = FClearValueBinding::DepthFar;

	// (Deferred Shading) Dynamic GBuffer configuration used to control allocation and slotting of base pass textures.
	FGBufferParams GBufferParams;
	FGBufferBinding GBufferA;
	FGBufferBinding GBufferB;
	FGBufferBinding GBufferC;
	FGBufferBinding GBufferD;
	FGBufferBinding GBufferE;
	FGBufferBinding GBufferVelocity;

	// (VR) True if scene color and depth should be multi-view allocated.
	uint32 bRequireMultiView : 1;

	// True if platform is using GBuffers.
	uint32 bIsUsingGBuffers : 1;

	// (Mobile) True if the platform should write depth content back to memory.
	uint32 bKeepDepthContent : 1;
	
	// (Mobile) True if SceneDepthAux should use a precise pixel format
	uint32 bPreciseDepthAux : 1;

	// (Mobile) True if both CustomDepth and CustomStencil are sampled in a shader
	uint32 bSamplesCustomDepthAndStencil : 1;

private:
	static FSceneTexturesConfig GlobalInstance;
};

/** RDG struct containing the minimal set of scene textures common across all rendering configurations. */
struct RENDERER_API FMinimalSceneTextures
{
	// Initializes the minimal scene textures structure in the FViewFamilyInfo
	static void InitializeViewFamily(FRDGBuilder& GraphBuilder, FViewFamilyInfo& ViewFamily);

	// Immutable copy of the config used to create scene textures.
	FSceneTexturesConfig Config;

	// Uniform buffers for deferred or mobile.
	TRDGUniformBufferRef<FSceneTextureUniformParameters> UniformBuffer{};
	TRDGUniformBufferRef<FMobileSceneTextureUniformParameters> MobileUniformBuffer{};

	// Setup modes used when creating uniform buffers. These are updated on demand.
	ESceneTextureSetupMode SetupMode = ESceneTextureSetupMode::None;
	EMobileSceneTextureSetupMode MobileSetupMode = EMobileSceneTextureSetupMode::None;

	// Texture containing scene color information with lighting but without post processing. Will be two textures if MSAA.
	FRDGTextureMSAA Color{};

	// Texture containing scene depth. Will be two textures if MSAA.
	FRDGTextureMSAA Depth{};

	// Texture containing a stencil view of the resolved (if MSAA) scene depth. 
	FRDGTextureSRVRef Stencil{};

	// Textures containing depth / stencil information from the custom depth pass.
	FCustomDepthTextures CustomDepth{};

	FSceneTextureShaderParameters GetSceneTextureShaderParameters(ERHIFeatureLevel::Type FeatureLevel) const;
};

/** RDG struct containing the complete set of scene textures for the deferred or mobile renderers. */
struct RENDERER_API FSceneTextures : public FMinimalSceneTextures
{
	// Initializes the scene textures structure in the FViewFamilyInfo
	static void InitializeViewFamily(FRDGBuilder& GraphBuilder, FViewFamilyInfo& ViewFamily);

	// Configures an array of render targets for the GBuffer pass.
	uint32 GetGBufferRenderTargets(TStaticArray<FTextureRenderTargetBinding, MaxSimultaneousRenderTargets>& RenderTargets) const;
	uint32 GetGBufferRenderTargets(ERenderTargetLoadAction LoadAction, FRenderTargetBindingSlots& RenderTargets) const;

	// (Deferred) Texture containing conservative downsampled depth for occlusion.
	FRDGTextureRef SmallDepth{};

	// (Deferred) Textures containing geometry information for deferred shading.
	FRDGTextureRef GBufferA{};
	FRDGTextureRef GBufferB{};
	FRDGTextureRef GBufferC{};
	FRDGTextureRef GBufferD{};
	FRDGTextureRef GBufferE{};
	FRDGTextureRef GBufferF{};

	// Additional Buffer texture used by mobile
	FRDGTextureMSAA DepthAux{};

	// Texture containing dynamic motion vectors. Can be bound by the base pass or its own velocity pass.
	FRDGTextureRef Velocity{};

	// Texture containing the screen space ambient occlusion result.
	FRDGTextureRef ScreenSpaceAO{};

	// Texture used by the quad overdraw debug view mode when enabled.
	FRDGTextureRef QuadOverdraw{};

	// (Mobile) Texture used by mobile PPR in the next frame.
	FRDGTextureRef PixelProjectedReflection{};

	// Textures used to composite editor primitives. Also used by the base pass when in wireframe mode.
#if WITH_EDITOR
	FRDGTextureRef EditorPrimitiveColor{};
	FRDGTextureRef EditorPrimitiveDepth{};
#endif
};

/** Extracts scene textures into the global extraction instance. */
void QueueSceneTextureExtractions(FRDGBuilder& GraphBuilder, const FSceneTextures& SceneTextures);

/** Utility functions for accessing common global scene texture configuration state. Reads a bit less awkwardly than the singleton access. */

UE_DEPRECATED(5.1, "Single pass multiple view family rendering makes this obsolete.  Use ViewFamily.SceneTexturesConfig.NumSamples instead.")
inline uint32 GetSceneTextureNumSamples()
{
	return FSceneTexturesConfig::Get().NumSamples;
}

UE_DEPRECATED(5.1, "Single pass multiple view family rendering makes this obsolete.  Use ViewFamily.SceneTexturesConfig.EditorPrimitiveNumSamples instead.")
inline uint32 GetEditorPrimitiveNumSamples()
{
	return FSceneTexturesConfig::Get().EditorPrimitiveNumSamples;
}

UE_DEPRECATED(5.1, "Single pass multiple view family rendering makes this obsolete.  Use ViewFamily.SceneTexturesConfig.DepthClearValue instead.")
inline FClearValueBinding GetSceneDepthClearValue()
{
	return FSceneTexturesConfig::Get().DepthClearValue;
}

UE_DEPRECATED(5.1, "Single pass multiple view family rendering makes this obsolete.  Use ViewFamily.SceneTexturesConfig.ColorClearValue instead.")
inline FClearValueBinding GetSceneColorClearValue()
{
	return FSceneTexturesConfig::Get().ColorClearValue;
}

UE_DEPRECATED(5.1, "Single pass multiple view family rendering makes this obsolete.  Use ViewFamily.SceneTexturesConfig.ColorFormat instead.")
inline EPixelFormat GetSceneColorFormat()
{
	return FSceneTexturesConfig::Get().ColorFormat;
}
