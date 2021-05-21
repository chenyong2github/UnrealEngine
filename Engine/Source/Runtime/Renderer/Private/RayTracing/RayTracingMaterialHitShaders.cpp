// Copyright Epic Games, Inc. All Rights Reserved.

#include "RayTracing/RayTracingMaterialHitShaders.h"
#include "DeferredShadingRenderer.h"
#include "PipelineStateCache.h"

#if RHI_RAYTRACING
#include "RayTracingDefinitions.h"
#include "RayTracingInstance.h"
#include "BuiltInRayTracingShaders.h"
#include "RaytracingOptions.h"

int32 GEnableRayTracingMaterials = 1;
static FAutoConsoleVariableRef CVarEnableRayTracingMaterials(
	TEXT("r.RayTracing.EnableMaterials"),
	GEnableRayTracingMaterials,
	TEXT(" 0: bind default material shader that outputs placeholder data\n")
	TEXT(" 1: bind real material shaders (default)\n"),
	ECVF_RenderThreadSafe
);

int32 GCompileRayTracingMaterialCHS = 1;
static FAutoConsoleVariableRef CVarCompileRayTracingMaterialCHS(
	TEXT("r.RayTracing.CompileMaterialCHS"),
	GCompileRayTracingMaterialCHS,
	TEXT(" 0: skip compilation of closest-hit shaders for materials (useful if only shadows or ambient occlusion effects are needed)\n")
	TEXT(" 1: compile closest hit shaders for all ray tracing materials (default)\n"),
	ECVF_ReadOnly
);

int32 GCompileRayTracingMaterialAHS = 1;
static FAutoConsoleVariableRef CVarCompileRayTracingMaterialAHS(
	TEXT("r.RayTracing.CompileMaterialAHS"),
	GCompileRayTracingMaterialAHS,
	TEXT(" 0: skip compilation of any-hit shaders for materials (useful if alpha masked or translucent materials are not needed)\n")
	TEXT(" 1: compile any hit shaders for all ray tracing materials (default)\n"),
	ECVF_ReadOnly
);

static int32 GRayTracingNonBlockingPipelineCreation = 0;
static FAutoConsoleVariableRef CVarRayTracingNonBlockingPipelineCreation(
	TEXT("r.RayTracing.NonBlockingPipelineCreation"),
	GRayTracingNonBlockingPipelineCreation,
	TEXT("Enable background ray tracing pipeline creation, without blocking RHI or Render thread.\n")
	TEXT("Fallback opaque black material will be used for missing shaders meanwhile.\n")
	TEXT(" 0: off (default, rendering will always use correct requested material)\n")
	TEXT(" 1: on (non-blocking mode may sometimes use the fallback opaque black material)\n"),
	ECVF_RenderThreadSafe);

// CVar defined in DeferredShadingRenderer.cpp
extern int32 GRayTracingUseTextureLod;

static bool IsSupportedVertexFactoryType(const FVertexFactoryType* VertexFactoryType)
{
	static FName LocalVfFname = FName(TEXT("FLocalVertexFactory"), FNAME_Find);
	static FName LSkinnedVfFname = FName(TEXT("FGPUSkinPassthroughVertexFactory"), FNAME_Find);
	static FName InstancedVfFname = FName(TEXT("FInstancedStaticMeshVertexFactory"), FNAME_Find);
	static FName NiagaraRibbonVfFname = FName(TEXT("FNiagaraRibbonVertexFactory"), FNAME_Find);
	static FName NiagaraSpriteVfFname = FName(TEXT("FNiagaraSpriteVertexFactory"), FNAME_Find);
	static FName NiagaraSpriteExVfFname = FName(TEXT("FNiagaraSpriteVertexFactoryEx"), FNAME_Find);
	static FName NiagaraMeshVfFname = FName(TEXT("FNiagaraMeshVertexFactory"), FNAME_Find);
	static FName NiagaraMeshExVfFname = FName(TEXT("FNiagaraMeshVertexFactoryEx"), FNAME_Find);
	static FName GeometryCacheVfFname = FName(TEXT("FGeometryCacheVertexVertexFactory"), FNAME_Find);
	static FName LandscapeVfFname = FName(TEXT("FLandscapeVertexFactory"), FNAME_Find);
	static FName LandscapeFixedGridVfFname = FName(TEXT("FLandscapeFixedGridVertexFactory"), FNAME_Find);
	static FName LandscapeXYOffsetVfFname = FName(TEXT("FLandscapeXYOffsetVertexFactory"), FNAME_Find);
	static FName HairCardVfFname = FName(TEXT("FHairCardsVertexFactory"), FNAME_Find);

	return VertexFactoryType == FindVertexFactoryType(LocalVfFname)
		|| VertexFactoryType == FindVertexFactoryType(LSkinnedVfFname)
		|| VertexFactoryType == FindVertexFactoryType(InstancedVfFname)
		|| VertexFactoryType == FindVertexFactoryType(NiagaraRibbonVfFname)
		|| VertexFactoryType == FindVertexFactoryType(NiagaraSpriteVfFname)
		|| VertexFactoryType == FindVertexFactoryType(NiagaraSpriteExVfFname)
		|| VertexFactoryType == FindVertexFactoryType(NiagaraMeshVfFname)
		|| VertexFactoryType == FindVertexFactoryType(NiagaraMeshExVfFname)
		|| VertexFactoryType == FindVertexFactoryType(GeometryCacheVfFname)
		|| VertexFactoryType == FindVertexFactoryType(LandscapeVfFname)
		|| VertexFactoryType == FindVertexFactoryType(LandscapeFixedGridVfFname)
		|| VertexFactoryType == FindVertexFactoryType(LandscapeXYOffsetVfFname)
		|| VertexFactoryType == FindVertexFactoryType(HairCardVfFname);
}

class FMaterialCHS : public FMeshMaterialShader, public FUniformLightMapPolicyShaderParametersType
{
	DECLARE_INLINE_TYPE_LAYOUT_EXPLICIT_BASES(FMaterialCHS, NonVirtual, FMeshMaterialShader, FUniformLightMapPolicyShaderParametersType);
public:
	FMaterialCHS(const FMeshMaterialShaderType::CompiledShaderInitializerType& Initializer)
		: FMeshMaterialShader(Initializer)
	{
		PassUniformBuffer.Bind(Initializer.ParameterMap, FSceneTextureUniformParameters::StaticStructMetadata.GetShaderVariableName());
		FUniformLightMapPolicyShaderParametersType::Bind(Initializer.ParameterMap);
	}

	FMaterialCHS() {}

	void GetShaderBindings(
		const FScene* Scene,
		ERHIFeatureLevel::Type FeatureLevel,
		const FPrimitiveSceneProxy* PrimitiveSceneProxy,
		const FMaterialRenderProxy& MaterialRenderProxy,
		const FMaterial& Material,
		const FMeshPassProcessorRenderState& DrawRenderState,
		const TBasePassShaderElementData<FUniformLightMapPolicy>& ShaderElementData,
		FMeshDrawSingleShaderBindings& ShaderBindings) const
	{
		FMeshMaterialShader::GetShaderBindings(Scene, FeatureLevel, PrimitiveSceneProxy, MaterialRenderProxy, Material, DrawRenderState, ShaderElementData, ShaderBindings);
		
		FUniformLightMapPolicy::GetPixelShaderBindings(
			PrimitiveSceneProxy,
			ShaderElementData.LightMapPolicyElementData,
			this,
			ShaderBindings);
	}

	void GetElementShaderBindings(
		const FShaderMapPointerTable& PointerTable,
		const FScene* Scene,
		const FSceneView* ViewIfDynamicMeshCommand,
		const FVertexFactory* VertexFactory,
		const EVertexInputStreamType InputStreamType,
		ERHIFeatureLevel::Type FeatureLevel,
		const FPrimitiveSceneProxy* PrimitiveSceneProxy,
		const FMeshBatch& MeshBatch, 
		const FMeshBatchElement& BatchElement,
		const TBasePassShaderElementData<FUniformLightMapPolicy>& ShaderElementData,
		FMeshDrawSingleShaderBindings& ShaderBindings,
		FVertexInputStreamArray& VertexStreams) const
	{
		FMeshMaterialShader::GetElementShaderBindings(PointerTable, Scene, ViewIfDynamicMeshCommand, VertexFactory, InputStreamType, FeatureLevel, PrimitiveSceneProxy, MeshBatch, BatchElement, ShaderElementData, ShaderBindings, VertexStreams);
	}
};

template<typename LightMapPolicyType, bool UseAnyHitShader, bool UseRayConeTextureLod>
class TMaterialCHS : public FMaterialCHS
{
	DECLARE_SHADER_TYPE(TMaterialCHS, MeshMaterial);
public:

	TMaterialCHS(const FMeshMaterialShaderType::CompiledShaderInitializerType& Initializer)
		: FMaterialCHS(Initializer)
	{}

	TMaterialCHS() {}

	static bool ShouldCompilePermutation(const FMeshMaterialShaderPermutationParameters& Parameters)
	{
		if (!GCompileRayTracingMaterialAHS && !GCompileRayTracingMaterialCHS)
		{
			return false;
		}

		return IsSupportedVertexFactoryType(Parameters.VertexFactoryType)
			&& LightMapPolicyType::ShouldCompilePermutation(Parameters)
			&& ShouldCompileRayTracingShadersForProject(Parameters.Platform)
			&& (bool)GRayTracingUseTextureLod == UseRayConeTextureLod;
	}

	static void ModifyCompilationEnvironment(const FMaterialShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		// NOTE: Any CVars that are used in this function must be handled in ShaderMapAppendKeyString() to ensure shaders are recompiled when necessary.

		OutEnvironment.SetDefine(TEXT("USE_MATERIAL_CLOSEST_HIT_SHADER"), GCompileRayTracingMaterialCHS ? 1 : 0);
		OutEnvironment.SetDefine(TEXT("USE_MATERIAL_ANY_HIT_SHADER"), UseAnyHitShader ? 1 : 0);
		OutEnvironment.SetDefine(TEXT("USE_RAYTRACED_TEXTURE_RAYCONE_LOD"), UseRayConeTextureLod ? 1 : 0);
		OutEnvironment.SetDefine(TEXT("SCENE_TEXTURES_DISABLED"), 1);
		LightMapPolicyType::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		FMeshMaterialShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		static const auto CVar = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.VirtualTexturedLightmaps"));
		const bool VirtualTextureLightmaps = (CVar->GetValueOnAnyThread() != 0) && UseVirtualTexturing(GMaxRHIFeatureLevel, OutEnvironment.TargetPlatform);
		OutEnvironment.SetDefine(TEXT("LIGHTMAP_VT_ENABLED"), VirtualTextureLightmaps);
	}

	static bool ValidateCompiledResult(EShaderPlatform Platform, const FShaderParameterMap& ParameterMap, TArray<FString>& OutError)
	{
		if (ParameterMap.ContainsParameterAllocation(FSceneTextureUniformParameters::StaticStructMetadata.GetShaderVariableName()))
		{
			OutError.Add(TEXT("Ray tracing closest hit shaders cannot read from the SceneTexturesStruct."));
			return false;
		}

		for (const auto& It : ParameterMap.GetParameterMap())
		{
			const FParameterAllocation& ParamAllocation = It.Value;
			if (ParamAllocation.Type != EShaderParameterType::UniformBuffer
				&& ParamAllocation.Type != EShaderParameterType::LooseData)
			{
				OutError.Add(FString::Printf(TEXT("Invalid ray tracing shader parameter '%s'. Only uniform buffers and loose data parameters are supported."), *(It.Key)));
				return false;
			}
		}

		return true;
	}
};

class FTrivialMaterialCHS : public FMaterialCHS
{
	DECLARE_SHADER_TYPE(FTrivialMaterialCHS, MeshMaterial);
public:

	FTrivialMaterialCHS(const FMeshMaterialShaderType::CompiledShaderInitializerType& Initializer)
		: FMaterialCHS(Initializer)
	{}

	FTrivialMaterialCHS() {}

	static bool ShouldCompilePermutation(const FMeshMaterialShaderPermutationParameters& Parameters)
	{
		return IsSupportedVertexFactoryType(Parameters.VertexFactoryType)
			&& ShouldCompileRayTracingShadersForProject(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FMaterialShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
	}

	static bool ValidateCompiledResult(EShaderPlatform Platform, const FShaderParameterMap& ParameterMap, TArray<FString>& OutError)
	{
		return true;
	}
};

IMPLEMENT_MATERIAL_SHADER_TYPE(, FTrivialMaterialCHS, TEXT("/Engine/Private/RayTracing/RayTracingMaterialDefaultHitShaders.usf"), TEXT("closesthit=OpaqueShadowCHS"), SF_RayHitGroup);

#define IMPLEMENT_MATERIALCHS_TYPE(LightMapPolicyType, LightMapPolicyName, AnyHitShaderName) \
	typedef TMaterialCHS<LightMapPolicyType, false, false> TMaterialCHS##LightMapPolicyName; \
	IMPLEMENT_MATERIAL_SHADER_TYPE(template<>, TMaterialCHS##LightMapPolicyName, TEXT("/Engine/Private/RayTracing/RayTracingMaterialHitShaders.usf"), TEXT("closesthit=MaterialCHS"), SF_RayHitGroup); \
	typedef TMaterialCHS<LightMapPolicyType, true, false> TMaterialCHS##LightMapPolicyName##AnyHitShaderName; \
	IMPLEMENT_MATERIAL_SHADER_TYPE(template<>, TMaterialCHS##LightMapPolicyName##AnyHitShaderName, TEXT("/Engine/Private/RayTracing/RayTracingMaterialHitShaders.usf"), TEXT("closesthit=MaterialCHS anyhit=MaterialAHS"), SF_RayHitGroup) \
	typedef TMaterialCHS<LightMapPolicyType, false, true> TMaterialCHSLod##LightMapPolicyName; \
	IMPLEMENT_MATERIAL_SHADER_TYPE(template<>, TMaterialCHSLod##LightMapPolicyName, TEXT("/Engine/Private/RayTracing/RayTracingMaterialHitShaders.usf"), TEXT("closesthit=MaterialCHS"), SF_RayHitGroup); \
	typedef TMaterialCHS<LightMapPolicyType, true, true> TMaterialCHSLod##LightMapPolicyName##AnyHitShaderName; \
	IMPLEMENT_MATERIAL_SHADER_TYPE(template<>, TMaterialCHSLod##LightMapPolicyName##AnyHitShaderName, TEXT("/Engine/Private/RayTracing/RayTracingMaterialHitShaders.usf"), TEXT("closesthit=MaterialCHS anyhit=MaterialAHS"), SF_RayHitGroup);

IMPLEMENT_MATERIALCHS_TYPE(TUniformLightMapPolicy<LMP_NO_LIGHTMAP>, FNoLightMapPolicy, FAnyHitShader);
IMPLEMENT_MATERIALCHS_TYPE(TUniformLightMapPolicy<LMP_PRECOMPUTED_IRRADIANCE_VOLUME_INDIRECT_LIGHTING>, FPrecomputedVolumetricLightmapLightingPolicy, FAnyHitShader);
IMPLEMENT_MATERIALCHS_TYPE(TUniformLightMapPolicy<LMP_LQ_LIGHTMAP>, TLightMapPolicyLQ, FAnyHitShader);
IMPLEMENT_MATERIALCHS_TYPE(TUniformLightMapPolicy<LMP_HQ_LIGHTMAP>, TLightMapPolicyHQ, FAnyHitShader);
IMPLEMENT_MATERIALCHS_TYPE(TUniformLightMapPolicy<LMP_DISTANCE_FIELD_SHADOWS_AND_HQ_LIGHTMAP>, TDistanceFieldShadowsAndLightMapPolicyHQ, FAnyHitShader);

IMPLEMENT_GLOBAL_SHADER(FHiddenMaterialHitGroup, "/Engine/Private/RayTracing/RayTracingMaterialDefaultHitShaders.usf", "closesthit=HiddenMaterialCHS anyhit=HiddenMaterialAHS", SF_RayHitGroup);
IMPLEMENT_GLOBAL_SHADER(FOpaqueShadowHitGroup, "/Engine/Private/RayTracing/RayTracingMaterialDefaultHitShaders.usf", "closesthit=OpaqueShadowCHS", SF_RayHitGroup);

template<typename LightMapPolicyType>
static TShaderRef<FMaterialCHS> GetMaterialHitShader(const FMaterial& RESTRICT MaterialResource, const FVertexFactory* VertexFactory, bool UseTextureLod)
{
	const bool bMaterialsCompiled = GCompileRayTracingMaterialAHS || GCompileRayTracingMaterialCHS;
	checkf(bMaterialsCompiled, TEXT(""));

	if ((MaterialResource.IsMasked() || MaterialResource.GetBlendMode() != BLEND_Opaque) && GCompileRayTracingMaterialAHS)
	{
		if(UseTextureLod)
		{ 
			return MaterialResource.GetShader<TMaterialCHS<LightMapPolicyType, true, true>>(VertexFactory->GetType());
		}
		else
		{
			return MaterialResource.GetShader<TMaterialCHS<LightMapPolicyType, true, false>>(VertexFactory->GetType());
		}
	}
	else
	{
		if (UseTextureLod)
		{
			return MaterialResource.GetShader<TMaterialCHS<LightMapPolicyType, false, true>>(VertexFactory->GetType());
		}
		else
		{
			return MaterialResource.GetShader<TMaterialCHS<LightMapPolicyType, false, false>>(VertexFactory->GetType());
		}
	}
}

void FRayTracingMeshProcessor::Process(
	const FMeshBatch& RESTRICT MeshBatch,
	uint64 BatchElementMask,
	const FPrimitiveSceneProxy* RESTRICT PrimitiveSceneProxy,
	const FMaterialRenderProxy& RESTRICT MaterialRenderProxy,
	const FMaterial& RESTRICT MaterialResource,
	FMaterialShadingModelField ShadingModels,
	const FUniformLightMapPolicy& RESTRICT LightMapPolicy,
	const typename FUniformLightMapPolicy::ElementDataType& RESTRICT LightMapElementData)
{

	const bool bMaterialsCompiled = GCompileRayTracingMaterialAHS || GCompileRayTracingMaterialCHS;

	const FVertexFactory* VertexFactory = MeshBatch.VertexFactory;

	TMeshProcessorShaders<
		FMeshMaterialShader,
		FMeshMaterialShader,
		FMeshMaterialShader,
		FMeshMaterialShader,
		FMeshMaterialShader,
		FMaterialCHS> RayTracingShaders;

	if (bMaterialsCompiled)
	{
		const bool bUseTextureLOD = bool(GRayTracingUseTextureLod);

		switch (LightMapPolicy.GetIndirectPolicy())
		{
		case LMP_PRECOMPUTED_IRRADIANCE_VOLUME_INDIRECT_LIGHTING:
			RayTracingShaders.RayHitGroupShader = GetMaterialHitShader<TUniformLightMapPolicy<LMP_PRECOMPUTED_IRRADIANCE_VOLUME_INDIRECT_LIGHTING>>(MaterialResource, VertexFactory, bUseTextureLOD);
			break;
		case LMP_LQ_LIGHTMAP:
			RayTracingShaders.RayHitGroupShader = GetMaterialHitShader<TUniformLightMapPolicy<LMP_LQ_LIGHTMAP>>(MaterialResource, VertexFactory, bUseTextureLOD);
			break;
		case LMP_HQ_LIGHTMAP:
			RayTracingShaders.RayHitGroupShader = GetMaterialHitShader<TUniformLightMapPolicy<LMP_HQ_LIGHTMAP>>(MaterialResource, VertexFactory, bUseTextureLOD);
			break;
		case LMP_DISTANCE_FIELD_SHADOWS_AND_HQ_LIGHTMAP:
			RayTracingShaders.RayHitGroupShader = GetMaterialHitShader<TUniformLightMapPolicy<LMP_DISTANCE_FIELD_SHADOWS_AND_HQ_LIGHTMAP>>(MaterialResource, VertexFactory, bUseTextureLOD);
			break;
		case LMP_NO_LIGHTMAP:
			RayTracingShaders.RayHitGroupShader = GetMaterialHitShader<TUniformLightMapPolicy<LMP_NO_LIGHTMAP>>(MaterialResource, VertexFactory, bUseTextureLOD);
			break;
		default:
			check(false);
		}
	}
	else
	{
		RayTracingShaders.RayHitGroupShader = MaterialResource.GetShader<FTrivialMaterialCHS>(VertexFactory->GetType());
	}

	PassDrawRenderState.SetBlendState(TStaticBlendState<CW_RGBA, BO_Add, BF_One, BF_One, BO_Add, BF_Zero, BF_One>::GetRHI());
	PassDrawRenderState.SetDepthStencilState(TStaticDepthStencilState<false, CF_DepthNearOrEqual>::GetRHI());

	TBasePassShaderElementData<FUniformLightMapPolicy> ShaderElementData(LightMapElementData);
	ShaderElementData.InitializeMeshMaterialData(ViewIfDynamicMeshCommand, PrimitiveSceneProxy, MeshBatch, -1, true);

	BuildRayTracingMeshCommands(
		MeshBatch,
		BatchElementMask,
		PrimitiveSceneProxy,
		MaterialRenderProxy,
		MaterialResource,
		PassDrawRenderState,
		RayTracingShaders,
		ShaderElementData);
}

void FRayTracingMeshProcessor::AddMeshBatch(const FMeshBatch& RESTRICT MeshBatch, uint64 BatchElementMask, const FPrimitiveSceneProxy* RESTRICT PrimitiveSceneProxy)
{
	// Caveat: there are also branches not emitting any MDC
	if (MeshBatch.bUseForMaterial && IsSupportedVertexFactoryType(MeshBatch.VertexFactory->GetType()))
	{
		// Determine the mesh's material and blend mode.
		const FMaterialRenderProxy* FallbackMaterialRenderProxyPtr = nullptr;
		const FMaterial& Material = MeshBatch.MaterialRenderProxy->GetMaterialWithFallback(FeatureLevel, FallbackMaterialRenderProxyPtr);

		const FMaterialRenderProxy& MaterialRenderProxy = FallbackMaterialRenderProxyPtr ? *FallbackMaterialRenderProxyPtr : *MeshBatch.MaterialRenderProxy;

		const EBlendMode BlendMode = Material.GetBlendMode();
		const FMaterialShadingModelField ShadingModels = Material.GetShadingModels();

		// Only draw opaque materials.
		if ((!PrimitiveSceneProxy || PrimitiveSceneProxy->ShouldRenderInMainPass())
			&& ShouldIncludeDomainInMeshPass(Material.GetMaterialDomain()))
		{
			// Check for a cached light-map.
			const bool bIsLitMaterial = ShadingModels.IsLit();
			static const auto AllowStaticLightingVar = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.AllowStaticLighting"));
			const bool bAllowStaticLighting = (!AllowStaticLightingVar || AllowStaticLightingVar->GetValueOnRenderThread() != 0);

			const FLightMapInteraction LightMapInteraction = (bAllowStaticLighting && MeshBatch.LCI && bIsLitMaterial)
				? MeshBatch.LCI->GetLightMapInteraction(FeatureLevel)
				: FLightMapInteraction();

			// force LQ lightmaps based on system settings
			const bool bPlatformAllowsHighQualityLightMaps = AllowHighQualityLightmaps(FeatureLevel);
			const bool bAllowHighQualityLightMaps = bPlatformAllowsHighQualityLightMaps && LightMapInteraction.AllowsHighQualityLightmaps();

			const bool bAllowIndirectLightingCache = Scene && Scene->PrecomputedLightVolumes.Num() > 0;
			const bool bUseVolumetricLightmap = Scene && Scene->VolumetricLightmapSceneData.HasData();

			{
				static const auto CVarSupportLowQualityLightmap = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.SupportLowQualityLightmaps"));
				const bool bAllowLowQualityLightMaps = (!CVarSupportLowQualityLightmap) || (CVarSupportLowQualityLightmap->GetValueOnAnyThread() != 0);

				switch (LightMapInteraction.GetType())
				{
				case LMIT_Texture:
					if (bAllowHighQualityLightMaps)
					{
						const FShadowMapInteraction ShadowMapInteraction = (bAllowStaticLighting && MeshBatch.LCI && bIsLitMaterial)
							? MeshBatch.LCI->GetShadowMapInteraction(FeatureLevel)
							: FShadowMapInteraction();

						if (ShadowMapInteraction.GetType() == SMIT_Texture)
						{
							Process(
								MeshBatch,
								BatchElementMask,
								PrimitiveSceneProxy,
								MaterialRenderProxy,
								Material,
								ShadingModels,
								FUniformLightMapPolicy(LMP_DISTANCE_FIELD_SHADOWS_AND_HQ_LIGHTMAP),
								MeshBatch.LCI);
						}
						else
						{
							Process(
								MeshBatch,
								BatchElementMask,
								PrimitiveSceneProxy,
								MaterialRenderProxy,
								Material,
								ShadingModels,
								FUniformLightMapPolicy(LMP_HQ_LIGHTMAP),
								MeshBatch.LCI);
						}
					}
					else if (bAllowLowQualityLightMaps)
					{
						Process(
							MeshBatch,
							BatchElementMask,
							PrimitiveSceneProxy,
							MaterialRenderProxy,
							Material,
							ShadingModels,
							FUniformLightMapPolicy(LMP_LQ_LIGHTMAP),
							MeshBatch.LCI);
					}
					else
					{
						Process(
							MeshBatch,
							BatchElementMask,
							PrimitiveSceneProxy,
							MaterialRenderProxy,
							Material,
							ShadingModels,
							FUniformLightMapPolicy(LMP_NO_LIGHTMAP),
							MeshBatch.LCI);
					}
					break;
				default:
					if (bIsLitMaterial
						&& bAllowStaticLighting
						&& Scene
						&& Scene->VolumetricLightmapSceneData.HasData()
						&& PrimitiveSceneProxy
						&& (PrimitiveSceneProxy->IsMovable()
							|| PrimitiveSceneProxy->NeedsUnbuiltPreviewLighting()
							|| PrimitiveSceneProxy->GetLightmapType() == ELightmapType::ForceVolumetric))
					{
						Process(
							MeshBatch,
							BatchElementMask,
							PrimitiveSceneProxy,
							MaterialRenderProxy,
							Material,
							ShadingModels,
							FUniformLightMapPolicy(LMP_PRECOMPUTED_IRRADIANCE_VOLUME_INDIRECT_LIGHTING),
							MeshBatch.LCI);
					}
					else
					{
						Process(
							MeshBatch,
							BatchElementMask,
							PrimitiveSceneProxy,
							MaterialRenderProxy,
							Material,
							ShadingModels,
							FUniformLightMapPolicy(LMP_NO_LIGHTMAP),
							MeshBatch.LCI);
					}
					break;
				};
			}
		}
	}
}

static bool IsCompatibleFallbackPipelineSignature(FRayTracingPipelineStateSignature& B, FRayTracingPipelineStateSignature& A)
{
	// Compare everything except hit group table
	return A.MaxPayloadSizeInBytes == B.MaxPayloadSizeInBytes
		&& A.bAllowHitGroupIndexing == B.bAllowHitGroupIndexing
		&& A.GetRayGenHash() == B.GetRayGenHash()
		&& A.GetRayMissHash() == B.GetRayMissHash()
		&& A.GetCallableHash() == B.GetCallableHash();
}

static bool PipelineContainsHitShaders(FRayTracingPipelineState* Pipeline, const TArrayView<FRHIRayTracingShader*>& Shaders)
{
	for (FRHIRayTracingShader* Shader : Shaders)
	{
		int32 Index = FindRayTracingHitGroupIndex(Pipeline, Shader, false);
		if (Index == INDEX_NONE)
		{
			return false;
		}
	}
	return true;
}

FRayTracingPipelineState* FDeferredShadingSceneRenderer::BindRayTracingMaterialPipeline(
	FRHICommandList& RHICmdList,
	FViewInfo& View,
	const TArrayView<FRHIRayTracingShader*>& RayGenShaderTable,
	FRHIRayTracingShader* DefaultClosestHitShader
)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FDeferredShadingSceneRenderer::BindRayTracingMaterialPipeline);
	SCOPE_CYCLE_COUNTER(STAT_BindRayTracingPipeline);

	FRayTracingPipelineStateInitializer Initializer;

	Initializer.MaxPayloadSizeInBytes = 64; // sizeof(FPackedMaterialClosestHitPayload)
	Initializer.bAllowHitGroupIndexing = true;

	const bool bLightingMissShader = CanUseRayTracingLightingMissShader(View.GetShaderPlatform());

	FRHIRayTracingShader* DefaultMissShader = View.ShaderMap->GetShader<FPackedMaterialClosestHitPayloadMS>().GetRayTracingShader();

	FRHIRayTracingShader* RayTracingMissShaderLibrary[RAY_TRACING_NUM_MISS_SHADER_SLOTS] = {};
	RayTracingMissShaderLibrary[RAY_TRACING_MISS_SHADER_SLOT_DEFAULT] = DefaultMissShader;
	RayTracingMissShaderLibrary[RAY_TRACING_MISS_SHADER_SLOT_LIGHTING] = bLightingMissShader ? GetRayTracingLightingMissShader(View) : DefaultMissShader;
	Initializer.SetMissShaderTable(RayTracingMissShaderLibrary);

	Initializer.SetRayGenShaderTable(RayGenShaderTable);

	const bool bMaterialsCompiled = GCompileRayTracingMaterialAHS || GCompileRayTracingMaterialCHS;
	const bool bEnableMaterials = bMaterialsCompiled && GEnableRayTracingMaterials != 0;
	static auto CVarEnableShadowMaterials = IConsoleManager::Get().FindConsoleVariable(TEXT("r.RayTracing.Shadows.EnableMaterials"));
	const bool bEnableShadowMaterials = bMaterialsCompiled && (CVarEnableShadowMaterials ? CVarEnableShadowMaterials->GetInt() != 0 : true);

	TArray<FRHIRayTracingShader*> RayTracingMaterialLibrary;

	if (bEnableMaterials)
	{
		FShaderMapResource::GetRayTracingMaterialLibrary(RayTracingMaterialLibrary, DefaultClosestHitShader);
	}

	FRHIRayTracingShader* OpaqueShadowShader = View.ShaderMap->GetShader<FOpaqueShadowHitGroup>().GetRayTracingShader();
	FRHIRayTracingShader* HiddenMaterialShader = View.ShaderMap->GetShader<FHiddenMaterialHitGroup>().GetRayTracingShader();

	FRHIRayTracingShader* RequiredHitShaders[] =
	{
		DefaultClosestHitShader,
		OpaqueShadowShader,
		HiddenMaterialShader
	};

	for (FRHIRayTracingShader* Shader : RequiredHitShaders)
	{
		RayTracingMaterialLibrary.Add(Shader);
	}

	Initializer.SetHitGroupTable(RayTracingMaterialLibrary);

	FRayTracingPipelineState* FallbackPipelineState = GRayTracingNonBlockingPipelineCreation && View.ViewState
		? PipelineStateCache::GetRayTracingPipelineState(View.ViewState->LastRayTracingMaterialPipelineSignature)
		: nullptr;

	ERayTracingPipelineCacheFlags PipelineCacheFlags = ERayTracingPipelineCacheFlags::Default;
	if (GRayTracingNonBlockingPipelineCreation
		&& FallbackPipelineState
		&& View.ViewState
		&& IsCompatibleFallbackPipelineSignature(View.ViewState->LastRayTracingMaterialPipelineSignature, Initializer)
		&& PipelineContainsHitShaders(FallbackPipelineState, RequiredHitShaders))
	{
		PipelineCacheFlags |= ERayTracingPipelineCacheFlags::NonBlocking;
	}

	FRayTracingPipelineState* PipelineState = PipelineStateCache::GetAndOrCreateRayTracingPipelineState(RHICmdList, Initializer, PipelineCacheFlags);

	if (PipelineState)
	{
		if (View.ViewState)
		{
			// Save the current pipeline to be used as fallback in future frames
			View.ViewState->LastRayTracingMaterialPipelineSignature = static_cast<FRayTracingPipelineStateSignature&>(Initializer);
		}
	}
	else
	{
		// If pipeline was not found in cache, use the fallback from previous frame
		check(FallbackPipelineState);
		PipelineState = FallbackPipelineState;
	}

	check(PipelineState);

	const int32 DefaultClosestHitMaterialIndex = FindRayTracingHitGroupIndex(PipelineState, DefaultClosestHitShader, true);
	const int32 OpaqueShadowMaterialIndex = FindRayTracingHitGroupIndex(PipelineState, OpaqueShadowShader, true);
	const int32 HiddenMaterialIndex = FindRayTracingHitGroupIndex(PipelineState, HiddenMaterialShader, true);

	FViewInfo& ReferenceView = Views[0];

	const uint32 NumTotalMeshCommands = ReferenceView.VisibleRayTracingMeshCommands.Num();
	const uint32 TargetCommandsPerTask = 4096; // Granularity chosen based on profiling Infiltrator scene to balance wall time speedup and total CPU thread time.
	const uint32 NumTasks = FMath::Max(1u, FMath::DivideAndRoundUp(NumTotalMeshCommands, TargetCommandsPerTask));
	const uint32 CommandsPerTask = FMath::DivideAndRoundUp(NumTotalMeshCommands, NumTasks); // Evenly divide commands between tasks (avoiding potential short last task)

	FGraphEventArray TaskList;
	TaskList.Reserve(NumTasks);
	View.RayTracingMaterialBindings.SetNum(NumTasks);
	
	for (uint32 TaskIndex = 0; TaskIndex < NumTasks; ++TaskIndex)
	{
		const uint32 FirstTaskCommandIndex = TaskIndex * CommandsPerTask;
		const FVisibleRayTracingMeshCommand* MeshCommands = ReferenceView.VisibleRayTracingMeshCommands.GetData() + FirstTaskCommandIndex;
		const uint32 NumCommands = FMath::Min(CommandsPerTask, NumTotalMeshCommands - FirstTaskCommandIndex);

		FRayTracingLocalShaderBindingWriter* BindingWriter = new FRayTracingLocalShaderBindingWriter();
		View.RayTracingMaterialBindings[TaskIndex] = BindingWriter;

		TaskList.Add(FFunctionGraphTask::CreateAndDispatchWhenReady(
		[PipelineState, BindingWriter, MeshCommands, NumCommands, bEnableMaterials, bEnableShadowMaterials,
			DefaultClosestHitMaterialIndex, OpaqueShadowMaterialIndex, HiddenMaterialIndex, TaskIndex]()
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(BindRayTracingMaterialPipelineTask);

			for (uint32 CommandIndex = 0; CommandIndex < NumCommands; ++CommandIndex)
			{
				const FVisibleRayTracingMeshCommand VisibleMeshCommand = MeshCommands[CommandIndex];
				const FRayTracingMeshCommand& MeshCommand = *VisibleMeshCommand.RayTracingMeshCommand;

				int32 HitGroupIndex = DefaultClosestHitMaterialIndex; // Force the same shader to be used on all geometry unless materials are enabled

				if (bEnableMaterials)
				{
					const int32 FoundIndex = FindRayTracingHitGroupIndex(PipelineState, MeshCommand.MaterialShader, false);
					if (FoundIndex != INDEX_NONE)
					{
						HitGroupIndex = FoundIndex;
					}
					else
					{
						HitGroupIndex = DefaultClosestHitMaterialIndex;
					}
				}

				// Bind primary material shader

				{
					MeshCommand.ShaderBindings.SetRayTracingShaderBindingsForHitGroup(BindingWriter,
						VisibleMeshCommand.InstanceIndex,
						MeshCommand.GeometrySegmentIndex,
						HitGroupIndex,
						RAY_TRACING_SHADER_SLOT_MATERIAL);
				}

				// Bind shadow shader

				if (MeshCommand.bCastRayTracedShadows)
				{
					if (MeshCommand.bOpaque || !bEnableShadowMaterials)
					{
						FRayTracingLocalShaderBindings& Binding = BindingWriter->AddWithExternalParameters();
						Binding.InstanceIndex = VisibleMeshCommand.InstanceIndex;
						Binding.SegmentIndex = MeshCommand.GeometrySegmentIndex;
						Binding.ShaderSlot = RAY_TRACING_SHADER_SLOT_SHADOW;
						Binding.ShaderIndexInPipeline = OpaqueShadowMaterialIndex;
					}
					else
					{
						// Masked materials require full material evaluation with any-hit shader.
						// Full CHS is bound, however material evaluation is skipped for shadow rays using a dynamic branch on a ray payload flag.
						MeshCommand.ShaderBindings.SetRayTracingShaderBindingsForHitGroup(BindingWriter,
							VisibleMeshCommand.InstanceIndex,
							MeshCommand.GeometrySegmentIndex,
							HitGroupIndex,
							RAY_TRACING_SHADER_SLOT_SHADOW);
					}
				}
				else
				{
					FRayTracingLocalShaderBindings& Binding = BindingWriter->AddWithExternalParameters();
					Binding.InstanceIndex = VisibleMeshCommand.InstanceIndex;
					Binding.SegmentIndex = MeshCommand.GeometrySegmentIndex;
					Binding.ShaderSlot = RAY_TRACING_SHADER_SLOT_SHADOW;
					Binding.ShaderIndexInPipeline = HiddenMaterialIndex;
				}
			}
		},
		TStatId(), nullptr, ENamedThreads::AnyThread));
	}

	View.RayTracingMaterialBindingsTask = FFunctionGraphTask::CreateAndDispatchWhenReady([]() {}, TStatId(), &TaskList, ENamedThreads::AnyHiPriThreadHiPriTask);

	return PipelineState;
}

#endif // RHI_RAYTRACING
