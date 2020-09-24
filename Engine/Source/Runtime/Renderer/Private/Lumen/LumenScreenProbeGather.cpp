// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	LumenScreenProbeGather.cpp
=============================================================================*/

#include "LumenScreenProbeGather.h"
#include "RendererPrivate.h"
#include "ScenePrivate.h"
#include "SceneUtils.h"
#include "PipelineStateCache.h"
#include "ShaderParameterStruct.h"
#include "PixelShaderUtils.h"
#include "ReflectionEnvironment.h"
#include "DistanceFieldAmbientOcclusion.h"
#include "ScreenSpaceDenoise.h"

extern FLumenGatherCvarState GLumenGatherCvars;

int32 GLumenScreenProbeGather = 1;
FAutoConsoleVariableRef GVarLumenScreenProbeGather(
	TEXT("r.Lumen.ScreenProbeGather"),
	GLumenScreenProbeGather,
	TEXT("Whether to use the Screen Probe Final Gather"),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

int32 GLumenScreenProbeGatherAdaptiveScreenTileSampleResolution = 2;
FAutoConsoleVariableRef GVarLumenScreenProbeGatherAdaptiveScreenTileSampleResolution(
	TEXT("r.Lumen.ScreenProbeGather.AdaptiveScreenTileSampleResolution"),
	GLumenScreenProbeGatherAdaptiveScreenTileSampleResolution,
	TEXT("Resolution of adaptive screen probes to try placing on each screen tile."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

float GLumenScreenProbeGatherAdaptiveProbeAllocationFraction = .5f;
FAutoConsoleVariableRef GVarAdaptiveProbeAllocationFraction(
	TEXT("r.Lumen.ScreenProbeGather.AdaptiveProbeAllocationFraction"),
	GLumenScreenProbeGatherAdaptiveProbeAllocationFraction,
	TEXT("Fraction of uniform probes to allow for adaptive probe placement."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

int32 GLumenScreenProbeGatherReferenceMode = 0;
FAutoConsoleVariableRef GVarLumenScreenProbeGatherReferenceMode(
	TEXT("r.Lumen.ScreenProbeGather.ReferenceMode"),
	GLumenScreenProbeGatherReferenceMode,
	TEXT("When enabled, traces 1024 uniform rays per probe with no filtering, Importance Sampling or Radiance Caching."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

int32 GLumenScreenProbeTracingOctahedronResolution = 8;
FAutoConsoleVariableRef GVarLumenScreenProbeTracingOctahedronResolution(
	TEXT("r.Lumen.ScreenProbeGather.TracingOctahedronResolution"),
	GLumenScreenProbeTracingOctahedronResolution,
	TEXT("Resolution of the tracing octahedron.  Determines how many traces are done per probe."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

float GLumenScreenProbeGatherOctahedronResolutionScale = 1.0f;
FAutoConsoleVariableRef GVarLumenScreenProbeGatherOctahedronResolutionScale(
	TEXT("r.Lumen.ScreenProbeGather.GatherOctahedronResolutionScale"),
	GLumenScreenProbeGatherOctahedronResolutionScale,
	TEXT("Resolution that probe filtering and integration will happen at, as a scale of TracingOctahedronResolution"),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

int32 GLumenScreenProbeDownsampleFactor = 16;
FAutoConsoleVariableRef GVarLumenScreenProbeDownsampleFactor(
	TEXT("r.Lumen.ScreenProbeGather.DownsampleFactor"),
	GLumenScreenProbeDownsampleFactor,
	TEXT("Pixel size of the screen tile that a screen probe will be placed on."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

int32 GLumenOctahedralSolidAngleTextureSize = 16;
FAutoConsoleVariableRef CVarLumenScreenProbeOctahedralSolidAngleTextureSize(
	TEXT("r.Lumen.ScreenProbeGather.OctahedralSolidAngleTextureSize"),
	GLumenOctahedralSolidAngleTextureSize,
	TEXT("Resolution of the lookup texture to compute Octahedral Solid Angle."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

float GLumenScreenProbeFullResolutionJitterWidth = 1;
FAutoConsoleVariableRef GVarLumenScreenProbeFullResolutionJitterWidth(
	TEXT("r.Lumen.ScreenProbeGather.FullResolutionJitterWidth"),
	GLumenScreenProbeFullResolutionJitterWidth,
	TEXT("Size of the full resolution jitter applied to Screen Probe upsampling, as a fraction of a screen tile.  A width of 1 results in jittering by DownsampleFactor number of pixels."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

int32 GLumenScreenProbeDiffuseIntegralMethod = 0;
FAutoConsoleVariableRef CVarLumenScreenProbeDiffuseIntegralMethod(
	TEXT("r.Lumen.ScreenProbeGather.DiffuseIntegralMethod"),
	GLumenScreenProbeDiffuseIntegralMethod,
	TEXT("Spherical Harmonic = 0, Importance Sample BRDF = 1, Numerical Integral Reference = 2"),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

int32 GLumenScreenProbeTemporalFilter = 1;
FAutoConsoleVariableRef CVarLumenScreenProbeTemporalFilter(
	TEXT("r.Lumen.ScreenProbeGather.Temporal"),
	GLumenScreenProbeTemporalFilter,
	TEXT("Whether to use a temporal filter"),
	ECVF_Scalability | ECVF_RenderThreadSafe
	);

int32 GLumenScreenProbeClearHistoryEveryFrame = 0;
FAutoConsoleVariableRef CVarLumenScreenProbeClearHistoryEveryFrame(
	TEXT("r.Lumen.ScreenProbeGather.Temporal.ClearHistoryEveryFrame"),
	GLumenScreenProbeClearHistoryEveryFrame,
	TEXT("Whether to clear the history every frame for debugging"),
	ECVF_RenderThreadSafe
	);

float GLumenScreenProbeHistoryWeight = .9f;
FAutoConsoleVariableRef CVarLumenScreenProbeHistoryWeight(
	TEXT("r.Lumen.ScreenProbeGather.Temporal.HistoryWeight"),
	GLumenScreenProbeHistoryWeight,
	TEXT("Weight of the history lighting.  Values closer to 1 exponentially decrease noise but also response time to lighting changes."),
	ECVF_RenderThreadSafe
	);

float GLumenScreenProbeGradientHistoryWeight = .9f;
FAutoConsoleVariableRef CVarLumenScreenProbeGradientHistoryWeight(
	TEXT("r.Lumen.ScreenProbeGather.Temporal.GradientHistoryWeight"),
	GLumenScreenProbeGradientHistoryWeight,
	TEXT("Experimental"),
	ECVF_RenderThreadSafe
	);

float GLumenScreenProbeGradientSpeedupConvergenceThreshold = 1000;
FAutoConsoleVariableRef CVarLumenScreenProbeGradientSpeedupConvergenceThreshold(
	TEXT("r.Lumen.ScreenProbeGather.Temporal.GradientSpeedupConvergenceThreshold"),
	GLumenScreenProbeGradientSpeedupConvergenceThreshold,
	TEXT(""),
	ECVF_RenderThreadSafe
	);

int32 GLumenScreenProbeUseHistoryNeighborhoodClamp = 0;
FAutoConsoleVariableRef CVarLumenScreenProbeUseHistoryNeighborhoodClamp(
	TEXT("r.Lumen.ScreenProbeGather.Temporal.NeighborhoodClamp"),
	GLumenScreenProbeUseHistoryNeighborhoodClamp,
	TEXT("Whether to use a neighborhood clamp temporal filter instead of depth rejection.  Experimental."),
	ECVF_Scalability | ECVF_RenderThreadSafe
	);

float GLumenScreenProbeHistoryDistanceThreshold = 30;
FAutoConsoleVariableRef CVarLumenScreenProbeHistoryDistanceThreshold(
	TEXT("r.Lumen.ScreenProbeGather.Temporal.DistanceThreshold"),
	GLumenScreenProbeHistoryDistanceThreshold,
	TEXT("World space distance threshold needed to discard last frame's lighting results.  Lower values reduce ghosting from characters when near a wall but increase flickering artifacts."),
	ECVF_RenderThreadSafe
	);

float GLumenScreenProbeHistoryConvergenceWeight = .8f;
FAutoConsoleVariableRef CVarLumenScreenProbeHistoryConvergenceWeightt(
	TEXT("r.Lumen.ScreenProbeGather.Temporal.HistoryConvergenceWeight"),
	GLumenScreenProbeHistoryConvergenceWeight,
	TEXT(""),
	ECVF_RenderThreadSafe
	);

int32 GLumenScreenProbeSpatialFilter = 1;
FAutoConsoleVariableRef GVarLumenScreenProbeFilter(
	TEXT("r.Lumen.ScreenProbeGather.SpatialFilterProbes"),
	GLumenScreenProbeSpatialFilter,
	TEXT("Whether to spatially filter probe traces to reduce noise."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

int32 GLumenScreenSpaceBentNormal = 1;
FAutoConsoleVariableRef GVarLumenScreenSpaceBentNormal(
	TEXT("r.Lumen.ScreenProbeGather.ScreenSpaceBentNormal"),
	GLumenScreenSpaceBentNormal,
	TEXT("Whether to compute screen space directional occlusion to add high frequency occlusion (contact shadows) which Screen Probes lack due to downsampling."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

namespace LumenScreenProbeGather 
{
	int32 GetTracingOctahedronResolution()
	{
		return GLumenScreenProbeGatherReferenceMode ? 32 : GLumenScreenProbeTracingOctahedronResolution;
	}

	int32 GetGatherOctahedronResolution()
	{
		if (GLumenScreenProbeGatherReferenceMode)
		{
			return 8;
		}

		if (GLumenScreenProbeGatherOctahedronResolutionScale >= 1.0f)
		{
			const int32 Multiplier = FMath::RoundToInt(GLumenScreenProbeGatherOctahedronResolutionScale);
			return GLumenScreenProbeTracingOctahedronResolution * Multiplier;
		}
		else
		{
			const int32 Divisor = FMath::RoundToInt(1.0f / FMath::Max(GLumenScreenProbeGatherOctahedronResolutionScale, .1f));
			return GLumenScreenProbeTracingOctahedronResolution / Divisor;
		}
	}
	
	int32 GetScreenDownsampleFactor()
	{
		return GLumenScreenProbeGatherReferenceMode ? 16 : GLumenScreenProbeDownsampleFactor;
	}

	bool UseScreenSpaceBentNormal()
	{
		return GLumenScreenProbeGatherReferenceMode ? false : GLumenScreenSpaceBentNormal != 0;
	}

	bool UseProbeSpatialFilter()
	{
		return GLumenScreenProbeGatherReferenceMode ? false : GLumenScreenProbeSpatialFilter != 0;
	}

	bool UseRadianceCache(const FViewInfo& View)
	{
		return GLumenScreenProbeGatherReferenceMode ? false : LumenRadianceCache::IsEnabled(View);
	}

	int32 GetDiffuseIntegralMethod()
	{
		return GLumenScreenProbeGatherReferenceMode ? 2 : GLumenScreenProbeDiffuseIntegralMethod;
	}
}

class FOctahedralSolidAngleCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FOctahedralSolidAngleCS)
	SHADER_USE_PARAMETER_STRUCT(FOctahedralSolidAngleCS, FGlobalShader)

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, RWOctahedralSolidAngleTexture)
		SHADER_PARAMETER(uint32, OctahedralSolidAngleTextureSize)
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

IMPLEMENT_GLOBAL_SHADER(FOctahedralSolidAngleCS, "/Engine/Private/Lumen/LumenScreenProbeGather.usf", "OctahedralSolidAngleCS", SF_Compute);

FRDGTextureRef InitializeOctahedralSolidAngleTexture(
	FRDGBuilder& GraphBuilder, 
	FGlobalShaderMap* ShaderMap,
	FScreenProbeGatherTemporalState& ScreenProbeGatherState)
{
	if (ScreenProbeGatherState.OctahedralSolidAngleTextureRT)
	{
		return GraphBuilder.RegisterExternalTexture(ScreenProbeGatherState.OctahedralSolidAngleTextureRT, TEXT("OctahedralSolidAngleTexture"));
	}
	else
	{
		FRDGTextureDesc OctahedralSolidAngleTextureDesc(FRDGTextureDesc::Create2D(GLumenOctahedralSolidAngleTextureSize, PF_R16F, FClearValueBinding::Black, TexCreate_ShaderResource | TexCreate_UAV));
		FRDGTextureRef OctahedralSolidAngleTexture = GraphBuilder.CreateTexture(OctahedralSolidAngleTextureDesc, TEXT("OctahedralSolidAngleTexture"));
	
		{
			FOctahedralSolidAngleCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FOctahedralSolidAngleCS::FParameters>();
			PassParameters->RWOctahedralSolidAngleTexture = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(OctahedralSolidAngleTexture));
			PassParameters->OctahedralSolidAngleTextureSize = GLumenOctahedralSolidAngleTextureSize;

			auto ComputeShader = ShaderMap->GetShader<FOctahedralSolidAngleCS>(0);

			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("OctahedralSolidAngleCS"),
				ComputeShader,
				PassParameters,
				FComputeShaderUtils::GetGroupCount(FIntPoint(GLumenOctahedralSolidAngleTextureSize, GLumenOctahedralSolidAngleTextureSize), FOctahedralSolidAngleCS::GetGroupSize()));
		}

		GraphBuilder.QueueTextureExtraction(OctahedralSolidAngleTexture, &ScreenProbeGatherState.OctahedralSolidAngleTextureRT);
		return OctahedralSolidAngleTexture;
	}
}


class FScreenProbeDownsampleDepthUniformCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FScreenProbeDownsampleDepthUniformCS)
	SHADER_USE_PARAMETER_STRUCT(FScreenProbeDownsampleDepthUniformCS, FGlobalShader)

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, RWDownsampledDepth)
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FSceneTextureUniformParameters, SceneTexturesStruct)
		SHADER_PARAMETER_STRUCT_INCLUDE(FScreenProbeParameters, ScreenProbeParameters)
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

IMPLEMENT_GLOBAL_SHADER(FScreenProbeDownsampleDepthUniformCS, "/Engine/Private/Lumen/LumenScreenProbeGather.usf", "ScreenProbeDownsampleDepthUniformCS", SF_Compute);


class FScreenProbeAdaptivePlacementCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FScreenProbeAdaptivePlacementCS)
	SHADER_USE_PARAMETER_STRUCT(FScreenProbeAdaptivePlacementCS, FGlobalShader)

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWNumAdaptiveScreenProbes)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWAdaptiveScreenProbeData)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<uint>, RWScreenTileAdaptiveProbeHeader)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<uint>, RWScreenTileAdaptiveProbeIndices)
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FSceneTextureUniformParameters, SceneTexturesStruct)
		SHADER_PARAMETER_STRUCT_INCLUDE(FScreenProbeParameters, ScreenProbeParameters)
		SHADER_PARAMETER(uint32, PlacementIteration)
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

IMPLEMENT_GLOBAL_SHADER(FScreenProbeAdaptivePlacementCS, "/Engine/Private/Lumen/LumenScreenProbeGather.usf", "ScreenProbeAdaptivePlacementCS", SF_Compute);


class FScreenProbeWriteDepthForAdaptiveProbesCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FScreenProbeWriteDepthForAdaptiveProbesCS)
	SHADER_USE_PARAMETER_STRUCT(FScreenProbeWriteDepthForAdaptiveProbesCS, FGlobalShader)

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, RWDownsampledDepth)
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FSceneTextureUniformParameters, SceneTexturesStruct)
		SHADER_PARAMETER_STRUCT_INCLUDE(FScreenProbeParameters, ScreenProbeParameters)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportLumenGI(Parameters.Platform);
	}

	static int32 GetGroupSize() 
	{
		return 64;
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE"), GetGroupSize());
	}
};

IMPLEMENT_GLOBAL_SHADER(FScreenProbeWriteDepthForAdaptiveProbesCS, "/Engine/Private/Lumen/LumenScreenProbeGather.usf", "ScreenProbeWriteDepthForAdaptiveProbesCS", SF_Compute);


class FSetupAdaptiveProbeIndirectArgsCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FSetupAdaptiveProbeIndirectArgsCS)
	SHADER_USE_PARAMETER_STRUCT(FSetupAdaptiveProbeIndirectArgsCS, FGlobalShader)

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWScreenProbeIndirectArgs)
		SHADER_PARAMETER_STRUCT_INCLUDE(FScreenProbeParameters, ScreenProbeParameters)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportLumenGI(Parameters.Platform);
	}
};

IMPLEMENT_GLOBAL_SHADER(FSetupAdaptiveProbeIndirectArgsCS, "/Engine/Private/Lumen/LumenScreenProbeGather.usf", "SetupAdaptiveProbeIndirectArgsCS", SF_Compute);


class FScreenProbeIndirectCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FScreenProbeIndirectCS)
	SHADER_USE_PARAMETER_STRUCT(FScreenProbeIndirectCS, FGlobalShader)

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, RWDiffuseIndirect)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float3>, RWRoughSpecularIndirect)
		SHADER_PARAMETER_STRUCT_INCLUDE(FScreenProbeParameters, ScreenProbeParameters)
		SHADER_PARAMETER_STRUCT_INCLUDE(FScreenProbeGatherParameters, GatherParameters)
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FSceneTextureUniformParameters, SceneTexturesStruct)
		SHADER_PARAMETER_STRUCT_INCLUDE(FScreenSpaceBentNormalParameters, ScreenSpaceBentNormalParameters)
		SHADER_PARAMETER(float, FullResolutionJitterWidth)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportLumenGI(Parameters.Platform);
	}

	static int32 GetGroupSize() 
	{
		return 8;
	}

	class FDiffuseIntegralMethod : SHADER_PERMUTATION_INT("DIFFUSE_INTEGRAL_METHOD", 3);
	using FPermutationDomain = TShaderPermutationDomain<FDiffuseIntegralMethod>;

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE"), GetGroupSize());
	}
};

IMPLEMENT_GLOBAL_SHADER(FScreenProbeIndirectCS, "/Engine/Private/Lumen/LumenScreenProbeGather.usf", "ScreenProbeIndirectCS", SF_Compute);


class FScreenProbeTemporalReprojectionDepthRejectionPS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FScreenProbeTemporalReprojectionDepthRejectionPS)
	SHADER_USE_PARAMETER_STRUCT(FScreenProbeTemporalReprojectionDepthRejectionPS, FGlobalShader)

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		RENDER_TARGET_BINDING_SLOTS()
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FSceneTextureUniformParameters, SceneTexturesStruct)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, DiffuseIndirectHistory)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, RoughSpecularIndirectHistory)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, DiffuseIndirectDepthHistory)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, HistoryConvergence)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, LightingGradientHistory)
		SHADER_PARAMETER(float,HistoryDistanceThreshold)
		SHADER_PARAMETER(float,GradientHistoryWeight)
		SHADER_PARAMETER(float,GradientSpeedupConvergenceThreshold)
		SHADER_PARAMETER(float,HistoryWeight)
		SHADER_PARAMETER(float,HistoryConvergenceWeight)
		SHADER_PARAMETER(float,PrevInvPreExposure)
		SHADER_PARAMETER(FVector2D,InvDiffuseIndirectBufferSize)
		SHADER_PARAMETER(FVector4,HistoryScreenPositionScaleBias)
		SHADER_PARAMETER(FVector4,HistoryUVMinMax)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, VelocityTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, VelocityTextureSampler)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, DiffuseIndirect)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, RoughSpecularIndirect)
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

IMPLEMENT_GLOBAL_SHADER(FScreenProbeTemporalReprojectionDepthRejectionPS, "/Engine/Private/Lumen/LumenScreenProbeGather.usf", "ScreenProbeTemporalReprojectionDepthRejectionPS", SF_Pixel);


class FCopyDepthPS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FCopyDepthPS)
	SHADER_USE_PARAMETER_STRUCT(FCopyDepthPS, FGlobalShader)

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		RENDER_TARGET_BINDING_SLOTS()
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FSceneTextureUniformParameters, SceneTexturesStruct)
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

IMPLEMENT_GLOBAL_SHADER(FCopyDepthPS, "/Engine/Private/Lumen/LumenScreenProbeGather.usf", "CopyDepthPS", SF_Pixel);


class FGenerateCompressedGBuffer : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FGenerateCompressedGBuffer)
	SHADER_USE_PARAMETER_STRUCT(FGenerateCompressedGBuffer, FGlobalShader)

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, RWCompressedDepthBufferOutput)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<uint>, RWCompressedShadingModelOutput)
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER_STRUCT_INCLUDE(FSceneTextureParameters, SceneTextures)
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

IMPLEMENT_GLOBAL_SHADER(FGenerateCompressedGBuffer, "/Engine/Private/Lumen/LumenScreenProbeGather.usf", "GenerateCompressedGBuffer", SF_Compute);

void UpdateHistoryScreenProbeGather(
	FRDGBuilder& GraphBuilder,
	const FViewInfo& View, 
	FIntPoint BufferSize,
	FRDGTextureRef& DiffuseIndirect,
	FRDGTextureRef& RoughSpecularIndirect)
{
	LLM_SCOPE(ELLMTag::Lumen);
	
	if (View.ViewState)
	{
		FScreenProbeGatherTemporalState& ScreenProbeGatherState = View.ViewState->Lumen.ScreenProbeGatherState;
		TRefCountPtr<IPooledRenderTarget>* DiffuseIndirectHistoryState0 = &ScreenProbeGatherState.DiffuseIndirectHistoryRT[0];
		TRefCountPtr<IPooledRenderTarget>* RoughSpecularIndirectHistoryState = &ScreenProbeGatherState.RoughSpecularIndirectHistoryRT;
		FIntRect* DiffuseIndirectHistoryViewRect = &ScreenProbeGatherState.DiffuseIndirectHistoryViewRect;
		FVector4* DiffuseIndirectHistoryScreenPositionScaleBias = &ScreenProbeGatherState.DiffuseIndirectHistoryScreenPositionScaleBias;
		TRefCountPtr<IPooledRenderTarget>* DepthHistoryState = &ScreenProbeGatherState.DownsampledDepthHistoryRT;
		TRefCountPtr<IPooledRenderTarget>* HistoryConvergenceState = &ScreenProbeGatherState.HistoryConvergenceStateRT;

		FSceneTextureParameters SceneTextures = GetSceneTextureParameters(GraphBuilder);

		// Fallback to a black texture if no velocity.
		if (!SceneTextures.GBufferVelocityTexture)
		{
			SceneTextures.GBufferVelocityTexture = GSystemTextures.GetBlackDummy(GraphBuilder);
		}

		const FIntRect NewHistoryViewRect = View.ViewRect;
		FRDGTextureRef NewDepthHistory = GraphBuilder.CreateTexture(SceneTextures.SceneDepthTexture->Desc, TEXT("DepthHistory"));

		if (*DiffuseIndirectHistoryState0
			&& !View.bCameraCut 
			&& !View.bPrevTransformsReset
			&& !GLumenScreenProbeClearHistoryEveryFrame
			// If the scene render targets reallocate, toss the history so we don't read uninitialized data
			&& (*DiffuseIndirectHistoryState0)->GetDesc().Extent == BufferSize
			&& ScreenProbeGatherState.LumenGatherCvars == GLumenGatherCvars)
		{
			EPixelFormat HistoryFormat = PF_FloatRGBA;
			FRDGTextureDesc DiffuseIndirectDesc = FRDGTextureDesc::Create2D(BufferSize, PF_FloatRGBA, FClearValueBinding::Black, TexCreate_ShaderResource | TexCreate_RenderTargetable);
			FRDGTextureRef NewDiffuseIndirect = GraphBuilder.CreateTexture(DiffuseIndirectDesc, TEXT("DiffuseIndirect"));

			FRDGTextureRef OldDiffuseIndirectHistory = GraphBuilder.RegisterExternalTexture(ScreenProbeGatherState.DiffuseIndirectHistoryRT[0]);

			FRDGTextureDesc RoughSpecularIndirectDesc = FRDGTextureDesc::Create2D(BufferSize, PF_FloatRGB, FClearValueBinding::Black, TexCreate_ShaderResource | TexCreate_RenderTargetable | TexCreate_UAV);
			FRDGTextureRef NewRoughSpecularIndirect = GraphBuilder.CreateTexture(RoughSpecularIndirectDesc, TEXT("RoughSpecularIndirect"));

			FRDGTextureDesc HistoryConvergenceDesc(FRDGTextureDesc::Create2D(BufferSize, PF_R8G8, FClearValueBinding::Black, TexCreate_ShaderResource | TexCreate_RenderTargetable));
			FRDGTextureRef NewHistoryConvergence = GraphBuilder.CreateTexture(HistoryConvergenceDesc, TEXT("HistoryConvergence"));

			{
				FRDGTextureRef OldRoughSpecularIndirectHistory = GraphBuilder.RegisterExternalTexture(*RoughSpecularIndirectHistoryState);
				FRDGTextureRef OldDepthHistory = GraphBuilder.RegisterExternalTexture(*DepthHistoryState);
				FRDGTextureRef OldHistoryConvergence = GraphBuilder.RegisterExternalTexture(*HistoryConvergenceState);

				FScreenProbeTemporalReprojectionDepthRejectionPS::FPermutationDomain PermutationVector;
				auto PixelShader = View.ShaderMap->GetShader<FScreenProbeTemporalReprojectionDepthRejectionPS>(PermutationVector);

				FScreenProbeTemporalReprojectionDepthRejectionPS::FParameters* PassParameters = GraphBuilder.AllocParameters<FScreenProbeTemporalReprojectionDepthRejectionPS::FParameters>();
				PassParameters->RenderTargets[0] = FRenderTargetBinding(NewDiffuseIndirect, ERenderTargetLoadAction::ENoAction);
				PassParameters->RenderTargets[1] = FRenderTargetBinding(NewRoughSpecularIndirect, ERenderTargetLoadAction::ENoAction);
				PassParameters->RenderTargets[2] = FRenderTargetBinding(NewHistoryConvergence, ERenderTargetLoadAction::ENoAction);

				PassParameters->RenderTargets.DepthStencil = FDepthStencilBinding(NewDepthHistory, ERenderTargetLoadAction::ENoAction, FExclusiveDepthStencil::DepthWrite_StencilNop);
				PassParameters->View = View.ViewUniformBuffer;
				PassParameters->SceneTexturesStruct = CreateSceneTextureUniformBuffer(GraphBuilder, View.FeatureLevel);
				PassParameters->DiffuseIndirectHistory = OldDiffuseIndirectHistory;
				PassParameters->RoughSpecularIndirectHistory = OldRoughSpecularIndirectHistory;
				PassParameters->DiffuseIndirectDepthHistory = OldDepthHistory;
				PassParameters->HistoryConvergence  = OldHistoryConvergence;
				PassParameters->HistoryDistanceThreshold = GLumenScreenProbeHistoryDistanceThreshold;
				PassParameters->HistoryWeight = GLumenScreenProbeHistoryWeight;
				PassParameters->GradientHistoryWeight = GLumenScreenProbeGradientHistoryWeight;
				PassParameters->GradientSpeedupConvergenceThreshold = GLumenScreenProbeGradientSpeedupConvergenceThreshold;
				PassParameters->HistoryConvergenceWeight = GLumenScreenProbeHistoryConvergenceWeight;
				PassParameters->PrevInvPreExposure = 1.0f / View.PrevViewInfo.SceneColorPreExposure;
				const FVector2D InvBufferSize(1.0f / BufferSize.X, 1.0f / BufferSize.Y);
				PassParameters->InvDiffuseIndirectBufferSize = InvBufferSize;
				PassParameters->HistoryScreenPositionScaleBias = *DiffuseIndirectHistoryScreenPositionScaleBias;

				// Pull in the max UV to exclude the region which will read outside the viewport due to bilinear filtering
				PassParameters->HistoryUVMinMax = FVector4(
					(DiffuseIndirectHistoryViewRect->Min.X + 0.5f) * InvBufferSize.X,
					(DiffuseIndirectHistoryViewRect->Min.Y + 0.5f) * InvBufferSize.Y,
					(DiffuseIndirectHistoryViewRect->Max.X - 0.5f) * InvBufferSize.X,
					(DiffuseIndirectHistoryViewRect->Max.Y - 0.5f) * InvBufferSize.Y);

				PassParameters->VelocityTexture = SceneTextures.GBufferVelocityTexture;
				PassParameters->VelocityTextureSampler = TStaticSamplerState<SF_Bilinear>::GetRHI();
				PassParameters->DiffuseIndirect = DiffuseIndirect;
				PassParameters->RoughSpecularIndirect = RoughSpecularIndirect;

				FPixelShaderUtils::AddFullscreenPass(
					GraphBuilder,
					View.ShaderMap,
					RDG_EVENT_NAME("UpdateHistory"),
					PixelShader,
					PassParameters,
					NewHistoryViewRect,
					nullptr,
					nullptr,
					TStaticDepthStencilState<true, CF_Always>::GetRHI());

				// Queue updating the view state's render target reference with the new history
				GraphBuilder.QueueTextureExtraction(NewDiffuseIndirect, &ScreenProbeGatherState.DiffuseIndirectHistoryRT[0]);
				GraphBuilder.QueueTextureExtraction(NewRoughSpecularIndirect, RoughSpecularIndirectHistoryState);
				GraphBuilder.QueueTextureExtraction(NewDepthHistory, DepthHistoryState);
				GraphBuilder.QueueTextureExtraction(NewHistoryConvergence, HistoryConvergenceState);
			}

			RoughSpecularIndirect = NewRoughSpecularIndirect;
			DiffuseIndirect = NewDiffuseIndirect;
		}
		else
		{
			// Tossed the history for one frame, seed next frame's history with this frame's output

			{
				auto PixelShader = View.ShaderMap->GetShader<FCopyDepthPS>();

				FCopyDepthPS::FParameters* PassParameters = GraphBuilder.AllocParameters<FCopyDepthPS::FParameters>();
				PassParameters->RenderTargets.DepthStencil = FDepthStencilBinding(NewDepthHistory, ERenderTargetLoadAction::ENoAction, FExclusiveDepthStencil::DepthWrite_StencilNop);
				PassParameters->View = View.ViewUniformBuffer;
				PassParameters->SceneTexturesStruct = CreateSceneTextureUniformBuffer(GraphBuilder, View.FeatureLevel);
	
				FPixelShaderUtils::AddFullscreenPass(
					GraphBuilder,
					View.ShaderMap,
					RDG_EVENT_NAME("CopyDepth"),
					PixelShader,
					PassParameters,
					NewHistoryViewRect,
					nullptr,
					nullptr,
					TStaticDepthStencilState<true, CF_Always>::GetRHI());
			}

			// Queue updating the view state's render target reference with the new values
			GraphBuilder.QueueTextureExtraction(DiffuseIndirect, &ScreenProbeGatherState.DiffuseIndirectHistoryRT[0]);
			GraphBuilder.QueueTextureExtraction(RoughSpecularIndirect, RoughSpecularIndirectHistoryState);
			GraphBuilder.QueueTextureExtraction(NewDepthHistory, DepthHistoryState);
			*HistoryConvergenceState = GSystemTextures.BlackDummy;
		}

		*DiffuseIndirectHistoryViewRect = NewHistoryViewRect;
		*DiffuseIndirectHistoryScreenPositionScaleBias = View.GetScreenPositionScaleBias(FSceneRenderTargets::Get_FrameConstantsOnly().GetBufferSizeXY(), View.ViewRect);
		ScreenProbeGatherState.LumenGatherCvars = GLumenGatherCvars;
	}
	else
	{
		// Temporal reprojection is disabled or there is no view state - pass through
	}
}

DECLARE_GPU_STAT(LumenScreenProbeGather);

FSSDSignalTextures FDeferredShadingSceneRenderer::RenderLumenScreenProbeGather(
	FRDGBuilder& GraphBuilder,
	const FSceneTextureParameters& SceneTextures,
	const ScreenSpaceRayTracing::FPrevSceneColorMip& PrevSceneColorMip,
	const FViewInfo& View,
	FPreviousViewInfo* PreviousViewInfos,
	bool bSSGI,
	bool& bLumenUseDenoiserComposite,
	FLumenMeshSDFGridParameters& MeshSDFGridParameters)
{
	LLM_SCOPE(ELLMTag::Lumen);
	RDG_EVENT_SCOPE(GraphBuilder, "LumenScreenProbeGather");
	RDG_GPU_STAT_SCOPE(GraphBuilder, LumenScreenProbeGather);

	check(ShouldRenderLumenDiffuseGI(View));
	FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(GraphBuilder.RHICmdList);

	if (!GLumenScreenProbeGather)
	{
		FSSDSignalTextures ScreenSpaceDenoiserInputs;
		ScreenSpaceDenoiserInputs.Textures[0] = GraphBuilder.RegisterExternalTexture(GSystemTextures.BlackDummy);
		FRDGTextureDesc RoughSpecularIndirectDesc = FRDGTextureDesc::Create2D(SceneContext.GetBufferSizeXY(), PF_FloatRGB, FClearValueBinding::Black, TexCreate_ShaderResource | TexCreate_UAV);
		ScreenSpaceDenoiserInputs.Textures[1] = GraphBuilder.CreateTexture(RoughSpecularIndirectDesc, TEXT("RoughSpecularIndirect"));
		AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(FRDGTextureUAVDesc(ScreenSpaceDenoiserInputs.Textures[1])), FLinearColor::Black);
		bLumenUseDenoiserComposite = false;
		return ScreenSpaceDenoiserInputs;
	}

	FScreenProbeParameters ScreenProbeParameters;
	ScreenProbeParameters.ScreenProbeTracingOctahedronResolution = LumenScreenProbeGather::GetTracingOctahedronResolution();
	ScreenProbeParameters.ScreenProbeGatherOctahedronResolution = LumenScreenProbeGather::GetGatherOctahedronResolution();
	ScreenProbeParameters.ScreenProbeGatherOctahedronResolutionWithBorder = LumenScreenProbeGather::GetGatherOctahedronResolution() + 2 * (1 << (GLumenScreenProbeGatherNumMips - 1));
	ScreenProbeParameters.ScreenProbeDownsampleFactor = LumenScreenProbeGather::GetScreenDownsampleFactor();

	ScreenProbeParameters.ScreenProbeViewSize = FIntPoint::DivideAndRoundUp(View.ViewRect.Size(), (int32)ScreenProbeParameters.ScreenProbeDownsampleFactor);
	ScreenProbeParameters.ScreenProbeAtlasViewSize = ScreenProbeParameters.ScreenProbeViewSize;
	ScreenProbeParameters.ScreenProbeAtlasViewSize.Y += FMath::TruncToInt(ScreenProbeParameters.ScreenProbeViewSize.Y * GLumenScreenProbeGatherAdaptiveProbeAllocationFraction);

	ScreenProbeParameters.ScreenProbeAtlasBufferSize = FIntPoint::DivideAndRoundUp(SceneContext.GetBufferSizeXY(), (int32)ScreenProbeParameters.ScreenProbeDownsampleFactor);
	ScreenProbeParameters.ScreenProbeAtlasBufferSize.Y += FMath::TruncToInt(ScreenProbeParameters.ScreenProbeAtlasBufferSize.Y * GLumenScreenProbeGatherAdaptiveProbeAllocationFraction);

	ScreenProbeParameters.ScreenProbeTraceBufferSize = ScreenProbeParameters.ScreenProbeAtlasBufferSize * ScreenProbeParameters.ScreenProbeTracingOctahedronResolution;
	ScreenProbeParameters.ScreenProbeGatherBufferSize = ScreenProbeParameters.ScreenProbeAtlasBufferSize * ScreenProbeParameters.ScreenProbeGatherOctahedronResolution;
	ScreenProbeParameters.ScreenProbeGatherMaxMip = GLumenScreenProbeGatherNumMips - 1;
	ScreenProbeParameters.AdaptiveScreenTileSampleResolution = GLumenScreenProbeGatherAdaptiveScreenTileSampleResolution;
	ScreenProbeParameters.NumUniformScreenProbes = ScreenProbeParameters.ScreenProbeViewSize.X * ScreenProbeParameters.ScreenProbeViewSize.Y;
	ScreenProbeParameters.MaxNumAdaptiveProbes = FMath::TruncToInt(ScreenProbeParameters.NumUniformScreenProbes * GLumenScreenProbeGatherAdaptiveProbeAllocationFraction);

	FRDGTextureDesc DownsampledDepthDesc(FRDGTextureDesc::Create2D(ScreenProbeParameters.ScreenProbeAtlasBufferSize, PF_R32_FLOAT, FClearValueBinding::Black, TexCreate_ShaderResource | TexCreate_UAV));
	ScreenProbeParameters.DownsampledDepth = GraphBuilder.CreateTexture(DownsampledDepthDesc, TEXT("DownsampledDepth"));

	FBlueNoise BlueNoise;
	InitializeBlueNoise(BlueNoise);
	ScreenProbeParameters.BlueNoise = CreateUniformBufferImmediate(BlueNoise, EUniformBufferUsage::UniformBuffer_SingleDraw);

	ScreenProbeParameters.OctahedralSolidAngleParameters.InvOctahedralSolidAngleTextureResolutionSq = 1.0f / (GLumenOctahedralSolidAngleTextureSize * GLumenOctahedralSolidAngleTextureSize);
	ScreenProbeParameters.OctahedralSolidAngleParameters.OctahedralSolidAngleTexture = InitializeOctahedralSolidAngleTexture(GraphBuilder, View.ShaderMap, View.ViewState->Lumen.ScreenProbeGatherState);

	{
		FScreenProbeDownsampleDepthUniformCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FScreenProbeDownsampleDepthUniformCS::FParameters>();
		PassParameters->RWDownsampledDepth = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(ScreenProbeParameters.DownsampledDepth));
		PassParameters->View = View.ViewUniformBuffer;
		PassParameters->SceneTexturesStruct = CreateSceneTextureUniformBuffer(GraphBuilder, View.FeatureLevel);
		PassParameters->ScreenProbeParameters = ScreenProbeParameters;

		auto ComputeShader = View.ShaderMap->GetShader<FScreenProbeDownsampleDepthUniformCS>(0);

		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("DownsampleDepthUniform"),
			ComputeShader,
			PassParameters,
			FComputeShaderUtils::GetGroupCount(ScreenProbeParameters.ScreenProbeViewSize, FScreenProbeDownsampleDepthUniformCS::GetGroupSize()));
	}

	FRDGBufferRef NumAdaptiveScreenProbes = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateBufferDesc(sizeof(uint32), 1), TEXT("NumAdaptiveScreenProbes"));
	FRDGBufferRef AdaptiveScreenProbeData = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateBufferDesc(sizeof(uint32), FMath::Max<uint32>(ScreenProbeParameters.MaxNumAdaptiveProbes, 1)), TEXT("AdaptiveScreenProbeData"));

	ScreenProbeParameters.NumAdaptiveScreenProbes = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(NumAdaptiveScreenProbes, PF_R32_UINT));
	ScreenProbeParameters.AdaptiveScreenProbeData = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(AdaptiveScreenProbeData, PF_R32_UINT));

	const int32 NumScreenTileSubsamples = ScreenProbeParameters.AdaptiveScreenTileSampleResolution * ScreenProbeParameters.AdaptiveScreenTileSampleResolution;
	const FIntPoint ScreenProbeViewportBufferSize = FIntPoint::DivideAndRoundUp(SceneContext.GetBufferSizeXY(), (int32)ScreenProbeParameters.ScreenProbeDownsampleFactor);
	FRDGTextureDesc ScreenTileAdaptiveProbeHeaderDesc(FRDGTextureDesc::Create2D(ScreenProbeViewportBufferSize, PF_R32_UINT, FClearValueBinding::Black, TexCreate_ShaderResource | TexCreate_UAV));
	FIntPoint ScreenTileAdaptiveProbeIndicesBufferSize = FIntPoint(ScreenProbeViewportBufferSize.X * FMath::Max(NumScreenTileSubsamples, 1), ScreenProbeViewportBufferSize.Y);
	FRDGTextureDesc ScreenTileAdaptiveProbeIndicesDesc(FRDGTextureDesc::Create2D(ScreenTileAdaptiveProbeIndicesBufferSize, PF_R16_UINT, FClearValueBinding::Black, TexCreate_ShaderResource | TexCreate_UAV));
	ScreenProbeParameters.ScreenTileAdaptiveProbeHeader = GraphBuilder.CreateTexture(ScreenTileAdaptiveProbeHeaderDesc, TEXT("ScreenTileAdaptiveProbeHeader"));
	ScreenProbeParameters.ScreenTileAdaptiveProbeIndices = GraphBuilder.CreateTexture(ScreenTileAdaptiveProbeIndicesDesc, TEXT("ScreenTileAdaptiveProbeIndices"));

	FComputeShaderUtils::ClearUAV(GraphBuilder, View.ShaderMap, GraphBuilder.CreateUAV(FRDGBufferUAVDesc(NumAdaptiveScreenProbes, PF_R32_UINT)), 0);
	uint32 ClearValues[4] = {0, 0, 0, 0};
	AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(FRDGTextureUAVDesc(ScreenProbeParameters.ScreenTileAdaptiveProbeHeader)), ClearValues);

	if (ScreenProbeParameters.MaxNumAdaptiveProbes > 0 && ScreenProbeParameters.AdaptiveScreenTileSampleResolution > 0)
	{ 
		{
			FScreenProbeAdaptivePlacementCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FScreenProbeAdaptivePlacementCS::FParameters>();
			PassParameters->RWNumAdaptiveScreenProbes = GraphBuilder.CreateUAV(FRDGBufferUAVDesc(NumAdaptiveScreenProbes, PF_R32_UINT));
			PassParameters->RWAdaptiveScreenProbeData = GraphBuilder.CreateUAV(FRDGBufferUAVDesc(AdaptiveScreenProbeData, PF_R32_UINT));
			PassParameters->RWScreenTileAdaptiveProbeHeader = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(ScreenProbeParameters.ScreenTileAdaptiveProbeHeader));
			PassParameters->RWScreenTileAdaptiveProbeIndices = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(ScreenProbeParameters.ScreenTileAdaptiveProbeIndices));
			PassParameters->View = View.ViewUniformBuffer;
			PassParameters->SceneTexturesStruct = CreateSceneTextureUniformBuffer(GraphBuilder, View.FeatureLevel);
			PassParameters->ScreenProbeParameters = ScreenProbeParameters;

			auto ComputeShader = View.ShaderMap->GetShader<FScreenProbeAdaptivePlacementCS>(0);

			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("AdaptivePlacement Samples=%u", NumScreenTileSubsamples),
				ComputeShader,
				PassParameters,
				FComputeShaderUtils::GetGroupCount(ScreenProbeParameters.ScreenProbeViewSize * ScreenProbeParameters.AdaptiveScreenTileSampleResolution, FScreenProbeAdaptivePlacementCS::GetGroupSize()));
		}

		{
			FScreenProbeWriteDepthForAdaptiveProbesCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FScreenProbeWriteDepthForAdaptiveProbesCS::FParameters>();
			PassParameters->RWDownsampledDepth = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(ScreenProbeParameters.DownsampledDepth));
			PassParameters->View = View.ViewUniformBuffer;
			PassParameters->SceneTexturesStruct = CreateSceneTextureUniformBuffer(GraphBuilder, View.FeatureLevel);
			PassParameters->ScreenProbeParameters = ScreenProbeParameters;

			auto ComputeShader = View.ShaderMap->GetShader<FScreenProbeWriteDepthForAdaptiveProbesCS>(0);

			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("DownsampleDepthAdaptive"),
				ComputeShader,
				PassParameters,
				FComputeShaderUtils::GetGroupCount(ScreenProbeParameters.MaxNumAdaptiveProbes, FScreenProbeWriteDepthForAdaptiveProbesCS::GetGroupSize()));
		}
	}
	else
	{
		FComputeShaderUtils::ClearUAV(GraphBuilder, View.ShaderMap, GraphBuilder.CreateUAV(FRDGBufferUAVDesc(AdaptiveScreenProbeData, PF_R32_UINT)), 0);
		AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(FRDGTextureUAVDesc(ScreenProbeParameters.ScreenTileAdaptiveProbeIndices)), ClearValues);
	}

	FRDGBufferRef ScreenProbeIndirectArgs = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateIndirectDesc<FRHIDispatchIndirectParameters>((uint32)EScreenProbeIndirectArgs::Max), TEXT("ScreenProbeIndirectArgs"));

	{
		FSetupAdaptiveProbeIndirectArgsCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FSetupAdaptiveProbeIndirectArgsCS::FParameters>();
		PassParameters->RWScreenProbeIndirectArgs = GraphBuilder.CreateUAV(FRDGBufferUAVDesc(ScreenProbeIndirectArgs, PF_R32_UINT));
		PassParameters->ScreenProbeParameters = ScreenProbeParameters;

		auto ComputeShader = View.ShaderMap->GetShader<FSetupAdaptiveProbeIndirectArgsCS>(0);

		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("SetupAdaptiveProbeIndirectArgs"),
			ComputeShader,
			PassParameters,
			FIntVector(1, 1, 1));
	}

	ScreenProbeParameters.ProbeIndirectArgs = ScreenProbeIndirectArgs;

	FLumenCardTracingInputs TracingInputs(GraphBuilder, Scene, View);

	LumenRadianceCache::FRadianceCacheParameters RadianceCacheParameters;
	RenderRadianceCache(GraphBuilder, TracingInputs, View, nullptr, &ScreenProbeParameters, RadianceCacheParameters);

	if (LumenScreenProbeGather::UseImportanceSampling())
	{
		GenerateImportanceSamplingRays(
			GraphBuilder, 
			View, 
			RadianceCacheParameters,
			ScreenProbeParameters);
	}

	FRDGTextureDesc TraceRadianceDesc(FRDGTextureDesc::Create2D(ScreenProbeParameters.ScreenProbeTraceBufferSize, PF_FloatRGB, FClearValueBinding::Black, TexCreate_ShaderResource | TexCreate_UAV));
	ScreenProbeParameters.TraceRadiance = GraphBuilder.CreateTexture(TraceRadianceDesc, TEXT("TraceRadiance"));
	ScreenProbeParameters.RWTraceRadiance = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(ScreenProbeParameters.TraceRadiance));

	FRDGTextureDesc TraceHitDesc(FRDGTextureDesc::Create2D(ScreenProbeParameters.ScreenProbeTraceBufferSize, PF_R16F, FClearValueBinding::Black, TexCreate_ShaderResource | TexCreate_UAV));
	ScreenProbeParameters.TraceHit = GraphBuilder.CreateTexture(TraceHitDesc, TEXT("TraceHit"));
	ScreenProbeParameters.RWTraceHit = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(ScreenProbeParameters.TraceHit));

	TraceScreenProbes(
		GraphBuilder, 
		Scene,
		View, 
		bSSGI,
		GLumenGatherCvars.TraceCards != 0,
		SceneTextures,
		PrevSceneColorMip,
		TracingInputs,
		RadianceCacheParameters,
		ScreenProbeParameters,
		MeshSDFGridParameters);
	
	FScreenProbeGatherParameters GatherParameters;
	FilterScreenProbes(GraphBuilder, View, ScreenProbeParameters, GatherParameters);

	FScreenSpaceBentNormalParameters ScreenSpaceBentNormalParameters;
	ScreenSpaceBentNormalParameters.UseScreenBentNormal = 0;
	ScreenSpaceBentNormalParameters.ScreenBentNormal = GraphBuilder.RegisterExternalTexture(GSystemTextures.BlackDummy);
	ScreenSpaceBentNormalParameters.ScreenDiffuseLighting = GraphBuilder.RegisterExternalTexture(GSystemTextures.BlackDummy);

	if (LumenScreenProbeGather::UseScreenSpaceBentNormal())
	{
		ScreenSpaceBentNormalParameters = ComputeScreenSpaceBentNormal(GraphBuilder, Scene, View, ScreenProbeParameters);
	}

	FRDGTextureDesc DiffuseIndirectDesc = FRDGTextureDesc::Create2D(SceneContext.GetBufferSizeXY(), PF_FloatRGBA, FClearValueBinding::Black, TexCreate_ShaderResource | TexCreate_UAV);
	FRDGTextureRef DiffuseIndirect = GraphBuilder.CreateTexture(DiffuseIndirectDesc, TEXT("DiffuseIndirect"));

	FRDGTextureDesc RoughSpecularIndirectDesc = FRDGTextureDesc::Create2D(SceneContext.GetBufferSizeXY(), PF_FloatRGB, FClearValueBinding::Black, TexCreate_ShaderResource | TexCreate_UAV);
	FRDGTextureRef RoughSpecularIndirect = GraphBuilder.CreateTexture(RoughSpecularIndirectDesc, TEXT("RoughSpecularIndirect"));

	{
		FScreenProbeIndirectCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FScreenProbeIndirectCS::FParameters>();
		PassParameters->RWDiffuseIndirect = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(DiffuseIndirect));
		PassParameters->RWRoughSpecularIndirect = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(RoughSpecularIndirect));
		PassParameters->GatherParameters = GatherParameters;
		PassParameters->ScreenProbeParameters = ScreenProbeParameters;
		PassParameters->View = View.ViewUniformBuffer;
		PassParameters->SceneTexturesStruct = CreateSceneTextureUniformBuffer(GraphBuilder, View.FeatureLevel);
		PassParameters->FullResolutionJitterWidth = GLumenScreenProbeFullResolutionJitterWidth;
		PassParameters->ScreenSpaceBentNormalParameters = ScreenSpaceBentNormalParameters;

		FScreenProbeIndirectCS::FPermutationDomain PermutationVector;
		PermutationVector.Set< FScreenProbeIndirectCS::FDiffuseIntegralMethod >(LumenScreenProbeGather::GetDiffuseIntegralMethod());
		auto ComputeShader = View.ShaderMap->GetShader<FScreenProbeIndirectCS>(PermutationVector);

		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("ComputeIndirect %ux%u", View.ViewRect.Width(), View.ViewRect.Height()),
			ComputeShader,
			PassParameters,
			FComputeShaderUtils::GetGroupCount(View.ViewRect.Size(), FScreenProbeIndirectCS::GetGroupSize()));
	}

	FSSDSignalTextures DenoiserOutputs;
	DenoiserOutputs.Textures[0] = DiffuseIndirect;
	DenoiserOutputs.Textures[1] = RoughSpecularIndirect;
	bLumenUseDenoiserComposite = false;

	if (GLumenScreenProbeTemporalFilter)
	{
		if (GLumenScreenProbeUseHistoryNeighborhoodClamp)
		{
			FRDGTextureRef CompressedDepthTexture;
			FRDGTextureRef CompressedShadingModelTexture;
			{
				FRDGTextureDesc Desc = FRDGTextureDesc::Create2D(
					SceneTextures.SceneDepthTexture->Desc.Extent,
					PF_R16F,
					FClearValueBinding::None,					
					/* InTargetableFlags = */ TexCreate_ShaderResource | TexCreate_UAV);

				CompressedDepthTexture = GraphBuilder.CreateTexture(Desc, TEXT("ScreenProbeGather.CompressedDepth"));

				Desc.Format = PF_R8_UINT;
				CompressedShadingModelTexture = GraphBuilder.CreateTexture(Desc, TEXT("ScreenProbeGather.CompressedShadingModelID"));
			}

			{
				FGenerateCompressedGBuffer::FParameters* PassParameters = GraphBuilder.AllocParameters<FGenerateCompressedGBuffer::FParameters>();
				PassParameters->RWCompressedDepthBufferOutput = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(CompressedDepthTexture));
				PassParameters->RWCompressedShadingModelOutput = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(CompressedShadingModelTexture));
				PassParameters->View = View.ViewUniformBuffer;
				PassParameters->SceneTextures = SceneTextures;

				auto ComputeShader = View.ShaderMap->GetShader<FGenerateCompressedGBuffer>(0);

				FComputeShaderUtils::AddPass(
					GraphBuilder,
					RDG_EVENT_NAME("GenerateCompressedGBuffer"),
					ComputeShader,
					PassParameters,
					FComputeShaderUtils::GetGroupCount(View.ViewRect.Size(), FGenerateCompressedGBuffer::GetGroupSize()));
			}

			FSSDSignalTextures ScreenSpaceDenoiserInputs;
			ScreenSpaceDenoiserInputs.Textures[0] = DiffuseIndirect;
			ScreenSpaceDenoiserInputs.Textures[1] = RoughSpecularIndirect;

			DenoiserOutputs = IScreenSpaceDenoiser::DenoiseIndirectProbeHierarchy(
				GraphBuilder,
				View, 
				PreviousViewInfos,
				SceneTextures,
				ScreenSpaceDenoiserInputs,
				CompressedDepthTexture,
				CompressedShadingModelTexture);

			bLumenUseDenoiserComposite = true;
		}
		else
		{
			UpdateHistoryScreenProbeGather(
				GraphBuilder,
				View,
				SceneContext.GetBufferSizeXY(),
				DiffuseIndirect,
				RoughSpecularIndirect);

			DenoiserOutputs.Textures[0] = DiffuseIndirect;
			DenoiserOutputs.Textures[1] = RoughSpecularIndirect;
		}
	}

	return DenoiserOutputs;
}

