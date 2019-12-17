// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "DeferredShadingRenderer.h"

#if RHI_RAYTRACING

#include "RayTracingSkyLight.h"
#include "ScenePrivate.h"
#include "SceneRenderTargets.h"
#include "RenderGraphBuilder.h"
#include "RenderTargetPool.h"
#include "RHIResources.h"
#include "UniformBuffer.h"
#include "RayGenShaderUtils.h"
#include "PathTracingUniformBuffers.h"
#include "SceneTextureParameters.h"
#include "ScreenSpaceDenoise.h"
#include "ClearQuad.h"
#include "PostProcess/PostProcessing.h"
#include "PostProcess/SceneFilterRendering.h"
#include "Raytracing/RaytracingOptions.h"
#include "BlueNoise.h"
#include "SceneTextureParameters.h"
#include "RayTracingDefinitions.h"
#include "RayTracingDeferredMaterials.h"

static TAutoConsoleVariable<int32> CVarRayTracingGlobalIllumination(
	TEXT("r.RayTracing.GlobalIllumination"),
	-1,
	TEXT("-1: Value driven by postprocess volume (default) \n")
	TEXT(" 0: ray tracing global illumination off \n")
	TEXT(" 1: ray tracing global illumination enabled"),
	ECVF_RenderThreadSafe
);

static int32 GRayTracingGlobalIlluminationSamplesPerPixel = -1;
static FAutoConsoleVariableRef CVarRayTracingGlobalIlluminationSamplesPerPixel(
	TEXT("r.RayTracing.GlobalIllumination.SamplesPerPixel"),
	GRayTracingGlobalIlluminationSamplesPerPixel,
	TEXT("Samples per pixel (default = -1 (driven by postprocesing volume))")
);

static float GRayTracingGlobalIlluminationMaxRayDistance = 1.0e27;
static FAutoConsoleVariableRef CVarRayTracingGlobalIlluminationMaxRayDistance(
	TEXT("r.RayTracing.GlobalIllumination.MaxRayDistance"),
	GRayTracingGlobalIlluminationMaxRayDistance,
	TEXT("Max ray distance (default = 1.0e27)")
);

static TAutoConsoleVariable<int32> CVarRayTracingGlobalIlluminationMaxBounces(
	TEXT("r.RayTracing.GlobalIlluminGlobalIllumination.MaxBouncesation"),
	-1,
	TEXT("Max bounces (default = -1 (driven by postprocesing volume))"),
	ECVF_RenderThreadSafe
);

static int32 GRayTracingGlobalIlluminationNextEventEstimationSamples = 2;
static FAutoConsoleVariableRef CVarRayTracingGlobalIlluminationNextEventEstimationSamples(
	TEXT("r.RayTracing.GlobalIllumination.NextEventEstimationSamples"),
	GRayTracingGlobalIlluminationNextEventEstimationSamples,
	TEXT("Number of sample draws for next-event estimation (default = 2)")
	TEXT("NOTE: This parameter is experimental")
);

static float GRayTracingGlobalIlluminationDiffuseThreshold = 0.01;
static FAutoConsoleVariableRef CVarRayTracingGlobalIlluminationDiffuseThreshold(
	TEXT("r.RayTracing.GlobalIllumination.DiffuseThreshold"),
	GRayTracingGlobalIlluminationDiffuseThreshold,
	TEXT("Diffuse luminance threshold for evaluating global illumination")
	TEXT("NOTE: This parameter is experimental")
);

static int32 GRayTracingGlobalIlluminationDenoiser = 1;
static FAutoConsoleVariableRef CVarRayTracingGlobalIlluminationDenoiser(
	TEXT("r.RayTracing.GlobalIllumination.Denoiser"),
	GRayTracingGlobalIlluminationDenoiser,
	TEXT("Denoising options (default = 1)")
);

static int32 GRayTracingGlobalIlluminationEvalSkyLight = 0;
static FAutoConsoleVariableRef CVarRayTracingGlobalIlluminationEvalSkyLight(
	TEXT("r.RayTracing.GlobalIllumination.EvalSkyLight"),
	GRayTracingGlobalIlluminationEvalSkyLight,
	TEXT("Evaluate SkyLight multi-bounce contribution")
	TEXT("NOTE: This parameter is experimental")
);

static int32 GRayTracingGlobalIlluminationUseRussianRoulette = 0;
static FAutoConsoleVariableRef CVarRayTracingGlobalIlluminationUseRussianRoulette(
	TEXT("r.RayTracing.GlobalIllumination.UseRussianRoulette"),
	GRayTracingGlobalIlluminationUseRussianRoulette,
	TEXT("Perform Russian Roulette to only cast diffuse rays on surfaces with brighter albedos (default = 0)")
	TEXT("NOTE: This parameter is experimental")
);

static float GRayTracingGlobalIlluminationScreenPercentage = 50.0;
static FAutoConsoleVariableRef CVarRayTracingGlobalIlluminationScreenPercentage(
	TEXT("r.RayTracing.GlobalIllumination.ScreenPercentage"),
	GRayTracingGlobalIlluminationScreenPercentage,
	TEXT("Screen percentage for ray tracing global illumination (default = 50)")
);

static TAutoConsoleVariable<int32> CVarRayTracingGlobalIlluminationEnableLightAttenuation(
	TEXT("r.RayTracing.GlobalIllumination.EnableLightAttenuation"),
	1,
	TEXT("Enables light attenuation when calculating irradiance during next-event estimation (default = 1)"),
	ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<int32> CVarRayTracingGlobalIlluminationEnableTwoSidedGeometry(
	TEXT("r.RayTracing.GlobalIllumination.EnableTwoSidedGeometry"),
	1,
	TEXT("Enables two-sided geometry when tracing GI rays (default = 1)"),
	ECVF_RenderThreadSafe
);

static int32 GRayTracingGlobalIlluminationRenderTileSize = 0;
static FAutoConsoleVariableRef CVarRayTracingGlobalIlluminationRenderTileSize(
	TEXT("r.RayTracing.GlobalIllumination.RenderTileSize"),
	GRayTracingGlobalIlluminationRenderTileSize,
	TEXT("Render ray traced global illumination in NxN pixel tiles, where each tile is submitted as separate GPU command buffer, allowing high quality rendering without triggering timeout detection. (default = 0, tiling disabled)")
);

static TAutoConsoleVariable<int32> CVarRayTracingGlobalIlluminationMaxLightCount(
	TEXT("r.RayTracing.GlobalIllumination.MaxLightCount"),
	RAY_TRACING_LIGHT_COUNT_MAXIMUM,
	TEXT("Enables two-sided geometry when tracing GI rays (default = 256)"),
	ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<int32> CVarRayTracingGlobalIlluminationEnableFinalGather(
	TEXT("r.RayTracing.GlobalIllumination.EnableFinalGather"),
	0,
	TEXT("Enables final gather algorithm for 1-bounce global illumination (default = 0)"),
	ECVF_RenderThreadSafe
);

static float GRayTracingGlobalIlluminationFinalGatherDistance = 10.0;
static FAutoConsoleVariableRef CVarRayTracingGlobalIlluminationFinalGatherDistance(
	TEXT("r.RayTracing.GlobalIllumination.FinalGather.Distance"),
	GRayTracingGlobalIlluminationFinalGatherDistance,
	TEXT("Maximum world-space distance for valid, reprojected final gather points (default = 10)")
);

static TAutoConsoleVariable<int32> CVarRayTracingGlobalIlluminationFinalGatherSortMaterials(
	TEXT("r.RayTracing.GlobalIllumination.FinalGather.SortMaterials"),
	1,
	TEXT("Sets whether refected materials will be sorted before shading\n")
	TEXT("0: Disabled\n ")
	TEXT("1: Enabled, using Trace->Sort->Trace (Default)\n"),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarRayTracingGlobalIlluminationFinalGatherSortTileSize(
	TEXT("r.RayTracing.GlobalIllumination.FinalGather.SortTileSize"),
	64,
	TEXT("Size of pixel tiles for sorted global illumination (default = 64)\n"),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarRayTracingGlobalIlluminationFinalGatherSortSize(
	TEXT("r.RayTracing.GlobalIllumination.FinalGather.SortSize"),
	5,
	TEXT("Size of horizon for material ID sort\n")
	TEXT("0: Disabled\n")
	TEXT("1: 256 Elements\n")
	TEXT("2: 512 Elements\n")
	TEXT("3: 1024 Elements\n")
	TEXT("4: 2048 Elements\n")
	TEXT("5: 4096 Elements (Default)\n"),
	ECVF_RenderThreadSafe);

DECLARE_GPU_STAT_NAMED(RayTracingGIBruteForce, TEXT("Ray Tracing GI: Brute Force"));
DECLARE_GPU_STAT_NAMED(RayTracingGIFinalGather, TEXT("Ray Tracing GI: Final Gather"));
DECLARE_GPU_STAT_NAMED(RayTracingGICreateGatherPoints, TEXT("Ray Tracing GI: Create Gather Points"));

void SetupLightParameters(
	const FScene& Scene,
	const FViewInfo& View,
	FPathTracingLightData* LightParameters)
{
	LightParameters->Count = 0;

	// Get the SkyLight color

	FSkyLightSceneProxy* SkyLight = Scene.SkyLight;
	FVector SkyLightColor = FVector(0.0f, 0.0f, 0.0f);
	if (SkyLight && SkyLight->bAffectGlobalIllumination)
	{
		SkyLightColor = FVector(SkyLight->GetEffectiveLightColor());
	}

	// Prepend SkyLight to light buffer
	// WARNING: Until ray payload encodes Light data buffer, the execution depends on this ordering!
	uint32 SkyLightIndex = 0;
	LightParameters->Type[SkyLightIndex] = 0;
	LightParameters->Color[SkyLightIndex] = SkyLightColor;
	LightParameters->Count++;

	uint32 MaxLightCount = FMath::Min(CVarRayTracingGlobalIlluminationMaxLightCount.GetValueOnRenderThread(), RAY_TRACING_LIGHT_COUNT_MAXIMUM);
	for (auto Light : Scene.Lights)
	{
		if (LightParameters->Count >= MaxLightCount) break;

		if (Light.LightSceneInfo->Proxy->HasStaticLighting() && Light.LightSceneInfo->IsPrecomputedLightingValid()) continue;
		if (!Light.LightSceneInfo->Proxy->AffectGlobalIllumination()) continue;

		FLightShaderParameters LightShaderParameters;
		Light.LightSceneInfo->Proxy->GetLightShaderParameters(LightShaderParameters);

		ELightComponentType LightComponentType = (ELightComponentType)Light.LightSceneInfo->Proxy->GetLightType();
		switch (LightComponentType)
		{
		case LightType_Directional:
		{
			LightParameters->Type[LightParameters->Count] = 2;
			LightParameters->Normal[LightParameters->Count] = LightShaderParameters.Direction;
			LightParameters->Color[LightParameters->Count] = LightShaderParameters.Color;
			LightParameters->Attenuation[LightParameters->Count] = 1.0 / LightShaderParameters.InvRadius;
			break;
		}
		case LightType_Rect:
		{
			LightParameters->Type[LightParameters->Count] = 3;
			LightParameters->Position[LightParameters->Count] = LightShaderParameters.Position;
			LightParameters->Normal[LightParameters->Count] = -LightShaderParameters.Direction;
			LightParameters->dPdu[LightParameters->Count] = FVector::CrossProduct(LightShaderParameters.Direction, LightShaderParameters.Tangent);
			LightParameters->dPdv[LightParameters->Count] = LightShaderParameters.Tangent;
			LightParameters->Color[LightParameters->Count] = LightShaderParameters.Color;
			LightParameters->Dimensions[LightParameters->Count] = FVector(2.0f * LightShaderParameters.SourceRadius, 2.0f * LightShaderParameters.SourceLength, 0.0f);
			LightParameters->Attenuation[LightParameters->Count] = 1.0 / LightShaderParameters.InvRadius;
			LightParameters->RectLightBarnCosAngle[LightParameters->Count] = LightShaderParameters.RectLightBarnCosAngle;
			LightParameters->RectLightBarnLength[LightParameters->Count] = LightShaderParameters.RectLightBarnLength;
			break;
		}
		case LightType_Point:
		default:
		{
			LightParameters->Type[LightParameters->Count] = 1;
			LightParameters->Position[LightParameters->Count] = LightShaderParameters.Position;
			// #dxr_todo: UE-72556 define these differences from Lit..
			LightParameters->Color[LightParameters->Count] = LightShaderParameters.Color / (4.0 * PI);
			float SourceRadius = 0.0; // LightShaderParameters.SourceRadius causes too much noise for little pay off at this time
			LightParameters->Dimensions[LightParameters->Count] = FVector(0.0, 0.0, SourceRadius);
			LightParameters->Attenuation[LightParameters->Count] = 1.0 / LightShaderParameters.InvRadius;
			break;
		}
		case LightType_Spot:
		{
			LightParameters->Type[LightParameters->Count] = 4;
			LightParameters->Position[LightParameters->Count] = LightShaderParameters.Position;
			LightParameters->Normal[LightParameters->Count] = -LightShaderParameters.Direction;
			// #dxr_todo: UE-72556 define these differences from Lit..
			LightParameters->Color[LightParameters->Count] = 4.0 * PI * LightShaderParameters.Color;
			float SourceRadius = 0.0; // LightShaderParameters.SourceRadius causes too much noise for little pay off at this time
			LightParameters->Dimensions[LightParameters->Count] = FVector(LightShaderParameters.SpotAngles, SourceRadius);
			LightParameters->Attenuation[LightParameters->Count] = 1.0 / LightShaderParameters.InvRadius;
			break;
		}
		};

		LightParameters->Count++;
	}
}

void SetupGlobalIlluminationSkyLightParameters(
	const FScene& Scene,
	FSkyLightData* SkyLightData)
{
	FSkyLightSceneProxy* SkyLight = Scene.SkyLight;

	SetupSkyLightParameters(Scene, SkyLightData);

	// Override the Sky Light color if it should not affect global illumination
	if (SkyLight && !SkyLight->bAffectGlobalIllumination)
	{
		SkyLightData->Color = FVector(0.0f);
	}
}

int32 GetRayTracingGlobalIlluminationSamplesPerPixel(const FViewInfo& View)
{
	int32 SamplesPerPixel = GRayTracingGlobalIlluminationSamplesPerPixel > -1 ? GRayTracingGlobalIlluminationSamplesPerPixel : View.FinalPostProcessSettings.RayTracingGISamplesPerPixel;
	return SamplesPerPixel;
}

bool ShouldRenderRayTracingGlobalIllumination(const FViewInfo& View)
{
	if (!IsRayTracingEnabled())
	{
		return (false);
	}

	if (GetRayTracingGlobalIlluminationSamplesPerPixel(View) <= 0)
	{
		return false;
	}
	
	if (GetForceRayTracingEffectsCVarValue() >= 0)
	{
		return GetForceRayTracingEffectsCVarValue() > 0;
	}

	int32 CVarRayTracingGlobalIlluminationValue = CVarRayTracingGlobalIllumination.GetValueOnRenderThread();
	if (CVarRayTracingGlobalIlluminationValue >= 0)
	{
		return (CVarRayTracingGlobalIlluminationValue > 0);
	}
	else
	{
		return View.FinalPostProcessSettings.RayTracingGIType > ERayTracingGlobalIlluminationType::Disabled;
	}
}

bool IsFinalGatherEnabled(const FViewInfo& View)
{

	int32 bEnableFinalGather = CVarRayTracingGlobalIlluminationEnableFinalGather.GetValueOnRenderThread();
	if (bEnableFinalGather >= 0)
	{
		return bEnableFinalGather > 0;
	}

	return View.FinalPostProcessSettings.RayTracingGIType == ERayTracingGlobalIlluminationType::FinalGather;
}

class FGlobalIlluminationRGS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FGlobalIlluminationRGS)
	SHADER_USE_ROOT_PARAMETER_STRUCT(FGlobalIlluminationRGS, FGlobalShader)

	class FUseAttenuationTermDim : SHADER_PERMUTATION_BOOL("USE_ATTENUATION_TERM");
	class FEnableTwoSidedGeometryDim : SHADER_PERMUTATION_BOOL("ENABLE_TWO_SIDED_GEOMETRY");

	using FPermutationDomain = TShaderPermutationDomain<FUseAttenuationTermDim, FEnableTwoSidedGeometryDim>;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return ShouldCompileRayTracingShadersForProject(Parameters.Platform);
	}

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(uint32, SamplesPerPixel)
		SHADER_PARAMETER(uint32, MaxBounces)
		SHADER_PARAMETER(uint32, UpscaleFactor)
		SHADER_PARAMETER(float, MaxRayDistanceForGI)
		SHADER_PARAMETER(float, MaxRayDistanceForAO)
		SHADER_PARAMETER(float, NextEventEstimationSamples)
		SHADER_PARAMETER(float, DiffuseThreshold)
		SHADER_PARAMETER(uint32, EvalSkyLight)
		SHADER_PARAMETER(uint32, UseRussianRoulette)
		SHADER_PARAMETER(float, MaxNormalBias)
		SHADER_PARAMETER(uint32, RenderTileOffsetX)
		SHADER_PARAMETER(uint32, RenderTileOffsetY)

		SHADER_PARAMETER_SRV(RaytracingAccelerationStructure, TLAS)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, RWGlobalIlluminationUAV)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, RWRayDistanceUAV)
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, ViewUniformBuffer)
		SHADER_PARAMETER_STRUCT_REF(FHaltonIteration, HaltonIteration)
		SHADER_PARAMETER_STRUCT_REF(FHaltonPrimes, HaltonPrimes)
		SHADER_PARAMETER_STRUCT_REF(FBlueNoise, BlueNoise)
		SHADER_PARAMETER_STRUCT_REF(FPathTracingLightData, LightParameters)
		SHADER_PARAMETER_STRUCT_REF(FSkyLightData, SkyLight)
		SHADER_PARAMETER_STRUCT_INCLUDE(FSceneTextureParameters, SceneTextures)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SSProfilesTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, TransmissionProfilesLinearSampler)
	END_SHADER_PARAMETER_STRUCT()
};

IMPLEMENT_GLOBAL_SHADER(FGlobalIlluminationRGS, "/Engine/Private/RayTracing/RayTracingGlobalIlluminationRGS.usf", "GlobalIlluminationRGS", SF_RayGen);

// Note: This constant must match the definition in RayTracingGatherPoints.ush
constexpr int32 MAXIMUM_GATHER_POINTS_PER_PIXEL = 32;

struct FGatherPoint
{
	FVector CreationPoint;
	FVector Position;
	FIntPoint Irradiance;
};

class FRayTracingGlobalIlluminationCreateGatherPointsRGS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FRayTracingGlobalIlluminationCreateGatherPointsRGS)
	SHADER_USE_ROOT_PARAMETER_STRUCT(FRayTracingGlobalIlluminationCreateGatherPointsRGS, FGlobalShader)

		class FUseAttenuationTermDim : SHADER_PERMUTATION_BOOL("USE_ATTENUATION_TERM");
	class FEnableTwoSidedGeometryDim : SHADER_PERMUTATION_BOOL("ENABLE_TWO_SIDED_GEOMETRY");
	class FDeferredMaterialMode : SHADER_PERMUTATION_ENUM_CLASS("DIM_DEFERRED_MATERIAL_MODE", EDeferredMaterialMode);

	using FPermutationDomain = TShaderPermutationDomain<FUseAttenuationTermDim, FEnableTwoSidedGeometryDim, FDeferredMaterialMode>;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return ShouldCompileRayTracingShadersForProject(Parameters.Platform);
	}

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(uint32, GatherSamplesPerPixel)
		SHADER_PARAMETER(uint32, SamplesPerPixel)
		SHADER_PARAMETER(uint32, SampleIndex)
		SHADER_PARAMETER(uint32, MaxBounces)
		SHADER_PARAMETER(uint32, UpscaleFactor)
		SHADER_PARAMETER(uint32, RenderTileOffsetX)
		SHADER_PARAMETER(uint32, RenderTileOffsetY)
		SHADER_PARAMETER(float, MaxRayDistanceForGI)
		SHADER_PARAMETER(float, NextEventEstimationSamples)
		SHADER_PARAMETER(float, DiffuseThreshold)
		SHADER_PARAMETER(float, MaxNormalBias)
		SHADER_PARAMETER(uint32, EvalSkyLight)
		SHADER_PARAMETER(uint32, UseRussianRoulette)

		// Scene data
		SHADER_PARAMETER_SRV(RaytracingAccelerationStructure, TLAS)
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, ViewUniformBuffer)

		// Sampling sequence
		SHADER_PARAMETER_STRUCT_REF(FHaltonIteration, HaltonIteration)
		SHADER_PARAMETER_STRUCT_REF(FHaltonPrimes, HaltonPrimes)
		SHADER_PARAMETER_STRUCT_REF(FBlueNoise, BlueNoise)

		// Light data
		SHADER_PARAMETER_STRUCT_REF(FPathTracingLightData, LightParameters)
		SHADER_PARAMETER_STRUCT_REF(FSkyLightData, SkyLight)

		// Shading data
		SHADER_PARAMETER_STRUCT_INCLUDE(FSceneTextureParameters, SceneTextures)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SSProfilesTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, TransmissionProfilesLinearSampler)

		SHADER_PARAMETER(FIntPoint, GatherPointsResolution)
		SHADER_PARAMETER(FIntPoint, TileAlignedResolution)
		SHADER_PARAMETER(int32, SortTileSize)

		// Output
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<GatherPoints>, RWGatherPointsBuffer)
		// Optional indirection buffer used for sorted materials
		SHADER_PARAMETER_RDG_BUFFER_UAV(StructuredBuffer<FDeferredMaterialPayload>, MaterialBuffer)
		END_SHADER_PARAMETER_STRUCT()
};

IMPLEMENT_GLOBAL_SHADER(FRayTracingGlobalIlluminationCreateGatherPointsRGS, "/Engine/Private/RayTracing/RayTracingCreateGatherPointsRGS.usf", "RayTracingCreateGatherPointsRGS", SF_RayGen);

class FRayTracingGlobalIlluminationCreateGatherPointsTraceRGS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FRayTracingGlobalIlluminationCreateGatherPointsTraceRGS)
	SHADER_USE_ROOT_PARAMETER_STRUCT(FRayTracingGlobalIlluminationCreateGatherPointsTraceRGS, FGlobalShader)

		class FUseAttenuationTermDim : SHADER_PERMUTATION_BOOL("USE_ATTENUATION_TERM");
	class FEnableTwoSidedGeometryDim : SHADER_PERMUTATION_BOOL("ENABLE_TWO_SIDED_GEOMETRY");
	class FDeferredMaterialMode : SHADER_PERMUTATION_ENUM_CLASS("DIM_DEFERRED_MATERIAL_MODE", EDeferredMaterialMode);

	using FPermutationDomain = TShaderPermutationDomain<FUseAttenuationTermDim, FEnableTwoSidedGeometryDim, FDeferredMaterialMode>;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return ShouldCompileRayTracingShadersForProject(Parameters.Platform);
	}

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(uint32, GatherSamplesPerPixel)
		SHADER_PARAMETER(uint32, SamplesPerPixel)
		SHADER_PARAMETER(uint32, SampleIndex)
		SHADER_PARAMETER(uint32, MaxBounces)
		SHADER_PARAMETER(uint32, UpscaleFactor)
		SHADER_PARAMETER(uint32, RenderTileOffsetX)
		SHADER_PARAMETER(uint32, RenderTileOffsetY)
		SHADER_PARAMETER(float, MaxRayDistanceForGI)
		SHADER_PARAMETER(float, NextEventEstimationSamples)
		SHADER_PARAMETER(float, DiffuseThreshold)
		SHADER_PARAMETER(float, MaxNormalBias)
		SHADER_PARAMETER(uint32, EvalSkyLight)
		SHADER_PARAMETER(uint32, UseRussianRoulette)

		// Scene data
		SHADER_PARAMETER_SRV(RaytracingAccelerationStructure, TLAS)
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, ViewUniformBuffer)

		// Sampling sequence
		SHADER_PARAMETER_STRUCT_REF(FHaltonIteration, HaltonIteration)
		SHADER_PARAMETER_STRUCT_REF(FHaltonPrimes, HaltonPrimes)
		SHADER_PARAMETER_STRUCT_REF(FBlueNoise, BlueNoise)

		// Light data
		SHADER_PARAMETER_STRUCT_REF(FPathTracingLightData, LightParameters)
		SHADER_PARAMETER_STRUCT_REF(FSkyLightData, SkyLight)

		// Shading data
		SHADER_PARAMETER_STRUCT_INCLUDE(FSceneTextureParameters, SceneTextures)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SSProfilesTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, TransmissionProfilesLinearSampler)

		SHADER_PARAMETER(FIntPoint, GatherPointsResolution)
		SHADER_PARAMETER(FIntPoint, TileAlignedResolution)
		SHADER_PARAMETER(int32, SortTileSize)

		// Output
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<GatherPoints>, RWGatherPointsBuffer)
		// Optional indirection buffer used for sorted materials
		SHADER_PARAMETER_RDG_BUFFER_UAV(StructuredBuffer<FDeferredMaterialPayload>, MaterialBuffer)
		END_SHADER_PARAMETER_STRUCT()
};

IMPLEMENT_GLOBAL_SHADER(FRayTracingGlobalIlluminationCreateGatherPointsTraceRGS, "/Engine/Private/RayTracing/RayTracingCreateGatherPointsRGS.usf", "RayTracingCreateGatherPointsTraceRGS", SF_RayGen);

class FRayTracingGlobalIlluminationFinalGatherRGS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FRayTracingGlobalIlluminationFinalGatherRGS)
	SHADER_USE_ROOT_PARAMETER_STRUCT(FRayTracingGlobalIlluminationFinalGatherRGS, FGlobalShader)

		class FUseAttenuationTermDim : SHADER_PERMUTATION_BOOL("USE_ATTENUATION_TERM");
	class FEnableTwoSidedGeometryDim : SHADER_PERMUTATION_BOOL("ENABLE_TWO_SIDED_GEOMETRY");

	using FPermutationDomain = TShaderPermutationDomain<FUseAttenuationTermDim, FEnableTwoSidedGeometryDim>;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return ShouldCompileRayTracingShadersForProject(Parameters.Platform);
	}

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(uint32, SampleIndex)
		SHADER_PARAMETER(uint32, SamplesPerPixel)
		SHADER_PARAMETER(uint32, UpscaleFactor)
		SHADER_PARAMETER(uint32, RenderTileOffsetX)
		SHADER_PARAMETER(uint32, RenderTileOffsetY)
		SHADER_PARAMETER(float, DiffuseThreshold)
		SHADER_PARAMETER(float, MaxNormalBias)
		SHADER_PARAMETER(float, FinalGatherDistance)

		// Scene data
		SHADER_PARAMETER_SRV(RaytracingAccelerationStructure, TLAS)
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, ViewUniformBuffer)

		// Shading data
		SHADER_PARAMETER_STRUCT_INCLUDE(FSceneTextureParameters, SceneTextures)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SSProfilesTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, TransmissionProfilesLinearSampler)

		// Gather points
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<GatherPoints>, GatherPointsBuffer)
		SHADER_PARAMETER(FIntPoint, GatherPointsResolution)

		// Output
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, RWGlobalIlluminationUAV)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, RWRayDistanceUAV)
		END_SHADER_PARAMETER_STRUCT()
};

IMPLEMENT_GLOBAL_SHADER(FRayTracingGlobalIlluminationFinalGatherRGS, "/Engine/Private/RayTracing/RayTracingFinalGatherRGS.usf", "RayTracingFinalGatherRGS", SF_RayGen);

void FDeferredShadingSceneRenderer::PrepareRayTracingGlobalIllumination(const FViewInfo& View, TArray<FRHIRayTracingShader*>& OutRayGenShaders)
{
	const bool bSortMaterials = CVarRayTracingGlobalIlluminationFinalGatherSortMaterials.GetValueOnRenderThread() != 0;

	// Declare all RayGen shaders that require material closest hit shaders to be bound
	for (int UseAttenuationTerm = 0; UseAttenuationTerm < 2; ++UseAttenuationTerm)
	{
		for (int EnableTwoSidedGeometry = 0; EnableTwoSidedGeometry < 2; ++EnableTwoSidedGeometry)
		{
			FGlobalIlluminationRGS::FPermutationDomain PermutationVector;
			PermutationVector.Set<FGlobalIlluminationRGS::FUseAttenuationTermDim>(CVarRayTracingGlobalIlluminationEnableLightAttenuation.GetValueOnRenderThread() != 0);
			PermutationVector.Set<FGlobalIlluminationRGS::FEnableTwoSidedGeometryDim>(EnableTwoSidedGeometry == 1);
			TShaderMapRef<FGlobalIlluminationRGS> RayGenerationShader(View.ShaderMap, PermutationVector);
			OutRayGenShaders.Add(RayGenerationShader->GetRayTracingShader());

			if (bSortMaterials)
			{
				// Gather
				{
					FRayTracingGlobalIlluminationCreateGatherPointsTraceRGS::FPermutationDomain CreateGatherPointsPermutationVector;
					CreateGatherPointsPermutationVector.Set<FRayTracingGlobalIlluminationCreateGatherPointsTraceRGS::FUseAttenuationTermDim>(UseAttenuationTerm == 1);
					CreateGatherPointsPermutationVector.Set<FRayTracingGlobalIlluminationCreateGatherPointsTraceRGS::FEnableTwoSidedGeometryDim>(EnableTwoSidedGeometry == 1);
					CreateGatherPointsPermutationVector.Set<FRayTracingGlobalIlluminationCreateGatherPointsTraceRGS::FDeferredMaterialMode>(EDeferredMaterialMode::Gather);
					TShaderMapRef<FRayTracingGlobalIlluminationCreateGatherPointsTraceRGS> CreateGatherPointsRayGenerationShader(View.ShaderMap, CreateGatherPointsPermutationVector);
					OutRayGenShaders.Add(CreateGatherPointsRayGenerationShader->GetRayTracingShader());
				}

				// Shade
				{
					FRayTracingGlobalIlluminationCreateGatherPointsRGS::FPermutationDomain CreateGatherPointsPermutationVector;
					CreateGatherPointsPermutationVector.Set<FRayTracingGlobalIlluminationCreateGatherPointsRGS::FUseAttenuationTermDim>(UseAttenuationTerm == 1);
					CreateGatherPointsPermutationVector.Set<FRayTracingGlobalIlluminationCreateGatherPointsRGS::FEnableTwoSidedGeometryDim>(EnableTwoSidedGeometry == 1);
					CreateGatherPointsPermutationVector.Set<FRayTracingGlobalIlluminationCreateGatherPointsRGS::FDeferredMaterialMode>(EDeferredMaterialMode::Shade);
					TShaderMapRef<FRayTracingGlobalIlluminationCreateGatherPointsRGS> CreateGatherPointsRayGenerationShader(View.ShaderMap, CreateGatherPointsPermutationVector);
					OutRayGenShaders.Add(CreateGatherPointsRayGenerationShader->GetRayTracingShader());
				}
			}
			else
			{
				FRayTracingGlobalIlluminationCreateGatherPointsRGS::FPermutationDomain CreateGatherPointsPermutationVector;
				CreateGatherPointsPermutationVector.Set<FRayTracingGlobalIlluminationCreateGatherPointsRGS::FUseAttenuationTermDim>(UseAttenuationTerm == 1);
				CreateGatherPointsPermutationVector.Set<FRayTracingGlobalIlluminationCreateGatherPointsRGS::FEnableTwoSidedGeometryDim>(EnableTwoSidedGeometry == 1);
				CreateGatherPointsPermutationVector.Set<FRayTracingGlobalIlluminationCreateGatherPointsRGS::FDeferredMaterialMode>(EDeferredMaterialMode::None);
				TShaderMapRef<FRayTracingGlobalIlluminationCreateGatherPointsRGS> CreateGatherPointsRayGenerationShader(View.ShaderMap, CreateGatherPointsPermutationVector);
				OutRayGenShaders.Add(CreateGatherPointsRayGenerationShader->GetRayTracingShader());
			}

			FRayTracingGlobalIlluminationFinalGatherRGS::FPermutationDomain GatherPassPermutationVector;
			GatherPassPermutationVector.Set<FRayTracingGlobalIlluminationFinalGatherRGS::FUseAttenuationTermDim>(UseAttenuationTerm == 1);
			GatherPassPermutationVector.Set<FRayTracingGlobalIlluminationFinalGatherRGS::FEnableTwoSidedGeometryDim>(EnableTwoSidedGeometry == 1);
			TShaderMapRef<FRayTracingGlobalIlluminationFinalGatherRGS> GatherPassRayGenerationShader(View.ShaderMap, GatherPassPermutationVector);
			OutRayGenShaders.Add(GatherPassRayGenerationShader->GetRayTracingShader());
		}
	}

}

#endif // RHI_RAYTRACING

bool FDeferredShadingSceneRenderer::RenderRayTracingGlobalIllumination(
	FRDGBuilder& GraphBuilder, 
	FSceneTextureParameters& SceneTextures,
	FViewInfo& View,
	IScreenSpaceDenoiser::FAmbientOcclusionRayTracingConfig* OutRayTracingConfig,
	IScreenSpaceDenoiser::FDiffuseIndirectInputs* OutDenoiserInputs)
#if RHI_RAYTRACING
{	
	if (!View.ViewState) return false;

	int32 RayTracingGISamplesPerPixel = GetRayTracingGlobalIlluminationSamplesPerPixel(View);
	if (RayTracingGISamplesPerPixel <= 0) return false;

	OutRayTracingConfig->ResolutionFraction = 1.0;
	if (GRayTracingGlobalIlluminationDenoiser != 0)
	{
		OutRayTracingConfig->ResolutionFraction = FMath::Clamp(GRayTracingGlobalIlluminationScreenPercentage / 100.0, 0.25, 1.0);
	}

	OutRayTracingConfig->RayCountPerPixel = RayTracingGISamplesPerPixel;
	
	int32 UpscaleFactor = int32(1.0 / OutRayTracingConfig->ResolutionFraction);

	// Allocate input for the denoiser.
	{
		FRDGTextureDesc Desc = FRDGTextureDesc::Create2DDesc(
			SceneTextures.SceneDepthBuffer->Desc.Extent / UpscaleFactor,
			PF_FloatRGBA,
			FClearValueBinding::None,
			/* InFlags = */ TexCreate_None,
			/* InTargetableFlags = */ TexCreate_ShaderResource | TexCreate_RenderTargetable | TexCreate_UAV,
			/* bInForceSeparateTargetAndShaderResource = */ false);

		OutDenoiserInputs->Color = GraphBuilder.CreateTexture(Desc, TEXT("RayTracingDiffuseIndirect"));

		Desc.Format = PF_G16R16;
		OutDenoiserInputs->RayHitDistance = GraphBuilder.CreateTexture(Desc, TEXT("RayTracingDiffuseIndirectHitDistance"));
	}

	// Ray generation pass
	if (IsFinalGatherEnabled(View))
	{
		RenderRayTracingGlobalIlluminationFinalGather(GraphBuilder, SceneTextures, View, *OutRayTracingConfig, UpscaleFactor, OutDenoiserInputs);
	}
	else
	{
		RenderRayTracingGlobalIlluminationBruteForce(GraphBuilder, SceneTextures, View, *OutRayTracingConfig, UpscaleFactor, OutDenoiserInputs);
	}
	return true;
}
#else
{
	unimplemented();
	return false;
}
#endif // RHI_RAYTRACING

#if RHI_RAYTRACING
void CopyGatherPassParameters(
	const FRayTracingGlobalIlluminationCreateGatherPointsRGS::FParameters& PassParameters,
	FRayTracingGlobalIlluminationCreateGatherPointsTraceRGS::FParameters* NewParameters
)
{
	NewParameters->GatherSamplesPerPixel = PassParameters.GatherSamplesPerPixel;
	NewParameters->SamplesPerPixel = PassParameters.SamplesPerPixel;
	NewParameters->SampleIndex = PassParameters.SampleIndex;
	NewParameters->MaxBounces = PassParameters.MaxBounces;
	NewParameters->UpscaleFactor = PassParameters.UpscaleFactor;
	NewParameters->RenderTileOffsetX = PassParameters.RenderTileOffsetX;
	NewParameters->RenderTileOffsetY = PassParameters.RenderTileOffsetY;
	NewParameters->MaxRayDistanceForGI = PassParameters.MaxRayDistanceForGI;
	NewParameters->NextEventEstimationSamples = PassParameters.NextEventEstimationSamples;
	NewParameters->DiffuseThreshold = PassParameters.DiffuseThreshold;
	NewParameters->MaxNormalBias = PassParameters.MaxNormalBias;
	NewParameters->EvalSkyLight = PassParameters.EvalSkyLight;
	NewParameters->UseRussianRoulette = PassParameters.UseRussianRoulette;

	NewParameters->TLAS = PassParameters.TLAS;
	NewParameters->ViewUniformBuffer = PassParameters.ViewUniformBuffer;

	NewParameters->HaltonIteration = PassParameters.HaltonIteration;
	NewParameters->HaltonPrimes = PassParameters.HaltonPrimes;
	NewParameters->BlueNoise = PassParameters.BlueNoise;

	NewParameters->LightParameters = PassParameters.LightParameters;
	NewParameters->SkyLight = PassParameters.SkyLight;

	NewParameters->SceneTextures = PassParameters.SceneTextures;
	NewParameters->SSProfilesTexture = PassParameters.SSProfilesTexture;
	NewParameters->TransmissionProfilesLinearSampler = PassParameters.TransmissionProfilesLinearSampler;

	NewParameters->GatherPointsResolution = PassParameters.GatherPointsResolution;
	NewParameters->TileAlignedResolution = PassParameters.TileAlignedResolution;
	NewParameters->SortTileSize = PassParameters.SortTileSize;

	NewParameters->RWGatherPointsBuffer = PassParameters.RWGatherPointsBuffer;
	NewParameters->MaterialBuffer = PassParameters.MaterialBuffer;
}
#endif // RHI_RAYTRACING

void FDeferredShadingSceneRenderer::RayTracingGlobalIlluminationCreateGatherPoints(
	FRDGBuilder& GraphBuilder,
	FSceneTextureParameters& SceneTextures,
	FViewInfo& View,
	int32 UpscaleFactor,
	FRDGBufferRef& GatherPointsBuffer,
	FIntVector& GatherPointsResolution
)
#if RHI_RAYTRACING
{
	RDG_GPU_STAT_SCOPE(GraphBuilder, RayTracingGICreateGatherPoints);
	RDG_EVENT_SCOPE(GraphBuilder, "Ray Tracing GI: Create Gather Points");

	int32 GatherSamples = FMath::Min(GetRayTracingGlobalIlluminationSamplesPerPixel(View), MAXIMUM_GATHER_POINTS_PER_PIXEL);
	int32 SamplesPerPixel = 1;

	uint32 IterationCount = SamplesPerPixel;
	uint32 SequenceCount = 1;
	uint32 DimensionCount = 24;
	int32 FrameIndex = View.ViewState->FrameIndex % 1024;
	FHaltonSequenceIteration HaltonSequenceIteration(Scene->HaltonSequence, IterationCount, SequenceCount, DimensionCount, FrameIndex);

	FHaltonIteration HaltonIteration;
	InitializeHaltonSequenceIteration(HaltonSequenceIteration, HaltonIteration);

	FHaltonPrimes HaltonPrimes;
	InitializeHaltonPrimes(Scene->HaltonPrimesResource, HaltonPrimes);

	FBlueNoise BlueNoise;
	InitializeBlueNoise(BlueNoise);

	FPathTracingLightData LightParameters;
	SetupLightParameters(*Scene, View, &LightParameters);

	if (Scene->SkyLight && Scene->SkyLight->ShouldRebuildCdf())
	{
		// TODO: should be converted to RDG.
		BuildSkyLightCdfs(GraphBuilder.RHICmdList, Scene->SkyLight);
	}
	FSkyLightData SkyLightParameters;
	SetupGlobalIlluminationSkyLightParameters(*Scene, &SkyLightParameters);

	FRayTracingGlobalIlluminationCreateGatherPointsRGS::FParameters* PassParameters = GraphBuilder.AllocParameters<FRayTracingGlobalIlluminationCreateGatherPointsRGS::FParameters>();
	PassParameters->SampleIndex = (FrameIndex * SamplesPerPixel) % GatherSamples;
	PassParameters->GatherSamplesPerPixel = GatherSamples;
	PassParameters->SamplesPerPixel = SamplesPerPixel;
	PassParameters->MaxBounces = 1;
	PassParameters->MaxNormalBias = GetRaytracingMaxNormalBias();
	PassParameters->MaxRayDistanceForGI = GRayTracingGlobalIlluminationMaxRayDistance;
	PassParameters->EvalSkyLight = GRayTracingGlobalIlluminationEvalSkyLight != 0;
	PassParameters->UseRussianRoulette = GRayTracingGlobalIlluminationUseRussianRoulette != 0;
	PassParameters->DiffuseThreshold = GRayTracingGlobalIlluminationDiffuseThreshold;
	PassParameters->NextEventEstimationSamples = GRayTracingGlobalIlluminationNextEventEstimationSamples;
	PassParameters->UpscaleFactor = UpscaleFactor;
	PassParameters->RenderTileOffsetX = 0;
	PassParameters->RenderTileOffsetY = 0;

	// Global
	PassParameters->TLAS = View.RayTracingScene.RayTracingSceneRHI->GetShaderResourceView();
	PassParameters->ViewUniformBuffer = View.ViewUniformBuffer;

	// Sampling sequence
	PassParameters->HaltonIteration = CreateUniformBufferImmediate(HaltonIteration, EUniformBufferUsage::UniformBuffer_SingleDraw);
	PassParameters->HaltonPrimes = CreateUniformBufferImmediate(HaltonPrimes, EUniformBufferUsage::UniformBuffer_SingleDraw);
	PassParameters->BlueNoise = CreateUniformBufferImmediate(BlueNoise, EUniformBufferUsage::UniformBuffer_SingleDraw);

	// Light data
	PassParameters->LightParameters = CreateUniformBufferImmediate(LightParameters, EUniformBufferUsage::UniformBuffer_SingleDraw);
	PassParameters->SceneTextures = SceneTextures;
	PassParameters->SkyLight = CreateUniformBufferImmediate(SkyLightParameters, EUniformBufferUsage::UniformBuffer_SingleDraw);

	// Shading data
	TRefCountPtr<IPooledRenderTarget> SubsurfaceProfileRT((IPooledRenderTarget*)GetSubsufaceProfileTexture_RT(GraphBuilder.RHICmdList));
	if (!SubsurfaceProfileRT)
	{
		SubsurfaceProfileRT = GSystemTextures.BlackDummy;
	}
	PassParameters->SSProfilesTexture = GraphBuilder.RegisterExternalTexture(SubsurfaceProfileRT);
	PassParameters->TransmissionProfilesLinearSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();

	// Output
	FIntPoint DispatchResolution = FIntPoint::DivideAndRoundUp(View.ViewRect.Size(), UpscaleFactor);
	FIntVector LocalGatherPointsResolution(DispatchResolution.X, DispatchResolution.Y, GatherSamples);
	if (GatherPointsResolution != LocalGatherPointsResolution)
	{
		GatherPointsResolution = LocalGatherPointsResolution;
		FRDGBufferDesc BufferDesc = FRDGBufferDesc::CreateStructuredDesc(sizeof(FGatherPoint), GatherPointsResolution.X * GatherPointsResolution.Y * GatherPointsResolution.Z);
		GatherPointsBuffer = GraphBuilder.CreateBuffer(BufferDesc, TEXT("GatherPointsBuffer"), ERDGResourceFlags::MultiFrame);
	}
	else
	{
		GatherPointsBuffer = GraphBuilder.RegisterExternalBuffer(((FSceneViewState*)View.State)->GatherPointsBuffer, TEXT("GatherPointsBuffer"));
	}
	PassParameters->GatherPointsResolution = FIntPoint(GatherPointsResolution.X, GatherPointsResolution.Y);
	PassParameters->RWGatherPointsBuffer = GraphBuilder.CreateUAV(GatherPointsBuffer, EPixelFormat::PF_R32_UINT);

	// When deferred materials are used, two passes are invoked:
	// 1) Gather ray-hit data and sort by hit-shader ID
	// 2) Re-trace "short" ray and shade
	const bool bSortMaterials = CVarRayTracingGlobalIlluminationFinalGatherSortMaterials.GetValueOnRenderThread() != 0;
	if (!bSortMaterials)
	{
	FRayTracingGlobalIlluminationCreateGatherPointsRGS::FPermutationDomain PermutationVector;
	PermutationVector.Set<FRayTracingGlobalIlluminationCreateGatherPointsRGS::FUseAttenuationTermDim>(true);
	PermutationVector.Set<FRayTracingGlobalIlluminationCreateGatherPointsRGS::FEnableTwoSidedGeometryDim>(CVarRayTracingGlobalIlluminationEnableTwoSidedGeometry.GetValueOnRenderThread() != 0);
	TShaderMapRef<FRayTracingGlobalIlluminationCreateGatherPointsRGS> RayGenerationShader(GetGlobalShaderMap(FeatureLevel), PermutationVector);
	ClearUnusedGraphResources(*RayGenerationShader, PassParameters);

	GraphBuilder.AddPass(
		RDG_EVENT_NAME("GatherPoints %d%d", GatherPointsResolution.X, GatherPointsResolution.Y),
		PassParameters,
		ERDGPassFlags::Compute,
		[PassParameters, this, &View, RayGenerationShader, GatherPointsResolution](FRHICommandList& RHICmdList)
	{
		FRHIRayTracingScene* RayTracingSceneRHI = View.RayTracingScene.RayTracingSceneRHI;
		FRayTracingShaderBindingsWriter GlobalResources;
		SetShaderParameters(GlobalResources, *RayGenerationShader, *PassParameters);
		RHICmdList.RayTraceDispatch(View.RayTracingMaterialPipeline, RayGenerationShader->GetRayTracingShader(), RayTracingSceneRHI, GlobalResources, GatherPointsResolution.X, GatherPointsResolution.Y);
	});
	}
	else
	{
		// Determines tile-size for sorted-deferred path
		const int32 SortTileSize = CVarRayTracingGlobalIlluminationFinalGatherSortTileSize.GetValueOnRenderThread();
		FIntPoint TileAlignedResolution = FIntPoint(GatherPointsResolution.X, GatherPointsResolution.Y);
		if (SortTileSize)
		{
			TileAlignedResolution = FIntPoint::DivideAndRoundUp(TileAlignedResolution, SortTileSize) * SortTileSize;
		}
		PassParameters->TileAlignedResolution = TileAlignedResolution;
		PassParameters->SortTileSize = SortTileSize;

		FRDGBufferRef DeferredMaterialBuffer = nullptr;
		const uint32 DeferredMaterialBufferNumElements = TileAlignedResolution.X * TileAlignedResolution.Y;

		// Gather pass
		{
			FRayTracingGlobalIlluminationCreateGatherPointsTraceRGS::FParameters* GatherPassParameters = GraphBuilder.AllocParameters<FRayTracingGlobalIlluminationCreateGatherPointsTraceRGS::FParameters>();
			CopyGatherPassParameters(*PassParameters, GatherPassParameters);

			FRDGBufferDesc Desc = FRDGBufferDesc::CreateStructuredDesc(sizeof(FDeferredMaterialPayload), DeferredMaterialBufferNumElements);
			DeferredMaterialBuffer = GraphBuilder.CreateBuffer(Desc, TEXT("RayTracingGlobalIlluminationMaterialBuffer"));
			GatherPassParameters->MaterialBuffer = GraphBuilder.CreateUAV(DeferredMaterialBuffer);

			FRayTracingGlobalIlluminationCreateGatherPointsTraceRGS::FPermutationDomain PermutationVector;
			PermutationVector.Set<FRayTracingGlobalIlluminationCreateGatherPointsTraceRGS::FUseAttenuationTermDim>(true);
			PermutationVector.Set<FRayTracingGlobalIlluminationCreateGatherPointsTraceRGS::FEnableTwoSidedGeometryDim>(CVarRayTracingGlobalIlluminationEnableTwoSidedGeometry.GetValueOnRenderThread() != 0);
			PermutationVector.Set<FRayTracingGlobalIlluminationCreateGatherPointsTraceRGS::FDeferredMaterialMode>(EDeferredMaterialMode::Gather);
			TShaderMapRef<FRayTracingGlobalIlluminationCreateGatherPointsTraceRGS> RayGenerationShader(GetGlobalShaderMap(FeatureLevel), PermutationVector);

			ClearUnusedGraphResources(*RayGenerationShader, GatherPassParameters);

			GraphBuilder.AddPass(
				RDG_EVENT_NAME("GlobalIlluminationRayTracingGatherMaterials %dx%d", TileAlignedResolution.X, TileAlignedResolution.Y),
				GatherPassParameters,
				ERDGPassFlags::Compute,
				[GatherPassParameters, this, &View, RayGenerationShader, TileAlignedResolution](FRHICommandList& RHICmdList)
			{
				FRayTracingPipelineState* Pipeline = BindRayTracingDeferredMaterialGatherPipeline(RHICmdList, View, RayGenerationShader->GetRayTracingShader());

				FRayTracingShaderBindingsWriter GlobalResources;
				SetShaderParameters(GlobalResources, *RayGenerationShader, *GatherPassParameters);

				FRHIRayTracingScene* RayTracingSceneRHI = View.RayTracingScene.RayTracingSceneRHI;
				RHICmdList.RayTraceDispatch(Pipeline, RayGenerationShader->GetRayTracingShader(), RayTracingSceneRHI, GlobalResources, TileAlignedResolution.X, TileAlignedResolution.Y);
			});
		}

		// Sort by hit-shader ID
		const uint32 SortSize = CVarRayTracingGlobalIlluminationFinalGatherSortSize.GetValueOnRenderThread();
		SortDeferredMaterials(GraphBuilder, View, SortSize, DeferredMaterialBufferNumElements, DeferredMaterialBuffer);

		// Shade pass
		{
			PassParameters->MaterialBuffer = GraphBuilder.CreateUAV(DeferredMaterialBuffer);

			FRayTracingGlobalIlluminationCreateGatherPointsRGS::FPermutationDomain PermutationVector;
			PermutationVector.Set<FRayTracingGlobalIlluminationCreateGatherPointsRGS::FUseAttenuationTermDim>(true);
			PermutationVector.Set<FRayTracingGlobalIlluminationCreateGatherPointsRGS::FEnableTwoSidedGeometryDim>(CVarRayTracingGlobalIlluminationEnableTwoSidedGeometry.GetValueOnRenderThread() != 0);
			PermutationVector.Set<FRayTracingGlobalIlluminationCreateGatherPointsRGS::FDeferredMaterialMode>(EDeferredMaterialMode::Shade);
			TShaderMapRef<FRayTracingGlobalIlluminationCreateGatherPointsRGS> RayGenerationShader(GetGlobalShaderMap(FeatureLevel), PermutationVector);
			ClearUnusedGraphResources(*RayGenerationShader, PassParameters);

			GraphBuilder.AddPass(
				RDG_EVENT_NAME("GlobalIlluminationRayTracingShadeMaterials %d", DeferredMaterialBufferNumElements),
				PassParameters,
				ERDGPassFlags::Compute,
				[PassParameters, this, &View, RayGenerationShader, DeferredMaterialBufferNumElements](FRHICommandList& RHICmdList)
			{
				FRHIRayTracingScene* RayTracingSceneRHI = View.RayTracingScene.RayTracingSceneRHI;
				FRayTracingShaderBindingsWriter GlobalResources;
				SetShaderParameters(GlobalResources, *RayGenerationShader, *PassParameters);

				// Shading pass for sorted materials uses 1D dispatch over all elements in the material buffer.
				// This can be reduced to the number of output pixels if sorting pass guarantees that all invalid entries are moved to the end.
				RHICmdList.RayTraceDispatch(View.RayTracingMaterialPipeline, RayGenerationShader->GetRayTracingShader(), RayTracingSceneRHI, GlobalResources, DeferredMaterialBufferNumElements, 1);
			});
		}
	}

}
#else
{
	unimplemented();
}
#endif

void FDeferredShadingSceneRenderer::RenderRayTracingGlobalIlluminationFinalGather(
	FRDGBuilder& GraphBuilder,
	FSceneTextureParameters& SceneTextures,
	FViewInfo& View,
	const IScreenSpaceDenoiser::FAmbientOcclusionRayTracingConfig& RayTracingConfig,
	int32 UpscaleFactor,
	// Output
	IScreenSpaceDenoiser::FDiffuseIndirectInputs* OutDenoiserInputs)
#if RHI_RAYTRACING
{
	// Generate gather points
	FRDGBufferRef GatherPointsBuffer;
	FSceneViewState* SceneViewState = (FSceneViewState*)View.State;
	RayTracingGlobalIlluminationCreateGatherPoints(GraphBuilder, SceneTextures, View, UpscaleFactor, GatherPointsBuffer, SceneViewState->GatherPointsResolution);

	// Perform gather
	RDG_GPU_STAT_SCOPE(GraphBuilder, RayTracingGIFinalGather);
	RDG_EVENT_SCOPE(GraphBuilder, "Ray Tracing GI: Final Gather");

	FRayTracingGlobalIlluminationFinalGatherRGS::FParameters* PassParameters = GraphBuilder.AllocParameters<FRayTracingGlobalIlluminationFinalGatherRGS::FParameters>();
	int32 SamplesPerPixel = FMath::Min(GetRayTracingGlobalIlluminationSamplesPerPixel(View), MAXIMUM_GATHER_POINTS_PER_PIXEL);
	int32 SampleIndex = View.ViewState->FrameIndex % SamplesPerPixel;
	PassParameters->SampleIndex = SampleIndex;
	PassParameters->SamplesPerPixel = SamplesPerPixel;
	PassParameters->DiffuseThreshold = GRayTracingGlobalIlluminationDiffuseThreshold;
	PassParameters->MaxNormalBias = GetRaytracingMaxNormalBias();
	PassParameters->FinalGatherDistance = GRayTracingGlobalIlluminationFinalGatherDistance;
	PassParameters->UpscaleFactor = UpscaleFactor;
	PassParameters->RenderTileOffsetX = 0;
	PassParameters->RenderTileOffsetY = 0;

	// Scene data
	PassParameters->TLAS = View.RayTracingScene.RayTracingSceneRHI->GetShaderResourceView();
	PassParameters->ViewUniformBuffer = View.ViewUniformBuffer;

	// Shading data
	PassParameters->SceneTextures = SceneTextures;
	TRefCountPtr<IPooledRenderTarget> SubsurfaceProfileRT((IPooledRenderTarget*)GetSubsufaceProfileTexture_RT(GraphBuilder.RHICmdList));
	if (!SubsurfaceProfileRT)
	{
		SubsurfaceProfileRT = GSystemTextures.BlackDummy;
	}
	PassParameters->SSProfilesTexture = GraphBuilder.RegisterExternalTexture(SubsurfaceProfileRT);
	PassParameters->TransmissionProfilesLinearSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();

	// Gather points
	PassParameters->GatherPointsResolution = FIntPoint(SceneViewState->GatherPointsResolution.X, SceneViewState->GatherPointsResolution.Y);
	PassParameters->GatherPointsBuffer = GraphBuilder.CreateSRV(GatherPointsBuffer);

	// Output
	PassParameters->RWGlobalIlluminationUAV = GraphBuilder.CreateUAV(OutDenoiserInputs->Color);
	PassParameters->RWRayDistanceUAV = GraphBuilder.CreateUAV(OutDenoiserInputs->RayHitDistance);

	FRayTracingGlobalIlluminationFinalGatherRGS::FPermutationDomain PermutationVector;
	PermutationVector.Set<FRayTracingGlobalIlluminationFinalGatherRGS::FUseAttenuationTermDim>(true);
	PermutationVector.Set<FRayTracingGlobalIlluminationFinalGatherRGS::FEnableTwoSidedGeometryDim>(CVarRayTracingGlobalIlluminationEnableTwoSidedGeometry.GetValueOnRenderThread() != 0);
	TShaderMapRef<FRayTracingGlobalIlluminationFinalGatherRGS> RayGenerationShader(GetGlobalShaderMap(FeatureLevel), PermutationVector);
	ClearUnusedGraphResources(*RayGenerationShader, PassParameters);

	FIntPoint RayTracingResolution = FIntPoint::DivideAndRoundUp(View.ViewRect.Size(), UpscaleFactor);
	GraphBuilder.AddPass(
		RDG_EVENT_NAME("GlobalIlluminationRayTracing %dx%d", RayTracingResolution.X, RayTracingResolution.Y),
		PassParameters,
		ERDGPassFlags::Compute,
		[PassParameters, this, &View, RayGenerationShader, RayTracingResolution](FRHICommandList& RHICmdList)
	{
		FRHIRayTracingScene* RayTracingSceneRHI = View.RayTracingScene.RayTracingSceneRHI;

		FRayTracingShaderBindingsWriter GlobalResources;
		SetShaderParameters(GlobalResources, *RayGenerationShader, *PassParameters);
		RHICmdList.RayTraceDispatch(View.RayTracingMaterialPipeline, RayGenerationShader->GetRayTracingShader(), RayTracingSceneRHI, GlobalResources, RayTracingResolution.X, RayTracingResolution.Y);
	});


	GraphBuilder.QueueBufferExtraction(GatherPointsBuffer, &SceneViewState->GatherPointsBuffer, FRDGResourceState::EAccess::Read, FRDGResourceState::EPipeline::Compute);
}
#else
{
	unimplemented();
}
#endif

void FDeferredShadingSceneRenderer::RenderRayTracingGlobalIlluminationBruteForce(
	FRDGBuilder& GraphBuilder,
	FSceneTextureParameters& SceneTextures,
	FViewInfo& View,
	const IScreenSpaceDenoiser::FAmbientOcclusionRayTracingConfig& RayTracingConfig,
	int32 UpscaleFactor,
	IScreenSpaceDenoiser::FDiffuseIndirectInputs* OutDenoiserInputs)
#if RHI_RAYTRACING
{
	RDG_GPU_STAT_SCOPE(GraphBuilder, RayTracingGIBruteForce);
	RDG_EVENT_SCOPE(GraphBuilder, "Ray Tracing GI: Brute Force");

	int32 RayTracingGISamplesPerPixel = GetRayTracingGlobalIlluminationSamplesPerPixel(View);
	uint32 IterationCount = RayTracingGISamplesPerPixel;
	uint32 SequenceCount = 1;
	uint32 DimensionCount = 24;
	FHaltonSequenceIteration HaltonSequenceIteration(Scene->HaltonSequence, IterationCount, SequenceCount, DimensionCount, View.ViewState->FrameIndex % 1024);

	FHaltonIteration HaltonIteration;
	InitializeHaltonSequenceIteration(HaltonSequenceIteration, HaltonIteration);

	FHaltonPrimes HaltonPrimes;
	InitializeHaltonPrimes(Scene->HaltonPrimesResource, HaltonPrimes);

	FBlueNoise BlueNoise;
	InitializeBlueNoise(BlueNoise);

	FPathTracingLightData LightParameters;
	SetupLightParameters(*Scene, View, &LightParameters);

	if (Scene->SkyLight && Scene->SkyLight->ShouldRebuildCdf())
	{
		// TODO: should be converted to RDG.
		BuildSkyLightCdfs(GraphBuilder.RHICmdList, Scene->SkyLight);
	}
	FSkyLightData SkyLightParameters;
	SetupGlobalIlluminationSkyLightParameters(*Scene, &SkyLightParameters);

	FGlobalIlluminationRGS::FParameters* PassParameters = GraphBuilder.AllocParameters<FGlobalIlluminationRGS::FParameters>();
	PassParameters->SamplesPerPixel = RayTracingGISamplesPerPixel;
	int32 CVarRayTracingGlobalIlluminationMaxBouncesValue = CVarRayTracingGlobalIlluminationMaxBounces.GetValueOnRenderThread();
	PassParameters->MaxBounces = CVarRayTracingGlobalIlluminationMaxBouncesValue > -1? CVarRayTracingGlobalIlluminationMaxBouncesValue : View.FinalPostProcessSettings.RayTracingGIMaxBounces;
	PassParameters->MaxNormalBias = GetRaytracingMaxNormalBias();
	float MaxRayDistanceForGI = GRayTracingGlobalIlluminationMaxRayDistance;
	if (MaxRayDistanceForGI == -1.0)
	{
		MaxRayDistanceForGI = View.FinalPostProcessSettings.AmbientOcclusionRadius;
	}
	PassParameters->MaxRayDistanceForGI = MaxRayDistanceForGI;
	PassParameters->MaxRayDistanceForAO = View.FinalPostProcessSettings.AmbientOcclusionRadius;
	PassParameters->UpscaleFactor = UpscaleFactor;
	PassParameters->EvalSkyLight = GRayTracingGlobalIlluminationEvalSkyLight != 0;
	PassParameters->UseRussianRoulette = GRayTracingGlobalIlluminationUseRussianRoulette != 0;
	PassParameters->DiffuseThreshold = GRayTracingGlobalIlluminationDiffuseThreshold;
	PassParameters->NextEventEstimationSamples = GRayTracingGlobalIlluminationNextEventEstimationSamples;
	PassParameters->TLAS = View.RayTracingScene.RayTracingSceneRHI->GetShaderResourceView();
	PassParameters->ViewUniformBuffer = View.ViewUniformBuffer;
	PassParameters->HaltonIteration = CreateUniformBufferImmediate(HaltonIteration, EUniformBufferUsage::UniformBuffer_SingleDraw);
	PassParameters->HaltonPrimes = CreateUniformBufferImmediate(HaltonPrimes, EUniformBufferUsage::UniformBuffer_SingleDraw);
	PassParameters->BlueNoise = CreateUniformBufferImmediate(BlueNoise, EUniformBufferUsage::UniformBuffer_SingleDraw);
	PassParameters->LightParameters = CreateUniformBufferImmediate(LightParameters, EUniformBufferUsage::UniformBuffer_SingleDraw);
	PassParameters->SceneTextures = SceneTextures;
	PassParameters->SkyLight = CreateUniformBufferImmediate(SkyLightParameters, EUniformBufferUsage::UniformBuffer_SingleDraw);

	// TODO: should be converted to RDG
	TRefCountPtr<IPooledRenderTarget> SubsurfaceProfileRT((IPooledRenderTarget*) GetSubsufaceProfileTexture_RT(GraphBuilder.RHICmdList));
	if (!SubsurfaceProfileRT)
	{
		SubsurfaceProfileRT = GSystemTextures.BlackDummy;
	}
	PassParameters->SSProfilesTexture = GraphBuilder.RegisterExternalTexture(SubsurfaceProfileRT);
	PassParameters->TransmissionProfilesLinearSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
	PassParameters->RWGlobalIlluminationUAV = GraphBuilder.CreateUAV(OutDenoiserInputs->Color);
	PassParameters->RWRayDistanceUAV = GraphBuilder.CreateUAV(OutDenoiserInputs->RayHitDistance);
	PassParameters->RenderTileOffsetX = 0;
	PassParameters->RenderTileOffsetY = 0;

	FGlobalIlluminationRGS::FPermutationDomain PermutationVector;
	PermutationVector.Set<FGlobalIlluminationRGS::FUseAttenuationTermDim>(true);
	PermutationVector.Set<FGlobalIlluminationRGS::FEnableTwoSidedGeometryDim>(CVarRayTracingGlobalIlluminationEnableTwoSidedGeometry.GetValueOnRenderThread() != 0);
	TShaderMapRef<FGlobalIlluminationRGS> RayGenerationShader(GetGlobalShaderMap(FeatureLevel), PermutationVector);
	ClearUnusedGraphResources(*RayGenerationShader, PassParameters);

	FIntPoint RayTracingResolution = FIntPoint::DivideAndRoundUp(View.ViewRect.Size(), UpscaleFactor);

	if (GRayTracingGlobalIlluminationRenderTileSize <= 0)
	{
		GraphBuilder.AddPass(
			RDG_EVENT_NAME("GlobalIlluminationRayTracing %dx%d", RayTracingResolution.X, RayTracingResolution.Y),
			PassParameters,
			ERDGPassFlags::Compute,
			[PassParameters, this, &View, RayGenerationShader, RayTracingResolution](FRHICommandList& RHICmdList)
		{
			FRHIRayTracingScene* RayTracingSceneRHI = View.RayTracingScene.RayTracingSceneRHI;

			FRayTracingShaderBindingsWriter GlobalResources;
			SetShaderParameters(GlobalResources, *RayGenerationShader, *PassParameters);
			RHICmdList.RayTraceDispatch(View.RayTracingMaterialPipeline, RayGenerationShader->GetRayTracingShader(), RayTracingSceneRHI, GlobalResources, RayTracingResolution.X, RayTracingResolution.Y);
		});
	}
	else
	{
		int32 RenderTileSize = FMath::Max(32, GRayTracingGlobalIlluminationRenderTileSize);
		int32 NumTilesX = FMath::DivideAndRoundUp(RayTracingResolution.X, RenderTileSize);
		int32 NumTilesY = FMath::DivideAndRoundUp(RayTracingResolution.Y, RenderTileSize);
		for (int32 Y = 0; Y < NumTilesY; ++Y)
		{
			for (int32 X = 0; X < NumTilesX; ++X)
			{
				FGlobalIlluminationRGS::FParameters* TilePassParameters = PassParameters;

				if (X > 0 || Y > 0)
				{
					TilePassParameters = GraphBuilder.AllocParameters<FGlobalIlluminationRGS::FParameters>();
					*TilePassParameters = *PassParameters;

					TilePassParameters->RenderTileOffsetX = X * RenderTileSize;
					TilePassParameters->RenderTileOffsetY = Y * RenderTileSize;
				}

				int32 DispatchSizeX = FMath::Min<int32>(RenderTileSize, RayTracingResolution.X - TilePassParameters->RenderTileOffsetX);
				int32 DispatchSizeY = FMath::Min<int32>(RenderTileSize, RayTracingResolution.Y - TilePassParameters->RenderTileOffsetY);

				GraphBuilder.AddPass(
					RDG_EVENT_NAME("GlobalIlluminationRayTracing %dx%d (tile %dx%d)", DispatchSizeX, DispatchSizeY, X, Y),
					TilePassParameters,
					ERDGPassFlags::Compute,
					[TilePassParameters, this, &View, RayGenerationShader, DispatchSizeX, DispatchSizeY](FRHICommandList& RHICmdList)
				{
					FRHIRayTracingScene* RayTracingSceneRHI = View.RayTracingScene.RayTracingSceneRHI;

					FRayTracingShaderBindingsWriter GlobalResources;
					SetShaderParameters(GlobalResources, *RayGenerationShader, *TilePassParameters);
					RHICmdList.RayTraceDispatch(View.RayTracingMaterialPipeline, RayGenerationShader->GetRayTracingShader(), RayTracingSceneRHI, 
						GlobalResources, DispatchSizeX, DispatchSizeY);
					RHICmdList.SubmitCommandsHint();
				});
			}
		}
	}
}
#else
{
	unimplemented();
}
#endif // RHI_RAYTRACING
