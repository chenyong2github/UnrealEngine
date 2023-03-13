// Copyright Epic Games, Inc. All Rights Reserved.

#include "RendererPrivate.h"
#include "ScenePrivate.h"
#include "SceneUtils.h"
#include "PipelineStateCache.h"
#include "ShaderParameterStruct.h"
#include "PixelShaderUtils.h"
#include "ReflectionEnvironment.h"
#include "DistanceFieldAmbientOcclusion.h"
#include "SceneTextureParameters.h"
#include "IndirectLightRendering.h"
#include "LumenReflections.h"
#include "HairStrands/HairStrandsData.h"

#if RHI_RAYTRACING
#include "RayTracing/RayTracingDeferredMaterials.h"
#include "RayTracing/RaytracingOptions.h"
#include "RayTracing/RayTracingLighting.h"
#include "LumenHardwareRayTracingCommon.h"

static TAutoConsoleVariable<int32> CVarLumenReflectionsHardwareRayTracing(
	TEXT("r.Lumen.Reflections.HardwareRayTracing"),
	1,
	TEXT("Enables hardware ray tracing for Lumen reflections (Default = 1)"),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<int32> CVarLumenReflectionsHardwareRayTracingDefaultThreadCount(
	TEXT("r.Lumen.Reflections.HardwareRayTracing.Default.ThreadCount"),
	32768,
	TEXT("Determines the active number of threads (Default = 32768)"),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<int32> CVarLumenReflectionsHardwareRayTracingDefaultGroupCount(
	TEXT("r.Lumen.Reflections.HardwareRayTracing.Default.GroupCount"),
	1,
	TEXT("Determines the active number of groups (Default = 1)"),
	ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<int32> CVarLumenReflectionsHardwareRayTracingBucketMaterials(
	TEXT("r.Lumen.Reflections.HardwareRayTracing.BucketMaterials"),
	1,
	TEXT("Determines whether a secondary traces will be bucketed for coherent material access (default = 1"),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<int32> CVarLumenReflectionsHardwareRayTracingRetraceHitLighting(
	TEXT("r.Lumen.Reflections.HardwareRayTracing.Retrace.HitLighting"),
	0,
	TEXT("Determines whether a second trace will be fired for hit-lighting for invalid surface-cache hits (Default = 0)"),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<int32> CVarLumenReflectionsHardwareRayTracingRetraceFarField(
	TEXT("r.Lumen.Reflections.HardwareRayTracing.Retrace.FarField"),
	1,
	TEXT("Determines whether a second trace will be fired for far-field contribution (Default = 1)"),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<int32> CVarLumenReflectionsHardwareRayTracingRetraceThreadCount(
	TEXT("r.Lumen.Reflections.HardwareRayTracing.Retrace.ThreadCount"),
	32768,
	TEXT("Determines the active number of threads for re-traces (Default = 32768)"),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<int32> CVarLumenReflectionsHardwareRayTracingRetraceGroupCount(
	TEXT("r.Lumen.Reflections.HardwareRayTracing.Retrace.GroupCount"),
	1,
	TEXT("Determines the active number of groups for re-traces (Default = 1)"),
	ECVF_Scalability | ECVF_RenderThreadSafe
);
#endif // RHI_RAYTRACING

namespace Lumen
{
	bool UseHardwareRayTracedReflections(const FSceneViewFamily& ViewFamily)
	{
#if RHI_RAYTRACING
		return IsRayTracingEnabled() 
			&& Lumen::UseHardwareRayTracing(ViewFamily) 
			&& (CVarLumenReflectionsHardwareRayTracing.GetValueOnAnyThread() != 0);
#else
		return false;
#endif
	}
} // namespace Lumen

bool LumenReflections::IsHitLightingForceEnabled(const FViewInfo& View)
{
#if RHI_RAYTRACING
	return Lumen::GetHardwareRayTracingLightingMode(View) != Lumen::EHardwareRayTracingLightingMode::LightingFromSurfaceCache;
#else
	return false;
#endif
}

bool LumenReflections::UseHitLighting(const FViewInfo& View)
{
#if RHI_RAYTRACING
	return IsHitLightingForceEnabled(View) || (CVarLumenReflectionsHardwareRayTracingRetraceHitLighting.GetValueOnRenderThread() != 0);
#else
	return false;
#endif
}

bool LumenReflections::UseFarField(const FSceneViewFamily& ViewFamily)
{
#if RHI_RAYTRACING
	return Lumen::UseFarField(ViewFamily) && CVarLumenReflectionsHardwareRayTracingRetraceFarField.GetValueOnRenderThread();
#else
	return false;
#endif
}

#if RHI_RAYTRACING

enum class ERayTracingPass
{
	Default,
	FarField,
	HitLighting,
	MAX
};

class FLumenReflectionHardwareRayTracing : public FLumenHardwareRayTracingShaderBase
{
	DECLARE_LUMEN_RAYTRACING_SHADER(FLumenReflectionHardwareRayTracing, Lumen::ERayTracingShaderDispatchSize::DispatchSize1D)

	class FRayTracingPass : SHADER_PERMUTATION_ENUM_CLASS("RAY_TRACING_PASS", ERayTracingPass);
	class FWriteDataForHitLightingPass : SHADER_PERMUTATION_BOOL("WRITE_DATA_FOR_HIT_LIGHTING_PASS");
	class FRadianceCache : SHADER_PERMUTATION_BOOL("DIM_RADIANCE_CACHE");
	class FHairStrandsOcclusionDim : SHADER_PERMUTATION_BOOL("DIM_HAIRSTRANDS_VOXEL");
	class FIndirectDispatchDim : SHADER_PERMUTATION_BOOL("DIM_INDIRECT_DISPATCH");
	class FRecursiveReflectionTraces : SHADER_PERMUTATION_BOOL("RECURSIVE_REFLECTION_TRACES");
	using FPermutationDomain = TShaderPermutationDomain<FRayTracingPass, FWriteDataForHitLightingPass, FRadianceCache, FHairStrandsOcclusionDim, FIndirectDispatchDim, FRecursiveReflectionTraces>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FLumenHardwareRayTracingShaderBase::FSharedParameters, SharedParameters)
		RDG_BUFFER_ACCESS(HardwareRayTracingIndirectArgs, ERHIAccess::IndirectArgs | ERHIAccess::SRVCompute)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, CompactedTraceTexelAllocator)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, CompactedTraceTexelData)
		SHADER_PARAMETER_STRUCT_INCLUDE(FLumenHZBScreenTraceParameters, HZBScreenTraceParameters)
		SHADER_PARAMETER(float, RelativeDepthThickness)
		SHADER_PARAMETER(float, SampleSceneColorNormalTreshold)
		SHADER_PARAMETER(int32, SampleSceneColor)

		SHADER_PARAMETER(int, ThreadCount)
		SHADER_PARAMETER(int, GroupCount)
		SHADER_PARAMETER(int, NearFieldLightingMode)

		SHADER_PARAMETER(float, RayTracingCullingRadius)
		SHADER_PARAMETER(float, FarFieldBias)
		SHADER_PARAMETER(float, PullbackBias)
		SHADER_PARAMETER(int, MaxTranslucentSkipCount)
		SHADER_PARAMETER(uint32, MaxTraversalIterations)
		SHADER_PARAMETER(int, ApplySkyLight)
		SHADER_PARAMETER(int, HitLightingForceEnabled)
		SHADER_PARAMETER(FVector3f, FarFieldReferencePos)

		// Reflection-specific includes (includes output targets)
		SHADER_PARAMETER_STRUCT_INCLUDE(FLumenReflectionTracingParameters, ReflectionTracingParameters)
		SHADER_PARAMETER_STRUCT_INCLUDE(FLumenReflectionTileParameters, ReflectionTileParameters)
		SHADER_PARAMETER_STRUCT_INCLUDE(LumenRadianceCache::FRadianceCacheInterpolationParameters, RadianceCacheParameters)
		SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FVirtualVoxelParameters, HairStrandsVoxel)
	END_SHADER_PARAMETER_STRUCT()

	static FPermutationDomain RemapPermutation(FPermutationDomain PermutationVector)
	{
		if (PermutationVector.Get<FRayTracingPass>() == ERayTracingPass::Default)
		{
			PermutationVector.Set<FRecursiveReflectionTraces>(false);
		}
		else if (PermutationVector.Get<FRayTracingPass>() == ERayTracingPass::FarField)
		{
			PermutationVector.Set<FRecursiveReflectionTraces>(false);
			PermutationVector.Set<FRadianceCache>(false);
			PermutationVector.Set<FHairStrandsOcclusionDim>(false);
		}
		else if (PermutationVector.Get<FRayTracingPass>() == ERayTracingPass::HitLighting)
		{
			PermutationVector.Set<FWriteDataForHitLightingPass>(false);
		}

		return PermutationVector;
	}

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters, Lumen::ERayTracingShaderDispatchType ShaderDispatchType)
	{
		FPermutationDomain PermutationVector(Parameters.PermutationId);

		if (RemapPermutation(PermutationVector) != PermutationVector)
		{
			return false;
		}

		if (ShaderDispatchType == Lumen::ERayTracingShaderDispatchType::Inline && PermutationVector.Get<FRayTracingPass>() == ERayTracingPass::HitLighting)
		{
			return false;
		}

		return FLumenHardwareRayTracingShaderBase::ShouldCompilePermutation(Parameters, ShaderDispatchType);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, Lumen::ERayTracingShaderDispatchType ShaderDispatchType, FShaderCompilerEnvironment& OutEnvironment)
	{
		FLumenHardwareRayTracingShaderBase::ModifyCompilationEnvironment(Parameters, ShaderDispatchType, Lumen::ESurfaceCacheSampling::HighResPages, OutEnvironment);

		FPermutationDomain PermutationVector(Parameters.PermutationId);

		if (PermutationVector.Get<FRayTracingPass>() == ERayTracingPass::Default)
		{
			OutEnvironment.SetDefine(TEXT("ENABLE_NEAR_FIELD_TRACING"), 1);
		}

		if (PermutationVector.Get<FRayTracingPass>() == ERayTracingPass::FarField)
		{
			OutEnvironment.SetDefine(TEXT("ENABLE_FAR_FIELD_TRACING"), 1);
		}
	}

	static ERayTracingPayloadType GetRayTracingPayloadType(const int32 PermutationId)
	{
		FPermutationDomain PermutationVector(PermutationId);
		if (PermutationVector.Get<FRayTracingPass>() == ERayTracingPass::HitLighting)
		{
			return ERayTracingPayloadType::RayTracingMaterial;
		}
		else
		{
			return ERayTracingPayloadType::LumenMinimal;	
		}
	}
};

IMPLEMENT_LUMEN_RAYGEN_AND_COMPUTE_RAYTRACING_SHADERS(FLumenReflectionHardwareRayTracing)

IMPLEMENT_GLOBAL_SHADER(FLumenReflectionHardwareRayTracingCS, "/Engine/Private/Lumen/LumenReflectionHardwareRayTracing.usf", "LumenReflectionHardwareRayTracingCS", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FLumenReflectionHardwareRayTracingRGS, "/Engine/Private/Lumen/LumenReflectionHardwareRayTracing.usf", "LumenReflectionHardwareRayTracingRGS", SF_RayGen);

class FLumenReflectionHardwareRayTracingIndirectArgsCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FLumenReflectionHardwareRayTracingIndirectArgsCS)
	SHADER_USE_PARAMETER_STRUCT(FLumenReflectionHardwareRayTracingIndirectArgsCS, FGlobalShader)

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, CompactedTraceTexelAllocator)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWHardwareRayTracingIndirectArgs)
		SHADER_PARAMETER(FIntPoint, OutputThreadGroupSize)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportLumenGI(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE_1D"), GetThreadGroupSize1D());
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE_2D"), GetThreadGroupSize2D());
	}

	static int32 GetThreadGroupSize1D() { return GetThreadGroupSize2D() * GetThreadGroupSize2D(); }
	static int32 GetThreadGroupSize2D() { return 8; }
};

IMPLEMENT_GLOBAL_SHADER(FLumenReflectionHardwareRayTracingIndirectArgsCS, "/Engine/Private/Lumen/LumenReflectionHardwareRayTracing.usf", "FLumenReflectionHardwareRayTracingIndirectArgsCS", SF_Compute);

void FDeferredShadingSceneRenderer::PrepareLumenHardwareRayTracingReflections(const FViewInfo& View, TArray<FRHIRayTracingShader*>& OutRayGenShaders)
{
	if (Lumen::UseHardwareRayTracedReflections(*View.Family) && LumenReflections::UseHitLighting(View))
	{
		for (int32 HairOcclusion = 0; HairOcclusion < 2; HairOcclusion++)
		{
			FLumenReflectionHardwareRayTracingRGS::FPermutationDomain PermutationVector;
			PermutationVector.Set<FLumenReflectionHardwareRayTracingRGS::FRayTracingPass>(ERayTracingPass::HitLighting);
			PermutationVector.Set<FLumenReflectionHardwareRayTracingRGS::FWriteDataForHitLightingPass>(false);
			PermutationVector.Set<FLumenReflectionHardwareRayTracingRGS::FRadianceCache>(false);
			PermutationVector.Set<FLumenReflectionHardwareRayTracingRGS::FHairStrandsOcclusionDim>(HairOcclusion != 0);
			PermutationVector.Set<FLumenReflectionHardwareRayTracingRGS::FIndirectDispatchDim>(Lumen::UseHardwareIndirectRayTracing());
			PermutationVector.Set<FLumenReflectionHardwareRayTracingRGS::FRecursiveReflectionTraces>(LumenReflections::GetMaxReflectionBounces(View) > 1);
			TShaderRef<FLumenReflectionHardwareRayTracingRGS> RayGenerationShader = View.ShaderMap->GetShader<FLumenReflectionHardwareRayTracingRGS>(PermutationVector);
			OutRayGenShaders.Add(RayGenerationShader.GetRayTracingShader());
		}
	}
}

void FDeferredShadingSceneRenderer::PrepareLumenHardwareRayTracingReflectionsDeferredMaterial(const FViewInfo& View, TArray<FRHIRayTracingShader*>& OutRayGenShaders)
{
}

void FDeferredShadingSceneRenderer::PrepareLumenHardwareRayTracingReflectionsLumenMaterial(const FViewInfo& View, TArray<FRHIRayTracingShader*>& OutRayGenShaders)
{
	if (Lumen::UseHardwareRayTracedReflections(*View.Family))
	{
		const bool bUseFarField = LumenReflections::UseFarField(*View.Family);
		const bool bUseHitLighting = LumenReflections::UseHitLighting(View);

		// Default
		for (int RadianceCache = 0; RadianceCache < 2; ++RadianceCache)
		{
			for (int32 HairOcclusion = 0; HairOcclusion < 2; ++HairOcclusion)
			{
				FLumenReflectionHardwareRayTracingRGS::FPermutationDomain PermutationVector;
				PermutationVector.Set<FLumenReflectionHardwareRayTracingRGS::FRayTracingPass>(ERayTracingPass::Default);
				PermutationVector.Set<FLumenReflectionHardwareRayTracingRGS::FWriteDataForHitLightingPass>(bUseHitLighting);
				PermutationVector.Set<FLumenReflectionHardwareRayTracingRGS::FRadianceCache>(RadianceCache != 0);
				PermutationVector.Set<FLumenReflectionHardwareRayTracingRGS::FHairStrandsOcclusionDim>(HairOcclusion != 0);
				PermutationVector.Set<FLumenReflectionHardwareRayTracingRGS::FIndirectDispatchDim>(Lumen::UseHardwareIndirectRayTracing());
				PermutationVector.Set<FLumenReflectionHardwareRayTracingRGS::FRecursiveReflectionTraces>(false);
				TShaderRef<FLumenReflectionHardwareRayTracingRGS> RayGenerationShader = View.ShaderMap->GetShader<FLumenReflectionHardwareRayTracingRGS>(PermutationVector);
				OutRayGenShaders.Add(RayGenerationShader.GetRayTracingShader());
			}
		}

		// Far-field continuation
		if (bUseFarField)
		{
			FLumenReflectionHardwareRayTracingRGS::FPermutationDomain PermutationVector;
			PermutationVector.Set<FLumenReflectionHardwareRayTracingRGS::FRayTracingPass>(ERayTracingPass::FarField);
			PermutationVector.Set<FLumenReflectionHardwareRayTracingRGS::FWriteDataForHitLightingPass>(bUseHitLighting);
			PermutationVector.Set<FLumenReflectionHardwareRayTracingRGS::FRadianceCache>(false);
			PermutationVector.Set<FLumenReflectionHardwareRayTracingRGS::FHairStrandsOcclusionDim>(false);
			PermutationVector.Set<FLumenReflectionHardwareRayTracingRGS::FIndirectDispatchDim>(Lumen::UseHardwareIndirectRayTracing());
			PermutationVector.Set<FLumenReflectionHardwareRayTracingRGS::FRecursiveReflectionTraces>(false);
			TShaderRef<FLumenReflectionHardwareRayTracingRGS> RayGenerationShader = View.ShaderMap->GetShader<FLumenReflectionHardwareRayTracingRGS>(PermutationVector);
			OutRayGenShaders.Add(RayGenerationShader.GetRayTracingShader());
		}
	}
}

void SetLumenHardwareRayTracingReflectionParameters(
	FRDGBuilder& GraphBuilder,
	const FSceneTextures& SceneTextures,
	const FSceneTextureParameters& SceneTextureParameters,
	const FViewInfo& View,
	const FLumenCardTracingParameters& TracingParameters,
	const FLumenReflectionTracingParameters& ReflectionTracingParameters,
	const FLumenReflectionTileParameters& ReflectionTileParameters,
	const LumenRadianceCache::FRadianceCacheInterpolationParameters& RadianceCacheParameters,
	bool bApplySkyLight,
	bool bIsHitLightingForceEnabled,
	bool bSampleSceneColorAtHit,
	bool bNeedTraceHairVoxel,
	FRDGBufferRef HardwareRayTracingIndirectArgsBuffer,
	FRDGBufferRef CompactedTraceTexelAllocator,
	FRDGBufferRef CompactedTraceTexelData,
	FLumenReflectionHardwareRayTracing::FParameters* Parameters
)
{
	uint32 DefaultThreadCount = CVarLumenReflectionsHardwareRayTracingDefaultThreadCount.GetValueOnRenderThread();
	uint32 DefaultGroupCount = CVarLumenReflectionsHardwareRayTracingDefaultGroupCount.GetValueOnRenderThread();

	SetLumenHardwareRayTracingSharedParameters(
		GraphBuilder,
		SceneTextureParameters,
		View,
		TracingParameters,
		&Parameters->SharedParameters
	);
	Parameters->HardwareRayTracingIndirectArgs = HardwareRayTracingIndirectArgsBuffer;
	Parameters->CompactedTraceTexelAllocator = GraphBuilder.CreateSRV(CompactedTraceTexelAllocator, PF_R32_UINT);
	Parameters->CompactedTraceTexelData = GraphBuilder.CreateSRV(CompactedTraceTexelData, PF_R32_UINT);

	Parameters->HZBScreenTraceParameters = SetupHZBScreenTraceParameters(GraphBuilder, View, SceneTextures);

	if (Parameters->HZBScreenTraceParameters.PrevSceneColorTexture == SceneTextures.Color.Resolve || !Parameters->SharedParameters.SceneTextures.GBufferVelocityTexture)
	{
		Parameters->SharedParameters.SceneTextures.GBufferVelocityTexture = GSystemTextures.GetBlackDummy(GraphBuilder);
	}

	extern float GLumenReflectionSampleSceneColorRelativeDepthThreshold;
	Parameters->RelativeDepthThickness = GLumenReflectionSampleSceneColorRelativeDepthThreshold;
	Parameters->SampleSceneColorNormalTreshold = LumenReflections::GetSampleSceneColorNormalTreshold();
	Parameters->SampleSceneColor = bSampleSceneColorAtHit ? 1 : 0;

	Parameters->ThreadCount = DefaultThreadCount;
	Parameters->GroupCount = DefaultGroupCount;
	Parameters->NearFieldLightingMode = static_cast<int32>(Lumen::GetHardwareRayTracingLightingMode(View));
	Parameters->RayTracingCullingRadius = GetRayTracingCulling() != 0 ? GetRayTracingCullingRadius() : FLT_MAX;
	Parameters->FarFieldBias = LumenHardwareRayTracing::GetFarFieldBias();
	Parameters->FarFieldReferencePos = (FVector3f)Lumen::GetFarFieldReferencePos();
	Parameters->PullbackBias = Lumen::GetHardwareRayTracingPullbackBias();
	Parameters->MaxTranslucentSkipCount = Lumen::GetMaxTranslucentSkipCount();
	Parameters->MaxTraversalIterations = LumenHardwareRayTracing::GetMaxTraversalIterations();
	Parameters->ApplySkyLight = bApplySkyLight;
	Parameters->HitLightingForceEnabled = bIsHitLightingForceEnabled;
		
	// Reflection-specific
	Parameters->ReflectionTracingParameters = ReflectionTracingParameters;
	Parameters->ReflectionTileParameters = ReflectionTileParameters;
	Parameters->RadianceCacheParameters = RadianceCacheParameters;

	if (bNeedTraceHairVoxel)
	{
		Parameters->HairStrandsVoxel = HairStrands::BindHairStrandsVoxelUniformParameters(View);
	}
}

void DispatchLumenReflectionHardwareRayTracingIndirectArgs(FRDGBuilder& GraphBuilder, const FViewInfo& View, FRDGBufferRef HardwareRayTracingIndirectArgsBuffer, FRDGBufferRef CompactedTraceTexelAllocator, FIntPoint OutputThreadGroupSize, ERDGPassFlags ComputePassFlags)
{
	FLumenReflectionHardwareRayTracingIndirectArgsCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FLumenReflectionHardwareRayTracingIndirectArgsCS::FParameters>();
	
	PassParameters->CompactedTraceTexelAllocator = GraphBuilder.CreateSRV(CompactedTraceTexelAllocator, PF_R32_UINT);
	PassParameters->RWHardwareRayTracingIndirectArgs = GraphBuilder.CreateUAV(HardwareRayTracingIndirectArgsBuffer, PF_R32_UINT);
	PassParameters->OutputThreadGroupSize = OutputThreadGroupSize;

	TShaderRef<FLumenReflectionHardwareRayTracingIndirectArgsCS> ComputeShader = View.ShaderMap->GetShader<FLumenReflectionHardwareRayTracingIndirectArgsCS>();
	FComputeShaderUtils::AddPass(
		GraphBuilder,
		RDG_EVENT_NAME("ReflectionCompactRaysIndirectArgs"),
		ComputePassFlags,
		ComputeShader,
		PassParameters,
		FIntVector(1, 1, 1));
}

void DispatchRayGenOrComputeShader(
	FRDGBuilder& GraphBuilder,
	const FSceneTextures& SceneTextures,
	const FSceneTextureParameters& SceneTextureParameters,
	const FScene* Scene,
	const FViewInfo& View,
	const FLumenCardTracingParameters& TracingParameters,
	const FLumenReflectionTracingParameters& ReflectionTracingParameters,
	const FLumenReflectionTileParameters& ReflectionTileParameters,
	const FCompactedReflectionTraceParameters& CompactedTraceParameters,
	const LumenRadianceCache::FRadianceCacheInterpolationParameters& RadianceCacheParameters,
	const FLumenReflectionHardwareRayTracing::FPermutationDomain& PermutationVector,
	uint32 RayCount,
	bool bApplySkyLight,
	bool bIsHitLightingForceEnabled,
	bool bUseRadianceCache,
	bool bInlineRayTracing,
	bool bSampleSceneColorAtHit,
	bool bNeedTraceHairVoxel,
	ERDGPassFlags ComputePassFlags)
{
	FRDGBufferRef CompactedTraceTexelAllocator = CompactedTraceParameters.CompactedTraceTexelAllocator->Desc.Buffer;
	FRDGBufferRef CompactedTraceTexelData = CompactedTraceParameters.CompactedTraceTexelData->Desc.Buffer;

	FRDGBufferRef HardwareRayTracingIndirectArgsBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateIndirectDesc<FRHIDispatchIndirectParameters>(1), TEXT("Lumen.Reflection.CompactTracingIndirectArgs"));
	FIntPoint OutputThreadGroupSize = bInlineRayTracing ? FLumenReflectionHardwareRayTracingCS::GetThreadGroupSize() : FLumenReflectionHardwareRayTracingRGS::GetThreadGroupSize();
	DispatchLumenReflectionHardwareRayTracingIndirectArgs(GraphBuilder, View, HardwareRayTracingIndirectArgsBuffer, CompactedTraceTexelAllocator, OutputThreadGroupSize, ComputePassFlags);

	uint32 DefaultThreadCount = CVarLumenReflectionsHardwareRayTracingDefaultThreadCount.GetValueOnRenderThread();
	uint32 DefaultGroupCount = CVarLumenReflectionsHardwareRayTracingDefaultGroupCount.GetValueOnRenderThread();

	FLumenReflectionHardwareRayTracing::FParameters* PassParameters = GraphBuilder.AllocParameters<FLumenReflectionHardwareRayTracing::FParameters>();
	SetLumenHardwareRayTracingReflectionParameters(
		GraphBuilder,
		SceneTextures,
		SceneTextureParameters,
		View,
		TracingParameters,
		ReflectionTracingParameters,
		ReflectionTileParameters,
		RadianceCacheParameters,
		bApplySkyLight,
		bIsHitLightingForceEnabled,
		bSampleSceneColorAtHit,
		bNeedTraceHairVoxel,
		HardwareRayTracingIndirectArgsBuffer,
		CompactedTraceTexelAllocator,
		CompactedTraceTexelData,
		PassParameters
	);
	
	ERayTracingPass RayTracingPass = PermutationVector.Get<FLumenReflectionHardwareRayTracingRGS::FRayTracingPass>();
	auto GenerateModeString = [RayTracingPass]()
	{
		FString ModeStr = RayTracingPass == ERayTracingPass::HitLighting ? FString::Printf(TEXT("[hit-lighting]")) :
			(RayTracingPass == ERayTracingPass::FarField ? FString::Printf(TEXT("[far-field]")) : FString::Printf(TEXT("[default]")));

		return ModeStr;
	};

	FIntPoint DispatchResolution = FIntPoint(DefaultThreadCount, DefaultGroupCount);
	auto GenerateResolutionString = [DispatchResolution]()
	{
		FString ResolutionStr = Lumen::UseHardwareIndirectRayTracing() ? FString::Printf(TEXT("<indirect>")) :
			FString::Printf(TEXT("%ux%u"), DispatchResolution.X, DispatchResolution.Y);

		return ResolutionStr;
	};

	if (bInlineRayTracing)
	{
		TShaderRef<FLumenReflectionHardwareRayTracingCS> ComputeShader = View.ShaderMap->GetShader<FLumenReflectionHardwareRayTracingCS>(PermutationVector);

		// Inline always runs as an indirect compute shader
		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("ReflectionHardwareRayTracing (inline) %s <indirect>", *GenerateModeString()),
			ComputePassFlags,
			ComputeShader,
			PassParameters,
			PassParameters->HardwareRayTracingIndirectArgs,
			0);		
	}
	else
	{
		const bool bUseMinimalPayload = RayTracingPass != ERayTracingPass::HitLighting;

		TShaderRef<FLumenReflectionHardwareRayTracingRGS> RayGenerationShader = View.ShaderMap->GetShader<FLumenReflectionHardwareRayTracingRGS>(PermutationVector);		
		if (Lumen::UseHardwareIndirectRayTracing())
		{
			AddLumenRayTraceDispatchIndirectPass(
				GraphBuilder,
				RDG_EVENT_NAME("ReflectionHardwareRayTracing (raygen) %s %s", *GenerateModeString(), *GenerateResolutionString()),
				RayGenerationShader,
				PassParameters,
				PassParameters->HardwareRayTracingIndirectArgs,
				0,
				View,
				bUseMinimalPayload);
		}
		else
		{
			AddLumenRayTraceDispatchPass(
				GraphBuilder,
				RDG_EVENT_NAME("ReflectionHardwareRayTracing (raygen) %s %s", *GenerateModeString(), *GenerateResolutionString()),
				RayGenerationShader,
				PassParameters,
				DispatchResolution,
				View,
				bUseMinimalPayload);
		}
	}
}

#endif // RHI_RAYTRACING

void RenderLumenHardwareRayTracingReflections(
	FRDGBuilder& GraphBuilder,
	const FSceneTextures& SceneTextures,
	const FSceneTextureParameters& SceneTextureParameters,
	const FScene* Scene,
	const FViewInfo& View,
	const FLumenCardTracingParameters& TracingParameters,
	const FLumenReflectionTracingParameters& ReflectionTracingParameters,
	const FLumenReflectionTileParameters& ReflectionTileParameters,
	float MaxTraceDistance,
	bool bUseRadianceCache,
	const LumenRadianceCache::FRadianceCacheInterpolationParameters& RadianceCacheParameters,
	bool bSampleSceneColorAtHit,
	ERDGPassFlags ComputePassFlags
)
{
#if RHI_RAYTRACING	
	const bool bUseHitLighting = LumenReflections::UseHitLighting(View);
	const bool bIsHitLightingForceEnabled = LumenReflections::IsHitLightingForceEnabled(View);
	const bool bInlineRayTracing = Lumen::UseHardwareInlineRayTracing(*View.Family) && !bIsHitLightingForceEnabled;
	const bool bUseFarFieldForReflections = LumenReflections::UseFarField(*View.Family);
	extern int32 GLumenReflectionHairStrands_VoxelTrace;
	const bool bNeedTraceHairVoxel = HairStrands::HasViewHairStrandsVoxelData(View) && GLumenReflectionHairStrands_VoxelTrace > 0;
	const bool bIndirectDispatch = Lumen::UseHardwareIndirectRayTracing() || bInlineRayTracing;

	checkf(ComputePassFlags != ERDGPassFlags::AsyncCompute || bInlineRayTracing, TEXT("Async Lumen HWRT is only supported for inline ray tracing"));

	const FIntPoint BufferSize = ReflectionTracingParameters.ReflectionTracingBufferSize;
	const int32 RayCount = BufferSize.X * BufferSize.Y;

	// Default tracing for near field with only surface cache
	{
		FCompactedReflectionTraceParameters CompactedTraceParameters = LumenReflections::CompactTraces(
			GraphBuilder,
			View,
			TracingParameters,
			ReflectionTracingParameters,
			ReflectionTileParameters,
			false,
			0.0f,
			MaxTraceDistance,
			ComputePassFlags);

		bool bApplySkyLight = !bUseFarFieldForReflections;
		bool bHitLightingRetrace = false;
		const bool bWriteFinalLighting = !bIsHitLightingForceEnabled || !bUseFarFieldForReflections;

		FLumenReflectionHardwareRayTracing::FPermutationDomain PermutationVector;
		PermutationVector.Set<FLumenReflectionHardwareRayTracing::FRayTracingPass>(ERayTracingPass::Default);
		PermutationVector.Set<FLumenReflectionHardwareRayTracing::FWriteDataForHitLightingPass>(bUseHitLighting);
		PermutationVector.Set<FLumenReflectionHardwareRayTracing::FIndirectDispatchDim>(bIndirectDispatch);
		PermutationVector.Set<FLumenReflectionHardwareRayTracing::FRadianceCache>(bUseRadianceCache);
		PermutationVector.Set<FLumenReflectionHardwareRayTracing::FHairStrandsOcclusionDim>(bNeedTraceHairVoxel);
		PermutationVector.Set<FLumenReflectionHardwareRayTracing::FRecursiveReflectionTraces>(false);
		PermutationVector = FLumenReflectionHardwareRayTracing::RemapPermutation(PermutationVector);

		DispatchRayGenOrComputeShader(GraphBuilder, SceneTextures, SceneTextureParameters, Scene, View, TracingParameters, ReflectionTracingParameters, ReflectionTileParameters, CompactedTraceParameters, RadianceCacheParameters,
			PermutationVector, RayCount, bApplySkyLight, bIsHitLightingForceEnabled, bUseRadianceCache, bInlineRayTracing, bSampleSceneColorAtHit, bNeedTraceHairVoxel, ComputePassFlags);
	}

	// Far Field
	if (bUseFarFieldForReflections)
	{
		FCompactedReflectionTraceParameters CompactedTraceParameters = LumenReflections::CompactTraces(
			GraphBuilder,
			View,
			TracingParameters,
			ReflectionTracingParameters,
			ReflectionTileParameters,
			false,
			0.0f,
			Lumen::GetFarFieldMaxTraceDistance(),
			ComputePassFlags,
			LumenReflections::ETraceCompactionMode::FarField);

		bool bApplySkyLight = true;
		bool bHitLightingRetrace = false;

		FLumenReflectionHardwareRayTracing::FPermutationDomain PermutationVector;
		PermutationVector.Set<FLumenReflectionHardwareRayTracing::FRayTracingPass>(ERayTracingPass::FarField);
		PermutationVector.Set<FLumenReflectionHardwareRayTracing::FWriteDataForHitLightingPass>(bUseHitLighting);
		PermutationVector.Set<FLumenReflectionHardwareRayTracing::FIndirectDispatchDim>(bIndirectDispatch);
		PermutationVector.Set<FLumenReflectionHardwareRayTracing::FRadianceCache>(false);
		PermutationVector.Set<FLumenReflectionHardwareRayTracing::FHairStrandsOcclusionDim>(false);
		PermutationVector.Set<FLumenReflectionHardwareRayTracing::FRecursiveReflectionTraces>(false);
		PermutationVector = FLumenReflectionHardwareRayTracing::RemapPermutation(PermutationVector);

		// Trace continuation rays
		DispatchRayGenOrComputeShader(GraphBuilder, SceneTextures, SceneTextureParameters, Scene, View, TracingParameters, ReflectionTracingParameters, ReflectionTileParameters, CompactedTraceParameters, RadianceCacheParameters,
			PermutationVector, RayCount, bApplySkyLight, bIsHitLightingForceEnabled, bUseRadianceCache, bInlineRayTracing, bSampleSceneColorAtHit, bNeedTraceHairVoxel, ComputePassFlags);
	}

	// Hit Lighting
	if (bUseHitLighting)
	{
		FCompactedReflectionTraceParameters CompactedTraceParameters = LumenReflections::CompactTraces(
			GraphBuilder,
			View,
			TracingParameters,
			ReflectionTracingParameters,
			ReflectionTileParameters,
			false,
			0.0f,
			bUseFarFieldForReflections ? Lumen::GetFarFieldMaxTraceDistance() : MaxTraceDistance,
			ComputePassFlags,
			LumenReflections::ETraceCompactionMode::HitLighting,
			/*bSortByMaterial*/ CVarLumenReflectionsHardwareRayTracingBucketMaterials.GetValueOnRenderThread() != 0);

		// Trace with hit-lighting
		{
			bool bApplySkyLight = true;
			bool bUseInline = false;

			FLumenReflectionHardwareRayTracing::FPermutationDomain PermutationVector;
			PermutationVector.Set<FLumenReflectionHardwareRayTracing::FRayTracingPass>(ERayTracingPass::HitLighting);
			PermutationVector.Set<FLumenReflectionHardwareRayTracing::FWriteDataForHitLightingPass>(false);
			PermutationVector.Set<FLumenReflectionHardwareRayTracing::FIndirectDispatchDim>(bIndirectDispatch);
			PermutationVector.Set<FLumenReflectionHardwareRayTracing::FRadianceCache>(false);
			PermutationVector.Set<FLumenReflectionHardwareRayTracing::FHairStrandsOcclusionDim>(bNeedTraceHairVoxel);
			PermutationVector.Set<FLumenReflectionHardwareRayTracing::FRecursiveReflectionTraces>(ReflectionTracingParameters.MaxReflectionBounces > 1);
			PermutationVector = FLumenReflectionHardwareRayTracing::RemapPermutation(PermutationVector);

			DispatchRayGenOrComputeShader(GraphBuilder, SceneTextures, SceneTextureParameters, Scene, View, TracingParameters, ReflectionTracingParameters, ReflectionTileParameters, CompactedTraceParameters, RadianceCacheParameters,
				PermutationVector, RayCount, bApplySkyLight, bIsHitLightingForceEnabled, bUseRadianceCache, bUseInline, bSampleSceneColorAtHit, bNeedTraceHairVoxel, ComputePassFlags);
		}
	}
#endif
}