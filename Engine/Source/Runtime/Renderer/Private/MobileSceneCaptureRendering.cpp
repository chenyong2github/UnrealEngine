// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
MobileSceneCaptureRendering.cpp - Mobile specific scene capture code.
=============================================================================*/

#include "MobileSceneCaptureRendering.h"
#include "Misc/MemStack.h"
#include "RHIDefinitions.h"
#include "RHI.h"
#include "UnrealClient.h"
#include "SceneInterface.h"
#include "ShaderParameters.h"
#include "RHIStaticStates.h"
#include "RendererInterface.h"
#include "Shader.h"
#include "TextureResource.h"
#include "StaticBoundShaderState.h"
#include "SceneUtils.h"
#include "GlobalShader.h"
#include "SceneRenderTargetParameters.h"
#include "PostProcess/SceneRenderTargets.h"
#include "SceneRendering.h"
#include "RenderTargetPool.h"
#include "PostProcess/SceneFilterRendering.h"
#include "ScreenRendering.h"
#include "ClearQuad.h"
#include "PipelineStateCache.h"
#include "CommonRenderResources.h"
#include "GenerateMips.h"
#include "ScreenPass.h"

/**
* Shader set for the copy of scene color to capture target, alpha channel will contain opacity information. (Determined from depth buffer content)
*/

static const TCHAR* GShaderSourceModeDefineName[] =
{
	TEXT("SOURCE_MODE_SCENE_COLOR_AND_OPACITY"),
	TEXT("SOURCE_MODE_SCENE_COLOR_NO_ALPHA"),
	nullptr,
	TEXT("SOURCE_MODE_SCENE_COLOR_SCENE_DEPTH"),
	TEXT("SOURCE_MODE_SCENE_DEPTH"),
	TEXT("SOURCE_MODE_DEVICE_DEPTH"),
	TEXT("SOURCE_MODE_NORMAL"),
	TEXT("SOURCE_MODE_BASE_COLOR"),
	nullptr
};

class FMobileSceneCaptureCopyPS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FMobileSceneCaptureCopyPS);
	SHADER_USE_PARAMETER_STRUCT(FMobileSceneCaptureCopyPS, FGlobalShader);

	class FCaptureSourceDim : SHADER_PERMUTATION_INT("CAPTURE_SOURCE", ESceneCaptureSource::SCS_MAX);

	using FPermutationDomain = TShaderPermutationDomain<FCaptureSourceDim>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, InTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, InTextureSampler)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) 
	{ 
		FPermutationDomain PermutationVector(Parameters.PermutationId);

		auto CaptureSourceDim = PermutationVector.Get<FCaptureSourceDim>();
		return IsMobilePlatform(Parameters.Platform) && 
			(CaptureSourceDim == SCS_SceneColorHDR
			|| CaptureSourceDim == SCS_FinalColorLDR
			|| CaptureSourceDim == SCS_FinalColorHDR
			|| CaptureSourceDim == SCS_SceneColorHDRNoAlpha
			|| CaptureSourceDim == SCS_SceneColorSceneDepth
			|| CaptureSourceDim == SCS_SceneDepth
			|| CaptureSourceDim == SCS_DeviceDepth);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FPermutationDomain PermutationVector(Parameters.PermutationId);

		auto CaptureSourceDim = PermutationVector.Get<FCaptureSourceDim>();

		OutEnvironment.SetDefine(TEXT("POST_PROCESS_MATERIAL"), 1u); // this will force reading depth from a SceneColor.A
		const TCHAR* DefineName = GShaderSourceModeDefineName[CaptureSourceDim];
		if (DefineName)
		{
			OutEnvironment.SetDefine(DefineName, 1);
		}
	}

	static FPermutationDomain BuildPermutationVector(ESceneCaptureSource CaptureSource)
	{
		FPermutationDomain PermutationVector;
		return PermutationVector;
	}
};

IMPLEMENT_GLOBAL_SHADER(FMobileSceneCaptureCopyPS, "/Engine/Private/MobileSceneCapture.usf", "MainCopyPS", SF_Pixel);

/**
* A vertex shader for rendering a textured screen element.
*/
class FMobileSceneCaptureCopyVS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FMobileSceneCaptureCopyVS);
	SHADER_USE_PARAMETER_STRUCT_WITH_LEGACY_BASE(FMobileSceneCaptureCopyVS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER(FVector2D, InvTexSize)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) { return IsMobilePlatform(Parameters.Platform); }
};

IMPLEMENT_GLOBAL_SHADER(FMobileSceneCaptureCopyVS, "/Engine/Private/MobileSceneCapture.usf", "MainCopyVS", SF_Vertex);
 
// Copies into render target, optionally flipping it in the Y-axis
static void CopyCaptureToTarget(
	FRDGBuilder& GraphBuilder,
	const FRDGTextureRef Target, 
	const FIntPoint& TargetSize, 
	FViewInfo& View, 
	const FIntRect& ViewRect, 
	FRDGTextureRef SourceTexture,
	bool bNeedsFlippedRenderTarget,
	FSceneRenderer* SceneRenderer,
	const FResolveParams& ResolveParams)
{
	FIntPoint SourceTexSize = SourceTexture->Desc.Extent;

	ESceneCaptureSource CaptureSource = View.Family->SceneCaptureSource;

	// Normal and BaseColor not supported on mobile, fall back to scene colour.
	if (CaptureSource == SCS_Normal || CaptureSource == SCS_BaseColor)
	{
		CaptureSource = SCS_SceneColorHDR;
	}

	const bool bEnableTiling = ResolveParams.DestRect.IsValid();

	auto SetViewportIfTiled = [](bool bEnableTiling, bool bNeedsFlippedRenderTarget, const FIntRect& ViewRect, FRDGTextureRef Target, const FResolveParams& ResolveParams, FRHICommandListImmediate& RHICmdList)
	{
		if (!bEnableTiling)
		{
			return;
		}

		if (bNeedsFlippedRenderTarget)
		{
			FResolveRect DestRect = ResolveParams.DestRect;
			int32 TileYID = DestRect.Y1 / ViewRect.Height();
			int32 TileYCount = (Target->Desc.GetSize().Y / ViewRect.Height()) - 1;
			DestRect.Y1 = (TileYCount - TileYID) * ViewRect.Height();
			DestRect.Y2 = DestRect.Y1 + ViewRect.Height();
			RHICmdList.SetViewport
			(
				float(DestRect.X1),
				float(DestRect.Y1),
				0.0f,
				float(DestRect.X2),
				float(DestRect.Y2),
				1.0f
			);
		}
		else
		{
			RHICmdList.SetViewport
			(
				float(ResolveParams.DestRect.X1),
				float(ResolveParams.DestRect.Y1),
				0.0f,
				float(ResolveParams.DestRect.X2),
				float(ResolveParams.DestRect.Y2),
				1.0f
			);
		}
	};

	ESceneCaptureCompositeMode CaptureCompositeMode = View.Family->SceneCaptureCompositeMode;
	{
		ERenderTargetLoadAction RTLoadAction;
		FRHIBlendState* BlendState;
		if (CaptureSource == SCS_SceneColorHDR && CaptureCompositeMode == SCCM_Composite)
		{
			// Blend with existing render target color. Scene capture color is already pre-multiplied by alpha.
			BlendState = TStaticBlendState<CW_RGBA, BO_Add, BF_One, BF_SourceAlpha, BO_Add, BF_Zero, BF_SourceAlpha>::GetRHI();
			RTLoadAction = ERenderTargetLoadAction::ELoad;
		}
		else if (CaptureSource == SCS_SceneColorHDR && CaptureCompositeMode == SCCM_Additive)
		{
			// Add to existing render target color. Scene capture color is already pre-multiplied by alpha.
			BlendState = TStaticBlendState<CW_RGBA, BO_Add, BF_One, BF_One, BO_Add, BF_Zero, BF_SourceAlpha>::GetRHI();
			RTLoadAction = ERenderTargetLoadAction::ELoad;
		}
		else
		{
			RTLoadAction = ERenderTargetLoadAction::ENoAction;
			BlendState = TStaticBlendState<>::GetRHI();
		}

		TShaderMapRef<FMobileSceneCaptureCopyVS> VertexShader(View.ShaderMap);

		FMobileSceneCaptureCopyVS::FParameters VSShaderParameters;

		VSShaderParameters.View = View.ViewUniformBuffer;
		VSShaderParameters.InvTexSize = FVector2D(1.0f / SourceTexSize.X, 1.0f / SourceTexSize.Y);

		auto ShaderPermutationVector = FMobileSceneCaptureCopyPS::BuildPermutationVector(CaptureSource);

		TShaderMapRef<FMobileSceneCaptureCopyPS> PixelShader(View.ShaderMap, ShaderPermutationVector);

		FMobileSceneCaptureCopyPS::FParameters* PSShaderParameters = GraphBuilder.AllocParameters<FMobileSceneCaptureCopyPS::FParameters>();
		PSShaderParameters->View = View.ViewUniformBuffer;
		PSShaderParameters->InTexture = SourceTexture;
		PSShaderParameters->InTextureSampler = TStaticSamplerState<SF_Point>::GetRHI();
		PSShaderParameters->RenderTargets[0] = FRenderTargetBinding(Target, RTLoadAction);

		GraphBuilder.AddPass(
			RDG_EVENT_NAME("CaptureToTarget"),
			PSShaderParameters,
			ERDGPassFlags::Raster,
			[VertexShader, VSShaderParameters, PixelShader, PSShaderParameters, BlendState, bNeedsFlippedRenderTarget, &ViewRect, &TargetSize, SourceTexSize, SetViewportIfTiled, bEnableTiling, Target, ResolveParams](FRHICommandListImmediate& RHICmdList)
		{
			RHICmdList.SetViewport(ViewRect.Min.X, ViewRect.Min.Y, 0.0f, ViewRect.Max.X, ViewRect.Max.Y, 1.0f);

			FGraphicsPipelineStateInitializer GraphicsPSOInit;
			RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);

			GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GFilterVertexDeclaration.VertexDeclarationRHI;
			GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
			GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
			GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();
			GraphicsPSOInit.BlendState = BlendState;
			GraphicsPSOInit.PrimitiveType = PT_TriangleList;
			GraphicsPSOInit.RasterizerState = TStaticRasterizerState<FM_Solid, CM_None>::GetRHI();

			SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);

			SetShaderParameters(RHICmdList, VertexShader, VertexShader.GetVertexShader(), VSShaderParameters);
			SetShaderParameters(RHICmdList, PixelShader, PixelShader.GetPixelShader(), *PSShaderParameters);

			if (bNeedsFlippedRenderTarget)
			{
				SetViewportIfTiled(bEnableTiling, bNeedsFlippedRenderTarget, ViewRect, Target, ResolveParams, RHICmdList);

				DrawRectangle(
					RHICmdList,
					ViewRect.Min.X, ViewRect.Min.Y,
					ViewRect.Width(), ViewRect.Height(),
					ViewRect.Min.X, ViewRect.Height() - ViewRect.Min.Y,
					ViewRect.Width(), -ViewRect.Height(),
					TargetSize,
					SourceTexSize,
					VertexShader,
					EDRF_UseTriangleOptimization);
			}
			else
			{
				SetViewportIfTiled(bEnableTiling, bNeedsFlippedRenderTarget, ViewRect, Target, ResolveParams, RHICmdList);

				DrawRectangle(
					RHICmdList,
					ViewRect.Min.X, ViewRect.Min.Y,
					ViewRect.Width(), ViewRect.Height(),
					ViewRect.Min.X, ViewRect.Min.Y,
					ViewRect.Width(), ViewRect.Height(),
					TargetSize,
					SourceTexSize,
					VertexShader,
					EDRF_UseTriangleOptimization);
			}
		});
	}

	// if opacity is needed.
	if (CaptureSource == SCS_SceneColorHDR)
	{
		// render translucent opacity. (to scene color)
		check(View.Family->Scene->GetShadingPath() == EShadingPath::Mobile);
		FMobileSceneRenderer* MobileSceneRenderer = (FMobileSceneRenderer*)SceneRenderer;

		{
			MobileSceneRenderer->RenderInverseOpacity(GraphBuilder, View);
		}

		// Set capture target.
		FCopyRectPS::FParameters* PSShaderParameters = GraphBuilder.AllocParameters<FCopyRectPS::FParameters>();
		PSShaderParameters->InputTexture = SourceTexture;
		PSShaderParameters->InputSampler = TStaticSamplerState<SF_Point>::GetRHI();
		PSShaderParameters->RenderTargets[0] = FRenderTargetBinding(Target, ERenderTargetLoadAction::ELoad);

		TShaderMapRef<FScreenPassVS> ScreenVertexShader(View.ShaderMap);
		TShaderMapRef<FCopyRectPS> PixelShader(View.ShaderMap);

		GraphBuilder.AddPass(RDG_EVENT_NAME("OpacitySceneCapturePass"), PSShaderParameters, ERDGPassFlags::Raster,
			[&View, ScreenVertexShader, PixelShader, bNeedsFlippedRenderTarget, PSShaderParameters, &ViewRect, &TargetSize, SourceTexSize, SetViewportIfTiled, bEnableTiling, Target, ResolveParams](FRHICommandListImmediate& RHICmdList)
		{
			FGraphicsPipelineStateInitializer GraphicsPSOInit;
			RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);

			GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();
			// Note lack of inverse, both the target and source images are already inverted.
			GraphicsPSOInit.BlendState = TStaticBlendState<CW_ALPHA, BO_Add, BF_DestColor, BF_Zero, BO_Add, BF_Zero, BF_SourceAlpha>::GetRHI();
			GraphicsPSOInit.RasterizerState = TStaticRasterizerState<FM_Solid, CM_None>::GetRHI();
			GraphicsPSOInit.PrimitiveType = PT_TriangleList;

			// Combine translucent opacity pass to earlier opaque pass to build final inverse opacity.
			GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GFilterVertexDeclaration.VertexDeclarationRHI;
			GraphicsPSOInit.BoundShaderState.VertexShaderRHI = ScreenVertexShader.GetVertexShader();
			GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
			GraphicsPSOInit.PrimitiveType = PT_TriangleList;

			SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);

			SetShaderParameters(RHICmdList, PixelShader, PixelShader.GetPixelShader(), *PSShaderParameters);

			int32 TargetPosY = ViewRect.Min.Y;
			int32 TargetHeight = ViewRect.Height();

			SetViewportIfTiled(bEnableTiling, bNeedsFlippedRenderTarget, ViewRect, Target, ResolveParams, RHICmdList);

			if (bNeedsFlippedRenderTarget)
			{
				TargetPosY = ViewRect.Height() - TargetPosY;
				TargetHeight = -TargetHeight;
			}

			DrawRectangle(
				RHICmdList,
				ViewRect.Min.X, ViewRect.Min.Y,
				ViewRect.Width(), ViewRect.Height(),
				ViewRect.Min.X, TargetPosY,
				ViewRect.Width(), TargetHeight,
				TargetSize,
				SourceTexSize,
				ScreenVertexShader,
				EDRF_UseTriangleOptimization);
		});
	}
}

void UpdateSceneCaptureContentMobile_RenderThread(
	FRHICommandListImmediate& RHICmdList,
	FSceneRenderer* SceneRenderer,
	FRenderTarget* RenderTarget,
	FTexture* RenderTargetTexture,
	const FString& EventName,
	const FResolveParams& ResolveParams,
	bool bGenerateMips,
	const FGenerateMipsParams& GenerateMipsParams,
	bool bDisableFlipCopyGLES)
{
	SceneRenderer->RenderThreadBegin(RHICmdList);

	// update any resources that needed a deferred update
	FDeferredUpdateResource::UpdateResources(RHICmdList);
	bool bUseSceneTextures = SceneRenderer->ViewFamily.SceneCaptureSource != SCS_FinalColorLDR &&
								SceneRenderer->ViewFamily.SceneCaptureSource != SCS_FinalColorHDR;

	{
#if WANTS_DRAW_MESH_EVENTS
		SCOPED_DRAW_EVENTF(RHICmdList, SceneCaptureMobile, TEXT("SceneCaptureMobile %s"), *EventName);
		FRDGBuilder GraphBuilder(RHICmdList, RDG_EVENT_NAME("SceneCaptureMobile %s", *EventName));
#else
		SCOPED_DRAW_EVENT(RHICmdList, UpdateSceneCaptureContentMobile_RenderThread);
		FRDGBuilder GraphBuilder(RHICmdList, RDG_EVENT_NAME("SceneCaptureMobile"));
#endif

		FViewInfo& View = SceneRenderer->Views[0];

		const bool bIsMobileHDR = IsMobileHDR();
		const bool bRHINeedsFlip = RHINeedsToSwitchVerticalAxis(GMaxRHIShaderPlatform) && !bDisableFlipCopyGLES;
		// note that GLES code will flip the image when:
		//	bIsMobileHDR && SceneCaptureSource == SCS_FinalColorLDR (flip performed during post processing)
		//	!bIsMobileHDR (rendering is flipped by vertex shader)
		// they need flipping again so it is correct for texture addressing.
		const bool bNeedsFlippedCopy = (!bIsMobileHDR || !bUseSceneTextures) && bRHINeedsFlip;
		const bool bNeedsFlippedFinalColor = bNeedsFlippedCopy && !bUseSceneTextures;

		// Intermediate render target that will need to be flipped (needed on !IsMobileHDR())
		FRDGTextureRef FlippedOutputTexture{};

		const FRenderTarget* Target = SceneRenderer->ViewFamily.RenderTarget;
		if (bNeedsFlippedFinalColor)
		{
			// We need to use an intermediate render target since the result will be flipped
			auto& RenderTargetRHI = Target->GetRenderTargetTexture();
			FRDGTextureDesc Desc(FRDGTextureDesc::Create2D(
				Target->GetSizeXY(),
				RenderTargetRHI.GetReference()->GetFormat(),
				RenderTargetRHI.GetReference()->GetClearBinding(),
				TexCreate_RenderTargetable));
			FlippedOutputTexture = GraphBuilder.CreateTexture(Desc, TEXT("SceneCaptureFlipped"));
		}

		// We don't support screen percentage in scene capture.
		FIntRect ViewRect = View.UnscaledViewRect;
		FIntRect UnconstrainedViewRect = View.UnconstrainedViewRect;

		if(bNeedsFlippedFinalColor)
		{
			AddClearRenderTargetPass(GraphBuilder, FlippedOutputTexture, FLinearColor::Black, ViewRect);
		}

		// Register pass for InverseOpacity for this scope
		extern FMeshPassProcessor* CreateMobileInverseOpacityPassProcessor(const FScene* Scene, const FSceneView* InViewIfDynamicMeshCommand, FMeshPassDrawListContext* InDrawListContext);
		FRegisterPassProcessorCreateFunction RegisterMobileInverseOpacityPass(&CreateMobileInverseOpacityPassProcessor, EShadingPath::Mobile, EMeshPass::MobileInverseOpacity, EMeshPassFlags::MainView);
		
		// Render the scene normally
		{
			RDG_RHI_EVENT_SCOPE(GraphBuilder, RenderScene);

			if (bNeedsFlippedFinalColor)
			{
				// Helper class to allow setting render target
				struct FRenderTargetOverride : public FRenderTarget
				{
					FRenderTargetOverride(const FRenderTarget* TargetIn, FRHITexture2D* OverrideTexture)
					{
						RenderTargetTextureRHI = OverrideTexture;
						OriginalTarget = TargetIn;
					}

					virtual FIntPoint GetSizeXY() const override { return FIntPoint(RenderTargetTextureRHI->GetSizeX(), RenderTargetTextureRHI->GetSizeY()); }
					virtual float GetDisplayGamma() const override { return OriginalTarget->GetDisplayGamma(); }

					FTexture2DRHIRef GetTextureParamRef() { return RenderTargetTextureRHI; }
					const FRenderTarget* OriginalTarget;
				};

				// Hijack the render target
				FRHITexture2D* FlippedOutputTextureRHI = GraphBuilder.ConvertToExternalTexture(FlippedOutputTexture)->GetTargetableRHI()->GetTexture2D();
				SceneRenderer->ViewFamily.RenderTarget = GraphBuilder.AllocObject<FRenderTargetOverride>(Target, FlippedOutputTextureRHI); //-V506
			}

			SceneRenderer->Render(GraphBuilder);

			if (bNeedsFlippedFinalColor)
			{
				// And restore it
				SceneRenderer->ViewFamily.RenderTarget = Target;
			}
		}

		FRDGTextureRef OutputTexture = RegisterExternalTexture(GraphBuilder, Target->GetRenderTargetTexture(), TEXT("OutputTexture"));

		const FIntPoint TargetSize(UnconstrainedViewRect.Width(), UnconstrainedViewRect.Height());
		if (bNeedsFlippedFinalColor)
		{
			// We need to flip this texture upside down (since we depended on tonemapping to fix this on the hdr path)
			RDG_EVENT_SCOPE(GraphBuilder, "FlipCapture");
			CopyCaptureToTarget(GraphBuilder, OutputTexture, TargetSize, View, ViewRect, FlippedOutputTexture, bNeedsFlippedCopy, SceneRenderer, ResolveParams);
		}
		else if(bUseSceneTextures)
		{
			const FMinimalSceneTextures& SceneTextures = FSceneTextures::Get(GraphBuilder);

			// Copy the captured scene into the destination texture
			RDG_EVENT_SCOPE(GraphBuilder, "CaptureSceneColor");
			CopyCaptureToTarget(GraphBuilder, OutputTexture, TargetSize, View, ViewRect, SceneTextures.Color.Target, bNeedsFlippedCopy, SceneRenderer, ResolveParams);
		}

		if (bGenerateMips)
		{
			FGenerateMips::Execute(GraphBuilder, OutputTexture, GenerateMipsParams);
		}

		GraphBuilder.Execute();
	}

	SceneRenderer->RenderThreadEnd(RHICmdList);
}
