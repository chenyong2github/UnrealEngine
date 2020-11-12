// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	LumenReflections.cpp
=============================================================================*/

#include "LumenReflections.h"
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

extern FLumenGatherCvarState GLumenGatherCvars;

int32 GLumenReflectionDownsampleFactor = 1;
FAutoConsoleVariableRef GVarLumenReflectionDownsampleFactor(
	TEXT("r.Lumen.Reflections.DownsampleFactor"),
	GLumenReflectionDownsampleFactor,
	TEXT(""),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

int32 GLumenReflectionTraceCards = 1;
FAutoConsoleVariableRef GVarLumenReflectionTraceCards(
	TEXT("r.Lumen.Reflections.TraceCards"),
	GLumenReflectionTraceCards,
	TEXT(""),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

float GLumenReflectionMaxRoughnessToTrace = .4f;
FAutoConsoleVariableRef GVarLumenReflectionMaxRoughnessToTrace(
	TEXT("r.Lumen.Reflections.MaxRoughnessToTrace"),
	GLumenReflectionMaxRoughnessToTrace,
	TEXT(""),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

float GLumenReflectionRoughnessFadeLength = .1f;
FAutoConsoleVariableRef GVarLumenReflectionRoughnessFadeLength(
	TEXT("r.Lumen.Reflections.RoughnessFadeLength"),
	GLumenReflectionRoughnessFadeLength,
	TEXT(""),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

float GLumenReflectionGGXSamplingBias = .1f;
FAutoConsoleVariableRef GVarLumenReflectionGGXSamplingBias(
	TEXT("r.Lumen.Reflections.GGXSamplingBias"),
	GLumenReflectionGGXSamplingBias,
	TEXT(""),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

int32 GLumenReflectionTemporalFilter = 1;
FAutoConsoleVariableRef CVarLumenReflectionTemporalFilter(
	TEXT("r.Lumen.Reflections.Temporal"),
	GLumenReflectionTemporalFilter,
	TEXT("Whether to use a temporal filter"),
	ECVF_Scalability | ECVF_RenderThreadSafe
	);

float GLumenReflectionHistoryWeight = .9f;
FAutoConsoleVariableRef CVarLumenReflectionHistoryWeight(
	TEXT("r.Lumen.Reflections.Temporal.HistoryWeight"),
	GLumenReflectionHistoryWeight,
	TEXT("Weight of the history lighting.  Values closer to 1 exponentially decrease noise but also response time to lighting changes."),
	ECVF_RenderThreadSafe
	);

float GLumenReflectionHistoryDistanceThreshold = 30;
FAutoConsoleVariableRef CVarLumenReflectionHistoryDistanceThreshold(
	TEXT("r.Lumen.Reflections.Temporal.DistanceThreshold"),
	GLumenReflectionHistoryDistanceThreshold,
	TEXT("World space distance threshold needed to discard last frame's lighting results.  Lower values reduce ghosting from characters when near a wall but increase flickering artifacts."),
	ECVF_RenderThreadSafe
	);

float GLumenReflectionMaxRayIntensity = 100;
FAutoConsoleVariableRef GVarLumenReflectionMaxRayIntensity(
	TEXT("r.Lumen.Reflections.MaxRayIntensity"),
	GLumenReflectionMaxRayIntensity,
	TEXT("Clamps the maximum ray lighting intensity (with PreExposure) to reduce fireflies."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

int32 GLumenReflectionScreenSpaceReconstruction = 1;
FAutoConsoleVariableRef CVarLumenReflectionScreenSpaceReconstruction(
	TEXT("r.Lumen.Reflections.ScreenSpaceReconstruction"),
	GLumenReflectionScreenSpaceReconstruction,
	TEXT("Whether to use the screen space BRDF reweighting reconstruction"),
	ECVF_RenderThreadSafe
	);

int32 GLumenReflectionScreenSpaceReconstructionNumSamples = 5;
FAutoConsoleVariableRef CVarLumenReflectionScreenSpaceReconstructionNumSamples(
	TEXT("r.Lumen.Reflections.ScreenSpaceReconstruction.NumSamples"),
	GLumenReflectionScreenSpaceReconstructionNumSamples,
	TEXT("Number of samples to use for the screen space BRDF reweighting reconstruction"),
	ECVF_RenderThreadSafe
	);

float GLumenReflectionScreenSpaceReconstructionScreenWidth = .02f;
FAutoConsoleVariableRef CVarLumenReflectionScreenSpaceReconstructionKernelScreenWidth(
	TEXT("r.Lumen.Reflections.ScreenSpaceReconstruction.KernelScreenWidth"),
	GLumenReflectionScreenSpaceReconstructionScreenWidth,
	TEXT("Size of the kernel in a fraction of the screen"),
	ECVF_RenderThreadSafe
	);

float GLumenReflectionScreenSpaceReconstructionRoughnessScale = 1.0f;
FAutoConsoleVariableRef CVarLumenReflectionScreenSpaceReconstructionRoughnessScale(
	TEXT("r.Lumen.Reflections.ScreenSpaceReconstruction.RoughnessScale"),
	GLumenReflectionScreenSpaceReconstructionRoughnessScale,
	TEXT("Values higher than 1 allow neighbor traces to be blurred together more aggressively, but is not physically correct."),
	ECVF_RenderThreadSafe
	);

class FReflectionClearTileIndirectArgsCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FReflectionClearTileIndirectArgsCS)
	SHADER_USE_PARAMETER_STRUCT(FReflectionClearTileIndirectArgsCS, FGlobalShader)

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWReflectionResolveTileIndirectArgs)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWReflectionTracingTileIndirectArgs)
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

IMPLEMENT_GLOBAL_SHADER(FReflectionClearTileIndirectArgsCS, "/Engine/Private/Lumen/LumenReflections.usf", "ReflectionClearTileIndirectArgsCS", SF_Compute);


class FReflectionGBufferTileClassificationCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FReflectionGBufferTileClassificationCS)
	SHADER_USE_PARAMETER_STRUCT(FReflectionGBufferTileClassificationCS, FGlobalShader)

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWReflectionResolveTileIndirectArgs)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWReflectionTracingTileIndirectArgs)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWReflectionResolveTileData)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWReflectionTracingTileData)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, RWDownsampledDepth)
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER(float, MaxRoughnessToTrace)
		SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FSceneTextureUniformParameters, SceneTexturesStruct)
		SHADER_PARAMETER_STRUCT_INCLUDE(FLumenReflectionTracingParameters, ReflectionTracingParameters)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportLumenGI(Parameters.Platform);
	}

	static uint32 GetThreadGroupSize(uint32 DownsampleFactor)
	{
		if (DownsampleFactor == 1)
		{
			return 8;
		}
		else if (DownsampleFactor == 2)
		{
			return 16;
		}
		else if (DownsampleFactor == 3)
		{
			return 24;
		}
		else if (DownsampleFactor == 4)
		{
			return 32;
		}
		else
		{
			return MAX_uint32;
		}
	}

	class FThreadGroupSize : SHADER_PERMUTATION_SPARSE_INT("THREADGROUP_SIZE", 8, 16, 24, 32);
	using FPermutationDomain = TShaderPermutationDomain<FThreadGroupSize>;

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
	}
};

IMPLEMENT_GLOBAL_SHADER(FReflectionGBufferTileClassificationCS, "/Engine/Private/Lumen/LumenReflections.usf", "ReflectionGBufferTileClassificationCS", SF_Compute);


class FReflectionGenerateRaysCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FReflectionGenerateRaysCS)
	SHADER_USE_PARAMETER_STRUCT(FReflectionGenerateRaysCS, FGlobalShader)

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, RWRayBuffer)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, RWDownsampledDepth)
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER(float, MaxRoughnessToTrace)
		SHADER_PARAMETER(float, GGXSamplingBias)
		SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FSceneTextureUniformParameters, SceneTexturesStruct)
		SHADER_PARAMETER_STRUCT_INCLUDE(FLumenReflectionTracingParameters, ReflectionTracingParameters)
		SHADER_PARAMETER_STRUCT_INCLUDE(FLumenReflectionTileParameters, ReflectionTileParameters)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportLumenGI(Parameters.Platform);
	}

	static int32 GetGroupSize() 
	{
		return 8;
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE"), GetGroupSize());
	}
};

IMPLEMENT_GLOBAL_SHADER(FReflectionGenerateRaysCS, "/Engine/Private/Lumen/LumenReflections.usf", "ReflectionGenerateRaysCS", SF_Compute);


class FReflectionResolveCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FReflectionResolveCS)
	SHADER_USE_PARAMETER_STRUCT(FReflectionResolveCS, FGlobalShader)

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float3>, RWSpecularIndirect)
		SHADER_PARAMETER(float, MaxRoughnessToTrace)
		SHADER_PARAMETER(float, InvRoughnessFadeLength)
		SHADER_PARAMETER(uint32, NumSpatialReconstructionSamples)
		SHADER_PARAMETER(float, SpatialReconstructionScreenWidth)
		SHADER_PARAMETER(float, SpatialReconstructionRoughnessScale)
		SHADER_PARAMETER_STRUCT_INCLUDE(FLumenReflectionTracingParameters, ReflectionTracingParameters)
		SHADER_PARAMETER_STRUCT_INCLUDE(FLumenReflectionTileParameters, ReflectionTileParameters)
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FSceneTextureUniformParameters, SceneTexturesStruct)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportLumenGI(Parameters.Platform);
	}

	static int32 GetGroupSize() 
	{
		return 8;
	}

	class FSpatialReconstruction : SHADER_PERMUTATION_BOOL("USE_SPATIAL_RECONSTRUCTION");
	using FPermutationDomain = TShaderPermutationDomain<FSpatialReconstruction>;

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE"), GetGroupSize());
	}
};

IMPLEMENT_GLOBAL_SHADER(FReflectionResolveCS, "/Engine/Private/Lumen/LumenReflections.usf", "ReflectionResolveCS", SF_Compute);


class FReflectionTemporalReprojectionCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FReflectionTemporalReprojectionCS)
	SHADER_USE_PARAMETER_STRUCT(FReflectionTemporalReprojectionCS, FGlobalShader)

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float3>, RWSpecularIndirect)
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FSceneTextureUniformParameters, SceneTexturesStruct)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SpecularIndirectHistory)
		SHADER_PARAMETER(float,HistoryDistanceThreshold)
		SHADER_PARAMETER(float,HistoryWeight)
		SHADER_PARAMETER(float,PrevInvPreExposure)
		SHADER_PARAMETER(FVector2D,InvDiffuseIndirectBufferSize)
		SHADER_PARAMETER(FVector4,HistoryScreenPositionScaleBias)
		SHADER_PARAMETER(FVector4,HistoryUVMinMax)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, VelocityTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, VelocityTextureSampler)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, ResolvedReflections)
		SHADER_PARAMETER_STRUCT_INCLUDE(FLumenReflectionTileParameters, ReflectionTileParameters)
	END_SHADER_PARAMETER_STRUCT()

	using FPermutationDomain = TShaderPermutationDomain<>;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportLumenGI(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
	}
};

IMPLEMENT_GLOBAL_SHADER(FReflectionTemporalReprojectionCS, "/Engine/Private/Lumen/LumenReflections.usf", "ReflectionTemporalReprojectionCS", SF_Compute);


class FReflectionPassthroughCopyCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FReflectionPassthroughCopyCS)
	SHADER_USE_PARAMETER_STRUCT(FReflectionPassthroughCopyCS, FGlobalShader)

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float3>, RWSpecularIndirect)
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, ResolvedReflections)
		SHADER_PARAMETER_STRUCT_INCLUDE(FLumenReflectionTileParameters, ReflectionTileParameters)
	END_SHADER_PARAMETER_STRUCT()

	using FPermutationDomain = TShaderPermutationDomain<>;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportLumenGI(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
	}
};

IMPLEMENT_GLOBAL_SHADER(FReflectionPassthroughCopyCS, "/Engine/Private/Lumen/LumenReflections.usf", "ReflectionPassthroughCopyCS", SF_Compute);


bool ShouldRenderLumenReflections(const FViewInfo& View)
{
	const FScene* Scene = (const FScene*)View.Family->Scene;
	if (Scene)
	{
		return Lumen::ShouldRenderLumenForView(Scene, View) && View.Family->EngineShowFlags.LumenReflections;
	}
	
	return false;
}

FLumenReflectionTileParameters ReflectionTileClassification(
	FRDGBuilder& GraphBuilder,
	const FViewInfo& View,
	const FLumenReflectionTracingParameters& ReflectionTracingParameters)
{
	FLumenReflectionTileParameters ReflectionTileParameters;

	const int32 NumTracingTiles = FMath::DivideAndRoundUp(ReflectionTracingParameters.ReflectionTracingBufferSize.X, FReflectionGenerateRaysCS::GetGroupSize())
		* FMath::DivideAndRoundUp(ReflectionTracingParameters.ReflectionTracingBufferSize.Y, FReflectionGenerateRaysCS::GetGroupSize());
	const int32 NumResolveTiles = NumTracingTiles * ReflectionTracingParameters.ReflectionDownsampleFactor * ReflectionTracingParameters.ReflectionDownsampleFactor;

	FRDGBufferRef ReflectionResolveTileData = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateBufferDesc(sizeof(uint32), NumResolveTiles), TEXT("ReflectionResolveTileData"));
	FRDGBufferRef ReflectionResolveTileIndirectArgs = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateIndirectDesc<FRHIDispatchIndirectParameters>(1), TEXT("ReflectionResolveTileIndirectArgs"));

	FRDGBufferRef ReflectionTracingTileData = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateBufferDesc(sizeof(uint32), NumTracingTiles), TEXT("ReflectionTracingTileData"));
	FRDGBufferRef ReflectionTracingTileIndirectArgs = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateIndirectDesc<FRHIDispatchIndirectParameters>(1), TEXT("ReflectionTracingTileIndirectArgs"));

	{
		FReflectionClearTileIndirectArgsCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FReflectionClearTileIndirectArgsCS::FParameters>();
		PassParameters->RWReflectionResolveTileIndirectArgs = GraphBuilder.CreateUAV(ReflectionResolveTileIndirectArgs, PF_R32_UINT);
		PassParameters->RWReflectionTracingTileIndirectArgs = GraphBuilder.CreateUAV(ReflectionTracingTileIndirectArgs, PF_R32_UINT);

		auto ComputeShader = View.ShaderMap->GetShader<FReflectionClearTileIndirectArgsCS>(0);

		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("ClearTileIndirectArgs"),
			ComputeShader,
			PassParameters,
			FIntVector(1, 1, 1));
	}

	const uint32 TileClassificationGroupSize = FReflectionGBufferTileClassificationCS::GetThreadGroupSize(ReflectionTracingParameters.ReflectionDownsampleFactor);
	check(TileClassificationGroupSize != MAX_uint32);
	{
		FReflectionGBufferTileClassificationCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FReflectionGBufferTileClassificationCS::FParameters>();
		PassParameters->RWReflectionResolveTileIndirectArgs = GraphBuilder.CreateUAV(ReflectionResolveTileIndirectArgs, PF_R32_UINT);
		PassParameters->RWReflectionTracingTileIndirectArgs = GraphBuilder.CreateUAV(ReflectionTracingTileIndirectArgs, PF_R32_UINT);
		PassParameters->RWReflectionResolveTileData = GraphBuilder.CreateUAV(ReflectionResolveTileData, PF_R32_UINT);
		PassParameters->RWReflectionTracingTileData = GraphBuilder.CreateUAV(ReflectionTracingTileData, PF_R32_UINT);
		PassParameters->RWDownsampledDepth = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(ReflectionTracingParameters.DownsampledDepth));
		PassParameters->View = View.ViewUniformBuffer;
		PassParameters->MaxRoughnessToTrace = GLumenReflectionMaxRoughnessToTrace;
		PassParameters->SceneTexturesStruct = CreateSceneTextureUniformBuffer(GraphBuilder, View.FeatureLevel);
		PassParameters->ReflectionTracingParameters = ReflectionTracingParameters;

		FReflectionGBufferTileClassificationCS::FPermutationDomain PermutationVector;
		PermutationVector.Set< FReflectionGBufferTileClassificationCS::FThreadGroupSize >(TileClassificationGroupSize);
		auto ComputeShader = View.ShaderMap->GetShader<FReflectionGBufferTileClassificationCS>(PermutationVector);

		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("GBufferTileClassification %ux%u DownsampleFactor %u", View.ViewRect.Width(), View.ViewRect.Height(), ReflectionTracingParameters.ReflectionDownsampleFactor),
			ComputeShader,
			PassParameters,
			FComputeShaderUtils::GetGroupCount(View.ViewRect.Size(), TileClassificationGroupSize));
	}

	ReflectionTileParameters.ResolveIndirectArgs = ReflectionResolveTileIndirectArgs;
	ReflectionTileParameters.TracingIndirectArgs = ReflectionTracingTileIndirectArgs;
	ReflectionTileParameters.ReflectionResolveTileData = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(ReflectionResolveTileData, PF_R32_UINT));
	ReflectionTileParameters.ReflectionTracingTileData = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(ReflectionTracingTileData, PF_R32_UINT));
	return ReflectionTileParameters;
}

void UpdateHistoryReflections(
	FRDGBuilder& GraphBuilder,
	const FViewInfo& View, 
	FIntPoint BufferSize,
	const FLumenReflectionTileParameters& ReflectionTileParameters,
	FRDGTextureRef ResolvedReflections,
	FRDGTextureRef FinalSpecularIndirect)
{
	LLM_SCOPE_BYTAG(Lumen);
	
	FSceneTextureParameters SceneTextures = GetSceneTextureParameters(GraphBuilder);

	// Fallback to a black texture if no velocity.
	if (!SceneTextures.GBufferVelocityTexture)
	{
		SceneTextures.GBufferVelocityTexture = GSystemTextures.GetBlackDummy(GraphBuilder);
	}

	if (GLumenReflectionTemporalFilter
		&& View.ViewState
		&& View.ViewState->Lumen.ReflectionState.SpecularIndirectHistoryRT
		&& !View.bCameraCut 
		&& !View.bPrevTransformsReset
		// If the scene render targets reallocate, toss the history so we don't read uninitialized data
		&& View.ViewState->Lumen.ReflectionState.SpecularIndirectHistoryRT->GetDesc().Extent == BufferSize)
	{
		FReflectionTemporalState& ReflectionTemporalState = View.ViewState->Lumen.ReflectionState;
		TRefCountPtr<IPooledRenderTarget>* SpecularIndirectHistoryState = &ReflectionTemporalState.SpecularIndirectHistoryRT;
		FIntRect* HistoryViewRect = &ReflectionTemporalState.HistoryViewRect;
		FVector4* HistoryScreenPositionScaleBias = &ReflectionTemporalState.HistoryScreenPositionScaleBias;

		{
			FRDGTextureRef OldSpecularIndirectHistory = GraphBuilder.RegisterExternalTexture(*SpecularIndirectHistoryState);

			FReflectionTemporalReprojectionCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FReflectionTemporalReprojectionCS::FParameters>();
			PassParameters->RWSpecularIndirect = GraphBuilder.CreateUAV(FinalSpecularIndirect);
			PassParameters->View = View.ViewUniformBuffer;
			PassParameters->SceneTexturesStruct = CreateSceneTextureUniformBuffer(GraphBuilder, View.FeatureLevel);
			PassParameters->SpecularIndirectHistory = OldSpecularIndirectHistory;
			PassParameters->HistoryDistanceThreshold = GLumenReflectionHistoryDistanceThreshold;
			PassParameters->HistoryWeight = GLumenReflectionHistoryWeight;
			PassParameters->PrevInvPreExposure = 1.0f / View.PrevViewInfo.SceneColorPreExposure;
			const FVector2D InvBufferSize(1.0f / BufferSize.X, 1.0f / BufferSize.Y);
			PassParameters->InvDiffuseIndirectBufferSize = InvBufferSize;
			PassParameters->HistoryScreenPositionScaleBias = *HistoryScreenPositionScaleBias;

			// Pull in the max UV to exclude the region which will read outside the viewport due to bilinear filtering
			PassParameters->HistoryUVMinMax = FVector4(
				(HistoryViewRect->Min.X + 0.5f) * InvBufferSize.X,
				(HistoryViewRect->Min.Y + 0.5f) * InvBufferSize.Y,
				(HistoryViewRect->Max.X - 0.5f) * InvBufferSize.X,
				(HistoryViewRect->Max.Y - 0.5f) * InvBufferSize.Y);

			PassParameters->VelocityTexture = SceneTextures.GBufferVelocityTexture;
			PassParameters->VelocityTextureSampler = TStaticSamplerState<SF_Bilinear>::GetRHI();
			PassParameters->ResolvedReflections = ResolvedReflections;
			PassParameters->ReflectionTileParameters = ReflectionTileParameters;

			FReflectionTemporalReprojectionCS::FPermutationDomain PermutationVector;
			auto ComputeShader = View.ShaderMap->GetShader<FReflectionTemporalReprojectionCS>(PermutationVector);

			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("Temporal Reprojection"),
				ComputeShader,
				PassParameters,
				ReflectionTileParameters.ResolveIndirectArgs,
				0);
		}
	}
	else
	{
		FReflectionPassthroughCopyCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FReflectionPassthroughCopyCS::FParameters>();
		PassParameters->RWSpecularIndirect = GraphBuilder.CreateUAV(FinalSpecularIndirect);
		PassParameters->View = View.ViewUniformBuffer;
		PassParameters->ResolvedReflections = ResolvedReflections;
		PassParameters->ReflectionTileParameters = ReflectionTileParameters;

		FReflectionPassthroughCopyCS::FPermutationDomain PermutationVector;
		auto ComputeShader = View.ShaderMap->GetShader<FReflectionPassthroughCopyCS>(PermutationVector);

		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("Passthrough"),
			ComputeShader,
			PassParameters,
			ReflectionTileParameters.ResolveIndirectArgs,
			0);
	}

	if (View.ViewState)
	{
		FReflectionTemporalState& ReflectionTemporalState = View.ViewState->Lumen.ReflectionState;
		ReflectionTemporalState.HistoryViewRect = View.ViewRect;
		ReflectionTemporalState.HistoryScreenPositionScaleBias = View.GetScreenPositionScaleBias(FSceneRenderTargets::Get().GetBufferSizeXY(), View.ViewRect);

		// Queue updating the view state's render target reference with the new values
		ConvertToExternalTexture(GraphBuilder, FinalSpecularIndirect, ReflectionTemporalState.SpecularIndirectHistoryRT);
	}
}

DECLARE_GPU_STAT(LumenReflections);

FRDGTextureRef FDeferredShadingSceneRenderer::RenderLumenReflections(
	FRDGBuilder& GraphBuilder, 
	const FViewInfo& View,
	const FSceneTextureParameters& SceneTextures,
	const FLumenMeshSDFGridParameters& MeshSDFGridParameters,
	FLumenReflectionCompositeParameters& OutCompositeParameters)
{
	OutCompositeParameters.MaxRoughnessToTrace = GLumenReflectionMaxRoughnessToTrace;
	OutCompositeParameters.InvRoughnessFadeLength = 1.0f / GLumenReflectionRoughnessFadeLength;

	if (!ShouldRenderLumenReflections(View))
	{
		return nullptr;
	}

	LLM_SCOPE_BYTAG(Lumen);
	RDG_EVENT_SCOPE(GraphBuilder, "LumenReflections");
	RDG_GPU_STAT_SCOPE(GraphBuilder, LumenReflections);

	FLumenReflectionTracingParameters ReflectionTracingParameters;
	FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get();

	ReflectionTracingParameters.ReflectionDownsampleFactor = FMath::Clamp(GLumenReflectionDownsampleFactor, 1, 4);
	ReflectionTracingParameters.ReflectionTracingViewSize = FIntPoint::DivideAndRoundUp(View.ViewRect.Size(), (int32)ReflectionTracingParameters.ReflectionDownsampleFactor);
	ReflectionTracingParameters.ReflectionTracingBufferSize = FIntPoint::DivideAndRoundUp(SceneContext.GetBufferSizeXY(), (int32)ReflectionTracingParameters.ReflectionDownsampleFactor);
	ReflectionTracingParameters.MaxRayIntensity = GLumenReflectionMaxRayIntensity;

	FRDGTextureDesc RayBufferDesc(FRDGTextureDesc::Create2D(ReflectionTracingParameters.ReflectionTracingBufferSize, PF_FloatRGBA, FClearValueBinding::Black, TexCreate_ShaderResource | TexCreate_UAV));
	ReflectionTracingParameters.RayBuffer = GraphBuilder.CreateTexture(RayBufferDesc, TEXT("ReflectionRayBuffer"));

	FRDGTextureDesc DownsampledDepthDesc(FRDGTextureDesc::Create2D(ReflectionTracingParameters.ReflectionTracingBufferSize, PF_R32_FLOAT, FClearValueBinding::Black, TexCreate_ShaderResource | TexCreate_UAV));
	ReflectionTracingParameters.DownsampledDepth = GraphBuilder.CreateTexture(DownsampledDepthDesc, TEXT("ReflectionDownsampledDepth"));

	FBlueNoise BlueNoise;
	InitializeBlueNoise(BlueNoise);
	ReflectionTracingParameters.BlueNoise = CreateUniformBufferImmediate(BlueNoise, EUniformBufferUsage::UniformBuffer_SingleDraw);

	FLumenReflectionTileParameters ReflectionTileParameters = ReflectionTileClassification(GraphBuilder, View, ReflectionTracingParameters);

	{
		FReflectionGenerateRaysCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FReflectionGenerateRaysCS::FParameters>();
		PassParameters->RWRayBuffer = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(ReflectionTracingParameters.RayBuffer));
		PassParameters->RWDownsampledDepth = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(ReflectionTracingParameters.DownsampledDepth));
		PassParameters->View = View.ViewUniformBuffer;
		PassParameters->MaxRoughnessToTrace = GLumenReflectionMaxRoughnessToTrace;
		PassParameters->GGXSamplingBias = GLumenReflectionGGXSamplingBias;
		PassParameters->SceneTexturesStruct = CreateSceneTextureUniformBuffer(GraphBuilder, View.FeatureLevel);
		PassParameters->ReflectionTracingParameters = ReflectionTracingParameters;
		PassParameters->ReflectionTileParameters = ReflectionTileParameters;

		auto ComputeShader = View.ShaderMap->GetShader<FReflectionGenerateRaysCS>(0);

		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("GenerateRaysCS"),
			ComputeShader,
			PassParameters,
			ReflectionTileParameters.TracingIndirectArgs,
			0);
	}

	FLumenCardTracingInputs TracingInputs(GraphBuilder, Scene, View);

	FRDGTextureDesc TraceRadianceDesc(FRDGTextureDesc::Create2D(ReflectionTracingParameters.ReflectionTracingBufferSize, PF_FloatRGB, FClearValueBinding::Black, TexCreate_ShaderResource | TexCreate_UAV));
	ReflectionTracingParameters.TraceRadiance = GraphBuilder.CreateTexture(TraceRadianceDesc, TEXT("ReflectionTraceRadiance"));
	ReflectionTracingParameters.RWTraceRadiance = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(ReflectionTracingParameters.TraceRadiance));

	FRDGTextureDesc TraceHitDesc(FRDGTextureDesc::Create2D(ReflectionTracingParameters.ReflectionTracingBufferSize, PF_R16F, FClearValueBinding::Black, TexCreate_ShaderResource | TexCreate_UAV));
	ReflectionTracingParameters.TraceHit = GraphBuilder.CreateTexture(TraceHitDesc, TEXT("ReflectionTraceHit"));
	ReflectionTracingParameters.RWTraceHit = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(ReflectionTracingParameters.TraceHit));

	const bool bScreenSpaceReflections = ScreenSpaceRayTracing::ShouldRenderScreenSpaceReflections(View);

	TraceReflections(
		GraphBuilder, 
		Scene,
		View, 
		bScreenSpaceReflections,
		GLumenReflectionTraceCards != 0,
		SceneTextures,
		TracingInputs,
		ReflectionTracingParameters,
		ReflectionTileParameters,
		MeshSDFGridParameters);
	
	FRDGTextureDesc SpecularIndirectDesc = FRDGTextureDesc::Create2D(SceneContext.GetBufferSizeXY(), PF_FloatRGBA, FClearValueBinding::Black, TexCreate_ShaderResource | TexCreate_UAV);
	FRDGTextureRef ResolvedSpecularIndirect = GraphBuilder.CreateTexture(SpecularIndirectDesc, TEXT("ResolvedSpecularIndirect"));

	{
		FReflectionResolveCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FReflectionResolveCS::FParameters>();
		PassParameters->RWSpecularIndirect = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(ResolvedSpecularIndirect));
		PassParameters->MaxRoughnessToTrace = GLumenReflectionMaxRoughnessToTrace;
		PassParameters->InvRoughnessFadeLength = 1.0f / GLumenReflectionRoughnessFadeLength;
		PassParameters->NumSpatialReconstructionSamples = GLumenReflectionScreenSpaceReconstructionNumSamples;
		PassParameters->SpatialReconstructionScreenWidth = GLumenReflectionScreenSpaceReconstructionScreenWidth;
		PassParameters->SpatialReconstructionRoughnessScale = GLumenReflectionScreenSpaceReconstructionRoughnessScale;
		PassParameters->ReflectionTracingParameters = ReflectionTracingParameters;
		PassParameters->View = View.ViewUniformBuffer;
		PassParameters->SceneTexturesStruct = CreateSceneTextureUniformBuffer(GraphBuilder, View.FeatureLevel);
		PassParameters->ReflectionTileParameters = ReflectionTileParameters;

		FReflectionResolveCS::FPermutationDomain PermutationVector;
		PermutationVector.Set< FReflectionResolveCS::FSpatialReconstruction >(GLumenReflectionScreenSpaceReconstruction != 0);
		auto ComputeShader = View.ShaderMap->GetShader<FReflectionResolveCS>(PermutationVector);

		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("ReflectionResolve"),
			ComputeShader,
			PassParameters,
			ReflectionTileParameters.ResolveIndirectArgs,
			0);
	}

	FRDGTextureRef SpecularIndirect = GraphBuilder.CreateTexture(SpecularIndirectDesc, TEXT("SpecularIndirect"));

	//@todo - only clear tiles not written to by history pass
	AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(FRDGTextureUAVDesc(SpecularIndirect)), FLinearColor(0.0f, 0.0f, 0.0f, 0.0f));

	UpdateHistoryReflections(
		GraphBuilder,
		View,
		SceneContext.GetBufferSizeXY(),
		ReflectionTileParameters,
		ResolvedSpecularIndirect,
		SpecularIndirect);

	return SpecularIndirect;
}

