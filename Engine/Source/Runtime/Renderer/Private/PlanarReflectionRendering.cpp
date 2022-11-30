// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
 PlanarReflectionRendering.cpp
=============================================================================*/

#include "PlanarReflectionRendering.h"
#include "Engine/Scene.h"
#include "SceneInterface.h"
#include "RenderingThread.h"
#include "RHIStaticStates.h"
#include "RendererInterface.h"
#include "Camera/CameraTypes.h"
#include "Shader.h"
#include "TextureResource.h"
#include "StaticBoundShaderState.h"
#include "SceneUtils.h"
#include "ScenePrivateBase.h"
#include "PostProcess/SceneRenderTargets.h"
#include "GlobalShader.h"
#include "SceneRenderTargetParameters.h"
#include "SceneRendering.h"
#include "DeferredShadingRenderer.h"
#include "ScenePrivate.h"
#include "PostProcess/SceneFilterRendering.h"
#include "PostProcess/PostProcessing.h"
#include "LightRendering.h"
#include "Components/SceneCaptureComponent.h"
#include "Components/PlanarReflectionComponent.h"
#include "PlanarReflectionSceneProxy.h"
#include "Containers/ArrayView.h"
#include "PipelineStateCache.h"
#include "ClearQuad.h"
#include "SceneTextureParameters.h"
#include "RendererUtils.h"

TAutoConsoleVariable<int32> CVarPlanarReflectionPreferCompute(
	TEXT("r.PlanarReflection.PreferCompute"),
	0,
	TEXT("Will use compute shaders for planar reflection blur."),
	ECVF_RenderThreadSafe);

void SetupPlanarReflectionUniformParameters(const class FSceneView& View, const FPlanarReflectionSceneProxy* ReflectionSceneProxy, FPlanarReflectionUniformParameters& OutParameters)
{
	// Degenerate plane causes shader to branch around the reflection lookup
	OutParameters.ReflectionPlane.Set(0.0f, 0.0f, 0.0f, 0.0f);
	FTexture* PlanarReflectionTextureValue = GBlackTexture;

	if (ReflectionSceneProxy && ReflectionSceneProxy->RenderTarget)
	{
		ensure(ReflectionSceneProxy->ViewRect[0].Min.X >= 0);

		// Need to set W separately due to FVector = FPlane, which sets W to 1.0.
		OutParameters.ReflectionPlane = ReflectionSceneProxy->ReflectionPlane;
		OutParameters.ReflectionPlane.W = ReflectionSceneProxy->ReflectionPlane.W;

		PlanarReflectionTextureValue = ReflectionSceneProxy->RenderTarget;

		FIntPoint BufferSize = ReflectionSceneProxy->RenderTarget->GetSizeXY();
		float InvBufferSizeX = 1.0f / BufferSize.X;
		float InvBufferSizeY = 1.0f / BufferSize.Y;

		FVector2D PlanarReflectionScreenBoundValue(
			1 - 2 * 0.5 / ReflectionSceneProxy->ViewRect[0].Width(),
			1 - 2 * 0.5 / ReflectionSceneProxy->ViewRect[0].Height());

		// Uses hardware's texture unit to reliably clamp UV if the view fill the entire buffer.
		if (View.Family->Views.Num() == 1 &&
			ReflectionSceneProxy->ViewRect[0].Min == FIntPoint::ZeroValue &&
			ReflectionSceneProxy->ViewRect[0].Max == BufferSize)
		{
			PlanarReflectionScreenBoundValue = FVector2D(1, 1);
		}

		FVector4 ScreenScaleBiasValue[2] = {
			FVector4(0, 0, 0, 0),
			FVector4(0, 0, 0, 0),
		};
		for (int32 ViewIndex = 0; ViewIndex < FMath::Min(View.Family->Views.Num(), GMaxPlanarReflectionViews); ViewIndex++)
		{
			FIntRect ViewRect = ReflectionSceneProxy->ViewRect[ViewIndex];
			ScreenScaleBiasValue[ViewIndex] = FVector4(
				ViewRect.Width() * InvBufferSizeX / +2.0f,
				ViewRect.Height() * InvBufferSizeY / (-2.0f * GProjectionSignY),
				(ViewRect.Width() / 2.0f + ViewRect.Min.X) * InvBufferSizeX,
				(ViewRect.Height() / 2.0f + ViewRect.Min.Y) * InvBufferSizeY);
		}

		OutParameters.PlanarReflectionOrigin = ReflectionSceneProxy->PlanarReflectionOrigin;
		OutParameters.PlanarReflectionXAxis = ReflectionSceneProxy->PlanarReflectionXAxis;
		OutParameters.PlanarReflectionYAxis = ReflectionSceneProxy->PlanarReflectionYAxis;
		OutParameters.InverseTransposeMirrorMatrix = ReflectionSceneProxy->InverseTransposeMirrorMatrix;
		OutParameters.PlanarReflectionParameters = ReflectionSceneProxy->PlanarReflectionParameters;
		OutParameters.PlanarReflectionParameters2 = ReflectionSceneProxy->PlanarReflectionParameters2;
		OutParameters.bIsStereo = ReflectionSceneProxy->bIsStereo;
		OutParameters.PlanarReflectionScreenBound = PlanarReflectionScreenBoundValue;

		// Instanced stereo needs both view's values available at once
		if (ReflectionSceneProxy->bIsStereo || View.Family->Views.Num() == 1)
		{
			static_assert(UE_ARRAY_COUNT(ReflectionSceneProxy->ProjectionWithExtraFOV) == 2 
				&& GPlanarReflectionUniformMaxReflectionViews == 2, "Code assumes max 2 planar reflection views.");

			OutParameters.ProjectionWithExtraFOV[0] = ReflectionSceneProxy->ProjectionWithExtraFOV[0];
			OutParameters.ProjectionWithExtraFOV[1] = ReflectionSceneProxy->ProjectionWithExtraFOV[1];

			OutParameters.PlanarReflectionScreenScaleBias[0] = ScreenScaleBiasValue[0];
			OutParameters.PlanarReflectionScreenScaleBias[1] = ScreenScaleBiasValue[1];
		}
		else
		{
			int32 ViewIndex = 0;

			for (int32 i = 0; i < View.Family->Views.Num(); i++)
			{
				if (&View == View.Family->Views[i])
				{
					ViewIndex = i;
					break;
				}
			}

			FMatrix ProjectionWithExtraFOVValue[2];

			// Make sure the current view's value is at index 0
			ProjectionWithExtraFOVValue[0] = ReflectionSceneProxy->ProjectionWithExtraFOV[ViewIndex];
			ProjectionWithExtraFOVValue[1] = FMatrix::Identity;

			ScreenScaleBiasValue[1] = FVector4(0, 0, 0, 0);

			OutParameters.ProjectionWithExtraFOV[0] = ProjectionWithExtraFOVValue[0];
			OutParameters.ProjectionWithExtraFOV[1] = ProjectionWithExtraFOVValue[1];

			OutParameters.PlanarReflectionScreenScaleBias[0] = ScreenScaleBiasValue[0];
			OutParameters.PlanarReflectionScreenScaleBias[1] = ScreenScaleBiasValue[1];
		}
	}
	else
	{
		OutParameters.bIsStereo = false;
	}

	const bool bIsMobilePixelProjectedReflectionEnabled = IsMobilePixelProjectedReflectionEnabled(View.GetShaderPlatform());

	// We don't generate the RHI resource for the planar reflection render target if the mobile pixel projected reflection is enabled
	if (bIsMobilePixelProjectedReflectionEnabled)
	{
		PlanarReflectionTextureValue = GBlackTexture;
	}

	OutParameters.PlanarReflectionTexture = PlanarReflectionTextureValue->TextureRHI;
	OutParameters.PlanarReflectionSampler = PlanarReflectionTextureValue->SamplerStateRHI;
}

IMPLEMENT_GLOBAL_SHADER_PARAMETER_STRUCT(FPlanarReflectionUniformParameters, "PlanarReflectionStruct");


template< bool bEnablePlanarReflectionPrefilter >
class FPrefilterPlanarReflectionPS : public FGlobalShader
{
	DECLARE_SHADER_TYPE(FPrefilterPlanarReflectionPS, Global);
public:

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return bEnablePlanarReflectionPrefilter ? IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5) : true;
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		OutEnvironment.SetDefine(TEXT("ENABLE_PLANAR_REFLECTIONS_PREFILTER"), bEnablePlanarReflectionPrefilter);
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
	}

	/** Default constructor. */
	FPrefilterPlanarReflectionPS() {}

	/** Initialization constructor. */
	FPrefilterPlanarReflectionPS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{
		KernelRadiusY.Bind(Initializer.ParameterMap, TEXT("KernelRadiusY"));
		ValidContentMaskFactor.Bind(Initializer.ParameterMap, TEXT("ValidContentMaskFactor"));
		InvPrefilterRoughnessDistance.Bind(Initializer.ParameterMap, TEXT("InvPrefilterRoughnessDistance"));
		SceneColorInputTexture.Bind(Initializer.ParameterMap, TEXT("SceneColorInputTexture"));
		SceneColorInputSampler.Bind(Initializer.ParameterMap, TEXT("SceneColorInputSampler"));
	}

	void SetParameters(FRHICommandList& RHICmdList, const FSceneView& View, const FPlanarReflectionSceneProxy* ReflectionSceneProxy, FRHITexture* SceneColorInput, int32 FilterWidth)
	{
		FRHIPixelShader* ShaderRHI = RHICmdList.GetBoundPixelShader();
		FGlobalShader::SetParameters<FViewUniformShaderParameters>(RHICmdList, ShaderRHI, View.ViewUniformBuffer);

		const float KernelRadiusYValue = FMath::Clamp(ReflectionSceneProxy->PrefilterRoughness, 0.0f, 0.04f) * 0.5f * FilterWidth;
		SetShaderValue(RHICmdList, ShaderRHI, KernelRadiusY, KernelRadiusYValue);

		const float ValidContentMaskFactorValue = FMath::Clamp(ReflectionSceneProxy->ValidContentMaskFactor, 1.0f, 1000.0f);
		SetShaderValue(RHICmdList, ShaderRHI, ValidContentMaskFactor, ValidContentMaskFactorValue);

		SetShaderValue(RHICmdList, ShaderRHI, InvPrefilterRoughnessDistance, 1.0f / FMath::Max(ReflectionSceneProxy->PrefilterRoughnessDistance, DELTA));

		SetTextureParameter(RHICmdList, ShaderRHI, SceneColorInputTexture, SceneColorInputSampler, TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI(), SceneColorInput);

		FPlanarReflectionUniformParameters PlanarReflectionUniformParameters;
		SetupPlanarReflectionUniformParameters(View, ReflectionSceneProxy, PlanarReflectionUniformParameters);
		SetUniformBufferParameterImmediate(RHICmdList, ShaderRHI, GetUniformBufferParameter<FPlanarReflectionUniformParameters>(), PlanarReflectionUniformParameters);
	}

private:

	LAYOUT_FIELD(FShaderParameter, KernelRadiusY)
	LAYOUT_FIELD(FShaderParameter, ValidContentMaskFactor)
	LAYOUT_FIELD(FShaderParameter, InvPrefilterRoughnessDistance)
	LAYOUT_FIELD(FShaderResourceParameter, SceneColorInputTexture)
	LAYOUT_FIELD(FShaderResourceParameter, SceneColorInputSampler)
};

IMPLEMENT_SHADER_TYPE(template<>, FPrefilterPlanarReflectionPS<false>, TEXT("/Engine/Private/PlanarReflectionShaders.usf"), TEXT("PrefilterPlanarReflectionPS"), SF_Pixel);
IMPLEMENT_SHADER_TYPE(template<>, FPrefilterPlanarReflectionPS<true>, TEXT("/Engine/Private/PlanarReflectionShaders.usf"), TEXT("PrefilterPlanarReflectionPS"), SF_Pixel);

using namespace RendererUtils;

template<bool bEnablePlanarReflectionPrefilter>
void PrefilterPlanarReflection(FRHICommandListImmediate& RHICmdList, FViewInfo& View, const FUniformBufferRHIRef& PassUniformBuffer, FPlanarReflectionSceneProxy* ReflectionSceneProxy, const FRenderTarget* Target)
{
	FRHITexture* SceneColorInput = FSceneRenderTargets::Get(RHICmdList).GetSceneColorTexture();

	if(View.FeatureLevel >= ERHIFeatureLevel::SM5)
	{
		TRefCountPtr<IPooledRenderTarget> FilteredSceneColor;
		GPostProcessing.ProcessPlanarReflection(RHICmdList, View, FilteredSceneColor);

		if (FilteredSceneColor)
		{
			SceneColorInput = FilteredSceneColor->GetRenderTargetItem().ShaderResourceTexture;
		}
	}
	else if (ReflectionSceneProxy->bApplyBlur)
	{
		bool bUseComputeShader = CVarPlanarReflectionPreferCompute.GetValueOnAnyThread() != 0;

		const FIntPoint BufferSize = Target->GetSizeXY();
		const EPixelFormat PixelFormat = Target->GetRenderTargetTexture()->GetFormat();
		FPooledRenderTargetDesc Desc = FPooledRenderTargetDesc::Create2DDesc(BufferSize, PixelFormat,
			FClearValueBinding::White, TexCreate_ShaderResource, TexCreate_RenderTargetable, false);
		Desc.TargetableFlags |= bUseComputeShader ? TexCreate_UAV : TexCreate_None;

		GRenderTargetPool.FindFreeElement(RHICmdList, Desc, ReflectionSceneProxy->HorizontalBlurRenderTarget, TEXT("HorizontalBlurRenderTarget"));
		GRenderTargetPool.FindFreeElement(RHICmdList, Desc, ReflectionSceneProxy->VerticalBlurRenderTarget, TEXT("VerticalBlurRenderTarget"));
		ensure(ReflectionSceneProxy->HorizontalBlurRenderTarget && ReflectionSceneProxy->VerticalBlurRenderTarget);

		FMemMark Mark(FMemStack::Get());
		FRDGBuilder GraphBuilder(RHICmdList);

		FRDGTextureRef SceneColorTexture = GraphBuilder.RegisterExternalTexture(FSceneRenderTargets::Get(RHICmdList).GetSceneColor());
		FRDGTextureRef HorizontalBlurTexture = GraphBuilder.RegisterExternalTexture(ReflectionSceneProxy->HorizontalBlurRenderTarget);
		FRDGTextureRef VerticalBlurTexture = GraphBuilder.RegisterExternalTexture(ReflectionSceneProxy->VerticalBlurRenderTarget);

		RendererUtils::AddGaussianBlurFilter(GraphBuilder,
			View,
			SceneColorTexture,
			HorizontalBlurTexture,
			VerticalBlurTexture,
			bUseComputeShader);

		GraphBuilder.Execute();

		SceneColorInput = ReflectionSceneProxy->VerticalBlurRenderTarget->GetRenderTargetItem().TargetableTexture;
	}
	else
	{
		ReflectionSceneProxy->HorizontalBlurRenderTarget.SafeRelease();
		ReflectionSceneProxy->VerticalBlurRenderTarget.SafeRelease();
	}

	{
		SCOPED_DRAW_EVENT(RHICmdList, PrefilterPlanarReflection);

		FUniformBufferStaticBindings GlobalUniformBuffers(PassUniformBuffer);
		SCOPED_UNIFORM_BUFFER_GLOBAL_BINDINGS(RHICmdList, GlobalUniformBuffers);

		// Workaround for a possible driver bug on S7 Adreno, missing planar reflections
		ERenderTargetLoadAction RTLoadAction = IsVulkanMobilePlatform(View.GetShaderPlatform()) ?  ERenderTargetLoadAction::EClear : ERenderTargetLoadAction::ENoAction;

		RHICmdList.Transition(FRHITransitionInfo(Target->GetRenderTargetTexture(), ERHIAccess::Unknown, ERHIAccess::RTV));

		FRHIRenderPassInfo RPInfo(Target->GetRenderTargetTexture(), MakeRenderTargetActions(RTLoadAction, ERenderTargetStoreAction::EStore));
		RHICmdList.BeginRenderPass(RPInfo, TEXT("PrefilterPlanarReflections"));
		{
			RHICmdList.SetViewport(View.ViewRect.Min.X, View.ViewRect.Min.Y, 0.0f, View.ViewRect.Max.X, View.ViewRect.Max.Y, 1.0f);

			FGraphicsPipelineStateInitializer GraphicsPSOInit;
			RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);
			GraphicsPSOInit.BlendState = TStaticBlendState<>::GetRHI();
			GraphicsPSOInit.RasterizerState = TStaticRasterizerState<FM_Solid, CM_None>::GetRHI();
			GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();

			TShaderMapRef<TDeferredLightVS<false> > VertexShader(View.ShaderMap);
			TShaderMapRef<FPrefilterPlanarReflectionPS<bEnablePlanarReflectionPrefilter> > PixelShader(View.ShaderMap);

			GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GFilterVertexDeclaration.VertexDeclarationRHI;
			GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
			GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
			GraphicsPSOInit.PrimitiveType = PT_TriangleList;

			SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);

			PixelShader->SetParameters(RHICmdList, View, ReflectionSceneProxy, SceneColorInput, View.ViewRect.Width());
			VertexShader->SetSimpleLightParameters(RHICmdList, View, FSphere(0));

			FIntPoint UV = View.ViewRect.Min;
			FIntPoint UVSize = View.ViewRect.Size();

			if (RHINeedsToSwitchVerticalAxis(GShaderPlatformForFeatureLevel[View.FeatureLevel]) && !IsMobileHDR())
			{
				UV.Y = UV.Y + UVSize.Y;
				UVSize.Y = -UVSize.Y;
			}

			DrawRectangle(
				RHICmdList,
				0, 0,
				View.ViewRect.Width(), View.ViewRect.Height(),
				UV.X, UV.Y,
				UVSize.X, UVSize.Y,
				View.ViewRect.Size(),
				FSceneRenderTargets::Get(RHICmdList).GetBufferSizeXY(),
				VertexShader,
				EDRF_UseTriangleOptimization);
		}
		RHICmdList.EndRenderPass();
	}
}

static void UpdatePlanarReflectionContents_RenderThread(
	FRHICommandListImmediate& RHICmdList, 
	FSceneRenderer* MainSceneRenderer, 
	FSceneRenderer* SceneRenderer, 
	FPlanarReflectionSceneProxy* SceneProxy,
	FPlanarReflectionRenderTarget* RenderTarget, 
	FTexture* RenderTargetTexture, 
	const FPlane& MirrorPlane,
	const FName OwnerName, 
	const FResolveParams& ResolveParams, 
	bool bUseSceneColorTexture)
{
	QUICK_SCOPE_CYCLE_COUNTER(STAT_RenderPlanarReflection);

	FMemMark MemStackMark(FMemStack::Get());

	// Make sure we render to the same set of GPUs as the main scene renderer.
	if (MainSceneRenderer->ViewFamily.RenderTarget != nullptr)
	{
		RenderTarget->SetActiveGPUMask(MainSceneRenderer->ViewFamily.RenderTarget->GetGPUMask(RHICmdList));
	}
	else
	{
		RenderTarget->SetActiveGPUMask(FRHIGPUMask::GPU0());
	}

	FBox PlanarReflectionBounds = SceneProxy->WorldBounds;

	bool bIsInAnyFrustum = false;
	for (int32 ViewIndex = 0; ViewIndex < MainSceneRenderer->Views.Num(); ++ViewIndex)
	{
		FViewInfo& View = MainSceneRenderer->Views[ViewIndex];
		if (MirrorPlane.PlaneDot(View.ViewMatrices.GetViewOrigin()) > 0)
		{
			if (View.ViewFrustum.IntersectBox(PlanarReflectionBounds.GetCenter(), PlanarReflectionBounds.GetExtent()))
			{
				bIsInAnyFrustum = true;
				break;
			}
		}
	}

	if (bIsInAnyFrustum)
	{
		bool bIsVisibleInAnyView = true;
		for (int32 ViewIndex = 0; ViewIndex < MainSceneRenderer->Views.Num(); ++ViewIndex)
		{
			FViewInfo& View = MainSceneRenderer->Views[ViewIndex];
			FSceneViewState* ViewState = View.ViewState;

			if (ViewState)
			{
				FIndividualOcclusionHistory& OcclusionHistory = ViewState->PlanarReflectionOcclusionHistories.FindOrAdd(SceneProxy->PlanarReflectionId);

				// +1 to buffered frames because the query is submitted late into the main frame, but read at the beginning of a reflection capture frame
				const int32 NumBufferedFrames = FOcclusionQueryHelpers::GetNumBufferedFrames(SceneRenderer->FeatureLevel) + 1;
				// +1 to frame counter because we are operating before the main view's InitViews, which is where OcclusionFrameCounter is incremented
				uint32 OcclusionFrameCounter = ViewState->OcclusionFrameCounter + 1;
				FRHIRenderQuery* PastQuery = OcclusionHistory.GetPastQuery(OcclusionFrameCounter, NumBufferedFrames);

				if (PastQuery)
				{
					uint64 NumSamples = 0;
					QUICK_SCOPE_CYCLE_COUNTER(STAT_PlanarReflectionOcclusionQueryResults);

					if (RHIGetRenderQueryResult(PastQuery, NumSamples, true))
					{
						bIsVisibleInAnyView = NumSamples > 0;
						if (bIsVisibleInAnyView)
						{
							break;
						}
					}
				}
			}
		}

		if (bIsVisibleInAnyView)
		{
			// update any resources that needed a deferred update
			FDeferredUpdateResource::UpdateResources(RHICmdList);

			{
#if WANTS_DRAW_MESH_EVENTS
				FString EventName;
				OwnerName.ToString(EventName);
				SCOPED_DRAW_EVENTF(RHICmdList, SceneCapture, TEXT("PlanarReflection %s"), *EventName);
#else
				SCOPED_DRAW_EVENT(RHICmdList, UpdatePlanarReflectionContent_RenderThread);
#endif

				const FRenderTarget* Target = SceneRenderer->ViewFamily.RenderTarget;

				// Reflection view late update
				if (SceneRenderer->Views.Num() > 1)
				{
					const FMirrorMatrix MirrorMatrix(MirrorPlane);
					for (int32 ViewIndex = 0; ViewIndex < SceneRenderer->Views.Num(); ++ViewIndex)
					{
						FViewInfo& ReflectionViewToUpdate = SceneRenderer->Views[ViewIndex];
						const FViewInfo& UpdatedParentView = MainSceneRenderer->Views[ViewIndex];

						ReflectionViewToUpdate.UpdatePlanarReflectionViewMatrix(UpdatedParentView, MirrorMatrix);
					}
				}

				// Render the scene normally
				{
					SCOPED_DRAW_EVENT(RHICmdList, RenderScene);
					SceneRenderer->Render(RHICmdList);
				}

				SceneProxy->RenderTarget = RenderTarget;

				// Update the view rects into the planar reflection proxy.
				for (int32 ViewIndex = 0; ViewIndex < SceneRenderer->Views.Num(); ++ViewIndex)
				{
					// Make sure screen percentage has correctly been set on render thread.
					check(SceneRenderer->Views[ViewIndex].ViewRect.Area() > 0);
					SceneProxy->ViewRect[ViewIndex] = SceneRenderer->Views[ViewIndex].ViewRect;
				}

				FUniformBufferRHIRef PassUniformBuffer = CreateSceneTextureUniformBufferDependentOnShadingPath(RHICmdList, SceneRenderer->FeatureLevel);

				for (int32 ViewIndex = 0; ViewIndex < SceneRenderer->Views.Num(); ++ViewIndex)
				{
					FViewInfo& View = SceneRenderer->Views[ViewIndex];
					SCOPED_GPU_MASK(RHICmdList, View.GPUMask);
					if (MainSceneRenderer->Scene->GetShadingPath() == EShadingPath::Deferred)
					{
						PrefilterPlanarReflection<true>(RHICmdList, View, PassUniformBuffer, SceneProxy, Target);
					}
					else
					{
						PrefilterPlanarReflection<false>(RHICmdList, View, PassUniformBuffer, SceneProxy, Target);
					}
				}
				RHICmdList.CopyToResolveTarget(RenderTarget->GetRenderTargetTexture(), RenderTargetTexture->TextureRHI, ResolveParams);
			}
		}
	}
	FSceneRenderer::WaitForTasksClearSnapshotsAndDeleteSceneRenderer(RHICmdList, SceneRenderer);
}

// Used for generate valid data to update planar reflection uniform buffer but don't actually render the reflection scene when we are using mobile pixel projected reflection.
static void UpdatePlanarReflectionContentsWithoutRendering_RenderThread(
	FRHICommandListImmediate& RHICmdList, 
	FSceneRenderer* MainSceneRenderer, 
	FSceneRenderer* SceneRenderer, 
	FPlanarReflectionSceneProxy* SceneProxy,
	FPlanarReflectionRenderTarget* RenderTarget,  
	const FPlane& MirrorPlane,
	const FName OwnerName)
{
	QUICK_SCOPE_CYCLE_COUNTER(STAT_RenderPlanarReflection);

	FMemMark MemStackMark(FMemStack::Get());

	FBox PlanarReflectionBounds = SceneProxy->WorldBounds;

	bool bIsInAnyFrustum = false;
	for (int32 ViewIndex = 0; ViewIndex < MainSceneRenderer->Views.Num(); ++ViewIndex)
	{
		FViewInfo& View = MainSceneRenderer->Views[ViewIndex];
		if (MirrorPlane.PlaneDot(View.ViewMatrices.GetViewOrigin()) > 0)
		{
			if (View.ViewFrustum.IntersectBox(PlanarReflectionBounds.GetCenter(), PlanarReflectionBounds.GetExtent()))
			{
				bIsInAnyFrustum = true;
				break;
			}
		}
	}

	if (bIsInAnyFrustum)
	{
#if WANTS_DRAW_MESH_EVENTS
		FString EventName;
		OwnerName.ToString(EventName);
		SCOPED_DRAW_EVENTF(RHICmdList, SceneCapture, TEXT("PlanarReflection %s"), *EventName);
#else
		SCOPED_DRAW_EVENT(RHICmdList, UpdatePlanarReflectionContent_RenderThread);
#endif

		// Reflection view late update
		if (SceneRenderer->Views.Num() > 1)
		{
			const FMirrorMatrix MirrorMatrix(MirrorPlane);
			for (int32 ViewIndex = 0; ViewIndex < SceneRenderer->Views.Num(); ++ViewIndex)
			{
				FViewInfo& ReflectionViewToUpdate = SceneRenderer->Views[ViewIndex];
				const FViewInfo& UpdatedParentView = MainSceneRenderer->Views[ViewIndex];

				ReflectionViewToUpdate.UpdatePlanarReflectionViewMatrix(UpdatedParentView, MirrorMatrix);
			}
		}

		SceneRenderer->PrepareViewRectsForRendering();

		SceneProxy->RenderTarget = RenderTarget;

		// Update the view rects into the planar reflection proxy.
		for (int32 ViewIndex = 0; ViewIndex < SceneRenderer->Views.Num(); ++ViewIndex)
		{
			// Make sure screen percentage has correctly been set on render thread.
			check(SceneRenderer->Views[ViewIndex].ViewRect.Area() > 0);
			SceneProxy->ViewRect[ViewIndex] = SceneRenderer->Views[ViewIndex].ViewRect;
		}
	}
	FSceneRenderer::WaitForTasksClearSnapshotsAndDeleteSceneRenderer(RHICmdList, SceneRenderer);
}

extern void BuildProjectionMatrix(FIntPoint RenderTargetSize, ECameraProjectionMode::Type ProjectionType, float FOV, float OrthoWidth, float InNearClippingPlane, FMatrix& ProjectionMatrix);

extern void SetupViewFamilyForSceneCapture(
	FSceneViewFamily& ViewFamily,
	USceneCaptureComponent* SceneCaptureComponent,
	const TArrayView<const FSceneCaptureViewInfo> Views,
	float MaxViewDistance,
	bool bCaptureSceneColor,
	bool bIsPlanarReflection,
	FPostProcessSettings* PostProcessSettings,
	float PostProcessBlendWeight,
	const AActor* ViewActor);

void FScene::UpdatePlanarReflectionContents(UPlanarReflectionComponent* CaptureComponent, FSceneRenderer& MainSceneRenderer)
{
	check(CaptureComponent);

	{
		FIntPoint DesiredBufferSize = FSceneRenderer::GetDesiredInternalBufferSize(MainSceneRenderer.ViewFamily);
		FVector2D DesiredPlanarReflectionTextureSizeFloat = FVector2D(DesiredBufferSize.X, DesiredBufferSize.Y) * FMath::Clamp(CaptureComponent->ScreenPercentage / 100.f, 0.25f, 1.f);
		FIntPoint DesiredPlanarReflectionTextureSize;
		DesiredPlanarReflectionTextureSize.X = FMath::Clamp(FMath::CeilToInt(DesiredPlanarReflectionTextureSizeFloat.X), 1, static_cast<int32>(DesiredBufferSize.X));
		DesiredPlanarReflectionTextureSize.Y = FMath::Clamp(FMath::CeilToInt(DesiredPlanarReflectionTextureSizeFloat.Y), 1, static_cast<int32>(DesiredBufferSize.Y));

		const bool bIsMobilePixelProjectedReflectionEnabled = IsMobilePixelProjectedReflectionEnabled(GetShaderPlatform());

		const bool bIsRenderTargetValid = CaptureComponent->RenderTarget != NULL
									&& CaptureComponent->RenderTarget->GetSizeXY() == DesiredPlanarReflectionTextureSize
									// The RenderTarget's TextureRHI could be nullptr if it is used for mobile pixel projected reflection.
									&& (bIsMobilePixelProjectedReflectionEnabled || CaptureComponent->RenderTarget->TextureRHI.IsValid());
		

		if (CaptureComponent->RenderTarget != NULL && !bIsRenderTargetValid)
		{
			FPlanarReflectionRenderTarget* RenderTarget = CaptureComponent->RenderTarget;
			ENQUEUE_RENDER_COMMAND(ReleaseRenderTargetCommand)(
				[RenderTarget](FRHICommandListImmediate& RHICmdList)
				{
					RenderTarget->ReleaseResource();
					delete RenderTarget;
				});

			CaptureComponent->RenderTarget = NULL;
		}

		if (CaptureComponent->RenderTarget == NULL)
		{
			CaptureComponent->RenderTarget = new FPlanarReflectionRenderTarget(DesiredPlanarReflectionTextureSize);

			FPlanarReflectionRenderTarget* RenderTarget = CaptureComponent->RenderTarget;
			FPlanarReflectionSceneProxy* SceneProxy = CaptureComponent->SceneProxy;
			ENQUEUE_RENDER_COMMAND(InitRenderTargetCommand)(
				[RenderTarget, SceneProxy, bIsMobilePixelProjectedReflectionEnabled](FRHICommandListImmediate& RHICmdList)
				{
					// Don't create the RenderTarget's RHI if it is used for mobile pixel projected reflection
					if (!bIsMobilePixelProjectedReflectionEnabled)
					{
						RenderTarget->InitResource();
					}
					SceneProxy->RenderTarget = nullptr;
				});
		}
		else
		{
			// Remove the render target on the planar reflection proxy so that this planar reflection is not getting drawn in its own FSceneRenderer.
			FPlanarReflectionSceneProxy* SceneProxy = CaptureComponent->SceneProxy;
			ENQUEUE_RENDER_COMMAND(InitRenderTargetCommand)(
				[SceneProxy](FRHICommandListImmediate& RHICmdList)
				{
					SceneProxy->RenderTarget = nullptr;
				});
		}

		const FMatrix ComponentTransform = CaptureComponent->GetComponentTransform().ToMatrixWithScale();
		FPlane MirrorPlane = FPlane(ComponentTransform.TransformPosition(FVector::ZeroVector), ComponentTransform.TransformVector(FVector(0, 0, 1)));

		// Normalize the plane to remove component scaling
		bool bNormalized = MirrorPlane.Normalize();

		if (!bNormalized)
		{
			MirrorPlane = FPlane(FVector(0, 0, 1), 0);
		}

		TArray<FSceneCaptureViewInfo> SceneCaptureViewInfo;

		for (int32 ViewIndex = 0; ViewIndex < MainSceneRenderer.Views.Num() && ViewIndex < GMaxPlanarReflectionViews; ++ViewIndex)
		{
			const FViewInfo& View = MainSceneRenderer.Views[ViewIndex];
			FSceneCaptureViewInfo NewView;

			FVector2D ViewRectMin = FVector2D(View.UnscaledViewRect.Min.X, View.UnscaledViewRect.Min.Y);
			FVector2D ViewRectMax = FVector2D(View.UnscaledViewRect.Max.X, View.UnscaledViewRect.Max.Y);
			ViewRectMin *= FMath::Clamp(CaptureComponent->ScreenPercentage / 100.f, 0.25f, 1.f);
			ViewRectMax *= FMath::Clamp(CaptureComponent->ScreenPercentage / 100.f, 0.25f, 1.f);

			NewView.ViewRect.Min.X = FMath::TruncToInt(ViewRectMin.X);
			NewView.ViewRect.Min.Y = FMath::TruncToInt(ViewRectMin.Y);
			NewView.ViewRect.Max.X = FMath::CeilToInt(ViewRectMax.X);
			NewView.ViewRect.Max.Y = FMath::CeilToInt(ViewRectMax.Y);

			// Create a mirror matrix and premultiply the view transform by it
			const FMirrorMatrix MirrorMatrix(MirrorPlane);
			const FMatrix ViewMatrix(MirrorMatrix * View.ViewMatrices.GetViewMatrix());
			const FVector ViewLocation = ViewMatrix.InverseTransformPosition(FVector::ZeroVector);
			const FMatrix ViewRotationMatrix = ViewMatrix.RemoveTranslation();
			const float HalfFOV = FMath::Atan(1.0f / View.ViewMatrices.GetProjectionMatrix().M[0][0]);

			FMatrix ProjectionMatrix;
			BuildProjectionMatrix(View.UnscaledViewRect.Size(), ECameraProjectionMode::Perspective, HalfFOV + FMath::DegreesToRadians(CaptureComponent->ExtraFOV), 1.0f, GNearClippingPlane, ProjectionMatrix);

			NewView.ViewLocation = ViewLocation;
			NewView.ViewRotationMatrix = ViewRotationMatrix;
			NewView.ProjectionMatrix = ProjectionMatrix;
			NewView.StereoPass = View.StereoPass;

			SceneCaptureViewInfo.Add(NewView);
		}
		
		FPostProcessSettings PostProcessSettings;

		FSceneViewFamilyContext ViewFamily(FSceneViewFamily::ConstructionValues(
			CaptureComponent->RenderTarget,
			this,
			CaptureComponent->ShowFlags)
			.SetResolveScene(false)
			.SetRealtimeUpdate(true));

		// Uses the exact same secondary view fraction on the planar reflection as the main viewport.
		ViewFamily.SecondaryViewFraction = MainSceneRenderer.ViewFamily.SecondaryViewFraction;

		SetupViewFamilyForSceneCapture(
			ViewFamily,
			CaptureComponent,
			SceneCaptureViewInfo, CaptureComponent->MaxViewDistanceOverride,
			/* bCaptureSceneColor = */ true, /* bIsPlanarReflection = */ true,
			&PostProcessSettings, 1.0f,
			/*ViewActor =*/ nullptr);

		// Fork main renderer's screen percentage interface to have exactly same settings.
		ViewFamily.EngineShowFlags.ScreenPercentage = MainSceneRenderer.ViewFamily.EngineShowFlags.ScreenPercentage;
		ViewFamily.SetScreenPercentageInterface(FSceneRenderer::ForkScreenPercentageInterface(
			MainSceneRenderer.ViewFamily.GetScreenPercentageInterface(), ViewFamily));

		FSceneRenderer* SceneRenderer = FSceneRenderer::CreateSceneRenderer(&ViewFamily, nullptr);

		// Disable screen percentage on planar reflection renderer if main one has screen percentage disabled.
		SceneRenderer->ViewFamily.EngineShowFlags.ScreenPercentage = MainSceneRenderer.ViewFamily.EngineShowFlags.ScreenPercentage;

		for (int32 ViewIndex = 0; ViewIndex < SceneCaptureViewInfo.Num(); ++ViewIndex)
		{
			SceneRenderer->Views[ViewIndex].GlobalClippingPlane = MirrorPlane;
			// Jitter can't be removed completely due to the clipping plane
			// Also, this prevents the prefilter pass, which reads from jittered depth, from having to do special handling of it's depth-dependent input
			SceneRenderer->Views[ViewIndex].bAllowTemporalJitter = false;
			SceneRenderer->Views[ViewIndex].bRenderSceneTwoSided = CaptureComponent->bRenderSceneTwoSided;

			CaptureComponent->ProjectionWithExtraFOV[ViewIndex] = SceneCaptureViewInfo[ViewIndex].ProjectionMatrix;

			// Plumb down the main view's screen percentage to the planar reflection.
			SceneRenderer->Views[ViewIndex].FinalPostProcessSettings.ScreenPercentage =
				MainSceneRenderer.Views[ViewIndex].FinalPostProcessSettings.ScreenPercentage;

			const bool bIsStereo = IStereoRendering::IsStereoEyeView(MainSceneRenderer.Views[0]);

			const FMatrix ProjectionMatrix = SceneCaptureViewInfo[ViewIndex].ProjectionMatrix;
			FPlanarReflectionSceneProxy* SceneProxy = CaptureComponent->SceneProxy;

			ENQUEUE_RENDER_COMMAND(UpdateProxyCommand)(
				[ProjectionMatrix, ViewIndex, bIsStereo, SceneProxy](FRHICommandList& RHICmdList)
				{
					SceneProxy->ProjectionWithExtraFOV[ViewIndex] = ProjectionMatrix;
					SceneProxy->bIsStereo = bIsStereo;
				});
		}

		{
			const FName OwnerName = CaptureComponent->GetOwner() ? CaptureComponent->GetOwner()->GetFName() : NAME_None;
			FSceneRenderer* MainSceneRendererPtr = &MainSceneRenderer;
			FPlanarReflectionSceneProxy* SceneProxyPtr = CaptureComponent->SceneProxy;
			FPlanarReflectionRenderTarget* RenderTargetPtr = CaptureComponent->RenderTarget;

			if (bIsMobilePixelProjectedReflectionEnabled)
			{
				ENQUEUE_RENDER_COMMAND(CaptureCommand)(
					[SceneRenderer, MirrorPlane, OwnerName, MainSceneRendererPtr, SceneProxyPtr, RenderTargetPtr](FRHICommandListImmediate& RHICmdList)
				{
					UpdatePlanarReflectionContentsWithoutRendering_RenderThread(RHICmdList, MainSceneRendererPtr, SceneRenderer, SceneProxyPtr, RenderTargetPtr, MirrorPlane, OwnerName);
				});
			}
			else
			{
				ENQUEUE_RENDER_COMMAND(CaptureCommand)(
					[SceneRenderer, MirrorPlane, OwnerName, MainSceneRendererPtr, SceneProxyPtr, RenderTargetPtr](FRHICommandListImmediate& RHICmdList)
				{
					UpdatePlanarReflectionContents_RenderThread(RHICmdList, MainSceneRendererPtr, SceneRenderer, SceneProxyPtr, RenderTargetPtr, RenderTargetPtr, MirrorPlane, OwnerName, FResolveParams(), true);
				});
			}
		}
	}
}

void FScene::AddPlanarReflection(UPlanarReflectionComponent* Component)
{
	check(Component->SceneProxy);
	PlanarReflections_GameThread.Add(Component);

	FPlanarReflectionSceneProxy* SceneProxy = Component->SceneProxy;
	FScene* Scene = this;
	ENQUEUE_RENDER_COMMAND(FAddPlanarReflectionCommand)(
		[SceneProxy, Scene](FRHICommandListImmediate& RHICmdList)
		{
			Scene->ReflectionSceneData.bRegisteredReflectionCapturesHasChanged = true;
			Scene->PlanarReflections.Add(SceneProxy);
		});
}

void FScene::RemovePlanarReflection(UPlanarReflectionComponent* Component) 
{
	check(Component->SceneProxy);
	PlanarReflections_GameThread.Remove(Component);

	FPlanarReflectionSceneProxy* SceneProxy = Component->SceneProxy;
	FScene* Scene = this;
	ENQUEUE_RENDER_COMMAND(FRemovePlanarReflectionCommand)(
		[SceneProxy, Scene](FRHICommandListImmediate& RHICmdList)
		{
			Scene->ReflectionSceneData.bRegisteredReflectionCapturesHasChanged = true;
			Scene->PlanarReflections.Remove(SceneProxy);
		});
}

void FScene::UpdatePlanarReflectionTransform(UPlanarReflectionComponent* Component)
{	
	check(Component->SceneProxy);

	FPlanarReflectionSceneProxy* SceneProxy = Component->SceneProxy;
	FMatrix Transform = Component->GetComponentTransform().ToMatrixWithScale();
	FScene* Scene = this;
	ENQUEUE_RENDER_COMMAND(FUpdatePlanarReflectionCommand)(
		[SceneProxy, Transform, Scene](FRHICommandListImmediate& RHICmdList)
		{
			Scene->ReflectionSceneData.bRegisteredReflectionCapturesHasChanged = true;
			SceneProxy->UpdateTransform(Transform);
		});
}

class FPlanarReflectionPS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FPlanarReflectionPS);
	SHADER_USE_PARAMETER_STRUCT(FPlanarReflectionPS, FGlobalShader)

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FSceneTextureParameters, SceneTextures)

		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, ViewUniformBuffer)
		SHADER_PARAMETER_STRUCT_REF(FPlanarReflectionUniformParameters, PlanarReflectionParameters)

		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()
};

IMPLEMENT_GLOBAL_SHADER(FPlanarReflectionPS, "/Engine/Private/PlanarReflectionShaders.usf", "PlanarReflectionPS", SF_Pixel);

bool FDeferredShadingSceneRenderer::HasDeferredPlanarReflections(const FViewInfo& View) const
{
	if (View.bIsPlanarReflection || View.bIsReflectionCapture)
	{
		return false;
	}

	// Prevent rendering unsupported views when ViewIndex >= GMaxPlanarReflectionViews
	// Planar reflections in those views will fallback to other reflection methods
	{
		int32 ViewIndex = INDEX_NONE;

		ViewFamily.Views.Find(&View, ViewIndex);

		if (ViewIndex >= GMaxPlanarReflectionViews)
		{
			return false;
		}
	}

	bool bAnyVisiblePlanarReflections = false;

	for (int32 PlanarReflectionIndex = 0; PlanarReflectionIndex < Scene->PlanarReflections.Num(); PlanarReflectionIndex++)
	{
		FPlanarReflectionSceneProxy* ReflectionSceneProxy = Scene->PlanarReflections[PlanarReflectionIndex];

		if (View.ViewFrustum.IntersectBox(ReflectionSceneProxy->WorldBounds.GetCenter(), ReflectionSceneProxy->WorldBounds.GetExtent()))
		{
			bAnyVisiblePlanarReflections = true;
			break;
		}
	}

	bool bComposePlanarReflections = Scene->PlanarReflections.Num() > 0 && bAnyVisiblePlanarReflections;

	return bComposePlanarReflections;
}

void FDeferredShadingSceneRenderer::RenderDeferredPlanarReflections(FRDGBuilder& GraphBuilder, const FSceneTextureParameters& SceneTextures, const FViewInfo& View, FRDGTextureRef& ReflectionsOutputTexture)
{
	check(HasDeferredPlanarReflections(View));

	// Allocate planar reflection texture
	bool bClearReflectionsOutputTexture = false;
	if (!ReflectionsOutputTexture)
	{
		FRDGTextureDesc Desc = FRDGTextureDesc::Create2D(
			SceneTextures.SceneDepthTexture->Desc.Extent,
			PF_FloatRGBA, FClearValueBinding(FLinearColor(0, 0, 0, 0)),
			TexCreate_ShaderResource | TexCreate_RenderTargetable);

		ReflectionsOutputTexture = GraphBuilder.CreateTexture(Desc, TEXT("PlanarReflections"));
		bClearReflectionsOutputTexture = true;
	}

	FPlanarReflectionPS::FParameters* PassParameters = GraphBuilder.AllocParameters<FPlanarReflectionPS::FParameters>();
	PassParameters->SceneTextures.SceneDepthTexture = SceneTextures.SceneDepthTexture;
	PassParameters->SceneTextures.GBufferATexture = SceneTextures.GBufferATexture;
	PassParameters->SceneTextures.GBufferBTexture = SceneTextures.GBufferBTexture;
	if (IsHlslccShaderPlatform(GMaxRHIShaderPlatform))
	{
		// hlslcc doesn't remove all unused parameters
		PassParameters->SceneTextures.GBufferCTexture = GraphBuilder.RegisterExternalTexture(GSystemTextures.BlackDummy);
	}
	PassParameters->ViewUniformBuffer = View.ViewUniformBuffer;
	PassParameters->RenderTargets[0] = FRenderTargetBinding(
		ReflectionsOutputTexture, bClearReflectionsOutputTexture ? ERenderTargetLoadAction::EClear : ERenderTargetLoadAction::ELoad);

	GraphBuilder.AddPass(
		RDG_EVENT_NAME("CompositePlanarReflections"),
		PassParameters,
		ERDGPassFlags::Raster,
		[PassParameters, &View, this](FRHICommandList& RHICmdList)
	{
		RHICmdList.SetViewport(View.ViewRect.Min.X, View.ViewRect.Min.Y, 0.0f, View.ViewRect.Max.X, View.ViewRect.Max.Y, 1.0f);

		FGraphicsPipelineStateInitializer GraphicsPSOInit;
		RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);

		// Blend over previous reflections in the output target (SSR or planar reflections that have already been rendered)
		// Planar reflections win over SSR and reflection environment
		//@todo - this is order dependent blending, but ordering is coming from registration order
		GraphicsPSOInit.BlendState = TStaticBlendState<CW_RGBA, BO_Add, BF_One, BF_InverseSourceAlpha, BO_Max, BF_One, BF_One>::GetRHI();
		GraphicsPSOInit.RasterizerState = TStaticRasterizerState<FM_Solid, CM_None>::GetRHI();
		GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();

		for (FPlanarReflectionSceneProxy* ReflectionSceneProxy : Scene->PlanarReflections)
		{
			if (!View.ViewFrustum.IntersectBox(ReflectionSceneProxy->WorldBounds.GetCenter(), ReflectionSceneProxy->WorldBounds.GetExtent()))
			{
				continue;
			}

			SCOPED_DRAW_EVENTF(RHICmdList, PlanarReflection, *ReflectionSceneProxy->OwnerName.ToString());

			TShaderMapRef<TDeferredLightVS<false> > VertexShader(View.ShaderMap);
			TShaderMapRef<FPlanarReflectionPS> PixelShader(View.ShaderMap);

			GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GFilterVertexDeclaration.VertexDeclarationRHI;
			GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
			GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
			GraphicsPSOInit.PrimitiveType = PT_TriangleList;

			SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);

			VertexShader->SetSimpleLightParameters(RHICmdList, View, FSphere(0));

			{
				FPlanarReflectionUniformParameters PlanarReflectionUniformParameters;
				SetupPlanarReflectionUniformParameters(View, ReflectionSceneProxy, PlanarReflectionUniformParameters);
		
				FPlanarReflectionPS::FParameters ShaderParameters = *PassParameters;
				ShaderParameters.PlanarReflectionParameters = CreateUniformBufferImmediate(PlanarReflectionUniformParameters, UniformBuffer_SingleDraw);
				SetShaderParameters(RHICmdList, PixelShader, PixelShader.GetPixelShader(), ShaderParameters);
			}

			DrawRectangle(
				RHICmdList,
				0, 0,
				View.ViewRect.Width(), View.ViewRect.Height(),
				View.ViewRect.Min.X, View.ViewRect.Min.Y,
				View.ViewRect.Width(), View.ViewRect.Height(),
				View.ViewRect.Size(),
				FSceneRenderTargets::Get(RHICmdList).GetBufferSizeXY(),
				VertexShader,
				EDRF_UseTriangleOptimization);
		}
	});
}
