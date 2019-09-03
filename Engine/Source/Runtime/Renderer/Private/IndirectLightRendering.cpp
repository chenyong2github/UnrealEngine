// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "IndirectLightRendering.h"
#include "RenderGraph.h"
#include "PixelShaderUtils.h"
#include "AmbientCubemapParameters.h"
#include "SceneTextureParameters.h"
#include "ScreenSpaceDenoise.h"
#include "ScreenSpaceRayTracing.h"
#include "DeferredShadingRenderer.h"
#include "PostProcessing.h" // for FPostProcessVS
#include "RendererModule.h" 
#include "RayTracing/RaytracingOptions.h"


static TAutoConsoleVariable<int32> CVarDiffuseIndirectDenoiser(
	TEXT("r.DiffuseIndirect.Denoiser"), 1,
	TEXT("Denoising options (default = 1)"),
	ECVF_RenderThreadSafe);


bool IsAmbientCubemapPassRequired(const FSceneView& View);


class FDiffuseIndirectCompositePS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FDiffuseIndirectCompositePS)
	SHADER_USE_PARAMETER_STRUCT(FDiffuseIndirectCompositePS, FGlobalShader)

	class FApplyDiffuseIndirectDim : SHADER_PERMUTATION_BOOL("DIM_APPLY_DIFFUSE_INDIRECT");
	class FApplyAmbientOcclusionDim : SHADER_PERMUTATION_BOOL("DIM_APPLY_AMBIENT_OCCLUSION");

	using FPermutationDomain = TShaderPermutationDomain<
		FApplyDiffuseIndirectDim,
		FApplyAmbientOcclusionDim>;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		FPermutationDomain PermutationVector(Parameters.PermutationId);

		// Do not compile a shader that does not apply anything.
		if (!PermutationVector.Get<FApplyDiffuseIndirectDim>() &&
			!PermutationVector.Get<FApplyAmbientOcclusionDim>())
		{
			return false;
		}

		// Diffuse indirect generation is SM5 only.
		if (PermutationVector.Get<FApplyDiffuseIndirectDim>())
		{
			return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
		}

		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(float, AmbientOcclusionStaticFraction)

		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, DiffuseIndirectTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, DiffuseIndirectSampler)
		
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, AmbientOcclusionTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState,  AmbientOcclusionSampler)
		
		SHADER_PARAMETER_STRUCT_INCLUDE(FSceneTextureParameters, SceneTextures)
		SHADER_PARAMETER_STRUCT_INCLUDE(FSceneTextureSamplerParameters, SceneTextureSamplers)
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, ViewUniformBuffer)
		
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()
};

class FAmbientCubemapCompositePS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FAmbientCubemapCompositePS)
	SHADER_USE_PARAMETER_STRUCT(FAmbientCubemapCompositePS, FGlobalShader)

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_TEXTURE(Texture2D, PreIntegratedGF)
		SHADER_PARAMETER_SAMPLER(SamplerState, PreIntegratedGFSampler)

		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, AmbientOcclusionTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState,  AmbientOcclusionSampler)
		
		SHADER_PARAMETER_STRUCT_INCLUDE(FAmbientCubemapParameters, AmbientCubemap)
		SHADER_PARAMETER_STRUCT_INCLUDE(FSceneTextureParameters, SceneTextures)
		SHADER_PARAMETER_STRUCT_INCLUDE(FSceneTextureSamplerParameters, SceneTextureSamplers)
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, ViewUniformBuffer)
		
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()
};

IMPLEMENT_GLOBAL_SHADER(FDiffuseIndirectCompositePS, "/Engine/Private/DiffuseIndirectComposite.usf", "MainPS", SF_Pixel);
IMPLEMENT_GLOBAL_SHADER(FAmbientCubemapCompositePS, "/Engine/Private/AmbientCubemapComposite.usf", "MainPS", SF_Pixel);


void FDeferredShadingSceneRenderer::RenderDiffuseIndirectAndAmbientOcclusion(FRHICommandListImmediate& RHICmdListImmediate)
{
	SCOPED_DRAW_EVENT(RHICmdListImmediate, DiffuseIndirectAndAO)

	FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(RHICmdListImmediate);

	// Forwared shading SSAO is applied before the basepass using only the depth buffer.
	if (IsForwardShadingEnabled(ViewFamily.GetShaderPlatform()))
	{
		return;
	}

	FRDGBuilder GraphBuilder(RHICmdListImmediate);
	
	FSceneTextureParameters SceneTextures;
	SetupSceneTextureParameters(GraphBuilder, &SceneTextures);
	
	FRDGTextureRef SceneColor = GraphBuilder.RegisterExternalTexture(SceneContext.GetSceneColor());

	for (FViewInfo& View : Views)
	{
		// TODO: enum cvar. 
		const bool bApplyRTGI = ShouldRenderRayTracingGlobalIllumination(View);
		const bool bApplySSGI = ShouldRenderScreenSpaceDiffuseIndirect(View);
		const bool bApplySSAO = SceneContext.bScreenSpaceAOIsValid;
		const bool bApplyRTAO = ShouldRenderRayTracingAmbientOcclusion(View);

		int32 DenoiseMode = CVarDiffuseIndirectDenoiser.GetValueOnRenderThread();

		IScreenSpaceDenoiser::FAmbientOcclusionRayTracingConfig RayTracingConfig;

		// TODO: hybrid SSGI / RTGI
		IScreenSpaceDenoiser::FDiffuseIndirectInputs DenoiserInputs;
		if (bApplyRTGI)
		{
			RenderRayTracingGlobalIllumination(GraphBuilder, SceneTextures, View, /* out */ &RayTracingConfig, /* out */ &DenoiserInputs);
		}
		else if (bApplySSGI)
		{
			static bool bWarnExperimental = false;
			if (!bWarnExperimental)
			{
				UE_LOG(LogRenderer, Warning, TEXT("SSGI is experimental."));
				bWarnExperimental = true;
			}

			RenderScreenSpaceDiffuseIndirect(GraphBuilder, SceneTextures, SceneColor, View, /* out */ &DenoiserInputs);
			
			// TODO: Denoise.
			DenoiseMode = 0;
		}
		else
		{
			// No need for denoising.
			DenoiseMode = 0;
		}
		
		IScreenSpaceDenoiser::FDiffuseIndirectOutputs DenoiserOutputs;
		if (DenoiseMode != 0)
		{
			const IScreenSpaceDenoiser* DefaultDenoiser = IScreenSpaceDenoiser::GetDefaultDenoiser();
			const IScreenSpaceDenoiser* DenoiserToUse = DenoiseMode == 1 ? DefaultDenoiser : GScreenSpaceDenoiser;

			RDG_EVENT_SCOPE(GraphBuilder, "%s%s(DiffuseIndirect) %dx%d",
				DenoiserToUse != DefaultDenoiser ? TEXT("ThirdParty ") : TEXT(""),
				DenoiserToUse->GetDebugName(),
				View.ViewRect.Width(), View.ViewRect.Height());

			DenoiserOutputs = DenoiserToUse->DenoiseDiffuseIndirect(
				GraphBuilder,
				View,
				&View.PrevViewInfo,
				SceneTextures,
				DenoiserInputs,
				RayTracingConfig);
		}
		else
		{
			DenoiserOutputs.Color = DenoiserInputs.Color;
			DenoiserOutputs.AmbientOcclusionMask = DenoiserInputs.AmbientOcclusionMask;
		}

		// Render RTAO that override any technic.
		if (bApplyRTAO)
		{
			FRDGTextureRef AmbientOcclusionMask = nullptr;

			RenderRayTracingAmbientOcclusion(
				GraphBuilder,
				View,
				SceneTextures,
				&AmbientOcclusionMask);

			DenoiserOutputs.AmbientOcclusionMask = AmbientOcclusionMask;
		}

		// Extract the dynamic AO for application of AO beyond RenderDiffuseIndirectAndAmbientOcclusion()
		if (DenoiserOutputs.AmbientOcclusionMask)
		{
			//ensureMsgf(!bApplySSAO, TEXT("Looks like SSAO has been computed for this view but is being overridden."));
			ensureMsgf(Views.Num() == 1, TEXT("Need to add support for one AO texture per view in FSceneRenderTargets")); // TODO.
			GraphBuilder.QueueTextureExtraction(DenoiserOutputs.AmbientOcclusionMask, &SceneContext.ScreenSpaceAO);
			SceneContext.bScreenSpaceAOIsValid = true;
		}
		else if (bApplySSAO)
		{
			// Fetch result of SSAO that was done earlier.
			DenoiserOutputs.AmbientOcclusionMask = GraphBuilder.RegisterExternalTexture(SceneContext.ScreenSpaceAO);
		}

		// Applies diffuse indirect and ambient occlusion to the scene color.
		if (DenoiserOutputs.Color || DenoiserOutputs.AmbientOcclusionMask)
		{
			FDiffuseIndirectCompositePS::FParameters* PassParameters = GraphBuilder.AllocParameters<FDiffuseIndirectCompositePS::FParameters>();
			
			PassParameters->AmbientOcclusionStaticFraction = FMath::Clamp(View.FinalPostProcessSettings.AmbientOcclusionStaticFraction, 0.0f, 1.0f);

			PassParameters->DiffuseIndirectTexture = DenoiserOutputs.Color;
			PassParameters->DiffuseIndirectSampler = TStaticSamplerState<SF_Point>::GetRHI();

			PassParameters->AmbientOcclusionTexture = DenoiserOutputs.AmbientOcclusionMask;
			PassParameters->AmbientOcclusionSampler = TStaticSamplerState<SF_Point>::GetRHI();
			
			PassParameters->SceneTextures = SceneTextures;
			SetupSceneTextureSamplers(&PassParameters->SceneTextureSamplers);
			PassParameters->ViewUniformBuffer = View.ViewUniformBuffer;

			PassParameters->RenderTargets[0] = FRenderTargetBinding(
				SceneColor, ERenderTargetLoadAction::ELoad);
		
			FDiffuseIndirectCompositePS::FPermutationDomain PermutationVector;
			PermutationVector.Set<FDiffuseIndirectCompositePS::FApplyDiffuseIndirectDim>(PassParameters->DiffuseIndirectTexture != nullptr);
			PermutationVector.Set<FDiffuseIndirectCompositePS::FApplyAmbientOcclusionDim>(PassParameters->AmbientOcclusionTexture != nullptr);

			TShaderMapRef<FDiffuseIndirectCompositePS> PixelShader(View.ShaderMap, PermutationVector);
			ClearUnusedGraphResources(*PixelShader, PassParameters);

			GraphBuilder.AddPass(
				RDG_EVENT_NAME(
					"DiffuseIndirectComposite(ApplyAO=%s ApplyDiffuseIndirect=%s) %dx%d",
					PermutationVector.Get<FDiffuseIndirectCompositePS::FApplyAmbientOcclusionDim>() ? TEXT("Yes") : TEXT("No"),
					PermutationVector.Get<FDiffuseIndirectCompositePS::FApplyDiffuseIndirectDim>() ? TEXT("Yes") : TEXT("No"),
					View.ViewRect.Width(), View.ViewRect.Height()),
				PassParameters,
				ERDGPassFlags::Raster,
				[PassParameters, &View, PixelShader, PermutationVector](FRHICommandList& RHICmdList)
			{
				RHICmdList.SetViewport(View.ViewRect.Min.X, View.ViewRect.Min.Y, 0.0f, View.ViewRect.Max.X, View.ViewRect.Max.Y, 0.0);
				
				FGraphicsPipelineStateInitializer GraphicsPSOInit;
				FPixelShaderUtils::InitFullscreenPipelineState(RHICmdList, View.ShaderMap, *PixelShader, /* out */ GraphicsPSOInit);
				
				if (PermutationVector.Get<FDiffuseIndirectCompositePS::FApplyAmbientOcclusionDim>())
				{
					GraphicsPSOInit.BlendState = TStaticBlendState<CW_RGBA, BO_Add, BF_One, BF_SourceAlpha, BO_Add, BF_Zero, BF_SourceAlpha>::GetRHI();
				}
				else
				{
					GraphicsPSOInit.BlendState = TStaticBlendState<CW_RGB, BO_Add, BF_One, BF_One>::GetRHI();
				}
				SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);

				SetShaderParameters(RHICmdList, *PixelShader, PixelShader->GetPixelShader(), *PassParameters);

				FPixelShaderUtils::DrawFullscreenTriangle(RHICmdList);
			});
		} // if (DenoiserOutputs.Color || bApplySSAO)

		// Apply the ambient cubemaps
		if (IsAmbientCubemapPassRequired(View))
		{
			FAmbientCubemapCompositePS::FParameters* PassParameters = GraphBuilder.AllocParameters<FAmbientCubemapCompositePS::FParameters>();
			
			PassParameters->PreIntegratedGF = GSystemTextures.PreintegratedGF->GetRenderTargetItem().ShaderResourceTexture;
			PassParameters->PreIntegratedGFSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
			
			PassParameters->AmbientOcclusionTexture = DenoiserOutputs.AmbientOcclusionMask;
			PassParameters->AmbientOcclusionSampler = TStaticSamplerState<SF_Point>::GetRHI();
			
			if (!PassParameters->AmbientOcclusionTexture)
			{
				PassParameters->AmbientOcclusionTexture = GraphBuilder.RegisterExternalTexture(GSystemTextures.WhiteDummy);
			}

			PassParameters->SceneTextures = SceneTextures;
			SetupSceneTextureSamplers(&PassParameters->SceneTextureSamplers);
			PassParameters->ViewUniformBuffer = View.ViewUniformBuffer;

			PassParameters->RenderTargets[0] = FRenderTargetBinding(
				SceneColor, ERenderTargetLoadAction::ELoad);
		
			TShaderMapRef<FAmbientCubemapCompositePS> PixelShader(View.ShaderMap);
			GraphBuilder.AddPass(
				RDG_EVENT_NAME("AmbientCubemapComposite %dx%d", View.ViewRect.Width(), View.ViewRect.Height()),
				PassParameters,
				ERDGPassFlags::Raster,
				[PassParameters, &View, PixelShader](FRHICommandList& RHICmdList)
			{
				TShaderMapRef<FPostProcessVS> VertexShader(View.ShaderMap);
				
				RHICmdList.SetViewport(View.ViewRect.Min.X, View.ViewRect.Min.Y, 0.0f, View.ViewRect.Max.X, View.ViewRect.Max.Y, 0.0);

				FGraphicsPipelineStateInitializer GraphicsPSOInit;
				RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);

				// set the state
				GraphicsPSOInit.BlendState = TStaticBlendState<CW_RGB, BO_Add, BF_One, BF_One, BO_Add, BF_One, BF_One>::GetRHI();
				GraphicsPSOInit.RasterizerState = TStaticRasterizerState<>::GetRHI();
				GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();

				GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GFilterVertexDeclaration.VertexDeclarationRHI;
				GraphicsPSOInit.BoundShaderState.VertexShaderRHI = GETSAFERHISHADER_VERTEX(*VertexShader);
				GraphicsPSOInit.BoundShaderState.PixelShaderRHI = GETSAFERHISHADER_PIXEL(*PixelShader);
				GraphicsPSOInit.PrimitiveType = PT_TriangleList;

				SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);

				uint32 Count = View.FinalPostProcessSettings.ContributingCubemaps.Num();
				for (const FFinalPostProcessSettings::FCubemapEntry& CubemapEntry : View.FinalPostProcessSettings.ContributingCubemaps)
				{
					FAmbientCubemapCompositePS::FParameters ShaderParameters = *PassParameters;
					SetupAmbientCubemapParameters(CubemapEntry, &ShaderParameters.AmbientCubemap);
					SetShaderParameters(RHICmdList, *PixelShader, PixelShader->GetPixelShader(), ShaderParameters);
					
					DrawPostProcessPass(
						RHICmdList,
						0, 0,
						View.ViewRect.Width(), View.ViewRect.Height(),
						View.ViewRect.Min.X, View.ViewRect.Min.Y,
						View.ViewRect.Width(), View.ViewRect.Height(),
						View.ViewRect.Size(),
						FSceneRenderTargets::Get(RHICmdList).GetBufferSizeXY(),
						*VertexShader,
						View.StereoPass, 
						false, // TODO.
						EDRF_UseTriangleOptimization);
				}
			});
		} // if (IsAmbientCubemapPassRequired(View))
	} // for (FViewInfo& View : Views)

	GraphBuilder.Execute();
}
