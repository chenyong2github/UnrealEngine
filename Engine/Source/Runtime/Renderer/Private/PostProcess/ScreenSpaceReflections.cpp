// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	ScreenSpaceReflections.cpp: Post processing Screen Space Reflections implementation.
=============================================================================*/

#include "PostProcess/ScreenSpaceReflections.h"
#include "ScenePrivate.h"
#include "RenderGraph.h"
#include "PixelShaderUtils.h"
#include "SceneViewFamilyBlackboard.h"

static TAutoConsoleVariable<int32> CVarSSRQuality(
	TEXT("r.SSR.Quality"),
	3,
	TEXT("Whether to use screen space reflections and at what quality setting.\n")
	TEXT("(limits the setting in the post process settings which has a different scale)\n")
	TEXT("(costs performance, adds more visual realism but the technique has limits)\n")
	TEXT(" 0: off (default)\n")
	TEXT(" 1: low (no glossy)\n")
	TEXT(" 2: medium (no glossy)\n")
	TEXT(" 3: high (glossy/using roughness, few samples)\n")
	TEXT(" 4: very high (likely too slow for real-time)"),
	ECVF_Scalability | ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarSSRTemporal(
	TEXT("r.SSR.Temporal"),
	0,
	TEXT("Defines if we use the temporal smoothing for the screen space reflection\n")
	TEXT(" 0 is off (for debugging), 1 is on (default)"),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarSSRStencil(
	TEXT("r.SSR.Stencil"),
	0,
	TEXT("Defines if we use the stencil prepass for the screen space reflection\n")
	TEXT(" 0 is off (default), 1 is on"),
	ECVF_RenderThreadSafe);

DECLARE_GPU_STAT_NAMED(ScreenSpaceReflections, TEXT("ScreenSpace Reflections"));

bool ShouldRenderScreenSpaceReflections(const FViewInfo& View)
{
	if(!View.Family->EngineShowFlags.ScreenSpaceReflections)
	{
		return false;
	}

	if(!View.State)
	{
		// not view state (e.g. thumbnail rendering?), no HZB (no screen space reflections or occlusion culling)
		return false;
	}

	int SSRQuality = CVarSSRQuality.GetValueOnRenderThread();

	if(SSRQuality <= 0)
	{
		return false;
	}

	if(View.FinalPostProcessSettings.ScreenSpaceReflectionIntensity < 1.0f)
	{
		return false;
	}

	if (IsAnyForwardShadingEnabled(View.GetShaderPlatform()))
	{
		return false;
	}

	return true;
}

bool IsSSRTemporalPassRequired(const FViewInfo& View, bool bCheckSSREnabled)
{
	if (bCheckSSREnabled && !ShouldRenderScreenSpaceReflections(View))
	{
		return false;
	}
	if (!View.State)
	{
		return false;
	}
	return View.AntiAliasingMethod != AAM_TemporalAA || CVarSSRTemporal.GetValueOnRenderThread() != 0;
}

namespace
{

float ComputeRoughnessMaskScale(const FViewInfo& View, uint32 SSRQuality)
{
	float MaxRoughness = FMath::Clamp(View.FinalPostProcessSettings.ScreenSpaceReflectionMaxRoughness, 0.01f, 1.0f);

	// f(x) = x * Scale + Bias
	// f(MaxRoughness) = 0
	// f(MaxRoughness/2) = 1

	float RoughnessMaskScale = -2.0f / MaxRoughness;
	return RoughnessMaskScale * (SSRQuality < 3 ? 2.0f : 1.0f);
}

FLinearColor ComputeSSRParams(const FViewInfo& View, uint32 SSRQuality, bool bEnableDiscard)
{
	float RoughnessMaskScale = ComputeRoughnessMaskScale(View, SSRQuality);

	float FrameRandom = 0;

	if(View.ViewState)
	{
		bool bTemporalAAIsOn = View.AntiAliasingMethod == AAM_TemporalAA;

		if(bTemporalAAIsOn)
		{
			// usually this number is in the 0..7 range but it depends on the TemporalAA quality
			FrameRandom = View.ViewState->GetCurrentTemporalAASampleIndex() * 1551;
		}
		else
		{
			// 8 aligns with the temporal smoothing, larger number will do more flickering (power of two for best performance)
			FrameRandom = View.ViewState->GetFrameIndex(8) * 1551;
		}
	}

	return FLinearColor(
		FMath::Clamp(View.FinalPostProcessSettings.ScreenSpaceReflectionIntensity * 0.01f, 0.0f, 1.0f), 
		RoughnessMaskScale,
		(float)bEnableDiscard,	// TODO 
		FrameRandom);
}


BEGIN_SHADER_PARAMETER_STRUCT(FSSRCommonParameters, )
	SHADER_PARAMETER(FLinearColor, SSRParams)
	SHADER_PARAMETER_STRUCT_INCLUDE(FSceneViewFamilyBlackboard, SceneTextures)
	SHADER_PARAMETER_STRUCT_INCLUDE(FSceneTextureSamplerParameters, SceneTextureSamplers)
	SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, ViewUniformBuffer)
END_SHADER_PARAMETER_STRUCT()


class FScreenSpaceReflectionsStencilPS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FScreenSpaceReflectionsStencilPS);
	SHADER_USE_PARAMETER_STRUCT(FScreenSpaceReflectionsStencilPS, FGlobalShader);

	using FPermutationDomain = FShaderPermutationNone;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM4);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine( TEXT("PREV_FRAME_COLOR"), uint32(0) );
		OutEnvironment.SetDefine( TEXT("SSR_QUALITY"), uint32(0) );
	}
	
	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FSSRCommonParameters, CommonParameters)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()
};


static constexpr int32 QualityCount = 5;

class FSSRQualityDim : SHADER_PERMUTATION_INT("SSR_QUALITY", QualityCount);
class FSSRPrevFrameColorDim : SHADER_PERMUTATION_BOOL("PREV_FRAME_COLOR");
class FSSRPrevFrameColorDim2 : SHADER_PERMUTATION_BOOL("PREV_FRAME_COLOR");


class FScreenSpaceReflectionsPS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FScreenSpaceReflectionsPS);
	SHADER_USE_PARAMETER_STRUCT(FScreenSpaceReflectionsPS, FGlobalShader);

	using FPermutationDomain = TShaderPermutationDomain<FSSRQualityDim, FSSRPrevFrameColorDim>;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		FPermutationDomain PermutationVector(Parameters.PermutationId);
		if ((PermutationVector.Get<FSSRQualityDim>() == 0) && PermutationVector.Get<FSSRPrevFrameColorDim>())
		{
			return false;
		}
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM4);
	}
	
	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FSSRCommonParameters, CommonParameters)

		SHADER_PARAMETER(FVector4, HZBUvFactorAndInvFactor)
		SHADER_PARAMETER(FVector4, PrevScreenPositionScaleBias)
		SHADER_PARAMETER(float, PrevSceneColorPreExposureCorrection)
		
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SceneColor)
		SHADER_PARAMETER_SAMPLER(SamplerState, SceneColorSampler)
		
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, HZB)
		SHADER_PARAMETER_SAMPLER(SamplerState, HZBSampler)
		
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()
};

IMPLEMENT_GLOBAL_SHADER(FScreenSpaceReflectionsPS, "/Engine/Private/ScreenSpaceReflections.usf", "ScreenSpaceReflectionsPS", SF_Pixel);
IMPLEMENT_GLOBAL_SHADER(FScreenSpaceReflectionsStencilPS, "/Engine/Private/ScreenSpaceReflections.usf", "ScreenSpaceReflectionsStencilPS", SF_Pixel);

// @param Quality usually in 0..100 range, default is 50
// @return see CVarSSRQuality, never 0
static int32 ComputeSSRQuality(float Quality)
{
	int32 Ret;

	if(Quality >= 60.0f)
	{
		Ret = (Quality >= 80.0f) ? 4 : 3;
	}
	else
	{
		Ret = (Quality >= 40.0f) ? 2 : 1;
	}

	int SSRQualityCVar = FMath::Clamp(CVarSSRQuality.GetValueOnRenderThread(), 0, QualityCount - 1);

	return FMath::Min(Ret, SSRQualityCVar);
}

} // namespace

FRDGTextureRef RenderScreenSpaceReflections(
	FRDGBuilder& GraphBuilder,
	const FSceneViewFamilyBlackboard& SceneTextures,
	const FRDGTextureRef CurrentSceneColor,
	const FViewInfo& View)
{
	RDG_EVENT_SCOPE(GraphBuilder, "ScreenSpaceReflections");
	
	FRDGTextureRef InputColor = CurrentSceneColor;
	bool bSamplePrevFrame = false;
	if (View.PrevViewInfo.CustomSSRInput.IsValid())
	{
		InputColor = GraphBuilder.RegisterExternalTexture(View.PrevViewInfo.CustomSSRInput);
		bSamplePrevFrame = true;
	}
	else if (View.PrevViewInfo.TemporalAAHistory.IsValid())
	{
		InputColor = GraphBuilder.RegisterExternalTexture(View.PrevViewInfo.TemporalAAHistory.RT[0]);
		bSamplePrevFrame = true;
	}

	const bool VisualizeSSR = View.Family->EngineShowFlags.VisualizeSSR;
	const bool SSRStencilPrePass = CVarSSRStencil.GetValueOnRenderThread() != 0 && !VisualizeSSR;
	const int32 SSRQuality = VisualizeSSR ? 0 : FMath::Clamp(ComputeSSRQuality(View.FinalPostProcessSettings.ScreenSpaceReflectionQuality), 1, 4);
	
	// Alloc SSR output.
	FRDGTextureRef SSROutput;
	{
		FRDGTextureDesc Desc = FPooledRenderTargetDesc::Create2DDesc(
			FSceneRenderTargets::Get_FrameConstantsOnly().GetBufferSizeXY(),
			PF_FloatRGBA, FClearValueBinding(FLinearColor(0, 0, 0, 0)),
			TexCreate_None, TexCreate_RenderTargetable,
			false);

		Desc.AutoWritable = false;
		Desc.Flags |= GFastVRamConfig.SSR;

		SSROutput = GraphBuilder.CreateTexture(Desc, TEXT("ScreenSpaceReflections"));
	}
		
	FSSRCommonParameters CommonParameters;
	CommonParameters.SSRParams = ComputeSSRParams(View, SSRQuality, false);
	CommonParameters.ViewUniformBuffer = View.ViewUniformBuffer;
	CommonParameters.SceneTextures = SceneTextures;
	SetupSceneTextureSamplers(&CommonParameters.SceneTextureSamplers);
	
	FRenderTargetBindingSlots RenderTargets;
	RenderTargets[0] = FRenderTargetBinding(SSROutput, ERenderTargetLoadAction::ENoAction, ERenderTargetStoreAction::EStore);

	// Do a pre pass that output 0, or set a stencil mask to run the more expensive pixel shader.
	if (SSRStencilPrePass)
	{
		// Also bind the depth buffer
		RenderTargets.DepthStencil = FDepthStencilBinding(
			SceneTextures.SceneDepthBuffer,
			ERenderTargetLoadAction::ENoAction, ERenderTargetStoreAction::ENoAction,
			ERenderTargetLoadAction::ELoad, ERenderTargetStoreAction::EStore,
			FExclusiveDepthStencil::DepthRead_StencilWrite);

		FScreenSpaceReflectionsStencilPS::FParameters* PassParameters = GraphBuilder.AllocParameters<FScreenSpaceReflectionsStencilPS::FParameters>();
		PassParameters->CommonParameters = CommonParameters;
		PassParameters->RenderTargets = RenderTargets;
		
		TShaderMapRef<FScreenSpaceReflectionsStencilPS> PixelShader(View.ShaderMap);
		ClearUnusedGraphResources(*PixelShader, PassParameters);
		
		GraphBuilder.AddPass(
			RDG_EVENT_NAME("SSR StencilSetup %dx%d", View.ViewRect.Width(), View.ViewRect.Height()),
			PassParameters,
			ERenderGraphPassFlags::None,
			[PassParameters, &View, PixelShader](FRHICommandList& RHICmdList)
		{
			SCOPED_GPU_STAT(RHICmdList, ScreenSpaceReflections);
			RHICmdList.SetViewport(View.ViewRect.Min.X, View.ViewRect.Min.Y, 0.0f, View.ViewRect.Max.X, View.ViewRect.Max.Y, 1.0f);
			RHICmdList.SetStencilRef(0x80);
		
			FGraphicsPipelineStateInitializer GraphicsPSOInit;
			FPixelShaderUtils::InitFullscreenPipelineState(RHICmdList, View.ShaderMap, *PixelShader, /* out */ GraphicsPSOInit);
			// Clobers the stencil to pixel that should not compute SSR
			GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always, true, CF_Always, SO_Replace, SO_Replace, SO_Replace>::GetRHI();

			SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);
			SetShaderParameters(RHICmdList, *PixelShader, PixelShader->GetPixelShader(), *PassParameters);

			FPixelShaderUtils::DrawFullscreenTriangle(RHICmdList);
		});
	}

	// Adds SSR pass.
	{
		FScreenSpaceReflectionsPS::FPermutationDomain PermutationVector;
		PermutationVector.Set<FSSRPrevFrameColorDim>(bSamplePrevFrame && SSRQuality != 0);
		PermutationVector.Set<FSSRQualityDim>(SSRQuality);

		FScreenSpaceReflectionsPS::FParameters* PassParameters = GraphBuilder.AllocParameters<FScreenSpaceReflectionsPS::FParameters>();
		PassParameters->CommonParameters = CommonParameters;
		{
			const FVector2D HZBUvFactor(
				float(View.ViewRect.Width()) / float(2 * View.HZBMipmap0Size.X),
				float(View.ViewRect.Height()) / float(2 * View.HZBMipmap0Size.Y));
			PassParameters->HZBUvFactorAndInvFactor = FVector4(
				HZBUvFactor.X,
				HZBUvFactor.Y,
				1.0f / HZBUvFactor.X,
				1.0f / HZBUvFactor.Y);
		}
		{
			FIntPoint ViewportOffset = View.ViewRect.Min;
			FIntPoint ViewportExtent = View.ViewRect.Size();
			FIntPoint BufferSize = SceneTextures.SceneDepthBuffer->Desc.Extent;

			if (View.PrevViewInfo.TemporalAAHistory.IsValid())
			{
				ViewportOffset = View.PrevViewInfo.TemporalAAHistory.ViewportRect.Min;
				ViewportExtent = View.PrevViewInfo.TemporalAAHistory.ViewportRect.Size();
				BufferSize = View.PrevViewInfo.TemporalAAHistory.ReferenceBufferSize;
			}

			FVector2D InvBufferSize(1.0f / float(BufferSize.X), 1.0f / float(BufferSize.Y));

			PassParameters->PrevScreenPositionScaleBias = FVector4(
				ViewportExtent.X * 0.5f * InvBufferSize.X,
				-ViewportExtent.Y * 0.5f * InvBufferSize.Y,
				(ViewportExtent.X * 0.5f + ViewportOffset.X) * InvBufferSize.X,
				(ViewportExtent.Y * 0.5f + ViewportOffset.Y) * InvBufferSize.Y);
		}
		PassParameters->PrevSceneColorPreExposureCorrection = bSamplePrevFrame ? View.PreExposure / View.PrevViewInfo.SceneColorPreExposure : 1.0f;
		
		PassParameters->SceneColor = InputColor;
		PassParameters->SceneColorSampler = TStaticSamplerState<SF_Point>::GetRHI();
		
		PassParameters->HZB = GraphBuilder.RegisterExternalTexture(View.HZB);
		PassParameters->HZBSampler = TStaticSamplerState<SF_Point>::GetRHI();
		
		PassParameters->RenderTargets = RenderTargets;

		TShaderMapRef<FScreenSpaceReflectionsPS> PixelShader(View.ShaderMap, PermutationVector);
		ClearUnusedGraphResources(*PixelShader, PassParameters);
		
		GraphBuilder.AddPass(
			RDG_EVENT_NAME("SSR RayMarch(Quality=%d) %dx%d",
				SSRQuality, View.ViewRect.Width(), View.ViewRect.Height()),
			PassParameters,
			ERenderGraphPassFlags::None,
			[PassParameters, &View, PixelShader, SSRStencilPrePass](FRHICommandList& RHICmdList)
		{
			SCOPED_GPU_STAT(RHICmdList, ScreenSpaceReflections);
			RHICmdList.SetViewport(View.ViewRect.Min.X, View.ViewRect.Min.Y, 0.0f, View.ViewRect.Max.X, View.ViewRect.Max.Y, 1.0f);
			RHICmdList.SetStencilRef(0x80);
		
			FGraphicsPipelineStateInitializer GraphicsPSOInit;
			FPixelShaderUtils::InitFullscreenPipelineState(RHICmdList, View.ShaderMap, *PixelShader, /* out */ GraphicsPSOInit);
			if (SSRStencilPrePass)
			{
				// Clobers the stencil to pixel that should not compute SSR
				GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always, true, CF_Equal, SO_Keep, SO_Keep, SO_Keep>::GetRHI();
			}

			SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);
			SetShaderParameters(RHICmdList, *PixelShader, PixelShader->GetPixelShader(), *PassParameters);

			FPixelShaderUtils::DrawFullscreenTriangle(RHICmdList);
		});
	}

	return SSROutput;
}
