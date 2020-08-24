// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	LumenDiffuseIndirect.cpp
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
#include "LumenSceneBVH.h"
#include "SceneTextureParameters.h"
#include "IndirectLightRendering.h"
#include "LumenRadianceCache.h"

int32 GLumenDiffuseTraceCards = 1;
FAutoConsoleVariableRef GVarLumenDiffuseTraceCards(
	TEXT("r.Lumen.DiffuseIndirect.TraceCards"),
	GLumenDiffuseTraceCards,
	TEXT("."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

int32 GLumenProbeHierarchyTraceCards = 0;
FAutoConsoleVariableRef GVarLumenProbeHierarchyTraceCards(
	TEXT("r.Lumen.ProbeHierarchy.TraceCards"),
	GLumenProbeHierarchyTraceCards,
	TEXT("."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

float GLumenDiffuseMaxCardTraceDistance = 180.0f;
FAutoConsoleVariableRef GVarLumenDiffuseMaxCardTraceDistance(
	TEXT("r.Lumen.DiffuseIndirect.MaxCardTraceDistance"),
	GLumenDiffuseMaxCardTraceDistance,
	TEXT("Max trace distance for the diffuse indirect card rays."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

int32 GLumenDiffuseCardTraceMeshSDF = 1;
FAutoConsoleVariableRef GVarLumenDiffuseTraceSDF(
	TEXT("r.Lumen.DiffuseIndirect.CardTraceMeshSDF"),
	GLumenDiffuseCardTraceMeshSDF,
	TEXT("."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

int32 GLumenDiffuseCubeMapTree = 1;
FAutoConsoleVariableRef GVarLumenDiffuseCubeMapTree(
	TEXT("r.Lumen.DiffuseIndirect.CubeMapTree"),
	GLumenDiffuseCubeMapTree,
	TEXT("Whether to use cube map trees to apply texture on mesh SDF hit points."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

float GDiffuseTraceStepFactor = 1;
FAutoConsoleVariableRef CVarDiffuseTraceStepFactor(
	TEXT("r.Lumen.DiffuseIndirect.TraceStepFactor"),
	GDiffuseTraceStepFactor,
	TEXT("."),
	ECVF_Scalability | ECVF_RenderThreadSafe
	);

int32 GLumenDiffuseNumTargetCones = 128;
FAutoConsoleVariableRef CVarLumenDiffuseNumTargetCones(
	TEXT("r.Lumen.DiffuseIndirect.NumCones"),
	GLumenDiffuseNumTargetCones,
	TEXT(""),
	ECVF_Scalability | ECVF_RenderThreadSafe
	);

float GLumenDiffuseMinSampleRadius = 10;
FAutoConsoleVariableRef CVarLumenDiffuseMinSampleRadius(
	TEXT("r.Lumen.DiffuseIndirect.MinSampleRadius"),
	GLumenDiffuseMinSampleRadius,
	TEXT("."),
	ECVF_Scalability | ECVF_RenderThreadSafe
	);

float GLumenDiffuseMinTraceDistance = 0;
FAutoConsoleVariableRef CVarLumenDiffuseMinTraceDistance(
	TEXT("r.Lumen.DiffuseIndirect.MinTraceDistance"),
	GLumenDiffuseMinTraceDistance,
	TEXT("."),
	ECVF_Scalability | ECVF_RenderThreadSafe
	);

float GLumenDiffuseSurfaceBias = 5.0f;
FAutoConsoleVariableRef CVarLumenDiffuseSurfaceBias(
	TEXT("r.Lumen.DiffuseIndirect.SurfaceBias"),
	GLumenDiffuseSurfaceBias,
	TEXT("."),
	ECVF_Scalability | ECVF_RenderThreadSafe
	);

float GLumenDiffuseConeAngleScale = 0.5f;
FAutoConsoleVariableRef CVarLumenDiffuseConeAngleScale(
	TEXT("r.Lumen.DiffuseIndirect.ConeAngleScale"),
	GLumenDiffuseConeAngleScale,
	TEXT("Indirect cone angle scale. Smaller cones are more precise, but introduce more noise."),
	ECVF_Scalability | ECVF_RenderThreadSafe
	);

float GLumenDiffuseCardInterpolateInfluenceRadius = 10;
FAutoConsoleVariableRef CVarDiffuseCardInterpolateInfluenceRadius(
	TEXT("r.Lumen.DiffuseIndirect.CardInterpolateInfluenceRadius"),
	GLumenDiffuseCardInterpolateInfluenceRadius,
	TEXT("."),
	ECVF_Scalability | ECVF_RenderThreadSafe
	);

int32 GLumenDiffuseUseHistory = 1;
FAutoConsoleVariableRef CVarLumenDiffuseUseHistory(
	TEXT("r.Lumen.DiffuseIndirect.HistoryReprojection"),
	GLumenDiffuseUseHistory,
	TEXT(""),
	ECVF_Scalability | ECVF_RenderThreadSafe
	);

int32 GLumenDiffuseClearHistory = 0;
FAutoConsoleVariableRef CVarLumenDiffuseClearHistory(
	TEXT("r.Lumen.DiffuseIndirect.HistoryClearEveryFrame"),
	GLumenDiffuseClearHistory,
	TEXT(""),
	ECVF_RenderThreadSafe
	);

float GLumenDiffuseHistoryDistanceThreshold = 30;
FAutoConsoleVariableRef CVarLumenDiffuseHistoryDistanceThreshold(
	TEXT("r.Lumen.DiffuseIndirect.HistoryDistanceThreshold"),
	GLumenDiffuseHistoryDistanceThreshold,
	TEXT("World space distance threshold needed to discard last frame's Diffuse Indirect results.  Lower values reduce ghosting from characters when near a wall but increase flickering artifacts."),
	ECVF_RenderThreadSafe
	);

float GLumenDiffuseHistoryWeight = .9f;
FAutoConsoleVariableRef CVarLumenDiffuseHistoryWeight(
	TEXT("r.Lumen.DiffuseIndirect.HistoryWeight"),
	GLumenDiffuseHistoryWeight,
	TEXT("Amount of last frame's Diffuse Indirect to lerp into the final result.  Higher values increase stability, lower values have less streaking under occluder movement."),
	ECVF_RenderThreadSafe
	);

float GLumenDiffuseHistoryConvergenceWeight = .8f;
FAutoConsoleVariableRef CVarLumenDiffuseHistoryConvergenceWeight(
	TEXT("r.Lumen.DiffuseIndirect.HistoryConvergenceWeight"),
	GLumenDiffuseHistoryConvergenceWeight,
	TEXT(""),
	ECVF_RenderThreadSafe
	);

float GLumenDiffuseIntensity = 1;
FAutoConsoleVariableRef CVarLumenDiffuseIntensity(
	TEXT("r.Lumen.DiffuseIndirect.Intensity"),
	GLumenDiffuseIntensity,
	TEXT(""),
	ECVF_RenderThreadSafe
	);

int32 GLumenDiffuseSpatialFilter = 1;
FAutoConsoleVariableRef CVarLumenDiffuseSpatialFilter(
	TEXT("r.Lumen.DiffuseIndirect.SpatialFilter"),
	GLumenDiffuseSpatialFilter,
	TEXT(""),
	ECVF_RenderThreadSafe
	);

int32 GLumenDiffuseLowConvergenceSpatialFilter = 1;
FAutoConsoleVariableRef CVarLumenDiffuseLowConvergenceSpatialFilter(
	TEXT("r.Lumen.DiffuseIndirect.LowConvergenceSpatialFilter"),
	GLumenDiffuseLowConvergenceSpatialFilter,
	TEXT(""),
	ECVF_Scalability | ECVF_RenderThreadSafe
	);

float GLumenDiffuseLowConvergenceSpatialFilterSpread = 2;
FAutoConsoleVariableRef CVarLumenDiffuseLowConvergenceSpatialFilterSpread(
	TEXT("r.Lumen.DiffuseIndirect.LowConvergenceSpatialFilterSpread"),
	GLumenDiffuseLowConvergenceSpatialFilterSpread,
	TEXT(""),
	ECVF_Scalability | ECVF_RenderThreadSafe
	);

float GLumenDiffuseNearTraceDistanceScale = 1;
FAutoConsoleVariableRef CVarLumenDiffuseNearTraceDistanceScale(
	TEXT("r.Lumen.DiffuseIndirect.NearTraceDistanceScale"),
	GLumenDiffuseNearTraceDistanceScale,
	TEXT("Max trace distance scale for near field GI (trace distance = distance between probes * scale). After this distance far field will be approximated by probe volume."),
	ECVF_RenderThreadSafe
);

float GLumenDiffuseVoxelStepFactor = 1.0f;
FAutoConsoleVariableRef CVarLumenDiffuseVoxelStepFactor(
	TEXT("r.Lumen.DiffuseIndirect.VoxelStepFactor"),
	GLumenDiffuseVoxelStepFactor,
	TEXT(""),
	ECVF_Scalability | ECVF_RenderThreadSafe
	);

float GDiffuseCardTraceEndDistanceFromCamera = 4000.0f;
FAutoConsoleVariableRef CVarDiffuseCardTraceEndDistanceFromCamera(
	TEXT("r.Lumen.DiffuseIndirect.CardTraceEndDistanceFromCamera"),
	GDiffuseCardTraceEndDistanceFromCamera,
	TEXT(""),
	ECVF_Scalability | ECVF_RenderThreadSafe
	);

int32 GLumenGBufferDownsampleFilter = 1;
FAutoConsoleVariableRef CVarLumenGBufferDownsampleFilter(
	TEXT("r.Lumen.DiffuseIndirect.GBufferDownsampleFilter"),
	GLumenGBufferDownsampleFilter,
	TEXT("Whether to filter GBuffer inputs for indirect GI tracing input."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

int32 GLumenTracingVoxelTracingMode = 1;
FAutoConsoleVariableRef CVarLumenTracingVoxelTracingMode(
	TEXT("r.Lumen.Tracing.VoxelTracingMode"),
	GLumenTracingVoxelTracingMode,
	TEXT("Voxel tracing mode. 0 - Voxel cone tracing, 1 - Voxel cone tracing with global distance field, 2 - Voxel ray tracing."),
	ECVF_RenderThreadSafe
);

float GLumenTracingMaxTraceDistance = 10000.0f;
FAutoConsoleVariableRef CVarLumenTracingMaxTraceDistance(
	TEXT("r.Lumen.Tracing.MaxTraceDistance"),
	GLumenTracingMaxTraceDistance,
	TEXT("Max tracing distance for voxel cone tracing."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

uint32 Lumen::GetVoxelTracingMode()
{
	return FMath::Clamp(GLumenTracingVoxelTracingMode, 0, 2);
}

bool Lumen::UseVoxelRayTracing()
{
	return GetVoxelTracingMode() == 2;
}

float Lumen::GetMaxTraceDistance()
{
	return FMath::Clamp(GLumenTracingMaxTraceDistance, .01f, (float)HALF_WORLD_MAX);
}

void FHemisphereDirectionSampleGenerator::GenerateSamples(int32 TargetNumSamples, int32 InPowerOfTwoDivisor, int32 InSeed, bool bInFullSphere, bool bInCosineDistribution)
{
	int32 NumThetaSteps = FMath::TruncToInt(FMath::Sqrt(TargetNumSamples / ((float)PI)));
	//int32 NumPhiSteps = FMath::TruncToInt(NumThetaSteps * (float)PI);
	int32 NumPhiSteps = FMath::DivideAndRoundDown(TargetNumSamples, NumThetaSteps);
	NumPhiSteps = FMath::Max(FMath::DivideAndRoundDown(NumPhiSteps, InPowerOfTwoDivisor), 1) * InPowerOfTwoDivisor;

	if (SampleDirections.Num() != NumThetaSteps * NumPhiSteps || PowerOfTwoDivisor != InPowerOfTwoDivisor || Seed != InSeed || bInFullSphere != bFullSphere)
	{
		SampleDirections.Empty(NumThetaSteps * NumPhiSteps);
		FRandomStream RandomStream(InSeed);

		for (int32 ThetaIndex = 0; ThetaIndex < NumThetaSteps; ThetaIndex++)
		{
			for (int32 PhiIndex = 0; PhiIndex < NumPhiSteps; PhiIndex++)
			{
				const float U1 = RandomStream.GetFraction();
				const float U2 = RandomStream.GetFraction();

				float Fraction1 = (ThetaIndex + U1) / (float)NumThetaSteps;

				if (bInFullSphere)
				{
					Fraction1 = Fraction1 * 2 - 1;
				}

				const float Fraction2 = (PhiIndex + U2) / (float)NumPhiSteps;
				const float Phi = 2.0f * (float)PI * Fraction2;

				if (bInCosineDistribution)
				{
					const float CosTheta = FMath::Sqrt(Fraction1);
					const float SinTheta = FMath::Sqrt(1.0f - CosTheta * CosTheta);
					SampleDirections.Add(FVector4(FMath::Cos(Phi) * SinTheta, FMath::Sin(Phi) * SinTheta, CosTheta));
				}
				else
				{
					const float CosTheta = Fraction1;
					const float SinTheta = FMath::Sqrt(1.0f - CosTheta * CosTheta);
					SampleDirections.Add(FVector4(FMath::Cos(Phi) * SinTheta, FMath::Sin(Phi) * SinTheta, CosTheta));
				}
			}
		}

		ConeHalfAngle = FMath::Acos(1 - 1.0f / (float)SampleDirections.Num());
		Seed = InSeed;
		PowerOfTwoDivisor = InPowerOfTwoDivisor;
		bFullSphere = bInFullSphere;
		bCosineDistribution = bInCosineDistribution;
	}
}

class FDownsampleDepthAndNormalPS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FDownsampleDepthAndNormalPS)
	SHADER_USE_PARAMETER_STRUCT(FDownsampleDepthAndNormalPS, FGlobalShader)
		
	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		RENDER_TARGET_BINDING_SLOTS()
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER_STRUCT_REF(FSceneTexturesUniformParameters, SceneTexturesStruct)
		SHADER_PARAMETER(FIntPoint, SourceViewMax)
		SHADER_PARAMETER(int32, DownscaleFactor)
	END_SHADER_PARAMETER_STRUCT()

	class FDownsampleFilter : SHADER_PERMUTATION_BOOL("DOWNSAMPLE_FILTER");
	using FPermutationDomain = TShaderPermutationDomain<FDownsampleFilter>;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportLumenGI(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("DIFFUSE_TRACE_CARDS"), 1);
	}
};

IMPLEMENT_GLOBAL_SHADER(FDownsampleDepthAndNormalPS, "/Engine/Private/Lumen/LumenDiffuseIndirect.usf", "DownsampleDepthAndNormalPS", SF_Pixel);

class FDiffuseIndirectTraceCardsCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FDiffuseIndirectTraceCardsCS)
	SHADER_USE_PARAMETER_STRUCT(FDiffuseIndirectTraceCardsCS, FGlobalShader)

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FLumenCardTracingParameters, TracingParameters)
		SHADER_PARAMETER_STRUCT_INCLUDE(FLumenCardFroxelGridParameters, GridParameters)
		SHADER_PARAMETER_STRUCT_INCLUDE(FLumenMeshSDFGridParameters, MeshSDFGridParameters)
		SHADER_PARAMETER_STRUCT_INCLUDE(FLumenDiffuseTracingParameters, DiffuseTracingParameters)
	END_SHADER_PARAMETER_STRUCT()

	class FCulledCardsGrid : SHADER_PERMUTATION_BOOL("CULLED_CARDS_GRID");
	class FCardTraceMeshSDF : SHADER_PERMUTATION_BOOL("CARD_TRACE_MESH_SDF");
	class FCubeMapTree : SHADER_PERMUTATION_BOOL("CUBE_MAP_TREE");
	class FResumeRays : SHADER_PERMUTATION_BOOL("RESUME_RAYS");

	using FPermutationDomain = TShaderPermutationDomain<FCulledCardsGrid, FCardTraceMeshSDF, FCubeMapTree, FResumeRays>;

	static FPermutationDomain RemapPermutation(FPermutationDomain PermutationVector)
	{
		if (!PermutationVector.Get<FCardTraceMeshSDF>())
		{
			PermutationVector.Set<FCardTraceMeshSDF>(false);
		}

		if (!PermutationVector.Get<FCardTraceMeshSDF>())
		{
			PermutationVector.Set<FCubeMapTree>(false);
		}

		return PermutationVector;
	}

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		FPermutationDomain PermutationVector(Parameters.PermutationId);

		if (RemapPermutation(PermutationVector) != PermutationVector)
		{
			return false;
		}

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
		OutEnvironment.SetDefine(TEXT("DIFFUSE_TRACE_CARDS"), 1);
		OutEnvironment.CompilerFlags.Add(CFLAG_Wave32);
		OutEnvironment.CompilerFlags.Add(CFLAG_AllowTypedUAVLoads);
	}
};

IMPLEMENT_GLOBAL_SHADER(FDiffuseIndirectTraceCardsCS, "/Engine/Private/Lumen/LumenDiffuseIndirect.usf", "DiffuseIndirectTraceCardsCS", SF_Compute);


class FDiffuseIndirectTraceVoxelsCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FDiffuseIndirectTraceVoxelsCS)
	SHADER_USE_PARAMETER_STRUCT(FDiffuseIndirectTraceVoxelsCS, FGlobalShader)

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FLumenCardTracingParameters, TracingParameters)
		SHADER_PARAMETER_STRUCT_INCLUDE(FLumenDiffuseTracingParameters, DiffuseTracingParameters)
		SHADER_PARAMETER_STRUCT_INCLUDE(LumenRadianceCache::FRadianceCacheParameters, RadianceCacheParameters)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D, RWDiffuseIndirect0)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D, RWDiffuseIndirect1)
	END_SHADER_PARAMETER_STRUCT()

	class FVoxelTracingMode : SHADER_PERMUTATION_RANGE_INT("VOXEL_TRACING_MODE", 0, 3);
	class FDynamicSkyLight : SHADER_PERMUTATION_BOOL("ENABLE_DYNAMIC_SKY_LIGHT");
	class FTraceCards : SHADER_PERMUTATION_BOOL("DIFFUSE_TRACE_CARDS");
	class FTraceDistantScene : SHADER_PERMUTATION_BOOL("OLD_DENOISER_TRACE_DISTANT_SCENE");
	class FOutputIndividualRays : SHADER_PERMUTATION_BOOL("OUTPUT_INDIVIDUAL_RAYS");
	class FRadianceCache : SHADER_PERMUTATION_BOOL("RADIANCE_CACHE");
	using FPermutationDomain = TShaderPermutationDomain<FVoxelTracingMode, FDynamicSkyLight, FTraceCards, FTraceDistantScene, FOutputIndividualRays, FRadianceCache>;

	static FPermutationDomain RemapPermutation(FPermutationDomain PermutationVector)
	{
		return PermutationVector;
	}

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		FPermutationDomain PermutationVector(Parameters.PermutationId);

		if (RemapPermutation(PermutationVector) != PermutationVector)
		{
			return false;
		}

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
		OutEnvironment.CompilerFlags.Add(CFLAG_Wave32);
		OutEnvironment.CompilerFlags.Add(CFLAG_AllowTypedUAVLoads);
	}
};

IMPLEMENT_GLOBAL_SHADER(FDiffuseIndirectTraceVoxelsCS, "/Engine/Private/Lumen/LumenDiffuseIndirect.usf", "DiffuseIndirectTraceVoxelsCS", SF_Compute);

class FDiffuseIndirectFilterPS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FDiffuseIndirectFilterPS)
	SHADER_USE_PARAMETER_STRUCT(FDiffuseIndirectFilterPS, FGlobalShader)

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		RENDER_TARGET_BINDING_SLOTS()
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, DiffuseIndirect0)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, DiffuseIndirect1)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, DownsampledDepth)
		SHADER_PARAMETER_SAMPLER(SamplerState, DiffuseIndirectSampler)
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER_STRUCT_REF(FSceneTexturesUniformParameters, SceneTexturesStruct)
		SHADER_PARAMETER(FVector2D, DiffuseIndirectTexelSize)
		SHADER_PARAMETER(FVector2D, MaxDiffuseIndirectBufferUV)
		SHADER_PARAMETER(int32, DownscaleFactor)
	END_SHADER_PARAMETER_STRUCT()

	using FPermutationDomain = TShaderPermutationDomain<>;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportLumenGI(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("DOWNSAMPLE_FACTOR"), 2);
		OutEnvironment.SetDefine(TEXT("DIFFUSE_TRACE_CARDS"), 1);
	}
};

IMPLEMENT_GLOBAL_SHADER(FDiffuseIndirectFilterPS, "/Engine/Private/Lumen/LumenDiffuseIndirect.usf", "DiffuseIndirectFilterPS", SF_Pixel);


class FUpdateHistoryDiffuseIndirectPS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FUpdateHistoryDiffuseIndirectPS)
	SHADER_USE_PARAMETER_STRUCT(FUpdateHistoryDiffuseIndirectPS, FGlobalShader)

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		RENDER_TARGET_BINDING_SLOTS()
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER(int32, DownscaleFactor)
		SHADER_PARAMETER(float,HistoryDistanceThreshold)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, DiffuseIndirectHistory0)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, DiffuseIndirectHistory1)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, DiffuseIndirectDepthHistory)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, HistoryConvergence)
		SHADER_PARAMETER(float,HistoryWeight)
		SHADER_PARAMETER(float,HistoryConvergenceWeight)
		SHADER_PARAMETER(float,PrevInvPreExposure)
		SHADER_PARAMETER(FVector2D,InvDiffuseIndirectBufferSize)
		SHADER_PARAMETER(FVector4,HistoryScreenPositionScaleBias)
		SHADER_PARAMETER(FVector4,HistoryUVMinMax)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, VelocityTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, VelocityTextureSampler)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, DiffuseIndirect0)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, DiffuseIndirect1)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, DownsampledDepth)
		SHADER_PARAMETER_SAMPLER(SamplerState, DiffuseIndirectSampler)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportLumenGI(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("DOWNSAMPLE_FACTOR"), 2);
		OutEnvironment.SetDefine(TEXT("DIFFUSE_TRACE_CARDS"), 1);
	}
};

IMPLEMENT_GLOBAL_SHADER(FUpdateHistoryDiffuseIndirectPS, "/Engine/Private/Lumen/LumenDiffuseIndirect.usf", "UpdateHistoryDepthRejectionPS", SF_Pixel);


class FLowConvergenceSpatialFilterPS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FLowConvergenceSpatialFilterPS)
	SHADER_USE_PARAMETER_STRUCT(FLowConvergenceSpatialFilterPS, FGlobalShader)

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		RENDER_TARGET_BINDING_SLOTS()
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, DiffuseIndirect0)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, DiffuseIndirect1)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, DownsampledDepth)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, ConvergenceTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, DiffuseIndirectSampler)
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER(FVector2D, DiffuseIndirectTexelSize)
		SHADER_PARAMETER(FVector2D, MaxDiffuseIndirectBufferUV)
		SHADER_PARAMETER(float, HistoryWeight)
		SHADER_PARAMETER(float, LowConvergenceSpatialFilterSpread)
	END_SHADER_PARAMETER_STRUCT()

	using FPermutationDomain = TShaderPermutationDomain<>;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportLumenGI(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("DOWNSAMPLE_FACTOR"), 2);
		OutEnvironment.SetDefine(TEXT("DIFFUSE_TRACE_CARDS"), 1);
	}
};

IMPLEMENT_GLOBAL_SHADER(FLowConvergenceSpatialFilterPS, "/Engine/Private/Lumen/LumenDiffuseIndirect.usf", "LowConvergenceSpatialFilterPS", SF_Pixel);


class FUpsampleDiffuseIndirectPS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FUpsampleDiffuseIndirectPS)
	SHADER_USE_PARAMETER_STRUCT(FUpsampleDiffuseIndirectPS, FGlobalShader)

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		RENDER_TARGET_BINDING_SLOTS()
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, DiffuseIndirect0)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, DiffuseIndirect1)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, DownsampledDepth)
		SHADER_PARAMETER_SAMPLER(SamplerState, DiffuseIndirectSampler)
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER_STRUCT_REF(FSceneTexturesUniformParameters, SceneTexturesStruct)
		SHADER_PARAMETER(int, bVisualizeDiffuseIndirect)
	END_SHADER_PARAMETER_STRUCT()

	class FUpsampleRequired : SHADER_PERMUTATION_BOOL("UPSAMPLE_REQUIRED");

	using FPermutationDomain = TShaderPermutationDomain<FUpsampleRequired>;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportLumenGI(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("DOWNSAMPLE_FACTOR"), 2);
		OutEnvironment.SetDefine(TEXT("DIFFUSE_TRACE_CARDS"), 1);
	}
};

IMPLEMENT_GLOBAL_SHADER(FUpsampleDiffuseIndirectPS, "/Engine/Private/Lumen/LumenDiffuseIndirect.usf", "UpsampleDiffuseIndirectPS", SF_Pixel);

void AllocateDiffuseIndirectTargets(
	FRDGBuilder& GraphBuilder,
	const HybridIndirectLighting::FCommonParameters& CommonDiffuseParameters, 
	uint32 TargetableFlags, FRDGTextureRef* OutDiffuseIndirect)
{
	LLM_SCOPE(ELLMTag::Lumen);

	FPooledRenderTargetDesc DiffuseIndirectDesc0;
	FPooledRenderTargetDesc DiffuseIndirectDesc1;

	//@todo DynamicGI - should be able to use PF_FloatR11G11B10 here, but it changes brightness
	EPixelFormat Vector0Format = PF_FloatRGBA; //PF_FloatR11G11B10;
	DiffuseIndirectDesc0 = FPooledRenderTargetDesc::Create2DDesc(CommonDiffuseParameters.TracingViewportBufferSize, Vector0Format, FClearValueBinding::Black, TexCreate_None, TexCreate_ShaderResource | TargetableFlags, false);
	DiffuseIndirectDesc1 = FPooledRenderTargetDesc::Create2DDesc(CommonDiffuseParameters.TracingViewportBufferSize, PF_R8G8B8A8, FClearValueBinding::Black, TexCreate_None, TexCreate_ShaderResource | TargetableFlags, false);

	OutDiffuseIndirect[0] = GraphBuilder.CreateTexture(DiffuseIndirectDesc0, TEXT("DiffuseIndirect0"));
	OutDiffuseIndirect[1] = GraphBuilder.CreateTexture(DiffuseIndirectDesc1, TEXT("DiffuseIndirect1"));
}

void UpdateHistory(
	FRDGBuilder& GraphBuilder,
	const HybridIndirectLighting::FCommonParameters& CommonDiffuseParameters,
	const FViewInfo& View, 
	FRDGTextureRef DiffuseIndirect[NumLumenDiffuseIndirectTextures],
	FRDGTextureRef DownsampledDepth,
	FIntRect* DiffuseIndirectHistoryViewRect,
	FVector4* DiffuseIndirectHistoryScreenPositionScaleBias,
	/** Contains last frame's history, if non-NULL.  This will be updated with the new frame's history. */
	TRefCountPtr<IPooledRenderTarget>* DiffuseIndirectHistoryState[NumLumenDiffuseIndirectTextures],
	TRefCountPtr<IPooledRenderTarget>* DownsampledDepthHistoryState,
	TRefCountPtr<IPooledRenderTarget>* HistoryConvergenceState,
	/** Output of Temporal Reprojection for the next step in the pipeline. */
	FRDGTextureRef* DiffuseIndirectHistoryOutput)
{
	LLM_SCOPE(ELLMTag::Lumen);

	if (DiffuseIndirectHistoryState[0] && GLumenDiffuseUseHistory)
	{
		FIntPoint BufferSize = CommonDiffuseParameters.TracingViewportBufferSize;
		const FIntRect NewHistoryViewRect = FIntRect(FIntPoint(0, 0), FIntPoint::DivideAndRoundDown(View.ViewRect.Size(), CommonDiffuseParameters.DownscaleFactor));

		if (*DiffuseIndirectHistoryState[0]
			&& !View.bCameraCut 
			&& !View.bPrevTransformsReset
			&& !GLumenDiffuseClearHistory
			// If the scene render targets reallocate, toss the history so we don't read uninitialized data
			&& (*DiffuseIndirectHistoryState[0])->GetDesc().Extent == BufferSize)
		{
			FRDGTextureRef NewDiffuseIndirectHistory[NumLumenDiffuseIndirectTextures];
			AllocateDiffuseIndirectTargets(GraphBuilder, CommonDiffuseParameters, TexCreate_RenderTargetable, NewDiffuseIndirectHistory);

			FPooledRenderTargetDesc DownsampledDepthHistoryDesc(FPooledRenderTargetDesc::Create2DDesc(BufferSize, PF_R32_FLOAT, FClearValueBinding::Black, TexCreate_None, TexCreate_ShaderResource | TexCreate_RenderTargetable, false));
			FRDGTextureRef NewDownsampledDepthHistory = GraphBuilder.CreateTexture(DownsampledDepthHistoryDesc, TEXT("DownsampledDepthHistory"));

			FPooledRenderTargetDesc HistoryConvergenceDesc(FPooledRenderTargetDesc::Create2DDesc(BufferSize, PF_G8, FClearValueBinding::Black, TexCreate_None, TexCreate_ShaderResource | TexCreate_RenderTargetable, false));
			FRDGTextureRef NewHistoryConvergence = GraphBuilder.CreateTexture(HistoryConvergenceDesc, TEXT("HistoryConvergence"));

			{
				FRDGTextureRef OldDiffuseIndirectHistory[NumLumenDiffuseIndirectTextures];
				OldDiffuseIndirectHistory[0] = GraphBuilder.RegisterExternalTexture(*DiffuseIndirectHistoryState[0]);
				OldDiffuseIndirectHistory[1] = GraphBuilder.RegisterExternalTexture(*DiffuseIndirectHistoryState[1]);
				FRDGTextureRef OldDownsampledDepthHistory = GraphBuilder.RegisterExternalTexture(*DownsampledDepthHistoryState);
				FRDGTextureRef OldHistoryConvergence = GraphBuilder.RegisterExternalTexture(*HistoryConvergenceState);

				auto PixelShader = View.ShaderMap->GetShader<FUpdateHistoryDiffuseIndirectPS>();

				FUpdateHistoryDiffuseIndirectPS::FParameters* PassParameters = GraphBuilder.AllocParameters<FUpdateHistoryDiffuseIndirectPS::FParameters>();
				PassParameters->RenderTargets[0] = FRenderTargetBinding(NewDiffuseIndirectHistory[0], ERenderTargetLoadAction::ENoAction);
				PassParameters->RenderTargets[1] = FRenderTargetBinding(NewDiffuseIndirectHistory[1], ERenderTargetLoadAction::ENoAction);
				PassParameters->RenderTargets[2] = FRenderTargetBinding(NewDownsampledDepthHistory, ERenderTargetLoadAction::ENoAction);
				PassParameters->RenderTargets[3] = FRenderTargetBinding(NewHistoryConvergence, ERenderTargetLoadAction::ENoAction);
				PassParameters->View = View.ViewUniformBuffer;
				PassParameters->DownscaleFactor = CommonDiffuseParameters.DownscaleFactor;
				PassParameters->HistoryDistanceThreshold = GLumenDiffuseHistoryDistanceThreshold;
				PassParameters->DiffuseIndirectHistory0 = OldDiffuseIndirectHistory[0];
				PassParameters->DiffuseIndirectHistory1 = OldDiffuseIndirectHistory[1];
				PassParameters->DiffuseIndirectDepthHistory = OldDownsampledDepthHistory;
				PassParameters->HistoryConvergence  = OldHistoryConvergence;
				PassParameters->HistoryWeight = GLumenDiffuseHistoryWeight;
				PassParameters->HistoryConvergenceWeight = GLumenDiffuseHistoryConvergenceWeight;
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

				PassParameters->VelocityTexture = CommonDiffuseParameters.SceneTextures.SceneVelocityBuffer;
				PassParameters->VelocityTextureSampler = TStaticSamplerState<SF_Bilinear>::GetRHI();
				PassParameters->DiffuseIndirect0 = DiffuseIndirect[0];
				PassParameters->DiffuseIndirect1 = DiffuseIndirect[1];
				PassParameters->DownsampledDepth = DownsampledDepth;
				PassParameters->DiffuseIndirectSampler = TStaticSamplerState<SF_Bilinear>::GetRHI();
				
				FPixelShaderUtils::AddFullscreenPass(
					GraphBuilder,
					View.ShaderMap,
					RDG_EVENT_NAME("UpdateHistory"),
					PixelShader,
					PassParameters,
					NewHistoryViewRect);

				// Queue updating the view state's render target reference with the new history
				GraphBuilder.QueueTextureExtraction(NewDiffuseIndirectHistory[0], DiffuseIndirectHistoryState[0]);
				GraphBuilder.QueueTextureExtraction(NewDiffuseIndirectHistory[1], DiffuseIndirectHistoryState[1]);
				GraphBuilder.QueueTextureExtraction(NewDownsampledDepthHistory, DownsampledDepthHistoryState);
				GraphBuilder.QueueTextureExtraction(NewHistoryConvergence, HistoryConvergenceState);
			}

			if (GLumenDiffuseLowConvergenceSpatialFilter)
			{
				FRDGTextureRef FilteredDiffuseIndirect[NumLumenDiffuseIndirectTextures];
				AllocateDiffuseIndirectTargets(GraphBuilder, CommonDiffuseParameters, TexCreate_RenderTargetable, FilteredDiffuseIndirect);
				auto PixelShader = View.ShaderMap->GetShader<FLowConvergenceSpatialFilterPS>();

				FLowConvergenceSpatialFilterPS::FParameters* PassParameters = GraphBuilder.AllocParameters<FLowConvergenceSpatialFilterPS::FParameters>();
				PassParameters->RenderTargets[0] = FRenderTargetBinding(FilteredDiffuseIndirect[0], ERenderTargetLoadAction::ENoAction);
				PassParameters->RenderTargets[1] = FRenderTargetBinding(FilteredDiffuseIndirect[1], ERenderTargetLoadAction::ENoAction);
				PassParameters->DiffuseIndirect0 = NewDiffuseIndirectHistory[0];
				PassParameters->DiffuseIndirect1 = NewDiffuseIndirectHistory[1];
				PassParameters->DownsampledDepth = DownsampledDepth;
				PassParameters->ConvergenceTexture = NewHistoryConvergence;
				PassParameters->DiffuseIndirectSampler = TStaticSamplerState<SF_Bilinear>::GetRHI();
				PassParameters->View = View.ViewUniformBuffer;
				PassParameters->DiffuseIndirectTexelSize = FVector2D(1.0f / BufferSize.X, 1.0f / BufferSize.Y);

				const FIntRect HalfResViewRect = FIntRect(FIntPoint(0, 0), CommonDiffuseParameters.TracingViewportSize);

				PassParameters->MaxDiffuseIndirectBufferUV = FVector2D(
					(HalfResViewRect.Width() - 0.5f) / BufferSize.X,
					(HalfResViewRect.Height() - 0.5f) / BufferSize.Y);

				PassParameters->HistoryWeight = GLumenDiffuseHistoryWeight;
				PassParameters->LowConvergenceSpatialFilterSpread = GLumenDiffuseLowConvergenceSpatialFilterSpread;

				FPixelShaderUtils::AddFullscreenPass(
					GraphBuilder,
					View.ShaderMap,
					RDG_EVENT_NAME("LowConvergenceSpatialFilter"),
					PixelShader,
					PassParameters,
					HalfResViewRect);

				DiffuseIndirectHistoryOutput[0] = FilteredDiffuseIndirect[0];
				DiffuseIndirectHistoryOutput[1] = FilteredDiffuseIndirect[1];
			}
			else
			{
				DiffuseIndirectHistoryOutput[0] = NewDiffuseIndirectHistory[0];
				DiffuseIndirectHistoryOutput[1] = NewDiffuseIndirectHistory[1];
			}
		}
		else
		{
			// Tossed the history for one frame, seed next frame's history with this frame's output

			// Queue updating the view state's render target reference with the new values
			GraphBuilder.QueueTextureExtraction(DiffuseIndirect[0], DiffuseIndirectHistoryState[0]);
			GraphBuilder.QueueTextureExtraction(DiffuseIndirect[1], DiffuseIndirectHistoryState[1]);
			GraphBuilder.QueueTextureExtraction(DownsampledDepth, DownsampledDepthHistoryState);
			*HistoryConvergenceState = GSystemTextures.BlackDummy;
			DiffuseIndirectHistoryOutput[0] = DiffuseIndirect[0];
			DiffuseIndirectHistoryOutput[1] = DiffuseIndirect[1];
		}

		*DiffuseIndirectHistoryViewRect = NewHistoryViewRect;
		*DiffuseIndirectHistoryScreenPositionScaleBias = View.GetScreenPositionScaleBias(FSceneRenderTargets::Get_FrameConstantsOnly().GetBufferSizeXY(), View.ViewRect);
	}
	else
	{
		// Temporal reprojection is disabled or there is no view state - pass through
		DiffuseIndirectHistoryOutput[0] = DiffuseIndirect[0];
		DiffuseIndirectHistoryOutput[1] = DiffuseIndirect[1];
	}
}

bool ShouldRenderLumenDiffuseGI(EShaderPlatform ShaderPlatform, const FSceneViewFamily& ViewFamily)
{
	static const auto CVar = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.GenerateMeshDistanceFields"));

	return GAllowLumenScene
		&& DoesPlatformSupportLumenGI(ShaderPlatform)
		&& ViewFamily.EngineShowFlags.LumenDiffuseIndirect
		&& CVar->GetValueOnRenderThread() != 0;
}

bool FDeferredShadingSceneRenderer::ShouldRenderLumenDiffuseGI(const FViewInfo& View) const
{
	FLumenSceneData& LumenSceneData = *Scene->LumenSceneData;

	return ::ShouldRenderLumenDiffuseGI(ShaderPlatform, ViewFamily)
		&& Views.Num() == 1
		&& (LumenSceneData.VisibleCardsIndices.Num() > 0 || ShouldRenderDynamicSkyLight(Scene, ViewFamily))
		&& LumenSceneData.AlbedoAtlas
		&& ViewFamily.EngineShowFlags.GlobalIllumination
		//@todo - support GI in secondary views without updating the scene
		&& !View.bIsPlanarReflection 
		&& !View.bIsSceneCapture
		&& !View.bIsReflectionCapture
		&& View.ViewState;
}

extern FHemisphereDirectionSampleGenerator DiffuseGIDirections;

void SetupLumenDiffuseTracingParameters(FLumenIndirectTracingParameters& OutParameters)
{
	OutParameters.StepFactor = FMath::Clamp(GDiffuseTraceStepFactor, .1f, 10.0f);
	OutParameters.VoxelStepFactor = FMath::Clamp(GLumenDiffuseVoxelStepFactor, .1f, 10.0f);
	OutParameters.CardTraceEndDistanceFromCamera = GDiffuseCardTraceEndDistanceFromCamera;
	OutParameters.MinSampleRadius = FMath::Clamp(GLumenDiffuseMinSampleRadius, .01f, 100.0f);
	OutParameters.MinTraceDistance = FMath::Clamp(GLumenDiffuseMinTraceDistance, .01f, 1000.0f);
	OutParameters.MaxTraceDistance = Lumen::GetMaxTraceDistance();
	OutParameters.MaxCardTraceDistance = FMath::Clamp(GLumenDiffuseMaxCardTraceDistance, OutParameters.MinTraceDistance, OutParameters.MaxTraceDistance);
	OutParameters.SurfaceBias = FMath::Clamp(GLumenDiffuseSurfaceBias, .01f, 100.0f);
	OutParameters.CardInterpolateInfluenceRadius = FMath::Clamp(GLumenDiffuseCardInterpolateInfluenceRadius, .01f, 1000.0f);
	OutParameters.DiffuseConeHalfAngle = DiffuseGIDirections.ConeHalfAngle * GLumenDiffuseConeAngleScale;
	OutParameters.TanDiffuseConeHalfAngle = FMath::Tan(OutParameters.DiffuseConeHalfAngle);
	OutParameters.SpecularFromDiffuseRoughnessStart = 0.0f;
	OutParameters.SpecularFromDiffuseRoughnessEnd = 0.0f;
}

void SetupLumenDiffuseTracingParametersForProbe(FLumenIndirectTracingParameters& OutParameters, float DiffuseConeHalfAngle)
{
	SetupLumenDiffuseTracingParameters(OutParameters);

	// Probe tracing doesn't have surface bias, but should bias MinTraceDistance due to the mesh SDF world space error
	OutParameters.SurfaceBias = 0.0f;
	OutParameters.MinTraceDistance = FMath::Clamp(FMath::Max(GLumenDiffuseSurfaceBias, GLumenDiffuseMinTraceDistance), .01f, 1000.0f);

	if (DiffuseConeHalfAngle >= 0.0f)
	{
		OutParameters.DiffuseConeHalfAngle = DiffuseConeHalfAngle;
		OutParameters.TanDiffuseConeHalfAngle = FMath::Tan(DiffuseConeHalfAngle);
	}
}

// TODO(Guillaume): Merge with denoiser's existing code path.
static void DownscaleDepthAndNormalForLumen(
	FRDGBuilder& GraphBuilder,
	const FViewInfo& View,
	const HybridIndirectLighting::FCommonParameters& CommonDiffuseParameters,
	FRDGTextureRef& OutDownsampledDepth,
	FRDGTextureRef& OutDownsampledNormal)
{
	LLM_SCOPE(ELLMTag::Lumen);

	// TODO(Guillaume): Merge with denoiser's existing code path.
	FPooledRenderTargetDesc DownsampledDepthDesc(FPooledRenderTargetDesc::Create2DDesc(CommonDiffuseParameters.TracingViewportBufferSize, PF_R32_FLOAT, FClearValueBinding::Black, TexCreate_None, TexCreate_ShaderResource | TexCreate_RenderTargetable, false));
	OutDownsampledDepth = GraphBuilder.CreateTexture(DownsampledDepthDesc, TEXT("DownsampledDepth"));

	FPooledRenderTargetDesc DownsampledNormalDesc(FPooledRenderTargetDesc::Create2DDesc(CommonDiffuseParameters.TracingViewportBufferSize, PF_R8G8B8A8, FClearValueBinding::Black, TexCreate_None, TexCreate_ShaderResource | TexCreate_RenderTargetable, false));
	OutDownsampledNormal = GraphBuilder.CreateTexture(DownsampledNormalDesc, TEXT("DownsampledNormal"));

	FDownsampleDepthAndNormalPS::FParameters* PassParameters = GraphBuilder.AllocParameters<FDownsampleDepthAndNormalPS::FParameters>();
	PassParameters->RenderTargets[0] = FRenderTargetBinding(OutDownsampledDepth, ERenderTargetLoadAction::ENoAction);
	PassParameters->RenderTargets[1] = FRenderTargetBinding(OutDownsampledNormal, ERenderTargetLoadAction::ENoAction);
	PassParameters->View = View.ViewUniformBuffer;
	PassParameters->SceneTexturesStruct = CreateSceneTextureUniformBufferSingleDraw(GraphBuilder.RHICmdList, ESceneTextureSetupMode::All, View.FeatureLevel);
	PassParameters->DownscaleFactor = CommonDiffuseParameters.DownscaleFactor;
	PassParameters->SourceViewMax = View.ViewRect.Size() - FIntPoint(1, 1);

	FDownsampleDepthAndNormalPS::FPermutationDomain PermutationVector;
	PermutationVector.Set<FDownsampleDepthAndNormalPS::FDownsampleFilter>(GLumenGBufferDownsampleFilter != 0);
	auto PixelShader = View.ShaderMap->GetShader<FDownsampleDepthAndNormalPS>(PermutationVector);

	const FIntRect DownsampledViewRect = FIntRect(FIntPoint(0, 0), CommonDiffuseParameters.TracingViewportSize);
	// TODO(Guillaume): const FIntRect DownsampledViewRect = FIntRect(FIntPoint(0, 0), FIntPoint::DivideAndRoundDown(View.ViewRect.Size(), GetDiffuseDownsampleFactor()));

	FPixelShaderUtils::AddFullscreenPass(
		GraphBuilder,
		View.ShaderMap,
		RDG_EVENT_NAME("DownsampleDepthAndNormal"),
		PixelShader,
		PassParameters,
		DownsampledViewRect);
}

void CullForCardTracing(
	FRDGBuilder& GraphBuilder,
	const FScene* Scene,
	const FViewInfo& View,
	FLumenCardTracingInputs TracingInputs,
	const FLumenDiffuseTracingParameters& DiffuseTracingParameters,
	FLumenCardFroxelGridParameters& GridParameters,
	FLumenMeshSDFGridParameters& MeshSDFGridParameters)
{
	LLM_SCOPE(ELLMTag::Lumen);

	extern int32 GCardFroxelGridPixelSize;
	extern void GetCardGridZParams(float NearPlane, float FarPlane, FVector& OutZParams, int32& OutGridSizeZ);

	const FLumenIndirectTracingParameters& IndirectTracingParameters = DiffuseTracingParameters.IndirectTracingParameters;

	FVector ZParams;
	int32 CardGridSizeZ;
	GetCardGridZParams(View.NearClippingDistance, IndirectTracingParameters.CardTraceEndDistanceFromCamera, ZParams, CardGridSizeZ);

	{
		GridParameters.CardGridPixelSizeShift = FMath::FloorLog2(GCardFroxelGridPixelSize);
		GridParameters.CardGridZParams = ZParams;

		const FIntPoint CardGridSizeXY = FIntPoint::DivideAndRoundUp(View.ViewRect.Size(), GCardFroxelGridPixelSize);
		const FIntVector CullGridSize(CardGridSizeXY.X, CardGridSizeXY.Y, CardGridSizeZ);
		GridParameters.CullGridSize = CullGridSize;
	}

	if (GLumenDiffuseCardTraceMeshSDF)
	{
		FLumenMeshSDFGridCompactParameters GridCompactParameters;

		CullMeshSDFObjectsToViewGrid(
			View,
			Scene,
			IndirectTracingParameters.MaxCardTraceDistance,
			IndirectTracingParameters.CardTraceEndDistanceFromCamera,
			GCardFroxelGridPixelSize,
			CardGridSizeZ,
			ZParams,
			GraphBuilder,
			MeshSDFGridParameters,
			GridCompactParameters);

		CullMeshSDFObjectGridToGBuffer(
			View,
			Scene,
			IndirectTracingParameters.MaxCardTraceDistance,
			IndirectTracingParameters.CardTraceEndDistanceFromCamera,
			DiffuseTracingParameters.CommonDiffuseParameters,
			DiffuseTracingParameters.DownsampledDepth,
			GCardFroxelGridPixelSize,
			CardGridSizeZ,
			ZParams,
			GraphBuilder,
			MeshSDFGridParameters,
			GridCompactParameters);
	}
	else
	{
		CullLumenCardsToFroxelGrid(
			View,
			TracingInputs,
			IndirectTracingParameters.TanDiffuseConeHalfAngle,
			IndirectTracingParameters.MinTraceDistance,
			IndirectTracingParameters.MaxTraceDistance,
			IndirectTracingParameters.MaxCardTraceDistance,
			IndirectTracingParameters.CardTraceEndDistanceFromCamera,
			DiffuseTracingParameters.CommonDiffuseParameters.DownscaleFactor,
			DiffuseTracingParameters.DownsampledDepth,
			GraphBuilder,
			GridParameters);
	}
}

DECLARE_GPU_STAT(LumenDiffuseGI);

void FDeferredShadingSceneRenderer::RenderLumenDiffuseGI(
	FRDGBuilder& GraphBuilder,
	const HybridIndirectLighting::FCommonParameters& CommonDiffuseParameters,
	const FViewInfo& View,
	bool bResumeRays,
	FRDGTextureRef SceneColor,
	FRDGTextureRef RoughSpecularIndirect)
{
	LLM_SCOPE(ELLMTag::Lumen);
	RDG_EVENT_SCOPE(GraphBuilder, "LumenDiffuseGI");
	RDG_GPU_STAT_SCOPE(GraphBuilder, LumenDiffuseGI);

	check(ShouldRenderLumenDiffuseGI(View));

	FRDGTextureRef DownsampledDepth;
	FRDGTextureRef DownsampledNormal;
	DownscaleDepthAndNormalForLumen(
		GraphBuilder,
		View,
		CommonDiffuseParameters,
		/* out */ DownsampledDepth,
		/* out */ DownsampledNormal);

	FLumenCardTracingInputs TracingInputs(GraphBuilder, Scene, View);

	LumenRadianceCache::FRadianceCacheParameters RadianceCacheParameters;
	RenderRadianceCache(GraphBuilder, TracingInputs, View, nullptr, RadianceCacheParameters);

	FLumenDiffuseTracingParameters DiffuseTracingParameters;
	SetupLumenDiffuseTracingParameters(/* out */ DiffuseTracingParameters.IndirectTracingParameters);
	DiffuseTracingParameters.CommonDiffuseParameters = CommonDiffuseParameters;
	DiffuseTracingParameters.SampleWeight = (GLumenDiffuseIntensity * 2.0f * PI) / float(CommonDiffuseParameters.RayCountPerPixel);
	DiffuseTracingParameters.DownsampledNormal = DownsampledNormal;
	DiffuseTracingParameters.DownsampledDepth = DownsampledDepth;

	const bool bTraceCards = GLumenDiffuseTraceCards && Scene->DistanceFieldSceneData.NumObjectsInBuffer > 0;

	if (bTraceCards)
	{
		FLumenCardFroxelGridParameters GridParameters;
		FLumenMeshSDFGridParameters MeshSDFGridParameters;

		CullForCardTracing(
			GraphBuilder,
			Scene, View,
			TracingInputs,
			DiffuseTracingParameters,
			/* out */ GridParameters,
			/* out */ MeshSDFGridParameters);

		{
			FDiffuseIndirectTraceCardsCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FDiffuseIndirectTraceCardsCS::FParameters>();
			GetLumenCardTracingParameters(View, TracingInputs, PassParameters->TracingParameters);
			PassParameters->GridParameters = GridParameters;
			PassParameters->DiffuseTracingParameters = DiffuseTracingParameters;
			PassParameters->MeshSDFGridParameters = MeshSDFGridParameters;

			extern int32 GLumenGIDiffuseIndirectBVHCulling;
			FDiffuseIndirectTraceCardsCS::FPermutationDomain PermutationVector;
			PermutationVector.Set< FDiffuseIndirectTraceCardsCS::FCulledCardsGrid >(GLumenGIDiffuseIndirectBVHCulling != 0);
			PermutationVector.Set< FDiffuseIndirectTraceCardsCS::FCardTraceMeshSDF >(GLumenDiffuseCardTraceMeshSDF != 0);
			PermutationVector.Set< FDiffuseIndirectTraceCardsCS::FCubeMapTree >(GLumenDiffuseCubeMapTree != 0);
			PermutationVector.Set< FDiffuseIndirectTraceCardsCS::FResumeRays >(bResumeRays);
			PermutationVector = FDiffuseIndirectTraceCardsCS::RemapPermutation(PermutationVector);

			auto ComputeShader = View.ShaderMap->GetShader<FDiffuseIndirectTraceCardsCS>(PermutationVector);

			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("ConeTraceCards %ux%u Res %u Cones %.1f ConeAngle",
					CommonDiffuseParameters.TracingViewportSize.X,
					CommonDiffuseParameters.TracingViewportSize.Y,
					CommonDiffuseParameters.RayCountPerPixel,
					DiffuseTracingParameters.IndirectTracingParameters.DiffuseConeHalfAngle * 180.0f / PI),
				ComputeShader,
				PassParameters,
				FComputeShaderUtils::GetGroupCount(CommonDiffuseParameters.TracingViewportSize, FDiffuseIndirectTraceCardsCS::GetGroupSize()));
		}
	}

	// If there is scene color, use Lumen's post processing to output to it.
	const bool bOutputIndiviualRays = SceneColor == nullptr;

	FRDGTextureRef DiffuseIndirect[NumLumenDiffuseIndirectTextures];
	{
		AllocateDiffuseIndirectTargets(GraphBuilder, CommonDiffuseParameters, TexCreate_UAV, DiffuseIndirect);
		FRDGTextureUAVRef DiffuseIndirect0UAV = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(DiffuseIndirect[0]));
		FRDGTextureUAVRef DiffuseIndirect1UAV = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(DiffuseIndirect[1]));

		FDiffuseIndirectTraceVoxelsCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FDiffuseIndirectTraceVoxelsCS::FParameters>();
		PassParameters->RWDiffuseIndirect0 = DiffuseIndirect0UAV;
		PassParameters->RWDiffuseIndirect1 = DiffuseIndirect1UAV;

		PassParameters->RadianceCacheParameters = RadianceCacheParameters;
		GetLumenCardTracingParameters(View, TracingInputs, PassParameters->TracingParameters);
		PassParameters->DiffuseTracingParameters = DiffuseTracingParameters;

		const bool bRadianceCache = LumenRadianceCache::IsEnabled(View);

		FDiffuseIndirectTraceVoxelsCS::FPermutationDomain PermutationVector;
		PermutationVector.Set< FDiffuseIndirectTraceVoxelsCS::FVoxelTracingMode >(Lumen::GetVoxelTracingMode());
		PermutationVector.Set< FDiffuseIndirectTraceVoxelsCS::FDynamicSkyLight >(ShouldRenderDynamicSkyLight(Scene, ViewFamily));
		PermutationVector.Set< FDiffuseIndirectTraceVoxelsCS::FTraceCards >(bTraceCards || bResumeRays);
		PermutationVector.Set< FDiffuseIndirectTraceVoxelsCS::FTraceDistantScene >(Scene->LumenSceneData->DistantCardIndices.Num() > 0);
		PermutationVector.Set< FDiffuseIndirectTraceVoxelsCS::FOutputIndividualRays >(bOutputIndiviualRays);
		PermutationVector.Set< FDiffuseIndirectTraceVoxelsCS::FRadianceCache >(bRadianceCache);
		auto ComputeShader = View.ShaderMap->GetShader<FDiffuseIndirectTraceVoxelsCS>(PermutationVector);

		FIntPoint GroupSize(FIntPoint::DivideAndRoundUp(CommonDiffuseParameters.TracingViewportSize, FDiffuseIndirectTraceVoxelsCS::GetGroupSize()));

		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("ConeTraceVoxels %ux%u Res %u Cones %.1f ConeAngle",
				CommonDiffuseParameters.TracingViewportSize.X,
				CommonDiffuseParameters.TracingViewportSize.Y,
				CommonDiffuseParameters.RayCountPerPixel,
				DiffuseTracingParameters.IndirectTracingParameters.DiffuseConeHalfAngle * 180.0f / PI),
			ComputeShader,
			PassParameters,
			FIntVector(GroupSize.X, GroupSize.Y, 1));
	}

	if (bOutputIndiviualRays)
	{
		return;
	}

	if (GLumenDiffuseSpatialFilter) // && HybridIndirectLighting::kInterleavingTileSize > 1)
	{
		FIntPoint BufferSize = CommonDiffuseParameters.TracingViewportBufferSize;
		FRDGTextureRef FilteredDiffuseIndirect[NumLumenDiffuseIndirectTextures];
		AllocateDiffuseIndirectTargets(GraphBuilder, CommonDiffuseParameters, TexCreate_RenderTargetable, FilteredDiffuseIndirect);
		auto PixelShader = View.ShaderMap->GetShader<FDiffuseIndirectFilterPS>();

		FDiffuseIndirectFilterPS::FParameters* PassParameters = GraphBuilder.AllocParameters<FDiffuseIndirectFilterPS::FParameters>();
		PassParameters->RenderTargets[0] = FRenderTargetBinding(FilteredDiffuseIndirect[0], ERenderTargetLoadAction::ENoAction);
		PassParameters->RenderTargets[1] = FRenderTargetBinding(FilteredDiffuseIndirect[1], ERenderTargetLoadAction::ENoAction);
		PassParameters->DiffuseIndirect0 = DiffuseIndirect[0];
		PassParameters->DiffuseIndirect1 = DiffuseIndirect[1];
		PassParameters->DownsampledDepth = DownsampledDepth;
		PassParameters->DiffuseIndirectSampler = TStaticSamplerState<SF_Bilinear>::GetRHI();
		PassParameters->View = View.ViewUniformBuffer;
		PassParameters->SceneTexturesStruct = CreateSceneTextureUniformBufferSingleDraw(GraphBuilder.RHICmdList, ESceneTextureSetupMode::All, View.FeatureLevel);
		PassParameters->DiffuseIndirectTexelSize = FVector2D(1.0f / BufferSize.X, 1.0f / BufferSize.Y);

		const FIntRect HalfResViewRect = FIntRect(FIntPoint(0, 0), CommonDiffuseParameters.TracingViewportSize);

		PassParameters->MaxDiffuseIndirectBufferUV = FVector2D(
			(HalfResViewRect.Width() - 0.5f) / BufferSize.X,
			(HalfResViewRect.Height() - 0.5f) / BufferSize.Y);
		PassParameters->DownscaleFactor = CommonDiffuseParameters.DownscaleFactor;

		FPixelShaderUtils::AddFullscreenPass(
			GraphBuilder,
			View.ShaderMap,
			RDG_EVENT_NAME("Filter"),
			PixelShader,
			PassParameters,
			HalfResViewRect);

		DiffuseIndirect[0] = FilteredDiffuseIndirect[0];
		DiffuseIndirect[1] = FilteredDiffuseIndirect[1];
	}

	{
		FIntRect* HistoryViewRect = View.ViewState ? &View.ViewState->Lumen.DiffuseIndirectHistoryViewRect : nullptr;
		FVector4* HistoryScreenPositionScaleBias = View.ViewState ? &View.ViewState->Lumen.DiffuseIndirectHistoryScreenPositionScaleBias : nullptr;
		TRefCountPtr<IPooledRenderTarget>* DiffuseIndirectHistoryState[NumLumenDiffuseIndirectTextures];
		DiffuseIndirectHistoryState[0] = View.ViewState ? &View.ViewState->Lumen.DiffuseIndirectHistoryRT[0] : nullptr;
		DiffuseIndirectHistoryState[1] = View.ViewState ? &View.ViewState->Lumen.DiffuseIndirectHistoryRT[1] : nullptr;
		TRefCountPtr<IPooledRenderTarget>* DownsampledDepthHistoryState = View.ViewState ? &View.ViewState->Lumen.DownsampledDepthHistoryRT : nullptr;
		TRefCountPtr<IPooledRenderTarget>* HistoryConvergenceState = View.ViewState ? &View.ViewState->Lumen.HistoryConvergenceStateRT : nullptr;

		UpdateHistory(
			GraphBuilder,
			CommonDiffuseParameters,
			View,
			DiffuseIndirect,
			DownsampledDepth,
			HistoryViewRect,
			HistoryScreenPositionScaleBias,
			DiffuseIndirectHistoryState,
			DownsampledDepthHistoryState,
			HistoryConvergenceState,
			DiffuseIndirect);
	}

	{
		FUpsampleDiffuseIndirectPS::FPermutationDomain PermutationVector;
		PermutationVector.Set< FUpsampleDiffuseIndirectPS::FUpsampleRequired >(CommonDiffuseParameters.DownscaleFactor != 1);
		auto PixelShader = View.ShaderMap->GetShader<FUpsampleDiffuseIndirectPS>(PermutationVector);

		FUpsampleDiffuseIndirectPS::FParameters* PassParameters = GraphBuilder.AllocParameters<FUpsampleDiffuseIndirectPS::FParameters>();
		PassParameters->RenderTargets[0] = FRenderTargetBinding(SceneColor, ERenderTargetLoadAction::ELoad);
		PassParameters->RenderTargets[1] = FRenderTargetBinding(RoughSpecularIndirect, ERenderTargetLoadAction::ENoAction);
		PassParameters->DiffuseIndirect0 = DiffuseIndirect[0];
		PassParameters->DiffuseIndirect1 = DiffuseIndirect[1];
		PassParameters->DownsampledDepth = DownsampledDepth;
		PassParameters->DiffuseIndirectSampler = TStaticSamplerState<SF_Bilinear>::GetRHI();
		PassParameters->View = View.ViewUniformBuffer;
		PassParameters->SceneTexturesStruct = CreateSceneTextureUniformBufferSingleDraw(GraphBuilder.RHICmdList, ESceneTextureSetupMode::All, View.FeatureLevel);
		PassParameters->bVisualizeDiffuseIndirect = ViewFamily.EngineShowFlags.VisualizeLumenIndirectDiffuse;

		FPixelShaderUtils::AddFullscreenPass(
			GraphBuilder,
			View.ShaderMap,
			RDG_EVENT_NAME("Upsample"),
			PixelShader,
			PassParameters,
			View.ViewRect,
			PassParameters->bVisualizeDiffuseIndirect ? TStaticBlendState<>::GetRHI() : TStaticBlendState<CW_RGBA, BO_Add, BF_One, BF_One>::GetRHI());
	}
}
