// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	LumenScenePrefilter.cpp
=============================================================================*/

#include "RendererPrivate.h"
#include "ScenePrivate.h"
#include "SceneUtils.h"
#include "PipelineStateCache.h"
#include "ShaderParameterStruct.h"
#include "LumenSceneUtils.h"

class FLumenCardCopyPS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FLumenCardCopyPS);
	SHADER_USE_PARAMETER_STRUCT(FLumenCardCopyPS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FLumenCardScene, LumenCardScene)
		SHADER_PARAMETER(float, SourceMip)
		SHADER_PARAMETER(FMatrix, ChannelSwizzle)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SourceAtlas)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportLumenGI(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment( const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment )
	{
		FGlobalShader::ModifyCompilationEnvironment( Parameters, OutEnvironment );
	}
};

IMPLEMENT_GLOBAL_SHADER(FLumenCardCopyPS, "/Engine/Private/Lumen/LumenSceneLighting.usf", "LumenCardCopyPS", SF_Pixel);

BEGIN_SHADER_PARAMETER_STRUCT(FLumenCardCopy, )
	SHADER_PARAMETER_STRUCT_INCLUDE(FRasterizeToCardsVS::FParameters, VS)
	SHADER_PARAMETER_STRUCT_INCLUDE(FLumenCardCopyPS::FParameters, PS)
	RENDER_TARGET_BINDING_SLOTS()
END_SHADER_PARAMETER_STRUCT()


class FLumenCardCopyDepthPS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FLumenCardCopyDepthPS);
	SHADER_USE_PARAMETER_STRUCT(FLumenCardCopyDepthPS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FLumenCardScene, LumenCardScene)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SourceDepthAtlas)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportLumenGI(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment( const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment )
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetRenderTargetOutputFormat(0, EPixelFormat::PF_G16R16);
	}
};

IMPLEMENT_GLOBAL_SHADER(FLumenCardCopyDepthPS, "/Engine/Private/Lumen/LumenSceneLighting.usf", "LumenCardCopyDepthPS", SF_Pixel);

BEGIN_SHADER_PARAMETER_STRUCT(FLumenCardCopyDepth, )
	SHADER_PARAMETER_STRUCT_INCLUDE(FRasterizeToCardsVS::FParameters, VS)
	SHADER_PARAMETER_STRUCT_INCLUDE(FLumenCardCopyDepthPS::FParameters, PS)
	RENDER_TARGET_BINDING_SLOTS()
END_SHADER_PARAMETER_STRUCT()

void FDeferredShadingSceneRenderer::CopyLumenSceneDepth(
	FRDGBuilder& GraphBuilder,
	TRDGUniformBufferRef<FLumenCardScene> LumenCardSceneUniformBuffer,
	FRDGTextureRef DepthBufferAtlas,
	const FViewInfo& View)
{
	LLM_SCOPE_BYTAG(Lumen);
	RDG_EVENT_SCOPE(GraphBuilder, "CopyLumenSceneDepth");

	FLumenSceneData& LumenSceneData = *Scene->LumenSceneData;

	FLumenCardScatterContext CardScatterContext;

	CardScatterContext.Init(
		GraphBuilder,
		View,
		LumenSceneData,
		LumenCardRenderer,
		ECullCardsMode::OperateOnCardPagesToRender);

	CardScatterContext.CullCardPagesToShape(
		GraphBuilder,
		View,
		LumenSceneData,
		LumenCardRenderer,
		LumenCardSceneUniformBuffer,
		ECullCardsShapeType::None,
		FCullCardsShapeParameters(),
		1.0f,
		0);

	CardScatterContext.BuildScatterIndirectArgs(
		GraphBuilder,
		View);

	FGlobalShaderMap* GlobalShaderMap = View.ShaderMap;

	FRDGTextureRef OpacityAtlas = GraphBuilder.RegisterExternalTexture(LumenSceneData.OpacityAtlas);
	FRDGTextureRef DepthAtlas = GraphBuilder.RegisterExternalTexture(LumenSceneData.DepthAtlas);

	FScene* LocalScene = Scene;
	const FIntPoint ViewportSize = LumenSceneData.GetPhysicalAtlasSize();

	{
		FLumenCardCopyDepth* PassParameters = GraphBuilder.AllocParameters<FLumenCardCopyDepth>();
		PassParameters->RenderTargets[0] = FRenderTargetBinding(DepthAtlas, ERenderTargetLoadAction::ENoAction, 0);
		PassParameters->VS.LumenCardScene = LumenCardSceneUniformBuffer;
		PassParameters->VS.CardScatterParameters = CardScatterContext.Parameters;
		PassParameters->VS.ScatterInstanceIndex = 0;
		PassParameters->VS.DownsampledInputAtlasSize = FVector2D::ZeroVector;
		PassParameters->PS.View = View.ViewUniformBuffer;
		PassParameters->PS.LumenCardScene = LumenCardSceneUniformBuffer;
		PassParameters->PS.SourceDepthAtlas = DepthBufferAtlas;

		GraphBuilder.AddPass(
			RDG_EVENT_NAME("CopyDepthMip0"),
			PassParameters,
			ERDGPassFlags::Raster,
			[LocalScene, PassParameters, ViewportSize, GlobalShaderMap](FRHICommandListImmediate& RHICmdList)
		{
			auto PixelShader = GlobalShaderMap->GetShader< FLumenCardCopyDepthPS >();
			DrawQuadsToAtlas(ViewportSize, PixelShader, PassParameters, GlobalShaderMap, TStaticBlendState<>::GetRHI(), RHICmdList);
		});
	}

	{
		FLumenCardCopy* PassParameters = GraphBuilder.AllocParameters<FLumenCardCopy>();
		PassParameters->RenderTargets[0] = FRenderTargetBinding(OpacityAtlas, ERenderTargetLoadAction::ENoAction, 0);
		PassParameters->VS.LumenCardScene = LumenCardSceneUniformBuffer;
		PassParameters->VS.CardScatterParameters = CardScatterContext.Parameters;
		PassParameters->VS.ScatterInstanceIndex = 0;
		PassParameters->VS.DownsampledInputAtlasSize = FVector2D::ZeroVector;
		PassParameters->PS.View = View.ViewUniformBuffer;
		PassParameters->PS.LumenCardScene = LumenCardSceneUniformBuffer;
		PassParameters->PS.SourceMip = 0;
		// Move Opacity in Alpha of AlbedoAtlas into R channel of OpacityAtlas
		PassParameters->PS.ChannelSwizzle = FMatrix(FPlane(0,0,0,0),FPlane(0,0,0,0),FPlane(0,0,0,0),FPlane(1,0,0,0));
		FRDGTextureRef AlbedoAtlas = GraphBuilder.RegisterExternalTexture(LumenSceneData.AlbedoAtlas);
		PassParameters->PS.SourceAtlas = AlbedoAtlas;

		GraphBuilder.AddPass(
			RDG_EVENT_NAME("CopyOpacityMip0"),
			PassParameters,
			ERDGPassFlags::Raster,
			[LocalScene, PassParameters, ViewportSize, GlobalShaderMap](FRHICommandListImmediate& RHICmdList)
		{
			auto PixelShader = GlobalShaderMap->GetShader< FLumenCardCopyPS >();
			DrawQuadsToAtlas(ViewportSize, PixelShader, PassParameters, GlobalShaderMap, TStaticBlendState<>::GetRHI(), RHICmdList);
		});
	}

	LumenSceneData.OpacityAtlas = GraphBuilder.ConvertToExternalTexture(OpacityAtlas);
	LumenSceneData.DepthAtlas = GraphBuilder.ConvertToExternalTexture(DepthAtlas);
}