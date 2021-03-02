// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	SkyPassRendering.cpp: Sky pass rendering implementation.
=============================================================================*/

#include "SkyPassRendering.h"
#include "BasePassRendering.h"
#include "MobileBasePassRendering.h"
#include "DeferredShadingRenderer.h"
#include "ScenePrivate.h"
#include "SceneRendering.h"
#include "MeshPassProcessor.inl"



FSkyPassMeshProcessor::FSkyPassMeshProcessor(const FScene* Scene, const FSceneView* InViewIfDynamicMeshCommand, const FMeshPassProcessorRenderState& InPassDrawRenderState, FMeshPassDrawListContext* InDrawListContext)
	: FMeshPassProcessor(Scene, Scene->GetFeatureLevel(), InViewIfDynamicMeshCommand, InDrawListContext)
	, PassDrawRenderState(InPassDrawRenderState)
{
}

void FSkyPassMeshProcessor::AddMeshBatch(const FMeshBatch& RESTRICT MeshBatch, uint64 BatchElementMask, const FPrimitiveSceneProxy* RESTRICT PrimitiveSceneProxy, int32 StaticMeshId)
{
	// Determine the mesh's material and blend mode.
	const FMaterialRenderProxy* FallbackMaterialRenderProxyPtr = nullptr;
	const FMaterial& Material = MeshBatch.MaterialRenderProxy->GetMaterialWithFallback(FeatureLevel, FallbackMaterialRenderProxyPtr);

	if (Material.IsSky())
	{
		const FMeshDrawingPolicyOverrideSettings OverrideSettings = ComputeMeshOverrideSettings(MeshBatch);
		const ERasterizerFillMode MeshFillMode = ComputeMeshFillMode(MeshBatch, Material, OverrideSettings);
		const ERasterizerCullMode MeshCullMode = ComputeMeshCullMode(MeshBatch, Material, OverrideSettings);
		const FMaterialRenderProxy& MaterialRenderProxy = FallbackMaterialRenderProxyPtr ? *FallbackMaterialRenderProxyPtr : *MeshBatch.MaterialRenderProxy;
		Process(MeshBatch, BatchElementMask, PrimitiveSceneProxy, StaticMeshId, MaterialRenderProxy, Material, MeshFillMode, MeshCullMode);
	}
}

void FSkyPassMeshProcessor::Process(
	const FMeshBatch& MeshBatch,
	uint64 BatchElementMask,
	const FPrimitiveSceneProxy* RESTRICT PrimitiveSceneProxy,
	int32 StaticMeshId,
	const FMaterialRenderProxy& RESTRICT MaterialRenderProxy,
	const FMaterial& RESTRICT MaterialResource,
	ERasterizerFillMode MeshFillMode,
	ERasterizerCullMode MeshCullMode)
{
	typedef FUniformLightMapPolicy LightMapPolicyType;
	FUniformLightMapPolicy NoLightmapPolicy(LMP_NO_LIGHTMAP);
	const FVertexFactory* VertexFactory = MeshBatch.VertexFactory;

	if (Scene->GetShadingPath()==EShadingPath::Deferred)
	{
		TMeshProcessorShaders<
			TBasePassVertexShaderPolicyParamType<LightMapPolicyType>,
			FBaseHS,
			FBaseDS,
			TBasePassPixelShaderPolicyParamType<LightMapPolicyType>> SkyPassShaders;

		const bool bRenderSkylight = false;
		const bool bRenderAtmosphericFog = false;
		GetBasePassShaders<LightMapPolicyType>(
			MaterialResource,
			VertexFactory->GetType(),
			NoLightmapPolicy,
			FeatureLevel,
			bRenderAtmosphericFog,
			bRenderSkylight,
			false,
			SkyPassShaders.HullShader,
			SkyPassShaders.DomainShader,
			SkyPassShaders.VertexShader,
			SkyPassShaders.PixelShader
			);

		TBasePassShaderElementData<LightMapPolicyType> ShaderElementData(nullptr);
		ShaderElementData.InitializeMeshMaterialData(ViewIfDynamicMeshCommand, PrimitiveSceneProxy, MeshBatch, StaticMeshId, false);

		const FMeshDrawCommandSortKey SortKey = CalculateMeshStaticSortKey(SkyPassShaders.VertexShader, SkyPassShaders.PixelShader);

		BuildMeshDrawCommands(
			MeshBatch,
			BatchElementMask,
			PrimitiveSceneProxy,
			MaterialRenderProxy,
			MaterialResource,
			PassDrawRenderState,
			SkyPassShaders,
			MeshFillMode,
			MeshCullMode,
			SortKey,
			EMeshPassFeatures::Default,
			ShaderElementData);
	}
	else
	{
		TMeshProcessorShaders<
			TMobileBasePassVSPolicyParamType<LightMapPolicyType>,
			FBaseHS,
			FBaseDS,
			TMobileBasePassPSPolicyParamType<LightMapPolicyType>> SkyPassShaders;

		MobileBasePass::GetShaders(
			LMP_NO_LIGHTMAP,
			0,
			MaterialResource,
			VertexFactory->GetType(),
			false,
			SkyPassShaders.VertexShader,
			SkyPassShaders.PixelShader
		);

		TMobileBasePassShaderElementData<LightMapPolicyType> ShaderElementData(nullptr);
		ShaderElementData.InitializeMeshMaterialData(ViewIfDynamicMeshCommand, PrimitiveSceneProxy, MeshBatch, StaticMeshId, false);

		const FMeshDrawCommandSortKey SortKey = CalculateMeshStaticSortKey(SkyPassShaders.VertexShader, SkyPassShaders.PixelShader);

		BuildMeshDrawCommands(
			MeshBatch,
			BatchElementMask,
			PrimitiveSceneProxy,
			MaterialRenderProxy,
			MaterialResource,
			PassDrawRenderState,
			SkyPassShaders,
			MeshFillMode,
			MeshCullMode,
			SortKey,
			EMeshPassFeatures::Default,
			ShaderElementData);
	}
}

FMeshPassProcessor* CreateSkyPassProcessor(const FScene* Scene, const FSceneView* InViewIfDynamicMeshCommand, FMeshPassDrawListContext* InDrawListContext)
{
	FMeshPassProcessorRenderState DrawRenderState(Scene->UniformBuffers.ViewUniformBuffer);
	DrawRenderState.SetInstancedViewUniformBuffer(Scene->UniformBuffers.InstancedViewUniformBuffer);

	if (Scene->GetShadingPath() == EShadingPath::Mobile)
	{
		DrawRenderState.SetPassUniformBuffer(Scene->UniformBuffers.MobileOpaqueBasePassUniformBuffer);
	}

	FExclusiveDepthStencil::Type BasePassDepthStencilAccess_NoDepthWrite = FExclusiveDepthStencil::Type(Scene->DefaultBasePassDepthStencilAccess & ~FExclusiveDepthStencil::DepthWrite);
	SetupBasePassState(BasePassDepthStencilAccess_NoDepthWrite, false, DrawRenderState);

	return new(FMemStack::Get()) FSkyPassMeshProcessor(Scene, InViewIfDynamicMeshCommand, DrawRenderState, InDrawListContext);
}

FRegisterPassProcessorCreateFunction RegisterSkyPass(&CreateSkyPassProcessor, EShadingPath::Deferred, EMeshPass::SkyPass, EMeshPassFlags::MainView);
// Mobile skypass is only active if mobile has a full depth pass
FRegisterPassProcessorCreateFunction RegisterMobileSkyPass(&CreateSkyPassProcessor, EShadingPath::Mobile, EMeshPass::SkyPass, EMeshPassFlags::MainView);
