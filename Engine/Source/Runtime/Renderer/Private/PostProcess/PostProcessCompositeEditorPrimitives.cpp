// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_EDITOR

#include "PostProcess/PostProcessCompositeEditorPrimitives.h"
#include "EditorPrimitivesRendering.h"
#include "MeshPassProcessor.inl"
#include "BasePassRendering.h"
#include "MobileBasePassRendering.h"
#include "SceneRenderingUtils.h"

namespace
{
TAutoConsoleVariable<float> CVarEditorOpaqueGizmo(
	TEXT("r.Editor.OpaqueGizmo"),
	0.0f,
	TEXT("0..1\n0: occluded gizmo is partly transparent (default), 1:gizmo is never occluded"),
	ECVF_RenderThreadSafe);

class FPopulateEditorDepthPS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FPopulateEditorDepthPS);
	SHADER_USE_PARAMETER_STRUCT(FPopulateEditorDepthPS, FGlobalShader);

	class FUseMSAADimension : SHADER_PERMUTATION_BOOL("USE_MSAA");
	using FPermutationDomain = TShaderPermutationDomain<FUseMSAADimension>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER_STRUCT(FScreenPassTextureViewportParameters, Depth)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, DepthTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, DepthSampler)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		const FPermutationDomain PermutationVector(Parameters.PermutationId);
		const bool bUseMSAA = PermutationVector.Get<FUseMSAADimension>();

		// Only SM5+ platforms supports MSAA.
		if (!IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5) && bUseMSAA)
		{
			return false;
		}

		// Only PC platforms render editor primitives.
		return IsPCPlatform(Parameters.Platform);
	}
};

IMPLEMENT_GLOBAL_SHADER(FPopulateEditorDepthPS, "/Engine/Private/PostProcessCompositeEditorPrimitives.usf", "MainPopulateSceneDepthPS", SF_Pixel);

class FCompositeEditorPrimitivesPS : public FEditorPrimitiveShader
{
public:
	DECLARE_GLOBAL_SHADER(FCompositeEditorPrimitivesPS);
	SHADER_USE_PARAMETER_STRUCT(FCompositeEditorPrimitivesPS, FEditorPrimitiveShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER_STRUCT(FScreenPassTextureViewportParameters, Color)
		SHADER_PARAMETER_STRUCT(FScreenPassTextureViewportParameters, Depth)
		SHADER_PARAMETER_STRUCT(FScreenPassTextureViewportTransform, ColorToDepth)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, EditorPrimitivesDepth)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, EditorPrimitivesColor)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, ColorTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, ColorSampler)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, DepthTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, DepthSampler)
		SHADER_PARAMETER(uint32, bOpaqueEditorGizmo)
		SHADER_PARAMETER(uint32, bCompositeAnyNonNullDepth)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("OUTPUT_SRGB_BUFFER"), IsMobileColorsRGB() && IsMobilePlatform(Parameters.Platform));
	}
};

IMPLEMENT_GLOBAL_SHADER(FCompositeEditorPrimitivesPS, "/Engine/Private/PostProcessCompositeEditorPrimitives.usf", "MainCompositeEditorPrimitivesPS", SF_Pixel);

void RenderEditorPrimitives(FRHICommandListImmediate& RHICmdList, const FViewInfo& View, FMeshPassProcessorRenderState& DrawRenderState, FInstanceCullingManager& InstanceCullingManager)
{
	// Always depth test against other editor primitives
	DrawRenderState.SetDepthStencilState(TStaticDepthStencilState<
		true, CF_DepthNearOrEqual,
		true, CF_Always, SO_Keep, SO_Keep, SO_Replace,
		false, CF_Always, SO_Keep, SO_Keep, SO_Keep,
		0xFF, GET_STENCIL_BIT_MASK(RECEIVE_DECAL, 1) | STENCIL_LIGHTING_CHANNELS_MASK(0x7)>::GetRHI());

	DrawDynamicMeshPass(View, RHICmdList,
		[&View, &DrawRenderState](FDynamicPassMeshDrawListContext* DynamicMeshPassContext)
	{
		FEditorPrimitivesBasePassMeshProcessor PassMeshProcessor(
			View.Family->Scene->GetRenderScene(),
			View.GetFeatureLevel(),
			&View,
			DrawRenderState,
			false,
			DynamicMeshPassContext);

		const uint64 DefaultBatchElementMask = ~0ull;
		const int32 NumDynamicEditorMeshBatches = View.DynamicEditorMeshElements.Num();

		for (int32 MeshIndex = 0; MeshIndex < NumDynamicEditorMeshBatches; MeshIndex++)
		{
			const FMeshBatchAndRelevance& MeshAndRelevance = View.DynamicEditorMeshElements[MeshIndex];

			if (MeshAndRelevance.GetHasOpaqueOrMaskedMaterial() || View.Family->EngineShowFlags.Wireframe)
			{
				PassMeshProcessor.AddMeshBatch(*MeshAndRelevance.Mesh, DefaultBatchElementMask, MeshAndRelevance.PrimitiveSceneProxy);
			}
		}

		for (int32 MeshIndex = 0; MeshIndex < View.ViewMeshElements.Num(); MeshIndex++)
		{
			const FMeshBatch& MeshBatch = View.ViewMeshElements[MeshIndex];
			PassMeshProcessor.AddMeshBatch(MeshBatch, DefaultBatchElementMask, nullptr);
		}
	});

	View.EditorSimpleElementCollector.DrawBatchedElements(RHICmdList, DrawRenderState, View, EBlendModeFilter::OpaqueAndMasked, SDPG_World);

	const auto FeatureLevel = View.GetFeatureLevel();
	const auto ShaderPlatform = GShaderPlatformForFeatureLevel[FeatureLevel];
	const bool bNeedToSwitchVerticalAxis = RHINeedsToSwitchVerticalAxis(ShaderPlatform);

	// Draw the view's batched simple elements(lines, sprites, etc).
	View.BatchedViewElements.Draw(RHICmdList, DrawRenderState, FeatureLevel, bNeedToSwitchVerticalAxis, View, false, 1.0f);
}

void RenderForegroundEditorPrimitives(FRHICommandListImmediate& RHICmdList, const FViewInfo& View, FMeshPassProcessorRenderState& DrawRenderState, FInstanceCullingManager& InstanceCullingManager)
{
	const auto FeatureLevel = View.GetFeatureLevel();
	const auto ShaderPlatform = GShaderPlatformForFeatureLevel[FeatureLevel];
	const bool bNeedToSwitchVerticalAxis = RHINeedsToSwitchVerticalAxis(ShaderPlatform);

	// Draw a first time the foreground primitive without depth test to over right depth from non-foreground editor primitives.
	{
		DrawRenderState.SetDepthStencilState(TStaticDepthStencilState<true, CF_Always>::GetRHI());

		View.EditorSimpleElementCollector.DrawBatchedElements(RHICmdList, DrawRenderState, View, EBlendModeFilter::OpaqueAndMasked, SDPG_Foreground);

		DrawDynamicMeshPass(View, RHICmdList,
			[&View, &DrawRenderState](FDynamicPassMeshDrawListContext* DynamicMeshPassContext)
		{
			FEditorPrimitivesBasePassMeshProcessor PassMeshProcessor(
				View.Family->Scene->GetRenderScene(),
				View.GetFeatureLevel(),
				&View,
				DrawRenderState,
				false,
				DynamicMeshPassContext);

			const uint64 DefaultBatchElementMask = ~0ull;

			for (int32 MeshIndex = 0; MeshIndex < View.TopViewMeshElements.Num(); MeshIndex++)
			{
				const FMeshBatch& MeshBatch = View.TopViewMeshElements[MeshIndex];
				PassMeshProcessor.AddMeshBatch(MeshBatch, DefaultBatchElementMask, nullptr);
			}
		});

		View.TopBatchedViewElements.Draw(RHICmdList, DrawRenderState, FeatureLevel, bNeedToSwitchVerticalAxis, View, false);
	}

	// Draw a second time the foreground primitive with depth test to have proper depth test between foreground primitives.
	{
		DrawRenderState.SetDepthStencilState(TStaticDepthStencilState<true, CF_DepthNearOrEqual>::GetRHI());

		View.EditorSimpleElementCollector.DrawBatchedElements(RHICmdList, DrawRenderState, View, EBlendModeFilter::OpaqueAndMasked, SDPG_Foreground);

		DrawDynamicMeshPass(View, RHICmdList,
			[&View, &DrawRenderState](FDynamicPassMeshDrawListContext* DynamicMeshPassContext)
		{
			FEditorPrimitivesBasePassMeshProcessor PassMeshProcessor(
				View.Family->Scene->GetRenderScene(),
				View.GetFeatureLevel(),
				&View,
				DrawRenderState,
				false,
				DynamicMeshPassContext);

			const uint64 DefaultBatchElementMask = ~0ull;

			for (int32 MeshIndex = 0; MeshIndex < View.TopViewMeshElements.Num(); MeshIndex++)
			{
				const FMeshBatch& MeshBatch = View.TopViewMeshElements[MeshIndex];
				PassMeshProcessor.AddMeshBatch(MeshBatch, DefaultBatchElementMask, nullptr);
			}
		});

		View.TopBatchedViewElements.Draw(RHICmdList, DrawRenderState, FeatureLevel, bNeedToSwitchVerticalAxis, View, false);
	}
}

} //! namespace

const FViewInfo* CreateEditorPrimitiveView(const FViewInfo& ParentView, FIntRect ViewRect, uint32 NumSamples)
{
	FViewInfo* EditorView = ParentView.CreateSnapshot();

	// Patch view rect.
	EditorView->ViewRect = ViewRect;

	// Override pre exposure to 1.0f, because rendering after tonemapper. 
	EditorView->PreExposure = 1.0f;

	// Kills material texture mipbias because after TAA.
	EditorView->MaterialTextureMipBias = 0.0f;

	// Disable decals so that we don't do a SetDepthStencilState() in TMobileBasePassDrawingPolicy::SetupPipelineState()
	EditorView->bSceneHasDecals = false;

	if (EditorView->AntiAliasingMethod == AAM_TemporalAA)
	{
		EditorView->ViewMatrices.HackRemoveTemporalAAProjectionJitter();
	}

	FBox VolumeBounds[TVC_MAX];
	TUniquePtr<FViewUniformShaderParameters> ViewParameters = MakeUnique<FViewUniformShaderParameters>();
	EditorView->SetupUniformBufferParameters(VolumeBounds, TVC_MAX, *ViewParameters);
	ViewParameters->NumSceneColorMSAASamples = NumSamples;
	EditorView->ViewUniformBuffer = TUniformBufferRef<FViewUniformShaderParameters>::CreateUniformBufferImmediate(*ViewParameters, UniformBuffer_SingleFrame);
	EditorView->CachedViewUniformShaderParameters = MoveTemp(ViewParameters);
	return EditorView;
}

BEGIN_SHADER_PARAMETER_STRUCT(FEditorPrimitivesPassParameters, )
	SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
	SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FOpaqueBasePassUniformParameters, BasePass)
	SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FMobileBasePassUniformParameters, MobileBasePass)
	RENDER_TARGET_BINDING_SLOTS()
END_SHADER_PARAMETER_STRUCT()

FScreenPassTexture AddEditorPrimitivePass(
	FRDGBuilder& GraphBuilder,
	const FViewInfo& View,
	const FEditorPrimitiveInputs& Inputs,
	FInstanceCullingManager& InstanceCullingManager)
{
	check(Inputs.SceneColor.IsValid());
	check(Inputs.SceneDepth.IsValid());
	check(Inputs.BasePassType != FEditorPrimitiveInputs::EBasePassType::MAX);

	RDG_EVENT_SCOPE(GraphBuilder, "CompositeEditorPrimitives");

	const FSceneTextures& SceneTextures = FSceneTextures::Get(GraphBuilder);
	const uint32 NumSamples = SceneTextures.Config.EditorPrimitiveNumSamples;
	const FViewInfo* EditorView = CreateEditorPrimitiveView(View, Inputs.SceneColor.ViewRect, NumSamples);

	// Load the color target if it already exists.
	const bool bProducedByPriorPass = HasBeenProduced(SceneTextures.EditorPrimitiveColor);

	FRDGTextureRef EditorPrimitiveColor;
	FRDGTextureRef EditorPrimitiveDepth;
	if (bProducedByPriorPass)
	{
		ensureMsgf(
			Inputs.SceneColor.ViewRect == Inputs.SceneDepth.ViewRect,
			TEXT("Temporal upsampling should be disabled when drawing directly to EditorPrimitivesColor."));
		EditorPrimitiveColor = SceneTextures.EditorPrimitiveColor;
		EditorPrimitiveDepth = SceneTextures.EditorPrimitiveDepth;
	}
	else
	{
		const FSceneTexturesConfig& Config = SceneTextures.Config;

		FIntPoint Extent = Inputs.SceneColor.Texture->Desc.Extent;

		const FRDGTextureDesc ColorDesc = FRDGTextureDesc::Create2D(
			Extent,
			PF_B8G8R8A8,
			FClearValueBinding::Transparent,
			TexCreate_ShaderResource | TexCreate_RenderTargetable,
			1,
			Config.EditorPrimitiveNumSamples);

		const FRDGTextureDesc DepthDesc = FRDGTextureDesc::Create2D(
			Extent,
			PF_DepthStencil,
			FClearValueBinding::DepthFar,
			TexCreate_ShaderResource | TexCreate_DepthStencilTargetable,
			1,
			Config.EditorPrimitiveNumSamples);

		EditorPrimitiveColor = GraphBuilder.CreateTexture(ColorDesc, TEXT("Editor.PrimitivesDepth"));
		EditorPrimitiveDepth = GraphBuilder.CreateTexture(DepthDesc, TEXT("Editor.PrimitivesColor"));
	}

	// Load the color target if it already exists.
	const FScreenPassTextureViewport EditorPrimitivesViewport(EditorPrimitiveColor, Inputs.SceneColor.ViewRect);

	// The editor primitive composition pass is also used when rendering VMI_WIREFRAME in order to use MSAA.
	// So we need to check whether the editor primitives are enabled inside this function.
	if (View.Family->EngineShowFlags.CompositeEditorPrimitives)
	{
		const FScreenPassTextureViewport SceneDepthViewport(Inputs.SceneDepth);

		// Populate depth if a prior pass did not already do it.
		if (!bProducedByPriorPass)
		{
			FPopulateEditorDepthPS::FParameters* PassParameters = GraphBuilder.AllocParameters<FPopulateEditorDepthPS::FParameters>();
			PassParameters->View = View.ViewUniformBuffer;
			PassParameters->Depth = GetScreenPassTextureViewportParameters(SceneDepthViewport);
			PassParameters->DepthTexture = Inputs.SceneDepth.Texture;
			PassParameters->DepthSampler = TStaticSamplerState<SF_Point, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
			PassParameters->RenderTargets[0] = FRenderTargetBinding(EditorPrimitiveColor, ERenderTargetLoadAction::EClear);
			PassParameters->RenderTargets.DepthStencil = FDepthStencilBinding(EditorPrimitiveDepth, ERenderTargetLoadAction::EClear, ERenderTargetLoadAction::EClear, FExclusiveDepthStencil::DepthWrite_StencilWrite);

			FPopulateEditorDepthPS::FPermutationDomain PermutationVector;
			PermutationVector.Set<FPopulateEditorDepthPS::FUseMSAADimension>(NumSamples > 1);
			TShaderMapRef<FPopulateEditorDepthPS> PopulateDepthPixelShader(View.ShaderMap, PermutationVector);
			TShaderMapRef<FScreenPassVS> PopulateDepthVertexShader(View.ShaderMap);

			AddDrawScreenPass(
				GraphBuilder,
				RDG_EVENT_NAME("PopulateDepth"),
				View,
				EditorPrimitivesViewport,
				SceneDepthViewport,
				PopulateDepthVertexShader,
				PopulateDepthPixelShader,
				TStaticDepthStencilState<true, CF_Always>::GetRHI(),
				PassParameters);
		}

		{
			FEditorPrimitivesPassParameters* PassParameters = GraphBuilder.AllocParameters<FEditorPrimitivesPassParameters>();
			PassParameters->View = EditorView->ViewUniformBuffer;
			PassParameters->RenderTargets[0] = FRenderTargetBinding(EditorPrimitiveColor, ERenderTargetLoadAction::ELoad);
			PassParameters->RenderTargets.DepthStencil = FDepthStencilBinding(EditorPrimitiveDepth, ERenderTargetLoadAction::ELoad, ERenderTargetLoadAction::ELoad, FExclusiveDepthStencil::DepthWrite_StencilWrite);

			const FEditorPrimitiveInputs::EBasePassType BasePassType = Inputs.BasePassType;

			if (BasePassType == FEditorPrimitiveInputs::EBasePassType::Deferred)
			{
				PassParameters->BasePass = CreateOpaqueBasePassUniformBuffer(GraphBuilder, *EditorView, 0);
			}
			else
			{
				PassParameters->MobileBasePass = CreateMobileBasePassUniformBuffer(GraphBuilder, *EditorView, EMobileBasePass::Translucent);
			}

			GraphBuilder.AddPass(
				RDG_EVENT_NAME("EditorPrimitives"),
				PassParameters,
				ERDGPassFlags::Raster,
				[&View, &InstanceCullingManager, PassParameters, EditorView, EditorPrimitivesViewport, SceneDepthViewport, BasePassType, NumSamples](FRHICommandListImmediate& RHICmdList)
			{
				RHICmdList.SetViewport(EditorPrimitivesViewport.Rect.Min.X, EditorPrimitivesViewport.Rect.Min.Y, 0.0f, EditorPrimitivesViewport.Rect.Max.X, EditorPrimitivesViewport.Rect.Max.Y, 1.0f);

				FMeshPassProcessorRenderState DrawRenderState;
				DrawRenderState.SetDepthStencilAccess(FExclusiveDepthStencil::DepthWrite_StencilWrite);
				DrawRenderState.SetBlendState(TStaticBlendStateWriteMask<CW_RGBA>::GetRHI());

				// Draw editor primitives.
				{
					SCOPED_DRAW_EVENTF(RHICmdList, EditorPrimitives,
						TEXT("RenderViewEditorPrimitives %dx%d msaa=%d"),
						EditorPrimitivesViewport.Rect.Width(), EditorPrimitivesViewport.Rect.Height(), NumSamples);

					RenderEditorPrimitives(RHICmdList, *EditorView, DrawRenderState, InstanceCullingManager);
				}

				// Draw foreground editor primitives.
				{
					SCOPED_DRAW_EVENTF(RHICmdList, EditorPrimitives,
						TEXT("RenderViewEditorForegroundPrimitives %dx%d msaa=%d"),
						EditorPrimitivesViewport.Rect.Width(), EditorPrimitivesViewport.Rect.Height(), NumSamples);

					RenderForegroundEditorPrimitives(RHICmdList, *EditorView, DrawRenderState, InstanceCullingManager);
				}
			});
		}
	}

	FScreenPassRenderTarget Output = Inputs.OverrideOutput;

	if (!Output.IsValid())
	{
		Output = FScreenPassRenderTarget::CreateFromInput(GraphBuilder, Inputs.SceneColor, View.GetOverwriteLoadAction(), TEXT("EditorPrimitives"));
	}

	const FScreenPassTextureViewport OutputViewport(Output);
	const FScreenPassTextureViewport ColorViewport(Inputs.SceneColor);
	const FScreenPassTextureViewport DepthViewport(Inputs.SceneDepth);

	FRHISamplerState* PointClampSampler = TStaticSamplerState<SF_Point, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();

	const bool bOpaqueEditorGizmo = CVarEditorOpaqueGizmo.GetValueOnRenderThread() != 0 || View.Family->EngineShowFlags.Wireframe;

	FCompositeEditorPrimitivesPS::FParameters* PassParameters = GraphBuilder.AllocParameters<FCompositeEditorPrimitivesPS::FParameters>();
	PassParameters->RenderTargets[0] = Output.GetRenderTargetBinding();
	PassParameters->View = View.ViewUniformBuffer;
	PassParameters->Color = GetScreenPassTextureViewportParameters(ColorViewport);
	PassParameters->Depth = GetScreenPassTextureViewportParameters(DepthViewport);
	PassParameters->ColorToDepth = GetScreenPassTextureViewportTransform(PassParameters->Color, PassParameters->Depth);
	PassParameters->ColorTexture = Inputs.SceneColor.Texture;
	PassParameters->ColorSampler = PointClampSampler;
	PassParameters->DepthTexture = Inputs.SceneDepth.Texture;
	PassParameters->DepthSampler = PointClampSampler;
	PassParameters->EditorPrimitivesDepth = EditorPrimitiveDepth;
	PassParameters->EditorPrimitivesColor = EditorPrimitiveColor;
	PassParameters->bOpaqueEditorGizmo = bOpaqueEditorGizmo;
	PassParameters->bCompositeAnyNonNullDepth = bProducedByPriorPass;

	FCompositeEditorPrimitivesPS::FPermutationDomain PermutationVector;
	PermutationVector.Set<FCompositeEditorPrimitivesPS::FSampleCountDimension>(NumSamples);

	TShaderMapRef<FCompositeEditorPrimitivesPS> PixelShader(View.ShaderMap, PermutationVector);
	AddDrawScreenPass(
		GraphBuilder,
		RDG_EVENT_NAME("Composite %dx%d msaa=%d", OutputViewport.Rect.Width(), OutputViewport.Rect.Height(), NumSamples),
		View,
		OutputViewport,
		ColorViewport,
		PixelShader,
		PassParameters);

	return MoveTemp(Output);
}

#endif