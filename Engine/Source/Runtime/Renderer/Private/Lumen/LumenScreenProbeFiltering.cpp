// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	LumenScreenProbeFiltering.cpp
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

int32 GLumenScreenProbeSpatialFilterNumPasses = 3;
FAutoConsoleVariableRef GVarLumenScreenProbeSpatialFilterNumPasses(
	TEXT("r.Lumen.ScreenProbeGather.SpatialFilterNumPasses"),
	GLumenScreenProbeSpatialFilterNumPasses,
	TEXT("Number of spatial filter passes"),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

int32 GLumenScreenProbeSpatialFilterHalfKernelSize = 1;
FAutoConsoleVariableRef GVarLumenScreenProbeSpatialFilterHalfKernelSize(
	TEXT("r.Lumen.ScreenProbeGather.SpatialFilterHalfKernelSize"),
	GLumenScreenProbeSpatialFilterHalfKernelSize,
	TEXT("Experimental"),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

float GLumenScreenProbeFilterMaxRadianceHitAngle = 10.0f;
FAutoConsoleVariableRef GVarLumenScreenProbeFilterMaxRadianceHitAngle(
	TEXT("r.Lumen.ScreenProbeGather.SpatialFilterMaxRadianceHitAngle"),
	GLumenScreenProbeFilterMaxRadianceHitAngle,
	TEXT("In Degrees.  Larger angles allow more filtering but lose contact shadows."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

float GLumenScreenFilterPositionWeightScale = 1000.0f;
FAutoConsoleVariableRef GVarLumenScreenFilterPositionWeightScale(
	TEXT("r.Lumen.ScreenProbeGather.SpatialFilterPositionWeightScale"),
	GLumenScreenFilterPositionWeightScale,
	TEXT("Determines how far probes can be in world space while still filtering lighting"),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

int32 GLumenScreenProbeGatherNumMips = 1;
FAutoConsoleVariableRef GVarLumenScreenProbeGatherNumMips(
	TEXT("r.Lumen.ScreenProbeGather.GatherNumMips"),
	GLumenScreenProbeGatherNumMips,
	TEXT("Number of mip maps to prepare for diffuse integration"),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

float GLumenScreenProbeGatherMaxRayIntensity = 100;
FAutoConsoleVariableRef GVarLumenScreenProbeGatherMaxRayIntensity(
	TEXT("r.Lumen.ScreenProbeGather.MaxRayIntensity"),
	GLumenScreenProbeGatherMaxRayIntensity,
	TEXT("Clamps the maximum ray lighting intensity (with PreExposure) to reduce fireflies."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

class FScreenProbeCompositeTracesWithScatterCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FScreenProbeCompositeTracesWithScatterCS)
	SHADER_USE_PARAMETER_STRUCT(FScreenProbeCompositeTracesWithScatterCS, FGlobalShader)

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float3>, RWScreenProbeRadiance)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, RWScreenProbeHitDistance)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, RWScreenProbeTraceMoving)
		SHADER_PARAMETER_STRUCT_INCLUDE(FScreenProbeParameters, ScreenProbeParameters)
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER(float, MaxRayIntensity)
	END_SHADER_PARAMETER_STRUCT()

	static uint32 GetThreadGroupSize(uint32 GatherResolution)
	{
		if (GatherResolution <= 8)
		{
			return 8;
		}
		else if (GatherResolution <= 16)
		{
			return 16;
		}
		else if (GatherResolution <= 32)
		{
			return 32;
		}
		else
		{
			return MAX_uint32;
		}
	}

	class FThreadGroupSize : SHADER_PERMUTATION_SPARSE_INT("THREADGROUP_SIZE", 8, 16, 32);
	class FStructuredImportanceSampling : SHADER_PERMUTATION_BOOL("STRUCTURED_IMPORTANCE_SAMPLING");
	using FPermutationDomain = TShaderPermutationDomain<FThreadGroupSize, FStructuredImportanceSampling>;

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

IMPLEMENT_GLOBAL_SHADER(FScreenProbeCompositeTracesWithScatterCS, "/Engine/Private/Lumen/LumenScreenProbeFiltering.usf", "ScreenProbeCompositeTracesWithScatterCS", SF_Compute);


class FScreenProbeFilterGatherTracesCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FScreenProbeFilterGatherTracesCS)
	SHADER_USE_PARAMETER_STRUCT(FScreenProbeFilterGatherTracesCS, FGlobalShader)

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float3>, RWScreenProbeRadiance)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, ScreenProbeRadiance)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, ScreenProbeHitDistance)
		SHADER_PARAMETER(float, SpatialFilterMaxRadianceHitAngle)
		SHADER_PARAMETER(float, SpatialFilterPositionWeightScale)
		SHADER_PARAMETER(int32, SpatialFilterHalfKernelSize)
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
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

IMPLEMENT_GLOBAL_SHADER(FScreenProbeFilterGatherTracesCS, "/Engine/Private/Lumen/LumenScreenProbeFiltering.usf", "ScreenProbeFilterGatherTracesCS", SF_Compute);


class FScreenProbeConvertToSphericalHarmonicCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FScreenProbeConvertToSphericalHarmonicCS)
	SHADER_USE_PARAMETER_STRUCT(FScreenProbeConvertToSphericalHarmonicCS, FGlobalShader)

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<float3>, RWScreenProbeRadianceSHAmbient)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<unorm float3>, RWScreenProbeRadianceSHDirectional)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, ScreenProbeRadiance)
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER_STRUCT_INCLUDE(FScreenProbeParameters, ScreenProbeParameters)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		FPermutationDomain PermutationVector(Parameters.PermutationId);

		if (PermutationVector.Get<FWaveOps>() && !RHISupportsWaveOperations(Parameters.Platform))
		{
			return false;
		}

		return DoesPlatformSupportLumenGI(Parameters.Platform);
	}

	static uint32 GetThreadGroupSize(uint32 GatherResolution)
	{
		if (GatherResolution <= 4)
		{
			return 4;
		}
		else if (GatherResolution <= 8)
		{
			return 8;
		}
		else if (GatherResolution <= 16)
		{
			return 16;
		}
		else
		{
			return MAX_uint32;
		}
	}

	class FThreadGroupSize : SHADER_PERMUTATION_SPARSE_INT("THREADGROUP_SIZE", 4, 8, 16);
	class FWaveOps : SHADER_PERMUTATION_BOOL("WAVE_OPS");
	using FPermutationDomain = TShaderPermutationDomain<FThreadGroupSize, FWaveOps>;

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);

		FPermutationDomain PermutationVector(Parameters.PermutationId);

		if (PermutationVector.Get<FWaveOps>())
		{
			OutEnvironment.CompilerFlags.Add(CFLAG_WaveOperations);
		}
	}
};

IMPLEMENT_GLOBAL_SHADER(FScreenProbeConvertToSphericalHarmonicCS, "/Engine/Private/Lumen/LumenScreenProbeFiltering.usf", "ScreenProbeConvertToSphericalHarmonicCS", SF_Compute);


class FScreenProbeCalculateMovingCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FScreenProbeCalculateMovingCS)
	SHADER_USE_PARAMETER_STRUCT(FScreenProbeCalculateMovingCS, FGlobalShader)

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<float>, RWScreenProbeMoving)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, ScreenProbeTraceMoving)
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER_STRUCT_INCLUDE(FScreenProbeParameters, ScreenProbeParameters)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportLumenGI(Parameters.Platform);
	}

	static uint32 GetThreadGroupSize(uint32 GatherResolution)
	{
		if (GatherResolution <= 4)
		{
			return 4;
		}
		else if (GatherResolution <= 8)
		{
			return 8;
		}
		else if (GatherResolution <= 16)
		{
			return 16;
		}
		else
		{
			return MAX_uint32;
		}
	}

	class FThreadGroupSize : SHADER_PERMUTATION_SPARSE_INT("THREADGROUP_SIZE", 4, 8, 16);
	using FPermutationDomain = TShaderPermutationDomain<FThreadGroupSize>;
};

IMPLEMENT_GLOBAL_SHADER(FScreenProbeCalculateMovingCS, "/Engine/Private/Lumen/LumenScreenProbeFiltering.usf", "ScreenProbeCalculateMovingCS", SF_Compute);


class FScreenProbeFixupBordersCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FScreenProbeFixupBordersCS)
	SHADER_USE_PARAMETER_STRUCT(FScreenProbeFixupBordersCS, FGlobalShader)

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float3>, RWScreenProbeRadiance)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, ScreenProbeRadiance)
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
		OutEnvironment.CompilerFlags.Add(CFLAG_Wave32);
	}
};

IMPLEMENT_GLOBAL_SHADER(FScreenProbeFixupBordersCS, "/Engine/Private/Lumen/LumenScreenProbeFiltering.usf", "ScreenProbeFixupBordersCS", SF_Compute);


class FScreenProbeGenerateMipLevelCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FScreenProbeGenerateMipLevelCS)
	SHADER_USE_PARAMETER_STRUCT(FScreenProbeGenerateMipLevelCS, FGlobalShader)

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float3>, RWScreenProbeRadianceWithBorderMip)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2D, ScreenProbeRadianceWithBorderParentMip)
		SHADER_PARAMETER(uint32, MipLevel)
		SHADER_PARAMETER_STRUCT_INCLUDE(FScreenProbeParameters, ScreenProbeParameters)
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
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
		OutEnvironment.CompilerFlags.Add(CFLAG_Wave32);
	}
};

IMPLEMENT_GLOBAL_SHADER(FScreenProbeGenerateMipLevelCS, "/Engine/Private/Lumen/LumenScreenProbeFiltering.usf", "ScreenProbeGenerateMipLevelCS", SF_Compute);

void FilterScreenProbes(
	FRDGBuilder& GraphBuilder, 
	const FViewInfo& View, 
	const FScreenProbeParameters& ScreenProbeParameters,
	FScreenProbeGatherParameters& GatherParameters)
{
	const FIntPoint ScreenProbeGatherBufferSize = ScreenProbeParameters.ScreenProbeAtlasBufferSize * ScreenProbeParameters.ScreenProbeGatherOctahedronResolution;
	FRDGTextureDesc ScreenProbeRadianceDesc(FRDGTextureDesc::Create2D(ScreenProbeGatherBufferSize, PF_FloatRGB, FClearValueBinding::Black, TexCreate_ShaderResource | TexCreate_UAV));
	FRDGTextureRef ScreenProbeRadiance = GraphBuilder.CreateTexture(ScreenProbeRadianceDesc, TEXT("ScreenProbeRadiance"));

	FRDGTextureDesc ScreenProbeHitDistanceDesc(FRDGTextureDesc::Create2D(ScreenProbeGatherBufferSize, PF_R8, FClearValueBinding::Black, TexCreate_ShaderResource | TexCreate_UAV));
	FRDGTextureRef ScreenProbeHitDistance = GraphBuilder.CreateTexture(ScreenProbeHitDistanceDesc, TEXT("ScreenProbeHitDistance"));
	FRDGTextureRef ScreenProbeTraceMoving = GraphBuilder.CreateTexture(ScreenProbeHitDistanceDesc, TEXT("ScreenProbeTraceMoving"));

	{
		const uint32 CompositeScatterThreadGroupSize = FScreenProbeCompositeTracesWithScatterCS::GetThreadGroupSize(FMath::Max(ScreenProbeParameters.ScreenProbeGatherOctahedronResolution, ScreenProbeParameters.ScreenProbeTracingOctahedronResolution));
		ensureMsgf(CompositeScatterThreadGroupSize != MAX_uint32, TEXT("Missing permutation for FScreenProbeCompositeTracesWithScatterCS"));
		FScreenProbeCompositeTracesWithScatterCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FScreenProbeCompositeTracesWithScatterCS::FParameters>();
		PassParameters->RWScreenProbeRadiance = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(ScreenProbeRadiance));
		PassParameters->RWScreenProbeHitDistance = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(ScreenProbeHitDistance));
		PassParameters->RWScreenProbeTraceMoving = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(ScreenProbeTraceMoving));
		PassParameters->ScreenProbeParameters = ScreenProbeParameters;
		PassParameters->View = View.ViewUniformBuffer;
		// This is used to quantize to uint during compositing, prevent poor precision
		PassParameters->MaxRayIntensity = FMath::Min(GLumenScreenProbeGatherMaxRayIntensity, 100000.0f);

		FScreenProbeCompositeTracesWithScatterCS::FPermutationDomain PermutationVector;
		PermutationVector.Set< FScreenProbeCompositeTracesWithScatterCS::FThreadGroupSize >(CompositeScatterThreadGroupSize);
		PermutationVector.Set< FScreenProbeCompositeTracesWithScatterCS::FStructuredImportanceSampling >(LumenScreenProbeGather::UseImportanceSampling(View));
		auto ComputeShader = View.ShaderMap->GetShader<FScreenProbeCompositeTracesWithScatterCS>(PermutationVector);

		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("CompositeTraces"),
			ComputeShader,
			PassParameters,
			ScreenProbeParameters.ProbeIndirectArgs,
			(uint32)EScreenProbeIndirectArgs::GroupPerProbe * sizeof(FRHIDispatchIndirectParameters));
	}

	if (LumenScreenProbeGather::UseProbeSpatialFilter() && GLumenScreenProbeSpatialFilterHalfKernelSize > 0)
	{
		for (int32 PassIndex = 0; PassIndex < GLumenScreenProbeSpatialFilterNumPasses; PassIndex++)
		{
			FRDGTextureRef FilteredScreenProbeRadiance = GraphBuilder.CreateTexture(ScreenProbeRadianceDesc, TEXT("ScreenProbeFilteredRadiance"));

			FScreenProbeFilterGatherTracesCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FScreenProbeFilterGatherTracesCS::FParameters>();
			PassParameters->RWScreenProbeRadiance = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(FilteredScreenProbeRadiance));
			PassParameters->ScreenProbeRadiance = ScreenProbeRadiance;
			PassParameters->ScreenProbeHitDistance = ScreenProbeHitDistance;
			PassParameters->SpatialFilterMaxRadianceHitAngle = FMath::Clamp<float>(GLumenScreenProbeFilterMaxRadianceHitAngle * PI / 180.0f, 0.0f, PI);
			PassParameters->SpatialFilterPositionWeightScale = GLumenScreenFilterPositionWeightScale;
			PassParameters->SpatialFilterHalfKernelSize = GLumenScreenProbeSpatialFilterHalfKernelSize;
			PassParameters->View = View.ViewUniformBuffer;
			PassParameters->ScreenProbeParameters = ScreenProbeParameters;

			auto ComputeShader = View.ShaderMap->GetShader<FScreenProbeFilterGatherTracesCS>(0);

			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("FilterRadianceWithGather"),
				ComputeShader,
				PassParameters,
				ScreenProbeParameters.ProbeIndirectArgs,
				(uint32)EScreenProbeIndirectArgs::ThreadPerGather * sizeof(FRHIDispatchIndirectParameters));

			ScreenProbeRadiance = FilteredScreenProbeRadiance;
		}
	}

	FScreenProbeGatherTemporalState& ScreenProbeGatherState = View.ViewState->Lumen.ScreenProbeGatherState;
	ConvertToExternalTexture(GraphBuilder, ScreenProbeRadiance, ScreenProbeGatherState.ImportanceSamplingHistoryScreenProbeRadiance);
	ConvertToExternalTexture(GraphBuilder, ScreenProbeParameters.ScreenProbeSceneDepth, ScreenProbeGatherState.ImportanceSamplingHistoryScreenProbeSceneDepth);

	const uint32 ConvertToSHThreadGroupSize = FScreenProbeConvertToSphericalHarmonicCS::GetThreadGroupSize(ScreenProbeParameters.ScreenProbeGatherOctahedronResolution);
	const int32 RadianceSHBufferSize = ScreenProbeParameters.ScreenProbeAtlasBufferSize.X * ScreenProbeParameters.ScreenProbeAtlasBufferSize.Y;
	const EPixelFormat SHAmbientFormat = PF_FloatRGB;
	const EPixelFormat SHDirectionalFormat = PF_FloatRGBA;
	FRDGBufferDesc ScreenProbeRadianceSHAmbientDesc = FRDGBufferDesc::CreateBufferDesc(GPixelFormats[SHAmbientFormat].BlockBytes, RadianceSHBufferSize * 1);
	FRDGBufferRef ScreenProbeRadianceSHAmbient = GraphBuilder.CreateBuffer(ScreenProbeRadianceSHAmbientDesc, TEXT("ScreenProbeRadianceSHAmbient"));
	FRDGBufferDesc ScreenProbeRadianceSHDirectionalDesc = FRDGBufferDesc::CreateBufferDesc(GPixelFormats[SHDirectionalFormat].BlockBytes, RadianceSHBufferSize * 6);
	FRDGBufferRef ScreenProbeRadianceSHDirectional = GraphBuilder.CreateBuffer(ScreenProbeRadianceSHDirectionalDesc, TEXT("ScreenProbeRadianceSHDirectional"));

	if (ConvertToSHThreadGroupSize != MAX_uint32)
	{
		FScreenProbeConvertToSphericalHarmonicCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FScreenProbeConvertToSphericalHarmonicCS::FParameters>();
		PassParameters->RWScreenProbeRadianceSHAmbient = GraphBuilder.CreateUAV(FRDGBufferUAVDesc(ScreenProbeRadianceSHAmbient, SHAmbientFormat));
		PassParameters->RWScreenProbeRadianceSHDirectional = GraphBuilder.CreateUAV(FRDGBufferUAVDesc(ScreenProbeRadianceSHDirectional, SHDirectionalFormat));
		PassParameters->ScreenProbeRadiance = ScreenProbeRadiance;
		PassParameters->View = View.ViewUniformBuffer;
		PassParameters->ScreenProbeParameters = ScreenProbeParameters;

		FScreenProbeConvertToSphericalHarmonicCS::FPermutationDomain PermutationVector;
		PermutationVector.Set< FScreenProbeConvertToSphericalHarmonicCS::FThreadGroupSize >(ConvertToSHThreadGroupSize);
		//@todo - fix or remove
		PermutationVector.Set< FScreenProbeConvertToSphericalHarmonicCS::FWaveOps >(false && GRHISupportsWaveOperations && GRHIMinimumWaveSize >= 32 && RHISupportsWaveOperations(View.GetShaderPlatform()));
		auto ComputeShader = View.ShaderMap->GetShader<FScreenProbeConvertToSphericalHarmonicCS>(PermutationVector);

		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("ConvertToSH"),
			ComputeShader,
			PassParameters,
			ScreenProbeParameters.ProbeIndirectArgs,
			(uint32)EScreenProbeIndirectArgs::GroupPerProbe * sizeof(FRHIDispatchIndirectParameters));
	}

	const uint32 CalculateMovingThreadGroupSize = FScreenProbeCalculateMovingCS::GetThreadGroupSize(ScreenProbeParameters.ScreenProbeGatherOctahedronResolution);
	const EPixelFormat ProbeMovingFormat = PF_R8;
	FRDGBufferDesc ScreenProbeMovingDesc = FRDGBufferDesc::CreateBufferDesc(GPixelFormats[ProbeMovingFormat].BlockBytes, ScreenProbeParameters.ScreenProbeAtlasBufferSize.X * ScreenProbeParameters.ScreenProbeAtlasBufferSize.Y);
	FRDGBufferRef ScreenProbeMoving = GraphBuilder.CreateBuffer(ScreenProbeMovingDesc, TEXT("ScreenProbeMoving"));

	ensureMsgf(CalculateMovingThreadGroupSize != MAX_uint32, TEXT("Unsupported gather resolution"));

	{
		FScreenProbeCalculateMovingCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FScreenProbeCalculateMovingCS::FParameters>();
		PassParameters->RWScreenProbeMoving = GraphBuilder.CreateUAV(FRDGBufferUAVDesc(ScreenProbeMoving, ProbeMovingFormat));
		PassParameters->ScreenProbeTraceMoving = ScreenProbeTraceMoving;
		PassParameters->View = View.ViewUniformBuffer;
		PassParameters->ScreenProbeParameters = ScreenProbeParameters;

		FScreenProbeCalculateMovingCS::FPermutationDomain PermutationVector;
		PermutationVector.Set< FScreenProbeCalculateMovingCS::FThreadGroupSize >(CalculateMovingThreadGroupSize);
		auto ComputeShader = View.ShaderMap->GetShader<FScreenProbeCalculateMovingCS>(PermutationVector);

		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("CalculateMoving"),
			ComputeShader,
			PassParameters,
			ScreenProbeParameters.ProbeIndirectArgs,
			(uint32)EScreenProbeIndirectArgs::GroupPerProbe * sizeof(FRHIDispatchIndirectParameters));
	}

	FRDGTextureRef ScreenProbeRadianceWithBorder;
	{
		const FIntPoint ScreenProbeGatherWithBorderBufferSize = ScreenProbeParameters.ScreenProbeAtlasBufferSize * ScreenProbeParameters.ScreenProbeGatherOctahedronResolutionWithBorder;
		FRDGTextureDesc ScreenProbeRadianceWithBorderDesc(FRDGTextureDesc::Create2D(ScreenProbeGatherWithBorderBufferSize, PF_FloatRGB, FClearValueBinding::Black, TexCreate_ShaderResource | TexCreate_UAV, GLumenScreenProbeGatherNumMips));
		ScreenProbeRadianceWithBorder = GraphBuilder.CreateTexture(ScreenProbeRadianceWithBorderDesc, TEXT("ScreenProbeFilteredRadianceWithBorder"));

		FScreenProbeFixupBordersCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FScreenProbeFixupBordersCS::FParameters>();
		PassParameters->RWScreenProbeRadiance = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(ScreenProbeRadianceWithBorder));
		PassParameters->ScreenProbeRadiance = ScreenProbeRadiance;
		PassParameters->ScreenProbeParameters = ScreenProbeParameters;

		auto ComputeShader = View.ShaderMap->GetShader<FScreenProbeFixupBordersCS>(0);

		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("FixupBorders"),
			ComputeShader,
			PassParameters,
			ScreenProbeParameters.ProbeIndirectArgs,
			(uint32)EScreenProbeIndirectArgs::ThreadPerGatherWithBorder * sizeof(FRHIDispatchIndirectParameters));
	}

	for (int32 MipLevel = 1; MipLevel < GLumenScreenProbeGatherNumMips; MipLevel++)
	{
		FScreenProbeGenerateMipLevelCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FScreenProbeGenerateMipLevelCS::FParameters>();
		PassParameters->RWScreenProbeRadianceWithBorderMip = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(ScreenProbeRadianceWithBorder, MipLevel));
		PassParameters->ScreenProbeRadianceWithBorderParentMip = GraphBuilder.CreateSRV(FRDGTextureSRVDesc::CreateForMipLevel(ScreenProbeRadianceWithBorder, MipLevel - 1));
		PassParameters->MipLevel = MipLevel;
		PassParameters->ScreenProbeParameters = ScreenProbeParameters;
		PassParameters->View = View.ViewUniformBuffer;

		auto ComputeShader = View.ShaderMap->GetShader<FScreenProbeGenerateMipLevelCS>();

		const uint32 MipSize = ScreenProbeParameters.ScreenProbeGatherOctahedronResolutionWithBorder >> MipLevel;

		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("GenerateMip"),
			ComputeShader,
			PassParameters,
			FComputeShaderUtils::GetGroupCount(ScreenProbeParameters.ScreenProbeAtlasViewSize * MipSize, FScreenProbeGenerateMipLevelCS::GetGroupSize()));
	}

	GatherParameters.ScreenProbeRadiance = ScreenProbeRadiance;
	GatherParameters.ScreenProbeRadianceWithBorder = ScreenProbeRadianceWithBorder;
	GatherParameters.ScreenProbeRadianceSHAmbient =  GraphBuilder.CreateSRV(FRDGBufferSRVDesc(ScreenProbeRadianceSHAmbient, SHAmbientFormat));
	GatherParameters.ScreenProbeRadianceSHDirectional =  GraphBuilder.CreateSRV(FRDGBufferSRVDesc(ScreenProbeRadianceSHDirectional, SHDirectionalFormat));
	GatherParameters.ScreenProbeMoving = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(ScreenProbeMoving, ProbeMovingFormat));
}
