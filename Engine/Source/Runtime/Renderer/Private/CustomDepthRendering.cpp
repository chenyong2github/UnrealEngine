// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	CustomDepthRendering.cpp: CustomDepth rendering implementation.
=============================================================================*/

#include "SceneUtils.h"
#include "DepthRendering.h"
#include "SceneRendering.h"
#include "SceneCore.h"
#include "ScenePrivate.h"
#include "MeshPassProcessor.inl"

class FCustomDepthPassMeshProcessor : public FMeshPassProcessor
{
public:
	FCustomDepthPassMeshProcessor(const FScene* Scene, const FSceneView* InViewIfDynamicMeshCommand, FMeshPassDrawListContext* InDrawListContext);

	virtual void AddMeshBatch(const FMeshBatch& RESTRICT MeshBatch, uint64 BatchElementMask, const FPrimitiveSceneProxy* RESTRICT PrimitiveSceneProxy, int32 StaticMeshId = -1) override final;

private:
	template<bool bPositionOnly, bool bUsesMobileColorValue>
	void Process(
		const FMeshBatch& MeshBatch,
		uint64 BatchElementMask,
		int32 StaticMeshId,
		const FPrimitiveSceneProxy* RESTRICT PrimitiveSceneProxy,
		const FMaterialRenderProxy& RESTRICT MaterialRenderProxy,
		const FMaterial& RESTRICT MaterialResource,
		ERasterizerFillMode MeshFillMode,
		ERasterizerCullMode MeshCullMode,
		float MobileColorValue);

	FMeshPassProcessorRenderState PassDrawRenderState;
};

FCustomDepthPassMeshProcessor::FCustomDepthPassMeshProcessor(const FScene* Scene, const FSceneView* InViewIfDynamicMeshCommand, FMeshPassDrawListContext* InDrawListContext)
	: FMeshPassProcessor(Scene, Scene->GetFeatureLevel(), InViewIfDynamicMeshCommand, InDrawListContext)
{
	PassDrawRenderState.SetViewUniformBuffer(Scene->UniformBuffers.CustomDepthViewUniformBuffer);
	PassDrawRenderState.SetInstancedViewUniformBuffer(Scene->UniformBuffers.InstancedCustomDepthViewUniformBuffer);
	PassDrawRenderState.SetBlendState(TStaticBlendState<>::GetRHI());
	PassDrawRenderState.SetDepthStencilState(TStaticDepthStencilState<true, CF_DepthNearOrEqual>::GetRHI());
}

void FCustomDepthPassMeshProcessor::AddMeshBatch(const FMeshBatch& RESTRICT MeshBatch, uint64 BatchElementMask, const FPrimitiveSceneProxy* RESTRICT PrimitiveSceneProxy, int32 StaticMeshId)
{
	if (PrimitiveSceneProxy->ShouldRenderCustomDepth())
	{
		// Determine the mesh's material and blend mode.
		const FMaterialRenderProxy* FallbackMaterialRenderProxyPtr = nullptr;
		const FMaterial& Material = MeshBatch.MaterialRenderProxy->GetMaterialWithFallback(FeatureLevel, FallbackMaterialRenderProxyPtr);
		const FMaterialRenderProxy& MaterialRenderProxy = FallbackMaterialRenderProxyPtr ? *FallbackMaterialRenderProxyPtr : *MeshBatch.MaterialRenderProxy;

		const EBlendMode BlendMode = Material.GetBlendMode();
		const FMeshDrawingPolicyOverrideSettings OverrideSettings = ComputeMeshOverrideSettings(MeshBatch);
		const ERasterizerFillMode MeshFillMode = ComputeMeshFillMode(MeshBatch, Material, OverrideSettings);
		const ERasterizerCullMode MeshCullMode = ComputeMeshCullMode(MeshBatch, Material, OverrideSettings);
		const bool bIsTranslucent = IsTranslucentBlendMode(BlendMode);


		const bool bWriteCustomStencilValues = FSceneRenderTargets::IsCustomDepthPassWritingStencil(FeatureLevel);
		float MobileColorValue = 0.0f;

		if (bWriteCustomStencilValues)
		{
			const uint32 CustomDepthStencilValue = PrimitiveSceneProxy->GetCustomDepthStencilValue();

			static FRHIDepthStencilState* StencilStates[EStencilMask::SM_Count] =
			{
				TStaticDepthStencilState<true, CF_DepthNearOrEqual, true, CF_Always, SO_Keep, SO_Keep, SO_Replace, false, CF_Always, SO_Keep, SO_Keep, SO_Keep, 255, 255>::GetRHI(),
				TStaticDepthStencilState<true, CF_DepthNearOrEqual, true, CF_Always, SO_Keep, SO_Replace, SO_Replace, false, CF_Always, SO_Keep, SO_Keep, SO_Keep, 255, 255>::GetRHI(),
				TStaticDepthStencilState<true, CF_DepthNearOrEqual, true, CF_Always, SO_Keep, SO_Replace, SO_Replace, false, CF_Always, SO_Keep, SO_Keep, SO_Keep, 255, 1>::GetRHI(),
				TStaticDepthStencilState<true, CF_DepthNearOrEqual, true, CF_Always, SO_Keep, SO_Replace, SO_Replace, false, CF_Always, SO_Keep, SO_Keep, SO_Keep, 255, 2>::GetRHI(),
				TStaticDepthStencilState<true, CF_DepthNearOrEqual, true, CF_Always, SO_Keep, SO_Replace, SO_Replace, false, CF_Always, SO_Keep, SO_Keep, SO_Keep, 255, 4>::GetRHI(),
				TStaticDepthStencilState<true, CF_DepthNearOrEqual, true, CF_Always, SO_Keep, SO_Replace, SO_Replace, false, CF_Always, SO_Keep, SO_Keep, SO_Keep, 255, 8>::GetRHI(),
				TStaticDepthStencilState<true, CF_DepthNearOrEqual, true, CF_Always, SO_Keep, SO_Replace, SO_Replace, false, CF_Always, SO_Keep, SO_Keep, SO_Keep, 255, 16>::GetRHI(),
				TStaticDepthStencilState<true, CF_DepthNearOrEqual, true, CF_Always, SO_Keep, SO_Replace, SO_Replace, false, CF_Always, SO_Keep, SO_Keep, SO_Keep, 255, 32>::GetRHI(),
				TStaticDepthStencilState<true, CF_DepthNearOrEqual, true, CF_Always, SO_Keep, SO_Replace, SO_Replace, false, CF_Always, SO_Keep, SO_Keep, SO_Keep, 255, 64>::GetRHI(),
				TStaticDepthStencilState<true, CF_DepthNearOrEqual, true, CF_Always, SO_Keep, SO_Replace, SO_Replace, false, CF_Always, SO_Keep, SO_Keep, SO_Keep, 255, 128>::GetRHI()
			};
			checkSlow(EStencilMask::SM_Count == UE_ARRAY_COUNT(StencilStates));

			PassDrawRenderState.SetDepthStencilState(StencilStates[(int32)PrimitiveSceneProxy->GetStencilWriteMask()]);
			PassDrawRenderState.SetStencilRef(CustomDepthStencilValue);

			if (FeatureLevel <= ERHIFeatureLevel::ES3_1)
			{
				// On mobile platforms write custom stencil value to color target
				MobileColorValue = CustomDepthStencilValue / 255.0f;
			}

			// If DepthStencilState isn't the default, then use it as the pass render state.
			EDepthStencilState DepthStencilState = PrimitiveSceneProxy->GetCustomDepthStencilState();
			if (DepthStencilState != EDepthStencilState::DSS_DepthTest_StencilAlways)
			{
				static FRHIDepthStencilState* _DepthStencilStates[EDepthStencilState::DDS_Count] =
				{
					TStaticDepthStencilState<true, CF_DepthNearOrEqual, true, CF_Always, SO_Keep, SO_Keep, SO_Replace>::GetRHI(),
					TStaticDepthStencilState<true, CF_Always,           true, CF_Always, SO_Keep, SO_Keep, SO_Replace>::GetRHI(),
					TStaticDepthStencilState<true, CF_DepthNearOrEqual, true, CF_Equal, SO_Keep, SO_Keep, SO_Invert>::GetRHI(),
					TStaticDepthStencilState<true, CF_Always,           true, CF_Equal, SO_Keep, SO_Keep, SO_Invert>::GetRHI()
				};
				checkSlow(EDepthStencilState::DDS_Count == UE_ARRAY_COUNT(_DepthStencilStates));

				PassDrawRenderState.SetDepthStencilState(_DepthStencilStates[(int32)DepthStencilState]);
				PassDrawRenderState.SetStencilRef(CustomDepthStencilValue);

				if (FeatureLevel <= ERHIFeatureLevel::ES3_1)
				{
					// On mobile platforms write custom stencil value to color target
					MobileColorValue = CustomDepthStencilValue / 255.0f;

					switch (DepthStencilState)
					{
					case DSS_DepthTest_StencilAlways:
					case DSS_DepthAlways_StencilAlways:
						break;
					case DDS_DepthTest_StencilEqual_Invert:
					case DDS_DepthAlways_StencilEqual_Invert:
						MobileColorValue = 1 - CustomDepthStencilValue / 255.0f;
						break;
					default:
						break;
					}
				}
			}
		}
		else
		{
			PassDrawRenderState.SetDepthStencilState(TStaticDepthStencilState<true, CF_DepthNearOrEqual>::GetRHI());
		}

		const bool bUsesMobileColorValue = FeatureLevel <= ERHIFeatureLevel::ES3_1;


		if (BlendMode == BLEND_Opaque
			&& MeshBatch.VertexFactory->SupportsPositionOnlyStream()
			&& !Material.MaterialModifiesMeshPosition_RenderThread()
			&& Material.WritesEveryPixel()
			&& !bUsesMobileColorValue)
		{
			const FMaterialRenderProxy& DefaultProxy = *UMaterial::GetDefaultMaterial(MD_Surface)->GetRenderProxy();
			const FMaterial& DefaultMaterial = *DefaultProxy.GetMaterial(FeatureLevel);
			Process<true, false>(MeshBatch, BatchElementMask, StaticMeshId, PrimitiveSceneProxy, DefaultProxy, DefaultMaterial, MeshFillMode, MeshCullMode, MobileColorValue);
		}
		else if (!IsTranslucentBlendMode(BlendMode) || Material.IsTranslucencyWritingCustomDepth())
		{
			const bool bMaterialMasked = !Material.WritesEveryPixel() || Material.IsTranslucencyWritingCustomDepth();

			const FMaterialRenderProxy* EffectiveMaterialRenderProxy = &MaterialRenderProxy;
			const FMaterial* EffectiveMaterial = &Material;

			if (!bMaterialMasked && !Material.MaterialModifiesMeshPosition_RenderThread())
			{
				// Override with the default material for opaque materials that are not two sided
				EffectiveMaterialRenderProxy = UMaterial::GetDefaultMaterial(MD_Surface)->GetRenderProxy();
				EffectiveMaterial = EffectiveMaterialRenderProxy->GetMaterial(FeatureLevel);
			}

			if (bUsesMobileColorValue)
			{
				Process<false, true>(MeshBatch, BatchElementMask, StaticMeshId, PrimitiveSceneProxy, *EffectiveMaterialRenderProxy, *EffectiveMaterial, MeshFillMode, MeshCullMode, MobileColorValue);
			}
			else
			{
				Process<false, false>(MeshBatch, BatchElementMask, StaticMeshId, PrimitiveSceneProxy, *EffectiveMaterialRenderProxy, *EffectiveMaterial, MeshFillMode, MeshCullMode, MobileColorValue);
			}
		}
	}
}

FMeshDrawCommandSortKey CalculateCustomDepthMeshStaticSortKey(EBlendMode BlendMode, const FMeshMaterialShader* VertexShader, const FMeshMaterialShader* PixelShader)
{
	FMeshDrawCommandSortKey SortKey;
	SortKey.CustomDepthPass.VertexShaderHash = PointerHash(VertexShader) & 0xFFFF;
	SortKey.CustomDepthPass.PixelShaderHash = PointerHash(PixelShader);
	SortKey.CustomDepthPass.Priority = BlendMode == EBlendMode::BLEND_Translucent ? 1 : 0;

	return SortKey;
}

template<bool bPositionOnly, bool bUsesMobileColorValue>
void FCustomDepthPassMeshProcessor::Process(
	const FMeshBatch& RESTRICT MeshBatch,
	uint64 BatchElementMask,
	int32 StaticMeshId,
	const FPrimitiveSceneProxy* RESTRICT PrimitiveSceneProxy,
	const FMaterialRenderProxy& RESTRICT MaterialRenderProxy,
	const FMaterial& RESTRICT MaterialResource,
	ERasterizerFillMode MeshFillMode,
	ERasterizerCullMode MeshCullMode,
	float MobileColorValue)
{
	const FVertexFactory* VertexFactory = MeshBatch.VertexFactory;

	TMeshProcessorShaders<
		TDepthOnlyVS<bPositionOnly>,
		FDepthOnlyHS,
		FDepthOnlyDS,
		FDepthOnlyPS<bUsesMobileColorValue>> DepthPassShaders;

	FShaderPipelineRef ShaderPipeline;
	GetDepthPassShaders<bPositionOnly, bUsesMobileColorValue>(
		MaterialResource,
		VertexFactory->GetType(),
		FeatureLevel,
		DepthPassShaders.HullShader,
		DepthPassShaders.DomainShader,
		DepthPassShaders.VertexShader,
		DepthPassShaders.PixelShader,
		ShaderPipeline
		);

	FDepthOnlyShaderElementData ShaderElementData(MobileColorValue);
	ShaderElementData.InitializeMeshMaterialData(ViewIfDynamicMeshCommand, PrimitiveSceneProxy, MeshBatch, StaticMeshId, false);

	const FMeshDrawCommandSortKey SortKey = CalculateCustomDepthMeshStaticSortKey(MaterialResource.GetBlendMode(), DepthPassShaders.VertexShader.GetShader(), DepthPassShaders.PixelShader.GetShader());

	BuildMeshDrawCommands(
		MeshBatch,
		BatchElementMask,
		PrimitiveSceneProxy,
		MaterialRenderProxy,
		MaterialResource,
		PassDrawRenderState,
		DepthPassShaders,
		MeshFillMode,
		MeshCullMode,
		SortKey,
		bPositionOnly ? EMeshPassFeatures::PositionOnly : EMeshPassFeatures::Default,
		ShaderElementData);
}

FMeshPassProcessor* CreateCustomDepthPassProcessor(const FScene* Scene, const FSceneView* InViewIfDynamicMeshCommand, FMeshPassDrawListContext* InDrawListContext)
{
	return new(FMemStack::Get()) FCustomDepthPassMeshProcessor(Scene, InViewIfDynamicMeshCommand, InDrawListContext);
}

FRegisterPassProcessorCreateFunction RegisterCustomDepthPass(&CreateCustomDepthPassProcessor, EShadingPath::Deferred, EMeshPass::CustomDepth, EMeshPassFlags::MainView);
FRegisterPassProcessorCreateFunction RegisterMobileCustomDepthPass(&CreateCustomDepthPassProcessor, EShadingPath::Mobile, EMeshPass::CustomDepth, EMeshPassFlags::MainView);