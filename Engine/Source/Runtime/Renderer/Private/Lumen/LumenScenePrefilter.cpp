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

float GLumenSceneHeightfieldSlopeThreshold = 45;
FAutoConsoleVariableRef CVarLumenSceneHeightfieldSlopeThreshold(
	TEXT("r.LumenScene.HeightfieldSlopeThreshold"),
	GLumenSceneHeightfieldSlopeThreshold,
	TEXT(""),
	FConsoleVariableDelegate::CreateLambda([](IConsoleVariable* InVariable)
	{
		extern int32 GLumenSceneGeneration;
		FPlatformAtomics::InterlockedAdd(&GLumenSceneGeneration, 1);
	}),
	ECVF_Scalability | ECVF_RenderThreadSafe
	);

class FLumenCardCopyPS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FLumenCardCopyPS);
	SHADER_USE_PARAMETER_STRUCT(FLumenCardCopyPS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER_STRUCT_REF(FLumenCardScene, LumenCardScene)
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
		OutEnvironment.SetRenderTargetOutputFormat( 0, EPixelFormat::PF_R32_UINT );
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
		SHADER_PARAMETER_STRUCT_REF(FLumenCardScene, LumenCardScene)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SourceDepthAtlas)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportLumenGI(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment( const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment )
	{
		FGlobalShader::ModifyCompilationEnvironment( Parameters, OutEnvironment );
		OutEnvironment.SetRenderTargetOutputFormat( 0, EPixelFormat::PF_R32_UINT );
	}
};

IMPLEMENT_GLOBAL_SHADER(FLumenCardCopyDepthPS, "/Engine/Private/Lumen/LumenSceneLighting.usf", "LumenCardCopyDepthPS", SF_Pixel);

BEGIN_SHADER_PARAMETER_STRUCT(FLumenCardCopyDepth, )
	SHADER_PARAMETER_STRUCT_INCLUDE(FRasterizeToCardsVS::FParameters, VS)
	SHADER_PARAMETER_STRUCT_INCLUDE(FLumenCardCopyDepthPS::FParameters, PS)
	RENDER_TARGET_BINDING_SLOTS()
END_SHADER_PARAMETER_STRUCT()


class FLumenCardPrefilterDepthPS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FLumenCardPrefilterDepthPS);
	SHADER_USE_PARAMETER_STRUCT(FLumenCardPrefilterDepthPS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER_STRUCT_REF(FLumenCardScene, LumenCardScene)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2D, ParentDepthAtlas)
		SHADER_PARAMETER(FVector2D, InvSize)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportLumenGI(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment( const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment )
	{
		FGlobalShader::ModifyCompilationEnvironment( Parameters, OutEnvironment );
		OutEnvironment.SetRenderTargetOutputFormat( 0, EPixelFormat::PF_R32_UINT );
	}
};

IMPLEMENT_GLOBAL_SHADER(FLumenCardPrefilterDepthPS, "/Engine/Private/Lumen/LumenSceneLighting.usf", "LumenCardPrefilterDepthPS", SF_Pixel);

BEGIN_SHADER_PARAMETER_STRUCT(FLumenCardPrefilterDepth, )
	SHADER_PARAMETER_STRUCT_INCLUDE(FRasterizeToCardsVS::FParameters, VS)
	SHADER_PARAMETER_STRUCT_INCLUDE(FLumenCardPrefilterDepthPS::FParameters, PS)
	RENDER_TARGET_BINDING_SLOTS()
END_SHADER_PARAMETER_STRUCT()

class FLumenCardDilateForegroundDepthsPS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FLumenCardDilateForegroundDepthsPS);
	SHADER_USE_PARAMETER_STRUCT(FLumenCardDilateForegroundDepthsPS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER_STRUCT_REF(FLumenCardScene, LumenCardScene)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2D, ChildDepthAtlas)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2D, OriginalDepthAtlas)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportLumenGI(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment( const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment )
	{
		FGlobalShader::ModifyCompilationEnvironment( Parameters, OutEnvironment );
		OutEnvironment.SetRenderTargetOutputFormat( 0, EPixelFormat::PF_R32_UINT );
	}
};

IMPLEMENT_GLOBAL_SHADER(FLumenCardDilateForegroundDepthsPS, "/Engine/Private/Lumen/LumenSceneLighting.usf", "LumenCardDilateForegroundDepthsPS", SF_Pixel);

BEGIN_SHADER_PARAMETER_STRUCT(FLumenCardDilateForegroundDepths, )
	SHADER_PARAMETER_STRUCT_INCLUDE(FRasterizeToCardsVS::FParameters, VS)
	SHADER_PARAMETER_STRUCT_INCLUDE(FLumenCardDilateForegroundDepthsPS::FParameters, PS)
	RENDER_TARGET_BINDING_SLOTS()
END_SHADER_PARAMETER_STRUCT()


class FLumenCardPostprocessOpacityPS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FLumenCardPostprocessOpacityPS);
	SHADER_USE_PARAMETER_STRUCT(FLumenCardPostprocessOpacityPS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER_STRUCT_REF(FLumenCardScene, LumenCardScene)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2D, DepthAtlas)
		SHADER_PARAMETER(FVector2D, InvSize)
		SHADER_PARAMETER(float, TanHeightfieldSlopeThreshold)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportLumenGI(Parameters.Platform);
	}
};

IMPLEMENT_GLOBAL_SHADER(FLumenCardPostprocessOpacityPS, "/Engine/Private/Lumen/LumenSceneLighting.usf", "LumenCardPostprocessOpacityPS", SF_Pixel);

BEGIN_SHADER_PARAMETER_STRUCT(FLumenCardPostprocessOpacity, )
	SHADER_PARAMETER_STRUCT_INCLUDE(FRasterizeToCardsVS::FParameters, VS)
	SHADER_PARAMETER_STRUCT_INCLUDE(FLumenCardPostprocessOpacityPS::FParameters, PS)
	RENDER_TARGET_BINDING_SLOTS()
END_SHADER_PARAMETER_STRUCT()


class FLumenCardPrefilterOpacityPS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FLumenCardPrefilterOpacityPS);
	SHADER_USE_PARAMETER_STRUCT(FLumenCardPrefilterOpacityPS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER_STRUCT_REF(FLumenCardScene, LumenCardScene)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2D, ParentOpacityAtlas)
		SHADER_PARAMETER(FVector2D, InvSize)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportLumenGI(Parameters.Platform);
	}
};

IMPLEMENT_GLOBAL_SHADER(FLumenCardPrefilterOpacityPS, "/Engine/Private/Lumen/LumenSceneLighting.usf", "LumenCardPrefilterOpacityPS", SF_Pixel);

BEGIN_SHADER_PARAMETER_STRUCT(FLumenCardPrefilterOpacity, )
	SHADER_PARAMETER_STRUCT_INCLUDE(FRasterizeToCardsVS::FParameters, VS)
	SHADER_PARAMETER_STRUCT_INCLUDE(FLumenCardPrefilterOpacityPS::FParameters, PS)
	RENDER_TARGET_BINDING_SLOTS()
END_SHADER_PARAMETER_STRUCT()


void FDeferredShadingSceneRenderer::PrefilterLumenSceneDepth(
	FRDGBuilder& GraphBuilder,
	FRDGTextureRef DepthBufferAtlas,
	const TArray<uint32, SceneRenderingAllocator>& CardIdsToRender,
	const FViewInfo& View)
{
	LLM_SCOPE_BYTAG(Lumen);
	RDG_EVENT_SCOPE(GraphBuilder, "Prefilter");

	FLumenSceneData& LumenSceneData = *Scene->LumenSceneData;

	FLumenCardScatterContext CardScatterContext;

	CardScatterContext.Init(
		GraphBuilder,
		View,
		LumenSceneData,
		LumenCardRenderer,
		ECullCardsMode::OperateOnCardsToRender);

	CardScatterContext.CullCardsToShape(
		GraphBuilder,
		View,
		LumenSceneData,
		LumenCardRenderer,
		ECullCardsShapeType::None,
		FCullCardsShapeParameters(),
		1.0f,
		0);

	CardScatterContext.BuildScatterIndirectArgs(
		GraphBuilder,
		View);

	FGlobalShaderMap* GlobalShaderMap = View.ShaderMap;

	FRDGTextureRef OpacityAtlas = GraphBuilder.RegisterExternalTexture(LumenSceneData.OpacityAtlas);
	FRDGTextureRef DilatedDepthAtlas = GraphBuilder.RegisterExternalTexture(LumenSceneData.DepthAtlas);
	FRDGTextureRef UndilatedDepthAtlas = GraphBuilder.CreateTexture(DilatedDepthAtlas->Desc, TEXT("UndilatedDepthAtlas"));

	FScene* LocalScene = Scene;
	const FIntPoint ViewportSize = LumenSceneData.MaxAtlasSize;

	{
		FLumenCardCopyDepth* PassParameters = GraphBuilder.AllocParameters<FLumenCardCopyDepth>();
		PassParameters->RenderTargets[0] = FRenderTargetBinding(UndilatedDepthAtlas, ERenderTargetLoadAction::ENoAction, 0);
		PassParameters->VS.LumenCardScene = LumenSceneData.UniformBuffer;
		PassParameters->VS.CardScatterParameters = CardScatterContext.Parameters;
		PassParameters->VS.ScatterInstanceIndex = 0;
		PassParameters->VS.CardUVSamplingOffset = FVector2D::ZeroVector;
		PassParameters->PS.View = View.ViewUniformBuffer;
		PassParameters->PS.LumenCardScene = LumenSceneData.UniformBuffer;
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

	const int32 NumMips = FMath::CeilLogTwo(FMath::Max(LumenSceneData.MaxAtlasSize.X, LumenSceneData.MaxAtlasSize.Y)) + 1;
	{
		FIntPoint SrcSize = LumenSceneData.MaxAtlasSize;
		FIntPoint DestSize = SrcSize / 2;

		for (int32 MipIndex = 1; MipIndex < NumMips; MipIndex++)
		{
			SrcSize.X = FMath::Max(SrcSize.X, 1);
			SrcSize.Y = FMath::Max(SrcSize.Y, 1);
			DestSize.X = FMath::Max(DestSize.X, 1);
			DestSize.Y = FMath::Max(DestSize.Y, 1);

			FLumenCardPrefilterDepth* PassParameters = GraphBuilder.AllocParameters<FLumenCardPrefilterDepth>();
			PassParameters->RenderTargets[0] = FRenderTargetBinding(UndilatedDepthAtlas, ERenderTargetLoadAction::ENoAction, MipIndex);
			PassParameters->VS.LumenCardScene = LumenSceneData.UniformBuffer;
			PassParameters->VS.CardScatterParameters = CardScatterContext.Parameters;
			PassParameters->VS.ScatterInstanceIndex = 0;
			PassParameters->VS.CardUVSamplingOffset = FVector2D::ZeroVector;
			PassParameters->PS.View = View.ViewUniformBuffer;
			PassParameters->PS.LumenCardScene = LumenSceneData.UniformBuffer;
			PassParameters->PS.ParentDepthAtlas = GraphBuilder.CreateSRV(FRDGTextureSRVDesc::CreateForMipLevel(UndilatedDepthAtlas, MipIndex - 1));
			PassParameters->PS.InvSize = FVector2D(1.0f / SrcSize.X, 1.0f / SrcSize.Y);

			GraphBuilder.AddPass(
				RDG_EVENT_NAME("PrefilterDepthMip"),
				PassParameters,
				ERDGPassFlags::Raster,
				[LocalScene, PassParameters, DestSize, GlobalShaderMap](FRHICommandListImmediate& RHICmdList)
			{
				auto PixelShader = GlobalShaderMap->GetShader< FLumenCardPrefilterDepthPS >();
				DrawQuadsToAtlas(DestSize, PixelShader, PassParameters, GlobalShaderMap, TStaticBlendState<>::GetRHI(), RHICmdList);
			});

			SrcSize /= 2;
			DestSize /= 2;
		}
	}

	{
		FLumenCardCopy* PassParameters = GraphBuilder.AllocParameters<FLumenCardCopy>();
		PassParameters->RenderTargets[0] = FRenderTargetBinding(DilatedDepthAtlas, ERenderTargetLoadAction::ENoAction, NumMips - 1);
		PassParameters->VS.LumenCardScene = LumenSceneData.UniformBuffer;
		PassParameters->VS.CardScatterParameters = CardScatterContext.Parameters;
		PassParameters->VS.ScatterInstanceIndex = 0;
		PassParameters->VS.CardUVSamplingOffset = FVector2D::ZeroVector;
		PassParameters->PS.View = View.ViewUniformBuffer;
		PassParameters->PS.LumenCardScene = LumenSceneData.UniformBuffer;
		PassParameters->PS.SourceMip = NumMips - 1;
		PassParameters->PS.ChannelSwizzle = FMatrix::Identity;
		PassParameters->PS.SourceAtlas = UndilatedDepthAtlas;

		GraphBuilder.AddPass(
			RDG_EVENT_NAME("CopyLastMip"),
			PassParameters,
			ERDGPassFlags::Raster,
			[LocalScene, PassParameters, ViewportSize, GlobalShaderMap](FRHICommandListImmediate& RHICmdList)
		{
			auto PixelShader = GlobalShaderMap->GetShader< FLumenCardCopyPS >();
			DrawQuadsToAtlas(ViewportSize, PixelShader, PassParameters, GlobalShaderMap, TStaticBlendState<>::GetRHI(), RHICmdList);
		});
	}

	for (int32 MipIndex = NumMips - 2; MipIndex >= 0; MipIndex--)
	{
		FIntPoint DestSize(
			FMath::Max(LumenSceneData.MaxAtlasSize.X >> MipIndex, 1),
			FMath::Max(LumenSceneData.MaxAtlasSize.Y >> MipIndex, 1));

		FLumenCardDilateForegroundDepths* PassParameters = GraphBuilder.AllocParameters<FLumenCardDilateForegroundDepths>();
		PassParameters->RenderTargets[0] = FRenderTargetBinding(DilatedDepthAtlas, ERenderTargetLoadAction::ENoAction, MipIndex);
		PassParameters->VS.LumenCardScene = LumenSceneData.UniformBuffer;
		PassParameters->VS.CardScatterParameters = CardScatterContext.Parameters;
		PassParameters->VS.ScatterInstanceIndex = 0;
		PassParameters->VS.CardUVSamplingOffset = FVector2D::ZeroVector;
		PassParameters->PS.View = View.ViewUniformBuffer;
		PassParameters->PS.LumenCardScene = LumenSceneData.UniformBuffer;
		PassParameters->PS.ChildDepthAtlas = GraphBuilder.CreateSRV(FRDGTextureSRVDesc::CreateForMipLevel(DilatedDepthAtlas, MipIndex + 1));
		PassParameters->PS.OriginalDepthAtlas = GraphBuilder.CreateSRV(FRDGTextureSRVDesc::CreateForMipLevel(UndilatedDepthAtlas, MipIndex));

		GraphBuilder.AddPass(
			RDG_EVENT_NAME("DilateMip"),
			PassParameters,
			ERDGPassFlags::Raster,
			[LocalScene, PassParameters, DestSize, GlobalShaderMap](FRHICommandListImmediate& RHICmdList)
		{
			auto PixelShader = GlobalShaderMap->GetShader< FLumenCardDilateForegroundDepthsPS >();
			DrawQuadsToAtlas(DestSize, PixelShader, PassParameters, GlobalShaderMap, TStaticBlendState<>::GetRHI(), RHICmdList);
		});
	}

	{
		FLumenCardCopy* PassParameters = GraphBuilder.AllocParameters<FLumenCardCopy>();
		PassParameters->RenderTargets[0] = FRenderTargetBinding(OpacityAtlas, ERenderTargetLoadAction::ENoAction, 0);
		PassParameters->VS.LumenCardScene = LumenSceneData.UniformBuffer;
		PassParameters->VS.CardScatterParameters = CardScatterContext.Parameters;
		PassParameters->VS.ScatterInstanceIndex = 0;
		PassParameters->VS.CardUVSamplingOffset = FVector2D::ZeroVector;
		PassParameters->PS.View = View.ViewUniformBuffer;
		PassParameters->PS.LumenCardScene = LumenSceneData.UniformBuffer;
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

	{
		FIntPoint SrcSize = LumenSceneData.MaxAtlasSize;

		int32 MipIndex = 0;
		//for (int32 MipIndex = 0; MipIndex < NumMips; MipIndex++)
		{
			SrcSize.X = FMath::Max(SrcSize.X, 1);
			SrcSize.Y = FMath::Max(SrcSize.Y, 1);

			FLumenCardPostprocessOpacity* PassParameters = GraphBuilder.AllocParameters<FLumenCardPostprocessOpacity>();
			PassParameters->RenderTargets[0] = FRenderTargetBinding(OpacityAtlas, ERenderTargetLoadAction::ELoad, MipIndex);
			PassParameters->VS.LumenCardScene = LumenSceneData.UniformBuffer;
			PassParameters->VS.CardScatterParameters = CardScatterContext.Parameters;
			PassParameters->VS.ScatterInstanceIndex = 0;
			PassParameters->VS.CardUVSamplingOffset = FVector2D::ZeroVector;
			PassParameters->PS.View = View.ViewUniformBuffer;
			PassParameters->PS.LumenCardScene = LumenSceneData.UniformBuffer;
			PassParameters->PS.DepthAtlas = GraphBuilder.CreateSRV(FRDGTextureSRVDesc::CreateForMipLevel(DilatedDepthAtlas, MipIndex));
			PassParameters->PS.TanHeightfieldSlopeThreshold = FMath::Tan(FMath::Clamp<float>(GLumenSceneHeightfieldSlopeThreshold * PI / 180.0f, 0, PI / 2 - .1f));
			PassParameters->PS.InvSize = FVector2D(1.0f / SrcSize.X, 1.0f / SrcSize.Y);

			GraphBuilder.AddPass(
				RDG_EVENT_NAME("PostprocessOpacity"),
				PassParameters,
				ERDGPassFlags::Raster,
				[LocalScene, PassParameters, SrcSize, GlobalShaderMap](FRHICommandListImmediate& RHICmdList)
			{
				auto PixelShader = GlobalShaderMap->GetShader< FLumenCardPostprocessOpacityPS >();
				DrawQuadsToAtlas(SrcSize, PixelShader, PassParameters, GlobalShaderMap, TStaticBlendState<CW_RED, BO_Add, BF_Zero, BF_SourceColor>::GetRHI(), RHICmdList);
			});

			SrcSize /= 2;
		}
	}

	{
		FIntPoint SrcSize = LumenSceneData.MaxAtlasSize;
		FIntPoint DestSize = SrcSize / 2;

		for (int32 MipIndex = 1; MipIndex < NumMips; MipIndex++)
		{
			SrcSize.X = FMath::Max(SrcSize.X, 1);
			SrcSize.Y = FMath::Max(SrcSize.Y, 1);
			DestSize.X = FMath::Max(DestSize.X, 1);
			DestSize.Y = FMath::Max(DestSize.Y, 1);

			FLumenCardPrefilterOpacity* PassParameters = GraphBuilder.AllocParameters<FLumenCardPrefilterOpacity>();
			PassParameters->RenderTargets[0] = FRenderTargetBinding(OpacityAtlas, ERenderTargetLoadAction::ENoAction, MipIndex);
			PassParameters->VS.LumenCardScene = LumenSceneData.UniformBuffer;
			PassParameters->VS.CardScatterParameters = CardScatterContext.Parameters;
			PassParameters->VS.ScatterInstanceIndex = 0;
			PassParameters->VS.CardUVSamplingOffset = FVector2D::ZeroVector;
			PassParameters->PS.View = View.ViewUniformBuffer;
			PassParameters->PS.LumenCardScene = LumenSceneData.UniformBuffer;
			PassParameters->PS.ParentOpacityAtlas = GraphBuilder.CreateSRV(FRDGTextureSRVDesc::CreateForMipLevel(OpacityAtlas, MipIndex - 1));
			PassParameters->PS.InvSize = FVector2D(1.0f / SrcSize.X, 1.0f / SrcSize.Y);

			GraphBuilder.AddPass(
				RDG_EVENT_NAME("PrefilterOpacityMip"),
				PassParameters,
				ERDGPassFlags::Raster,
				[LocalScene, PassParameters, DestSize, GlobalShaderMap](FRHICommandListImmediate& RHICmdList)
			{
				auto PixelShader = GlobalShaderMap->GetShader< FLumenCardPrefilterOpacityPS >();
				DrawQuadsToAtlas(DestSize, PixelShader, PassParameters, GlobalShaderMap, TStaticBlendState<>::GetRHI(), RHICmdList);
			});

			SrcSize /= 2;
			DestSize /= 2;
		}
	}

	ConvertToExternalTexture(GraphBuilder, OpacityAtlas, LumenSceneData.OpacityAtlas);
	ConvertToExternalTexture(GraphBuilder, DilatedDepthAtlas, LumenSceneData.DepthAtlas);
}

class FLumenCardPrefilterLightingPS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FLumenCardPrefilterLightingPS);
	SHADER_USE_PARAMETER_STRUCT(FLumenCardPrefilterLightingPS, FGlobalShader);

	class FUseIrradianceAtlas : SHADER_PERMUTATION_INT("USE_IRRADIANCE_ATLAS", 2);
	class FUseIndirectIrradianceAtlas : SHADER_PERMUTATION_INT("USE_INDIRECTIRRADIANCE_ATLAS", 2);
	using FPermutationDomain = TShaderPermutationDomain<FUseIrradianceAtlas, FUseIndirectIrradianceAtlas>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER_STRUCT_REF(FLumenCardScene, LumenCardScene)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2D, ParentFinalLightingAtlas)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2D, ParentIrradianceAtlas)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2D, ParentIndirectIrradianceAtlas)
		SHADER_PARAMETER(FVector2D, InvSize)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportLumenGI(Parameters.Platform);
	}
};

IMPLEMENT_GLOBAL_SHADER(FLumenCardPrefilterLightingPS, "/Engine/Private/Lumen/LumenSceneLighting.usf", "LumenCardPrefilterLightingPS", SF_Pixel);

BEGIN_SHADER_PARAMETER_STRUCT(FLumenCardPrefilterLighting, )
	SHADER_PARAMETER_STRUCT_INCLUDE(FRasterizeToCardsVS::FParameters, VS)
	SHADER_PARAMETER_STRUCT_INCLUDE(FLumenCardPrefilterLightingPS::FParameters, PS)
	RENDER_TARGET_BINDING_SLOTS()
END_SHADER_PARAMETER_STRUCT()

void FDeferredShadingSceneRenderer::PrefilterLumenSceneLighting(
	FRDGBuilder& GraphBuilder,
	const FViewInfo& View,
	FLumenCardTracingInputs& TracingInputs,
	FGlobalShaderMap* GlobalShaderMap,
	const FLumenCardScatterContext& VisibleCardScatterContext)
{
	LLM_SCOPE_BYTAG(Lumen);
	RDG_EVENT_SCOPE(GraphBuilder, "Prefilter");

	FLumenSceneData& LumenSceneData = *Scene->LumenSceneData;

	const int32 NumMips = FMath::CeilLogTwo(FMath::Max(LumenSceneData.MaxAtlasSize.X, LumenSceneData.MaxAtlasSize.Y)) + 1;
	{
		FIntPoint SrcSize = LumenSceneData.MaxAtlasSize;
		FIntPoint DestSize = SrcSize / 2;

		for (int32 MipIndex = 1; MipIndex < NumMips; MipIndex++)
		{
			SrcSize.X = FMath::Max(SrcSize.X, 1);
			SrcSize.Y = FMath::Max(SrcSize.Y, 1);
			DestSize.X = FMath::Max(DestSize.X, 1);
			DestSize.Y = FMath::Max(DestSize.Y, 1);

			FLumenCardPrefilterLighting* PassParameters = GraphBuilder.AllocParameters<FLumenCardPrefilterLighting>();
			PassParameters->RenderTargets[0] = FRenderTargetBinding(TracingInputs.FinalLightingAtlas, ERenderTargetLoadAction::ENoAction, MipIndex);
			bool bUseIrradianceAtlas = Lumen::UseIrradianceAtlas();
			bool bUseIndirectIrradianceAtlas = Lumen::UseIndirectIrradianceAtlas();
			if (bUseIrradianceAtlas)
			{
				PassParameters->RenderTargets[1] = FRenderTargetBinding(TracingInputs.IrradianceAtlas, ERenderTargetLoadAction::ENoAction, MipIndex);
				if (bUseIndirectIrradianceAtlas)
				{
					PassParameters->RenderTargets[2] = FRenderTargetBinding(TracingInputs.IndirectIrradianceAtlas, ERenderTargetLoadAction::ENoAction, MipIndex);
				}
			}
			else if (bUseIndirectIrradianceAtlas)
			{
				PassParameters->RenderTargets[1] = FRenderTargetBinding(TracingInputs.IndirectIrradianceAtlas, ERenderTargetLoadAction::ENoAction, MipIndex);
			}
			PassParameters->VS.LumenCardScene = LumenSceneData.UniformBuffer;
			PassParameters->VS.CardScatterParameters = VisibleCardScatterContext.Parameters;
			PassParameters->VS.ScatterInstanceIndex = 0;
			PassParameters->VS.CardUVSamplingOffset = FVector2D::ZeroVector;
			PassParameters->PS.View = View.ViewUniformBuffer;
			PassParameters->PS.LumenCardScene = LumenSceneData.UniformBuffer;
			PassParameters->PS.ParentFinalLightingAtlas = GraphBuilder.CreateSRV(FRDGTextureSRVDesc::CreateForMipLevel(TracingInputs.FinalLightingAtlas, MipIndex - 1));
			if (bUseIrradianceAtlas)
			{
				PassParameters->PS.ParentIrradianceAtlas = GraphBuilder.CreateSRV(FRDGTextureSRVDesc::CreateForMipLevel(TracingInputs.IrradianceAtlas, MipIndex - 1));
			}
			if (bUseIndirectIrradianceAtlas)
			{
				PassParameters->PS.ParentIndirectIrradianceAtlas = GraphBuilder.CreateSRV(FRDGTextureSRVDesc::CreateForMipLevel(TracingInputs.IndirectIrradianceAtlas, MipIndex - 1));
			}
			PassParameters->PS.InvSize = FVector2D(1.0f / SrcSize.X, 1.0f / SrcSize.Y);

			FScene* LocalScene = Scene;

			GraphBuilder.AddPass(
				RDG_EVENT_NAME("PrefilterMip"),
				PassParameters,
				ERDGPassFlags::Raster,
				[LocalScene, PassParameters, DestSize, GlobalShaderMap, bUseIrradianceAtlas, bUseIndirectIrradianceAtlas](FRHICommandListImmediate& RHICmdList)
			{
				FLumenCardPrefilterLightingPS::FPermutationDomain PermutationVector;
				PermutationVector.Set<FLumenCardPrefilterLightingPS::FUseIrradianceAtlas>(bUseIrradianceAtlas != 0);
				PermutationVector.Set<FLumenCardPrefilterLightingPS::FUseIndirectIrradianceAtlas>(bUseIndirectIrradianceAtlas != 0);
				auto PixelShader = GlobalShaderMap->GetShader< FLumenCardPrefilterLightingPS >(PermutationVector);
				DrawQuadsToAtlas(DestSize, PixelShader, PassParameters, GlobalShaderMap, TStaticBlendState<>::GetRHI(), RHICmdList);
			});

			SrcSize /= 2;
			DestSize /= 2;
		}
	}
}
