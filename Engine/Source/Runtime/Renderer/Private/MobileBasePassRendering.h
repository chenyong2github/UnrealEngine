// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	MobileBasePassRendering.h: base pass rendering definitions.
=============================================================================*/

#pragma once

#include "CoreMinimal.h"
#include "HAL/IConsoleManager.h"
#include "RHI.h"
#include "ShaderParameters.h"
#include "Shader.h"
#include "HitProxies.h"
#include "RHIStaticStates.h"
#include "SceneManagement.h"
#include "PrimitiveSceneInfo.h"
#include "PostProcess/SceneRenderTargets.h"
#include "LightMapRendering.h"
#include "MeshMaterialShaderType.h"
#include "MeshMaterialShader.h"
#include "FogRendering.h"
#include "PlanarReflectionRendering.h"
#include "BasePassRendering.h"
#include "SkyAtmosphereRendering.h"
#include "RenderUtils.h"

BEGIN_GLOBAL_SHADER_PARAMETER_STRUCT(FMobileBasePassUniformParameters, )
	SHADER_PARAMETER(int32, UseCSM)
	SHADER_PARAMETER(float, AmbientOcclusionStaticFraction)
	SHADER_PARAMETER_STRUCT(FFogUniformParameters, Fog)
	SHADER_PARAMETER_STRUCT(FPlanarReflectionUniformParameters, PlanarReflection) // Single global planar reflection for the forward pass.
	SHADER_PARAMETER_STRUCT(FMobileSceneTextureUniformParameters, SceneTextures)
	SHADER_PARAMETER_TEXTURE(Texture2D, PreIntegratedGFTexture)
	SHADER_PARAMETER_SAMPLER(SamplerState, PreIntegratedGFSampler)
	SHADER_PARAMETER_SRV(Buffer<float4>, EyeAdaptationBuffer)
	SHADER_PARAMETER_TEXTURE(Texture2D, AmbientOcclusionTexture)
	SHADER_PARAMETER_SAMPLER(SamplerState, AmbientOcclusionSampler)
	SHADER_PARAMETER_TEXTURE(Texture2D, ScreenSpaceShadowMaskTexture)
	SHADER_PARAMETER_SAMPLER(SamplerState, ScreenSpaceShadowMaskSampler)
END_GLOBAL_SHADER_PARAMETER_STRUCT()

extern void SetupMobileBasePassUniformParameters(
	FRHICommandListImmediate& RHICmdList,
	const FViewInfo& View,
	bool bTranslucentPass,
	bool bCanUseCSM,
	FMobileBasePassUniformParameters& BasePassParameters);

extern void CreateMobileBasePassUniformBuffer(
	FRHICommandListImmediate& RHICmdList,
	const FViewInfo& View,
	bool bTranslucentPass,
	bool bCanUseCSM,
	TUniformBufferRef<FMobileBasePassUniformParameters>& BasePassUniformBuffer);

extern void SetupMobileDirectionalLightUniformParameters(
	const FScene& Scene,
	const FViewInfo& View,
	const TArray<FVisibleLightInfo,SceneRenderingAllocator> VisibleLightInfos,
	int32 ChannelIdx,
	bool bDynamicShadows,
	FMobileDirectionalLightShaderParameters& Parameters);

extern void SetupMobileSkyReflectionUniformParameters(
	class FSkyLightSceneProxy* SkyLight,
	FMobileReflectionCaptureShaderParameters& Parameters);



class FPlanarReflectionSceneProxy;
class FScene;

enum EOutputFormat
{
	LDR_GAMMA_32,
	HDR_LINEAR_64,
};

#define MAX_BASEPASS_DYNAMIC_POINT_LIGHTS 4

/* Info for dynamic point or spot lights rendered in base pass */
class FMobileBasePassMovableLightInfo
{
public:
	FMobileBasePassMovableLightInfo(const FPrimitiveSceneProxy* InSceneProxy);

	int32 NumMovablePointLights;
	FRHIUniformBuffer* MovablePointLightUniformBuffer[MAX_BASEPASS_DYNAMIC_POINT_LIGHTS];
};

bool ShouldCacheShaderByPlatformAndOutputFormat(EShaderPlatform Platform, EOutputFormat OutputFormat);

template<typename LightMapPolicyType>
class TMobileBasePassShaderElementData : public FMeshMaterialShaderElementData
{
public:
	TMobileBasePassShaderElementData(const typename LightMapPolicyType::ElementDataType& InLightMapPolicyElementData) :
		LightMapPolicyElementData(InLightMapPolicyElementData)
	{}

	typename LightMapPolicyType::ElementDataType LightMapPolicyElementData;
};

/**
 * The base shader type for vertex shaders that render the emissive color, and light-mapped/ambient lighting of a mesh.
 */

template<typename LightMapPolicyType>
class TMobileBasePassVSPolicyParamType : public FMeshMaterialShader, public LightMapPolicyType::VertexParametersType
{
	DECLARE_INLINE_TYPE_LAYOUT_EXPLICIT_BASES(TMobileBasePassVSPolicyParamType, NonVirtual, FMeshMaterialShader, typename LightMapPolicyType::VertexParametersType);
protected:

	TMobileBasePassVSPolicyParamType() {}
	TMobileBasePassVSPolicyParamType(const FMeshMaterialShaderType::CompiledShaderInitializerType& Initializer):
		FMeshMaterialShader(Initializer)
	{
		LightMapPolicyType::VertexParametersType::Bind(Initializer.ParameterMap);
		PassUniformBuffer.Bind(Initializer.ParameterMap, FMobileBasePassUniformParameters::StaticStructMetadata.GetShaderVariableName());
	}

public:

	// static bool ShouldCompilePermutation(EShaderPlatform Platform,const FMaterial* Material,const FVertexFactoryType* VertexFactoryType)

	static void ModifyCompilationEnvironment(const FMaterialShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FMeshMaterialShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
	}

	void GetShaderBindings(
		const FScene* Scene,
		ERHIFeatureLevel::Type FeatureLevel,
		const FPrimitiveSceneProxy* PrimitiveSceneProxy,
		const FMaterialRenderProxy& MaterialRenderProxy,
		const FMaterial& Material,
		const FMeshPassProcessorRenderState& DrawRenderState,
		const TMobileBasePassShaderElementData<LightMapPolicyType>& ShaderElementData,
		FMeshDrawSingleShaderBindings& ShaderBindings) const
	{
		FMeshMaterialShader::GetShaderBindings(Scene, FeatureLevel, PrimitiveSceneProxy, MaterialRenderProxy, Material, DrawRenderState, ShaderElementData, ShaderBindings);

		LightMapPolicyType::GetVertexShaderBindings(
			PrimitiveSceneProxy,
			ShaderElementData.LightMapPolicyElementData,
			this,
			ShaderBindings);
	}
};

template<typename LightMapPolicyType>
class TMobileBasePassVSBaseType : public TMobileBasePassVSPolicyParamType<LightMapPolicyType>
{
	typedef TMobileBasePassVSPolicyParamType<LightMapPolicyType> Super;
	DECLARE_INLINE_TYPE_LAYOUT(TMobileBasePassVSBaseType, NonVirtual);
protected:

	TMobileBasePassVSBaseType() {}
	TMobileBasePassVSBaseType(const FMeshMaterialShaderType::CompiledShaderInitializerType& Initializer) : Super(Initializer) {}

public:

	static bool ShouldCompilePermutation(const FMeshMaterialShaderPermutationParameters& Parameters)
	{
		return IsMobilePlatform(Parameters.Platform) && LightMapPolicyType::ShouldCompilePermutation(Parameters);
	}

	static void ModifyCompilationEnvironment(const FMaterialShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		LightMapPolicyType::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		Super::ModifyCompilationEnvironment(Parameters, OutEnvironment);
	}

	
	
};

template< typename LightMapPolicyType, EOutputFormat OutputFormat >
class TMobileBasePassVS : public TMobileBasePassVSBaseType<LightMapPolicyType>
{
	DECLARE_SHADER_TYPE(TMobileBasePassVS,MeshMaterial);
public:
	
	static bool ShouldCompilePermutation(const FMeshMaterialShaderPermutationParameters& Parameters)
	{		
		return TMobileBasePassVSBaseType<LightMapPolicyType>::ShouldCompilePermutation(Parameters) && ShouldCacheShaderByPlatformAndOutputFormat(Parameters.Platform,OutputFormat);
	}

	static void ModifyCompilationEnvironment(const FMaterialShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		static auto* MobileUseHWsRGBEncodingCVAR = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.Mobile.UseHWsRGBEncoding"));
		const bool bMobileUseHWsRGBEncoding = (MobileUseHWsRGBEncodingCVAR && MobileUseHWsRGBEncodingCVAR->GetValueOnAnyThread() == 1);

		TMobileBasePassVSBaseType<LightMapPolicyType>::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine( TEXT("OUTPUT_GAMMA_SPACE"), OutputFormat == LDR_GAMMA_32 && !bMobileUseHWsRGBEncoding);
		OutEnvironment.SetDefine( TEXT("OUTPUT_MOBILE_HDR"), OutputFormat == HDR_LINEAR_64 ? 1u : 0u);
	}
	
	/** Initialization constructor. */
	TMobileBasePassVS(const ShaderMetaType::CompiledShaderInitializerType& Initializer):
		TMobileBasePassVSBaseType<LightMapPolicyType>(Initializer)
	{}

	/** Default constructor. */
	TMobileBasePassVS() {}
};

/**
 * The base type for pixel shaders that render the emissive color, and light-mapped/ambient lighting of a mesh.
 */

template<typename LightMapPolicyType>
class TMobileBasePassPSPolicyParamType : public FMeshMaterialShader, public LightMapPolicyType::PixelParametersType
{
	DECLARE_INLINE_TYPE_LAYOUT_EXPLICIT_BASES(TMobileBasePassPSPolicyParamType, NonVirtual, FMeshMaterialShader, typename LightMapPolicyType::PixelParametersType);
public:

	static bool ShouldCompilePermutation(const FMeshMaterialShaderPermutationParameters& Parameters)
	{
		return IsMobilePlatform(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FMaterialShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FMeshMaterialShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		// Modify compilation environment depending upon material shader quality level settings.
		ModifyCompilationEnvironmentForQualityLevel(Parameters.Platform, Parameters.MaterialParameters.QualityLevel, OutEnvironment);
	}

	/** Initialization constructor. */
	TMobileBasePassPSPolicyParamType(const FMeshMaterialShaderType::CompiledShaderInitializerType& Initializer)
		: FMeshMaterialShader(Initializer)
	{
		LightMapPolicyType::PixelParametersType::Bind(Initializer.ParameterMap);
		PassUniformBuffer.Bind(Initializer.ParameterMap, FMobileBasePassUniformParameters::StaticStructMetadata.GetShaderVariableName());
		
		MobileDirectionLightBufferParam.Bind(Initializer.ParameterMap, FMobileDirectionalLightShaderParameters::StaticStructMetadata.GetShaderVariableName());
		ReflectionParameter.Bind(Initializer.ParameterMap, FMobileReflectionCaptureShaderParameters::StaticStructMetadata.GetShaderVariableName());

		HQReflectionCubemaps[0].Bind(Initializer.ParameterMap, TEXT("ReflectionCubemap0"));
		HQReflectionSamplers[0].Bind(Initializer.ParameterMap, TEXT("ReflectionCubemapSampler0"));
		HQReflectionCubemaps[1].Bind(Initializer.ParameterMap, TEXT("ReflectionCubemap1"));
		HQReflectionSamplers[1].Bind(Initializer.ParameterMap, TEXT("ReflectionCubemapSampler1"));
		HQReflectionCubemaps[2].Bind(Initializer.ParameterMap, TEXT("ReflectionCubemap2"));
		HQReflectionSamplers[2].Bind(Initializer.ParameterMap, TEXT("ReflectionCubemapSampler2"));
		HQReflectionInvAverageBrigtnessParams.Bind(Initializer.ParameterMap, TEXT("ReflectionAverageBrigtness"));
		HQReflectanceMaxValueRGBMParams.Bind(Initializer.ParameterMap, TEXT("ReflectanceMaxValueRGBM"));
		HQReflectionPositionsAndRadii.Bind(Initializer.ParameterMap, TEXT("ReflectionPositionsAndRadii"));
		HQReflectionCaptureBoxTransformArray.Bind(Initializer.ParameterMap, TEXT("CaptureBoxTransformArray"));
		HQReflectionCaptureBoxScalesArray.Bind(Initializer.ParameterMap, TEXT("CaptureBoxScalesArray"));

		NumDynamicPointLightsParameter.Bind(Initializer.ParameterMap, TEXT("NumDynamicPointLights"));
						
		CSMDebugHintParams.Bind(Initializer.ParameterMap, TEXT("CSMDebugHint"));
	}

	TMobileBasePassPSPolicyParamType() {}

private:
	LAYOUT_FIELD(FShaderUniformBufferParameter, MobileDirectionLightBufferParam);
	LAYOUT_FIELD(FShaderUniformBufferParameter, ReflectionParameter);

	// HQ reflection bound as loose params
	LAYOUT_ARRAY(FShaderResourceParameter, HQReflectionCubemaps, 3);
	LAYOUT_ARRAY(FShaderResourceParameter, HQReflectionSamplers, 3);
	LAYOUT_FIELD(FShaderParameter, HQReflectionInvAverageBrigtnessParams);
	LAYOUT_FIELD(FShaderParameter, HQReflectanceMaxValueRGBMParams);
	LAYOUT_FIELD(FShaderParameter, HQReflectionPositionsAndRadii);
	LAYOUT_FIELD(FShaderParameter, HQReflectionCaptureBoxTransformArray);
	LAYOUT_FIELD(FShaderParameter, HQReflectionCaptureBoxScalesArray);

	LAYOUT_FIELD(FShaderParameter, NumDynamicPointLightsParameter);

	LAYOUT_FIELD(FShaderParameter, CSMDebugHintParams);
	
public:
	void GetShaderBindings(
		const FScene* Scene,
		ERHIFeatureLevel::Type FeatureLevel,
		const FPrimitiveSceneProxy* PrimitiveSceneProxy,
		const FMaterialRenderProxy& MaterialRenderProxy,
		const FMaterial& Material,
		const FMeshPassProcessorRenderState& DrawRenderState,
		const TMobileBasePassShaderElementData<LightMapPolicyType>& ShaderElementData,
		FMeshDrawSingleShaderBindings& ShaderBindings) const;

private:
	static bool ModifyCompilationEnvironmentForQualityLevel(EShaderPlatform Platform, EMaterialQualityLevel::Type QualityLevel, FShaderCompilerEnvironment& OutEnvironment);
};

template<typename LightMapPolicyType>
class TMobileBasePassPSBaseType : public TMobileBasePassPSPolicyParamType<LightMapPolicyType>
{
	typedef TMobileBasePassPSPolicyParamType<LightMapPolicyType> Super;
	DECLARE_INLINE_TYPE_LAYOUT(TMobileBasePassPSBaseType, NonVirtual);
public:
	static bool ShouldCompilePermutation(const FMeshMaterialShaderPermutationParameters& Parameters)
	{
		return LightMapPolicyType::ShouldCompilePermutation(Parameters)
			&& Super::ShouldCompilePermutation(Parameters);
	}

	static void ModifyCompilationEnvironment(const FMaterialShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		LightMapPolicyType::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		Super::ModifyCompilationEnvironment(Parameters, OutEnvironment);
	}

	/** Initialization constructor. */
	TMobileBasePassPSBaseType(const FMeshMaterialShaderType::CompiledShaderInitializerType& Initializer) : Super(Initializer) {}
	TMobileBasePassPSBaseType() {}
};


namespace MobileBasePass
{
	ELightMapPolicyType SelectMeshLightmapPolicy(
		const FScene* Scene, 
		const FMeshBatch& MeshBatch, 
		const FPrimitiveSceneProxy* PrimitiveSceneProxy, 
		const FLightSceneInfo* MobileDirectionalLight, 
		FMaterialShadingModelField ShadingModels, 
		bool bPrimReceivesCSM, 
		bool bUsedDeferredShading,
		ERHIFeatureLevel::Type FeatureLevel,
		EBlendMode BlendMode);

	bool GetShaders(
		ELightMapPolicyType LightMapPolicyType,
		int32 NumMovablePointLights, 
		const FMaterial& MaterialResource,
		FVertexFactoryType* VertexFactoryType,
		bool bEnableSkyLight, 
		TShaderRef<TMobileBasePassVSPolicyParamType<FUniformLightMapPolicy>>& VertexShader,
		TShaderRef<TMobileBasePassPSPolicyParamType<FUniformLightMapPolicy>>& PixelShader);

	const FLightSceneInfo* GetDirectionalLightInfo(const FScene* Scene, const FPrimitiveSceneProxy* PrimitiveSceneProxy);
	int32 CalcNumMovablePointLights(const FMaterial& InMaterial, const FPrimitiveSceneProxy* InPrimitiveSceneProxy);

	bool StaticCanReceiveCSM(const FLightSceneInfo* LightSceneInfo, const FPrimitiveSceneProxy* PrimitiveSceneProxy);

	void SetOpaqueRenderState(FMeshPassProcessorRenderState& DrawRenderState, const FPrimitiveSceneProxy* PrimitiveSceneProxy, const FMaterial& Material, bool bEnableReceiveDecalOutput, bool bUsesDeferredShading);
	void SetTranslucentRenderState(FMeshPassProcessorRenderState& DrawRenderState, const FMaterial& Material);

	bool StationarySkyLightHasBeenApplied(const FScene* Scene, ELightMapPolicyType LightMapPolicyType);

	extern FShaderPlatformCachedIniValue<bool> MobileDynamicPointLightsUseStaticBranchIniValue;
	extern FShaderPlatformCachedIniValue<int32> MobileNumDynamicPointLightsIniValue;
};


inline bool UseSkylightPermutation(bool bEnableSkyLight, int32 MobileSkyLightPermutationOptions)
{
	if (bEnableSkyLight)
	{
		return MobileSkyLightPermutationOptions == 0 || MobileSkyLightPermutationOptions == 2;
	}
	else
	{
		return MobileSkyLightPermutationOptions == 0 || MobileSkyLightPermutationOptions == 1;
	}
}

template< typename LightMapPolicyType, EOutputFormat OutputFormat, bool bEnableSkyLight, int32 NumMovablePointLights>
class TMobileBasePassPS : public TMobileBasePassPSBaseType<LightMapPolicyType>
{
	DECLARE_SHADER_TYPE(TMobileBasePassPS,MeshMaterial);
public:

	static bool ShouldCompilePermutation(const FMeshMaterialShaderPermutationParameters& Parameters)
	{		
		// We compile the point light shader combinations based on the project settings
		static auto* MobileSkyLightPermutationCVar = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.Mobile.SkyLightPermutation"));

		const bool bMobileDynamicPointLightsUseStaticBranch = MobileBasePass::MobileDynamicPointLightsUseStaticBranchIniValue.Get(Parameters.Platform);
		const int32 MobileNumDynamicPointLights = MobileBasePass::MobileNumDynamicPointLightsIniValue.Get(Parameters.Platform);
		const int32 MobileSkyLightPermutationOptions = MobileSkyLightPermutationCVar->GetValueOnAnyThread();
		const bool bDeferredShading = IsMobileDeferredShadingEnabled(Parameters.Platform);
		
		const bool bIsLit = Parameters.MaterialParameters.ShadingModels.IsLit();
		const bool bMaterialUsesForwardShading = bIsLit && 
			(IsTranslucentBlendMode(Parameters.MaterialParameters.BlendMode) || Parameters.MaterialParameters.ShadingModels.HasShadingModel(MSM_SingleLayerWater));

		// Only compile skylight version for lit materials
		const bool bShouldCacheBySkylight = !bEnableSkyLight || bIsLit;

		// Only compile skylight permutations when they are enabled
		if (bIsLit && !UseSkylightPermutation(bEnableSkyLight, MobileSkyLightPermutationOptions))
		{
			return false;
		}

		// Deferred shading does not need SkyLight and PointLight permutations
		// TODO: skip skylight permutations for deferred
		const bool bShouldCacheByShading = (!bDeferredShading || bMaterialUsesForwardShading) || (NumMovablePointLights == 0);

		const bool bShouldCacheByNumDynamicPointLights =
			(NumMovablePointLights == 0 ||
			(bIsLit && NumMovablePointLights == INT32_MAX && bMobileDynamicPointLightsUseStaticBranch && MobileNumDynamicPointLights > 0) ||	// single shader for variable number of point lights
				(bIsLit && NumMovablePointLights <= MobileNumDynamicPointLights && !bMobileDynamicPointLightsUseStaticBranch));				// unique 1...N point light shaders

		return TMobileBasePassPSBaseType<LightMapPolicyType>::ShouldCompilePermutation(Parameters) && 
				ShouldCacheShaderByPlatformAndOutputFormat(Parameters.Platform, OutputFormat) && 
				bShouldCacheBySkylight && 
				bShouldCacheByNumDynamicPointLights &&
				bShouldCacheByShading;
	}
	
	static void ModifyCompilationEnvironment(const FMaterialShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{		
		static auto* MobileUseHWsRGBEncodingCVAR = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.Mobile.UseHWsRGBEncoding"));
		const bool bMobileUseHWsRGBEncoding = (MobileUseHWsRGBEncodingCVAR && MobileUseHWsRGBEncodingCVAR->GetValueOnAnyThread() == 1);

		TMobileBasePassPSBaseType<LightMapPolicyType>::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("ENABLE_SKY_LIGHT"), bEnableSkyLight);
		OutEnvironment.SetDefine(TEXT("OUTPUT_GAMMA_SPACE"), OutputFormat == LDR_GAMMA_32 && !bMobileUseHWsRGBEncoding);
		OutEnvironment.SetDefine(TEXT("OUTPUT_MOBILE_HDR"), OutputFormat == HDR_LINEAR_64 ? 1u : 0u);
		if (NumMovablePointLights == INT32_MAX)
		{
			static auto* MobileNumDynamicPointLightsCVar = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.MobileNumDynamicPointLights"));
			const int32 MaxDynamicPointLights = FMath::Clamp(MobileNumDynamicPointLightsCVar->GetValueOnAnyThread(), 0, MAX_BASEPASS_DYNAMIC_POINT_LIGHTS);
			
			OutEnvironment.SetDefine(TEXT("MAX_DYNAMIC_POINT_LIGHTS"), (uint32)MaxDynamicPointLights);
			OutEnvironment.SetDefine(TEXT("VARIABLE_NUM_DYNAMIC_POINT_LIGHTS"), (uint32)1);
		}
		else
		{
			OutEnvironment.SetDefine(TEXT("MAX_DYNAMIC_POINT_LIGHTS"), (uint32)NumMovablePointLights);
			OutEnvironment.SetDefine(TEXT("VARIABLE_NUM_DYNAMIC_POINT_LIGHTS"), (uint32)0);
			OutEnvironment.SetDefine(TEXT("NUM_DYNAMIC_POINT_LIGHTS"), (uint32)NumMovablePointLights);
		}

		OutEnvironment.SetDefine(TEXT("ENABLE_AMBIENT_OCCLUSION"), IsMobileAmbientOcclusionEnabled(Parameters.Platform) ? 1u : 0u);

		OutEnvironment.SetDefine(TEXT("ENABLE_DISTANCE_FIELD"), IsMobileDistanceFieldEnabled(Parameters.Platform));
	}
	
	/** Initialization constructor. */
	TMobileBasePassPS(const ShaderMetaType::CompiledShaderInitializerType& Initializer):
		TMobileBasePassPSBaseType<LightMapPolicyType>(Initializer)
	{}

	/** Default constructor. */
	TMobileBasePassPS() {}
};

class FMobileBasePassMeshProcessor : public FMeshPassProcessor
{
public:
	enum class EFlags
	{
		None = 0,

		// Informs the processor whether a depth-stencil target is bound when processed draw commands are issued.
		CanUseDepthStencil = (1 << 0),

		// Informs the processor whether primitives can receive shadows from cascade shadow maps.
		CanReceiveCSM = (1 << 1),

		// Informs the processor to use PassDrawRenderState for all mesh commands
		ForcePassDrawRenderState = (1 << 2)
	};

	FMobileBasePassMeshProcessor(
		const FScene* InScene,
		ERHIFeatureLevel::Type InFeatureLevel,
		const FSceneView* InViewIfDynamicMeshCommand,
		const FMeshPassProcessorRenderState& InDrawRenderState,
		FMeshPassDrawListContext* InDrawListContext,
		EFlags Flags,
		ETranslucencyPass::Type InTranslucencyPassType = ETranslucencyPass::TPT_MAX);

	virtual void AddMeshBatch(const FMeshBatch& RESTRICT MeshBatch, uint64 BatchElementMask, const FPrimitiveSceneProxy* RESTRICT PrimitiveSceneProxy, int32 StaticMeshId = -1) override final;

	FMeshPassProcessorRenderState PassDrawRenderState;

private:
	bool TryAddMeshBatch(const FMeshBatch& RESTRICT MeshBatch, uint64 BatchElementMask, const FPrimitiveSceneProxy* RESTRICT PrimitiveSceneProxy, int32 StaticMeshId, const FMaterialRenderProxy& MaterialRenderProxy, const FMaterial& Material);

	bool Process(
		const FMeshBatch& RESTRICT MeshBatch,
		uint64 BatchElementMask,
		int32 StaticMeshId,
		const FPrimitiveSceneProxy* RESTRICT PrimitiveSceneProxy,
		const FMaterialRenderProxy& RESTRICT MaterialRenderProxy,
		const FMaterial& RESTRICT MaterialResource,
		EBlendMode BlendMode,
		FMaterialShadingModelField ShadingModels,
		const ELightMapPolicyType LightMapPolicyType,
		const FUniformLightMapPolicy::ElementDataType& RESTRICT LightMapElementData);

	const ETranslucencyPass::Type TranslucencyPassType;
	const EFlags Flags;
	const bool bTranslucentBasePass;
	const bool bUsesDeferredShading;
};

ENUM_CLASS_FLAGS(FMobileBasePassMeshProcessor::EFlags);
