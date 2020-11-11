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

int32 GLumenRadiosity = 1;
FAutoConsoleVariableRef CVarLumenRadiosity(
	TEXT("r.Lumen.Radiosity"),
	GLumenRadiosity,
	TEXT(""),
	ECVF_RenderThreadSafe
	);

int32 GLumenRadiosityDownsampleFactor = 2;
FAutoConsoleVariableRef CVarLumenRadiosityDownsampleFactor(
	TEXT("r.Lumen.Radiosity.DownsampleFactor"),
	GLumenRadiosityDownsampleFactor,
	TEXT(""),
	ECVF_RenderThreadSafe
	);

float GRadiosityTraceStepFactor = 2;
FAutoConsoleVariableRef CVarRadiosityTraceStepFactor(
	TEXT("r.Lumen.Radiosity.TraceStepFactor"),
	GRadiosityTraceStepFactor,
	TEXT("."),
	ECVF_Scalability | ECVF_RenderThreadSafe
	);

int32 GLumenRadiosityNumTargetCones = 8;
FAutoConsoleVariableRef CVarLumenRadiosityNumTargetCones(
	TEXT("r.Lumen.Radiosity.NumCones"),
	GLumenRadiosityNumTargetCones,
	TEXT(""),
	ECVF_Scalability | ECVF_RenderThreadSafe
	);

float GLumenRadiosityMinSampleRadius = 10;
FAutoConsoleVariableRef CVarLumenRadiosityMinSampleRadius(
	TEXT("r.Lumen.Radiosity.MinSampleRadius"),
	GLumenRadiosityMinSampleRadius,
	TEXT("."),
	ECVF_Scalability | ECVF_RenderThreadSafe
	);

float GLumenRadiosityMinTraceDistance = 10;
FAutoConsoleVariableRef CVarLumenRadiosityMinTraceDistance(
	TEXT("r.Lumen.Radiosity.MinTraceDistance"),
	GLumenRadiosityMinTraceDistance,
	TEXT("."),
	ECVF_Scalability | ECVF_RenderThreadSafe
	);

float GLumenRadiositySurfaceBias = 5;
FAutoConsoleVariableRef CVarLumenRadiositySurfaceBias(
	TEXT("r.Lumen.Radiosity.SurfaceBias"),
	GLumenRadiositySurfaceBias,
	TEXT("."),
	ECVF_Scalability | ECVF_RenderThreadSafe
	);

float GLumenRadiosityConeAngleScale = 1.0f;
FAutoConsoleVariableRef CVarLumenRadiosityConeAngleScale(
	TEXT("r.Lumen.Radiosity.ConeAngleScale"),
	GLumenRadiosityConeAngleScale,
	TEXT("."),
	ECVF_Scalability | ECVF_RenderThreadSafe
	);

float GLumenRadiosityIntensity = 1.0f;
FAutoConsoleVariableRef CVarLumenRadiosityIntensity(
	TEXT("r.Lumen.Radiosity.Intensity"),
	GLumenRadiosityIntensity,
	TEXT(""),
	ECVF_RenderThreadSafe
	);

float GLumenRadiosityVoxelStepFactor = 1.0f;
FAutoConsoleVariableRef CVarRadiosityVoxelStepFactor(
	TEXT("r.Lumen.Radiosity.VoxelStepFactor"),
	GLumenRadiosityVoxelStepFactor,
	TEXT("."),
	ECVF_Scalability | ECVF_RenderThreadSafe
	);

float GLumenSceneCardRadiosityUpdateFrequencyScale = 1.0f;
FAutoConsoleVariableRef CVarLumenSceneCardRadiosityUpdateFrequencyScale(
	TEXT("r.Lumen.Radiosity.CardUpdateFrequencyScale"),
	GLumenSceneCardRadiosityUpdateFrequencyScale,
	TEXT(""),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

int32 GLumenRadiosityProbes = 0;
FAutoConsoleVariableRef CVarLumenRadiosityProbes(
	TEXT("r.Lumen.Radiosity.Probes"),
	GLumenRadiosityProbes,
	TEXT(""),
	ECVF_RenderThreadSafe
	);

float GLumenRadiosityProbeRadiusScale = 1.5f;
FAutoConsoleVariableRef CVarLumenRadiosityProbeRadiusScale(
	TEXT("r.Lumen.Radiosity.ProbeRadiusScale"),
	GLumenRadiosityProbeRadiusScale,
	TEXT("Larger probes decrease parallax error, but are more costly to update"),
	ECVF_RenderThreadSafe
);

int32 GLumenRadiosityComputeTraceBlocksScatter = 1;
FAutoConsoleVariableRef CVarLumenRadiosityComputeTraceBlocksScatter(
	TEXT("r.Lumen.Radiosity.ComputeScatter"),
	GLumenRadiosityComputeTraceBlocksScatter,
	TEXT(""),
	ECVF_Scalability | ECVF_RenderThreadSafe
	);

int32 GLumenRadiosityTraceBlocksAllocationDivisor = 2;
FAutoConsoleVariableRef CVarLumenRadiosityTraceBlocksAllocationDivisor(
	TEXT("r.Lumen.Radiosity.TraceBlocksAllocationDivisor"),
	GLumenRadiosityTraceBlocksAllocationDivisor,
	TEXT(""),
	ECVF_Scalability | ECVF_RenderThreadSafe
	);

// Must match LumenRadiosity.usf
static constexpr int32 RadiosityProbeResolution = 8;
static constexpr int32 RadiosityComposedProbeResolution = (RadiosityProbeResolution + 2); // Includes 2 texel border for bilinear filtering

bool IsRadiosityEnabled()
{
	return GLumenFastCameraMode ? false : bool(GLumenRadiosity);
}

FIntPoint GetRadiosityAtlasSize(FIntPoint MaxAtlasSize)
{
	return FIntPoint::DivideAndRoundDown(MaxAtlasSize, GLumenRadiosityDownsampleFactor);
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

class FPlaceProbesForRadiosityCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FPlaceProbesForRadiosityCS)
	SHADER_USE_PARAMETER_STRUCT(FPlaceProbesForRadiosityCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, RWRadiosityProbeData)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, QuadAllocator)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, QuadData)
		SHADER_PARAMETER_SRV(StructuredBuffer<float4>, CardBuffer)
		SHADER_PARAMETER_RDG_BUFFER(Buffer<uint>, IndirectArgs)
		SHADER_PARAMETER(float, RadiosityProbeRadiusScale)
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

IMPLEMENT_GLOBAL_SHADER(FPlaceProbesForRadiosityCS, "/Engine/Private/Lumen/LumenRadiosity.usf", "PlaceProbesForRadiosityCS", SF_Compute);

class FRadiosityProbeIndirectArgsCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FRadiosityProbeIndirectArgsCS)
	SHADER_USE_PARAMETER_STRUCT(FRadiosityProbeIndirectArgsCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWIndirectArgs)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, ProbeAllocator)
		SHADER_PARAMETER(FIntPoint, ProbeAtlasSizeInProbes)
	END_SHADER_PARAMETER_STRUCT()

	static uint32 GetGroupSize()
	{
		return 8;
	}

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportLumenGI(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE"), GetGroupSize());
	}
};

IMPLEMENT_GLOBAL_SHADER(FRadiosityProbeIndirectArgsCS, "/Engine/Private/Lumen/LumenRadiosity.usf", "RadiosityProbeIndirectArgsCS", SF_Compute);

class FTraceProbeCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FTraceProbeCS)
	SHADER_USE_PARAMETER_STRUCT(FTraceProbeCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FLumenCardTracingParameters, TracingParameters)
		SHADER_PARAMETER_STRUCT_INCLUDE(FLumenIndirectTracingParameters, IndirectTracingParameters)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, RWProbeLighting)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, ProbeAllocator)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, ProbeData)
		SHADER_PARAMETER(FIntPoint, ProbeAtlasSizeInProbes)
		SHADER_PARAMETER_RDG_BUFFER(Buffer<uint>, IndirectArgs)
	END_SHADER_PARAMETER_STRUCT()

	static uint32 GetGroupSize()
	{
		return 8;
	}

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportLumenGI(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE"), GetGroupSize());
	}
};

IMPLEMENT_GLOBAL_SHADER(FTraceProbeCS, "/Engine/Private/Lumen/LumenRadiosity.usf", "TraceProbeCS", SF_Compute);

class FComposeRadiosityProbesCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FComposeRadiosityProbesCS)
	SHADER_USE_PARAMETER_STRUCT(FComposeRadiosityProbesCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D, RWComposedProbeLighting)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, ProbeLighting)
		SHADER_PARAMETER(FIntPoint, ComposedProbeLightingAtlasSize)
		SHADER_PARAMETER_RDG_BUFFER(Buffer<uint>, IndirectArgs)
	END_SHADER_PARAMETER_STRUCT()

public:

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportLumenGI(Parameters.Platform);
	}

	static uint32 GetGroupSize()
	{
		return 8;
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE"), GetGroupSize());
	}
};

IMPLEMENT_GLOBAL_SHADER(FComposeRadiosityProbesCS, "/Engine/Private/Lumen/LumenRadiosity.usf", "ComposeRadiosityProbesCS", SF_Compute);

BEGIN_SHADER_PARAMETER_STRUCT(FProbeAtlasLighting, )
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, ProbeData)
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, ProbeLighting)
	SHADER_PARAMETER(FIntPoint, ProbeAtlasSizeInProbes)
	SHADER_PARAMETER(FVector2D, InvProbeAtlasResolution)
END_SHADER_PARAMETER_STRUCT()

void RenderRadiosityProbes(
	FRDGBuilder& GraphBuilder, 
	const FViewInfo& View,
	const FLumenSceneData& LumenSceneData,
	const FLumenCardTracingInputs& TracingInputs, 
	const FLumenCardScatterParameters& CardScatterParameters,
	FGlobalShaderMap* GlobalShaderMap,
	FProbeAtlasLighting& ProbeParameters)
{
	FRDGBufferRef PlaceProbeIndirectArgsBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateIndirectDesc<FRHIDispatchIndirectParameters>(1), TEXT("PlaceProbeIndirectArgsBuffer"));
	{
		FRDGBufferUAVRef PlaceProbeIndirectArgsBufferUAV = GraphBuilder.CreateUAV(FRDGBufferUAVDesc(PlaceProbeIndirectArgsBuffer));

		FPlaceProbeIndirectArgsCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FPlaceProbeIndirectArgsCS::FParameters>();
		PassParameters->RWIndirectArgs = PlaceProbeIndirectArgsBufferUAV;
		PassParameters->QuadAllocator = CardScatterParameters.QuadAllocator;

		auto ComputeShader = GlobalShaderMap->GetShader< FPlaceProbeIndirectArgsCS >(0);

		const FIntVector GroupSize(1, 1, 1);

		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("PlaceProbeIndirectArgsCS"),
			ComputeShader,
			PassParameters,
			GroupSize);
	}

	const int32 StableNumVisibleCards = FMath::DivideAndRoundUp(LumenSceneData.VisibleCardsIndices.Num(), 1024) * 1024;
	const FIntPoint ProbeAtlasSizeInProbes = FIntPoint(FMath::Min(StableNumVisibleCards, 256), FMath::DivideAndRoundUp(StableNumVisibleCards, 256));
	FRDGBufferRef RadiosityProbeDataBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(FVector4), StableNumVisibleCards), TEXT("RadiosityProbeData"));

	{
		FRDGBufferUAVRef RadiosityProbeDataBufferUAV = GraphBuilder.CreateUAV(FRDGBufferUAVDesc(RadiosityProbeDataBuffer, PF_A32B32G32R32F));

		FPlaceProbesForRadiosityCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FPlaceProbesForRadiosityCS::FParameters>();
		PassParameters->RWRadiosityProbeData = RadiosityProbeDataBufferUAV;
		PassParameters->QuadAllocator = CardScatterParameters.QuadAllocator;
		PassParameters->QuadData = CardScatterParameters.QuadData;
		PassParameters->CardBuffer = LumenSceneData.CardBuffer.SRV;
		PassParameters->IndirectArgs = PlaceProbeIndirectArgsBuffer;
		PassParameters->RadiosityProbeRadiusScale = FMath::Clamp(GLumenRadiosityProbeRadiusScale, 1.0f, 10.0f);

		auto ComputeShader = GlobalShaderMap->GetShader< FPlaceProbesForRadiosityCS >(0);

		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("PlaceProbesForRadiosityCS"),
			ComputeShader,
			PassParameters,
			PlaceProbeIndirectArgsBuffer,
			0);
	}

	FRDGBufferRef RadiosityProbeIndirectArgsBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateIndirectDesc<FRHIDispatchIndirectParameters>(2), TEXT("RadiosityProbeIndirectArgsBuffer"));

	{
		FRDGBufferUAVRef RadiosityProbeIndirectArgsBufferUAV = GraphBuilder.CreateUAV(FRDGBufferUAVDesc(RadiosityProbeIndirectArgsBuffer));

		FRadiosityProbeIndirectArgsCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FRadiosityProbeIndirectArgsCS::FParameters>();
		PassParameters->RWIndirectArgs = RadiosityProbeIndirectArgsBufferUAV;
		PassParameters->ProbeAllocator = CardScatterParameters.QuadAllocator;
		PassParameters->ProbeAtlasSizeInProbes = ProbeAtlasSizeInProbes;

		auto ComputeShader = GlobalShaderMap->GetShader<FRadiosityProbeIndirectArgsCS>();

		const FIntVector GroupSize(1, 1, 1);

		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("RadiosityProbeIndirectArgsCS"),
			ComputeShader,
			PassParameters,
			GroupSize);
	}

	const FIntPoint ProbeAtlasSize = FIntPoint(
		ProbeAtlasSizeInProbes.X * RadiosityProbeResolution,
		ProbeAtlasSizeInProbes.Y * RadiosityProbeResolution);

	FRDGTextureRef RadiosityProbeLightingAtlas = GraphBuilder.CreateTexture(FRDGTextureDesc::Create2D(ProbeAtlasSize, PF_FloatR11G11B10, FClearValueBinding::Black, TexCreate_ShaderResource | TexCreate_UAV), TEXT("RadiosityProbeLightingAtlas"));

	{
		FTraceProbeCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FTraceProbeCS::FParameters>();
		PassParameters->RWProbeLighting = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(RadiosityProbeLightingAtlas));
		PassParameters->ProbeData = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(RadiosityProbeDataBuffer, PF_A32B32G32R32F));
		PassParameters->ProbeAllocator = CardScatterParameters.QuadAllocator;
		PassParameters->ProbeAtlasSizeInProbes = ProbeAtlasSizeInProbes;
		GetLumenCardTracingParameters(View, TracingInputs, PassParameters->TracingParameters);
		SetupLumenDiffuseTracingParametersForProbe(PassParameters->IndirectTracingParameters, GetRadiosityConeHalfAngle());
		PassParameters->IndirectArgs = RadiosityProbeIndirectArgsBuffer;

		auto ComputeShader = GlobalShaderMap->GetShader<FTraceProbeCS>();

		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("TraceProbeCS"),
			ComputeShader,
			PassParameters,
			RadiosityProbeIndirectArgsBuffer,
			0);
	}

	const FIntPoint ComposedProbeAtlasSize = FIntPoint(
		ProbeAtlasSizeInProbes.X * RadiosityComposedProbeResolution,
		ProbeAtlasSizeInProbes.Y * RadiosityComposedProbeResolution);

	FRDGTextureRef RadiosityComposedProbeLightingAtlas = GraphBuilder.CreateTexture(FRDGTextureDesc::Create2D(ComposedProbeAtlasSize, PF_FloatR11G11B10, FClearValueBinding::Black, TexCreate_ShaderResource | TexCreate_UAV), TEXT("RadiosityComposedProbeLightingAtlas"));

	{
		FComposeRadiosityProbesCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FComposeRadiosityProbesCS::FParameters>();
		PassParameters->RWComposedProbeLighting = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(RadiosityComposedProbeLightingAtlas));
		PassParameters->ProbeLighting = RadiosityProbeLightingAtlas;
		PassParameters->ComposedProbeLightingAtlasSize = ComposedProbeAtlasSize;
		PassParameters->IndirectArgs = RadiosityProbeIndirectArgsBuffer;

		auto ComputeShader = GlobalShaderMap->GetShader<FComposeRadiosityProbesCS>();

		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("ComposeProbesCS"),
			ComputeShader,
			PassParameters,
			RadiosityProbeIndirectArgsBuffer,
			3 * 4);
	}

	ProbeParameters.ProbeLighting = RadiosityComposedProbeLightingAtlas;
	ProbeParameters.ProbeData = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(RadiosityProbeDataBuffer, PF_A32B32G32R32F));
	ProbeParameters.ProbeAtlasSizeInProbes = ProbeAtlasSizeInProbes;
	ProbeParameters.InvProbeAtlasResolution = FVector2D(1.0f, 1.0f) / FVector2D(ComposedProbeAtlasSize);
}

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
		SHADER_PARAMETER(FIntPoint, RadiosityAtlasSize)
		SHADER_PARAMETER_RDG_BUFFER(Buffer<uint>, IndirectArgs)
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

const static uint32 MaxRadiosityConeDirections = 32;

BEGIN_SHADER_PARAMETER_STRUCT(FRadiosityTraceFromTexelParameters, )
	SHADER_PARAMETER_STRUCT_INCLUDE(FLumenCardTracingParameters, TracingParameters)
	SHADER_PARAMETER_STRUCT_INCLUDE(FLumenIndirectTracingParameters, IndirectTracingParameters)
	SHADER_PARAMETER_STRUCT_INCLUDE(FProbeAtlasLighting, ProbeParameters)
	SHADER_PARAMETER_TEXTURE(Texture2D, NormalAtlas)
	SHADER_PARAMETER_TEXTURE(Texture2D, DepthBufferAtlas)
	SHADER_PARAMETER_TEXTURE(Texture2D, CurrentOpacityAtlas)
	SHADER_PARAMETER_SRV(StructuredBuffer<float4>, CardBuffer)
	SHADER_PARAMETER_ARRAY(FVector4, RadiosityConeDirections, [MaxRadiosityConeDirections])
	SHADER_PARAMETER(uint32, NumCones)
	SHADER_PARAMETER(float, SampleWeight)
	SHADER_PARAMETER(FIntPoint, RadiosityAtlasSize)
END_SHADER_PARAMETER_STRUCT()

void SetupTraceFromTexelParameters(
	const FViewInfo& View, 
	const FLumenCardTracingInputs& TracingInputs, 
	const FLumenSceneData& LumenSceneData, 
	const FProbeAtlasLighting& ProbeParameters,
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
	TraceFromTexelParameters.NormalAtlas = LumenSceneData.NormalAtlas->GetRenderTargetItem().ShaderResourceTexture;
	TraceFromTexelParameters.DepthBufferAtlas = LumenSceneData.DepthBufferAtlas->GetRenderTargetItem().ShaderResourceTexture;
	TraceFromTexelParameters.CurrentOpacityAtlas = LumenSceneData.OpacityAtlas->GetRenderTargetItem().ShaderResourceTexture;

	TraceFromTexelParameters.CardBuffer = LumenSceneData.CardBuffer.SRV;
	
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

	TraceFromTexelParameters.RadiosityAtlasSize = FIntPoint::DivideAndRoundDown(LumenSceneData.MaxAtlasSize, GLumenRadiosityDownsampleFactor);
	TraceFromTexelParameters.ProbeParameters = ProbeParameters;
}

class FLumenCardRadiosityTraceBlocksCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FLumenCardRadiosityTraceBlocksCS)
	SHADER_USE_PARAMETER_STRUCT(FLumenCardRadiosityTraceBlocksCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FRadiosityTraceFromTexelParameters, TraceFromTexelParameters)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D, RWRadiosityAtlas)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, CardTraceBlockAllocator)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint4>, CardTraceBlockData)
		SHADER_PARAMETER_RDG_BUFFER(Buffer<uint>, IndirectArgs)
	END_SHADER_PARAMETER_STRUCT()

	class FDynamicSkyLight : SHADER_PERMUTATION_BOOL("ENABLE_DYNAMIC_SKY_LIGHT");
	class FProbes : SHADER_PERMUTATION_BOOL("RADIOSITY_PROBES");

	using FPermutationDomain = TShaderPermutationDomain<FDynamicSkyLight, FProbes>;

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

void RenderRadiosityComputeScatter(
	FRDGBuilder& GraphBuilder,
	const FViewInfo& View,
	bool bRenderSkylight, 
	const FLumenSceneData& LumenSceneData,
	FRDGTextureRef RadiosityAtlas,
	const FLumenCardTracingInputs& TracingInputs,
	const FLumenCardScatterParameters& CardScatterParameters,
	const FProbeAtlasLighting& ProbeParameters,
	FGlobalShaderMap* GlobalShaderMap)
{
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
	extern int32 GLumenSceneCardLightingForceFullUpdate;
	const int32 Divisor = TraceBlockMaxSize * GLumenRadiosityDownsampleFactor * (GLumenSceneCardLightingForceFullUpdate ? 1 : GLumenRadiosityTraceBlocksAllocationDivisor);
	const int32 NumTraceBlocksToAllocate = (LumenSceneData.MaxAtlasSize.X / Divisor) 
		* (LumenSceneData.MaxAtlasSize.Y / Divisor);

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
		PassParameters->RadiosityAtlasSize = FIntPoint::DivideAndRoundDown(LumenSceneData.MaxAtlasSize, GLumenRadiosityDownsampleFactor);
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

		auto ComputeShader = GlobalShaderMap->GetShader< FTraceBlocksIndirectArgsCS >(0);

		const FIntVector GroupSize(1, 1, 1);

		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("TraceBlocksIndirectArgsCS"),
			ComputeShader,
			PassParameters,
			GroupSize);
	}

	{
		FLumenCardRadiosityTraceBlocksCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FLumenCardRadiosityTraceBlocksCS::FParameters>();
		PassParameters->RWRadiosityAtlas = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(RadiosityAtlas));
		PassParameters->CardTraceBlockAllocator = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(CardTraceBlockAllocator, PF_R32_UINT));
		PassParameters->CardTraceBlockData = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(CardTraceBlockData, PF_R32G32B32A32_UINT));
		PassParameters->IndirectArgs = TraceBlocksIndirectArgsBuffer;

		SetupTraceFromTexelParameters(View, TracingInputs, LumenSceneData, ProbeParameters, PassParameters->TraceFromTexelParameters);

		FLumenCardRadiosityTraceBlocksCS::FPermutationDomain PermutationVector;
		PermutationVector.Set<FLumenCardRadiosityTraceBlocksCS::FDynamicSkyLight>(bRenderSkylight);
		PermutationVector.Set<FLumenCardRadiosityTraceBlocksCS::FProbes>(GLumenRadiosityProbes != 0);
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
	class FProbes : SHADER_PERMUTATION_BOOL("RADIOSITY_PROBES");

	using FPermutationDomain = TShaderPermutationDomain<FDynamicSkyLight, FProbes>;

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
			ECullCardsMode::OperateOnSceneForceUpdateForCardsToRender);

		VisibleCardScatterContext.CullCardsToShape(
			GraphBuilder,
			MainView,
			LumenSceneData,
			LumenCardRenderer,
			ECullCardsShapeType::None,
			FCullCardsShapeParameters(),
			GLumenSceneCardRadiosityUpdateFrequencyScale,
			0);

		FProbeAtlasLighting ProbeParameters;

		if (GLumenRadiosityProbes)
		{
			RenderRadiosityProbes(
				GraphBuilder,
				MainView,
				LumenSceneData,
				TracingInputs,
				VisibleCardScatterContext.Parameters,
				GlobalShaderMap,
				ProbeParameters);
		}

		VisibleCardScatterContext.BuildScatterIndirectArgs(
			GraphBuilder,
			MainView);

		RadiosityDirections.GenerateSamples(
			FMath::Clamp(GLumenRadiosityNumTargetCones, 1, (int32)MaxRadiosityConeDirections),
			1,
			GLumenRadiosityNumTargetCones,
			false,
			true /* Cosine distribution */);

		const bool bRenderSkylight = ShouldRenderDynamicSkyLight(Scene, ViewFamily);

		if (GLumenRadiosityComputeTraceBlocksScatter)
		{
			RenderRadiosityComputeScatter(
				GraphBuilder,
				Views[0],
				bRenderSkylight,
				LumenSceneData,
				RadiosityAtlas,
				TracingInputs,
				VisibleCardScatterContext.Parameters,
				ProbeParameters,
				GlobalShaderMap);
		}
		else
		{
			FLumenCardRadiosity* PassParameters = GraphBuilder.AllocParameters<FLumenCardRadiosity>();

			PassParameters->RenderTargets[0] = FRenderTargetBinding(RadiosityAtlas, ERenderTargetLoadAction::ENoAction);

			PassParameters->VS.LumenCardScene = LumenSceneData.UniformBuffer;
			PassParameters->VS.CardScatterParameters = VisibleCardScatterContext.Parameters;
			PassParameters->VS.ScatterInstanceIndex = 0;
			PassParameters->VS.CardUVSamplingOffset = FVector2D::ZeroVector;

			SetupTraceFromTexelParameters(Views[0], TracingInputs, LumenSceneData, ProbeParameters, PassParameters->PS.TraceFromTexelParameters);

			FLumenCardRadiosityPS::FPermutationDomain PermutationVector;
			PermutationVector.Set<FLumenCardRadiosityPS::FDynamicSkyLight>(bRenderSkylight);
			PermutationVector.Set<FLumenCardRadiosityPS::FProbes>(GLumenRadiosityProbes != 0);
			auto PixelShader = GlobalShaderMap->GetShader<FLumenCardRadiosityPS>(PermutationVector);

			FScene* LocalScene = Scene;
			const int32 RadiosityDownsampleArea = GLumenRadiosityDownsampleFactor * GLumenRadiosityDownsampleFactor;

			GraphBuilder.AddPass(
				RDG_EVENT_NAME("TraceFromAtlasTexels: %u Cones", RadiosityDirections.SampleDirections.Num()),
				PassParameters,
				ERDGPassFlags::Raster,
				[LocalScene, PixelShader, PassParameters, GlobalShaderMap](FRHICommandListImmediate& RHICmdList)
			{
				FIntPoint ViewRect = FIntPoint::DivideAndRoundDown(LocalScene->LumenSceneData->MaxAtlasSize, GLumenRadiosityDownsampleFactor);
				DrawQuadsToAtlas(ViewRect, PixelShader, PassParameters, GlobalShaderMap, TStaticBlendState<>::GetRHI(), RHICmdList);
			});
		}
	}
	else
	{
		ClearAtlasRDG(GraphBuilder, RadiosityAtlas);
	}
}

