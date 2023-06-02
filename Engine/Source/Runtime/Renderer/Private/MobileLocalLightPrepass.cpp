// Copyright Epic Games, Inc. All Rights Reserved.

#include "SceneRendering.h"
#include "DataDrivenShaderPlatformInfo.h"
#include "BasePassRendering.h"
#include "PixelShaderUtils.h"
#include "MobileBasePassRendering.h"

class FLocalLightPrepassPS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FLocalLightPrepassPS);
	SHADER_USE_PARAMETER_STRUCT(FLocalLightPrepassPS, FGlobalShader)

		BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER_STRUCT_INCLUDE(FSceneTextureShaderParameters, SceneTextures)
		SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FForwardLightData, ForwardLightData)
		RENDER_TARGET_BINDING_SLOTS()
		END_SHADER_PARAMETER_STRUCT()

		static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsMobilePlatform(Parameters.Platform) && MobileForwardEnablePrepassLocalLights(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FForwardLightingParameters::ModifyCompilationEnvironment(Parameters.Platform, OutEnvironment);
		OutEnvironment.SetRenderTargetOutputFormat(0, PF_FloatR11G11B10);
		OutEnvironment.SetRenderTargetOutputFormat(1, PF_A2B10G10R10);
	}
};

IMPLEMENT_GLOBAL_SHADER(FLocalLightPrepassPS, "/Engine/Private/MobileLocalLightPrepass.usf", "Main", SF_Pixel);

void FMobileSceneRenderer::RenderLocalLightPrepass(FRDGBuilder& GraphBuilder, FSceneTextures& SceneTextures)
{
	RDG_EVENT_SCOPE(GraphBuilder, "RenderLocalLightPrepass");
	QUICK_SCOPE_CYCLE_COUNTER(STAT_RenderLocalLightPrepass);

	for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
	{
		FViewInfo& View = Views[ViewIndex];
		bool bHasNoLocalLights = (!View.ForwardLightingResources.ForwardLightData) || (View.ForwardLightingResources.ForwardLightData->NumLocalLights == 0);
		if (!View.ShouldRenderView() || bHasNoLocalLights)
		{
			continue;
		}

		FLocalLightPrepassPS::FParameters* PassParameters = GraphBuilder.AllocParameters<FLocalLightPrepassPS::FParameters>();
		PassParameters->View = View.ViewUniformBuffer;
		PassParameters->SceneTextures = SceneTextures.GetSceneTextureShaderParameters(View.FeatureLevel);
		PassParameters->RenderTargets[0] = FRenderTargetBinding(SceneTextures.MobileLocalLightTextureA, ERenderTargetLoadAction::EClear);;
		PassParameters->RenderTargets[1] = FRenderTargetBinding(SceneTextures.MobileLocalLightTextureB, ERenderTargetLoadAction::EClear);;
		PassParameters->ForwardLightData = View.ForwardLightingResources.ForwardLightUniformBuffer;

		FGlobalShaderMap* GlobalShaderMap = GetGlobalShaderMap(FeatureLevel);
		TShaderMapRef<FLocalLightPrepassPS> PixelShader(GlobalShaderMap);

		FPixelShaderUtils::AddFullscreenPass(
			GraphBuilder,
			GlobalShaderMap,
			RDG_EVENT_NAME("RenderLocalLightPrepass"),
			PixelShader,
			PassParameters,
			FIntRect(0, 0, SceneTextures.MobileLocalLightTextureA->Desc.Extent.X, SceneTextures.MobileLocalLightTextureA->Desc.Extent.Y));
	}
}