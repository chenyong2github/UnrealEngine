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

template<ESceneCaptureSource CaptureSource>
class FMobileSceneCaptureCopyPS : public FGlobalShader
{
	DECLARE_SHADER_TYPE(FMobileSceneCaptureCopyPS, Global)
public:

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) { return IsMobilePlatform(Parameters.Platform); }

	FMobileSceneCaptureCopyPS(const ShaderMetaType::CompiledShaderInitializerType& Initializer) :
		FGlobalShader(Initializer)
	{
		InTexture.Bind(Initializer.ParameterMap, TEXT("InTexture"), SPF_Mandatory);
		InTextureSampler.Bind(Initializer.ParameterMap, TEXT("InTextureSampler"));
	}
	FMobileSceneCaptureCopyPS() {}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		OutEnvironment.SetDefine(TEXT("POST_PROCESS_MATERIAL"), 1u); // this will force reading depth from a SceneColor.A
		const TCHAR* DefineName = GShaderSourceModeDefineName[CaptureSource];
		if (DefineName)
		{
			OutEnvironment.SetDefine(DefineName, 1);
		}
	}

	void SetParameters(FRHICommandList& RHICmdList, const FViewInfo& View, FRHISamplerState* SamplerStateRHI, FRHITexture* TextureRHI)
	{
		FGlobalShader::SetParameters<FViewUniformShaderParameters>(RHICmdList, RHICmdList.GetBoundPixelShader(), View.ViewUniformBuffer);
		SetTextureParameter(RHICmdList, RHICmdList.GetBoundPixelShader(), InTexture, InTextureSampler, SamplerStateRHI, TextureRHI);
	}

private:
	LAYOUT_FIELD(FShaderResourceParameter, InTexture)
	LAYOUT_FIELD(FShaderResourceParameter, InTextureSampler)
};

/**
* A vertex shader for rendering a textured screen element.
*/
class FMobileSceneCaptureCopyVS : public FGlobalShader
{
	DECLARE_SHADER_TYPE(FMobileSceneCaptureCopyVS, Global)
public:

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) { return IsMobilePlatform(Parameters.Platform); }

	FMobileSceneCaptureCopyVS(const ShaderMetaType::CompiledShaderInitializerType& Initializer) :
		FGlobalShader(Initializer)
	{
		InvTexSizeParameter.Bind(Initializer.ParameterMap, TEXT("InvTexSize"));
	}
	FMobileSceneCaptureCopyVS() {}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
	}

	void SetParameters(FRHICommandList& RHICmdList, const FViewInfo& View, const FIntPoint& SourceTexSize)
	{
		FGlobalShader::SetParameters<FViewUniformShaderParameters>(RHICmdList, RHICmdList.GetBoundVertexShader(), View.ViewUniformBuffer);
		if (InvTexSizeParameter.IsBound())
		{
			FVector2D InvTexSize(1.0f / SourceTexSize.X, 1.0f / SourceTexSize.Y);
			SetShaderValue(RHICmdList, RHICmdList.GetBoundVertexShader(), InvTexSizeParameter, InvTexSize);
		}
	}

	LAYOUT_FIELD(FShaderParameter, InvTexSizeParameter)
};

#define IMPLEMENT_MOBILE_SCENE_CAPTURECOPY(SCENETYPE) \
typedef FMobileSceneCaptureCopyPS<SCENETYPE> FMobileSceneCaptureCopyPS##SCENETYPE;\
IMPLEMENT_SHADER_TYPE(template<>, FMobileSceneCaptureCopyPS##SCENETYPE, TEXT("/Engine/Private/MobileSceneCapture.usf"), TEXT("MainCopyPS"), SF_Pixel); \

IMPLEMENT_MOBILE_SCENE_CAPTURECOPY(SCS_SceneColorHDR);
IMPLEMENT_MOBILE_SCENE_CAPTURECOPY(SCS_FinalColorLDR);
IMPLEMENT_MOBILE_SCENE_CAPTURECOPY(SCS_FinalColorHDR);
IMPLEMENT_MOBILE_SCENE_CAPTURECOPY(SCS_SceneColorHDRNoAlpha);
IMPLEMENT_MOBILE_SCENE_CAPTURECOPY(SCS_SceneColorSceneDepth);
IMPLEMENT_MOBILE_SCENE_CAPTURECOPY(SCS_SceneDepth);
IMPLEMENT_MOBILE_SCENE_CAPTURECOPY(SCS_DeviceDepth);
IMPLEMENT_SHADER_TYPE(, FMobileSceneCaptureCopyVS, TEXT("/Engine/Private/MobileSceneCapture.usf"), TEXT("MainCopyVS"), SF_Vertex);
 

template <ESceneCaptureSource CaptureSource>
static TShaderRef<FShader> SetCaptureToTargetShaders(FRHICommandListImmediate& RHICmdList, FGraphicsPipelineStateInitializer& GraphicsPSOInit, FViewInfo& View, const FIntPoint& SourceTexSize, FRHITexture* SourceTextureRHI)
{
	TShaderMapRef<FMobileSceneCaptureCopyVS> VertexShader(View.ShaderMap);
	TShaderMapRef<FMobileSceneCaptureCopyPS<CaptureSource>> PixelShader(View.ShaderMap);

	GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GFilterVertexDeclaration.VertexDeclarationRHI;
	GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
	GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
	SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);

	VertexShader->SetParameters(RHICmdList, View, SourceTexSize);
	PixelShader->SetParameters(RHICmdList, View, TStaticSamplerState<SF_Point>::GetRHI(), SourceTextureRHI);

	return VertexShader;
}

static TShaderRef<FShader> SetCaptureToTargetShaders(FRHICommandListImmediate& RHICmdList, FGraphicsPipelineStateInitializer& GraphicsPSOInit, ESceneCaptureSource CaptureSource, FViewInfo& View, const FIntPoint& SourceTexSize, FRHITexture* SourceTextureRHI)
{
	switch (CaptureSource)
	{
		case SCS_SceneColorHDR:
			return SetCaptureToTargetShaders<SCS_SceneColorHDR>(RHICmdList, GraphicsPSOInit, View, SourceTexSize, SourceTextureRHI);
		case SCS_FinalColorLDR:
			return SetCaptureToTargetShaders<SCS_FinalColorLDR>(RHICmdList, GraphicsPSOInit, View, SourceTexSize, SourceTextureRHI);
		case SCS_FinalColorHDR:
			return SetCaptureToTargetShaders<SCS_FinalColorHDR>(RHICmdList, GraphicsPSOInit, View, SourceTexSize, SourceTextureRHI);
		case SCS_SceneColorHDRNoAlpha:
			return SetCaptureToTargetShaders<SCS_SceneColorHDRNoAlpha>(RHICmdList, GraphicsPSOInit, View, SourceTexSize, SourceTextureRHI);
		case SCS_SceneColorSceneDepth:
			return SetCaptureToTargetShaders<SCS_SceneColorSceneDepth>(RHICmdList, GraphicsPSOInit, View, SourceTexSize, SourceTextureRHI);
		case SCS_SceneDepth:
			return SetCaptureToTargetShaders<SCS_SceneDepth>(RHICmdList, GraphicsPSOInit, View, SourceTexSize, SourceTextureRHI);
		case SCS_DeviceDepth:
			return SetCaptureToTargetShaders<SCS_DeviceDepth>(RHICmdList, GraphicsPSOInit, View, SourceTexSize, SourceTextureRHI);
		default:
			checkNoEntry();
			return TShaderRef<FShader>();
	}
}

// Copies into render target, optionally flipping it in the Y-axis
static void CopyCaptureToTarget(
	FRHICommandListImmediate& RHICmdList, 
	const FRenderTarget* Target, 
	const FIntPoint& TargetSize, 
	FViewInfo& View, 
	const FIntRect& ViewRect, 
	FRHITexture2D* SourceTextureRHI,
	bool bNeedsFlippedRenderTarget,
	FSceneRenderer* SceneRenderer)
{
	check(SourceTextureRHI);
	check(RHICmdList.IsOutsideRenderPass());

	FGraphicsPipelineStateInitializer GraphicsPSOInit;
	ESceneCaptureSource CaptureSource = View.Family->SceneCaptureSource;
	ESceneCaptureCompositeMode CaptureCompositeMode = View.Family->SceneCaptureCompositeMode;

	// Normal and BaseColor not supported on mobile, fall back to scene colour.
	if (CaptureSource == SCS_Normal || CaptureSource == SCS_BaseColor)
	{
		CaptureSource = SCS_SceneColorHDR;
	}

	ERenderTargetLoadAction RTLoadAction;
	if (CaptureSource == SCS_SceneColorHDR && CaptureCompositeMode == SCCM_Composite)
	{
		// Blend with existing render target color. Scene capture color is already pre-multiplied by alpha.
		GraphicsPSOInit.BlendState = TStaticBlendState<CW_RGBA, BO_Add, BF_One, BF_SourceAlpha, BO_Add, BF_Zero, BF_SourceAlpha>::GetRHI();
		RTLoadAction = ERenderTargetLoadAction::ELoad;
	}
	else if (CaptureSource == SCS_SceneColorHDR && CaptureCompositeMode == SCCM_Additive)
	{
		// Add to existing render target color. Scene capture color is already pre-multiplied by alpha.
		GraphicsPSOInit.BlendState = TStaticBlendState<CW_RGBA, BO_Add, BF_One, BF_One, BO_Add, BF_Zero, BF_SourceAlpha>::GetRHI();
		RTLoadAction = ERenderTargetLoadAction::ELoad;
	}
	else
	{
		RTLoadAction = ERenderTargetLoadAction::ENoAction;
		GraphicsPSOInit.BlendState = TStaticBlendState<>::GetRHI();
	}

	GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();
	FIntPoint SourceTexSize = SourceTextureRHI->GetSizeXY();
	
	{
		RHICmdList.Transition(FRHITransitionInfo(Target->GetRenderTargetTexture(), ERHIAccess::Unknown,ERHIAccess::RTV));
		FRHIRenderPassInfo RPInfo(Target->GetRenderTargetTexture(), MakeRenderTargetActions(RTLoadAction, ERenderTargetStoreAction::EStore));
		RHICmdList.BeginRenderPass(RPInfo, TEXT("CaptureToTarget"));
		RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);

		GraphicsPSOInit.PrimitiveType = PT_TriangleList;
		GraphicsPSOInit.RasterizerState = TStaticRasterizerState<FM_Solid, CM_None>::GetRHI();

		TShaderRef<FShader> VertexShader = SetCaptureToTargetShaders(RHICmdList, GraphicsPSOInit, CaptureSource, View, SourceTexSize, SourceTextureRHI);

		if (bNeedsFlippedRenderTarget)
		{
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
		RHICmdList.EndRenderPass();
	}
	

	// if opacity is needed.
	if (CaptureSource == SCS_SceneColorHDR)
	{
		// render translucent opacity. (to scene color)
		check(View.Family->Scene->GetShadingPath() == EShadingPath::Mobile);
		FMobileSceneRenderer* MobileSceneRenderer = (FMobileSceneRenderer*)SceneRenderer;

		{
			MobileSceneRenderer->RenderInverseOpacity(RHICmdList, View);
		}		

		// Set capture target.
		FRHIRenderPassInfo RPInfo(Target->GetRenderTargetTexture(), ERenderTargetActions::Load_Store);
		RHICmdList.BeginRenderPass(RPInfo, TEXT("OpacitySceneCapture"));
		{
			RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);

			GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();
			// Note lack of inverse, both the target and source images are already inverted.
			GraphicsPSOInit.BlendState = TStaticBlendState<CW_ALPHA, BO_Add, BF_DestColor, BF_Zero, BO_Add, BF_Zero, BF_SourceAlpha>::GetRHI();
			GraphicsPSOInit.RasterizerState = TStaticRasterizerState<FM_Solid, CM_None>::GetRHI();
			GraphicsPSOInit.PrimitiveType = PT_TriangleList;

			// Combine translucent opacity pass to earlier opaque pass to build final inverse opacity.
			TShaderMapRef<FScreenVS> ScreenVertexShader(View.ShaderMap);
			TShaderMapRef<FScreenPS> PixelShader(View.ShaderMap);

			GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GFilterVertexDeclaration.VertexDeclarationRHI;
			GraphicsPSOInit.BoundShaderState.VertexShaderRHI = ScreenVertexShader.GetVertexShader();
			GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
			GraphicsPSOInit.PrimitiveType = PT_TriangleList;

			SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);

			ScreenVertexShader->SetParameters(RHICmdList, View.ViewUniformBuffer);
			PixelShader->SetParameters(RHICmdList, TStaticSamplerState<SF_Point>::GetRHI(), SourceTextureRHI);

			int32 TargetPosY = ViewRect.Min.Y;
			int32 TargetHeight = ViewRect.Height();

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
		}
		RHICmdList.EndRenderPass();
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
	FMemMark MemStackMark(FMemStack::Get());

	// update any resources that needed a deferred update
	FDeferredUpdateResource::UpdateResources(RHICmdList);
	bool bUseSceneTextures = SceneRenderer->ViewFamily.SceneCaptureSource != SCS_FinalColorLDR &&
								SceneRenderer->ViewFamily.SceneCaptureSource != SCS_FinalColorHDR;

	{
#if WANTS_DRAW_MESH_EVENTS
		SCOPED_DRAW_EVENTF(RHICmdList, SceneCaptureMobile, TEXT("SceneCaptureMobile %s"), *EventName);
#else
		SCOPED_DRAW_EVENT(RHICmdList, UpdateSceneCaptureContentMobile_RenderThread);
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
		TRefCountPtr<IPooledRenderTarget> FlippedPooledRenderTarget;

		const FRenderTarget* Target = SceneRenderer->ViewFamily.RenderTarget;
		if (bNeedsFlippedFinalColor)
		{
			// We need to use an intermediate render target since the result will be flipped
			auto& RenderTargetRHI = Target->GetRenderTargetTexture();
			FPooledRenderTargetDesc Desc(FPooledRenderTargetDesc::Create2DDesc(Target->GetSizeXY(),
				RenderTargetRHI.GetReference()->GetFormat(),
				RenderTargetRHI.GetReference()->GetClearBinding(),
				TexCreate_None,
				TexCreate_RenderTargetable,
				false));
			GRenderTargetPool.FindFreeElement(RHICmdList, Desc, FlippedPooledRenderTarget, TEXT("SceneCaptureFlipped"));
		}

		// Helper class to allow setting render target
		struct FRenderTargetOverride : public FRenderTarget
		{
			FRenderTargetOverride(const FRenderTarget* TargetIn, FRHITexture2D* In)
			{
				RenderTargetTextureRHI = In;
				OriginalTarget = TargetIn;
			}

			virtual FIntPoint GetSizeXY() const override { return FIntPoint(RenderTargetTextureRHI->GetSizeX(), RenderTargetTextureRHI->GetSizeY()); }
			virtual float GetDisplayGamma() const override { return OriginalTarget->GetDisplayGamma(); }

			FTexture2DRHIRef GetTextureParamRef() { return RenderTargetTextureRHI; }
			const FRenderTarget* OriginalTarget;
		} FlippedRenderTarget(Target, 
			FlippedPooledRenderTarget.GetReference()
			? FlippedPooledRenderTarget.GetReference()->GetRenderTargetItem().TargetableTexture->GetTexture2D()
			: nullptr);

		// We don't support screen percentage in scene capture.
		FIntRect ViewRect = View.UnscaledViewRect;
		FIntRect UnconstrainedViewRect = View.UnconstrainedViewRect;

		if(bNeedsFlippedFinalColor)
		{
			auto& RenderTargetRHI = Target->GetRenderTargetTexture();
			
			FRHIRenderPassInfo RPInfo(RenderTargetRHI, ERenderTargetActions::DontLoad_Store);
			TransitionRenderPassTargets(RHICmdList, RPInfo);
			RHICmdList.BeginRenderPass(RPInfo, TEXT("Clear"));
			DrawClearQuad(RHICmdList, true, FLinearColor::Black, false, 0, false, 0, Target->GetSizeXY(), ViewRect);
			RHICmdList.EndRenderPass();
		}

		// Register pass for InverseOpacity for this scope
		extern FMeshPassProcessor* CreateMobileInverseOpacityPassProcessor(const FScene* Scene, const FSceneView* InViewIfDynamicMeshCommand, FMeshPassDrawListContext* InDrawListContext);
		FRegisterPassProcessorCreateFunction RegisterMobileInverseOpacityPass(&CreateMobileInverseOpacityPassProcessor, EShadingPath::Mobile, EMeshPass::MobileInverseOpacity, EMeshPassFlags::MainView);
		
		// Render the scene normally
		{
			SCOPED_DRAW_EVENT(RHICmdList, RenderScene);

			if (bNeedsFlippedFinalColor)
			{
				// Hijack the render target
				SceneRenderer->ViewFamily.RenderTarget = &FlippedRenderTarget; //-V506
			}

			SceneRenderer->Render(RHICmdList);

			if (bNeedsFlippedFinalColor)
			{
				// And restore it
				SceneRenderer->ViewFamily.RenderTarget = Target;
			}
		}

		const FIntPoint TargetSize(UnconstrainedViewRect.Width(), UnconstrainedViewRect.Height());
		if (bNeedsFlippedFinalColor)
		{
			// We need to flip this texture upside down (since we depended on tonemapping to fix this on the hdr path)
			SCOPED_DRAW_EVENT(RHICmdList, FlipCapture);
			CopyCaptureToTarget(RHICmdList, Target, TargetSize, View, ViewRect, FlippedRenderTarget.GetTextureParamRef(), bNeedsFlippedCopy, SceneRenderer);
		}
		else if(bUseSceneTextures)
		{
			// Copy the captured scene into the destination texture
			SCOPED_DRAW_EVENT(RHICmdList, CaptureSceneColor);
			// If multiview is enabled, the SceneColorTexture will be an array texture.
			if (View.bIsMobileMultiViewEnabled)
			{
				CopyCaptureToTarget(RHICmdList, Target, TargetSize, View, ViewRect, FSceneRenderTargets::Get(RHICmdList).GetSceneColorTexture()->GetTexture2DArray(), bNeedsFlippedCopy, SceneRenderer);
			}
			else
			{
				CopyCaptureToTarget(RHICmdList, Target, TargetSize, View, ViewRect, FSceneRenderTargets::Get(RHICmdList).GetSceneColorTexture()->GetTexture2D(), bNeedsFlippedCopy, SceneRenderer);
			}
		}

		FRDGBuilder GraphBuilder(RHICmdList);
		{
			FRDGTextureRef MipTexture = GraphBuilder.RegisterExternalTexture(CreateRenderTarget(RenderTarget->GetRenderTargetTexture(), TEXT("MipGenerationInput")));
			FRDGTextureRef OutputTexture = GraphBuilder.RegisterExternalTexture(CreateRenderTarget(RenderTargetTexture->TextureRHI, TEXT("MipGenerationOutput")));

			if (bGenerateMips)
			{
				FGenerateMips::Execute(GraphBuilder, MipTexture, GenerateMipsParams);
			}

			AddCopyToResolveTargetPass(GraphBuilder, MipTexture, OutputTexture, ResolveParams);
		}
		GraphBuilder.Execute();
	}
	FSceneRenderer::WaitForTasksClearSnapshotsAndDeleteSceneRenderer(RHICmdList, SceneRenderer);
}
