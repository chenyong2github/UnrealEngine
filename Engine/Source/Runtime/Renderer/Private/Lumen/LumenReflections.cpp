// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	LumenReflections.cpp
=============================================================================*/

#include "RendererPrivate.h"
#include "ScenePrivate.h"
#include "SceneUtils.h"
#include "PipelineStateCache.h"
#include "ShaderParameterStruct.h"
#include "LumenSceneUtils.h"
#include "PixelShaderUtils.h"
#include "ReflectionEnvironment.h"
#include "DistanceFieldAmbientOcclusion.h"
#include "SingleLayerWaterRendering.h"

int32 GLumenReflectionsTraceCards = 0;
FAutoConsoleVariableRef GVarLumenReflectionsTraceCards(
	TEXT("r.Lumen.Reflections.TraceCards"),
	GLumenReflectionsTraceCards,
	TEXT("."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

float GReflectionTraceStepFactor = 2;
FAutoConsoleVariableRef CVarReflectionTraceStepFactor(
	TEXT("r.Lumen.Reflections.TraceStepFactor"),
	GReflectionTraceStepFactor,
	TEXT("."),
	ECVF_Scalability | ECVF_RenderThreadSafe
	);

float GLumenReflectionMinSampleRadius = 5;
FAutoConsoleVariableRef CVarLumenReflectionMinSampleRadius(
	TEXT("r.Lumen.Reflections.MinSampleRadius"),
	GLumenReflectionMinSampleRadius,
	TEXT("."),
	ECVF_Scalability | ECVF_RenderThreadSafe
	);

float GLumenReflectionMinTraceDistance = 0;
FAutoConsoleVariableRef CVarLumenReflectionMinTraceDistance(
	TEXT("r.Lumen.Reflections.MinTraceDistance"),
	GLumenReflectionMinTraceDistance,
	TEXT("."),
	ECVF_Scalability | ECVF_RenderThreadSafe
	);

float GLumenReflectionMaxTraceDistance = 5000.0f;
FAutoConsoleVariableRef CVarLumenReflectionMaxTraceDistance(
	TEXT("r.Lumen.Reflections.MaxTraceDistance"),
	GLumenReflectionMaxTraceDistance,
	TEXT("."),
	ECVF_Scalability | ECVF_RenderThreadSafe
	);

float GLumenReflectionSurfaceBias = 1;
FAutoConsoleVariableRef CVarLumenReflectionSurfaceBias(
	TEXT("r.Lumen.Reflections.SurfaceBias"),
	GLumenReflectionSurfaceBias,
	TEXT("."),
	ECVF_Scalability | ECVF_RenderThreadSafe
	);

float GLumenReflectionVoxelStepFactor = .5f;
FAutoConsoleVariableRef CVarLumenReflectionVoxelStepFactor(
	TEXT("r.Lumen.Reflections.VoxelStepFactor"),
	GLumenReflectionVoxelStepFactor,
	TEXT("."),
	ECVF_Scalability | ECVF_RenderThreadSafe
	);

int32 GReflectionStencilOptimization = 1;
FAutoConsoleVariableRef CVarReflectionStencilOptimization(
	TEXT("r.Lumen.Reflections.StencilOptimization"),
	GReflectionStencilOptimization,
	TEXT("."),
	ECVF_Scalability | ECVF_RenderThreadSafe
	);

int32 GLumenReflectionRoughFromDiffuse = 1;
FAutoConsoleVariableRef CVarLumenReflectionRoughFromDiffuse(
	TEXT("r.Lumen.Reflections.RoughFromDiffuse"),
	GLumenReflectionRoughFromDiffuse,
	TEXT("."),
	ECVF_Scalability | ECVF_RenderThreadSafe
	);

float GLumenReflectionRoughFromDiffuseRoughnessStart = .5f;
FAutoConsoleVariableRef CVarLumenReflectionRoughFromDiffuseRoughnessStart(
	TEXT("r.Lumen.Reflections.RoughFromDiffuseRoughnessStart"),
	GLumenReflectionRoughFromDiffuseRoughnessStart,
	TEXT("."),
	ECVF_Scalability | ECVF_RenderThreadSafe
	);

float GLumenReflectionRoughFromDiffuseRoughnessFadeLength = .1f;
FAutoConsoleVariableRef CVarLumenReflectionRoughFromDiffuseRoughnessFadeLength(
	TEXT("r.Lumen.Reflections.RoughFromDiffuseRoughnessFadeLength"),
	GLumenReflectionRoughFromDiffuseRoughnessFadeLength,
	TEXT("."),
	ECVF_Scalability | ECVF_RenderThreadSafe
	);

class FLumenReflectionStencilPS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FLumenReflectionStencilPS)
	SHADER_USE_PARAMETER_STRUCT(FLumenReflectionStencilPS, FGlobalShader)

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		RENDER_TARGET_BINDING_SLOTS()
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER_STRUCT_INCLUDE(FSceneTextureParameters, SceneTextures)
		SHADER_PARAMETER_STRUCT_INCLUDE(FSceneTextureSamplerParameters, SceneTextureSamplers)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SSRTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, SSRSampler)
		SHADER_PARAMETER(float, RoughFromDiffuseRoughnessStart)
		SHADER_PARAMETER(float, RoughFromDiffuseRoughnessFadeLength)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportLumenGI(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
	}
};

IMPLEMENT_GLOBAL_SHADER(FLumenReflectionStencilPS, "/Engine/Private/Lumen/LumenReflections.usf", "LumenReflectionStencilPS", SF_Pixel);


class FLumenReflectionsPS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FLumenReflectionsPS)
	SHADER_USE_PARAMETER_STRUCT(FLumenReflectionsPS, FGlobalShader)

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		RENDER_TARGET_BINDING_SLOTS()
		SHADER_PARAMETER_STRUCT_INCLUDE(FLumenCardTracingParameters, TracingParameters)
		SHADER_PARAMETER_STRUCT_INCLUDE(FSceneTextureParameters, SceneTextures)
		SHADER_PARAMETER_STRUCT_INCLUDE(FSceneTextureSamplerParameters, SceneTextureSamplers)
		SHADER_PARAMETER_STRUCT_INCLUDE(FLumenIndirectTracingParameters, IndirectTracingParameters)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, RoughSpecularIndirectTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, RoughSpecularIndirectSampler)
		SHADER_PARAMETER(float, DownsampleFactor)
		SHADER_PARAMETER(float, RoughFromDiffuseRoughnessStart)
		SHADER_PARAMETER(float, RoughFromDiffuseRoughnessFadeLength)
	END_SHADER_PARAMETER_STRUCT()

	class FDynamicSkyLight : SHADER_PERMUTATION_BOOL("ENABLE_DYNAMIC_SKY_LIGHT");
	class FCardBVH : SHADER_PERMUTATION_BOOL("CARD_BVH");
	class FTraceCards : SHADER_PERMUTATION_BOOL("REFLECTIONS_TRACE_CARDS");

	using FPermutationDomain = TShaderPermutationDomain<FDynamicSkyLight, FCardBVH, FTraceCards>;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportLumenGI(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
	}
};

IMPLEMENT_GLOBAL_SHADER(FLumenReflectionsPS, "/Engine/Private/Lumen/LumenReflections.usf", "LumenReflectionsPS", SF_Pixel);


class FLumenRoughReflectionsPS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FLumenRoughReflectionsPS)
	SHADER_USE_PARAMETER_STRUCT(FLumenRoughReflectionsPS, FGlobalShader)

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		RENDER_TARGET_BINDING_SLOTS()
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER_STRUCT_INCLUDE(FSceneTextureParameters, SceneTextures)
		SHADER_PARAMETER_STRUCT_INCLUDE(FSceneTextureSamplerParameters, SceneTextureSamplers)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, RoughSpecularIndirectTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, RoughSpecularIndirectSampler)
		SHADER_PARAMETER(float, RoughFromDiffuseRoughnessStart)
		SHADER_PARAMETER(float, RoughFromDiffuseRoughnessFadeLength)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportLumenGI(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
	}
};

IMPLEMENT_GLOBAL_SHADER(FLumenRoughReflectionsPS, "/Engine/Private/Lumen/LumenReflections.usf", "LumenRoughReflectionsPS", SF_Pixel);


bool ShouldRenderLumenReflections(const FViewInfo& View)
{
	const FScene* Scene = (const FScene*) View.Family->Scene; // -V595
	if (Scene)
	{
		FLumenSceneData& LumenSceneData = *Scene->LumenSceneData; // -V595

		return GAllowLumenScene
			&& DoesPlatformSupportLumenGI(View.GetShaderPlatform())
			&& (LumenSceneData.VisibleCardsIndices.Num() > 0 || ShouldRenderDynamicSkyLight(Scene, *View.Family))
			&& LumenSceneData.AlbedoAtlas
			&& View.Family->EngineShowFlags.LumenReflections
			&& View.ViewState;
	}

	return false;
}

BEGIN_SHADER_PARAMETER_STRUCT(FLumenReflectionStencilParameters, )
	SHADER_PARAMETER_STRUCT_INCLUDE(FWaterTileVS::FParameters, VS)
	SHADER_PARAMETER_STRUCT_INCLUDE(FLumenReflectionStencilPS::FParameters, PS)
	SHADER_PARAMETER_RDG_BUFFER(Buffer<uint>, IndirectDrawParameter)
END_SHADER_PARAMETER_STRUCT()

BEGIN_SHADER_PARAMETER_STRUCT(FLumenReflectionsParameters, )
	SHADER_PARAMETER_STRUCT_INCLUDE(FWaterTileVS::FParameters, VS)
	SHADER_PARAMETER_STRUCT_INCLUDE(FLumenReflectionsPS::FParameters, PS)
	SHADER_PARAMETER_RDG_BUFFER(Buffer<uint>, IndirectDrawParameter)
END_SHADER_PARAMETER_STRUCT()

BEGIN_SHADER_PARAMETER_STRUCT(FLumenRoughReflectionsParameters, )
	SHADER_PARAMETER_STRUCT_INCLUDE(FWaterTileVS::FParameters, VS)
	SHADER_PARAMETER_STRUCT_INCLUDE(FLumenRoughReflectionsPS::FParameters, PS)
	SHADER_PARAMETER_RDG_BUFFER(Buffer<uint>, IndirectDrawParameter)
END_SHADER_PARAMETER_STRUCT()

void SetupLumenSpecularTracingParameters(FLumenIndirectTracingParameters& OutParameters)
{
	OutParameters.StepFactor = FMath::Clamp(GReflectionTraceStepFactor, .1f, 10.0f);
	OutParameters.VoxelStepFactor = FMath::Clamp(GLumenReflectionVoxelStepFactor, .01f, 10.0f);
	OutParameters.CardTraceEndDistanceFromCamera = 4000.0f;
	OutParameters.MinSampleRadius = FMath::Clamp(GLumenReflectionMinSampleRadius, .01f, 100.0f);
	OutParameters.MinTraceDistance = FMath::Clamp(GLumenReflectionMinTraceDistance, .01f, 1000.0f);
	OutParameters.MaxTraceDistance = FMath::Clamp(GLumenReflectionMaxTraceDistance, .01f, (float)HALF_WORLD_MAX);
	OutParameters.MaxCardTraceDistance = 0.0f;
	OutParameters.SurfaceBias = FMath::Clamp(GLumenReflectionSurfaceBias, .01f, 100.0f);
	OutParameters.CardInterpolateInfluenceRadius = 10.0f;
	OutParameters.DiffuseConeHalfAngle = 0.0f;
	OutParameters.TanDiffuseConeHalfAngle = 0.0f;
}

void FDeferredShadingSceneRenderer::RenderLumenReflections(
	FRDGBuilder& GraphBuilder, 
	const FViewInfo& View,
	const FSceneTextureParameters& SceneTextures,
	const TRefCountPtr<IPooledRenderTarget>& LumenRoughSpecularIndirect, 
	FRDGTextureRef& InOutReflectionComposition,
	FTiledScreenSpaceReflection* TiledScreenSpaceReflection)
{
	LLM_SCOPE(ELLMTag::Lumen);

	if (ShouldRenderLumenReflections(View))
	{
		FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(GraphBuilder.RHICmdList);

		{
			RDG_EVENT_SCOPE(GraphBuilder, "LumenReflections");
			FRDGTextureRef ReflectionInput = InOutReflectionComposition; 

			FRDGTextureRef ReflectionOutput;
			ERenderTargetLoadAction ReflectionLoadAction;
			FRHIBlendState* BlendState;

			if (!InOutReflectionComposition)
			{
				FPooledRenderTargetDesc Desc(FPooledRenderTargetDesc::Create2DDesc(SceneContext.GetBufferSizeXY(), PF_FloatRGBA, FClearValueBinding::Transparent, TexCreate_None, TexCreate_ShaderResource | TexCreate_RenderTargetable, false));
				ReflectionInput = GraphBuilder.RegisterExternalTexture(GSystemTextures.BlackDummy, TEXT("NoReflection"));
				ReflectionOutput = GraphBuilder.CreateTexture(Desc, TEXT("LumenReflections"));
				ReflectionLoadAction = ERenderTargetLoadAction::EClear;
				BlendState = TStaticBlendState<>::GetRHI();
			}
			else
			{
				ReflectionOutput = ReflectionInput;
				ReflectionLoadAction = ERenderTargetLoadAction::ELoad;
				BlendState = TStaticBlendState<CW_RGBA, BO_Add, BF_InverseDestAlpha, BF_One, BO_Add, BF_InverseDestAlpha, BF_One>::GetRHI();
			}

			FRHIDepthStencilState* ReadStencilState;
			FRDGTextureRef StencilTexture;
			
			if (GReflectionStencilOptimization)
			{
				FLumenReflectionStencilParameters* PassParameters = GraphBuilder.AllocParameters<FLumenReflectionStencilParameters>();

				FLumenReflectionStencilPS::FPermutationDomain PermutationVector;
				auto PixelShader = View.ShaderMap->GetShader<FLumenReflectionStencilPS>(PermutationVector);

				StencilTexture = SceneTextures.SceneDepthBuffer;

				PassParameters->PS.RenderTargets.DepthStencil = FDepthStencilBinding(StencilTexture, ERenderTargetLoadAction::ENoAction, ERenderTargetLoadAction::EClear, FExclusiveDepthStencil::DepthNop_StencilWrite);
				PassParameters->PS.View = View.ViewUniformBuffer;
				PassParameters->PS.SceneTextures = SceneTextures;
				SetupSceneTextureSamplers(&PassParameters->PS.SceneTextureSamplers);
				PassParameters->PS.SSRTexture = ReflectionInput;
				PassParameters->PS.SSRSampler = TStaticSamplerState<SF_Bilinear>::GetRHI();
				PassParameters->PS.RoughFromDiffuseRoughnessStart = GLumenReflectionRoughFromDiffuseRoughnessStart;
				PassParameters->PS.RoughFromDiffuseRoughnessFadeLength = GLumenReflectionRoughFromDiffuseRoughnessFadeLength;

				FRHIDepthStencilState* WriteDepthStencilState = TStaticDepthStencilState<
					false, CF_Always,
					// Write 1
					true, CF_Always, SO_Keep, SO_Keep, SO_Replace,
					false, CF_Always, SO_Keep, SO_Keep, SO_Keep,
					0xff, 0xff>::GetRHI();

				SingleLayerWaterAddTiledFullscreenPass(
					GraphBuilder,
					View.ShaderMap,
					RDG_EVENT_NAME("ReflectionStencil %dx%d", View.ViewRect.Width(), View.ViewRect.Height()),
					PixelShader,
					PassParameters,
					View.ViewUniformBuffer,
					View.ViewRect,
					TiledScreenSpaceReflection,
					TStaticBlendState<>::GetRHI(),
					TStaticRasterizerState<>::GetRHI(),
					WriteDepthStencilState,
					1);

				ReadStencilState = TStaticDepthStencilState<
					false, CF_Always,
					// Pass for pixels whose stencil is not equal to 0
					true, CF_NotEqual, SO_Keep, SO_Keep, SO_Keep,
					false, CF_Always, SO_Keep, SO_Keep, SO_Keep,
					0xff, 0xff>::GetRHI();
			}
			else
			{
				ReadStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();
				StencilTexture = nullptr;
			}

			{
				extern int32 GLumenGICardBVH;

				FLumenReflectionsPS::FPermutationDomain PermutationVector;
				PermutationVector.Set< FLumenReflectionsPS::FDynamicSkyLight >(ShouldRenderDynamicSkyLight(Scene, ViewFamily));
				PermutationVector.Set< FLumenReflectionsPS::FCardBVH >(GLumenGICardBVH != 0);
				PermutationVector.Set< FLumenReflectionsPS::FTraceCards >(GLumenReflectionsTraceCards != 0);
				auto PixelShader = View.ShaderMap->GetShader<FLumenReflectionsPS>(PermutationVector);

				FLumenReflectionsParameters* PassParameters = GraphBuilder.AllocParameters<FLumenReflectionsParameters>();
				PassParameters->PS.RenderTargets[0] = FRenderTargetBinding(ReflectionOutput, ReflectionLoadAction);

				if (StencilTexture)
				{
					PassParameters->PS.RenderTargets.DepthStencil = FDepthStencilBinding(StencilTexture, ERenderTargetLoadAction::ELoad, ERenderTargetLoadAction::ELoad, FExclusiveDepthStencil::DepthRead_StencilRead);
				}


				FLumenCardTracingInputs TracingInputs(GraphBuilder, Scene, View);
				GetLumenCardTracingParameters(View, TracingInputs, PassParameters->PS.TracingParameters);
				PassParameters->PS.SceneTextures = SceneTextures;
				SetupSceneTextureSamplers(&PassParameters->PS.SceneTextureSamplers);
				SetupLumenSpecularTracingParameters(PassParameters->PS.IndirectTracingParameters);
				PassParameters->PS.RoughFromDiffuseRoughnessStart = GLumenReflectionRoughFromDiffuseRoughnessStart;
				PassParameters->PS.RoughFromDiffuseRoughnessFadeLength = GLumenReflectionRoughFromDiffuseRoughnessFadeLength;

				SingleLayerWaterAddTiledFullscreenPass(
					GraphBuilder,
					View.ShaderMap,
					RDG_EVENT_NAME("ConeTraceReflection %ux%u", View.ViewRect.Width(), View.ViewRect.Height()),
					PixelShader,
					PassParameters,
					View.ViewUniformBuffer,
					View.ViewRect,
					TiledScreenSpaceReflection,
					BlendState,
					TStaticRasterizerState<>::GetRHI(),
					ReadStencilState,
					0);
			}

			if (GLumenReflectionRoughFromDiffuse && LumenRoughSpecularIndirect)
			{
				FRDGTextureRef RoughSpecularIndirectTexture = GraphBuilder.RegisterExternalTexture(LumenRoughSpecularIndirect.GetReference() ? LumenRoughSpecularIndirect : GSystemTextures.BlackDummy);

				auto PixelShader = View.ShaderMap->GetShader<FLumenRoughReflectionsPS>(0);

				FLumenRoughReflectionsParameters* PassParameters = GraphBuilder.AllocParameters<FLumenRoughReflectionsParameters>();
				PassParameters->PS.RenderTargets[0] = FRenderTargetBinding(ReflectionOutput, ERenderTargetLoadAction::ELoad);

				PassParameters->PS.View = View.ViewUniformBuffer;
				PassParameters->PS.SceneTextures = SceneTextures;
				SetupSceneTextureSamplers(&PassParameters->PS.SceneTextureSamplers);
				PassParameters->PS.RoughSpecularIndirectTexture = RoughSpecularIndirectTexture;
				PassParameters->PS.RoughSpecularIndirectSampler = TStaticSamplerState<SF_Bilinear>::GetRHI();
				PassParameters->PS.RoughFromDiffuseRoughnessStart = GLumenReflectionRoughFromDiffuseRoughnessStart;
				PassParameters->PS.RoughFromDiffuseRoughnessFadeLength = GLumenReflectionRoughFromDiffuseRoughnessFadeLength;

				SingleLayerWaterAddTiledFullscreenPass(
					GraphBuilder,
					View.ShaderMap,
					RDG_EVENT_NAME("RoughReflections %ux%u", View.ViewRect.Width(), View.ViewRect.Height()),
					PixelShader,
					PassParameters,
					View.ViewUniformBuffer,
					View.ViewRect,
					TiledScreenSpaceReflection,
					TStaticBlendState<CW_RGBA, BO_Add, BF_InverseDestAlpha, BF_One, BO_Add, BF_InverseDestAlpha, BF_One>::GetRHI());
			}

			InOutReflectionComposition = ReflectionOutput;
		}
	}
}

