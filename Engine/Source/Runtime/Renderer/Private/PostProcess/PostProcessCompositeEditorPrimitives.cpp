// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_EDITOR

#include "PostProcess/PostProcessCompositeEditorPrimitives.h"
#include "EditorPrimitivesRendering.h"
#include "MeshPassProcessor.inl"

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
		SHADER_PARAMETER_RDG_TEXTURE(, EditorPrimitivesDepth)
		SHADER_PARAMETER_RDG_TEXTURE(, EditorPrimitivesColor)
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

void RenderEditorPrimitives(FRHICommandListImmediate& RHICmdList, const FViewInfo& View, FMeshPassProcessorRenderState& DrawRenderState)
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

void RenderForegroundEditorPrimitives(FRHICommandListImmediate& RHICmdList, const FViewInfo& View, FMeshPassProcessorRenderState& DrawRenderState)
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

const FViewInfo* UpdateEditorPrimitiveView(
	FPersistentUniformBuffers& SceneUniformBuffers,
	FSceneRenderTargets& SceneContext,
	const FViewInfo& ParentView,
	FIntRect ViewRect)
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
	EditorView->SetupUniformBufferParameters(SceneContext, VolumeBounds, TVC_MAX, *ViewParameters);
	ViewParameters->NumSceneColorMSAASamples = SceneContext.GetEditorMSAACompositingSampleCount();
	EditorView->CachedViewUniformShaderParameters = MoveTemp(ViewParameters);
	EditorView->ViewUniformBuffer = SceneUniformBuffers.ViewUniformBuffer;
	return EditorView;
}

BEGIN_SHADER_PARAMETER_STRUCT(FEditorPrimitivesPassParameters, )
	SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FOpaqueBasePassUniformParameters, BasePass)
	RENDER_TARGET_BINDING_SLOTS()
END_SHADER_PARAMETER_STRUCT()

FScreenPassTexture AddEditorPrimitivePass(
	FRDGBuilder& GraphBuilder,
	const FViewInfo& View,
	const FEditorPrimitiveInputs& Inputs)
{
	check(Inputs.SceneColor.IsValid());
	check(Inputs.SceneDepth.IsValid());
	check(Inputs.BasePassType != FEditorPrimitiveInputs::EBasePassType::MAX);

	RDG_EVENT_SCOPE(GraphBuilder, "CompositeEditorPrimitives");

	FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(GraphBuilder.RHICmdList);
	FPersistentUniformBuffers& SceneUniformBuffers = View.Family->Scene->GetRenderScene()->UniformBuffers;

	const FViewInfo* EditorView = UpdateEditorPrimitiveView(SceneUniformBuffers, SceneContext, View, Inputs.SceneColor.ViewRect);

	const uint32 MSAASampleCount = SceneContext.GetEditorMSAACompositingSampleCount();

	// Load the color target if it already exists.
	const ERenderTargetLoadAction CompositeLoadAction = SceneContext.EditorPrimitivesColor ? ERenderTargetLoadAction::ELoad : ERenderTargetLoadAction::EClear;

	// These get calls will initialize the targets if they are null.
	SceneContext.GetEditorPrimitivesColor(GraphBuilder.RHICmdList);
	SceneContext.GetEditorPrimitivesDepth(GraphBuilder.RHICmdList);

	FRDGTextureRef EditorPrimitivesColor = GraphBuilder.RegisterExternalTexture(SceneContext.EditorPrimitivesColor);
	FRDGTextureRef EditorPrimitivesDepth = GraphBuilder.RegisterExternalTexture(SceneContext.EditorPrimitivesDepth);

	SceneContext.CleanUpEditorPrimitiveTargets();

	const FScreenPassTextureViewport EditorPrimitivesViewport(EditorPrimitivesColor, Inputs.SceneColor.ViewRect);

	// The editor primitive composition pass is also used when rendering VMI_WIREFRAME in order to use MSAA.
	// So we need to check whether the editor primitives are enabled inside this function.
	if (View.Family->EngineShowFlags.CompositeEditorPrimitives)
	{
		const FScreenPassTextureViewport SceneDepthViewport(Inputs.SceneDepth);

		const bool bPopulateDepth = EditorPrimitivesDepth && CompositeLoadAction == ERenderTargetLoadAction::EClear;

		if (bPopulateDepth)
		{
			FPopulateEditorDepthPS::FParameters* PassParameters = GraphBuilder.AllocParameters<FPopulateEditorDepthPS::FParameters>();
			PassParameters->View = View.ViewUniformBuffer;
			PassParameters->Depth = GetScreenPassTextureViewportParameters(SceneDepthViewport);
			PassParameters->DepthTexture = Inputs.SceneDepth.Texture;
			PassParameters->DepthSampler = TStaticSamplerState<SF_Point, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
			PassParameters->RenderTargets[0] = FRenderTargetBinding(EditorPrimitivesColor, ERenderTargetLoadAction::EClear);
			PassParameters->RenderTargets.DepthStencil = FDepthStencilBinding(EditorPrimitivesDepth, ERenderTargetLoadAction::EClear, ERenderTargetLoadAction::EClear, FExclusiveDepthStencil::DepthWrite_StencilWrite);

			FPopulateEditorDepthPS::FPermutationDomain PermutationVector;
			PermutationVector.Set<FPopulateEditorDepthPS::FUseMSAADimension>(MSAASampleCount > 1);
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
			PassParameters->RenderTargets[0] = FRenderTargetBinding(EditorPrimitivesColor, bPopulateDepth ? ERenderTargetLoadAction::EClear : ERenderTargetLoadAction::ELoad);

			if (EditorPrimitivesDepth)
			{
				PassParameters->RenderTargets.DepthStencil = FDepthStencilBinding(EditorPrimitivesDepth, ERenderTargetLoadAction::ELoad, ERenderTargetLoadAction::ELoad, FExclusiveDepthStencil::DepthWrite_StencilWrite);
			}

			const FEditorPrimitiveInputs::EBasePassType BasePassType = Inputs.BasePassType;

			if (BasePassType == FEditorPrimitiveInputs::EBasePassType::Deferred)
			{
				PassParameters->BasePass = CreateOpaqueBasePassUniformBuffer(GraphBuilder, *EditorView, nullptr, nullptr, 0);
			}

			GraphBuilder.AddPass(
				RDG_EVENT_NAME("EditorPrimitives"),
				PassParameters,
				ERDGPassFlags::Raster,
				[&View, PassParameters, EditorView, &SceneUniformBuffers, EditorPrimitivesViewport, SceneDepthViewport, BasePassType, MSAASampleCount](FRHICommandListImmediate& RHICmdList)
			{
				SceneUniformBuffers.UpdateViewUniformBufferImmediate(*EditorView->CachedViewUniformShaderParameters);

				RHICmdList.SetViewport(EditorPrimitivesViewport.Rect.Min.X, EditorPrimitivesViewport.Rect.Min.Y, 0.0f, EditorPrimitivesViewport.Rect.Max.X, EditorPrimitivesViewport.Rect.Max.Y, 1.0f);

				TUniformBufferRef<FMobileBasePassUniformParameters> MobileBasePassUniformBuffer;
				FRHIUniformBuffer* BasePassUniformBuffer = nullptr;

				if (BasePassType == FEditorPrimitiveInputs::EBasePassType::Mobile)
				{
					CreateMobileBasePassUniformBuffer(RHICmdList, *EditorView, true, false, MobileBasePassUniformBuffer);
					BasePassUniformBuffer = MobileBasePassUniformBuffer;
				}

				FMeshPassProcessorRenderState DrawRenderState(*EditorView, BasePassUniformBuffer);
				DrawRenderState.SetDepthStencilAccess(FExclusiveDepthStencil::DepthWrite_StencilWrite);
				DrawRenderState.SetBlendState(TStaticBlendStateWriteMask<CW_RGBA>::GetRHI());

				// Draw editor primitives.
				{
					SCOPED_DRAW_EVENTF(RHICmdList, EditorPrimitives,
						TEXT("RenderViewEditorPrimitives %dx%d msaa=%d"),
						EditorPrimitivesViewport.Rect.Width(), EditorPrimitivesViewport.Rect.Height(), MSAASampleCount);

					RenderEditorPrimitives(RHICmdList, *EditorView, DrawRenderState);
				}

				// Draw foreground editor primitives.
				{
					SCOPED_DRAW_EVENTF(RHICmdList, EditorPrimitives,
						TEXT("RenderViewEditorForegroundPrimitives %dx%d msaa=%d"),
						EditorPrimitivesViewport.Rect.Width(), EditorPrimitivesViewport.Rect.Height(), MSAASampleCount);

					RenderForegroundEditorPrimitives(RHICmdList, *EditorView, DrawRenderState);
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
	PassParameters->EditorPrimitivesDepth = EditorPrimitivesDepth;
	PassParameters->EditorPrimitivesColor = EditorPrimitivesColor;
	PassParameters->bOpaqueEditorGizmo = bOpaqueEditorGizmo;
	PassParameters->bCompositeAnyNonNullDepth = CompositeLoadAction != ERenderTargetLoadAction::EClear;

	FCompositeEditorPrimitivesPS::FPermutationDomain PermutationVector;
	PermutationVector.Set<FCompositeEditorPrimitivesPS::FSampleCountDimension>(MSAASampleCount);

	TShaderMapRef<FCompositeEditorPrimitivesPS> PixelShader(View.ShaderMap, PermutationVector);
	AddDrawScreenPass(
		GraphBuilder,
		RDG_EVENT_NAME("Composite %dx%d msaa=%d", OutputViewport.Rect.Width(), OutputViewport.Rect.Height(), MSAASampleCount),
		View,
		OutputViewport,
		ColorViewport,
		PixelShader,
		PassParameters);

	return MoveTemp(Output);
}

#endif