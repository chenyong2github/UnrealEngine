// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	PostProcessSubsurface.cpp: Screenspace subsurface scattering implementation.
=============================================================================*/

#include "PostProcess/PostProcessSubsurface.h"
#include "PostProcess/SceneRenderTargets.h"
#include "PostProcess/SubsurfaceCommon.h"
#include "PostProcess/SubsurfaceBurleyNormalized.h"
#include "PostProcess/SubsurfaceSeparable.h"
#include "Engine/SubsurfaceProfile.h"
#include "CanvasTypes.h"
#include "RenderTargetTemp.h"

namespace
{
	//only add ubber subsurface console variables here
	static TAutoConsoleVariable<int32> CVarSubsurfaceScatteringType(
		TEXT("r.SubsurfaceScattering.Type"),
		0,
		TEXT("Subsurface type:\n")
		TEXT(" 0. Separable SSS (default).\n")
		TEXT(" 1. Burley Normalized.")
		TEXT("Else. Separable SSS. Make sure we always have SSS"),
		ECVF_Scalability | ECVF_RenderThreadSafe
	);
}

bool IsSubsurfaceEnabled()
{
	static const auto CVarSubsurfaceScattering = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.SubsurfaceScattering"));
	static const auto CVarSSSScale = IConsoleManager::Get().FindTConsoleVariableDataFloat(TEXT("r.SSS.Scale"));
	check(CVarSubsurfaceScattering);
	check(CVarSSSScale);
	const bool bEnabled = CVarSubsurfaceScattering->GetValueOnAnyThread() != 0;
	const bool bHasScale = CVarSSSScale->GetValueOnAnyThread() > 0.0f;
	return (bEnabled && bHasScale);
}

bool IsSubsurfaceRequiredForView(const FViewInfo& View)
{
	const bool bSimpleDynamicLighting = IsAnyForwardShadingEnabled(View.GetShaderPlatform());
	const bool bSubsurfaceEnabled = IsSubsurfaceEnabled();
	const bool bViewHasSubsurfaceMaterials = ((View.ShadingModelMaskInView & GetUseSubsurfaceProfileShadingModelMask()) != 0);
	return (bSubsurfaceEnabled && bViewHasSubsurfaceMaterials && !bSimpleDynamicLighting);
}

uint32 GetSubsurfaceRequiredViewMask(const TArray<FViewInfo>& Views)
{
	const uint32 ViewCount = Views.Num();
	uint32 ViewMask = 0;

	// Traverse the views to make sure we only process subsurface if requested by any view.
	for (uint32 ViewIndex = 0; ViewIndex < ViewCount; ++ViewIndex)
	{
		const FViewInfo& View = Views[ViewIndex];

		if (IsSubsurfaceRequiredForView(View))
		{
			const uint32 ViewBit = 1 << ViewIndex;

			ViewMask |= ViewBit;
		}
	}

	return ViewMask;
}

bool IsSubsurfaceCheckerboardFormat(EPixelFormat SceneColorFormat)
{
	// No checkerboard for Burley
	if (CVarSubsurfaceScatteringType.GetValueOnRenderThread() == 1)
		return false;
	return IsSeparableSubsurfaceCheckerboardFormat(SceneColorFormat);
}

// Encapsulates the post processing subsurface scattering common pixel shader.
class FSubsurfaceVisualizePS : public FSubsurfaceShader
{
public:
	DECLARE_GLOBAL_SHADER(FSubsurfaceVisualizePS);
	SHADER_USE_PARAMETER_STRUCT(FSubsurfaceVisualizePS, FSubsurfaceShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FSubsurfaceParameters, Subsurface)
		SHADER_PARAMETER_STRUCT(FSubsurfaceInput, SubsurfaceInput0)
		SHADER_PARAMETER_TEXTURE(Texture2D, MiniFontTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, SubsurfaceSampler0)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()
};

IMPLEMENT_GLOBAL_SHADER(FSubsurfaceVisualizePS, "/Engine/Private/PostProcessSubsurface.usf", "VisualizePS", SF_Pixel);

// Encapsulates a simple copy pixel shader.
class FSubsurfaceViewportCopyPS : public FSubsurfaceShader
{
	DECLARE_GLOBAL_SHADER(FSubsurfaceViewportCopyPS);
	SHADER_USE_PARAMETER_STRUCT(FSubsurfaceViewportCopyPS, FSubsurfaceShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SubsurfaceInput0_Texture)
		SHADER_PARAMETER_SAMPLER(SamplerState, SubsurfaceSampler0)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT();

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

IMPLEMENT_GLOBAL_SHADER(FSubsurfaceViewportCopyPS, "/Engine/Private/PostProcessSubsurface.usf", "SubsurfaceViewportCopyPS", SF_Pixel);

void ComputeSubsurfaceForView(
	FRDGBuilder& GraphBuilder,
	const FScreenPassViewInfo& ScreenPassView,
	const FScreenPassTextureViewport& SceneViewport,
	FRDGTextureRef SceneTexture,
	FRDGTextureRef SceneTextureOutput,
	ERenderTargetLoadAction SceneTextureLoadAction)
{
	// Compute subsurface based on cmd configuration
	static const auto CVar = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.SubsurfaceScattering.Type"));
	check(CVar);

	if (CVar->GetValueOnRenderThread() == 1)	// Use Burley normalized SSS
	{
		ComputeBurleySubsurfaceForView(GraphBuilder, ScreenPassView, SceneViewport, SceneTexture, SceneTextureOutput, SceneTextureLoadAction);
	}
	else										// Use separable SSS.
	{
		ComputeSeparableSubsurfaceForView(GraphBuilder, ScreenPassView, SceneViewport, SceneTexture, SceneTextureOutput, SceneTextureLoadAction);
	}
}

FRDGTextureRef ComputeSubsurface(
	FRDGBuilder& GraphBuilder,
	FRDGTextureRef SceneTexture,
	const TArray<FViewInfo>& Views)
{
	const uint32 ViewCount = Views.Num();
	const uint32 ViewMaskAll = (1 << ViewCount) - 1;
	const uint32 ViewMask = GetSubsurfaceRequiredViewMask(Views);

	// Return the original target if no views have subsurface applied.
	if (!ViewMask)
	{
		return SceneTexture;
	}

	FRDGTextureDesc SceneColorDesc = SceneTexture->Desc;
	SceneColorDesc.TargetableFlags &= ~TexCreate_UAV;
	SceneColorDesc.TargetableFlags |= TexCreate_RenderTargetable;
	FRDGTextureRef SceneTextureOutput = GraphBuilder.CreateTexture(SceneColorDesc, TEXT("SceneColorSubsurface"));

	ERenderTargetLoadAction SceneTextureLoadAction = ERenderTargetLoadAction::ENoAction;

	const bool bHasNonSubsurfaceView = ViewMask != ViewMaskAll;

	/**
	 * Since we are outputting to a new texture and certain views may not utilize subsurface scattering,
	 * we need to copy all non-subsurface views onto the destination texture.
	 */
	if (bHasNonSubsurfaceView)
	{
		FSubsurfaceViewportCopyPS::FParameters* PassParameters = GraphBuilder.AllocParameters<FSubsurfaceViewportCopyPS::FParameters>();
		PassParameters->RenderTargets[0] = FRenderTargetBinding(SceneTextureOutput, ERenderTargetLoadAction::ENoAction);
		PassParameters->SubsurfaceInput0_Texture = SceneTexture;
		PassParameters->SubsurfaceSampler0 = TStaticSamplerState<SF_Point, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();

		TShaderMapRef<FSubsurfaceViewportCopyPS> PixelShader(Views[0].ShaderMap);

		const FIntPoint InputTextureSize = SceneTexture->Desc.Extent;

		GraphBuilder.AddPass(
			RDG_EVENT_NAME("SubsurfaceViewportCopy"),
			PassParameters,
			ERDGPassFlags::Raster,
			[&Views, ViewMask, ViewCount, PixelShader, InputTextureSize, PassParameters](FRHICommandListImmediate& RHICmdList)
		{
			for (uint32 ViewIndex = 0; ViewIndex < ViewCount; ++ViewIndex)
			{
				const uint32 ViewBit = 1 << ViewIndex;

				const bool bIsNonSubsurfaceView = (ViewMask & ViewBit) == 0;

				if (bIsNonSubsurfaceView)
				{
					const FViewInfo& View = Views[ViewIndex];
					const FScreenPassViewInfo ScreenPassView(View);
					const FScreenPassTextureViewport TextureViewport(View.ViewRect, InputTextureSize);

					DrawScreenPass(RHICmdList, ScreenPassView, TextureViewport, TextureViewport, *PixelShader, *PassParameters);
				}
			}
		});

		// Subsequent render passes should load the texture contents.
		SceneTextureLoadAction = ERenderTargetLoadAction::ELoad;
	}

	for (uint32 ViewIndex = 0; ViewIndex < ViewCount; ++ViewIndex)
	{
		const uint32 ViewBit = 1 << ViewIndex;

		const bool bIsSubsurfaceView = (ViewMask & ViewBit) != 0;

		if (bIsSubsurfaceView)
		{
			RDG_EVENT_SCOPE(GraphBuilder, "SubsurfaceScattering(ViewId=%d)", ViewIndex);

			const FViewInfo& View = Views[ViewIndex];
			const FScreenPassViewInfo ScreenPassView(View);
			const FScreenPassTextureViewport SceneViewport(View.ViewRect, SceneTexture);

			ComputeSubsurfaceForView(GraphBuilder, ScreenPassView, SceneViewport, SceneTexture, SceneTextureOutput, SceneTextureLoadAction);

			// Subsequent render passes should load the texture contents.
			SceneTextureLoadAction = ERenderTargetLoadAction::ELoad;
		}
	}

	return SceneTextureOutput;
}

void VisualizeSubsurface(
	FRDGBuilder& GraphBuilder,
	const FScreenPassViewInfo& ScreenPassView,
	const FScreenPassTextureViewport& SceneViewport,
	FRDGTextureRef SceneTexture,
	FRDGTextureRef SceneTextureOutput)
{
	check(SceneTexture);
	check(SceneTextureOutput);
	check(SceneViewport.Extent == SceneTexture->Desc.Extent);

	const FViewInfo& View = ScreenPassView.View;

	FSubsurfaceVisualizePS::FParameters* PassParameters = GraphBuilder.AllocParameters<FSubsurfaceVisualizePS::FParameters>();
	PassParameters->Subsurface = GetSubsurfaceCommonParameters(GraphBuilder.RHICmdList, View);
	PassParameters->RenderTargets[0] = FRenderTargetBinding(SceneTextureOutput, ERenderTargetLoadAction::EClear);
	PassParameters->SubsurfaceInput0.Texture = SceneTexture;
	PassParameters->SubsurfaceInput0.Viewport = GetScreenPassTextureViewportParameters(SceneViewport);
	PassParameters->SubsurfaceSampler0 = TStaticSamplerState<SF_Point, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
	PassParameters->MiniFontTexture = GetMiniFontTexture();

	TShaderMapRef<FSubsurfaceVisualizePS> PixelShader(View.ShaderMap);

	GraphBuilder.AddPass(
		RDG_EVENT_NAME("SubsurfaceVisualize"),
		PassParameters,
		ERDGPassFlags::Raster,
		[ScreenPassView, SceneViewport, SceneTextureOutput, PixelShader, PassParameters](FRHICommandListImmediate& RHICmdList)
	{
		DrawScreenPass(RHICmdList, ScreenPassView, SceneViewport, SceneViewport, *PixelShader, *PassParameters);

		// Draw debug text
		{
			const FViewInfo& LocalView = ScreenPassView.View;
			const FSceneViewFamily& ViewFamily = *LocalView.Family;
			FRenderTargetTemp TempRenderTarget(static_cast<FRHITexture2D*>(SceneTextureOutput->GetRHI()), SceneTextureOutput->Desc.Extent);
			FCanvas Canvas(&TempRenderTarget, nullptr, ViewFamily.CurrentRealTime, ViewFamily.CurrentWorldTime, ViewFamily.DeltaWorldTime, LocalView.GetFeatureLevel());

			float X = 30;
			float Y = 28;
			const float YStep = 14;

			FString Line = FString::Printf(TEXT("Visualize Screen Space Subsurface Scattering"));
			Canvas.DrawShadowedString(X, Y += YStep, *Line, GetStatsFont(), FLinearColor(1, 1, 1));

			Y += YStep;

			uint32 Index = 0;
			while (GSubsurfaceProfileTextureObject.GetEntryString(Index++, Line))
			{
				Canvas.DrawShadowedString(X, Y += YStep, *Line, GetStatsFont(), FLinearColor(1, 1, 1));
			}

			const bool bFlush = false;
			const bool bInsideRenderPass = true;
			Canvas.Flush_RenderThread(RHICmdList, bFlush, bInsideRenderPass);
		}
	});
}

//////////////////////////////////////////////////////////////////////////
//! Shim methods to hook into the legacy pipeline until the full RDG conversion is complete.

void ComputeSubsurfaceShim(FRHICommandListImmediate& RHICmdList, const TArray<FViewInfo>& Views)
{
	FSceneRenderTargets& SceneRenderTargets = FSceneRenderTargets::Get(RHICmdList);

	FRDGBuilder GraphBuilder(RHICmdList);

	FRDGTextureRef SceneTexture = GraphBuilder.RegisterExternalTexture(SceneRenderTargets.GetSceneColor(), TEXT("SceneColor"));

	FRDGTextureRef SceneTextureOutput = ComputeSubsurface(GraphBuilder, SceneTexture, Views);

	// Extract the result texture out and re-assign it to the scene render targets blackboard.
	TRefCountPtr<IPooledRenderTarget> SceneTarget;
	GraphBuilder.QueueTextureExtraction(SceneTextureOutput, &SceneTarget, false);
	GraphBuilder.Execute();

	SceneRenderTargets.SetSceneColor(SceneTarget);

	// The RT should be released as early as possible to allow sharing of that memory for other purposes.
	// This becomes even more important with some limited VRam (XBoxOne).
	SceneRenderTargets.SetLightAttenuation(nullptr);
}

FRenderingCompositeOutputRef VisualizeSubsurfaceShim(
	FRHICommandListImmediate& InRHICmdList,
	FRenderingCompositionGraph& Graph,
	FRenderingCompositeOutputRef Input)
{
	// we need the GBuffer, we release it Process()
	FSceneRenderTargets::Get(InRHICmdList).AdjustGBufferRefCount(InRHICmdList, 1);

	FRenderingCompositePass* SubsurfaceVisualizePass = Graph.RegisterPass(new(FMemStack::Get()) TRCPassForRDG<1, 1>(
		[](FRenderingCompositePass* Pass, FRenderingCompositePassContext& CompositePassContext)
	{
		FRDGBuilder GraphBuilder(CompositePassContext.RHICmdList);

		FRDGTextureRef SceneTexture = Pass->CreateRDGTextureForRequiredInput(GraphBuilder, ePId_Input0, TEXT("SceneColor"));
		FRDGTextureRef SceneTextureOutput = Pass->FindOrCreateRDGTextureForOutput(GraphBuilder, ePId_Output0, SceneTexture->Desc, TEXT("SubsurfaceVisualize"));

		const FScreenPassViewInfo ScreenPassView(CompositePassContext.View);
		const FScreenPassTextureViewport SceneViewport(CompositePassContext.View.ViewRect, SceneTexture->Desc.Extent);
		VisualizeSubsurface(GraphBuilder, ScreenPassView, SceneViewport, SceneTexture, SceneTextureOutput);

		Pass->ExtractRDGTextureForOutput(GraphBuilder, ePId_Output0, SceneTextureOutput);

		GraphBuilder.Execute();

		FRHICommandListImmediate& RHICmdList = GraphBuilder.RHICmdList;
		FSceneRenderTargets::Get(RHICmdList).AdjustGBufferRefCount(RHICmdList, -1);
	}));

	SubsurfaceVisualizePass->SetInput(ePId_Input0, Input);
	return FRenderingCompositeOutputRef(SubsurfaceVisualizePass);
}

//////////////////////////////////////////////////////////////////////////