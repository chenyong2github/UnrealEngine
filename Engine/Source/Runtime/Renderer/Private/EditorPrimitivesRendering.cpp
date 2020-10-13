// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	EditorPrimitivesRendering
=============================================================================*/

#include "EditorPrimitivesRendering.h"
#include "BasePassRendering.h"
#include "ScenePrivate.h"
#include "MobileBasePassRendering.h"
#include "MeshPassProcessor.inl"

FEditorPrimitivesBasePassMeshProcessor::FEditorPrimitivesBasePassMeshProcessor(const FScene* Scene, ERHIFeatureLevel::Type InFeatureLevel, const FSceneView* InViewIfDynamicMeshCommand, const FMeshPassProcessorRenderState& InDrawRenderState, bool bInTranslucentBasePass, FMeshPassDrawListContext* InDrawListContext) 
	: FMeshPassProcessor(Scene, InFeatureLevel, InViewIfDynamicMeshCommand, InDrawListContext)
	, PassDrawRenderState(InDrawRenderState)
	, bTranslucentBasePass(bInTranslucentBasePass)
{}

void FEditorPrimitivesBasePassMeshProcessor::AddMeshBatch(const FMeshBatch& RESTRICT MeshBatch, uint64 BatchElementMask, const FPrimitiveSceneProxy* RESTRICT PrimitiveSceneProxy, int32 StaticMeshId)
{
	if (MeshBatch.bUseForMaterial)
	{
		const FMaterialRenderProxy* MaterialRenderProxy = MeshBatch.MaterialRenderProxy;
		while (MaterialRenderProxy)
		{
			const FMaterial* Material = MaterialRenderProxy->GetMaterialNoFallback(FeatureLevel);
			if (Material && Material->GetRenderingThreadShaderMap())
			{
				if (TryAddMeshBatch(MeshBatch, BatchElementMask, PrimitiveSceneProxy, StaticMeshId, *MaterialRenderProxy, *Material))
				{
					break;
				}
			}

			MaterialRenderProxy = MaterialRenderProxy->GetFallback(FeatureLevel);
		}
	}
}

bool FEditorPrimitivesBasePassMeshProcessor::TryAddMeshBatch(const FMeshBatch& RESTRICT MeshBatch, uint64 BatchElementMask, const FPrimitiveSceneProxy* RESTRICT PrimitiveSceneProxy, int32 StaticMeshId, const FMaterialRenderProxy& MaterialRenderProxy, const FMaterial& Material)
{
	const EBlendMode BlendMode = Material.GetBlendMode();
	const bool bIsTranslucent = IsTranslucentBlendMode(BlendMode);

	bool bResult = true;
	if (bIsTranslucent == bTranslucentBasePass
		&& (!PrimitiveSceneProxy || PrimitiveSceneProxy->ShouldRenderInMainPass())
		&& ShouldIncludeDomainInMeshPass(Material.GetMaterialDomain()))
	{
		if (Scene->GetShadingPath(FeatureLevel) == EShadingPath::Mobile)
		{
			bResult = ProcessMobileShadingPath(MeshBatch, BatchElementMask, Material, MaterialRenderProxy, PrimitiveSceneProxy, StaticMeshId);
		}
		else
		{
			bResult = ProcessDeferredShadingPath(MeshBatch, BatchElementMask, Material, MaterialRenderProxy, PrimitiveSceneProxy, StaticMeshId);
		}
	}
	return bResult;
}

bool FEditorPrimitivesBasePassMeshProcessor::ProcessDeferredShadingPath(const FMeshBatch& MeshBatch, uint64 BatchElementMask, const FMaterial& Material, const FMaterialRenderProxy& MaterialRenderProxy, const FPrimitiveSceneProxy* PrimitiveSceneProxy, int32 StaticMeshId)
{
	FUniformLightMapPolicy NoLightmapPolicy(LMP_NO_LIGHTMAP);
	typedef FUniformLightMapPolicy LightMapPolicyType;

	const FVertexFactory* VertexFactory = MeshBatch.VertexFactory;
	const bool bRenderSkylight = false;
	const bool bRenderAtmosphericFog = false;

	TMeshProcessorShaders<
		TBasePassVertexShaderPolicyParamType<LightMapPolicyType>,
		FBaseHS,
		FBaseDS,
		TBasePassPixelShaderPolicyParamType<LightMapPolicyType>> BasePassShaders;

	if (!GetBasePassShaders<LightMapPolicyType>(
		Material,
		VertexFactory->GetType(),
		NoLightmapPolicy,
		FeatureLevel,
		bRenderAtmosphericFog,
		bRenderSkylight,
		false,
		BasePassShaders.HullShader,
		BasePassShaders.DomainShader,
		BasePassShaders.VertexShader,
		BasePassShaders.PixelShader
		))
	{
		return false;
	}

	FMeshPassProcessorRenderState DrawRenderState(PassDrawRenderState);

	if (bTranslucentBasePass)
	{
		extern void SetTranslucentRenderState(FMeshPassProcessorRenderState& DrawRenderState, const FMaterial& Material, const EShaderPlatform Platform, ETranslucencyPass::Type InTranslucencyPassType);
		SetTranslucentRenderState(DrawRenderState, Material, Scene->GetShaderPlatform(), ETranslucencyPass::TPT_StandardTranslucency);
	}

	const FMeshDrawingPolicyOverrideSettings OverrideSettings = ComputeMeshOverrideSettings(MeshBatch);
	ERasterizerFillMode MeshFillMode = ComputeMeshFillMode(MeshBatch, Material, OverrideSettings);
	ERasterizerCullMode MeshCullMode = ComputeMeshCullMode(MeshBatch, Material, OverrideSettings);
	
	TBasePassShaderElementData<LightMapPolicyType> ShaderElementData(nullptr);
	ShaderElementData.InitializeMeshMaterialData(ViewIfDynamicMeshCommand, PrimitiveSceneProxy, MeshBatch, StaticMeshId, false);

	const FMeshDrawCommandSortKey SortKey = CalculateMeshStaticSortKey(BasePassShaders.VertexShader, BasePassShaders.PixelShader);

	BuildMeshDrawCommands(
		MeshBatch,
		BatchElementMask,
		PrimitiveSceneProxy,
		MaterialRenderProxy,
		Material,
		DrawRenderState,
		BasePassShaders,
		MeshFillMode,
		MeshCullMode,
		SortKey,
		EMeshPassFeatures::Default,
		ShaderElementData);

	return true;
}

bool FEditorPrimitivesBasePassMeshProcessor::ProcessMobileShadingPath(const FMeshBatch& MeshBatch, uint64 BatchElementMask, const FMaterial& Material, const FMaterialRenderProxy& MaterialRenderProxy, const FPrimitiveSceneProxy* PrimitiveSceneProxy, int32 StaticMeshId)
{
	FUniformLightMapPolicy NoLightmapPolicy(LMP_NO_LIGHTMAP);
	typedef FUniformLightMapPolicy LightMapPolicyType;

	const FVertexFactory* VertexFactory = MeshBatch.VertexFactory;
	const int32 NumMovablePointLights = 0;
	const bool bEnableSkyLight = false;

	TMeshProcessorShaders<
		TMobileBasePassVSPolicyParamType<FUniformLightMapPolicy>,
		FBaseHS,
		FBaseDS,
		TMobileBasePassPSPolicyParamType<FUniformLightMapPolicy>> BasePassShaders;
	if (!MobileBasePass::GetShaders(
		NoLightmapPolicy.GetIndirectPolicy(),
		NumMovablePointLights,
		Material,
		VertexFactory->GetType(),
		bEnableSkyLight,
		BasePassShaders.VertexShader,
		BasePassShaders.PixelShader))
	{
		return false;
	}
	
	FMeshPassProcessorRenderState DrawRenderState(PassDrawRenderState);

	if (bTranslucentBasePass)
	{
		MobileBasePass::SetTranslucentRenderState(DrawRenderState, Material);
	}

	const FMeshDrawingPolicyOverrideSettings OverrideSettings = ComputeMeshOverrideSettings(MeshBatch);
	ERasterizerFillMode MeshFillMode = ComputeMeshFillMode(MeshBatch, Material, OverrideSettings);
	ERasterizerCullMode MeshCullMode = ComputeMeshCullMode(MeshBatch, Material, OverrideSettings);
	
	TMobileBasePassShaderElementData<LightMapPolicyType> ShaderElementData(nullptr);
	ShaderElementData.InitializeMeshMaterialData(ViewIfDynamicMeshCommand, PrimitiveSceneProxy, MeshBatch, StaticMeshId, false);

	const FMeshDrawCommandSortKey SortKey = CalculateMeshStaticSortKey(BasePassShaders.VertexShader, BasePassShaders.PixelShader);

	BuildMeshDrawCommands(
		MeshBatch,
		BatchElementMask,
		PrimitiveSceneProxy,
		MaterialRenderProxy,
		Material,
		DrawRenderState,
		BasePassShaders,
		MeshFillMode,
		MeshCullMode,
		SortKey,
		EMeshPassFeatures::Default,
		ShaderElementData);

	return true;
}