/*=============================================================================
	PostProcessScreenSpaceReflection.cpp
=============================================================================*/

#include "PostProcess/PostProcessScreenSpaceReflection.h"
#include "RenderGraph.h"
#include "PixelShaderUtils.h"
#include "ScreenPass.h"
#include "ScenePrivate.h"
#include "SceneTextureParameters.h"
#include "../ScreenSpaceRayTracing.h"

class FMobileScreenSpaceReflectionPassPS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FMobileScreenSpaceReflectionPassPS);
	SHADER_USE_PARAMETER_STRUCT(FMobileScreenSpaceReflectionPassPS, FGlobalShader);

	class FMobileSSRQualityDim : SHADER_PERMUTATION_ENUM_CLASS("SSR_QUALITY", ESSRQuality);
	using FPermutationDomain = TShaderPermutationDomain<FMobileSSRQualityDim>;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return AllowScreenSpaceReflection(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
	}

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, ViewUniformBuffer)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SceneColor)
		SHADER_PARAMETER_SAMPLER(SamplerState, SceneColorSampler)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, WorldNormalRoughnessTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, WorldNormalRoughnessSampler)
		SHADER_PARAMETER_TEXTURE(Texture2D, PreIntegratedGF)
		SHADER_PARAMETER_SAMPLER(SamplerState, PreIntegratedGFSampler)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, HZB)
		SHADER_PARAMETER_SAMPLER(SamplerState, HZBSampler)
		SHADER_PARAMETER(FVector4, HZBUvFactorAndInvFactor)
		SHADER_PARAMETER(FVector4, PrevScreenPositionScaleBias)
		SHADER_PARAMETER(FLinearColor, SSRParams)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FMobileScreenSpaceReflectionPassPS, "/Engine/Private/SSRT/SSRTReflections.usf", "MobileScreenSpaceReflectionPS", SF_Pixel);

class FMobileScreenSpaceReflectionCompositePassPS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FMobileScreenSpaceReflectionCompositePassPS);
	SHADER_USE_PARAMETER_STRUCT(FMobileScreenSpaceReflectionCompositePassPS, FGlobalShader);

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return AllowScreenSpaceReflection(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
	}

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, ViewUniformBuffer)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, ScreenSpaceReflectionTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, ScreenSpaceReflectionSampler)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FMobileScreenSpaceReflectionCompositePassPS, "/Engine/Private/SSRT/SSRTReflections.usf", "MobileScreenSpaceReflectionCompositePS", SF_Pixel);


FScreenSpaceReflectionMobileOutputs GScreenSpaceReflectionMobileOutputs;

void FMobileSceneRenderer::InitScreenSpaceReflectionOutputs(FRHICommandListImmediate& RHICmdList, const TRefCountPtr<IPooledRenderTarget>& SceneColor)
{
	FPooledRenderTargetDesc SceneDepthZDesc = SceneColor->GetDesc();
	const FIntPoint& BufferSize = SceneDepthZDesc.Extent;

	if (!GScreenSpaceReflectionMobileOutputs.IsValid() || GScreenSpaceReflectionMobileOutputs.ScreenSpaceReflectionTexture->GetDesc().Extent != BufferSize)
	{
		GScreenSpaceReflectionMobileOutputs.ScreenSpaceReflectionTexture.SafeRelease();

		FPooledRenderTargetDesc Desc = FPooledRenderTargetDesc::Create2DDesc(
			BufferSize,
			PF_FloatRGBA,
			FClearValueBinding::None,
			TexCreate_None,
			TexCreate_ShaderResource | TexCreate_RenderTargetable,
			false);

		Desc.Flags |= GFastVRamConfig.SSR;

		GRenderTargetPool.FindFreeElement(RHICmdList, Desc, GScreenSpaceReflectionMobileOutputs.ScreenSpaceReflectionTexture, TEXT("ScreenSpaceReflectionTexture"));
	}
}

void FMobileSceneRenderer::ReleaseScreenSpaceReflectionOutputs()
{
	GScreenSpaceReflectionMobileOutputs.Release();
}

void FMobileSceneRenderer::RenderScreenSpaceReflection(FRDGBuilder& GraphBuilder, const FViewInfo& View, const FSceneRenderTargets& SceneContext)
{
	if (!bRequriesScreenSpaceReflectionPass || !View.HZB)
	{
		return;
	}

	RDG_EVENT_SCOPE(GraphBuilder, "ScreenSpaceReflection");

	FRDGTextureRef SceneColorTexture = GraphBuilder.RegisterExternalTexture(SceneContext.GetSceneColor(), TEXT("SceneColorTexture"));
	FRDGTextureRef WorldNormalTexture = GraphBuilder.RegisterExternalTexture(SceneContext.WorldNormalRoughness, TEXT("WorldNormalRoughnessTexture"));
	FRDGTextureRef ScreenSpaceReflectionTexture = GraphBuilder.RegisterExternalTexture(GScreenSpaceReflectionMobileOutputs.ScreenSpaceReflectionTexture, TEXT("ScreenSpaceReflectionTexture"));

	RenderScreenSpaceReflection(GraphBuilder, View, SceneColorTexture, WorldNormalTexture, ScreenSpaceReflectionTexture);
}

void FMobileSceneRenderer::RenderScreenSpaceReflection(FRDGBuilder& GraphBuilder, const FViewInfo& View, FRDGTextureRef SceneColorTexture, FRDGTextureRef WorldNormalRoughnessTexture, FRDGTextureRef ScreenSpaceReflectionTexture)
{
	{
		FMobileScreenSpaceReflectionPassPS::FParameters* PassParameters = GraphBuilder.AllocParameters<FMobileScreenSpaceReflectionPassPS::FParameters>();
		PassParameters->ViewUniformBuffer = View.ViewUniformBuffer;
		PassParameters->SceneColor = SceneColorTexture;
		PassParameters->SceneColorSampler = TStaticSamplerState<SF_Bilinear>::GetRHI();
		PassParameters->WorldNormalRoughnessTexture = WorldNormalRoughnessTexture;
		PassParameters->WorldNormalRoughnessSampler = TStaticSamplerState<SF_Point>::GetRHI();
		PassParameters->PreIntegratedGF = GSystemTextures.PreintegratedGF->GetRenderTargetItem().ShaderResourceTexture;
		PassParameters->PreIntegratedGFSampler = TStaticSamplerState<SF_Bilinear>::GetRHI();
		PassParameters->HZB = GraphBuilder.RegisterExternalTexture(View.HZB);
		PassParameters->HZBSampler = TStaticSamplerState<SF_Point>::GetRHI();

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
			FIntPoint BufferSize = SceneColorTexture->Desc.Extent;
			if (View.PrevViewInfo.TemporalAAHistory.IsValid())
			{
				ViewportOffset = View.PrevViewInfo.TemporalAAHistory.ViewportRect.Min;
				ViewportExtent = View.PrevViewInfo.TemporalAAHistory.ViewportRect.Size();
				BufferSize = View.PrevViewInfo.TemporalAAHistory.ReferenceBufferSize;
				ensure(ViewportExtent.X > 0 && ViewportExtent.Y > 0);
				ensure(BufferSize.X > 0 && BufferSize.Y > 0);
			}

			FVector2D InvBufferSize(1.0f / float(BufferSize.X), 1.0f / float(BufferSize.Y));

			PassParameters->PrevScreenPositionScaleBias = FVector4(
				ViewportExtent.X * 0.5f * InvBufferSize.X,
				-ViewportExtent.Y * 0.5f * InvBufferSize.Y,
				(ViewportExtent.X * 0.5f + ViewportOffset.X) * InvBufferSize.X,
				(ViewportExtent.Y * 0.5f + ViewportOffset.Y) * InvBufferSize.Y);
		}

		ESSRQuality SSRQuality;
		{			
			if (View.FinalPostProcessSettings.ScreenSpaceReflectionQuality >= 80.0f)
			{
				SSRQuality = ESSRQuality::Epic;
			}
			else if (View.FinalPostProcessSettings.ScreenSpaceReflectionQuality >= 60.0f)
			{
				SSRQuality = ESSRQuality::High;
			}
			else if (View.FinalPostProcessSettings.ScreenSpaceReflectionQuality >= 40.0f)
			{
				SSRQuality = ESSRQuality::Medium;
			}
			else
			{
				SSRQuality = ESSRQuality::Low;
			}

			float MaxRoughness = FMath::Clamp(View.FinalPostProcessSettings.ScreenSpaceReflectionMaxRoughness, 0.01f, 1.0f);
			float RoughnessMaskScale = -2.0f / MaxRoughness * (int32(SSRQuality) < 3 ? 2.0f : 1.0f);

			PassParameters->SSRParams = FLinearColor(
				FMath::Clamp(View.FinalPostProcessSettings.ScreenSpaceReflectionIntensity * 0.01f, 0.0f, 1.0f),
				RoughnessMaskScale,
				View.ViewState->GetCurrentTemporalAASampleIndex() * 1551,
				View.ViewState->GetFrameIndex(8) * 1551);
		}

		PassParameters->RenderTargets[0] = FRenderTargetBinding(ScreenSpaceReflectionTexture, ERenderTargetLoadAction::ENoAction);

		FMobileScreenSpaceReflectionPassPS::FPermutationDomain PermutationVector;
		PermutationVector.Set<FMobileScreenSpaceReflectionPassPS::FMobileSSRQualityDim>(SSRQuality);

		TShaderMapRef<FMobileScreenSpaceReflectionPassPS> PixelShader(View.ShaderMap, PermutationVector);

		GraphBuilder.AddPass(
			RDG_EVENT_NAME("SSR RayMarch(Quality=%d RayPerPixel=1) %dx%d", SSRQuality, View.ViewRect.Width(), View.ViewRect.Height()),
			PassParameters,
			ERDGPassFlags::Raster,
			[PassParameters, &View, PixelShader](FRHICommandList& RHICmdList)
			{
				RHICmdList.SetViewport(View.ViewRect.Min.X, View.ViewRect.Min.Y, 0.0f, View.ViewRect.Max.X, View.ViewRect.Max.Y, 1.0f);

				FGraphicsPipelineStateInitializer GraphicsPSOInit;
				FPixelShaderUtils::InitFullscreenPipelineState(RHICmdList, View.ShaderMap, PixelShader, /* out */ GraphicsPSOInit);

				SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);
				SetShaderParameters(RHICmdList, PixelShader, PixelShader.GetPixelShader(), *PassParameters);

				FPixelShaderUtils::DrawFullscreenTriangle(RHICmdList);
			});
	}

	{
		FMobileScreenSpaceReflectionCompositePassPS::FParameters* PassParameters = GraphBuilder.AllocParameters<FMobileScreenSpaceReflectionCompositePassPS::FParameters>();
		PassParameters->ViewUniformBuffer = View.ViewUniformBuffer;
		PassParameters->ScreenSpaceReflectionTexture = ScreenSpaceReflectionTexture;
		PassParameters->ScreenSpaceReflectionSampler = TStaticSamplerState<SF_Bilinear>::GetRHI();
		PassParameters->RenderTargets[0] = FRenderTargetBinding(SceneColorTexture, ERenderTargetLoadAction::ELoad);

		TShaderMapRef<FMobileScreenSpaceReflectionCompositePassPS> PixelShader(View.ShaderMap);
		GraphBuilder.AddPass(
			RDG_EVENT_NAME("SSR Composite %dx%d", View.ViewRect.Width(), View.ViewRect.Height()),
			PassParameters,
			ERDGPassFlags::Raster,
			[PassParameters, &View, PixelShader](FRHICommandList& RHICmdList)
			{
				RHICmdList.SetViewport(View.ViewRect.Min.X, View.ViewRect.Min.Y, 0.0f, View.ViewRect.Max.X, View.ViewRect.Max.Y, 1.0f);

				FGraphicsPipelineStateInitializer GraphicsPSOInit;
				FPixelShaderUtils::InitFullscreenPipelineState(RHICmdList, View.ShaderMap, PixelShader, /* out */ GraphicsPSOInit);
				GraphicsPSOInit.BlendState = TStaticBlendState<CW_RGB, BO_Add, BF_One, BF_One, BO_Add, BF_Zero, BF_One>::GetRHI();

				SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);
				SetShaderParameters(RHICmdList, PixelShader, PixelShader.GetPixelShader(), *PassParameters);

				FPixelShaderUtils::DrawFullscreenTriangle(RHICmdList);
			});
	}
}

