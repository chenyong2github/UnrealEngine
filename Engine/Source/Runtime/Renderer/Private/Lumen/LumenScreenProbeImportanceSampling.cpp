// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	LumenScreenProbeImportanceSampling.cpp
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

int32 GLumenScreenProbeImportanceSampling = 1;
FAutoConsoleVariableRef GVarLumenScreenProbeGatherImportanceSampling(
	TEXT("r.Lumen.ScreenProbeGather.ImportanceSample"),
	GLumenScreenProbeImportanceSampling,
	TEXT("Whether to use Importance Sampling to generate probe trace directions."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

int32 GLumenScreenProbeImportanceSampleIncomingLighting = 1;
FAutoConsoleVariableRef GVarLumenScreenProbeImportanceSampleIncomingLighting(
	TEXT("r.Lumen.ScreenProbeGather.ImportanceSample.IncomingLighting"),
	GLumenScreenProbeImportanceSampleIncomingLighting,
	TEXT("Whether to Importance Sample incoming lighting to generate probe trace directions.  When disabled, only the BRDF will be importance sampled."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

int32 GLumenScreenProbeImportanceSampleProbeRadianceHistory = 1;
FAutoConsoleVariableRef GVarLumenScreenProbeImportanceSampleProbeRadianceHistory(
	TEXT("r.Lumen.ScreenProbeGather.ImportanceSample.ProbeRadianceHistory"),
	GLumenScreenProbeImportanceSampleProbeRadianceHistory,
	TEXT("Whether to Importance Sample incoming lighting from last frame's filtered traces to generate probe trace directions.  When disabled, the Radiance Cache will be used instead."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

int32 GLumenScreenProbeBRDFOctahedronResolution = 8;
FAutoConsoleVariableRef CVarLumenScreenProbeBRDFOctahedronResolution(
	TEXT("r.Lumen.ScreenProbeGather.ImportanceSample.BRDFOctahedronResolution"),
	GLumenScreenProbeBRDFOctahedronResolution,
	TEXT("Resolution of the BRDF PDF octahedron per probe."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

float GLumenScreenProbeImportanceSamplingMinPDFToTrace = .1f;
FAutoConsoleVariableRef GVarLumenScreenProbeImportanceSamplingMinPDFToTrace(
	TEXT("r.Lumen.ScreenProbeGather.ImportanceSample.MinPDFToTrace"),
	GLumenScreenProbeImportanceSamplingMinPDFToTrace,
	TEXT("Minimum normalized BRDF PDF to trace rays for.  Larger values cause black corners, but reduce noise as more rays are able to be reassigned to an important direction."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

float GLumenScreenProbeImportanceSamplingHistoryDistanceThreshold = 30;
FAutoConsoleVariableRef CVarLumenScreenProbeImportanceSamplingHistoryDistanceThreshold(
	TEXT("r.Lumen.ScreenProbeGather.ImportanceSample.HistoryDistanceThreshold"),
	GLumenScreenProbeImportanceSamplingHistoryDistanceThreshold,
	TEXT(""),
	ECVF_RenderThreadSafe
	);

extern int32 GLumenScreenProbeGatherReferenceMode;

namespace LumenScreenProbeGather
{
	bool UseImportanceSampling()
	{
		if (GLumenScreenProbeGatherReferenceMode)
		{
			return false;
		}

		// Shader permutations only created for these resolutions
		const int32 TracingResolution = GetTracingOctahedronResolution();
		return GLumenScreenProbeImportanceSampling != 0 && (TracingResolution == 4 || TracingResolution == 8 || TracingResolution == 16);
	}
}

class FScreenProbeComputeBRDFProbabilityDensityFunctionCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FScreenProbeComputeBRDFProbabilityDensityFunctionCS)
	SHADER_USE_PARAMETER_STRUCT(FScreenProbeComputeBRDFProbabilityDensityFunctionCS, FGlobalShader)

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, RWBRDFProbabilityDensityFunction)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<float>, RWBRDFProbabilityDensityFunctionSH)
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FSceneTextureUniformParameters, SceneTexturesStruct)
		SHADER_PARAMETER_STRUCT_INCLUDE(FScreenProbeParameters, ScreenProbeParameters)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportLumenGI(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.CompilerFlags.Add(CFLAG_Wave32);
	}
};

IMPLEMENT_GLOBAL_SHADER(FScreenProbeComputeBRDFProbabilityDensityFunctionCS, "/Engine/Private/Lumen/LumenScreenProbeImportanceSampling.usf", "ScreenProbeComputeBRDFProbabilityDensityFunctionCS", SF_Compute);


class FScreenProbeComputeLightingProbabilityDensityFunctionCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FScreenProbeComputeLightingProbabilityDensityFunctionCS)
	SHADER_USE_PARAMETER_STRUCT(FScreenProbeComputeLightingProbabilityDensityFunctionCS, FGlobalShader)

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, RWLightingProbabilityDensityFunction)
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER_STRUCT_INCLUDE(FScreenProbeParameters, ScreenProbeParameters)
		SHADER_PARAMETER_STRUCT_INCLUDE(LumenRadianceCache::FRadianceCacheParameters, RadianceCacheParameters)
		SHADER_PARAMETER(FVector4, ImportanceSamplingHistoryScreenPositionScaleBias)
		SHADER_PARAMETER(FVector4, ImportanceSamplingHistoryUVMinMax)
		SHADER_PARAMETER(float, ImportanceSamplingHistoryDistanceThreshold)
		SHADER_PARAMETER(float, PrevInvPreExposure)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float3>, HistoryScreenProbeRadiance)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, HistoryDownsampledDepth)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, VelocityTexture)
	END_SHADER_PARAMETER_STRUCT()

	class FThreadGroupSize : SHADER_PERMUTATION_SPARSE_INT("LIGHTING_PDF_THREADGROUP_SIZE", 4, 8, 16);
	class FProbeRadianceHistory : SHADER_PERMUTATION_BOOL("PROBE_RADIANCE_HISTORY");
	class FRadianceCache : SHADER_PERMUTATION_BOOL("RADIANCE_CACHE");
	using FPermutationDomain = TShaderPermutationDomain<FThreadGroupSize, FProbeRadianceHistory, FRadianceCache>;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportLumenGI(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.CompilerFlags.Add(CFLAG_Wave32);
	}
};

IMPLEMENT_GLOBAL_SHADER(FScreenProbeComputeLightingProbabilityDensityFunctionCS, "/Engine/Private/Lumen/LumenScreenProbeImportanceSampling.usf", "ScreenProbeComputeLightingProbabilityDensityFunctionCS", SF_Compute);



class FScreenProbeGenerateRaysCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FScreenProbeGenerateRaysCS)
	SHADER_USE_PARAMETER_STRUCT(FScreenProbeGenerateRaysCS, FGlobalShader)

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<uint>, RWStructuredImportanceSampledRayInfosForTracing)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<uint2>, RWStructuredImportanceSampledRayCoordForComposite)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, BRDFProbabilityDensityFunction)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<float>, BRDFProbabilityDensityFunctionSH)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, LightingProbabilityDensityFunction)
		SHADER_PARAMETER(float, MinPDFToTrace)
		SHADER_PARAMETER_STRUCT_INCLUDE(FScreenProbeParameters, ScreenProbeParameters)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportLumenGI(Parameters.Platform);
	}

	class FThreadGroupSize : SHADER_PERMUTATION_SPARSE_INT("GENERATE_RAYS_THREADGROUP_SIZE", 4, 8, 16);
	class FImportanceSampleLighting : SHADER_PERMUTATION_BOOL("IMPORTANCE_SAMPLE_LIGHTING");
	class FGenerateRaysForGatherComposite : SHADER_PERMUTATION_BOOL("GENERATE_RAYS_FOR_GATHER_COMPOSITE");
	using FPermutationDomain = TShaderPermutationDomain<FThreadGroupSize, FImportanceSampleLighting, FGenerateRaysForGatherComposite>;

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.CompilerFlags.Add(CFLAG_Wave32);
	}
};

IMPLEMENT_GLOBAL_SHADER(FScreenProbeGenerateRaysCS, "/Engine/Private/Lumen/LumenScreenProbeImportanceSampling.usf", "ScreenProbeGenerateRaysCS", SF_Compute);

void GenerateImportanceSamplingRays(
	FRDGBuilder& GraphBuilder, 
	const FViewInfo& View, 
	const LumenRadianceCache::FRadianceCacheParameters& RadianceCacheParameters,
	FScreenProbeParameters& ScreenProbeParameters)
{
	const uint32 MaxImportanceSamplingOctahedronResolution = ScreenProbeParameters.ScreenProbeTracingOctahedronResolution * 2;
	ScreenProbeParameters.ImportanceSampling.MaxImportanceSamplingOctahedronResolution = MaxImportanceSamplingOctahedronResolution;

	const uint32 BRDFOctahedronResolution = GLumenScreenProbeBRDFOctahedronResolution;
	ScreenProbeParameters.ImportanceSampling.ScreenProbeBRDFOctahedronResolution = BRDFOctahedronResolution;

	FIntPoint PDFBufferSize = ScreenProbeParameters.ScreenProbeAtlasBufferSize * BRDFOctahedronResolution;
	FRDGTextureDesc BRDFProbabilityDensityFunctionDesc(FRDGTextureDesc::Create2D(PDFBufferSize, PF_R16F, FClearValueBinding::Black, TexCreate_ShaderResource | TexCreate_UAV));
	FRDGTextureRef BRDFProbabilityDensityFunction = GraphBuilder.CreateTexture(BRDFProbabilityDensityFunctionDesc, TEXT("BRDFProbabilityDensityFunction"));
	
	const int32 BRDF_SHBufferSize = ScreenProbeParameters.ScreenProbeAtlasBufferSize.X * ScreenProbeParameters.ScreenProbeAtlasBufferSize.Y * 9;
	FRDGBufferDesc BRDFProbabilityDensityFunctionSHDesc = FRDGBufferDesc::CreateBufferDesc(sizeof(FFloat16), BRDF_SHBufferSize);
	FRDGBufferRef BRDFProbabilityDensityFunctionSH = GraphBuilder.CreateBuffer(BRDFProbabilityDensityFunctionSHDesc, TEXT("BRDFProbabilityDensityFunctionSH"));

	{
		FScreenProbeComputeBRDFProbabilityDensityFunctionCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FScreenProbeComputeBRDFProbabilityDensityFunctionCS::FParameters>();
		PassParameters->RWBRDFProbabilityDensityFunction = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(BRDFProbabilityDensityFunction));
		PassParameters->RWBRDFProbabilityDensityFunctionSH = GraphBuilder.CreateUAV(FRDGBufferUAVDesc(BRDFProbabilityDensityFunctionSH, PF_R16F));
		PassParameters->View = View.ViewUniformBuffer;
		PassParameters->SceneTexturesStruct = CreateSceneTextureUniformBuffer(GraphBuilder, View.FeatureLevel);
		PassParameters->ScreenProbeParameters = ScreenProbeParameters;

		auto ComputeShader = View.ShaderMap->GetShader<FScreenProbeComputeBRDFProbabilityDensityFunctionCS>(0);

		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("ComputeBRDF_PDF"),
			ComputeShader,
			PassParameters,
			ScreenProbeParameters.ProbeIndirectArgs,
			(uint32)EScreenProbeIndirectArgs::GroupPerProbe * sizeof(FRHIDispatchIndirectParameters));
	}

	const bool bImportanceSampleLighting = GLumenScreenProbeImportanceSampleIncomingLighting != 0;

	FRDGTextureRef LightingProbabilityDensityFunction = nullptr;

	if (bImportanceSampleLighting)
	{
		FIntPoint LightingProbabilityDensityFunctionBufferSize = ScreenProbeParameters.ScreenProbeAtlasBufferSize * ScreenProbeParameters.ScreenProbeTracingOctahedronResolution;
		FRDGTextureDesc LightingProbabilityDensityFunctionDesc(FRDGTextureDesc::Create2D(LightingProbabilityDensityFunctionBufferSize, PF_R16F, FClearValueBinding::Black, TexCreate_ShaderResource | TexCreate_UAV));
		LightingProbabilityDensityFunction = GraphBuilder.CreateTexture(LightingProbabilityDensityFunctionDesc, TEXT("LightingProbabilityDensityFunction"));

		FScreenProbeGatherTemporalState& ScreenProbeGatherState = View.ViewState->Lumen.ScreenProbeGatherState;

		const bool bUseProbeRadianceHistory = GLumenScreenProbeImportanceSampleProbeRadianceHistory != 0 
			&& ScreenProbeGatherState.ImportanceSamplingHistoryScreenProbeRadiance.IsValid()
			&& !View.bCameraCut 
			&& !View.bPrevTransformsReset
			&& ScreenProbeGatherState.ImportanceSamplingHistoryScreenProbeRadiance->GetDesc().Extent == ScreenProbeParameters.ScreenProbeTraceBufferSize;

		{
			FScreenProbeComputeLightingProbabilityDensityFunctionCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FScreenProbeComputeLightingProbabilityDensityFunctionCS::FParameters>();
			PassParameters->RWLightingProbabilityDensityFunction = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(LightingProbabilityDensityFunction));
			PassParameters->ScreenProbeParameters = ScreenProbeParameters;
			PassParameters->RadianceCacheParameters = RadianceCacheParameters;
			PassParameters->View = View.ViewUniformBuffer;

			if (bUseProbeRadianceHistory)
			{
				PassParameters->PrevInvPreExposure = 1.0f / View.PrevViewInfo.SceneColorPreExposure;
				PassParameters->ImportanceSamplingHistoryScreenPositionScaleBias = ScreenProbeGatherState.ImportanceSamplingHistoryScreenPositionScaleBias;

				FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(GraphBuilder.RHICmdList);
				
				const FVector2D InvBufferSize(1.0f / SceneContext.GetBufferSizeXY().X, 1.0f / SceneContext.GetBufferSizeXY().Y);

				// Pull in the max UV to exclude the region which will read outside the viewport due to bilinear filtering
				PassParameters->ImportanceSamplingHistoryUVMinMax = FVector4(
					(ScreenProbeGatherState.ImportanceSamplingHistoryViewRect.Min.X + 0.5f) * InvBufferSize.X,
					(ScreenProbeGatherState.ImportanceSamplingHistoryViewRect.Min.Y + 0.5f) * InvBufferSize.Y,
					(ScreenProbeGatherState.ImportanceSamplingHistoryViewRect.Max.X - 0.5f) * InvBufferSize.X,
					(ScreenProbeGatherState.ImportanceSamplingHistoryViewRect.Max.Y - 0.5f) * InvBufferSize.Y);

				FSceneTextureParameters SceneTextures = GetSceneTextureParameters(GraphBuilder);

				// Fallback to a black texture if no velocity.
				if (!SceneTextures.GBufferVelocityTexture)
				{
					SceneTextures.GBufferVelocityTexture = GSystemTextures.GetBlackDummy(GraphBuilder);
				}

				PassParameters->VelocityTexture = SceneTextures.GBufferVelocityTexture;

				PassParameters->ImportanceSamplingHistoryDistanceThreshold = GLumenScreenProbeImportanceSamplingHistoryDistanceThreshold;
				PassParameters->HistoryScreenProbeRadiance = GraphBuilder.RegisterExternalTexture(ScreenProbeGatherState.ImportanceSamplingHistoryScreenProbeRadiance);
				PassParameters->HistoryDownsampledDepth = GraphBuilder.RegisterExternalTexture(ScreenProbeGatherState.ImportanceSamplingHistoryDownsampledDepth);
			}

			FScreenProbeComputeLightingProbabilityDensityFunctionCS::FPermutationDomain PermutationVector;
			PermutationVector.Set<FScreenProbeComputeLightingProbabilityDensityFunctionCS::FThreadGroupSize>(ScreenProbeParameters.ScreenProbeTracingOctahedronResolution);
			PermutationVector.Set<FScreenProbeComputeLightingProbabilityDensityFunctionCS::FProbeRadianceHistory>(bUseProbeRadianceHistory);
			PermutationVector.Set<FScreenProbeComputeLightingProbabilityDensityFunctionCS::FRadianceCache>(LumenScreenProbeGather::UseRadianceCache(View));
			auto ComputeShader = View.ShaderMap->GetShader<FScreenProbeComputeLightingProbabilityDensityFunctionCS>(PermutationVector);

			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("ComputeLightingPDF"),
				ComputeShader,
				PassParameters,
				ScreenProbeParameters.ProbeIndirectArgs,
				// Spawn a group on every probe
				(uint32)EScreenProbeIndirectArgs::GroupPerProbe * sizeof(FRHIDispatchIndirectParameters));
		}

		ScreenProbeGatherState.ImportanceSamplingHistoryViewRect = View.ViewRect;
		ScreenProbeGatherState.ImportanceSamplingHistoryScreenPositionScaleBias = View.GetScreenPositionScaleBias(FSceneRenderTargets::Get_FrameConstantsOnly().GetBufferSizeXY(), View.ViewRect);
	}

	FIntPoint RayInfosForTracingBufferSize = ScreenProbeParameters.ScreenProbeAtlasBufferSize * ScreenProbeParameters.ScreenProbeTracingOctahedronResolution;
	FRDGTextureDesc StructuredImportanceSampledRayInfosForTracingDesc(FRDGTextureDesc::Create2D(RayInfosForTracingBufferSize, PF_R16_UINT, FClearValueBinding::Black, TexCreate_ShaderResource | TexCreate_UAV));
	ScreenProbeParameters.ImportanceSampling.StructuredImportanceSampledRayInfosForTracing = GraphBuilder.CreateTexture(StructuredImportanceSampledRayInfosForTracingDesc, TEXT("RayInfosForTracing"));

	FIntPoint RayCoordForCompositeBufferSize = ScreenProbeParameters.ScreenProbeAtlasBufferSize * MaxImportanceSamplingOctahedronResolution;
	FRDGTextureDesc StructuredImportanceSampledRayCoordForCompositeDesc(FRDGTextureDesc::Create2D(RayCoordForCompositeBufferSize, PF_R8G8, FClearValueBinding::Black, TexCreate_ShaderResource | TexCreate_UAV));
	ScreenProbeParameters.ImportanceSampling.StructuredImportanceSampledRayCoordForComposite = GraphBuilder.CreateTexture(StructuredImportanceSampledRayCoordForCompositeDesc, TEXT("RayCoordForComposite"));

	{
		FScreenProbeGenerateRaysCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FScreenProbeGenerateRaysCS::FParameters>();
		PassParameters->RWStructuredImportanceSampledRayInfosForTracing = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(ScreenProbeParameters.ImportanceSampling.StructuredImportanceSampledRayInfosForTracing));
		PassParameters->RWStructuredImportanceSampledRayCoordForComposite = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(ScreenProbeParameters.ImportanceSampling.StructuredImportanceSampledRayCoordForComposite));
		PassParameters->BRDFProbabilityDensityFunction = BRDFProbabilityDensityFunction;
		PassParameters->LightingProbabilityDensityFunction = LightingProbabilityDensityFunction;
		PassParameters->BRDFProbabilityDensityFunctionSH = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(BRDFProbabilityDensityFunctionSH, PF_R16F));
		PassParameters->MinPDFToTrace = GLumenScreenProbeImportanceSamplingMinPDFToTrace;
		PassParameters->ScreenProbeParameters = ScreenProbeParameters;

		FScreenProbeGenerateRaysCS::FPermutationDomain PermutationVector;
		PermutationVector.Set< FScreenProbeGenerateRaysCS::FThreadGroupSize >(ScreenProbeParameters.ScreenProbeTracingOctahedronResolution);
		PermutationVector.Set< FScreenProbeGenerateRaysCS::FImportanceSampleLighting >(bImportanceSampleLighting);
		PermutationVector.Set< FScreenProbeGenerateRaysCS::FGenerateRaysForGatherComposite >(GLumenScreenProbeSpatialFilterScatter == 0);
		auto ComputeShader = View.ShaderMap->GetShader<FScreenProbeGenerateRaysCS>(PermutationVector);

		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("GenerateRays"),
			ComputeShader,
			PassParameters,
			ScreenProbeParameters.ProbeIndirectArgs,
			// Spawn a group on every probe
			(uint32)EScreenProbeIndirectArgs::GroupPerProbe * sizeof(FRHIDispatchIndirectParameters));
	}
}
