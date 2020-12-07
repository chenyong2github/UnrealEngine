// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	SceneHitProxyRendering.cpp: Scene hit proxy rendering.
=============================================================================*/

#include "SceneHitProxyRendering.h"
#include "RendererInterface.h"
#include "BatchedElements.h"
#include "Materials/Material.h"
#include "PostProcess/SceneRenderTargets.h"
#include "MaterialShaderType.h"
#include "MeshMaterialShader.h"
#include "ShaderBaseClasses.h"
#include "SceneRendering.h"
#include "DeferredShadingRenderer.h"
#include "ScenePrivate.h"
#include "DynamicPrimitiveDrawing.h"
#include "ClearQuad.h"
#include "VisualizeTexture.h"
#include "MeshPassProcessor.inl"
#include "GPUScene.h"
#include "Rendering/ColorVertexBuffer.h"
#include "Rendering/NaniteResources.h"
#include "Rendering/NaniteStreamingManager.h"
#include "ShaderPrint.h"
#include "FXSystem.h"
#include "GPUSortManager.h"
#include "VT/VirtualTextureSystem.h"

class FHitProxyShaderElementData : public FMeshMaterialShaderElementData
{
public:
	FHitProxyShaderElementData(FHitProxyId InBatchHitProxyId)
		: BatchHitProxyId(InBatchHitProxyId)
	{
	}

	FHitProxyId BatchHitProxyId;
};

/**
 * A vertex shader for rendering the depth of a mesh.
 */
class FHitProxyVS : public FMeshMaterialShader
{
	DECLARE_SHADER_TYPE(FHitProxyVS,MeshMaterial);

public:
	static bool ShouldCompilePermutation(const FMeshMaterialShaderPermutationParameters& Parameters)
	{
		// Only compile the hit proxy vertex shader on PC
		return IsPCPlatform(Parameters.Platform)
			// and only compile for the default material or materials that are masked.
			&& (Parameters.MaterialParameters.bIsSpecialEngineMaterial ||
				!Parameters.MaterialParameters.bWritesEveryPixel ||
				Parameters.MaterialParameters.bMaterialMayModifyMeshPosition ||
				Parameters.MaterialParameters.bIsTwoSided);
	}

	void GetShaderBindings(
		const FScene* Scene,
		ERHIFeatureLevel::Type FeatureLevel,
		const FPrimitiveSceneProxy* PrimitiveSceneProxy,
		const FMaterialRenderProxy& MaterialRenderProxy,
		const FMaterial& Material,
		const FMeshPassProcessorRenderState& DrawRenderState,
		const FMeshMaterialShaderElementData& ShaderElementData,
		FMeshDrawSingleShaderBindings& ShaderBindings)
	{
		FMeshMaterialShader::GetShaderBindings(Scene, FeatureLevel, PrimitiveSceneProxy, MaterialRenderProxy, Material, DrawRenderState, ShaderElementData, ShaderBindings);

#if WITH_EDITOR
		const FColorVertexBuffer* HitProxyIdBuffer = PrimitiveSceneProxy ? PrimitiveSceneProxy->GetCustomHitProxyIdBuffer() : nullptr;
		if(HitProxyIdBuffer)
		{
			ShaderBindings.Add(VertexFetch_HitProxyIdBuffer, HitProxyIdBuffer->GetColorComponentsSRV());
		}
		else
		{
			ShaderBindings.Add(VertexFetch_HitProxyIdBuffer, GNullColorVertexBuffer.VertexBufferSRV);
		}
#endif
	}
protected:

	FHitProxyVS(const ShaderMetaType::CompiledShaderInitializerType& Initializer) :
		FMeshMaterialShader(Initializer)
	{
		VertexFetch_HitProxyIdBuffer.Bind(Initializer.ParameterMap, TEXT("VertexFetch_HitProxyIdBuffer"), SPF_Optional);
	}
	FHitProxyVS() {}

	LAYOUT_FIELD(FShaderResourceParameter, VertexFetch_HitProxyIdBuffer)
};

IMPLEMENT_MATERIAL_SHADER_TYPE(,FHitProxyVS,TEXT("/Engine/Private/HitProxyVertexShader.usf"),TEXT("Main"),SF_Vertex); 

/**
 * A hull shader for rendering the depth of a mesh.
 */
class FHitProxyHS : public FBaseHS
{
	DECLARE_SHADER_TYPE(FHitProxyHS,MeshMaterial);
protected:

	FHitProxyHS() {}

	FHitProxyHS(const ShaderMetaType::CompiledShaderInitializerType& Initializer) :
		FBaseHS(Initializer)
	{}

	static bool ShouldCompilePermutation(const FMeshMaterialShaderPermutationParameters& Parameters)
	{
		return FBaseHS::ShouldCompilePermutation(Parameters)
			&& FHitProxyVS::ShouldCompilePermutation(Parameters);
	}
};

/**
 * A domain shader for rendering the depth of a mesh.
 */
class FHitProxyDS : public FBaseDS
{
	DECLARE_SHADER_TYPE(FHitProxyDS,MeshMaterial);

protected:

	FHitProxyDS() {}

	FHitProxyDS(const ShaderMetaType::CompiledShaderInitializerType& Initializer) :
		FBaseDS(Initializer)
	{}

	static bool ShouldCompilePermutation(const FMeshMaterialShaderPermutationParameters& Parameters)
	{
		return FBaseDS::ShouldCompilePermutation(Parameters)
			&& FHitProxyVS::ShouldCompilePermutation(Parameters);
	}
};

IMPLEMENT_MATERIAL_SHADER_TYPE(,FHitProxyHS,TEXT("/Engine/Private/HitProxyVertexShader.usf"),TEXT("MainHull"),SF_Hull); 
IMPLEMENT_MATERIAL_SHADER_TYPE(,FHitProxyDS,TEXT("/Engine/Private/HitProxyVertexShader.usf"),TEXT("MainDomain"),SF_Domain);

/**
 * A pixel shader for rendering the HitProxyId of an object as a unique color in the scene.
 */
class FHitProxyPS : public FMeshMaterialShader
{
	DECLARE_SHADER_TYPE(FHitProxyPS,MeshMaterial);
public:

	static bool ShouldCompilePermutation(const FMeshMaterialShaderPermutationParameters& Parameters)
	{
		// Only compile the hit proxy vertex shader on PC
		return IsPCPlatform(Parameters.Platform) 
			// and only compile for default materials or materials that are masked.
			&& (Parameters.MaterialParameters.bIsSpecialEngineMaterial ||
				!Parameters.MaterialParameters.bWritesEveryPixel ||
				Parameters.MaterialParameters.bMaterialMayModifyMeshPosition ||
				Parameters.MaterialParameters.bIsTwoSided);
	}

	FHitProxyPS(const ShaderMetaType::CompiledShaderInitializerType& Initializer):
		FMeshMaterialShader(Initializer)
	{
		HitProxyId.Bind(Initializer.ParameterMap,TEXT("HitProxyId"), SPF_Optional); // There is no way to guarantee that this parameter will be preserved in a material that kill()s all fragments as the optimiser can remove the global - this happens in various projects.
	}

	FHitProxyPS() {}

	void GetShaderBindings(
		const FScene* Scene,
		ERHIFeatureLevel::Type FeatureLevel,
		const FPrimitiveSceneProxy* PrimitiveSceneProxy,
		const FMaterialRenderProxy& MaterialRenderProxy,
		const FMaterial& Material,
		const FMeshPassProcessorRenderState& DrawRenderState,
		const FHitProxyShaderElementData& ShaderElementData,
		FMeshDrawSingleShaderBindings& ShaderBindings) const
	{
		FMeshMaterialShader::GetShaderBindings(Scene, FeatureLevel, PrimitiveSceneProxy, MaterialRenderProxy, Material, DrawRenderState, ShaderElementData, ShaderBindings);

		FHitProxyId hitProxyId = ShaderElementData.BatchHitProxyId;

#if WITH_EDITOR
		if (PrimitiveSceneProxy && PrimitiveSceneProxy->GetCustomHitProxyIdBuffer())
		{
			hitProxyId = FColor(0);
		}
		else 
#endif
		if (PrimitiveSceneProxy && ShaderElementData.BatchHitProxyId == FHitProxyId())
		{
			hitProxyId = PrimitiveSceneProxy->GetPrimitiveSceneInfo()->DefaultDynamicHitProxyId;
		}

		// Per-instance hitproxies are supplied by the vertex factory.
		if (PrimitiveSceneProxy && PrimitiveSceneProxy->HasPerInstanceHitProxies())
		{
			hitProxyId = FColor(0);
		}

		ShaderBindings.Add(HitProxyId, hitProxyId.GetColor().ReinterpretAsLinear());
	}

private:
	LAYOUT_FIELD(FShaderParameter, HitProxyId)
};

IMPLEMENT_MATERIAL_SHADER_TYPE(,FHitProxyPS,TEXT("/Engine/Private/HitProxyPixelShader.usf"),TEXT("Main"),SF_Pixel);

#if WITH_EDITOR

void InitHitProxyRender(FRDGBuilder& GraphBuilder, const FSceneRenderer* SceneRenderer, FRDGTextureRef& OutHitProxyTexture, FRDGTextureRef& OutHitProxyDepthTexture)
{
	auto& ViewFamily = SceneRenderer->ViewFamily;
	auto FeatureLevel = ViewFamily.Scene->GetFeatureLevel();

	// Ensure VirtualTexture resources are allocated
	if (UseVirtualTexturing(FeatureLevel))
	{
		FVirtualTextureSystem::Get().AllocateResources(GraphBuilder, FeatureLevel);
		FVirtualTextureSystem::Get().CallPendingCallbacks();
	}

	// Initialize global system textures (pass-through if already initialized).
	GSystemTextures.InitializeTextures(GraphBuilder.RHICmdList, FeatureLevel);
	FRDGSystemTextures::Create(GraphBuilder);

	FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get();
	// Allocate the maximum scene render target space for the current view family.
	SceneContext.Allocate(GraphBuilder, SceneRenderer);

	// Create a texture to store the resolved light attenuation values, and a render-targetable surface to hold the unresolved light attenuation values.
	{
		FRDGTextureDesc Desc(FRDGTextureDesc::Create2D(SceneContext.GetBufferSizeXY(), PF_B8G8R8A8, FClearValueBinding::Black, TexCreate_RenderTargetable | TexCreate_ShaderResource));
		OutHitProxyTexture = GraphBuilder.CreateTexture(Desc, TEXT("HitProxy"));

		// create non-MSAA version for hit proxies on PC if needed
		const EShaderPlatform CurrentShaderPlatform = GShaderPlatformForFeatureLevel[FeatureLevel];
		FRDGTextureDesc DepthDesc = Translate(SceneContext.SceneDepthZ->GetDesc(), ERenderTargetTexture::Targetable);

		if (DepthDesc.NumSamples > 1 && RHISupportsSeparateMSAAAndResolveTextures(CurrentShaderPlatform))
		{
			DepthDesc.NumSamples = 1;
			OutHitProxyDepthTexture = GraphBuilder.CreateTexture(DepthDesc, TEXT("NoMSAASceneDepthZ"));
		}
		else
		{
			OutHitProxyDepthTexture = GraphBuilder.RegisterExternalTexture(SceneContext.SceneDepthZ);
		}
	}
}

BEGIN_SHADER_PARAMETER_STRUCT(FHitProxyPassParameters, )
	SHADER_PARAMETER_STRUCT_INCLUDE(FViewShaderParameters, View)
	SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FSceneTextureUniformParameters, SceneTextures)
	RENDER_TARGET_BINDING_SLOTS()
END_SHADER_PARAMETER_STRUCT()

BEGIN_SHADER_PARAMETER_STRUCT(FHitProxyCopyToViewFamilyParameters, )
	RDG_TEXTURE_ACCESS(HitProxyTexture, ERHIAccess::SRVGraphics)
	RENDER_TARGET_BINDING_SLOTS()
END_SHADER_PARAMETER_STRUCT()

static void DoRenderHitProxies(
	FRDGBuilder& GraphBuilder, 
	const FSceneRenderer* SceneRenderer, 
	FRDGTextureRef HitProxyTexture, 
	FRDGTextureRef HitProxyDepthTexture,
	const TArray<Nanite::FRasterResults, TInlineAllocator<2>>& NaniteRasterResults)
{
	auto& ViewFamily = SceneRenderer->ViewFamily;
	auto& Views = SceneRenderer->Views;
	const auto FeatureLevel = SceneRenderer->FeatureLevel;
	const bool bNeedToSwitchVerticalAxis = RHINeedsToSwitchVerticalAxis(GShaderPlatformForFeatureLevel[FeatureLevel]);
	const FIntPoint HitProxyTextureExtent = HitProxyTexture->Desc.Extent;

	{
		auto* PassParameters = GraphBuilder.AllocParameters<FRenderTargetParameters>();
		PassParameters->RenderTargets[0] = FRenderTargetBinding(HitProxyTexture, ERenderTargetLoadAction::EClear);
		PassParameters->RenderTargets.DepthStencil = FDepthStencilBinding(HitProxyDepthTexture, ERenderTargetLoadAction::EClear, ERenderTargetLoadAction::EClear, FExclusiveDepthStencil::DepthWrite_StencilWrite);

		GraphBuilder.AddPass(
			RDG_EVENT_NAME("ClearHitProxies"),
			PassParameters,
			ERDGPassFlags::Raster,
			[&Views, HitProxyTextureExtent](FRHICommandList& RHICmdList)
		{
			// Clear color for each view.
			for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
			{
				const FViewInfo& View = Views[ViewIndex];
				RHICmdList.SetViewport(View.ViewRect.Min.X, View.ViewRect.Min.Y, 0.0f, View.ViewRect.Max.X, View.ViewRect.Max.Y, 1.0f);
				DrawClearQuad(RHICmdList, true, FLinearColor::White, false, 0, false, 0, HitProxyTextureExtent, FIntRect());
				// Clear the depth buffer for each DPG.
				DrawClearQuad(RHICmdList, false, FLinearColor(), true, (float)ERHIZBuffer::FarPlane, true, 0, HitProxyTextureExtent, FIntRect());
			}
		});
	}

	if (NaniteRasterResults.Num() == Views.Num())
	{
		for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ++ViewIndex)
		{
			Nanite::DrawHitProxies(GraphBuilder, *SceneRenderer->Scene, Views[ViewIndex], NaniteRasterResults[ViewIndex], HitProxyTexture, HitProxyDepthTexture);
		}
	}

	for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
	{
		const FViewInfo& View = Views[ViewIndex];
		const FScene* LocalScene = SceneRenderer->Scene;
		View.BeginRenderView();

		auto* PassParameters = GraphBuilder.AllocParameters<FHitProxyPassParameters>();
		PassParameters->View = View.GetShaderParameters();
		PassParameters->RenderTargets[0] = FRenderTargetBinding(HitProxyTexture, ERenderTargetLoadAction::ELoad);
		PassParameters->RenderTargets.DepthStencil = FDepthStencilBinding(HitProxyDepthTexture, ERenderTargetLoadAction::ELoad, ERenderTargetLoadAction::ELoad, FExclusiveDepthStencil::DepthWrite_StencilWrite);
		PassParameters->SceneTextures = CreateSceneTextureUniformBuffer(GraphBuilder, SceneRenderer->FeatureLevel, ESceneTextureSetupMode::None);

		GraphBuilder.AddPass(
			RDG_EVENT_NAME("RenderHitProxies"),
			PassParameters,
			ERDGPassFlags::Raster,
			[SceneRenderer, &View, LocalScene, FeatureLevel, bNeedToSwitchVerticalAxis](FRHICommandList& RHICmdList)
		{
			FMeshPassProcessorRenderState DrawRenderState;

			// Set the device viewport for the view.
			RHICmdList.SetViewport(View.ViewRect.Min.X, View.ViewRect.Min.Y, 0.0f, View.ViewRect.Max.X, View.ViewRect.Max.Y, 1.0f);

			// Depth tests + writes, no alpha blending.
			DrawRenderState.SetDepthStencilState(TStaticDepthStencilState<true, CF_DepthNearOrEqual>::GetRHI());
			DrawRenderState.SetBlendState(TStaticBlendState<>::GetRHI());

			const bool bHitTesting = true;

			// Adjust the visibility map for this view
			if (View.bAllowTranslucentPrimitivesInHitProxy)
			{
				View.ParallelMeshDrawCommandPasses[EMeshPass::HitProxy].DispatchDraw(nullptr, RHICmdList);
			}
			else
			{
				View.ParallelMeshDrawCommandPasses[EMeshPass::HitProxyOpaqueOnly].DispatchDraw(nullptr, RHICmdList);
			}

			DrawDynamicMeshPass(View, RHICmdList,
				[&View, &DrawRenderState, LocalScene](FDynamicPassMeshDrawListContext* DynamicMeshPassContext)
			{
				FHitProxyMeshProcessor PassMeshProcessor(
					LocalScene,
					&View,
					View.bAllowTranslucentPrimitivesInHitProxy,
					DrawRenderState,
					DynamicMeshPassContext);

				const uint64 DefaultBatchElementMask = ~0ull;

				for (int32 MeshIndex = 0; MeshIndex < View.DynamicEditorMeshElements.Num(); MeshIndex++)
				{
					const FMeshBatchAndRelevance& MeshBatchAndRelevance = View.DynamicEditorMeshElements[MeshIndex];
					PassMeshProcessor.AddMeshBatch(*MeshBatchAndRelevance.Mesh, DefaultBatchElementMask, MeshBatchAndRelevance.PrimitiveSceneProxy);
				}
			});

			View.SimpleElementCollector.DrawBatchedElements(RHICmdList, DrawRenderState, View, EBlendModeFilter::All, SDPG_World);
			View.SimpleElementCollector.DrawBatchedElements(RHICmdList, DrawRenderState, View, EBlendModeFilter::All, SDPG_Foreground);

			View.EditorSimpleElementCollector.DrawBatchedElements(RHICmdList, DrawRenderState, View, EBlendModeFilter::All, SDPG_World);
			View.EditorSimpleElementCollector.DrawBatchedElements(RHICmdList, DrawRenderState, View, EBlendModeFilter::All, SDPG_Foreground);

			DrawDynamicMeshPass(View, RHICmdList,
				[&View, &DrawRenderState, LocalScene](FDynamicPassMeshDrawListContext* DynamicMeshPassContext)
			{
				FHitProxyMeshProcessor PassMeshProcessor(
					LocalScene,
					&View,
					View.bAllowTranslucentPrimitivesInHitProxy,
					DrawRenderState,
					DynamicMeshPassContext);

				const uint64 DefaultBatchElementMask = ~0ull;

				for (int32 MeshIndex = 0; MeshIndex < View.ViewMeshElements.Num(); MeshIndex++)
				{
					const FMeshBatch& MeshBatch = View.ViewMeshElements[MeshIndex];
					PassMeshProcessor.AddMeshBatch(MeshBatch, DefaultBatchElementMask, nullptr);
				}
			});

			DrawDynamicMeshPass(View, RHICmdList,
				[&View, &DrawRenderState, LocalScene](FDynamicPassMeshDrawListContext* DynamicMeshPassContext)
			{
				FHitProxyMeshProcessor PassMeshProcessor(
					LocalScene,
					&View,
					View.bAllowTranslucentPrimitivesInHitProxy,
					DrawRenderState,
					DynamicMeshPassContext);

				const uint64 DefaultBatchElementMask = ~0ull;

				for (int32 MeshIndex = 0; MeshIndex < View.TopViewMeshElements.Num(); MeshIndex++)
				{
					const FMeshBatch& MeshBatch = View.TopViewMeshElements[MeshIndex];
					PassMeshProcessor.AddMeshBatch(MeshBatch, DefaultBatchElementMask, nullptr);
				}
			});


			// Draw the view's batched simple elements(lines, sprites, etc).
			View.BatchedViewElements.Draw(RHICmdList, DrawRenderState, FeatureLevel, bNeedToSwitchVerticalAxis, View, true);

			// Some elements should never be occluded (e.g. gizmos).
			// So we render those twice, first to overwrite potentially nearer objects,
			// then again to allows proper occlusion within those elements.
			DrawRenderState.SetDepthStencilState(TStaticDepthStencilState<false, CF_Always>::GetRHI());

			DrawDynamicMeshPass(View, RHICmdList,
				[&View, &DrawRenderState, LocalScene](FDynamicPassMeshDrawListContext* DynamicMeshPassContext)
			{
				FHitProxyMeshProcessor PassMeshProcessor(
					LocalScene,
					&View,
					View.bAllowTranslucentPrimitivesInHitProxy,
					DrawRenderState,
					DynamicMeshPassContext);

				const uint64 DefaultBatchElementMask = ~0ull;

				for (int32 MeshIndex = 0; MeshIndex < View.TopViewMeshElements.Num(); MeshIndex++)
				{
					const FMeshBatch& MeshBatch = View.TopViewMeshElements[MeshIndex];
					PassMeshProcessor.AddMeshBatch(MeshBatch, DefaultBatchElementMask, nullptr);
				}
			});

			View.TopBatchedViewElements.Draw(RHICmdList, DrawRenderState, FeatureLevel, bNeedToSwitchVerticalAxis, View, true);

			DrawRenderState.SetDepthStencilState(TStaticDepthStencilState<true, CF_DepthNearOrEqual>::GetRHI());

			DrawDynamicMeshPass(View, RHICmdList,
				[&View, &DrawRenderState, LocalScene](FDynamicPassMeshDrawListContext* DynamicMeshPassContext)
			{
				FHitProxyMeshProcessor PassMeshProcessor(
					LocalScene,
					&View,
					View.bAllowTranslucentPrimitivesInHitProxy,
					DrawRenderState,
					DynamicMeshPassContext);

				const uint64 DefaultBatchElementMask = ~0ull;

				for (int32 MeshIndex = 0; MeshIndex < View.TopViewMeshElements.Num(); MeshIndex++)
				{
					const FMeshBatch& MeshBatch = View.TopViewMeshElements[MeshIndex];
					PassMeshProcessor.AddMeshBatch(MeshBatch, DefaultBatchElementMask, nullptr);
				}
			});

			View.TopBatchedViewElements.Draw(RHICmdList, DrawRenderState, FeatureLevel, bNeedToSwitchVerticalAxis, View, true);
		});
	}

	FRDGTextureRef ViewFamilyTexture = TryCreateViewFamilyTexture(GraphBuilder, ViewFamily);
	check(ViewFamilyTexture);

	//
	// Copy the hit proxy buffer into the view family's render target.
	//

	{
		auto* PassParameters = GraphBuilder.AllocParameters<FHitProxyCopyToViewFamilyParameters>();
		PassParameters->HitProxyTexture = HitProxyTexture;
		PassParameters->RenderTargets[0] = FRenderTargetBinding(ViewFamilyTexture, ERenderTargetLoadAction::ELoad);

		GraphBuilder.AddPass(
			RDG_EVENT_NAME("HitProxies"),
			PassParameters,
			ERDGPassFlags::Raster,
			[&Views, HitProxyTextureExtent, HitProxyTexture, ViewFamilyTexture, FeatureLevel, bNeedToSwitchVerticalAxis](FRHICommandList& RHICmdList)
		{
			// Set up a FTexture that is used to draw the hit proxy buffer to the view family's render target.
			FTexture HitProxyRenderTargetTexture;
			HitProxyRenderTargetTexture.TextureRHI = HitProxyTexture->GetRHI();
			HitProxyRenderTargetTexture.SamplerStateRHI = TStaticSamplerState<>::GetRHI();

			// Generate the vertices and triangles mapping the hit proxy RT pixels into the view family's RT pixels.
			FBatchedElements BatchedElements;
			for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
			{
				const FViewInfo& View = Views[ViewIndex];

				float InvBufferSizeX = 1.0f / HitProxyTextureExtent.X;
				float InvBufferSizeY = 1.0f / HitProxyTextureExtent.Y;

				const float U0 = View.ViewRect.Min.X * InvBufferSizeX;
				const float V0 = View.ViewRect.Min.Y * InvBufferSizeY;
				const float U1 = View.ViewRect.Max.X * InvBufferSizeX;
				const float V1 = View.ViewRect.Max.Y * InvBufferSizeY;

				// Note: High DPI .  We are drawing to the size of the unscaled view rect because that is the size of the views render target
				// if we do not do this clicking would be off.
				const int32 V00 = BatchedElements.AddVertex(FVector4(View.UnscaledViewRect.Min.X, View.UnscaledViewRect.Min.Y, 0, 1), FVector2D(U0, V0), FLinearColor::White, FHitProxyId());
				const int32 V10 = BatchedElements.AddVertex(FVector4(View.UnscaledViewRect.Max.X, View.UnscaledViewRect.Min.Y, 0, 1), FVector2D(U1, V0), FLinearColor::White, FHitProxyId());
				const int32 V01 = BatchedElements.AddVertex(FVector4(View.UnscaledViewRect.Min.X, View.UnscaledViewRect.Max.Y, 0, 1), FVector2D(U0, V1), FLinearColor::White, FHitProxyId());
				const int32 V11 = BatchedElements.AddVertex(FVector4(View.UnscaledViewRect.Max.X, View.UnscaledViewRect.Max.Y, 0, 1), FVector2D(U1, V1), FLinearColor::White, FHitProxyId());

				BatchedElements.AddTriangle(V00, V10, V11, &HitProxyRenderTargetTexture, BLEND_Opaque);
				BatchedElements.AddTriangle(V00, V11, V01, &HitProxyRenderTargetTexture, BLEND_Opaque);
			}

			// Generate a transform which maps from view family RT pixel coordinates to Normalized Device Coordinates.
			FIntPoint ViewFamilyTextureExtent = ViewFamilyTexture->Desc.Extent;

			const FMatrix PixelToView =
				FTranslationMatrix(FVector(0, 0, 0)) *
				FMatrix(
					FPlane(1.0f / ((float)ViewFamilyTextureExtent.X / 2.0f), 0.0, 0.0f, 0.0f),
					FPlane(0.0f, -GProjectionSignY / ((float)ViewFamilyTextureExtent.Y / 2.0f), 0.0f, 0.0f),
					FPlane(0.0f, 0.0f, 1.0f, 0.0f),
					FPlane(-1.0f, GProjectionSignY, 0.0f, 1.0f)
				);

			FSceneView SceneView = FBatchedElements::CreateProxySceneView(PixelToView, FIntRect(0, 0, ViewFamilyTextureExtent.X, ViewFamilyTextureExtent.Y));
			FMeshPassProcessorRenderState DrawRenderState;

			DrawRenderState.SetDepthStencilState(TStaticDepthStencilState<false, CF_Always>::GetRHI());
			DrawRenderState.SetBlendState(TStaticBlendState<>::GetRHI());

			BatchedElements.Draw(
				RHICmdList,
				DrawRenderState,
				FeatureLevel,
				bNeedToSwitchVerticalAxis,
				SceneView,
				false,
				1.0f
			);

			RHICmdList.EndScene();
		});
	}
}
#endif

void FMobileSceneRenderer::RenderHitProxies(FRHICommandListImmediate& RHICmdList)
{
	FRDGBuilder GraphBuilder(RHICmdList);

	Scene->UpdateAllPrimitiveSceneInfos(GraphBuilder);
	
	PrepareViewRectsForRendering();

#if WITH_EDITOR

	FRDGTextureRef HitProxyTexture = nullptr;
	FRDGTextureRef HitProxyDepthTexture = nullptr;
	InitHitProxyRender(GraphBuilder, this, HitProxyTexture, HitProxyDepthTexture);

	// Find the visible primitives.
	InitViews(GraphBuilder);

	GEngine->GetPreRenderDelegate().Broadcast();

	// Global dynamic buffers need to be committed before rendering.
	DynamicIndexBuffer.Commit();
	DynamicVertexBuffer.Commit();
	DynamicReadBuffer.Commit();

	TArray<Nanite::FRasterResults, TInlineAllocator<2>> NaniteRasterResults;

	::DoRenderHitProxies(GraphBuilder, this, HitProxyTexture, HitProxyDepthTexture, NaniteRasterResults);

	GEngine->GetPostRenderDelegate().Broadcast();
#endif

	GraphBuilder.Execute();

	if (bDeferredShading)
	{
		// Release the original reference on the scene render targets
		FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get();
		SceneContext.AdjustGBufferRefCount(RHICmdList, -1);
	}
}

void FDeferredShadingSceneRenderer::RenderHitProxies(FRHICommandListImmediate& RHICmdList)
{
	static const bool bNaniteEnabled = DoesPlatformSupportNanite(GMaxRHIShaderPlatform);

	FRDGBuilder GraphBuilder(RHICmdList);

	Scene->UpdateAllPrimitiveSceneInfos(GraphBuilder);

	FGPUSceneScopeBeginEndHelper GPUSceneScopeBeginEndHelper(Scene->GPUScene, GPUSceneDynamicContext, *Scene);

	PrepareViewRectsForRendering();

#if WITH_EDITOR
	FRDGTextureRef HitProxyTexture = nullptr;
	FRDGTextureRef HitProxyDepthTexture = nullptr;

	InitHitProxyRender(GraphBuilder, this, HitProxyTexture, HitProxyDepthTexture);

	const FIntPoint HitProxyTextureSize = HitProxyDepthTexture->Desc.Extent;

	// Find the visible primitives.
	FILCUpdatePrimTaskData ILCTaskData;
	bool bDoInitViewAftersPrepass = InitViews(GraphBuilder, FExclusiveDepthStencil::DepthWrite_StencilWrite, ILCTaskData);
	if (bDoInitViewAftersPrepass)
	{
		InitViewsPossiblyAfterPrepass(GraphBuilder, ILCTaskData);
	}

	extern TSet<IPersistentViewUniformBufferExtension*> PersistentViewUniformBufferExtensions;

	for (IPersistentViewUniformBufferExtension* Extension : PersistentViewUniformBufferExtensions)
	{
		Extension->BeginFrame();

		for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
		{
			// Must happen before RHI thread flush so any tasks we dispatch here can land in the idle gap during the flush
			Extension->PrepareView(&Views[ViewIndex]);
		}
	}

	Scene->GPUScene.Update(GraphBuilder, *Scene);

	for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
	{
		ShaderPrint::BeginView(GraphBuilder, Views[ViewIndex]);
	}

	for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
	{
		Scene->GPUScene.UploadDynamicPrimitiveShaderDataForView(GraphBuilder.RHICmdList, *Scene, Views[ViewIndex]);
	}

	if (bNaniteEnabled)
	{
		Nanite::GGlobalResources.Update(GraphBuilder);
		Nanite::GStreamingManager.BeginAsyncUpdate(GraphBuilder);
		Nanite::GStreamingManager.EndAsyncUpdate(GraphBuilder);
	}

	GEngine->GetPreRenderDelegate().Broadcast();

	// Global dynamic buffers need to be committed before rendering.
	DynamicIndexBufferForInitViews.Commit();
	DynamicVertexBufferForInitViews.Commit();
	DynamicReadBufferForInitViews.Commit();

	// Notify the FX system that the scene is about to be rendered.
	if (FXSystem && Views.IsValidIndex(0))
	{
		AddPass(GraphBuilder, [this](FRHICommandListImmediate& RHICmdList)
		{
			FGPUSortManager* GPUSortManager = FXSystem->GetGPUSortManager();
			FXSystem->PreRender(RHICmdList, Views[0].ViewUniformBuffer, &Views[0].GlobalDistanceFieldInfo.ParameterData, false);
			if (GPUSortManager)
			{
				GPUSortManager->OnPreRender(RHICmdList);
			}
			// Call PostRenderOpaque now as this is irrelevant for when rendering hit proxies.
			// because we don't tick the particles in the render loop (see last param being "false").
			FXSystem->PostRenderOpaque(RHICmdList, Views[0].ViewUniformBuffer, nullptr, nullptr, false);
			if (GPUSortManager)
			{
				GPUSortManager->OnPostRenderOpaque(RHICmdList);
			}
		});
	}

	TArray<Nanite::FRasterResults, TInlineAllocator<2>> NaniteRasterResults;
	if (bNaniteEnabled)
	{
		NaniteRasterResults.AddDefaulted(Views.Num());

		Nanite::FRasterState RasterState;
		Nanite::FRasterContext RasterContext = Nanite::InitRasterContext(GraphBuilder, HitProxyTextureSize);

		const bool bTwoPassOcclusion = false;
		const bool bUpdateStreaming = false;
		const bool bSupportsMultiplePasses = false;
		const bool bForceHWRaster = RasterContext.RasterScheduling == Nanite::ERasterScheduling::HardwareOnly;
		const bool bPrimaryContext = false;

		for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
		{
			Nanite::FCullingContext CullingContext = Nanite::InitCullingContext(
				GraphBuilder,
				*Scene,
				nullptr,
				FIntRect(),
				bTwoPassOcclusion,
				bUpdateStreaming,
				bSupportsMultiplePasses,
				bForceHWRaster,
				bPrimaryContext
			);
			Nanite::FPackedView PackedView = Nanite::CreatePackedViewFromViewInfo(Views[ViewIndex], HitProxyTextureSize, VIEW_FLAG_HZBTEST);
			Nanite::CullRasterize(GraphBuilder, *Scene, { PackedView }, CullingContext, RasterContext, RasterState);
			Nanite::ExtractResults(GraphBuilder, CullingContext, RasterContext, NaniteRasterResults[ViewIndex]);
		}
	}

	::DoRenderHitProxies(GraphBuilder, this, HitProxyTexture, HitProxyDepthTexture, NaniteRasterResults);

	for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
	{
		ShaderPrint::EndView(Views[ViewIndex]);
	}

	GEngine->GetPostRenderDelegate().Broadcast();

#endif

	GraphBuilder.Execute();
}

#if WITH_EDITOR

bool FHitProxyMeshProcessor::TryAddMeshBatch(const FMeshBatch& RESTRICT MeshBatch, uint64 BatchElementMask, const FPrimitiveSceneProxy* RESTRICT PrimitiveSceneProxy, int32 StaticMeshId, const FMaterialRenderProxy* MaterialRenderProxy, const FMaterial* Material)
{
	const EBlendMode BlendMode = Material->GetBlendMode();
	const FMeshDrawingPolicyOverrideSettings OverrideSettings = ComputeMeshOverrideSettings(MeshBatch);
	const ERasterizerFillMode MeshFillMode = ComputeMeshFillMode(MeshBatch, *Material, OverrideSettings);
	const ERasterizerCullMode MeshCullMode = ComputeMeshCullMode(MeshBatch, *Material, OverrideSettings);

	if (Material->WritesEveryPixel() && !Material->IsTwoSided() && !Material->MaterialModifiesMeshPosition_RenderThread())
	{
		// Default material doesn't handle masked, and doesn't have the correct bIsTwoSided setting.
		MaterialRenderProxy = UMaterial::GetDefaultMaterial(MD_Surface)->GetRenderProxy();
		check(MaterialRenderProxy);
		Material = MaterialRenderProxy->GetMaterialNoFallback(FeatureLevel);
	}

	check(Material && MaterialRenderProxy);

	bool bAddTranslucentPrimitive = bAllowTranslucentPrimitivesInHitProxy;

	// Check whether the primitive overrides the pass to force translucent hit proxies.
	if (!bAddTranslucentPrimitive)
	{
		FHitProxyId HitProxyId = MeshBatch.BatchHitProxyId;

		// Fallback to the primitive default hit proxy id if the mesh batch doesn't have one.
		if (MeshBatch.BatchHitProxyId == FHitProxyId() && PrimitiveSceneProxy)
		{
			if (const FPrimitiveSceneInfo* PrimitiveSceneInfo = PrimitiveSceneProxy->GetPrimitiveSceneInfo())
			{
				HitProxyId = PrimitiveSceneInfo->DefaultDynamicHitProxyId;
			}
		}

		if (const HHitProxy* HitProxy = GetHitProxyById(HitProxyId))
		{
			bAddTranslucentPrimitive = HitProxy->AlwaysAllowsTranslucentPrimitives();
		}
	}

	bool bResult = true;
	if (bAddTranslucentPrimitive || !IsTranslucentBlendMode(BlendMode))
	{
		bResult = Process(MeshBatch, BatchElementMask, StaticMeshId, PrimitiveSceneProxy, *MaterialRenderProxy, *Material, MeshFillMode, MeshCullMode);
	}
	return bResult;
}

void FHitProxyMeshProcessor::AddMeshBatch(const FMeshBatch& RESTRICT MeshBatch, uint64 BatchElementMask, const FPrimitiveSceneProxy* RESTRICT PrimitiveSceneProxy, int32 StaticMeshId)
{
	if (MeshBatch.BatchHitProxyId == FHitProxyId::InvisibleHitProxyId)
	{
		return;
	}

	if (MeshBatch.bUseForMaterial && MeshBatch.bSelectable && Scene->RequiresHitProxies() && (!PrimitiveSceneProxy || PrimitiveSceneProxy->IsSelectable()))
	{
		const FMaterialRenderProxy* MaterialRenderProxy = MeshBatch.MaterialRenderProxy;
		while (MaterialRenderProxy)
		{
			const FMaterial* Material = MaterialRenderProxy->GetMaterialNoFallback(FeatureLevel);
			if (Material && Material->GetRenderingThreadShaderMap())
			{
				if (TryAddMeshBatch(MeshBatch, BatchElementMask, PrimitiveSceneProxy, StaticMeshId, MaterialRenderProxy, Material))
				{
					break;
				}
			}

			MaterialRenderProxy = MaterialRenderProxy->GetFallback(FeatureLevel);
		}
	}
}

bool GetHitProxyPassShaders(
	const FMaterial& Material,
	FVertexFactoryType* VertexFactoryType,
	ERHIFeatureLevel::Type FeatureLevel,
	TShaderRef<FHitProxyHS>& HullShader,
	TShaderRef<FHitProxyDS>& DomainShader,
	TShaderRef<FHitProxyVS>& VertexShader,
	TShaderRef<FHitProxyPS>& PixelShader)
{
	const EMaterialTessellationMode MaterialTessellationMode = Material.GetTessellationMode();

	const bool bNeedsHSDS = RHISupportsTessellation(GShaderPlatformForFeatureLevel[FeatureLevel])
		&& VertexFactoryType->SupportsTessellationShaders()
		&& MaterialTessellationMode != MTM_NoTessellation;

	FMaterialShaderTypes ShaderTypes;
	if (bNeedsHSDS)
	{
		ShaderTypes.AddShaderType<FHitProxyDS>();
		ShaderTypes.AddShaderType<FHitProxyHS>();
	}

	ShaderTypes.AddShaderType<FHitProxyVS>();
	ShaderTypes.AddShaderType<FHitProxyPS>();

	FMaterialShaders Shaders;
	if (!Material.TryGetShaders(ShaderTypes, VertexFactoryType, Shaders))
	{
		return false;
	}

	Shaders.TryGetVertexShader(VertexShader);
	Shaders.TryGetPixelShader(PixelShader);
	Shaders.TryGetHullShader(HullShader);
	Shaders.TryGetDomainShader(DomainShader);
	return true;
}

bool FHitProxyMeshProcessor::Process(
	const FMeshBatch& MeshBatch,
	uint64 BatchElementMask,
	int32 StaticMeshId,
	const FPrimitiveSceneProxy* RESTRICT PrimitiveSceneProxy,
	const FMaterialRenderProxy& RESTRICT MaterialRenderProxy,
	const FMaterial& RESTRICT MaterialResource,
	ERasterizerFillMode MeshFillMode,
	ERasterizerCullMode MeshCullMode)
{
	const FVertexFactory* VertexFactory = MeshBatch.VertexFactory;

	TMeshProcessorShaders<
		FHitProxyVS,
		FHitProxyHS,
		FHitProxyDS,
		FHitProxyPS> HitProxyPassShaders;

	if (!GetHitProxyPassShaders(
		MaterialResource,
		VertexFactory->GetType(),
		FeatureLevel,
		HitProxyPassShaders.HullShader,
		HitProxyPassShaders.DomainShader,
		HitProxyPassShaders.VertexShader,
		HitProxyPassShaders.PixelShader))
	{
		return false;
	}

	FHitProxyShaderElementData ShaderElementData(MeshBatch.BatchHitProxyId);
	ShaderElementData.InitializeMeshMaterialData(ViewIfDynamicMeshCommand, PrimitiveSceneProxy, MeshBatch, StaticMeshId, false);

	const FMeshDrawCommandSortKey SortKey = CalculateMeshStaticSortKey(HitProxyPassShaders.VertexShader, HitProxyPassShaders.PixelShader);

	BuildMeshDrawCommands(
		MeshBatch,
		BatchElementMask,
		PrimitiveSceneProxy,
		MaterialRenderProxy,
		MaterialResource,
		PassDrawRenderState,
		HitProxyPassShaders,
		MeshFillMode,
		MeshCullMode,
		SortKey,
		EMeshPassFeatures::Default,
		ShaderElementData);

	return true;
}

FHitProxyMeshProcessor::FHitProxyMeshProcessor(const FScene* Scene, const FSceneView* InViewIfDynamicMeshCommand, bool InbAllowTranslucentPrimitivesInHitProxy, const FMeshPassProcessorRenderState& InRenderState, FMeshPassDrawListContext* InDrawListContext)
	: FMeshPassProcessor(Scene, Scene->GetFeatureLevel(), InViewIfDynamicMeshCommand, InDrawListContext)
	, PassDrawRenderState(InRenderState)
	, bAllowTranslucentPrimitivesInHitProxy(InbAllowTranslucentPrimitivesInHitProxy)
{
}

FMeshPassProcessor* CreateHitProxyPassProcessor(const FScene* Scene, const FSceneView* InViewIfDynamicMeshCommand, FMeshPassDrawListContext* InDrawListContext)
{
	FMeshPassProcessorRenderState PassDrawRenderState;
	PassDrawRenderState.SetDepthStencilState(TStaticDepthStencilState<true, CF_DepthNearOrEqual>::GetRHI());
	PassDrawRenderState.SetBlendState(TStaticBlendState<>::GetRHI());
	return new(FMemStack::Get()) FHitProxyMeshProcessor(Scene, InViewIfDynamicMeshCommand, true, PassDrawRenderState, InDrawListContext);
}

FMeshPassProcessor* CreateHitProxyOpaqueOnlyPassProcessor(const FScene* Scene, const FSceneView* InViewIfDynamicMeshCommand, FMeshPassDrawListContext* InDrawListContext)
{
	FMeshPassProcessorRenderState PassDrawRenderState;
	PassDrawRenderState.SetDepthStencilState(TStaticDepthStencilState<true, CF_DepthNearOrEqual>::GetRHI());
	PassDrawRenderState.SetBlendState(TStaticBlendState<>::GetRHI());
	return new(FMemStack::Get()) FHitProxyMeshProcessor(Scene, InViewIfDynamicMeshCommand, false, PassDrawRenderState, InDrawListContext);
}

FRegisterPassProcessorCreateFunction RegisterHitProxyPass(&CreateHitProxyPassProcessor, EShadingPath::Deferred, EMeshPass::HitProxy, EMeshPassFlags::CachedMeshCommands | EMeshPassFlags::MainView);
FRegisterPassProcessorCreateFunction RegisterHitProxyOpaqueOnlyPass(&CreateHitProxyOpaqueOnlyPassProcessor, EShadingPath::Deferred, EMeshPass::HitProxyOpaqueOnly, EMeshPassFlags::CachedMeshCommands | EMeshPassFlags::MainView);
FRegisterPassProcessorCreateFunction RegisterMobileHitProxyPass(&CreateHitProxyPassProcessor, EShadingPath::Mobile, EMeshPass::HitProxy, EMeshPassFlags::CachedMeshCommands | EMeshPassFlags::MainView);
FRegisterPassProcessorCreateFunction RegisterMobileHitProxyOpaqueOnlyPass(&CreateHitProxyOpaqueOnlyPassProcessor, EShadingPath::Mobile, EMeshPass::HitProxyOpaqueOnly, EMeshPassFlags::CachedMeshCommands | EMeshPassFlags::MainView);

bool FEditorSelectionMeshProcessor::TryAddMeshBatch(const FMeshBatch& RESTRICT MeshBatch, uint64 BatchElementMask, const FPrimitiveSceneProxy* RESTRICT PrimitiveSceneProxy, int32 StaticMeshId, const FMaterialRenderProxy* MaterialRenderProxy, const FMaterial* Material)
{
	const FMeshDrawingPolicyOverrideSettings OverrideSettings = ComputeMeshOverrideSettings(MeshBatch);
	const ERasterizerFillMode MeshFillMode = ComputeMeshFillMode(MeshBatch, *Material, OverrideSettings);
	const ERasterizerCullMode MeshCullMode = CM_None;

	if (Material->WritesEveryPixel() && !Material->IsTwoSided() && !Material->MaterialModifiesMeshPosition_RenderThread())
	{
		// Default material doesn't handle masked, and doesn't have the correct bIsTwoSided setting.
		MaterialRenderProxy = UMaterial::GetDefaultMaterial(MD_Surface)->GetRenderProxy();
		check(MaterialRenderProxy);
		Material = MaterialRenderProxy->GetMaterialNoFallback(FeatureLevel);
	}

	check(Material && MaterialRenderProxy);

	return Process(MeshBatch, BatchElementMask, StaticMeshId, PrimitiveSceneProxy, *MaterialRenderProxy, *Material, MeshFillMode, MeshCullMode);
}

void FEditorSelectionMeshProcessor::AddMeshBatch(const FMeshBatch& RESTRICT MeshBatch, uint64 BatchElementMask, const FPrimitiveSceneProxy* RESTRICT PrimitiveSceneProxy, int32 StaticMeshId)
{
	if (MeshBatch.bUseForMaterial 
		&& MeshBatch.bUseSelectionOutline 
		&& PrimitiveSceneProxy
		&& PrimitiveSceneProxy->WantsSelectionOutline() 
		&& (PrimitiveSceneProxy->IsSelected() || PrimitiveSceneProxy->IsHovered()))
	{
		const FMaterialRenderProxy* MaterialRenderProxy = MeshBatch.MaterialRenderProxy;
		while (MaterialRenderProxy)
		{
			const FMaterial* Material = MaterialRenderProxy->GetMaterialNoFallback(FeatureLevel);
			if (Material && Material->GetRenderingThreadShaderMap())
			{
				if (TryAddMeshBatch(MeshBatch, BatchElementMask, PrimitiveSceneProxy, StaticMeshId, MaterialRenderProxy, Material))
				{
					break;
				}
			}

			MaterialRenderProxy = MaterialRenderProxy->GetFallback(FeatureLevel);
		}
	}
}

bool FEditorSelectionMeshProcessor::Process(
	const FMeshBatch& MeshBatch,
	uint64 BatchElementMask,
	int32 StaticMeshId,
	const FPrimitiveSceneProxy* RESTRICT PrimitiveSceneProxy,
	const FMaterialRenderProxy& RESTRICT MaterialRenderProxy,
	const FMaterial& RESTRICT MaterialResource,
	ERasterizerFillMode MeshFillMode,
	ERasterizerCullMode MeshCullMode)
{
	const FVertexFactory* VertexFactory = MeshBatch.VertexFactory;

	TMeshProcessorShaders<
		FHitProxyVS,
		FHitProxyHS,
		FHitProxyDS,
		FHitProxyPS> HitProxyPassShaders;

	if (!GetHitProxyPassShaders(
		MaterialResource,
		VertexFactory->GetType(),
		FeatureLevel,
		HitProxyPassShaders.HullShader,
		HitProxyPassShaders.DomainShader,
		HitProxyPassShaders.VertexShader,
		HitProxyPassShaders.PixelShader))
	{
		return false;
	}

	const int32 StencilRef = GetStencilValue(ViewIfDynamicMeshCommand, PrimitiveSceneProxy);
	PassDrawRenderState.SetStencilRef(StencilRef);

	FHitProxyId DummyId;
	FHitProxyShaderElementData ShaderElementData(DummyId);
	ShaderElementData.InitializeMeshMaterialData(ViewIfDynamicMeshCommand, PrimitiveSceneProxy, MeshBatch, StaticMeshId, false);

	const FMeshDrawCommandSortKey SortKey = CalculateMeshStaticSortKey(HitProxyPassShaders.VertexShader, HitProxyPassShaders.PixelShader);

	BuildMeshDrawCommands(
		MeshBatch,
		BatchElementMask,
		PrimitiveSceneProxy,
		MaterialRenderProxy,
		MaterialResource,
		PassDrawRenderState,
		HitProxyPassShaders,
		MeshFillMode,
		MeshCullMode,
		SortKey,
		EMeshPassFeatures::Default,
		ShaderElementData);

	return true;
}

int32 FEditorSelectionMeshProcessor::GetStencilValue(const FSceneView* View, const FPrimitiveSceneProxy* PrimitiveSceneProxy)
{
	const bool bActorSelectionColorIsSubdued = View->bHasSelectedComponents;

	const int32* ExistingStencilValue = PrimitiveSceneProxy->IsIndividuallySelected() ? ProxyToStencilIndex.Find(PrimitiveSceneProxy) : ActorNameToStencilIndex.Find(PrimitiveSceneProxy->GetOwnerName());

	int32 StencilValue = 0;

	if (PrimitiveSceneProxy->GetOwnerName() == NAME_BSP)
	{
		StencilValue = 1;
	}
	else if (ExistingStencilValue != nullptr)
	{
		StencilValue = *ExistingStencilValue;
	}
	else if (PrimitiveSceneProxy->IsIndividuallySelected())
	{
		// Any component that is individually selected should have a stencil value of < 128 so that it can have a unique color.  We offset the value by 2 because 0 means no selection and 1 is for bsp
		StencilValue = ProxyToStencilIndex.Num() % 126 + 2;
		ProxyToStencilIndex.Add(PrimitiveSceneProxy, StencilValue);
	}
	else
	{
		// If we are subduing actor color highlight then use the top level bits to indicate that to the shader.  
		StencilValue = bActorSelectionColorIsSubdued ? ActorNameToStencilIndex.Num() % 128 + 128 : ActorNameToStencilIndex.Num() % 126 + 2;
		ActorNameToStencilIndex.Add(PrimitiveSceneProxy->GetOwnerName(), StencilValue);
	}

	return StencilValue;
}

FEditorSelectionMeshProcessor::FEditorSelectionMeshProcessor(const FScene* Scene, const FSceneView* InViewIfDynamicMeshCommand, FMeshPassDrawListContext* InDrawListContext)
	: FMeshPassProcessor(Scene, Scene->GetFeatureLevel(), InViewIfDynamicMeshCommand, InDrawListContext)
{
	checkf(InViewIfDynamicMeshCommand, TEXT("Editor selection mesh process required dynamic mesh command mode."));

	ActorNameToStencilIndex.Add(NAME_BSP, 1);

	PassDrawRenderState.SetDepthStencilState(TStaticDepthStencilState<true, CF_DepthNearOrEqual, true, CF_Always, SO_Keep, SO_Keep, SO_Replace>::GetRHI());
	PassDrawRenderState.SetBlendState(TStaticBlendStateWriteMask<CW_NONE, CW_NONE, CW_NONE, CW_NONE>::GetRHI());
}

FMeshPassProcessor* CreateEditorSelectionPassProcessor(const FScene* Scene, const FSceneView* InViewIfDynamicMeshCommand, FMeshPassDrawListContext* InDrawListContext)
{
	return new(FMemStack::Get()) FEditorSelectionMeshProcessor(Scene, InViewIfDynamicMeshCommand, InDrawListContext);
}

FRegisterPassProcessorCreateFunction RegisterEditorSelectionPass(&CreateEditorSelectionPassProcessor, EShadingPath::Deferred, EMeshPass::EditorSelection, EMeshPassFlags::MainView);
FRegisterPassProcessorCreateFunction RegisterMobileEditorSelectionPass(&CreateEditorSelectionPassProcessor, EShadingPath::Mobile, EMeshPass::EditorSelection, EMeshPassFlags::MainView);

void FEditorLevelInstanceMeshProcessor::AddMeshBatch(const FMeshBatch& RESTRICT MeshBatch, uint64 BatchElementMask, const FPrimitiveSceneProxy* RESTRICT PrimitiveSceneProxy, int32 StaticMeshId)
{
	if (MeshBatch.bUseForMaterial
		&& PrimitiveSceneProxy
		&& PrimitiveSceneProxy->IsEditingLevelInstanceChild())
	{
		// Determine the mesh's material and blend mode.
		const FMaterialRenderProxy* MaterialRenderProxy = nullptr;
		const FMaterial* Material = &MeshBatch.MaterialRenderProxy->GetMaterialWithFallback(FeatureLevel, MaterialRenderProxy);

		const FMeshDrawingPolicyOverrideSettings OverrideSettings = ComputeMeshOverrideSettings(MeshBatch);
		const ERasterizerFillMode MeshFillMode = ComputeMeshFillMode(MeshBatch, *Material, OverrideSettings);
		const ERasterizerCullMode MeshCullMode = CM_None;

		if (Material->WritesEveryPixel() && !Material->IsTwoSided() && !Material->MaterialModifiesMeshPosition_RenderThread())
		{
			// Default material doesn't handle masked, and doesn't have the correct bIsTwoSided setting.
			MaterialRenderProxy = UMaterial::GetDefaultMaterial(MD_Surface)->GetRenderProxy();
			check(MaterialRenderProxy);
			Material = MaterialRenderProxy->GetMaterialNoFallback(FeatureLevel);
		}

		if (!MaterialRenderProxy)
		{
			MaterialRenderProxy = MeshBatch.MaterialRenderProxy;
		}

		check(Material && MaterialRenderProxy);

		Process(MeshBatch, BatchElementMask, StaticMeshId, PrimitiveSceneProxy, *MaterialRenderProxy, *Material, MeshFillMode, MeshCullMode);
	}
}

void FEditorLevelInstanceMeshProcessor::Process(
	const FMeshBatch& MeshBatch,
	uint64 BatchElementMask,
	int32 StaticMeshId,
	const FPrimitiveSceneProxy* RESTRICT PrimitiveSceneProxy,
	const FMaterialRenderProxy& RESTRICT MaterialRenderProxy,
	const FMaterial& RESTRICT MaterialResource,
	ERasterizerFillMode MeshFillMode,
	ERasterizerCullMode MeshCullMode)
{
	const FVertexFactory* VertexFactory = MeshBatch.VertexFactory;

	TMeshProcessorShaders<
		FHitProxyVS,
		FHitProxyHS,
		FHitProxyDS,
		FHitProxyPS> HitProxyPassShaders;

	GetHitProxyPassShaders(
		MaterialResource,
		VertexFactory->GetType(),
		FeatureLevel,
		HitProxyPassShaders.HullShader,
		HitProxyPassShaders.DomainShader,
		HitProxyPassShaders.VertexShader,
		HitProxyPassShaders.PixelShader
	);

	const int32 StencilRef = GetStencilValue(ViewIfDynamicMeshCommand, PrimitiveSceneProxy);
	PassDrawRenderState.SetStencilRef(StencilRef);

	FHitProxyId DummyId;
	FHitProxyShaderElementData ShaderElementData(DummyId);
	ShaderElementData.InitializeMeshMaterialData(ViewIfDynamicMeshCommand, PrimitiveSceneProxy, MeshBatch, StaticMeshId, false);

	const FMeshDrawCommandSortKey SortKey = CalculateMeshStaticSortKey(HitProxyPassShaders.VertexShader, HitProxyPassShaders.PixelShader);

	BuildMeshDrawCommands(
		MeshBatch,
		BatchElementMask,
		PrimitiveSceneProxy,
		MaterialRenderProxy,
		MaterialResource,
		PassDrawRenderState,
		HitProxyPassShaders,
		MeshFillMode,
		MeshCullMode,
		SortKey,
		EMeshPassFeatures::Default,
		ShaderElementData);
}

int32 FEditorLevelInstanceMeshProcessor::GetStencilValue(const FSceneView* View, const FPrimitiveSceneProxy* PrimitiveSceneProxy)
{
	// Set the stencil value to 1 for primitives which belong to an editing level instance, 0 otherwise
	return PrimitiveSceneProxy->IsEditingLevelInstanceChild() ? 1 : 0;
}

FEditorLevelInstanceMeshProcessor::FEditorLevelInstanceMeshProcessor(const FScene* Scene, const FSceneView* InViewIfDynamicMeshCommand, FMeshPassDrawListContext* InDrawListContext)
	: FMeshPassProcessor(Scene, Scene->GetFeatureLevel(), InViewIfDynamicMeshCommand, InDrawListContext)
{
	checkf(InViewIfDynamicMeshCommand, TEXT("Editor selection mesh process required dynamic mesh command mode."));

	PassDrawRenderState.SetDepthStencilState(TStaticDepthStencilState<true, CF_DepthNearOrEqual, true, CF_Always, SO_Keep, SO_Keep, SO_Replace>::GetRHI());
	PassDrawRenderState.SetBlendState(TStaticBlendStateWriteMask<CW_NONE, CW_NONE, CW_NONE, CW_NONE>::GetRHI());
	PassDrawRenderState.SetViewUniformBuffer(Scene->UniformBuffers.ViewUniformBuffer);
	PassDrawRenderState.SetInstancedViewUniformBuffer(Scene->UniformBuffers.InstancedViewUniformBuffer);
}

FMeshPassProcessor* CreateEditorLevelInstancePassProcessor(const FScene* Scene, const FSceneView* InViewIfDynamicMeshCommand, FMeshPassDrawListContext* InDrawListContext)
{
	return new(FMemStack::Get()) FEditorLevelInstanceMeshProcessor(Scene, InViewIfDynamicMeshCommand, InDrawListContext);
}

FRegisterPassProcessorCreateFunction RegisterEditorLevelInstancePass(&CreateEditorLevelInstancePassProcessor, EShadingPath::Deferred, EMeshPass::EditorLevelInstance, EMeshPassFlags::MainView);
FRegisterPassProcessorCreateFunction RegisterMobileEditorLevelInstancePass(&CreateEditorLevelInstancePassProcessor, EShadingPath::Mobile, EMeshPass::EditorLevelInstance, EMeshPassFlags::MainView);


#endif
