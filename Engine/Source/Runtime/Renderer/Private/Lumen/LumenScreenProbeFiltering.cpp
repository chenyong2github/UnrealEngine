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

int32 GLumenScreenProbeSpatialFilterScatter = 0;
FAutoConsoleVariableRef GVarLumenScreenProbeSpatialFilterScatter(
	TEXT("r.Lumen.ScreenProbeGather.SpatialFilterProbesUseScatter"),
	GLumenScreenProbeSpatialFilterScatter,
	TEXT("Experimental"),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

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

int32 GLumenScreenProbeGatherMultiresolutionSpatialFilter = 0;
FAutoConsoleVariableRef GVarLumenScreenProbeGatherMultiresolutionSpatialFilter(
	TEXT("r.Lumen.ScreenProbeGather.MultiresolutionSpatialFilter"),
	GLumenScreenProbeGatherMultiresolutionSpatialFilter,
	TEXT("Experimental"),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

float GLumenScreenProbeGatherMaxRayIntensity = 20;
FAutoConsoleVariableRef GVarLumenScreenProbeGatherMaxRayIntensity(
	TEXT("r.Lumen.ScreenProbeGather.MaxRayIntensity"),
	GLumenScreenProbeGatherMaxRayIntensity,
	TEXT("Clamps the maximum ray lighting intensity (with PreExposure) to reduce fireflies."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

class FScreenProbeFilterScatterTracesCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FScreenProbeFilterScatterTracesCS)
	SHADER_USE_PARAMETER_STRUCT(FScreenProbeFilterScatterTracesCS, FGlobalShader)

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

	class FThreadGroupSize : SHADER_PERMUTATION_SPARSE_INT("SCATTER_THREADGROUP_SIZE", 4, 8, 16);
	class FStructuredImportanceSampling : SHADER_PERMUTATION_BOOL("STRUCTURED_IMPORTANCE_SAMPLING");
	using FPermutationDomain = TShaderPermutationDomain<FThreadGroupSize, FStructuredImportanceSampling>;

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.CompilerFlags.Add(CFLAG_Wave32);
	}
};

IMPLEMENT_GLOBAL_SHADER(FScreenProbeFilterScatterTracesCS, "/Engine/Private/Lumen/LumenScreenProbeFiltering.usf", "ScreenProbeFilterScatterTracesCS", SF_Compute);



class FScreenProbeDecomposeTracesCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FScreenProbeDecomposeTracesCS)
	SHADER_USE_PARAMETER_STRUCT(FScreenProbeDecomposeTracesCS, FGlobalShader)

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float3>, RWScreenProbeRadiance)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, RWScreenProbeRadiance0)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, RWScreenProbeRadiance1)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, RWScreenProbeRadiance2)
		SHADER_PARAMETER_STRUCT_INCLUDE(FScreenProbeParameters, ScreenProbeParameters)
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
	END_SHADER_PARAMETER_STRUCT()

	class FStructuredImportanceSampling : SHADER_PERMUTATION_BOOL("STRUCTURED_IMPORTANCE_SAMPLING");
	class FSpatialFilter : SHADER_PERMUTATION_BOOL("SPATIAL_FILTER_PROBES");
	using FPermutationDomain = TShaderPermutationDomain<FStructuredImportanceSampling, FSpatialFilter>;

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

IMPLEMENT_GLOBAL_SHADER(FScreenProbeDecomposeTracesCS, "/Engine/Private/Lumen/LumenScreenProbeFiltering.usf", "ScreenProbeDecomposeTracesCS", SF_Compute);



class FScreenProbeFilterMultiresolutionTracesCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FScreenProbeFilterMultiresolutionTracesCS)
	SHADER_USE_PARAMETER_STRUCT(FScreenProbeFilterMultiresolutionTracesCS, FGlobalShader)

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, RWScreenProbeRadiance1)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, RWScreenProbeRadiance2)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, ScreenProbeRadiance1)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, ScreenProbeRadiance2)
		SHADER_PARAMETER_STRUCT_INCLUDE(FScreenProbeParameters, ScreenProbeParameters)
		SHADER_PARAMETER(float, SpatialFilterPositionWeightScale)
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
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

IMPLEMENT_GLOBAL_SHADER(FScreenProbeFilterMultiresolutionTracesCS, "/Engine/Private/Lumen/LumenScreenProbeFiltering.usf", "ScreenProbeFilterMultiresolutionTracesCS", SF_Compute);


class FScreenProbeRecombineTracesCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FScreenProbeRecombineTracesCS)
	SHADER_USE_PARAMETER_STRUCT(FScreenProbeRecombineTracesCS, FGlobalShader)

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float3>, RWScreenProbeRadiance)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, ScreenProbeRadiance0)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, ScreenProbeRadiance1)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, ScreenProbeRadiance2)
		SHADER_PARAMETER_STRUCT_INCLUDE(FScreenProbeParameters, ScreenProbeParameters)
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
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

IMPLEMENT_GLOBAL_SHADER(FScreenProbeRecombineTracesCS, "/Engine/Private/Lumen/LumenScreenProbeFiltering.usf", "ScreenProbeRecombineTracesCS", SF_Compute);











class FScreenProbeCompositeTracesCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FScreenProbeCompositeTracesCS)
	SHADER_USE_PARAMETER_STRUCT(FScreenProbeCompositeTracesCS, FGlobalShader)

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float3>, RWScreenProbeRadiance)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, RWScreenProbeHitDistance)
		SHADER_PARAMETER_STRUCT_INCLUDE(FScreenProbeParameters, ScreenProbeParameters)
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER(float, MaxRayIntensity)
	END_SHADER_PARAMETER_STRUCT()

	class FStructuredImportanceSampling : SHADER_PERMUTATION_BOOL("STRUCTURED_IMPORTANCE_SAMPLING");
	using FPermutationDomain = TShaderPermutationDomain<FStructuredImportanceSampling>;

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

IMPLEMENT_GLOBAL_SHADER(FScreenProbeCompositeTracesCS, "/Engine/Private/Lumen/LumenScreenProbeFiltering.usf", "ScreenProbeCompositeTracesCS", SF_Compute);


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
	FRDGTextureDesc ScreenProbeRadianceDesc(FRDGTextureDesc::Create2D(ScreenProbeParameters.ScreenProbeGatherBufferSize, PF_FloatRGB, FClearValueBinding::Black, TexCreate_ShaderResource | TexCreate_UAV));
	FRDGTextureRef ScreenProbeRadiance = GraphBuilder.CreateTexture(ScreenProbeRadianceDesc, TEXT("ScreenProbeRadiance"));

	const uint32 ScatterThreadGroupSize = FScreenProbeFilterScatterTracesCS::GetThreadGroupSize(FMath::Max(ScreenProbeParameters.ScreenProbeGatherOctahedronResolution, ScreenProbeParameters.ScreenProbeTracingOctahedronResolution));

	if (LumenScreenProbeGather::UseProbeSpatialFilter() && GLumenScreenProbeSpatialFilterScatter && ScatterThreadGroupSize != MAX_uint32)
	{
		FScreenProbeFilterScatterTracesCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FScreenProbeFilterScatterTracesCS::FParameters>();
		PassParameters->RWScreenProbeRadiance = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(ScreenProbeRadiance));
		PassParameters->SpatialFilterMaxRadianceHitAngle = FMath::Clamp<float>(GLumenScreenProbeFilterMaxRadianceHitAngle * PI / 180.0f, 0.0f, PI);
		PassParameters->SpatialFilterPositionWeightScale = GLumenScreenFilterPositionWeightScale;
		PassParameters->SpatialFilterHalfKernelSize = LumenScreenProbeGather::UseProbeSpatialFilter() ? GLumenScreenProbeSpatialFilterHalfKernelSize : 1;
		PassParameters->View = View.ViewUniformBuffer;
		PassParameters->ScreenProbeParameters = ScreenProbeParameters;

		FScreenProbeFilterScatterTracesCS::FPermutationDomain PermutationVector;
		PermutationVector.Set< FScreenProbeFilterScatterTracesCS::FThreadGroupSize >(ScatterThreadGroupSize);
		PermutationVector.Set< FScreenProbeFilterScatterTracesCS::FStructuredImportanceSampling >(LumenScreenProbeGather::UseImportanceSampling());
		auto ComputeShader = View.ShaderMap->GetShader<FScreenProbeFilterScatterTracesCS>(PermutationVector);

		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("FilterRadianceWithScatter"),
			ComputeShader,
			PassParameters,
			ScreenProbeParameters.ProbeIndirectArgs,
			(uint32)EScreenProbeIndirectArgs::GroupPerProbe * sizeof(FRHIDispatchIndirectParameters));
	}
	else
	{
		if (GLumenScreenProbeGatherMultiresolutionSpatialFilter)
		{
			FRDGTextureDesc FilterRadianceDesc(FRDGTextureDesc::Create2D(ScreenProbeParameters.ScreenProbeGatherBufferSize, PF_FloatRGBA, FClearValueBinding::Black, TexCreate_ShaderResource | TexCreate_UAV));
			FRDGTextureRef ScreenProbeRadiance0 = GraphBuilder.CreateTexture(FilterRadianceDesc, TEXT("ScreenProbeRadiance0"));
			FRDGTextureRef ScreenProbeRadiance1 = GraphBuilder.CreateTexture(FilterRadianceDesc, TEXT("ScreenProbeRadiance1"));

			FRDGTextureDesc HalfResFilterRadianceDesc = FilterRadianceDesc;
			HalfResFilterRadianceDesc.Extent = FIntPoint(ScreenProbeParameters.ScreenProbeGatherBufferSize.X / 2, ScreenProbeParameters.ScreenProbeGatherBufferSize.Y / 2);
			FRDGTextureRef ScreenProbeRadiance2 = GraphBuilder.CreateTexture(HalfResFilterRadianceDesc, TEXT("ScreenProbeRadiance2"));

			{
				FScreenProbeDecomposeTracesCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FScreenProbeDecomposeTracesCS::FParameters>();
				PassParameters->RWScreenProbeRadiance = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(ScreenProbeRadiance));
				PassParameters->RWScreenProbeRadiance0 = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(ScreenProbeRadiance0));
				PassParameters->RWScreenProbeRadiance1 = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(ScreenProbeRadiance1));
				PassParameters->RWScreenProbeRadiance2 = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(ScreenProbeRadiance2));
				PassParameters->ScreenProbeParameters = ScreenProbeParameters;
				PassParameters->View = View.ViewUniformBuffer;

				FScreenProbeDecomposeTracesCS::FPermutationDomain PermutationVector;
				PermutationVector.Set< FScreenProbeDecomposeTracesCS::FStructuredImportanceSampling >(LumenScreenProbeGather::UseImportanceSampling());
				PermutationVector.Set< FScreenProbeDecomposeTracesCS::FSpatialFilter >(LumenScreenProbeGather::UseProbeSpatialFilter());
				auto ComputeShader = View.ShaderMap->GetShader<FScreenProbeDecomposeTracesCS>(PermutationVector);

				FComputeShaderUtils::AddPass(
					GraphBuilder,
					RDG_EVENT_NAME("DecomposeTraces"),
					ComputeShader,
					PassParameters,
					ScreenProbeParameters.ProbeIndirectArgs,
					(uint32)EScreenProbeIndirectArgs::ThreadPerGather * sizeof(FRHIDispatchIndirectParameters));
			}

			if (LumenScreenProbeGather::UseProbeSpatialFilter())
			{
				{
					FRDGTextureRef NewScreenProbeRadiance1 = GraphBuilder.CreateTexture(FilterRadianceDesc, TEXT("ScreenProbeRadiance1"));
					FRDGTextureRef NewScreenProbeRadiance2 = GraphBuilder.CreateTexture(HalfResFilterRadianceDesc, TEXT("ScreenProbeRadiance2"));

					FScreenProbeFilterMultiresolutionTracesCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FScreenProbeFilterMultiresolutionTracesCS::FParameters>();
					PassParameters->RWScreenProbeRadiance1 = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(NewScreenProbeRadiance1));
					PassParameters->RWScreenProbeRadiance2 = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(NewScreenProbeRadiance2));
					PassParameters->ScreenProbeRadiance1 = ScreenProbeRadiance1;
					PassParameters->ScreenProbeRadiance2 = ScreenProbeRadiance2;
					PassParameters->ScreenProbeParameters = ScreenProbeParameters;
					PassParameters->SpatialFilterPositionWeightScale = GLumenScreenFilterPositionWeightScale;
					PassParameters->View = View.ViewUniformBuffer;

					auto ComputeShader = View.ShaderMap->GetShader<FScreenProbeFilterMultiresolutionTracesCS>(0);

					FComputeShaderUtils::AddPass(
						GraphBuilder,
						RDG_EVENT_NAME("FilterTraces"),
						ComputeShader,
						PassParameters,
						ScreenProbeParameters.ProbeIndirectArgs,
						(uint32)EScreenProbeIndirectArgs::ThreadPerGather * sizeof(FRHIDispatchIndirectParameters));

					ScreenProbeRadiance1 = NewScreenProbeRadiance1;
					ScreenProbeRadiance2 = NewScreenProbeRadiance2;
				}

				{
					FScreenProbeRecombineTracesCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FScreenProbeRecombineTracesCS::FParameters>();
					PassParameters->RWScreenProbeRadiance = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(ScreenProbeRadiance));
					PassParameters->ScreenProbeRadiance0 = ScreenProbeRadiance0;
					PassParameters->ScreenProbeRadiance1 = ScreenProbeRadiance1;
					PassParameters->ScreenProbeRadiance2 = ScreenProbeRadiance2;
					PassParameters->ScreenProbeParameters = ScreenProbeParameters;
					PassParameters->View = View.ViewUniformBuffer;

					auto ComputeShader = View.ShaderMap->GetShader<FScreenProbeRecombineTracesCS>(0);

					FComputeShaderUtils::AddPass(
						GraphBuilder,
						RDG_EVENT_NAME("RecombineTraces"),
						ComputeShader,
						PassParameters,
						ScreenProbeParameters.ProbeIndirectArgs,
						(uint32)EScreenProbeIndirectArgs::ThreadPerGather * sizeof(FRHIDispatchIndirectParameters));
				}
			}
		}
		else
		{
			FRDGTextureDesc ScreenProbeHitDistanceDesc(FRDGTextureDesc::Create2D(ScreenProbeParameters.ScreenProbeGatherBufferSize, PF_R8, FClearValueBinding::Black, TexCreate_ShaderResource | TexCreate_UAV));
			FRDGTextureRef ScreenProbeHitDistance = GraphBuilder.CreateTexture(ScreenProbeHitDistanceDesc, TEXT("ScreenProbeHitDistance"));

			{
				FScreenProbeCompositeTracesCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FScreenProbeCompositeTracesCS::FParameters>();
				PassParameters->RWScreenProbeRadiance = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(ScreenProbeRadiance));
				PassParameters->RWScreenProbeHitDistance = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(ScreenProbeHitDistance));
				PassParameters->ScreenProbeParameters = ScreenProbeParameters;
				PassParameters->View = View.ViewUniformBuffer;
				PassParameters->MaxRayIntensity = GLumenScreenProbeGatherMaxRayIntensity;

				FScreenProbeCompositeTracesCS::FPermutationDomain PermutationVector;
				PermutationVector.Set< FScreenProbeCompositeTracesCS::FStructuredImportanceSampling >(LumenScreenProbeGather::UseImportanceSampling());
				auto ComputeShader = View.ShaderMap->GetShader<FScreenProbeCompositeTracesCS>(PermutationVector);

				FComputeShaderUtils::AddPass(
					GraphBuilder,
					RDG_EVENT_NAME("CompositeTraces"),
					ComputeShader,
					PassParameters,
					ScreenProbeParameters.ProbeIndirectArgs,
					(uint32)EScreenProbeIndirectArgs::ThreadPerGather * sizeof(FRHIDispatchIndirectParameters));
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
		}
	}

	FScreenProbeGatherTemporalState& ScreenProbeGatherState = View.ViewState->Lumen.ScreenProbeGatherState;
	GraphBuilder.QueueTextureExtraction(ScreenProbeRadiance, &ScreenProbeGatherState.ImportanceSamplingHistoryScreenProbeRadiance);
	GraphBuilder.QueueTextureExtraction(ScreenProbeParameters.DownsampledDepth, &ScreenProbeGatherState.ImportanceSamplingHistoryDownsampledDepth);

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
		FRDGTextureRef FilteredScreenProbeRadiance = GraphBuilder.CreateTexture(ScreenProbeRadianceDesc, TEXT("ScreenProbeFilteredRadiance"));

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
}
