// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "ScreenSpaceRayTracing.h"
#include "RenderGraph.h"
#include "PixelShaderUtils.h"
#include "ScreenPass.h"
#include "ScenePrivate.h"
#include "SceneTextureParameters.h"


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

static TAutoConsoleVariable<int32> CVarSSGIQuality(
	TEXT("r.SSGI.Quality"),
	0,
	TEXT("Whether to use screen space diffuse indirect and at what quality setting.\n"),
	ECVF_Scalability | ECVF_RenderThreadSafe);


DECLARE_GPU_STAT_NAMED(ScreenSpaceReflections, TEXT("ScreenSpace Reflections"));
DECLARE_GPU_STAT_NAMED(ScreenSpaceDiffuseIndirect, TEXT("Screen Space Diffuse Indirect"));


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

bool ShouldRenderScreenSpaceDiffuseIndirect(const FViewInfo& View)
{
	int Quality = CVarSSGIQuality.GetValueOnRenderThread();

	if (Quality <= 0)
	{
		return false;
	}

	if (IsAnyForwardShadingEnabled(View.GetShaderPlatform()))
	{
		return false;
	}

	return View.PrevViewInfo.TemporalAAHistory.IsValid();
}

bool IsSSRTemporalPassRequired(const FViewInfo& View)
{
	check(ShouldRenderScreenSpaceReflections(View));

	if (!View.State)
	{
		return false;
	}
	return View.AntiAliasingMethod != AAM_TemporalAA || CVarSSRTemporal.GetValueOnRenderThread() != 0;
}

namespace
{

float ComputeRoughnessMaskScale(const FViewInfo& View, ESSRQuality SSRQuality)
{
	float MaxRoughness = FMath::Clamp(View.FinalPostProcessSettings.ScreenSpaceReflectionMaxRoughness, 0.01f, 1.0f);

	// f(x) = x * Scale + Bias
	// f(MaxRoughness) = 0
	// f(MaxRoughness/2) = 1

	float RoughnessMaskScale = -2.0f / MaxRoughness;
	return RoughnessMaskScale * (int32(SSRQuality) < 3 ? 2.0f : 1.0f);
}

FLinearColor ComputeSSRParams(const FViewInfo& View, ESSRQuality SSRQuality, bool bEnableDiscard)
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
	SHADER_PARAMETER_STRUCT_INCLUDE(FSceneTextureParameters, SceneTextures)
	SHADER_PARAMETER_STRUCT_INCLUDE(FSceneTextureSamplerParameters, SceneTextureSamplers)
	SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, ViewUniformBuffer)
END_SHADER_PARAMETER_STRUCT()



class FSSRQualityDim : SHADER_PERMUTATION_ENUM_CLASS("SSR_QUALITY", ESSRQuality);
class FSSROutputForDenoiser : SHADER_PERMUTATION_BOOL("SSR_OUTPUT_FOR_DENOISER");


class FScreenSpaceReflectionsStencilPS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FScreenSpaceReflectionsStencilPS);
	SHADER_USE_PARAMETER_STRUCT(FScreenSpaceReflectionsStencilPS, FGlobalShader);

	using FPermutationDomain = TShaderPermutationDomain<FSSROutputForDenoiser>;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM4);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine( TEXT("SSR_QUALITY"), uint32(0) );
	}
	
	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FSSRCommonParameters, CommonParameters)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()
};

class FScreenSpaceReflectionsPS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FScreenSpaceReflectionsPS);
	SHADER_USE_PARAMETER_STRUCT(FScreenSpaceReflectionsPS, FGlobalShader);

	using FPermutationDomain = TShaderPermutationDomain<FSSRQualityDim, FSSROutputForDenoiser>;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		FPermutationDomain PermutationVector(Parameters.PermutationId);
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

class FScreenSpaceDiffuseIndirectCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FScreenSpaceDiffuseIndirectCS);
	SHADER_USE_PARAMETER_STRUCT(FScreenSpaceDiffuseIndirectCS, FGlobalShader)

	class FQualityDim : SHADER_PERMUTATION_INT( "QUALITY", 5 );
	using FPermutationDomain = TShaderPermutationDomain< FQualityDim >;
	
	BEGIN_SHADER_PARAMETER_STRUCT( FParameters, )
		SHADER_PARAMETER( FVector4,		HZBUvFactorAndInvFactor )
		SHADER_PARAMETER( FVector4,		PrevScreenPositionScaleBias )
		SHADER_PARAMETER( float,		PrevSceneColorPreExposureCorrection )
		
		SHADER_PARAMETER_RDG_TEXTURE( Texture2D,	HZBTexture )
		SHADER_PARAMETER_SAMPLER( SamplerState,		HZBSampler )

		SHADER_PARAMETER_RDG_TEXTURE( Texture2D,	VelocityTexture )
		SHADER_PARAMETER_SAMPLER( SamplerState,		VelocitySampler )

		SHADER_PARAMETER_RDG_TEXTURE( Texture2D,	ColorTexture )
		SHADER_PARAMETER_SAMPLER( SamplerState,		ColorSampler )
		
		SHADER_PARAMETER_STRUCT_INCLUDE(FSceneTextureParameters, SceneTextures)
		SHADER_PARAMETER_STRUCT_INCLUDE(FSceneTextureSamplerParameters, SceneTextureSamplers)
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)

		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture<float4>, IndirectDiffuseOutput)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture<float>,  AmbientOcclusionOutput)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};


IMPLEMENT_GLOBAL_SHADER(FScreenSpaceReflectionsPS,        "/Engine/Private/SSRT/SSRTReflections.usf", "ScreenSpaceReflectionsPS", SF_Pixel);
IMPLEMENT_GLOBAL_SHADER(FScreenSpaceReflectionsStencilPS, "/Engine/Private/SSRT/SSRTReflections.usf", "ScreenSpaceReflectionsStencilPS", SF_Pixel);
IMPLEMENT_GLOBAL_SHADER(FScreenSpaceDiffuseIndirectCS,    "/Engine/Private/SSRT/SSRTDiffuseIndirect.usf", "MainCS", SF_Compute);


void GetSSRShaderOptionsForQuality(ESSRQuality Quality, IScreenSpaceDenoiser::FReflectionsRayTracingConfig* OutRayTracingConfigs)
{
	if (Quality == ESSRQuality::VisualizeSSR)
	{
		OutRayTracingConfigs->RayCountPerPixel = 12;
	}
	else if (Quality == ESSRQuality::Epic)
	{
		OutRayTracingConfigs->RayCountPerPixel = 12;
	}
	else if (Quality == ESSRQuality::High)
	{
		OutRayTracingConfigs->RayCountPerPixel = 4;
	}
	else if (Quality == ESSRQuality::Medium)
	{
		OutRayTracingConfigs->RayCountPerPixel = 1;
	}
	else if (Quality == ESSRQuality::Low)
	{
		OutRayTracingConfigs->RayCountPerPixel = 1;
	}
	else
	{
		check(0);
	}
}

} // namespace

void GetSSRQualityForView(const FViewInfo& View, ESSRQuality* OutQuality, IScreenSpaceDenoiser::FReflectionsRayTracingConfig* OutRayTracingConfigs)
{
	check(ShouldRenderScreenSpaceReflections(View));
	
	int32 SSRQualityCVar = FMath::Clamp(CVarSSRQuality.GetValueOnRenderThread(), 0, int32(ESSRQuality::MAX) - 1);
	
	if (View.Family->EngineShowFlags.VisualizeSSR)
	{
		*OutQuality = ESSRQuality::VisualizeSSR;
		return;
	}
	else if (View.FinalPostProcessSettings.ScreenSpaceReflectionQuality >= 80.0f && SSRQualityCVar >= 4)
	{
		*OutQuality = ESSRQuality::Epic;
	}
	else if (View.FinalPostProcessSettings.ScreenSpaceReflectionQuality >= 60.0f && SSRQualityCVar >= 3)
	{
		*OutQuality = ESSRQuality::High;
	}
	else if (View.FinalPostProcessSettings.ScreenSpaceReflectionQuality >= 40.0f && SSRQualityCVar >= 2)
	{
		*OutQuality = ESSRQuality::Medium;
	}
	else
	{
		*OutQuality = ESSRQuality::Low;
	}

	GetSSRShaderOptionsForQuality(*OutQuality, OutRayTracingConfigs);
}

void RenderScreenSpaceReflections(
	FRDGBuilder& GraphBuilder,
	const FSceneTextureParameters& SceneTextures,
	const FRDGTextureRef CurrentSceneColor,
	const FViewInfo& View,
	ESSRQuality SSRQuality,
	bool bDenoiser,
	IScreenSpaceDenoiser::FReflectionsInputs* DenoiserInputs)
{
	FRDGTextureRef InputColor = CurrentSceneColor;
	if (SSRQuality != ESSRQuality::VisualizeSSR)
	{
		if (View.PrevViewInfo.CustomSSRInput.IsValid())
		{
			InputColor = GraphBuilder.RegisterExternalTexture(View.PrevViewInfo.CustomSSRInput);
		}
		else if (View.PrevViewInfo.TemporalAAHistory.IsValid())
		{
			InputColor = GraphBuilder.RegisterExternalTexture(View.PrevViewInfo.TemporalAAHistory.RT[0]);
		}
	}

	const bool SSRStencilPrePass = CVarSSRStencil.GetValueOnRenderThread() != 0 && SSRQuality != ESSRQuality::VisualizeSSR;
	
	// Alloc inputs for denoising.
	{
		FRDGTextureDesc Desc = FPooledRenderTargetDesc::Create2DDesc(
			FSceneRenderTargets::Get_FrameConstantsOnly().GetBufferSizeXY(),
			PF_FloatRGBA, FClearValueBinding(FLinearColor(0, 0, 0, 0)),
			TexCreate_None, TexCreate_RenderTargetable | TexCreate_ShaderResource,
			false);

		Desc.AutoWritable = false;
		Desc.Flags |= GFastVRamConfig.SSR;

		DenoiserInputs->Color = GraphBuilder.CreateTexture(Desc, TEXT("ScreenSpaceReflections"));

		if (bDenoiser)
		{
			Desc.Format = PF_R16F;
			DenoiserInputs->RayHitDistance = GraphBuilder.CreateTexture(Desc, TEXT("ScreenSpaceReflectionsHitDistance"));
		}
	}

	IScreenSpaceDenoiser::FReflectionsRayTracingConfig RayTracingConfigs;
	GetSSRShaderOptionsForQuality(SSRQuality, &RayTracingConfigs);
		
	FSSRCommonParameters CommonParameters;
	CommonParameters.SSRParams = ComputeSSRParams(View, SSRQuality, false);
	CommonParameters.ViewUniformBuffer = View.ViewUniformBuffer;
	CommonParameters.SceneTextures = SceneTextures;
	SetupSceneTextureSamplers(&CommonParameters.SceneTextureSamplers);
	
	FRenderTargetBindingSlots RenderTargets;
	RenderTargets[0] = FRenderTargetBinding(DenoiserInputs->Color, ERenderTargetLoadAction::ENoAction, ERenderTargetStoreAction::EStore);

	if (bDenoiser)
	{
		RenderTargets[1] = FRenderTargetBinding(DenoiserInputs->RayHitDistance, ERenderTargetLoadAction::ENoAction, ERenderTargetStoreAction::EStore);
	}

	// Do a pre pass that output 0, or set a stencil mask to run the more expensive pixel shader.
	if (SSRStencilPrePass)
	{
		// Also bind the depth buffer
		RenderTargets.DepthStencil = FDepthStencilBinding(
			SceneTextures.SceneDepthBuffer,
			ERenderTargetLoadAction::ENoAction, ERenderTargetStoreAction::ENoAction,
			ERenderTargetLoadAction::ELoad, ERenderTargetStoreAction::EStore,
			FExclusiveDepthStencil::DepthRead_StencilWrite);

		FScreenSpaceReflectionsStencilPS::FPermutationDomain PermutationVector;
		PermutationVector.Set<FSSROutputForDenoiser>(bDenoiser);

		FScreenSpaceReflectionsStencilPS::FParameters* PassParameters = GraphBuilder.AllocParameters<FScreenSpaceReflectionsStencilPS::FParameters>();
		PassParameters->CommonParameters = CommonParameters;
		PassParameters->RenderTargets = RenderTargets;
		
		TShaderMapRef<FScreenSpaceReflectionsStencilPS> PixelShader(View.ShaderMap, PermutationVector);
		ClearUnusedGraphResources(*PixelShader, PassParameters);
		
		GraphBuilder.AddPass(
			RDG_EVENT_NAME("SSR StencilSetup %dx%d", View.ViewRect.Width(), View.ViewRect.Height()),
			PassParameters,
			ERDGPassFlags::Raster,
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
		PermutationVector.Set<FSSRQualityDim>(SSRQuality);
		PermutationVector.Set<FSSROutputForDenoiser>(bDenoiser);

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
		PassParameters->PrevSceneColorPreExposureCorrection = InputColor != CurrentSceneColor ? View.PreExposure / View.PrevViewInfo.SceneColorPreExposure : 1.0f;
		
		// Pipe down a mid grey texture when not using TAA's history to avoid wrongly reprojecting current scene color as if previous frame's TAA history.
		if (InputColor == CurrentSceneColor)
		{
			// Technically should be 32767.0f / 65535.0f to perfectly null out DecodeVelocityFromTexture(), but 0.5f is good enough.
			PassParameters->CommonParameters.SceneTextures.SceneVelocityBuffer = GraphBuilder.RegisterExternalTexture(GSystemTextures.MidGreyDummy);
		}

		PassParameters->SceneColor = InputColor;
		PassParameters->SceneColorSampler = TStaticSamplerState<SF_Point>::GetRHI();
		
		PassParameters->HZB = GraphBuilder.RegisterExternalTexture(View.HZB);
		PassParameters->HZBSampler = TStaticSamplerState<SF_Point>::GetRHI();
		
		PassParameters->RenderTargets = RenderTargets;

		TShaderMapRef<FScreenSpaceReflectionsPS> PixelShader(View.ShaderMap, PermutationVector);
		ClearUnusedGraphResources(*PixelShader, PassParameters);
		
		GraphBuilder.AddPass(
			RDG_EVENT_NAME("SSR RayMarch(Quality=%d RayPerPixel=%d%s) %dx%d",
				SSRQuality, RayTracingConfigs.RayCountPerPixel, bDenoiser ? TEXT(" DenoiserOutput") : TEXT(""),
				View.ViewRect.Width(), View.ViewRect.Height()),
			PassParameters,
			ERDGPassFlags::Raster,
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
} // RenderScreenSpaceReflections()

void RenderScreenSpaceDiffuseIndirect(
	FRDGBuilder& GraphBuilder, 
	const FSceneTextureParameters& SceneTextures,
	const FRDGTextureRef CurrentSceneColor,
	const FViewInfo& View,
	IScreenSpaceDenoiser::FDiffuseIndirectInputs* OutDenoiserInputs)
{
	check(ShouldRenderScreenSpaceDiffuseIndirect(View));

	const FTemporalAAHistory& TemporalAAHistory = View.PrevViewInfo.TemporalAAHistory;
	check(TemporalAAHistory.IsValid()); // TODO.
	
	const int32 Quality = FMath::Clamp( CVarSSGIQuality.GetValueOnRenderThread(), 1, 4 );

	FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(GraphBuilder.RHICmdList);

	// Allocate outputs.
	{
		FRDGTextureDesc Desc = FRDGTextureDesc::Create2DDesc(
			SceneTextures.SceneDepthBuffer->Desc.Extent,
			PF_FloatRGBA,
			FClearValueBinding::None,
			/* InFlags = */ TexCreate_None,
			/* InTargetableFlags = */ TexCreate_ShaderResource | TexCreate_RenderTargetable | TexCreate_UAV,
			/* bInForceSeparateTargetAndShaderResource = */ false);

		OutDenoiserInputs->Color = GraphBuilder.CreateTexture(Desc, TEXT("SSRTDiffuseIndirect"));

		Desc.Format = PF_R16F;
		OutDenoiserInputs->AmbientOcclusionMask = GraphBuilder.CreateTexture(Desc, TEXT("SSRTAmbientOcclusion"));
	}

	FRDGTexture* HZBTexture	= GraphBuilder.RegisterExternalTexture( View.HZB );
	FRDGTexture* ColorTexture	= GraphBuilder.RegisterExternalTexture( TemporalAAHistory.RT[0] );

	FScreenSpaceDiffuseIndirectCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FScreenSpaceDiffuseIndirectCS::FParameters>();

	PassParameters->HZBTexture = HZBTexture;
	PassParameters->HZBSampler = TStaticSamplerState< SF_Point, AM_Clamp, AM_Clamp, AM_Clamp >::GetRHI();

	PassParameters->VelocityTexture = SceneTextures.SceneVelocityBuffer;
	PassParameters->VelocitySampler = TStaticSamplerState< SF_Point, AM_Clamp, AM_Clamp, AM_Clamp >::GetRHI();

	PassParameters->ColorTexture = ColorTexture;
	PassParameters->ColorSampler = TStaticSamplerState< SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp >::GetRHI();

	const FVector2D HZBUvFactor(
		float( View.ViewRect.Width() )  / float( 2 * View.HZBMipmap0Size.X ),
		float( View.ViewRect.Height() ) / float( 2 * View.HZBMipmap0Size.Y )
		);
			
	PassParameters->HZBUvFactorAndInvFactor = FVector4(
		HZBUvFactor.X,
		HZBUvFactor.Y,
		1.0f / HZBUvFactor.X,
		1.0f / HZBUvFactor.Y );

	FIntPoint ViewportOffset	= TemporalAAHistory.ViewportRect.Min;
	FIntPoint ViewportExtent	= TemporalAAHistory.ViewportRect.Size();
	FIntPoint BufferSize		= TemporalAAHistory.ReferenceBufferSize;

	PassParameters->PrevScreenPositionScaleBias = FVector4(
		 ViewportExtent.X * 0.5f / BufferSize.X,
		-ViewportExtent.Y * 0.5f / BufferSize.Y,
		(ViewportExtent.X * 0.5f + ViewportOffset.X) / BufferSize.X,
		(ViewportExtent.Y * 0.5f + ViewportOffset.Y) / BufferSize.Y );

	PassParameters->PrevSceneColorPreExposureCorrection = View.PreExposure / View.PrevViewInfo.SceneColorPreExposure;

	PassParameters->SceneTextures = SceneTextures;
	SetupSceneTextureSamplers(&PassParameters->SceneTextureSamplers);
	PassParameters->View = View.ViewUniformBuffer;
	
	PassParameters->IndirectDiffuseOutput = GraphBuilder.CreateUAV(OutDenoiserInputs->Color);
	PassParameters->AmbientOcclusionOutput = GraphBuilder.CreateUAV(OutDenoiserInputs->AmbientOcclusionMask);

	FScreenSpaceDiffuseIndirectCS::FPermutationDomain PermutationVector;
	PermutationVector.Set<FScreenSpaceDiffuseIndirectCS::FQualityDim>(Quality);

	TShaderMapRef<FScreenSpaceDiffuseIndirectCS> ComputeShader(View.ShaderMap, PermutationVector);
	FComputeShaderUtils::AddPass(
		GraphBuilder,
		RDG_EVENT_NAME("ScreenSpaceDiffuseIndirect(Quality=%d) %dx%d", Quality, View.ViewRect.Width(), View.ViewRect.Height()),
		*ComputeShader,
		PassParameters,
		FComputeShaderUtils::GetGroupCount(View.ViewRect.Size(), 8));
} // RenderScreenSpaceDiffuseIndirect()
