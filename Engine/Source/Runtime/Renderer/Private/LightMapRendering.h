// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	LightMapRendering.h: Light map rendering definitions.
=============================================================================*/

#pragma once

#include "CoreMinimal.h"
#include "HAL/IConsoleManager.h"
#include "RenderResource.h"
#include "UniformBuffer.h"
#include "ShaderParameters.h"
#include "ShadowRendering.h"
#include "LightMap.h"

class FPrimitiveSceneProxy;

extern bool GVisualizeMipLevels;

BEGIN_GLOBAL_SHADER_PARAMETER_STRUCT(FIndirectLightingCacheUniformParameters, )
	SHADER_PARAMETER(FVector, IndirectLightingCachePrimitiveAdd) // FCachedVolumeIndirectLightingPolicy
	SHADER_PARAMETER(FVector, IndirectLightingCachePrimitiveScale) // FCachedVolumeIndirectLightingPolicy
	SHADER_PARAMETER(FVector, IndirectLightingCacheMinUV) // FCachedVolumeIndirectLightingPolicy
	SHADER_PARAMETER(FVector, IndirectLightingCacheMaxUV) // FCachedVolumeIndirectLightingPolicy
	SHADER_PARAMETER(FVector4, PointSkyBentNormal) // FCachedPointIndirectLightingPolicy
	SHADER_PARAMETER_EX(float, DirectionalLightShadowing, EShaderPrecisionModifier::Half) // FCachedPointIndirectLightingPolicy
	SHADER_PARAMETER_ARRAY(FVector4, IndirectLightingSHCoefficients0, [3]) // FCachedPointIndirectLightingPolicy
	SHADER_PARAMETER_ARRAY(FVector4, IndirectLightingSHCoefficients1, [3]) // FCachedPointIndirectLightingPolicy
	SHADER_PARAMETER(FVector4,	IndirectLightingSHCoefficients2) // FCachedPointIndirectLightingPolicy
	SHADER_PARAMETER_EX(FVector4, IndirectLightingSHSingleCoefficient, EShaderPrecisionModifier::Half) // FCachedPointIndirectLightingPolicy used in forward Translucent
	SHADER_PARAMETER_TEXTURE(Texture3D, IndirectLightingCacheTexture0) // FCachedVolumeIndirectLightingPolicy
	SHADER_PARAMETER_TEXTURE(Texture3D, IndirectLightingCacheTexture1) // FCachedVolumeIndirectLightingPolicy
	SHADER_PARAMETER_TEXTURE(Texture3D, IndirectLightingCacheTexture2) // FCachedVolumeIndirectLightingPolicy
	SHADER_PARAMETER_SAMPLER(SamplerState, IndirectLightingCacheTextureSampler0) // FCachedVolumeIndirectLightingPolicy
	SHADER_PARAMETER_SAMPLER(SamplerState, IndirectLightingCacheTextureSampler1) // FCachedVolumeIndirectLightingPolicy
	SHADER_PARAMETER_SAMPLER(SamplerState, IndirectLightingCacheTextureSampler2) // FCachedVolumeIndirectLightingPolicy
END_GLOBAL_SHADER_PARAMETER_STRUCT()

/**
 * Default precomputed lighting data. Used for fully dynamic lightmap policies.
 */
class FEmptyPrecomputedLightingUniformBuffer : public TUniformBuffer< FPrecomputedLightingUniformParameters >
{
	typedef TUniformBuffer< FPrecomputedLightingUniformParameters > Super;
public:
	virtual void InitDynamicRHI() override;
};

/** Global uniform buffer containing the default precomputed lighting data. */
extern TGlobalResource< FEmptyPrecomputedLightingUniformBuffer > GEmptyPrecomputedLightingUniformBuffer;

void GetIndirectLightingCacheParameters(
	ERHIFeatureLevel::Type FeatureLevel,
	FIndirectLightingCacheUniformParameters& Parameters,
	const class FIndirectLightingCache* LightingCache,
	const class FIndirectLightingCacheAllocation* LightingAllocation,
	FVector VolumetricLightmapLookupPosition,
	uint32 SceneFrameNumber,
	class FVolumetricLightmapSceneData* VolumetricLightmapSceneData);

/**
 * Default precomputed lighting data. Used for fully dynamic lightmap policies.
 */
class FEmptyIndirectLightingCacheUniformBuffer : public TUniformBuffer< FIndirectLightingCacheUniformParameters >
{
	typedef TUniformBuffer< FIndirectLightingCacheUniformParameters > Super;
public:
	virtual void InitDynamicRHI() override;
};

/** Global uniform buffer containing the default precomputed lighting data. */
extern TGlobalResource< FEmptyIndirectLightingCacheUniformBuffer > GEmptyIndirectLightingCacheUniformBuffer;


/**
 * A policy for shaders without a light-map.
 */
struct FNoLightMapPolicy
{
	static bool ShouldCompilePermutation(const FMeshMaterialShaderPermutationParameters& Parameters)
	{
		return true;
	}

	static void ModifyCompilationEnvironment(const FMaterialShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{}

	static bool RequiresSkylight()
	{
		return false;
	}
};

enum ELightmapQuality
{
	LQ_LIGHTMAP,
	HQ_LIGHTMAP,
};

// One of these per lightmap quality
extern const TCHAR* GLightmapDefineName[2];
extern int32 GNumLightmapCoefficients[2];

/**
 * Base policy for shaders with lightmaps.
 */
template< ELightmapQuality LightmapQuality >
struct TLightMapPolicy
{
	static void ModifyCompilationEnvironment(const FMaterialShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		OutEnvironment.SetDefine(GLightmapDefineName[LightmapQuality],TEXT("1"));
		OutEnvironment.SetDefine(TEXT("NUM_LIGHTMAP_COEFFICIENTS"), GNumLightmapCoefficients[LightmapQuality]);

		static const auto CVar = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.VirtualTexturedLightmaps"));
		const bool VirtualTextureLightmaps = (CVar->GetValueOnAnyThread() != 0) && UseVirtualTexturing(GMaxRHIFeatureLevel, OutEnvironment.TargetPlatform);
		OutEnvironment.SetDefine(TEXT("LIGHTMAP_VT_ENABLED"), VirtualTextureLightmaps);
	}

	static bool ShouldCompilePermutation(const FMeshMaterialShaderPermutationParameters& Parameters)
	{
		static const auto AllowStaticLightingVar = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.AllowStaticLighting"));
		static const auto CVarProjectCanHaveLowQualityLightmaps = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.SupportLowQualityLightmaps"));
		static const auto CVarSupportAllShadersPermutations = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.SupportAllShaderPermutations"));
		const bool bForceAllPermutations = CVarSupportAllShadersPermutations && CVarSupportAllShadersPermutations->GetValueOnAnyThread() != 0;

		// if GEngine doesn't exist yet to have the project flag then we should be conservative and cache the LQ lightmap policy
		const bool bProjectCanHaveLowQualityLightmaps = bForceAllPermutations || (!CVarProjectCanHaveLowQualityLightmaps) || (CVarProjectCanHaveLowQualityLightmaps->GetValueOnAnyThread() != 0);

		const bool bShouldCacheQuality = (LightmapQuality != ELightmapQuality::LQ_LIGHTMAP) || bProjectCanHaveLowQualityLightmaps;

		// GetValueOnAnyThread() as it's possible that ShouldCache is called from rendering thread. That is to output some error message.
		return (Parameters.Material->GetShadingModels().IsLit())
			&& bShouldCacheQuality
			&& Parameters.VertexFactoryType->SupportsStaticLighting()
			&& (!AllowStaticLightingVar || AllowStaticLightingVar->GetValueOnAnyThread() != 0)
			&& (Parameters.Material->IsUsedWithStaticLighting() || Parameters.Material->IsSpecialEngineMaterial());
	}

	static bool RequiresSkylight()
	{
		return false;
	}
};

// A light map policy for computing up to 4 signed distance field shadow factors in the base pass.
template< ELightmapQuality LightmapQuality >
struct TDistanceFieldShadowsAndLightMapPolicy : public TLightMapPolicy< LightmapQuality >
{
	typedef TLightMapPolicy< LightmapQuality >	Super;

	static void ModifyCompilationEnvironment(const FMaterialShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		OutEnvironment.SetDefine(TEXT("STATICLIGHTING_TEXTUREMASK"), 1);
		OutEnvironment.SetDefine(TEXT("STATICLIGHTING_SIGNEDDISTANCEFIELD"), 1);
		Super::ModifyCompilationEnvironment(Parameters, OutEnvironment);
	}
};

/**
 * Policy for 'fake' texture lightmaps, such as the LightMap density rendering mode
 */
struct FDummyLightMapPolicy : public TLightMapPolicy< HQ_LIGHTMAP >
{
public:

	static bool ShouldCompilePermutation(const FMeshMaterialShaderPermutationParameters& Parameters)
	{
		return Parameters.Material->GetShadingModels().IsLit() && Parameters.VertexFactoryType->SupportsStaticLighting();
	}
};

/**
 * Policy for self shadowing translucency from a directional light
 */
class FSelfShadowedTranslucencyPolicy
{
public:

	typedef FRHIUniformBuffer* ElementDataType;

	class VertexParametersType
	{
	public:
		void Bind(const FShaderParameterMap& ParameterMap) {}
		void Serialize(FArchive& Ar) {}
	};

	class PixelParametersType
	{
	public:
		void Bind(const FShaderParameterMap& ParameterMap)
		{
			TranslucentSelfShadowBufferParameter.Bind(ParameterMap, TEXT("TranslucentSelfShadow"));
		}

		void Serialize(FArchive& Ar)
		{
			Ar << TranslucentSelfShadowBufferParameter;
		}

		FShaderUniformBufferParameter TranslucentSelfShadowBufferParameter;
	};

	static bool ShouldCompilePermutation(const FMeshMaterialShaderPermutationParameters& Parameters)
	{
		return Parameters.Material->GetShadingModels().IsLit() &&
			IsTranslucentBlendMode(Parameters.Material->GetBlendMode()) &&
			IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}

	static void ModifyCompilationEnvironment(const FMaterialShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		OutEnvironment.SetDefine(TEXT("TRANSLUCENT_SELF_SHADOWING"),TEXT("1"));
	}

	static bool RequiresSkylight()
	{
		return false;
	}

	/** Initialization constructor. */
	FSelfShadowedTranslucencyPolicy() {}
	
	static void GetVertexShaderBindings(
		const FPrimitiveSceneProxy* PrimitiveSceneProxy,
		const ElementDataType& ShaderElementData,
		const VertexParametersType* VertexShaderParameters,
		FMeshDrawSingleShaderBindings& ShaderBindings) {}

	static void GetPixelShaderBindings(
		const FPrimitiveSceneProxy* PrimitiveSceneProxy,
		const ElementDataType& ShaderElementData,
		const PixelParametersType* PixelShaderParameters,
		FMeshDrawSingleShaderBindings& ShaderBindings)
	{
		ShaderBindings.Add(PixelShaderParameters->TranslucentSelfShadowBufferParameter, ShaderElementData);
	}

	friend bool operator==(const FSelfShadowedTranslucencyPolicy A,const FSelfShadowedTranslucencyPolicy B)
	{
		return true;
	}
};

/**
 * Allows precomputed irradiance lookups at any point in space.
 */
struct FPrecomputedVolumetricLightmapLightingPolicy
{
	static bool ShouldCompilePermutation(const FMeshMaterialShaderPermutationParameters& Parameters)
	{
		static const auto AllowStaticLightingVar = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.AllowStaticLighting"));
	
		return Parameters.Material->GetShadingModels().IsLit() &&
			(!AllowStaticLightingVar || AllowStaticLightingVar->GetValueOnAnyThread() != 0);
	}

	static void ModifyCompilationEnvironment(const FMaterialShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		OutEnvironment.SetDefine(TEXT("PRECOMPUTED_IRRADIANCE_VOLUME_LIGHTING"),TEXT("1"));
	}
};

/**
 * Allows a dynamic object to access indirect lighting through a per-object allocation in a volume texture atlas
 */
struct FCachedVolumeIndirectLightingPolicy
{
	static bool ShouldCompilePermutation(const FMeshMaterialShaderPermutationParameters& Parameters)
	{
		static const auto AllowStaticLightingVar = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.AllowStaticLighting"));

		return Parameters.Material->GetShadingModels().IsLit()
			&& !IsTranslucentBlendMode(Parameters.Material->GetBlendMode())
			&& (!AllowStaticLightingVar || AllowStaticLightingVar->GetValueOnAnyThread() != 0)
			&& IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}

	static void ModifyCompilationEnvironment(const FMaterialShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		OutEnvironment.SetDefine(TEXT("CACHED_VOLUME_INDIRECT_LIGHTING"),TEXT("1"));
	}

	static bool RequiresSkylight()
	{
		return false;
	}
};


/**
 * Allows a dynamic object to access indirect lighting through a per-object lighting sample
 */
struct FCachedPointIndirectLightingPolicy
{
	static bool ShouldCompilePermutation(const FMeshMaterialShaderPermutationParameters& Parameters)
	{
		static const auto AllowStaticLightingVar = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.AllowStaticLighting"));
	
		return Parameters.Material->GetShadingModels().IsLit()
			&& (!AllowStaticLightingVar || AllowStaticLightingVar->GetValueOnAnyThread() != 0);
	}

	static void ModifyCompilationEnvironment(const FMaterialShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment);

	static bool RequiresSkylight()
	{
		return false;
	}
};

/**
 * Renders the base pass without outputting to GBuffers, used to support low end hardware where deferred shading is too expensive.
 */
struct FSimpleNoLightmapLightingPolicy : public FNoLightMapPolicy
{
	static bool ShouldCompilePermutation(const FMeshMaterialShaderPermutationParameters& Parameters)
	{
		return PlatformSupportsSimpleForwardShading(Parameters.Platform)
			&& FNoLightMapPolicy::ShouldCompilePermutation(Parameters);
	}

	static void ModifyCompilationEnvironment(const FMaterialShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		OutEnvironment.SetDefine(TEXT("SIMPLE_FORWARD_SHADING"),TEXT("1"));
		FNoLightMapPolicy::ModifyCompilationEnvironment(Parameters, OutEnvironment);
	}

	static bool RequiresSkylight()
	{
		return true;
	}
};

/**
 * Renders lightmaps without outputting to GBuffers, used to support low end hardware where deferred shading is too expensive.
 */
struct FSimpleLightmapOnlyLightingPolicy : public TLightMapPolicy<HQ_LIGHTMAP>
{
	static bool ShouldCompilePermutation(const FMeshMaterialShaderPermutationParameters& Parameters)
	{
		static const auto AllowStaticLightingVar = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.AllowStaticLighting"));

		return AllowStaticLightingVar->GetValueOnAnyThread() != 0
			&& PlatformSupportsSimpleForwardShading(Parameters.Platform)
			&& TLightMapPolicy<HQ_LIGHTMAP>::ShouldCompilePermutation(Parameters);
	}

	static void ModifyCompilationEnvironment(const FMaterialShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		OutEnvironment.SetDefine(TEXT("SIMPLE_FORWARD_SHADING"),TEXT("1"));
		TLightMapPolicy<HQ_LIGHTMAP>::ModifyCompilationEnvironment(Parameters, OutEnvironment);
	}

	static bool RequiresSkylight()
	{
		return true;
	}
};

/**
 * Renders an unshadowed directional light in the base pass, used to support low end hardware where deferred shading is too expensive.
 */
struct FSimpleDirectionalLightLightingPolicy
{
	static bool ShouldCompilePermutation(const FMeshMaterialShaderPermutationParameters& Parameters)
	{
		return PlatformSupportsSimpleForwardShading(Parameters.Platform)
			&& Parameters.Material->GetShadingModels().IsLit();
	}

	static void ModifyCompilationEnvironment(const FMaterialShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		OutEnvironment.SetDefine(TEXT("SIMPLE_FORWARD_SHADING"),TEXT("1"));
		OutEnvironment.SetDefine(TEXT("SIMPLE_FORWARD_DIRECTIONAL_LIGHT"),TEXT("1"));
	}

	static bool RequiresSkylight()
	{
		return true;
	}
};

/**
 * Renders a stationary directional light in the base pass with distance field precomputed shadows without outputting to GBuffers, used to support low end hardware where deferred shading is too expensive.
 */
struct FSimpleStationaryLightPrecomputedShadowsLightingPolicy : public TDistanceFieldShadowsAndLightMapPolicy<HQ_LIGHTMAP>
{
	static bool ShouldCompilePermutation(const FMeshMaterialShaderPermutationParameters& Parameters)
	{
		static const auto AllowStaticLightingVar = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.AllowStaticLighting"));

		return AllowStaticLightingVar->GetValueOnAnyThread() != 0
			&& PlatformSupportsSimpleForwardShading(Parameters.Platform)
			&& TDistanceFieldShadowsAndLightMapPolicy<HQ_LIGHTMAP>::ShouldCompilePermutation(Parameters);
	}

	static void ModifyCompilationEnvironment(const FMaterialShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		OutEnvironment.SetDefine(TEXT("SIMPLE_FORWARD_SHADING"),TEXT("1"));
		OutEnvironment.SetDefine(TEXT("SIMPLE_FORWARD_DIRECTIONAL_LIGHT"),TEXT("1"));
		TDistanceFieldShadowsAndLightMapPolicy<HQ_LIGHTMAP>::ModifyCompilationEnvironment(Parameters, OutEnvironment);
	}

	static bool RequiresSkylight()
	{
		return true;
	}
};

/**
 * Renders a stationary directional light in the base pass with single sample shadows without outputting to GBuffers, used to support low end hardware where deferred shading is too expensive.
 */
struct FSimpleStationaryLightSingleSampleShadowsLightingPolicy : public FCachedPointIndirectLightingPolicy
{
	static bool ShouldCompilePermutation(const FMeshMaterialShaderPermutationParameters& Parameters)
	{
		static const auto AllowStaticLightingVar = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.AllowStaticLighting"));

		return AllowStaticLightingVar->GetValueOnAnyThread() != 0
			&& PlatformSupportsSimpleForwardShading(Parameters.Platform)
			&& FCachedPointIndirectLightingPolicy::ShouldCompilePermutation(Parameters);
	}

	static void ModifyCompilationEnvironment(const FMaterialShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		OutEnvironment.SetDefine(TEXT("SIMPLE_FORWARD_SHADING"),TEXT("1"));
		OutEnvironment.SetDefine(TEXT("SIMPLE_FORWARD_DIRECTIONAL_LIGHT"),TEXT("1"));
		FCachedPointIndirectLightingPolicy::ModifyCompilationEnvironment(Parameters, OutEnvironment);
	}

	static bool RequiresSkylight()
	{
		return true;
	}
};

struct FSimpleStationaryLightVolumetricLightmapShadowsLightingPolicy : public FPrecomputedVolumetricLightmapLightingPolicy
{
	static bool ShouldCompilePermutation(const FMeshMaterialShaderPermutationParameters& Parameters)
	{
		static const auto AllowStaticLightingVar = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.AllowStaticLighting"));

		return AllowStaticLightingVar->GetValueOnAnyThread() != 0
			&& PlatformSupportsSimpleForwardShading(Parameters.Platform)
			&& FPrecomputedVolumetricLightmapLightingPolicy::ShouldCompilePermutation(Parameters);
	}

	static void ModifyCompilationEnvironment(const FMaterialShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		OutEnvironment.SetDefine(TEXT("SIMPLE_FORWARD_SHADING"),TEXT("1"));
		OutEnvironment.SetDefine(TEXT("SIMPLE_FORWARD_DIRECTIONAL_LIGHT"),TEXT("1"));
		FPrecomputedVolumetricLightmapLightingPolicy::ModifyCompilationEnvironment(Parameters, OutEnvironment);
	}
};

/** Mobile Specific: Combines a distance field shadow with LQ lightmaps. */
class FMobileDistanceFieldShadowsAndLQLightMapPolicy : public TDistanceFieldShadowsAndLightMapPolicy<LQ_LIGHTMAP>
{
	typedef TDistanceFieldShadowsAndLightMapPolicy<LQ_LIGHTMAP>	Super;
public:
	static bool ShouldCompilePermutation(const FMeshMaterialShaderPermutationParameters& Parameters)
	{
		static auto* CVarMobileAllowDistanceFieldShadows = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.Mobile.AllowDistanceFieldShadows"));
		const bool bMobileAllowDistanceFieldShadows = CVarMobileAllowDistanceFieldShadows->GetValueOnAnyThread() == 1;
		return bMobileAllowDistanceFieldShadows && Super::ShouldCompilePermutation(Parameters);
	}

	static bool RequiresSkylight()
	{
		return false;
	}
};

FORCEINLINE_DEBUGGABLE int32 GetMobileMaxShadowCascades()
{
	static const auto* CVar = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.Shadow.CSM.MaxMobileCascades"));
	return CVar ? FMath::Clamp(CVar->GetValueOnAnyThread(), 0, MAX_MOBILE_SHADOWCASCADES) : MAX_MOBILE_SHADOWCASCADES;
}

/** Mobile Specific: Combines an distance field shadow with LQ lightmaps and CSM. */
class FMobileDistanceFieldShadowsLightMapAndCSMLightingPolicy : public FMobileDistanceFieldShadowsAndLQLightMapPolicy
{
	typedef FMobileDistanceFieldShadowsAndLQLightMapPolicy Super;

public:
	static bool ShouldCompilePermutation(const FMeshMaterialShaderPermutationParameters& Parameters)
	{
		static auto* CVarMobileEnableStaticAndCSMShadowReceivers = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.Mobile.EnableStaticAndCSMShadowReceivers"));
		const bool bMobileEnableStaticAndCSMShadowReceivers = CVarMobileEnableStaticAndCSMShadowReceivers->GetValueOnAnyThread() == 1;
		return bMobileEnableStaticAndCSMShadowReceivers && (Parameters.Material->GetShadingModels().IsLit()) && Super::ShouldCompilePermutation(Parameters);
	}

	static void ModifyCompilationEnvironment(const FMaterialShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		OutEnvironment.SetDefine(TEXT("DIRECTIONAL_LIGHT_CSM"), TEXT("1"));
		OutEnvironment.SetDefine(TEXT(PREPROCESSOR_TO_STRING(MAX_MOBILE_SHADOWCASCADES)), GetMobileMaxShadowCascades());

		Super::ModifyCompilationEnvironment(Parameters, OutEnvironment);
	}

	static bool RequiresSkylight()
	{
		return false;
	}
};

/** Mobile Specific: Combines an unshadowed directional light with indirect lighting from a single SH sample. */
struct FMobileDirectionalLightAndSHIndirectPolicy
{
	static bool ShouldCompilePermutation(const FMeshMaterialShaderPermutationParameters& Parameters)
	{
		static auto* CVarAllowStaticLighting = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.AllowStaticLighting"));
		const bool bAllowStaticLighting = CVarAllowStaticLighting->GetValueOnAnyThread() != 0;

		return bAllowStaticLighting && Parameters.Material->GetShadingModels().IsLit() && FCachedPointIndirectLightingPolicy::ShouldCompilePermutation(Parameters);
	}

	static void ModifyCompilationEnvironment(const FMaterialShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FCachedPointIndirectLightingPolicy::ModifyCompilationEnvironment(Parameters, OutEnvironment);
	}

	static bool RequiresSkylight()
	{
		return false;
	}
};

/** Mobile Specific: Combines a movable directional light with indirect lighting from a single SH sample. */
struct FMobileMovableDirectionalLightAndSHIndirectPolicy : public FMobileDirectionalLightAndSHIndirectPolicy
{
	static bool ShouldCompilePermutation(const FMeshMaterialShaderPermutationParameters& Parameters)
	{
		static auto* CVarMobileAllowMovableDirectionalLights = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.Mobile.AllowMovableDirectionalLights"));
		const bool bMobileAllowMovableDirectionalLights = CVarMobileAllowMovableDirectionalLights->GetValueOnAnyThread() != 0;

		return bMobileAllowMovableDirectionalLights && FMobileDirectionalLightAndSHIndirectPolicy::ShouldCompilePermutation(Parameters);
	}
	
	static void ModifyCompilationEnvironment(const FMaterialShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		OutEnvironment.SetDefine(TEXT("MOVABLE_DIRECTIONAL_LIGHT"), TEXT("1"));
		OutEnvironment.SetDefine(TEXT(PREPROCESSOR_TO_STRING(MAX_MOBILE_SHADOWCASCADES)), GetMobileMaxShadowCascades());
		FMobileDirectionalLightAndSHIndirectPolicy::ModifyCompilationEnvironment(Parameters, OutEnvironment);
	}

	static bool RequiresSkylight()
	{
		return false;
	}
};

/** Mobile Specific: Combines a movable directional light with CSM with indirect lighting from a single SH sample. */
struct FMobileMovableDirectionalLightCSMAndSHIndirectPolicy : public FMobileMovableDirectionalLightAndSHIndirectPolicy
{
	static void ModifyCompilationEnvironment(const FMaterialShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		OutEnvironment.SetDefine(TEXT("DIRECTIONAL_LIGHT_CSM"), TEXT("1"));
		OutEnvironment.SetDefine(TEXT(PREPROCESSOR_TO_STRING(MAX_MOBILE_SHADOWCASCADES)), GetMobileMaxShadowCascades());
		FMobileMovableDirectionalLightAndSHIndirectPolicy::ModifyCompilationEnvironment(Parameters, OutEnvironment);
	}

	static bool RequiresSkylight()
	{
		return false;
	}
};

/** Mobile Specific: Combines a directional light with CSM with indirect lighting from a single SH sample. */
class FMobileDirectionalLightCSMAndSHIndirectPolicy : public FMobileDirectionalLightAndSHIndirectPolicy
{
public:
	static void ModifyCompilationEnvironment(const FMaterialShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		OutEnvironment.SetDefine(TEXT("DIRECTIONAL_LIGHT_CSM"), TEXT("1"));
		OutEnvironment.SetDefine(TEXT(PREPROCESSOR_TO_STRING(MAX_MOBILE_SHADOWCASCADES)), GetMobileMaxShadowCascades());
		FMobileDirectionalLightAndSHIndirectPolicy::ModifyCompilationEnvironment(Parameters, OutEnvironment);
	}

	static bool RequiresSkylight()
	{
		return false;
	}
};

/** Mobile Specific */
struct FMobileMovableDirectionalLightLightingPolicy
{
	static bool ShouldCompilePermutation(const FMeshMaterialShaderPermutationParameters& Parameters)
	{
		static auto* CVarMobileAllowMovableDirectionalLights = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.Mobile.AllowMovableDirectionalLights"));
		const bool bMobileAllowMovableDirectionalLights = CVarMobileAllowMovableDirectionalLights->GetValueOnAnyThread() != 0;

		return bMobileAllowMovableDirectionalLights && Parameters.Material->GetShadingModels().IsLit();
	}

	static void ModifyCompilationEnvironment(const FMaterialShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		OutEnvironment.SetDefine(TEXT("MOVABLE_DIRECTIONAL_LIGHT"),TEXT("1"));
	}

	static bool RequiresSkylight()
	{
		return false;
	}
};

/** Mobile Specific */
struct FMobileMovableDirectionalLightCSMLightingPolicy
{
	static bool ShouldCompilePermutation(const FMeshMaterialShaderPermutationParameters& Parameters)
	{
		static auto* CVarMobileAllowMovableDirectionalLights = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.Mobile.AllowMovableDirectionalLights"));
		const bool bMobileAllowMovableDirectionalLights = CVarMobileAllowMovableDirectionalLights->GetValueOnAnyThread() != 0;

		return bMobileAllowMovableDirectionalLights && Parameters.Material->GetShadingModels().IsLit();
	}	

	static void ModifyCompilationEnvironment(const FMaterialShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		OutEnvironment.SetDefine(TEXT("MOVABLE_DIRECTIONAL_LIGHT"), TEXT("1"));
		OutEnvironment.SetDefine(TEXT("DIRECTIONAL_LIGHT_CSM"), TEXT("1"));
		OutEnvironment.SetDefine(TEXT(PREPROCESSOR_TO_STRING(MAX_MOBILE_SHADOWCASCADES)), GetMobileMaxShadowCascades());

		FNoLightMapPolicy::ModifyCompilationEnvironment(Parameters, OutEnvironment);
	}

	static bool RequiresSkylight()
	{
		return false;
	}
};

/** Mobile Specific */
struct FMobileMovableDirectionalLightWithLightmapPolicy : public TLightMapPolicy<LQ_LIGHTMAP>
{
	typedef TLightMapPolicy<LQ_LIGHTMAP> Super;

	static bool ShouldCompilePermutation(const FMeshMaterialShaderPermutationParameters& Parameters)
	{
		static auto* CVarMobileAllowMovableDirectionalLights = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.Mobile.AllowMovableDirectionalLights"));
		const bool bMobileAllowMovableDirectionalLights = CVarMobileAllowMovableDirectionalLights->GetValueOnAnyThread() != 0;

		static auto* CVarAllowStaticLighting = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.AllowStaticLighting"));
		const bool bAllowStaticLighting = (!CVarAllowStaticLighting || CVarAllowStaticLighting->GetValueOnAnyThread() != 0);

		return bAllowStaticLighting && bMobileAllowMovableDirectionalLights && (Parameters.Material->GetShadingModels().IsLit()) && Super::ShouldCompilePermutation(Parameters);
	}

	static void ModifyCompilationEnvironment(const FMaterialShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		OutEnvironment.SetDefine(TEXT("MOVABLE_DIRECTIONAL_LIGHT"), TEXT("1"));
		OutEnvironment.SetDefine(TEXT(PREPROCESSOR_TO_STRING(MAX_MOBILE_SHADOWCASCADES)), GetMobileMaxShadowCascades());

		Super::ModifyCompilationEnvironment(Parameters, OutEnvironment);
	}

	static bool RequiresSkylight()
	{
		return false;
	}
};

/** Mobile Specific */
struct FMobileMovableDirectionalLightCSMWithLightmapPolicy : public FMobileMovableDirectionalLightWithLightmapPolicy
{
	static void ModifyCompilationEnvironment(const FMaterialShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		OutEnvironment.SetDefine(TEXT("DIRECTIONAL_LIGHT_CSM"), TEXT("1"));

		FMobileMovableDirectionalLightWithLightmapPolicy::ModifyCompilationEnvironment(Parameters, OutEnvironment);
	}

	static bool RequiresSkylight()
	{
		return false;
	}
};

enum ELightMapPolicyType
{
	LMP_NO_LIGHTMAP,
	LMP_PRECOMPUTED_IRRADIANCE_VOLUME_INDIRECT_LIGHTING,
	LMP_CACHED_VOLUME_INDIRECT_LIGHTING,
	LMP_CACHED_POINT_INDIRECT_LIGHTING,
	LMP_SIMPLE_NO_LIGHTMAP,
	LMP_SIMPLE_LIGHTMAP_ONLY_LIGHTING,
	LMP_SIMPLE_DIRECTIONAL_LIGHT_LIGHTING,
	LMP_SIMPLE_STATIONARY_PRECOMPUTED_SHADOW_LIGHTING,
	LMP_SIMPLE_STATIONARY_SINGLESAMPLE_SHADOW_LIGHTING,
	LMP_SIMPLE_STATIONARY_VOLUMETRICLIGHTMAP_SHADOW_LIGHTING,
	LMP_LQ_LIGHTMAP,
	LMP_HQ_LIGHTMAP,
	LMP_DISTANCE_FIELD_SHADOWS_AND_HQ_LIGHTMAP,
	// Mobile specific
	LMP_MOBILE_DISTANCE_FIELD_SHADOWS_AND_LQ_LIGHTMAP,
	LMP_MOBILE_DISTANCE_FIELD_SHADOWS_LIGHTMAP_AND_CSM,
	LMP_MOBILE_DIRECTIONAL_LIGHT_AND_SH_INDIRECT,
	LMP_MOBILE_MOVABLE_DIRECTIONAL_LIGHT_AND_SH_INDIRECT,
	LMP_MOBILE_MOVABLE_DIRECTIONAL_LIGHT_CSM_AND_SH_INDIRECT,
	LMP_MOBILE_DIRECTIONAL_LIGHT_CSM_AND_SH_INDIRECT,
	LMP_MOBILE_MOVABLE_DIRECTIONAL_LIGHT_CSM,
	LMP_MOBILE_MOVABLE_DIRECTIONAL_LIGHT_WITH_LIGHTMAP,
	LMP_MOBILE_MOVABLE_DIRECTIONAL_LIGHT_CSM_WITH_LIGHTMAP,
	// LightMapDensity
	LMP_DUMMY
};

class FUniformLightMapPolicyShaderParametersType
{
public:
	void Bind(const FShaderParameterMap& ParameterMap)
	{
		PrecomputedLightingBufferParameter.Bind(ParameterMap, TEXT("PrecomputedLightingBuffer"));
		IndirectLightingCacheParameter.Bind(ParameterMap, TEXT("IndirectLightingCache"));
		LightmapResourceCluster.Bind(ParameterMap, TEXT("LightmapResourceCluster"));
	}

	void Serialize(FArchive& Ar)
	{
		Ar << PrecomputedLightingBufferParameter;
		Ar << IndirectLightingCacheParameter;
		Ar << LightmapResourceCluster;
	}

	FShaderUniformBufferParameter PrecomputedLightingBufferParameter;
	FShaderUniformBufferParameter IndirectLightingCacheParameter;
	FShaderUniformBufferParameter LightmapResourceCluster;
};

class FUniformLightMapPolicy
{
public:

	typedef const FLightCacheInterface* ElementDataType;

	typedef FUniformLightMapPolicyShaderParametersType PixelParametersType;
	typedef FUniformLightMapPolicyShaderParametersType VertexParametersType;
#if RHI_RAYTRACING
	typedef FUniformLightMapPolicyShaderParametersType RayHitGroupParametersType;
#endif

	static bool ShouldCompilePermutation(const FMeshMaterialShaderPermutationParameters& Parameters)
	{
		return false; // This one does not compile shaders since we can't tell which policy to use.
	}

	static void ModifyCompilationEnvironment(const FMaterialShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{}

	FUniformLightMapPolicy(ELightMapPolicyType InIndirectPolicy) : IndirectPolicy(InIndirectPolicy) {}

	static void GetVertexShaderBindings(
		const FPrimitiveSceneProxy* PrimitiveSceneProxy,
		const ElementDataType& ShaderElementData,
		const VertexParametersType* VertexShaderParameters,
		FMeshDrawSingleShaderBindings& ShaderBindings);

	static void GetPixelShaderBindings(
		const FPrimitiveSceneProxy* PrimitiveSceneProxy,
		const ElementDataType& ShaderElementData,
		const PixelParametersType* PixelShaderParameters,
		FMeshDrawSingleShaderBindings& ShaderBindings);

#if RHI_RAYTRACING
	void GetRayHitGroupShaderBindings(
		const FPrimitiveSceneProxy* PrimitiveSceneProxy,
		const FLightCacheInterface* ElementData,
		const RayHitGroupParametersType* RayHitGroupShaderParameters,
		FMeshDrawSingleShaderBindings& RayHitGroupBindings
	) const;
#endif // RHI_RAYTRACING

	friend bool operator==(const FUniformLightMapPolicy A,const FUniformLightMapPolicy B)
	{
		return A.IndirectPolicy == B.IndirectPolicy;
	}

	ELightMapPolicyType GetIndirectPolicy() const { return IndirectPolicy; }

private:

	ELightMapPolicyType IndirectPolicy;
};

template <ELightMapPolicyType Policy>
class TUniformLightMapPolicy : public FUniformLightMapPolicy
{
public:

	TUniformLightMapPolicy() : FUniformLightMapPolicy(Policy) {}

	static bool ShouldCompilePermutation(const FMeshMaterialShaderPermutationParameters& Parameters)
	{
		CA_SUPPRESS(6326);
		switch (Policy)
		{
		case LMP_NO_LIGHTMAP:
			return FNoLightMapPolicy::ShouldCompilePermutation(Parameters);
		case LMP_PRECOMPUTED_IRRADIANCE_VOLUME_INDIRECT_LIGHTING:
			return FPrecomputedVolumetricLightmapLightingPolicy::ShouldCompilePermutation(Parameters);
		case LMP_CACHED_VOLUME_INDIRECT_LIGHTING:
			return FCachedVolumeIndirectLightingPolicy::ShouldCompilePermutation(Parameters);
		case LMP_CACHED_POINT_INDIRECT_LIGHTING:
			return FCachedPointIndirectLightingPolicy::ShouldCompilePermutation(Parameters);
		case LMP_SIMPLE_NO_LIGHTMAP:
			return FSimpleNoLightmapLightingPolicy::ShouldCompilePermutation(Parameters);
		case LMP_SIMPLE_LIGHTMAP_ONLY_LIGHTING:
			return FSimpleLightmapOnlyLightingPolicy::ShouldCompilePermutation(Parameters);
		case LMP_SIMPLE_DIRECTIONAL_LIGHT_LIGHTING:
			return FSimpleDirectionalLightLightingPolicy::ShouldCompilePermutation(Parameters);
		case LMP_SIMPLE_STATIONARY_PRECOMPUTED_SHADOW_LIGHTING:
			return FSimpleStationaryLightPrecomputedShadowsLightingPolicy::ShouldCompilePermutation(Parameters);
		case LMP_SIMPLE_STATIONARY_SINGLESAMPLE_SHADOW_LIGHTING:
			return FSimpleStationaryLightSingleSampleShadowsLightingPolicy::ShouldCompilePermutation(Parameters);
		case LMP_SIMPLE_STATIONARY_VOLUMETRICLIGHTMAP_SHADOW_LIGHTING:
			return FSimpleStationaryLightVolumetricLightmapShadowsLightingPolicy::ShouldCompilePermutation(Parameters);
		case LMP_LQ_LIGHTMAP:
			return TLightMapPolicy<LQ_LIGHTMAP>::ShouldCompilePermutation(Parameters);
		case LMP_HQ_LIGHTMAP:
			return TLightMapPolicy<HQ_LIGHTMAP>::ShouldCompilePermutation(Parameters);
		case LMP_DISTANCE_FIELD_SHADOWS_AND_HQ_LIGHTMAP:
			return TDistanceFieldShadowsAndLightMapPolicy<HQ_LIGHTMAP>::ShouldCompilePermutation(Parameters);

		// Mobile specific
		
		case LMP_MOBILE_DISTANCE_FIELD_SHADOWS_AND_LQ_LIGHTMAP:
			return FMobileDistanceFieldShadowsAndLQLightMapPolicy::ShouldCompilePermutation(Parameters);
		case LMP_MOBILE_DISTANCE_FIELD_SHADOWS_LIGHTMAP_AND_CSM:
			return FMobileDistanceFieldShadowsLightMapAndCSMLightingPolicy::ShouldCompilePermutation(Parameters);
		case LMP_MOBILE_DIRECTIONAL_LIGHT_AND_SH_INDIRECT:
			return FMobileDirectionalLightAndSHIndirectPolicy::ShouldCompilePermutation(Parameters);
		case LMP_MOBILE_MOVABLE_DIRECTIONAL_LIGHT_AND_SH_INDIRECT:
			return FMobileMovableDirectionalLightAndSHIndirectPolicy::ShouldCompilePermutation(Parameters);
		case LMP_MOBILE_DIRECTIONAL_LIGHT_CSM_AND_SH_INDIRECT:
			return FMobileDirectionalLightCSMAndSHIndirectPolicy::ShouldCompilePermutation(Parameters);
		case LMP_MOBILE_MOVABLE_DIRECTIONAL_LIGHT_CSM_AND_SH_INDIRECT:
			return FMobileMovableDirectionalLightCSMAndSHIndirectPolicy::ShouldCompilePermutation(Parameters);
		case LMP_MOBILE_MOVABLE_DIRECTIONAL_LIGHT_CSM:
			return FMobileMovableDirectionalLightCSMLightingPolicy::ShouldCompilePermutation(Parameters);
		case LMP_MOBILE_MOVABLE_DIRECTIONAL_LIGHT_WITH_LIGHTMAP:
			return FMobileMovableDirectionalLightWithLightmapPolicy::ShouldCompilePermutation(Parameters);
		case LMP_MOBILE_MOVABLE_DIRECTIONAL_LIGHT_CSM_WITH_LIGHTMAP:
			return FMobileMovableDirectionalLightCSMWithLightmapPolicy::ShouldCompilePermutation(Parameters);

		// LightMapDensity
	
		case LMP_DUMMY:
			return FDummyLightMapPolicy::ShouldCompilePermutation(Parameters);

		default:
			check(false);
			return false;
		};
	}

	static void ModifyCompilationEnvironment(const FMaterialShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		OutEnvironment.SetDefine(TEXT("MAX_NUM_LIGHTMAP_COEF"), MAX_NUM_LIGHTMAP_COEF);

		CA_SUPPRESS(6326);
		switch (Policy)
		{
		case LMP_NO_LIGHTMAP:							
			FNoLightMapPolicy::ModifyCompilationEnvironment(Parameters, OutEnvironment);
			break;
		case LMP_PRECOMPUTED_IRRADIANCE_VOLUME_INDIRECT_LIGHTING:
			FPrecomputedVolumetricLightmapLightingPolicy::ModifyCompilationEnvironment(Parameters, OutEnvironment);
			break;
		case LMP_CACHED_VOLUME_INDIRECT_LIGHTING:
			FCachedVolumeIndirectLightingPolicy::ModifyCompilationEnvironment(Parameters, OutEnvironment);
			break;
		case LMP_CACHED_POINT_INDIRECT_LIGHTING:
			FCachedPointIndirectLightingPolicy::ModifyCompilationEnvironment(Parameters, OutEnvironment);
			break;
		case LMP_SIMPLE_NO_LIGHTMAP:
			return FSimpleNoLightmapLightingPolicy::ModifyCompilationEnvironment(Parameters, OutEnvironment);
			break;
		case LMP_SIMPLE_LIGHTMAP_ONLY_LIGHTING:
			return FSimpleLightmapOnlyLightingPolicy::ModifyCompilationEnvironment(Parameters, OutEnvironment);
			break;
		case LMP_SIMPLE_DIRECTIONAL_LIGHT_LIGHTING:
			FSimpleDirectionalLightLightingPolicy::ModifyCompilationEnvironment(Parameters, OutEnvironment);
			break;
		case LMP_SIMPLE_STATIONARY_PRECOMPUTED_SHADOW_LIGHTING:
			return FSimpleStationaryLightPrecomputedShadowsLightingPolicy::ModifyCompilationEnvironment(Parameters, OutEnvironment);
			break;
		case LMP_SIMPLE_STATIONARY_SINGLESAMPLE_SHADOW_LIGHTING:
			return FSimpleStationaryLightSingleSampleShadowsLightingPolicy::ModifyCompilationEnvironment(Parameters, OutEnvironment);
			break;
		case LMP_SIMPLE_STATIONARY_VOLUMETRICLIGHTMAP_SHADOW_LIGHTING:
			return FSimpleStationaryLightVolumetricLightmapShadowsLightingPolicy::ModifyCompilationEnvironment(Parameters, OutEnvironment);
			break;
		case LMP_LQ_LIGHTMAP:
			TLightMapPolicy<LQ_LIGHTMAP>::ModifyCompilationEnvironment(Parameters, OutEnvironment);
			break;
		case LMP_HQ_LIGHTMAP:
			TLightMapPolicy<HQ_LIGHTMAP>::ModifyCompilationEnvironment(Parameters, OutEnvironment);
			break;
		case LMP_DISTANCE_FIELD_SHADOWS_AND_HQ_LIGHTMAP:
			TDistanceFieldShadowsAndLightMapPolicy<HQ_LIGHTMAP>::ModifyCompilationEnvironment(Parameters, OutEnvironment);
			break;

		// Mobile specific
		case LMP_MOBILE_DISTANCE_FIELD_SHADOWS_AND_LQ_LIGHTMAP:
			FMobileDistanceFieldShadowsAndLQLightMapPolicy::ModifyCompilationEnvironment(Parameters, OutEnvironment);
			break;
		case LMP_MOBILE_DISTANCE_FIELD_SHADOWS_LIGHTMAP_AND_CSM:
			FMobileDistanceFieldShadowsLightMapAndCSMLightingPolicy::ModifyCompilationEnvironment(Parameters, OutEnvironment);
			break;
		case LMP_MOBILE_DIRECTIONAL_LIGHT_AND_SH_INDIRECT:
			FMobileDirectionalLightAndSHIndirectPolicy::ModifyCompilationEnvironment(Parameters, OutEnvironment);
			break;
		case LMP_MOBILE_MOVABLE_DIRECTIONAL_LIGHT_AND_SH_INDIRECT:
			FMobileMovableDirectionalLightAndSHIndirectPolicy::ModifyCompilationEnvironment(Parameters, OutEnvironment);
			break;
		case LMP_MOBILE_DIRECTIONAL_LIGHT_CSM_AND_SH_INDIRECT:
			FMobileDirectionalLightCSMAndSHIndirectPolicy::ModifyCompilationEnvironment(Parameters, OutEnvironment);
			break;
		case LMP_MOBILE_MOVABLE_DIRECTIONAL_LIGHT_CSM_AND_SH_INDIRECT:
			FMobileMovableDirectionalLightCSMAndSHIndirectPolicy::ModifyCompilationEnvironment(Parameters, OutEnvironment);
			break;
		case LMP_MOBILE_MOVABLE_DIRECTIONAL_LIGHT_CSM:
			FMobileMovableDirectionalLightCSMLightingPolicy::ModifyCompilationEnvironment(Parameters, OutEnvironment);
			break;
		case LMP_MOBILE_MOVABLE_DIRECTIONAL_LIGHT_WITH_LIGHTMAP:
			FMobileMovableDirectionalLightWithLightmapPolicy::ModifyCompilationEnvironment(Parameters, OutEnvironment);
			break;
		case LMP_MOBILE_MOVABLE_DIRECTIONAL_LIGHT_CSM_WITH_LIGHTMAP:
			FMobileMovableDirectionalLightCSMWithLightmapPolicy::ModifyCompilationEnvironment(Parameters, OutEnvironment);
			break;

		// LightMapDensity
	
		case LMP_DUMMY:
			FDummyLightMapPolicy::ModifyCompilationEnvironment(Parameters, OutEnvironment);
			break;

		default:
			check(false);
			break;
		}
	}

	static bool RequiresSkylight()
	{
		CA_SUPPRESS(6326);
		switch (Policy)
		{
		// Simple forward
		case LMP_SIMPLE_NO_LIGHTMAP:
		case LMP_SIMPLE_LIGHTMAP_ONLY_LIGHTING:
		case LMP_SIMPLE_DIRECTIONAL_LIGHT_LIGHTING:
		case LMP_SIMPLE_STATIONARY_PRECOMPUTED_SHADOW_LIGHTING:
		case LMP_SIMPLE_STATIONARY_SINGLESAMPLE_SHADOW_LIGHTING:
			return true;
		default:
			return false;
		};
	}
};

struct FSelfShadowLightCacheElementData
{
	const FLightCacheInterface* LCI;
	FRHIUniformBuffer* SelfShadowTranslucencyUniformBuffer;
};

/**
 * Self shadowing translucency from a directional light + allows a dynamic object to access indirect lighting through a per-object lighting sample
 */
class FSelfShadowedCachedPointIndirectLightingPolicy : public FSelfShadowedTranslucencyPolicy
{
public:
	typedef const FSelfShadowLightCacheElementData ElementDataType;

	class PixelParametersType : public FUniformLightMapPolicyShaderParametersType, public FSelfShadowedTranslucencyPolicy::PixelParametersType
	{
	public:
		void Bind(const FShaderParameterMap& ParameterMap)
		{
			FUniformLightMapPolicyShaderParametersType::Bind(ParameterMap);
			FSelfShadowedTranslucencyPolicy::PixelParametersType::Bind(ParameterMap);
		}

		void Serialize(FArchive& Ar)
		{
			FUniformLightMapPolicyShaderParametersType::Serialize(Ar);
			FSelfShadowedTranslucencyPolicy::PixelParametersType::Serialize(Ar);
		}
	};

	static bool ShouldCompilePermutation(const FMeshMaterialShaderPermutationParameters& Parameters)
	{
		static IConsoleVariable* AllowStaticLightingVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.AllowStaticLighting"));

		return Parameters.Material->GetShadingModels().IsLit()
			&& IsTranslucentBlendMode(Parameters.Material->GetBlendMode())
			&& (!AllowStaticLightingVar || AllowStaticLightingVar->GetInt() != 0)
			&& FSelfShadowedTranslucencyPolicy::ShouldCompilePermutation(Parameters);
	}

	static void ModifyCompilationEnvironment(const FMaterialShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment);

	/** Initialization constructor. */
	FSelfShadowedCachedPointIndirectLightingPolicy() {}

	static void GetVertexShaderBindings(
		const FPrimitiveSceneProxy* PrimitiveSceneProxy,
		const ElementDataType& ShaderElementData,
		const VertexParametersType* VertexShaderParameters,
		FMeshDrawSingleShaderBindings& ShaderBindings) {}

	static void GetPixelShaderBindings(
		const FPrimitiveSceneProxy* PrimitiveSceneProxy,
		const ElementDataType& ShaderElementData,
		const PixelParametersType* PixelShaderParameters,
		FMeshDrawSingleShaderBindings& ShaderBindings);
};

class FSelfShadowedVolumetricLightmapPolicy : public FSelfShadowedTranslucencyPolicy
{
public:
	typedef const FSelfShadowLightCacheElementData ElementDataType;

	class PixelParametersType : public FUniformLightMapPolicyShaderParametersType, public FSelfShadowedTranslucencyPolicy::PixelParametersType
	{
	public:
		void Bind(const FShaderParameterMap& ParameterMap)
		{
			FUniformLightMapPolicyShaderParametersType::Bind(ParameterMap);
			FSelfShadowedTranslucencyPolicy::PixelParametersType::Bind(ParameterMap);
		}

		void Serialize(FArchive& Ar)
		{
			FUniformLightMapPolicyShaderParametersType::Serialize(Ar);
			FSelfShadowedTranslucencyPolicy::PixelParametersType::Serialize(Ar);
		}
	};

	static bool ShouldCompilePermutation(const FMeshMaterialShaderPermutationParameters& Parameters)
	{
		static IConsoleVariable* AllowStaticLightingVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.AllowStaticLighting"));

		return Parameters.Material->GetShadingModels().IsLit()
			&& IsTranslucentBlendMode(Parameters.Material->GetBlendMode())
			&& (!AllowStaticLightingVar || AllowStaticLightingVar->GetInt() != 0)
			&& FSelfShadowedTranslucencyPolicy::ShouldCompilePermutation(Parameters);
	}

	static void ModifyCompilationEnvironment(const FMaterialShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		OutEnvironment.SetDefine(TEXT("PRECOMPUTED_IRRADIANCE_VOLUME_LIGHTING"),TEXT("1"));	
		FSelfShadowedTranslucencyPolicy::ModifyCompilationEnvironment(Parameters, OutEnvironment);
	}

	/** Initialization constructor. */
	FSelfShadowedVolumetricLightmapPolicy() {}

	static void GetVertexShaderBindings(
		const FPrimitiveSceneProxy* PrimitiveSceneProxy,
		const ElementDataType& ShaderElementData,
		const VertexParametersType* VertexShaderParameters,
		FMeshDrawSingleShaderBindings& ShaderBindings) {}

	static void GetPixelShaderBindings(
		const FPrimitiveSceneProxy* PrimitiveSceneProxy,
		const ElementDataType& ShaderElementData,
		const PixelParametersType* PixelShaderParameters,
		FMeshDrawSingleShaderBindings& ShaderBindings);
};