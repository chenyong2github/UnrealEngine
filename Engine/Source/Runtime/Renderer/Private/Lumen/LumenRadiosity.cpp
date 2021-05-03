// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	LumenRadiosity.cpp
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
#include "LumenRadianceCache.h"

int32 GLumenRadiosity = 1;
FAutoConsoleVariableRef CVarLumenRadiosity(
	TEXT("r.LumenScene.Radiosity"),
	GLumenRadiosity,
	TEXT(""),
	ECVF_RenderThreadSafe
	);

int32 GLumenRadiosityDownsampleFactor = 2;
FAutoConsoleVariableRef CVarLumenRadiosityDownsampleFactor(
	TEXT("r.LumenScene.Radiosity.DownsampleFactor"),
	GLumenRadiosityDownsampleFactor,
	TEXT(""),
	ECVF_RenderThreadSafe
	);

float GRadiosityTraceStepFactor = 2;
FAutoConsoleVariableRef CVarRadiosityTraceStepFactor(
	TEXT("r.LumenScene.Radiosity.TraceStepFactor"),
	GRadiosityTraceStepFactor,
	TEXT("."),
	ECVF_Scalability | ECVF_RenderThreadSafe
	);

int32 GLumenRadiosityNumTargetCones = 8;
FAutoConsoleVariableRef CVarLumenRadiosityNumTargetCones(
	TEXT("r.LumenScene.Radiosity.NumCones"),
	GLumenRadiosityNumTargetCones,
	TEXT(""),
	ECVF_Scalability | ECVF_RenderThreadSafe
	);

float GLumenRadiosityMinSampleRadius = 10;
FAutoConsoleVariableRef CVarLumenRadiosityMinSampleRadius(
	TEXT("r.LumenScene.Radiosity.MinSampleRadius"),
	GLumenRadiosityMinSampleRadius,
	TEXT("."),
	ECVF_Scalability | ECVF_RenderThreadSafe
	);

float GLumenRadiosityMinTraceDistance = 10;
FAutoConsoleVariableRef CVarLumenRadiosityMinTraceDistance(
	TEXT("r.LumenScene.Radiosity.MinTraceDistance"),
	GLumenRadiosityMinTraceDistance,
	TEXT("."),
	ECVF_Scalability | ECVF_RenderThreadSafe
	);

float GLumenRadiositySurfaceBias = 5;
FAutoConsoleVariableRef CVarLumenRadiositySurfaceBias(
	TEXT("r.LumenScene.Radiosity.SurfaceBias"),
	GLumenRadiositySurfaceBias,
	TEXT("."),
	ECVF_Scalability | ECVF_RenderThreadSafe
	);

float GLumenRadiosityConeAngleScale = 1.0f;
FAutoConsoleVariableRef CVarLumenRadiosityConeAngleScale(
	TEXT("r.LumenScene.Radiosity.ConeAngleScale"),
	GLumenRadiosityConeAngleScale,
	TEXT("."),
	ECVF_Scalability | ECVF_RenderThreadSafe
	);

float GLumenRadiosityIntensity = 1.0f;
FAutoConsoleVariableRef CVarLumenRadiosityIntensity(
	TEXT("r.LumenScene.Radiosity.Intensity"),
	GLumenRadiosityIntensity,
	TEXT(""),
	ECVF_RenderThreadSafe
	);

float GLumenRadiosityVoxelStepFactor = 1.0f;
FAutoConsoleVariableRef CVarRadiosityVoxelStepFactor(
	TEXT("r.LumenScene.Radiosity.VoxelStepFactor"),
	GLumenRadiosityVoxelStepFactor,
	TEXT("."),
	ECVF_Scalability | ECVF_RenderThreadSafe
	);

float GLumenSceneCardRadiosityUpdateFrequencyScale = 1.0f;
FAutoConsoleVariableRef CVarLumenSceneCardRadiosityUpdateFrequencyScale(
	TEXT("r.LumenScene.Radiosity.CardUpdateFrequencyScale"),
	GLumenSceneCardRadiosityUpdateFrequencyScale,
	TEXT(""),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

float GLumenRadiosityProbeRadiusScale = 1.5f;
FAutoConsoleVariableRef CVarLumenRadiosityProbeRadiusScale(
	TEXT("r.LumenScene.Radiosity.ProbeRadiusScale"),
	GLumenRadiosityProbeRadiusScale,
	TEXT("Larger probes decrease parallax error, but are more costly to update"),
	ECVF_RenderThreadSafe
);

int32 GLumenRadiosityComputeTraceBlocksScatter = 1;
FAutoConsoleVariableRef CVarLumenRadiosityComputeTraceBlocksScatter(
	TEXT("r.LumenScene.Radiosity.ComputeScatter"),
	GLumenRadiosityComputeTraceBlocksScatter,
	TEXT(""),
	ECVF_Scalability | ECVF_RenderThreadSafe
	);

int32 GLumenRadiosityTraceBlocksAllocationDivisor = 2;
FAutoConsoleVariableRef CVarLumenRadiosityTraceBlocksAllocationDivisor(
	TEXT("r.LumenScene.Radiosity.TraceBlocksAllocationDivisor"),
	GLumenRadiosityTraceBlocksAllocationDivisor,
	TEXT(""),
	ECVF_Scalability | ECVF_RenderThreadSafe
	);

int32 GLumenRadiosityUseIrradianceCache = 0;
FAutoConsoleVariableRef CVarLumenRadiosityUseIrradianceCache(
	TEXT("r.LumenScene.Radiosity.IrradianceCache"),
	GLumenRadiosityUseIrradianceCache,
	TEXT("Whether to use the Irradiance Cache for Radiosity"),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

int32 GLumenRadiosityIrradianceCacheNumClipmaps = 3;
FAutoConsoleVariableRef CVarLumenRadiosityIrradianceCacheNumClipmaps(
	TEXT("r.LumenScene.Radiosity.IrradianceCache.NumClipmaps"),
	GLumenRadiosityIrradianceCacheNumClipmaps,
	TEXT("Number of radiance cache clipmaps."),
	ECVF_RenderThreadSafe
);

float GLumenRadiosityIrradianceCacheClipmapWorldExtent = 2500.0f;
FAutoConsoleVariableRef CVarLumenRadiosityIrradianceCacheClipmapWorldExtent(
	TEXT("r.LumenScene.Radiosity.IrradianceCache.ClipmapWorldExtent"),
	GLumenRadiosityIrradianceCacheClipmapWorldExtent,
	TEXT("World space extent of the first clipmap"),
	ECVF_RenderThreadSafe
);

float GLumenRadiosityIrradianceCacheClipmapDistributionBase = 2.0f;
FAutoConsoleVariableRef CVarLumenRadiosityIrradianceCacheClipmapDistributionBase(
	TEXT("r.LumenScene.Radiosity.IrradianceCache.ClipmapDistributionBase"),
	GLumenRadiosityIrradianceCacheClipmapDistributionBase,
	TEXT("Base of the Pow() that controls the size of each successive clipmap relative to the first."),
	ECVF_RenderThreadSafe
);

int32 GLumenRadiosityIrradianceCacheNumProbeTracesBudget = 200;
FAutoConsoleVariableRef CVarLumenRadiosityIrradianceCacheNumProbeTracesBudget(
	TEXT("r.LumenScene.Radiosity.IrradianceCache.NumProbeTracesBudget"),
	GLumenRadiosityIrradianceCacheNumProbeTracesBudget,
	TEXT(""),
	ECVF_RenderThreadSafe
);

int32 GLumenRadiosityIrradianceCacheGridResolution = 32;
FAutoConsoleVariableRef CVarLumenRadiosityIrradianceCacheResolution(
	TEXT("r.LumenScene.Radiosity.IrradianceCache.GridResolution"),
	GLumenRadiosityIrradianceCacheGridResolution,
	TEXT("Resolution of the probe placement grid within each clipmap"),
	ECVF_RenderThreadSafe
);

int32 GLumenRadiosityIrradianceCacheProbeResolution = 16;
FAutoConsoleVariableRef CVarLumenRadiosityIrradianceCacheProbeResolution(
	TEXT("r.LumenScene.Radiosity.IrradianceCache.ProbeResolution"),
	GLumenRadiosityIrradianceCacheProbeResolution,
	TEXT("Resolution of the probe's 2d radiance layout.  The number of rays traced for the probe will be ProbeResolution ^ 2"),
	ECVF_RenderThreadSafe
);

int32 GLumenRadiosityIrradianceCacheProbeIrradianceResolution = 6;
FAutoConsoleVariableRef CVarLumenRadiosityIrradianceCacheProbeIrradianceResolution(
	TEXT("r.LumenScene.Radiosity.IrradianceCache.IrradianceProbeResolution"),
	GLumenRadiosityIrradianceCacheProbeIrradianceResolution,
	TEXT("Resolution of the probe's 2d irradiance layout."),
	ECVF_RenderThreadSafe
);

int32 GLumenRadiosityIrradianceCacheProbeOcclusionResolution = 16;
FAutoConsoleVariableRef CVarLumenRadiosityIrradianceCacheProbeOcclusionResolution(
	TEXT("r.LumenScene.Radiosity.IrradianceCache.OcclusionProbeResolution"),
	GLumenRadiosityIrradianceCacheProbeOcclusionResolution,
	TEXT("Resolution of the probe's 2d occlusion layout."),
	ECVF_RenderThreadSafe
);

int32 GLumenRadiosityIrradianceCacheProbeAtlasResolutionInProbes = 128;
FAutoConsoleVariableRef CVarLumenRadiosityIrradianceCacheProbeAtlasResolutionInProbes(
	TEXT("r.LumenScene.Radiosity.IrradianceCache.ProbeAtlasResolutionInProbes"),
	GLumenRadiosityIrradianceCacheProbeAtlasResolutionInProbes,
	TEXT("Number of probes along one dimension of the probe atlas cache texture.  This controls the memory usage of the cache.  Overflow currently results in incorrect rendering."),
	ECVF_RenderThreadSafe
);

float GLumenRadiosityIrradianceCacheProbeOcclusionNormalBias = 20;
FAutoConsoleVariableRef CVarLumenRadiosityIrradianceCacheProbeOcclusionNormalBias(
	TEXT("r.LumenScene.Radiosity.IrradianceCache.ProbeOcclusionNormalBias"),
	GLumenRadiosityIrradianceCacheProbeOcclusionNormalBias,
	TEXT("Bias along the normal to reduce self-occlusion artifacts from Probe Occlusion"),
	ECVF_RenderThreadSafe
);

namespace LumenRadiosity
{
	LumenRadianceCache::FRadianceCacheInputs SetupRadianceCacheInputs()
	{
		LumenRadianceCache::FRadianceCacheInputs Parameters;
		Parameters.ReprojectionRadiusScale = 1.5f;
		Parameters.ClipmapWorldExtent = GLumenRadiosityIrradianceCacheClipmapWorldExtent;
		Parameters.ClipmapDistributionBase = GLumenRadiosityIrradianceCacheClipmapDistributionBase;
		Parameters.RadianceProbeClipmapResolution = FMath::Clamp(GLumenRadiosityIrradianceCacheGridResolution, 1, 256);
		Parameters.ProbeAtlasResolutionInProbes = FIntPoint(GLumenRadiosityIrradianceCacheProbeAtlasResolutionInProbes, GLumenRadiosityIrradianceCacheProbeAtlasResolutionInProbes);
		Parameters.NumRadianceProbeClipmaps = FMath::Clamp(GLumenRadiosityIrradianceCacheNumClipmaps, 1, LumenRadianceCache::MaxClipmaps);
		Parameters.RadianceProbeResolution = GLumenRadiosityIrradianceCacheProbeResolution;
		Parameters.FinalProbeResolution = GLumenRadiosityIrradianceCacheProbeResolution + 2;
		Parameters.FinalRadianceAtlasMaxMip = 0;
		Parameters.CalculateIrradiance = 1;
		Parameters.IrradianceProbeResolution = GLumenRadiosityIrradianceCacheProbeIrradianceResolution;
		Parameters.OcclusionProbeResolution = GLumenRadiosityIrradianceCacheProbeOcclusionResolution;
		Parameters.NumProbeTracesBudget = GLumenRadiosityIrradianceCacheNumProbeTracesBudget;
		return Parameters;
	}
}


// Must match LumenRadiosity.usf
static constexpr int32 RadiosityProbeResolution = 8;
static constexpr int32 RadiosityComposedProbeResolution = (RadiosityProbeResolution + 2); // Includes 2 texel border for bilinear filtering

bool IsRadiosityEnabled()
{
	return GLumenFastCameraMode ? false : bool(GLumenRadiosity);
}

FIntPoint FLumenSceneData::GetRadiosityAtlasSize() const
{
	return FIntPoint::DivideAndRoundDown(PhysicalAtlasSize, GLumenRadiosityDownsampleFactor);
}

FHemisphereDirectionSampleGenerator RadiosityDirections;

float GetRadiosityConeHalfAngle()
{
	return RadiosityDirections.ConeHalfAngle * GLumenRadiosityConeAngleScale;
}

uint32 GPlaceRadiosityProbeGroupSize = 64;

class FPlaceProbeIndirectArgsCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FPlaceProbeIndirectArgsCS)
	SHADER_USE_PARAMETER_STRUCT(FPlaceProbeIndirectArgsCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWIndirectArgs)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, QuadAllocator)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportLumenGI(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE"), GPlaceRadiosityProbeGroupSize);
	}
};

IMPLEMENT_GLOBAL_SHADER(FPlaceProbeIndirectArgsCS, "/Engine/Private/Lumen/LumenRadiosity.usf", "PlaceProbeIndirectArgsCS", SF_Compute);


uint32 GSetupCardTraceBlocksGroupSize = 64;

class FSetupCardTraceBlocksCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FSetupCardTraceBlocksCS)
	SHADER_USE_PARAMETER_STRUCT(FSetupCardTraceBlocksCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWCardTraceBlockAllocator)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint4>, RWCardTraceBlockData)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, QuadAllocator)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, QuadData)
		SHADER_PARAMETER_SRV(StructuredBuffer<float4>, CardBuffer)
		SHADER_PARAMETER_SRV(StructuredBuffer<float4>, CardPageBuffer)
		SHADER_PARAMETER(FIntPoint, RadiosityAtlasSize)
		RDG_BUFFER_ACCESS(IndirectArgs, ERHIAccess::IndirectArgs)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportLumenGI(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE"), GSetupCardTraceBlocksGroupSize);
		OutEnvironment.CompilerFlags.Add(CFLAG_Wave32);
	}
};

IMPLEMENT_GLOBAL_SHADER(FSetupCardTraceBlocksCS, "/Engine/Private/Lumen/LumenRadiosity.usf", "SetupCardTraceBlocksCS", SF_Compute);

uint32 GRadiosityTraceBlocksGroupSize = 64;

class FTraceBlocksIndirectArgsCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FTraceBlocksIndirectArgsCS)
	SHADER_USE_PARAMETER_STRUCT(FTraceBlocksIndirectArgsCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWIndirectArgs)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, CardTraceBlockAllocator)
	END_SHADER_PARAMETER_STRUCT()

	class FIrradianceCache : SHADER_PERMUTATION_BOOL("IRRADIANCE_CACHE");
	using FPermutationDomain = TShaderPermutationDomain<FIrradianceCache>;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportLumenGI(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE"), GRadiosityTraceBlocksGroupSize);
	}
};

IMPLEMENT_GLOBAL_SHADER(FTraceBlocksIndirectArgsCS, "/Engine/Private/Lumen/LumenRadiosity.usf", "TraceBlocksIndirectArgsCS", SF_Compute);


class FMarkRadianceProbesUsedByRadiosityCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FMarkRadianceProbesUsedByRadiosityCS)
	SHADER_USE_PARAMETER_STRUCT(FMarkRadianceProbesUsedByRadiosityCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture3D<uint>, RWRadianceProbeIndirectionTexture)
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER_STRUCT_INCLUDE(LumenRadianceCache::FRadianceCacheInterpolationParameters, RadianceCacheParameters)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, DepthAtlas)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, CurrentOpacityAtlas)
		SHADER_PARAMETER_SRV(StructuredBuffer<float4>, CardBuffer)
		SHADER_PARAMETER_SRV(StructuredBuffer<float4>, CardPageBuffer)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, CardTraceBlockAllocator)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint4>, CardTraceBlockData)
		SHADER_PARAMETER(FIntPoint, RadiosityAtlasSize)
		RDG_BUFFER_ACCESS(IndirectArgs, ERHIAccess::IndirectArgs)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportLumenGI(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE"), GRadiosityTraceBlocksGroupSize);
		OutEnvironment.CompilerFlags.Add(CFLAG_Wave32);
	}
};

IMPLEMENT_GLOBAL_SHADER(FMarkRadianceProbesUsedByRadiosityCS, "/Engine/Private/Lumen/LumenRadiosity.usf", "MarkRadianceProbesUsedByRadiosityCS", SF_Compute);


const static uint32 MaxRadiosityConeDirections = 32;

BEGIN_SHADER_PARAMETER_STRUCT(FRadiosityTraceFromTexelParameters, )
	SHADER_PARAMETER_STRUCT_INCLUDE(FLumenCardTracingParameters, TracingParameters)
	SHADER_PARAMETER_STRUCT_INCLUDE(FLumenIndirectTracingParameters, IndirectTracingParameters)
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D, CurrentNormalAtlas)
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D, CurrentOpacityAtlas)
	SHADER_PARAMETER_SRV(StructuredBuffer<float4>, CardBuffer)
	SHADER_PARAMETER_SRV(StructuredBuffer<float4>, CardPageBuffer)
	SHADER_PARAMETER_ARRAY(FVector4, RadiosityConeDirections, [MaxRadiosityConeDirections])
	SHADER_PARAMETER(uint32, NumCones)
	SHADER_PARAMETER(float, SampleWeight)
	SHADER_PARAMETER(FIntPoint, RadiosityAtlasSize)
END_SHADER_PARAMETER_STRUCT()

void SetupTraceFromTexelParameters(
	FRDGBuilder& GraphBuilder,
	const FViewInfo& View, 
	const FLumenCardTracingInputs& TracingInputs, 
	const FLumenSceneData& LumenSceneData, 
	FRadiosityTraceFromTexelParameters& TraceFromTexelParameters)
{
	GetLumenCardTracingParameters(View, TracingInputs, TraceFromTexelParameters.TracingParameters);

	const float RadiosityMinTraceDistance = FMath::Clamp(GLumenRadiosityMinTraceDistance, .01f, 1000.0f);
	SetupLumenDiffuseTracingParametersForProbe(TraceFromTexelParameters.IndirectTracingParameters, GetRadiosityConeHalfAngle());
	TraceFromTexelParameters.IndirectTracingParameters.StepFactor = FMath::Clamp(GRadiosityTraceStepFactor, .1f, 10.0f);
	TraceFromTexelParameters.IndirectTracingParameters.MinSampleRadius = FMath::Clamp(GLumenRadiosityMinSampleRadius, .01f, 100.0f);
	TraceFromTexelParameters.IndirectTracingParameters.MinTraceDistance = RadiosityMinTraceDistance;
	TraceFromTexelParameters.IndirectTracingParameters.MaxTraceDistance = Lumen::GetMaxTraceDistance();
	TraceFromTexelParameters.IndirectTracingParameters.SurfaceBias = FMath::Clamp(GLumenRadiositySurfaceBias, .01f, 100.0f);
	TraceFromTexelParameters.IndirectTracingParameters.VoxelStepFactor = FMath::Clamp(GLumenRadiosityVoxelStepFactor, .1f, 10.0f);

	// Trace from this frame's cards
	TraceFromTexelParameters.CurrentNormalAtlas  = GraphBuilder.RegisterExternalTexture(LumenSceneData.NormalAtlas);
	TraceFromTexelParameters.CurrentOpacityAtlas = GraphBuilder.RegisterExternalTexture(LumenSceneData.OpacityAtlas);

	TraceFromTexelParameters.CardBuffer = LumenSceneData.CardBuffer.SRV;
	TraceFromTexelParameters.CardPageBuffer = LumenSceneData.CardPageBuffer.SRV;
	
	int32 NumSampleDirections = 0;
	const FVector4* SampleDirections = nullptr;
	RadiosityDirections.GetSampleDirections(SampleDirections, NumSampleDirections);
	TraceFromTexelParameters.SampleWeight = (GLumenRadiosityIntensity * PI * 2.0f) / (float)NumSampleDirections;

	check(NumSampleDirections <= MaxRadiosityConeDirections);

	TraceFromTexelParameters.NumCones = NumSampleDirections;
	for (int32 i = 0; i < NumSampleDirections; i++)
	{
		TraceFromTexelParameters.RadiosityConeDirections[i] = SampleDirections[i];
	}

	TraceFromTexelParameters.RadiosityAtlasSize = LumenSceneData.GetRadiosityAtlasSize();
}

class FLumenCardRadiosityTraceBlocksCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FLumenCardRadiosityTraceBlocksCS)
	SHADER_USE_PARAMETER_STRUCT(FLumenCardRadiosityTraceBlocksCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FRadiosityTraceFromTexelParameters, TraceFromTexelParameters)
		SHADER_PARAMETER_STRUCT_INCLUDE(LumenRadianceCache::FRadianceCacheInterpolationParameters, RadianceCacheParameters)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D, RWRadiosityAtlas)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, CardTraceBlockAllocator)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint4>, CardTraceBlockData)
		SHADER_PARAMETER(float, ProbeOcclusionNormalBias)
		RDG_BUFFER_ACCESS(IndirectArgs, ERHIAccess::IndirectArgs)
	END_SHADER_PARAMETER_STRUCT()

	class FDynamicSkyLight : SHADER_PERMUTATION_BOOL("ENABLE_DYNAMIC_SKY_LIGHT");
	class FIrradianceCache : SHADER_PERMUTATION_BOOL("IRRADIANCE_CACHE");

	using FPermutationDomain = TShaderPermutationDomain<FDynamicSkyLight, FIrradianceCache>;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportLumenGI(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE"), GRadiosityTraceBlocksGroupSize);
		OutEnvironment.CompilerFlags.Add(CFLAG_Wave32);
	}
};

IMPLEMENT_GLOBAL_SHADER(FLumenCardRadiosityTraceBlocksCS, "/Engine/Private/Lumen/LumenRadiosity.usf", "LumenCardRadiosityTraceBlocksCS", SF_Compute);

static void RadianceCacheMarkUsedProbes(
	FRDGBuilder& GraphBuilder,
	const FViewInfo& View,
	const FIntPoint& RadiosityAtlasSize,
	const FLumenSceneData& LumenSceneData,
	const FRDGBufferRef CardTraceBlockAllocator,
	const FRDGBufferRef CardTraceBlockData,
	const FRDGBufferRef TraceBlocksIndirectArgsBuffer,
	const LumenRadianceCache::FRadianceCacheInterpolationParameters& RadianceCacheParameters,
	FRDGTextureUAVRef RadianceProbeIndirectionTextureUAV)
{
	FMarkRadianceProbesUsedByRadiosityCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FMarkRadianceProbesUsedByRadiosityCS::FParameters>();

	PassParameters->View = View.ViewUniformBuffer;
	PassParameters->DepthAtlas = GraphBuilder.RegisterExternalTexture(LumenSceneData.DepthAtlas);
	PassParameters->CurrentOpacityAtlas = GraphBuilder.RegisterExternalTexture(LumenSceneData.OpacityAtlas);
	PassParameters->CardTraceBlockAllocator = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(CardTraceBlockAllocator, PF_R32_UINT));
	PassParameters->CardTraceBlockData = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(CardTraceBlockData, PF_R32G32B32A32_UINT));
	PassParameters->CardBuffer = LumenSceneData.CardBuffer.SRV;
	PassParameters->CardPageBuffer = LumenSceneData.CardPageBuffer.SRV;
	PassParameters->RadiosityAtlasSize = RadiosityAtlasSize;
	PassParameters->IndirectArgs = TraceBlocksIndirectArgsBuffer;

	PassParameters->RadianceCacheParameters = RadianceCacheParameters;
	PassParameters->RWRadianceProbeIndirectionTexture = RadianceProbeIndirectionTextureUAV;
	auto ComputeShader = View.ShaderMap->GetShader< FMarkRadianceProbesUsedByRadiosityCS >(0);

	FComputeShaderUtils::AddPass(
		GraphBuilder,
		RDG_EVENT_NAME("MarkRadianceProbesUsedByRadiosity"),
		ComputeShader,
		PassParameters,
		PassParameters->IndirectArgs,
		0);
};

void RenderRadiosityComputeScatter(
	FRDGBuilder& GraphBuilder,
	const FScene* Scene,
	const FViewInfo& View,
	bool bRenderSkylight, 
	const FLumenSceneData& LumenSceneData,
	FRDGTextureRef RadiosityAtlas,
	const FLumenCardTracingInputs& TracingInputs,
	const FLumenCardScatterParameters& CardScatterParameters,
	FGlobalShaderMap* GlobalShaderMap)
{
	const bool bUseIrradianceCache = GLumenRadiosityUseIrradianceCache != 0;

	FRDGBufferRef SetupCardTraceBlocksIndirectArgsBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateIndirectDesc<FRHIDispatchIndirectParameters>(1), TEXT("SetupCardTraceBlocksIndirectArgsBuffer"));
	{
		FRDGBufferUAVRef SetupCardTraceBlocksIndirectArgsBufferUAV = GraphBuilder.CreateUAV(FRDGBufferUAVDesc(SetupCardTraceBlocksIndirectArgsBuffer));

		FPlaceProbeIndirectArgsCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FPlaceProbeIndirectArgsCS::FParameters>();
		PassParameters->RWIndirectArgs = SetupCardTraceBlocksIndirectArgsBufferUAV;
		PassParameters->QuadAllocator = CardScatterParameters.QuadAllocator;

		auto ComputeShader = GlobalShaderMap->GetShader< FPlaceProbeIndirectArgsCS >(0);

		ensure(GSetupCardTraceBlocksGroupSize == GPlaceRadiosityProbeGroupSize);
		const FIntVector GroupSize(1, 1, 1);

		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("SetupCardTraceBlocksIndirectArgsCS"),
			ComputeShader,
			PassParameters,
			GroupSize);
	}

	const int32 TraceBlockMaxSize = 2;
	extern int32 GLumenSceneLightingForceFullUpdate;
	const int32 Divisor = TraceBlockMaxSize * GLumenRadiosityDownsampleFactor * (GLumenSceneLightingForceFullUpdate ? 1 : GLumenRadiosityTraceBlocksAllocationDivisor);
	const int32 NumTraceBlocksToAllocate = FMath::Max((LumenSceneData.GetPhysicalAtlasSize().X / Divisor) * (LumenSceneData.GetPhysicalAtlasSize().Y / Divisor), 1024);
	const FIntPoint RadiosityAtlasSize = LumenSceneData.GetRadiosityAtlasSize();

	FRDGBufferRef CardTraceBlockAllocator = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateBufferDesc(sizeof(uint32), 1), TEXT("CardTraceBlockAllocator"));
	FRDGBufferRef CardTraceBlockData = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateBufferDesc(sizeof(FIntVector4), NumTraceBlocksToAllocate), TEXT("CardTraceBlockData"));
	FRDGBufferUAVRef CardTraceBlockAllocatorUAV = GraphBuilder.CreateUAV(FRDGBufferUAVDesc(CardTraceBlockAllocator, PF_R32_UINT));
	FRDGBufferUAVRef CardTraceBlockDataUAV = GraphBuilder.CreateUAV(FRDGBufferUAVDesc(CardTraceBlockData, PF_R32G32B32A32_UINT));

	FComputeShaderUtils::ClearUAV(GraphBuilder, View.ShaderMap, CardTraceBlockAllocatorUAV, 0);

	{
		FSetupCardTraceBlocksCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FSetupCardTraceBlocksCS::FParameters>();
		PassParameters->RWCardTraceBlockAllocator = CardTraceBlockAllocatorUAV;
		PassParameters->RWCardTraceBlockData = CardTraceBlockDataUAV;
		PassParameters->QuadAllocator = CardScatterParameters.QuadAllocator;
		PassParameters->QuadData = CardScatterParameters.QuadData;
		PassParameters->CardBuffer = LumenSceneData.CardBuffer.SRV;
		PassParameters->CardPageBuffer = LumenSceneData.CardPageBuffer.SRV;
		PassParameters->RadiosityAtlasSize = RadiosityAtlasSize;
		PassParameters->IndirectArgs = SetupCardTraceBlocksIndirectArgsBuffer;

		auto ComputeShader = GlobalShaderMap->GetShader<FSetupCardTraceBlocksCS>();

		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("SetupCardTraceBlocksCS"),
			ComputeShader,
			PassParameters,
			SetupCardTraceBlocksIndirectArgsBuffer,
			0);
	}

	FRDGBufferRef TraceBlocksIndirectArgsBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateIndirectDesc<FRHIDispatchIndirectParameters>(1), TEXT("TraceBlocksIndirectArgsBuffer"));
	{
		FRDGBufferUAVRef TraceBlocksIndirectArgsBufferUAV = GraphBuilder.CreateUAV(FRDGBufferUAVDesc(TraceBlocksIndirectArgsBuffer));

		FTraceBlocksIndirectArgsCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FTraceBlocksIndirectArgsCS::FParameters>();
		PassParameters->RWIndirectArgs = TraceBlocksIndirectArgsBufferUAV;
		PassParameters->CardTraceBlockAllocator = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(CardTraceBlockAllocator, PF_R32_UINT));

		FTraceBlocksIndirectArgsCS::FPermutationDomain PermutationVector;
		PermutationVector.Set<FTraceBlocksIndirectArgsCS::FIrradianceCache>(bUseIrradianceCache);
		auto ComputeShader = GlobalShaderMap->GetShader< FTraceBlocksIndirectArgsCS >(PermutationVector);

		const FIntVector GroupSize(1, 1, 1);

		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("TraceBlocksIndirectArgsCS"),
			ComputeShader,
			PassParameters,
			GroupSize);
	}

	LumenRadianceCache::FRadianceCacheInterpolationParameters RadianceCacheParameters;

	if (bUseIrradianceCache)
	{
		const LumenRadianceCache::FRadianceCacheInputs RadianceCacheInputs = LumenRadiosity::SetupRadianceCacheInputs();

		FMarkUsedRadianceCacheProbes Callback;
		Callback.AddLambda([RadiosityAtlasSize, &LumenSceneData, &CardTraceBlockAllocator, &CardTraceBlockData, &TraceBlocksIndirectArgsBuffer](
			FRDGBuilder& GraphBuilder, 
			const FViewInfo& View, 
			const LumenRadianceCache::FRadianceCacheInterpolationParameters& RadianceCacheParameters, 
			FRDGTextureUAVRef RadianceProbeIndirectionTextureUAV)
			{
				RadianceCacheMarkUsedProbes(
					GraphBuilder,
					View,
					RadiosityAtlasSize,
					LumenSceneData,
					CardTraceBlockAllocator,
					CardTraceBlockData,
					TraceBlocksIndirectArgsBuffer,
					RadianceCacheParameters,
					RadianceProbeIndirectionTextureUAV);
			});

		RenderRadianceCache(
			GraphBuilder, 
			TracingInputs, 
			RadianceCacheInputs, 
			Scene,
			View, 
			nullptr, 
			nullptr, 
			Callback,
			View.ViewState->RadiosityRadianceCacheState, 
			RadianceCacheParameters);
	}

	{
		FLumenCardRadiosityTraceBlocksCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FLumenCardRadiosityTraceBlocksCS::FParameters>();
		PassParameters->RWRadiosityAtlas = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(RadiosityAtlas));
		PassParameters->RadianceCacheParameters = RadianceCacheParameters;
		PassParameters->CardTraceBlockAllocator = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(CardTraceBlockAllocator, PF_R32_UINT));
		PassParameters->CardTraceBlockData = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(CardTraceBlockData, PF_R32G32B32A32_UINT));
		PassParameters->ProbeOcclusionNormalBias = GLumenRadiosityIrradianceCacheProbeOcclusionNormalBias;
		PassParameters->IndirectArgs = TraceBlocksIndirectArgsBuffer;

		SetupTraceFromTexelParameters(GraphBuilder, View, TracingInputs, LumenSceneData, PassParameters->TraceFromTexelParameters);

		FLumenCardRadiosityTraceBlocksCS::FPermutationDomain PermutationVector;
		PermutationVector.Set<FLumenCardRadiosityTraceBlocksCS::FDynamicSkyLight>(bRenderSkylight);
		PermutationVector.Set<FLumenCardRadiosityTraceBlocksCS::FIrradianceCache>(bUseIrradianceCache);
		auto ComputeShader = GlobalShaderMap->GetShader< FLumenCardRadiosityTraceBlocksCS >(PermutationVector);

		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("TraceFromAtlasTexels: %u Cones", RadiosityDirections.SampleDirections.Num()),
			ComputeShader,
			PassParameters,
			TraceBlocksIndirectArgsBuffer,
			0);
	}
}

class FLumenCardRadiosityPS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FLumenCardRadiosityPS);
	SHADER_USE_PARAMETER_STRUCT(FLumenCardRadiosityPS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FRadiosityTraceFromTexelParameters, TraceFromTexelParameters)
	END_SHADER_PARAMETER_STRUCT()

	class FDynamicSkyLight : SHADER_PERMUTATION_BOOL("ENABLE_DYNAMIC_SKY_LIGHT");

	using FPermutationDomain = TShaderPermutationDomain<FDynamicSkyLight>;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportLumenGI(Parameters.Platform);
	}
};

IMPLEMENT_GLOBAL_SHADER(FLumenCardRadiosityPS, "/Engine/Private/Lumen/LumenRadiosity.usf", "LumenCardRadiosityPS", SF_Pixel);


BEGIN_SHADER_PARAMETER_STRUCT(FLumenCardRadiosity, )
	SHADER_PARAMETER_STRUCT_INCLUDE(FRasterizeToCardsVS::FParameters, VS)
	SHADER_PARAMETER_STRUCT_INCLUDE(FLumenCardRadiosityPS::FParameters, PS)
	RENDER_TARGET_BINDING_SLOTS()
END_SHADER_PARAMETER_STRUCT()

void FDeferredShadingSceneRenderer::RenderRadiosityForLumenScene(
	FRDGBuilder& GraphBuilder, 
	const FLumenCardTracingInputs& TracingInputs, 
	FGlobalShaderMap* GlobalShaderMap, 
	FRDGTextureRef RadiosityAtlas)
{
	LLM_SCOPE_BYTAG(Lumen);

	const FViewInfo& MainView = Views[0];
	FLumenSceneData& LumenSceneData = *Scene->LumenSceneData;

	extern int32 GLumenSceneRecaptureLumenSceneEveryFrame;

	if (IsRadiosityEnabled() 
		&& !GLumenSceneRecaptureLumenSceneEveryFrame
		&& LumenSceneData.bFinalLightingAtlasContentsValid
		&& TracingInputs.NumClipmapLevels > 0)
	{
		RDG_EVENT_SCOPE(GraphBuilder, "Radiosity");

		FLumenCardScatterContext VisibleCardScatterContext;

		// Build the indirect args to write to the card faces we are going to update radiosity for this frame
		VisibleCardScatterContext.Init(
			GraphBuilder,
			MainView,
			LumenSceneData,
			LumenCardRenderer,
			ECullCardsMode::OperateOnSceneForceUpdateForCardPagesToRender);

		VisibleCardScatterContext.CullCardPagesToShape(
			GraphBuilder,
			MainView,
			LumenSceneData,
			LumenCardRenderer,
			TracingInputs.LumenCardSceneUniformBuffer,
			ECullCardsShapeType::None,
			FCullCardsShapeParameters(),
			GLumenSceneCardRadiosityUpdateFrequencyScale,
			0);

		VisibleCardScatterContext.BuildScatterIndirectArgs(
			GraphBuilder,
			MainView);

		RadiosityDirections.GenerateSamples(
			FMath::Clamp(GLumenRadiosityNumTargetCones, 1, (int32)MaxRadiosityConeDirections),
			1,
			GLumenRadiosityNumTargetCones,
			false,
			true /* Cosine distribution */);

		const bool bRenderSkylight = Lumen::ShouldHandleSkyLight(Scene, ViewFamily);

		if (GLumenRadiosityComputeTraceBlocksScatter)
		{
			RenderRadiosityComputeScatter(
				GraphBuilder,
				Scene,
				Views[0],
				bRenderSkylight,
				LumenSceneData,
				RadiosityAtlas,
				TracingInputs,
				VisibleCardScatterContext.Parameters,
				GlobalShaderMap);
		}
		else
		{
			FLumenCardRadiosity* PassParameters = GraphBuilder.AllocParameters<FLumenCardRadiosity>();

			PassParameters->RenderTargets[0] = FRenderTargetBinding(RadiosityAtlas, ERenderTargetLoadAction::ENoAction);

			PassParameters->VS.LumenCardScene = TracingInputs.LumenCardSceneUniformBuffer;
			PassParameters->VS.CardScatterParameters = VisibleCardScatterContext.Parameters;
			PassParameters->VS.ScatterInstanceIndex = 0;
			PassParameters->VS.DownsampledInputAtlasSize = FVector2D::ZeroVector;

			SetupTraceFromTexelParameters(GraphBuilder, Views[0], TracingInputs, LumenSceneData, PassParameters->PS.TraceFromTexelParameters);

			FLumenCardRadiosityPS::FPermutationDomain PermutationVector;
			PermutationVector.Set<FLumenCardRadiosityPS::FDynamicSkyLight>(bRenderSkylight);
			auto PixelShader = GlobalShaderMap->GetShader<FLumenCardRadiosityPS>(PermutationVector);

			FScene* LocalScene = Scene;
			const int32 RadiosityDownsampleArea = GLumenRadiosityDownsampleFactor * GLumenRadiosityDownsampleFactor;
			const FIntPoint RadiosityAtlasSize = LumenSceneData.GetRadiosityAtlasSize();

			GraphBuilder.AddPass(
				RDG_EVENT_NAME("TraceFromAtlasTexels: %u Cones", RadiosityDirections.SampleDirections.Num()),
				PassParameters,
				ERDGPassFlags::Raster,
				[LocalScene, PixelShader, PassParameters, GlobalShaderMap, RadiosityAtlasSize](FRHICommandListImmediate& RHICmdList)
			{
				FIntPoint ViewRect = RadiosityAtlasSize;
				DrawQuadsToAtlas(ViewRect, PixelShader, PassParameters, GlobalShaderMap, TStaticBlendState<>::GetRHI(), RHICmdList);
			});
		}
	}
	else
	{
		AddClearRenderTargetPass(GraphBuilder, RadiosityAtlas);
	}
}