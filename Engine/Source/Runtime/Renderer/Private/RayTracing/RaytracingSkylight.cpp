// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	RaytracingSkyLight.cpp implements sky lighting with ray tracing
=============================================================================*/

#include "DeferredShadingRenderer.h"
#include "SceneTextureParameters.h"

static int32 GRayTracingSkyLight = -1;

#if RHI_RAYTRACING

#include "RayTracingSkyLight.h"
#include "RayTracingMaterialHitShaders.h"
#include "ClearQuad.h"
#include "DistanceFieldAmbientOcclusion.h"
#include "SceneRendering.h"
#include "ScenePrivate.h"
#include "SceneRenderTargets.h"
#include "RenderGraphBuilder.h"
#include "RenderTargetPool.h"
#include "VisualizeTexture.h"
#include "RayGenShaderUtils.h"
#include "SceneTextureParameters.h"
#include "ScreenSpaceDenoise.h"

#include "Raytracing/RaytracingOptions.h"
#include "PostProcess/PostProcessing.h"
#include "PostProcess/SceneFilterRendering.h"

static FAutoConsoleVariableRef CVarRayTracingSkyLight(
	TEXT("r.RayTracing.SkyLight"),
	GRayTracingSkyLight,
	TEXT("Enables ray tracing SkyLight (default = 0)")
);

static int32 GRayTracingSkyLightSamplesPerPixel = -1;
static FAutoConsoleVariableRef CVarRayTracingSkyLightSamplesPerPixel(
	TEXT("r.RayTracing.SkyLight.SamplesPerPixel"),
	GRayTracingSkyLightSamplesPerPixel,
	TEXT("Sets the samples-per-pixel for ray tracing SkyLight (default = -1)")
);

static float GRayTracingSkyLightMaxRayDistance = 1.0e7;
static FAutoConsoleVariableRef CVarRayTracingSkyLightMaxRayDistance(
	TEXT("r.RayTracing.SkyLight.MaxRayDistance"),
	GRayTracingSkyLightMaxRayDistance,
	TEXT("Sets the max ray distance for ray tracing SkyLight (default = 1.0e7)")
);

static float GRayTracingSkyLightMaxShadowThickness = 1.0e3;
static FAutoConsoleVariableRef CVarRayTracingSkyLightMaxShadowThickness(
	TEXT("r.RayTracing.SkyLight.MaxShadowThickness"),
	GRayTracingSkyLightMaxShadowThickness,
	TEXT("Sets the max shadow thickness for translucent materials for ray tracing SkyLight (default = 1.0e3)")
);

static int32 GRayTracingSkyLightSamplingStopLevel = 0;
static FAutoConsoleVariableRef CVarRayTracingSkyLightSamplingStopLevel(
	TEXT("r.RayTracing.SkyLight.Sampling.StopLevel"),
	GRayTracingSkyLightSamplingStopLevel,
	TEXT("Sets the stop level for MIP-sampling (default = 0)")
);

static int32 GRayTracingSkyLightDenoiser = 1;
static FAutoConsoleVariableRef CVarRayTracingSkyLightDenoiser(
	TEXT("r.RayTracing.SkyLight.Denoiser"),
	GRayTracingSkyLightDenoiser,
	TEXT("Denoising options (default = 1)")
);

static TAutoConsoleVariable<int32> CVarRayTracingSkyLightEnableTwoSidedGeometry(
	TEXT("r.RayTracing.SkyLight.EnableTwoSidedGeometry"),
	1,
	TEXT("Enables two-sided geometry when tracing shadow rays (default = 1)"),
	ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<int32> CVarRayTracingSkyLightEnableMaterials(
	TEXT("r.RayTracing.SkyLight.EnableMaterials"),
	0,
	TEXT("Enables material shader binding for shadow rays. If this is disabled, then a default trivial shader is used. (default = 0)"),
	ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<int32> CVarRayTracingSkyLightDecoupleSampleGeneration(
	TEXT("r.RayTracing.SkyLight.DecoupleSampleGeneration"),
	1,
	TEXT("Decouples sample generation from ray traversal (default = 1)"),
	ECVF_RenderThreadSafe
);

int32 GetRayTracingSkyLightDecoupleSampleGenerationCVarValue()
{
	return CVarRayTracingSkyLightDecoupleSampleGeneration.GetValueOnRenderThread();
}

int32 GetSkyLightSamplesPerPixel(const FSkyLightSceneProxy* SkyLightSceneProxy)
{
	check(SkyLightSceneProxy != nullptr);

	// Clamp to 2spp minimum due to poor 1spp denoised quality
	return GRayTracingSkyLightSamplesPerPixel >= 0 ? GRayTracingSkyLightSamplesPerPixel : FMath::Max(SkyLightSceneProxy->SamplesPerPixel, 2);
}

bool ShouldRenderRayTracingSkyLight(const FSkyLightSceneProxy* SkyLightSceneProxy)
{
	if (SkyLightSceneProxy == nullptr)
	{
		return false;
	}

	const int32 ForceAllRayTracingEffects = GetForceRayTracingEffectsCVarValue();
	bool bRayTracingSkyEnabled;
	if (ForceAllRayTracingEffects >= 0)
	{
		bRayTracingSkyEnabled = ForceAllRayTracingEffects > 0;
	}
	else
	{
		bRayTracingSkyEnabled = GRayTracingSkyLight >= 0 ? (GRayTracingSkyLight != 0) : SkyLightSceneProxy->bCastRayTracedShadow;
	}

	bRayTracingSkyEnabled = bRayTracingSkyEnabled && (GetSkyLightSamplesPerPixel(SkyLightSceneProxy) > 0);

	return
		IsRayTracingEnabled() &&
		bRayTracingSkyEnabled &&
		(SkyLightSceneProxy->ImportanceSamplingData != nullptr) &&
		SkyLightSceneProxy->ImportanceSamplingData->bIsValid;
}

IMPLEMENT_GLOBAL_SHADER_PARAMETER_STRUCT(FSkyLightData, "SkyLight");

struct FSkyLightVisibilityRays
{
	FVector4 DirectionAndPdf;
};

void SetupSkyLightParameters(
	const FScene& Scene,
	FSkyLightData* SkyLightData)
{
	// Check if parameters should be set based on if the sky light's texture has been processed and if its mip tree has been built yet
	if (
		Scene.SkyLight &&
		Scene.SkyLight->ProcessedTexture &&
		Scene.SkyLight->ImportanceSamplingData)
	{
		check(Scene.SkyLight->ImportanceSamplingData->bIsValid);

		SkyLightData->SamplesPerPixel = GetSkyLightSamplesPerPixel(Scene.SkyLight);
		SkyLightData->SamplingStopLevel = GRayTracingSkyLightSamplingStopLevel;
		SkyLightData->MaxRayDistance = GRayTracingSkyLightMaxRayDistance;
		SkyLightData->MaxNormalBias = GetRaytracingMaxNormalBias();
		SkyLightData->MaxShadowThickness = GRayTracingSkyLightMaxShadowThickness;

		ensure(SkyLightData->SamplesPerPixel > 0);

		SkyLightData->Color = FVector(Scene.SkyLight->GetEffectiveLightColor());
		SkyLightData->Texture = Scene.SkyLight->ProcessedTexture->TextureRHI;
		SkyLightData->TextureDimensions = FIntVector(Scene.SkyLight->ProcessedTexture->GetSizeX(), Scene.SkyLight->ProcessedTexture->GetSizeY(), 0);
		SkyLightData->TextureSampler = Scene.SkyLight->ProcessedTexture->SamplerStateRHI;
		SkyLightData->MipDimensions = Scene.SkyLight->ImportanceSamplingData->MipDimensions;

		SkyLightData->MipTreePosX = Scene.SkyLight->ImportanceSamplingData->MipTreePosX.SRV;
		SkyLightData->MipTreeNegX = Scene.SkyLight->ImportanceSamplingData->MipTreeNegX.SRV;
		SkyLightData->MipTreePosY = Scene.SkyLight->ImportanceSamplingData->MipTreePosY.SRV;
		SkyLightData->MipTreeNegY = Scene.SkyLight->ImportanceSamplingData->MipTreeNegY.SRV;
		SkyLightData->MipTreePosZ = Scene.SkyLight->ImportanceSamplingData->MipTreePosZ.SRV;
		SkyLightData->MipTreeNegZ = Scene.SkyLight->ImportanceSamplingData->MipTreeNegZ.SRV;

		SkyLightData->MipTreePdfPosX = Scene.SkyLight->ImportanceSamplingData->MipTreePdfPosX.SRV;
		SkyLightData->MipTreePdfNegX = Scene.SkyLight->ImportanceSamplingData->MipTreePdfNegX.SRV;
		SkyLightData->MipTreePdfPosY = Scene.SkyLight->ImportanceSamplingData->MipTreePdfPosY.SRV;
		SkyLightData->MipTreePdfNegY = Scene.SkyLight->ImportanceSamplingData->MipTreePdfNegY.SRV;
		SkyLightData->MipTreePdfPosZ = Scene.SkyLight->ImportanceSamplingData->MipTreePdfPosZ.SRV;
		SkyLightData->MipTreePdfNegZ = Scene.SkyLight->ImportanceSamplingData->MipTreePdfNegZ.SRV;
		SkyLightData->SolidAnglePdf = Scene.SkyLight->ImportanceSamplingData->SolidAnglePdf.SRV;
	}
	else
	{
		SkyLightData->SamplesPerPixel = -1;
		SkyLightData->SamplingStopLevel = 0;
		SkyLightData->MaxRayDistance = 0.0f;
		SkyLightData->MaxNormalBias = 0.0f;
		SkyLightData->MaxShadowThickness = 0.0f;

		SkyLightData->Color = FVector(0.0);
		SkyLightData->Texture = GBlackTextureCube->TextureRHI;
		SkyLightData->TextureSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
		SkyLightData->MipDimensions = FIntVector(0);

		SkyLightData->MipTreePosX = GBlackTextureWithSRV->ShaderResourceViewRHI;
		SkyLightData->MipTreeNegX = GBlackTextureWithSRV->ShaderResourceViewRHI;
		SkyLightData->MipTreePosY = GBlackTextureWithSRV->ShaderResourceViewRHI;
		SkyLightData->MipTreeNegY = GBlackTextureWithSRV->ShaderResourceViewRHI;
		SkyLightData->MipTreePosZ = GBlackTextureWithSRV->ShaderResourceViewRHI;
		SkyLightData->MipTreeNegZ = GBlackTextureWithSRV->ShaderResourceViewRHI;

		SkyLightData->MipTreePdfPosX = GBlackTextureWithSRV->ShaderResourceViewRHI;
		SkyLightData->MipTreePdfNegX = GBlackTextureWithSRV->ShaderResourceViewRHI;
		SkyLightData->MipTreePdfPosY = GBlackTextureWithSRV->ShaderResourceViewRHI;
		SkyLightData->MipTreePdfNegY = GBlackTextureWithSRV->ShaderResourceViewRHI;
		SkyLightData->MipTreePdfPosZ = GBlackTextureWithSRV->ShaderResourceViewRHI;
		SkyLightData->MipTreePdfNegZ = GBlackTextureWithSRV->ShaderResourceViewRHI;
		SkyLightData->SolidAnglePdf = GBlackTextureWithSRV->ShaderResourceViewRHI;
	}
}

void SetupSkyLightQuasiRandomParameters(
	const FScene& Scene,
	const FViewInfo& View,
	FIntVector& OutBlueNoiseDimensions,
	FSkyLightQuasiRandomData* OutSkyLightQuasiRandomData)
{
	uint32 IterationCount;

	// Choose to set the iteration count to the sky light samples per pixel or a dummy value depending on its presence
	if (Scene.SkyLight != nullptr)
	{
		IterationCount = FMath::Max(GetSkyLightSamplesPerPixel(Scene.SkyLight), 1);
	}
	else
	{
		IterationCount = 1;
	}

	// Halton iteration setup
	uint32 SequenceCount = 1;
	uint32 DimensionCount = 3;
	FHaltonSequenceIteration HaltonSequenceIteration(Scene.HaltonSequence, IterationCount, SequenceCount, DimensionCount, View.ViewState ? (View.ViewState->FrameIndex % 1024) : 0);

	FHaltonIteration HaltonIteration;
	InitializeHaltonSequenceIteration(HaltonSequenceIteration, HaltonIteration);
	
	// Halton primes setup
	FHaltonPrimes HaltonPrimes;
	InitializeHaltonPrimes(Scene.HaltonPrimesResource, HaltonPrimes);

	// Blue noise setup
	FBlueNoise BlueNoise;
	InitializeBlueNoise(BlueNoise);

	// Set the Blue Noise dimensions
	OutBlueNoiseDimensions = FIntVector(BlueNoise.Dimensions.X, BlueNoise.Dimensions.Y, 0);

	// Set Sky Light Quasi Random Data information
	OutSkyLightQuasiRandomData->HaltonIteration = CreateUniformBufferImmediate(HaltonIteration, EUniformBufferUsage::UniformBuffer_SingleDraw);
	OutSkyLightQuasiRandomData->HaltonPrimes = CreateUniformBufferImmediate(HaltonPrimes, EUniformBufferUsage::UniformBuffer_SingleDraw);
	OutSkyLightQuasiRandomData->BlueNoise = CreateUniformBufferImmediate(BlueNoise, EUniformBufferUsage::UniformBuffer_SingleDraw);
}

void SetupSkyLightVisibilityRaysParameters(
	FRDGBuilder& GraphBuilder,
	const FViewInfo& View,
	FSkyLightVisibilityRaysData* OutSkyLightVisibilityRaysData)
{
	// Get the Scene View State
	FSceneViewState* SceneViewState = (FSceneViewState*)View.State;

	TRefCountPtr<FPooledRDGBuffer> PooledSkyLightVisibilityRaysBuffer;
	FIntVector SkyLightVisibilityRaysDimensions;

	// Check if the Sky Light Visibility Ray Data should be set based on if decoupled sample generation is being used
	if (
		(SceneViewState != nullptr) &&
		(SceneViewState->SkyLightVisibilityRaysBuffer != nullptr) &&
		(CVarRayTracingSkyLightDecoupleSampleGeneration.GetValueOnRenderThread() == 1))
	{
		// Set the Sky Light Visibility Ray pooled buffer to the stored pooled buffer
		PooledSkyLightVisibilityRaysBuffer = SceneViewState->SkyLightVisibilityRaysBuffer;

		// Set the Sky Light Visibility Ray Dimensions from the stored dimensions
		SkyLightVisibilityRaysDimensions = SceneViewState->SkyLightVisibilityRaysDimensions;
	}
	else
	{
		// Create a dummy Sky Light Visibility Ray buffer in a dummy RDG
		FRDGBuilder DummyGraphBuilder{ GraphBuilder.RHICmdList };
		FRDGBufferDesc DummyBufferDesc = FRDGBufferDesc::CreateStructuredDesc(sizeof(FSkyLightVisibilityRays), 1);
		FRDGBufferRef DummyRDGBuffer = DummyGraphBuilder.CreateBuffer(DummyBufferDesc, TEXT("DummySkyLightVisibilityRays"));
		FRDGBufferUAVRef DummyRDGBufferUAV = DummyGraphBuilder.CreateUAV(DummyRDGBuffer, EPixelFormat::PF_R32_UINT);

		// Clear the dummy Sky Light Visibility Ray buffer
		AddClearUAVPass(DummyGraphBuilder, DummyRDGBufferUAV, 0);

		// Set the Sky Light Visibility Ray pooled buffer to the extracted pooled dummy buffer
		DummyGraphBuilder.QueueBufferExtraction(DummyRDGBuffer, &PooledSkyLightVisibilityRaysBuffer, FRDGResourceState::EAccess::Read, FRDGResourceState::EPipeline::Compute);
		DummyGraphBuilder.Execute();

		// Set the Sky Light Visibility Ray Dimensions to a dummy value
		SkyLightVisibilityRaysDimensions = FIntVector(1);
	}

	// Set the Sky Light Visibility Ray Buffer to the pooled RDG buffer
	FRDGBufferRef SkyLightVisibilityRaysBuffer = GraphBuilder.RegisterExternalBuffer(PooledSkyLightVisibilityRaysBuffer);

	// Set Sky Light Visibility Ray Data information
	OutSkyLightVisibilityRaysData->SkyLightVisibilityRays = GraphBuilder.CreateSRV(SkyLightVisibilityRaysBuffer, EPixelFormat::PF_R32_UINT);
	OutSkyLightVisibilityRaysData->SkyLightVisibilityRaysDimensions = SkyLightVisibilityRaysDimensions;
}

class FRayTracingSkyLightRGS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FRayTracingSkyLightRGS)
	SHADER_USE_ROOT_PARAMETER_STRUCT(FRayTracingSkyLightRGS, FGlobalShader)

	class FEnableTwoSidedGeometryDim : SHADER_PERMUTATION_BOOL("ENABLE_TWO_SIDED_GEOMETRY");
	class FEnableMaterialsDim : SHADER_PERMUTATION_BOOL("ENABLE_MATERIALS");
	class FDecoupleSampleGeneration : SHADER_PERMUTATION_BOOL("DECOUPLE_SAMPLE_GENERATION");

	using FPermutationDomain = TShaderPermutationDomain<FEnableTwoSidedGeometryDim, FEnableMaterialsDim, FDecoupleSampleGeneration>;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return ShouldCompileRayTracingShadersForProject(Parameters.Platform);
	}

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_SRV(RaytracingAccelerationStructure, TLAS)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, RWOcclusionMaskUAV)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float2>, RWRayDistanceUAV)

		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, ViewUniformBuffer)
		SHADER_PARAMETER_STRUCT_REF(FSkyLightData, SkyLightData)
		
		SHADER_PARAMETER_STRUCT_INCLUDE(FSkyLightQuasiRandomData, SkyLightQuasiRandomData)
		SHADER_PARAMETER_STRUCT_INCLUDE(FSkyLightVisibilityRaysData, SkyLightVisibilityRaysData)
		SHADER_PARAMETER_STRUCT_INCLUDE(FSceneTextureParameters, SceneTextures)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SSProfilesTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, TransmissionProfilesLinearSampler)
	END_SHADER_PARAMETER_STRUCT()
};

IMPLEMENT_GLOBAL_SHADER(FRayTracingSkyLightRGS, "/Engine/Private/Raytracing/RaytracingSkylightRGS.usf", "SkyLightRGS", SF_RayGen);

void FDeferredShadingSceneRenderer::PrepareRayTracingSkyLight(const FViewInfo& View, TArray<FRHIRayTracingShader*>& OutRayGenShaders)
{
	// Declare all RayGen shaders that require material closest hit shaders to be bound
	FRayTracingSkyLightRGS::FPermutationDomain PermutationVector;
	for (uint32 TwoSidedGeometryIndex = 0; TwoSidedGeometryIndex < 2; ++TwoSidedGeometryIndex)
	{
		for (uint32 EnableMaterialsIndex = 0; EnableMaterialsIndex < 2; ++EnableMaterialsIndex)
		{
			for (uint32 DecoupleSampleGeneration = 0; DecoupleSampleGeneration < 2; ++DecoupleSampleGeneration)
			{
				PermutationVector.Set<FRayTracingSkyLightRGS::FEnableTwoSidedGeometryDim>(TwoSidedGeometryIndex != 0);
				PermutationVector.Set<FRayTracingSkyLightRGS::FEnableMaterialsDim>(EnableMaterialsIndex != 0);
				PermutationVector.Set<FRayTracingSkyLightRGS::FDecoupleSampleGeneration>(DecoupleSampleGeneration != 0);
				TShaderMapRef<FRayTracingSkyLightRGS> RayGenerationShader(View.ShaderMap, PermutationVector);
				OutRayGenShaders.Add(RayGenerationShader->GetRayTracingShader());
			}
		}
	}
}

class FGenerateSkyLightVisibilityRaysCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FGenerateSkyLightVisibilityRaysCS);
	SHADER_USE_PARAMETER_STRUCT(FGenerateSkyLightVisibilityRaysCS, FGlobalShader);

	static const uint32 kGroupSize = 16;
	using FPermutationDomain = FShaderPermutationNone;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return ShouldCompileRayTracingShadersForProject(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("TILE_SIZE"), kGroupSize);
	}

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(int32, SamplesPerPixel)

		SHADER_PARAMETER_STRUCT_REF(FSkyLightData, SkyLightData)

		SHADER_PARAMETER_STRUCT_INCLUDE(FSkyLightQuasiRandomData, SkyLightQuasiRandomData)
		// Writable variant to allow for Sky Light Visibility Ray output
		SHADER_PARAMETER_STRUCT_INCLUDE(FWritableSkyLightVisibilityRaysData, WritableSkyLightVisibilityRaysData)
	END_SHADER_PARAMETER_STRUCT()
};

IMPLEMENT_GLOBAL_SHADER(FGenerateSkyLightVisibilityRaysCS, "/Engine/Private/RayTracing/GenerateSkyLightVisibilityRaysCS.usf", "MainCS", SF_Compute);

void FDeferredShadingSceneRenderer::GenerateSkyLightVisibilityRays(
	FRDGBuilder& GraphBuilder,
	FRDGBufferRef& SkyLightVisibilityRaysBuffer,
	FIntVector& Dimensions
)
{
	FRHIResourceCreateInfo CreateInfo;
	CreateInfo.DebugName = TEXT("SkyLightVisibilityRays");

	// SkyLight data setup
	FSkyLightData SkyLightData;
	SetupSkyLightParameters(*Scene, &SkyLightData);

	// Sky Light Quasi Random data setup
	// Note: Sets Dimensions based on blue noise dimensions
	FSkyLightQuasiRandomData SkyLightQuasiRandomData;
	SetupSkyLightQuasiRandomParameters(*Scene, Views[0], Dimensions, &SkyLightQuasiRandomData);

	// Output structured buffer creation
	FRDGBufferDesc BufferDesc = FRDGBufferDesc::CreateStructuredDesc(sizeof(FSkyLightVisibilityRays), Dimensions.X * Dimensions.Y * SkyLightData.SamplesPerPixel);
	SkyLightVisibilityRaysBuffer = GraphBuilder.CreateBuffer(BufferDesc, TEXT("SkyLightVisibilityRays"));

	// Compute Pass parameter definition
	FGenerateSkyLightVisibilityRaysCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FGenerateSkyLightVisibilityRaysCS::FParameters>();
	PassParameters->SkyLightData = CreateUniformBufferImmediate(SkyLightData, EUniformBufferUsage::UniformBuffer_SingleDraw);
	PassParameters->SkyLightQuasiRandomData = SkyLightQuasiRandomData;
	PassParameters->WritableSkyLightVisibilityRaysData.SkyLightVisibilityRaysDimensions = FIntVector(Dimensions.X, Dimensions.Y, 0);
	PassParameters->WritableSkyLightVisibilityRaysData.OutSkyLightVisibilityRays = GraphBuilder.CreateUAV(SkyLightVisibilityRaysBuffer, EPixelFormat::PF_R32_UINT);

	TShaderMapRef<FGenerateSkyLightVisibilityRaysCS> ComputeShader(GetGlobalShaderMap(FeatureLevel));

	FComputeShaderUtils::AddPass(
		GraphBuilder,
		RDG_EVENT_NAME("GenerateSkyLightVisibilityRays"),
		*ComputeShader,
		PassParameters,
		FComputeShaderUtils::GetGroupCount(FIntPoint(Dimensions.X, Dimensions.Y), FGenerateSkyLightVisibilityRaysCS::kGroupSize)
	);
}

DECLARE_GPU_STAT_NAMED(RayTracingSkyLight, TEXT("Ray Tracing SkyLight"));

void FDeferredShadingSceneRenderer::RenderRayTracingSkyLight(
	FRHICommandListImmediate& RHICmdList,
	TRefCountPtr<IPooledRenderTarget>& SkyLightRT,
	TRefCountPtr<IPooledRenderTarget>& HitDistanceRT
)
{
	SCOPED_DRAW_EVENT(RHICmdList, RayTracingSkyLight);
	SCOPED_GPU_STAT(RHICmdList, RayTracingSkyLight);

	if (!ShouldRenderRayTracingSkyLight(Scene->SkyLight))
	{
		return;
	}

	check(Scene->SkyLight->ProcessedTexture);
	check((Scene->SkyLight->ImportanceSamplingData != nullptr) && Scene->SkyLight->ImportanceSamplingData->bIsValid);

	// Declare render targets
	FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(RHICmdList);
	{
		FPooledRenderTargetDesc Desc = SceneContext.GetSceneColor()->GetDesc();
		Desc.Format = PF_FloatRGBA;
		Desc.Flags &= ~(TexCreate_FastVRAM | TexCreate_Transient);
		GRenderTargetPool.FindFreeElement(RHICmdList, Desc, SkyLightRT, TEXT("RayTracingSkylight"));

		Desc.Format = PF_G16R16;
		GRenderTargetPool.FindFreeElement(RHICmdList, Desc, HitDistanceRT, TEXT("RayTracingSkyLightHitDistance"));
	}

	FRDGBuilder GraphBuilder(RHICmdList);
	FRDGBufferRef SkyLightVisibilityRaysBuffer;
	FIntVector SkyLightVisibilityRaysDimensions;
	if (CVarRayTracingSkyLightDecoupleSampleGeneration.GetValueOnRenderThread() == 1)
	{
		GenerateSkyLightVisibilityRays(GraphBuilder, SkyLightVisibilityRaysBuffer, SkyLightVisibilityRaysDimensions);
	}
	else
	{
		FRDGBufferDesc BufferDesc = FRDGBufferDesc::CreateStructuredDesc(sizeof(FSkyLightVisibilityRays), 1);
		SkyLightVisibilityRaysBuffer = GraphBuilder.CreateBuffer(BufferDesc, TEXT("SkyLightVisibilityRays"));
		SkyLightVisibilityRaysDimensions = FIntVector(1);
	}

	// Define RDG targets
	FRDGTextureRef SkyLightTexture;
	{
		FRDGTextureDesc Desc = SceneContext.GetSceneColor()->GetDesc();
		Desc.Format = PF_FloatRGBA;
		Desc.Flags &= ~(TexCreate_FastVRAM | TexCreate_Transient);
		SkyLightTexture = GraphBuilder.CreateTexture(Desc, TEXT("RayTracingSkylight"));
	}

	FRDGTextureRef RayDistanceTexture;
	{
		FRDGTextureDesc Desc = SceneContext.GetSceneColor()->GetDesc();
		Desc.Format = PF_G16R16;
		Desc.Flags &= ~(TexCreate_FastVRAM | TexCreate_Transient);
		RayDistanceTexture = GraphBuilder.CreateTexture(Desc, TEXT("RayTracingSkyLightHitDistance"));
	}

	FRDGTextureUAV* SkyLightkUAV = GraphBuilder.CreateUAV(SkyLightTexture);
	FRDGTextureUAV* RayDistanceUAV = GraphBuilder.CreateUAV(RayDistanceTexture);

	// Fill Sky Light parameters
	FSkyLightData SkyLightData;
	SetupSkyLightParameters(*Scene, &SkyLightData);

	// Fill Scene Texture parameters
	FSceneTextureParameters SceneTextures;
	SetupSceneTextureParameters(GraphBuilder, &SceneTextures);

	TRefCountPtr<IPooledRenderTarget> SubsurfaceProfileRT((IPooledRenderTarget*)GetSubsufaceProfileTexture_RT(RHICmdList));
	if (!SubsurfaceProfileRT)
	{
		SubsurfaceProfileRT = GSystemTextures.BlackDummy;
	}

	for (FViewInfo& View : Views)
	{
		FSceneViewState* SceneViewState = (FSceneViewState*)View.State;

		// Sky Light Quasi Random data setup
		FIntVector BlueNoiseDimensions;
		FSkyLightQuasiRandomData SkyLightQuasiRandomData;
		SetupSkyLightQuasiRandomParameters(*Scene, View, BlueNoiseDimensions, &SkyLightQuasiRandomData);

		FRayTracingSkyLightRGS::FParameters *PassParameters = GraphBuilder.AllocParameters<FRayTracingSkyLightRGS::FParameters>();
		PassParameters->RWOcclusionMaskUAV = SkyLightkUAV;
		PassParameters->RWRayDistanceUAV = RayDistanceUAV;
		PassParameters->SkyLightData = CreateUniformBufferImmediate(SkyLightData, EUniformBufferUsage::UniformBuffer_SingleDraw);
		PassParameters->SkyLightQuasiRandomData = SkyLightQuasiRandomData;
		PassParameters->SkyLightVisibilityRaysData.SkyLightVisibilityRaysDimensions = SkyLightVisibilityRaysDimensions;
		if (CVarRayTracingSkyLightDecoupleSampleGeneration.GetValueOnRenderThread() == 1)
		{
			PassParameters->SkyLightVisibilityRaysData.SkyLightVisibilityRays = GraphBuilder.CreateSRV(SkyLightVisibilityRaysBuffer, EPixelFormat::PF_R32_UINT);
		}
		PassParameters->SSProfilesTexture = GraphBuilder.RegisterExternalTexture(SubsurfaceProfileRT);
		PassParameters->TransmissionProfilesLinearSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
		PassParameters->SceneTextures = SceneTextures;

		PassParameters->TLAS = View.RayTracingScene.RayTracingSceneRHI->GetShaderResourceView();
		PassParameters->ViewUniformBuffer = View.ViewUniformBuffer;

		FRayTracingSkyLightRGS::FPermutationDomain PermutationVector;
		PermutationVector.Set<FRayTracingSkyLightRGS::FEnableTwoSidedGeometryDim>(CVarRayTracingSkyLightEnableTwoSidedGeometry.GetValueOnRenderThread() != 0);
		PermutationVector.Set<FRayTracingSkyLightRGS::FEnableMaterialsDim>(CVarRayTracingSkyLightEnableMaterials.GetValueOnRenderThread() != 0);
		PermutationVector.Set<FRayTracingSkyLightRGS::FDecoupleSampleGeneration>(CVarRayTracingSkyLightDecoupleSampleGeneration.GetValueOnRenderThread() != 0);
		TShaderMapRef<FRayTracingSkyLightRGS> RayGenerationShader(GetGlobalShaderMap(FeatureLevel), PermutationVector);
		ClearUnusedGraphResources(*RayGenerationShader, PassParameters);

		FIntPoint RayTracingResolution = View.ViewRect.Size();
		GraphBuilder.AddPass(
			RDG_EVENT_NAME("SkyLightRayTracing %dx%d", RayTracingResolution.X, RayTracingResolution.Y),
			PassParameters,
			ERDGPassFlags::Compute,
			[PassParameters, this, &View, RayGenerationShader, RayTracingResolution](FRHICommandList& RHICmdList)
		{
			FRayTracingShaderBindingsWriter GlobalResources;
			SetShaderParameters(GlobalResources, *RayGenerationShader, *PassParameters);

			FRayTracingPipelineState* Pipeline = View.RayTracingMaterialPipeline;
			if (CVarRayTracingSkyLightEnableMaterials.GetValueOnRenderThread() == 0)
			{
				// Declare default pipeline
				FRayTracingPipelineStateInitializer Initializer;
				Initializer.MaxPayloadSizeInBytes = 52; // sizeof(FPackedMaterialClosestHitPayload)
				FRHIRayTracingShader* RayGenShaderTable[] = { RayGenerationShader->GetRayTracingShader() };
				Initializer.SetRayGenShaderTable(RayGenShaderTable);

				FRHIRayTracingShader* HitGroupTable[] = { View.ShaderMap->GetShader<FOpaqueShadowHitGroup>()->GetRayTracingShader() };
				Initializer.SetHitGroupTable(HitGroupTable);
				Initializer.bAllowHitGroupIndexing = false; // Use the same hit shader for all geometry in the scene by disabling SBT indexing.

				Pipeline = PipelineStateCache::GetAndOrCreateRayTracingPipelineState(RHICmdList, Initializer);
			}

			FRHIRayTracingScene* RayTracingSceneRHI = View.RayTracingScene.RayTracingSceneRHI;
			RHICmdList.RayTraceDispatch(Pipeline, RayGenerationShader->GetRayTracingShader(), RayTracingSceneRHI, GlobalResources, RayTracingResolution.X, RayTracingResolution.Y);
		});

		// Denoising
		if (GRayTracingSkyLightDenoiser != 0)
		{
			const IScreenSpaceDenoiser* DefaultDenoiser = IScreenSpaceDenoiser::GetDefaultDenoiser();
			const IScreenSpaceDenoiser* DenoiserToUse = DefaultDenoiser;// GRayTracingGlobalIlluminationDenoiser == 1 ? DefaultDenoiser : GScreenSpaceDenoiser;

			IScreenSpaceDenoiser::FDiffuseIndirectInputs DenoiserInputs;
			DenoiserInputs.Color = SkyLightTexture;
			DenoiserInputs.RayHitDistance = RayDistanceTexture;

			{
				IScreenSpaceDenoiser::FAmbientOcclusionRayTracingConfig RayTracingConfig;
				RayTracingConfig.ResolutionFraction = 1.0;
				RayTracingConfig.RayCountPerPixel = GetSkyLightSamplesPerPixel(Scene->SkyLight);

				RDG_EVENT_SCOPE(GraphBuilder, "%s%s(SkyLight) %dx%d",
					DenoiserToUse != DefaultDenoiser ? TEXT("ThirdParty ") : TEXT(""),
					DenoiserToUse->GetDebugName(),
					View.ViewRect.Width(), View.ViewRect.Height());

				IScreenSpaceDenoiser::FDiffuseIndirectOutputs DenoiserOutputs = DenoiserToUse->DenoiseSkyLight(
					GraphBuilder,
					View,
					&View.PrevViewInfo,
					SceneTextures,
					DenoiserInputs,
					RayTracingConfig);

				SkyLightTexture = DenoiserOutputs.Color;
			}
		}

		if (SceneViewState != nullptr)
		{
			if (CVarRayTracingSkyLightDecoupleSampleGeneration.GetValueOnRenderThread() == 1)
			{
				// Set the Sky Light Visibility Ray dimensions and its extracted pooled RDG buffer on the scene view state
				GraphBuilder.QueueBufferExtraction(SkyLightVisibilityRaysBuffer, &SceneViewState->SkyLightVisibilityRaysBuffer, FRDGResourceState::EAccess::Read, FRDGResourceState::EPipeline::Compute);
				SceneViewState->SkyLightVisibilityRaysDimensions = SkyLightVisibilityRaysDimensions;
			}
			else
			{
				// Set invalid Sky Light Visibility Ray dimensions and pooled RDG buffer
				SceneViewState->SkyLightVisibilityRaysBuffer = nullptr;
				SceneViewState->SkyLightVisibilityRaysDimensions = FIntVector(1);
			}
		}
	}

	GraphBuilder.QueueTextureExtraction(SkyLightTexture, &SkyLightRT);
	GraphBuilder.Execute();
	GVisualizeTexture.SetCheckPoint(RHICmdList, SkyLightRT);
}

class FCompositeSkyLightPS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FCompositeSkyLightPS)
	SHADER_USE_PARAMETER_STRUCT(FCompositeSkyLightPS, FGlobalShader)

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return ShouldCompileRayTracingShadersForProject(Parameters.Platform);
	}

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		RENDER_TARGET_BINDING_SLOTS()
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SkyLightTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, SkyLightTextureSampler)
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, ViewUniformBuffer)

		SHADER_PARAMETER_STRUCT_INCLUDE(FSceneTextureParameters, SceneTextures)
	END_SHADER_PARAMETER_STRUCT()
};

IMPLEMENT_GLOBAL_SHADER(FCompositeSkyLightPS, "/Engine/Private/RayTracing/CompositeSkyLightPS.usf", "CompositeSkyLightPS", SF_Pixel);


#endif // RHI_RAYTRACING

void FDeferredShadingSceneRenderer::CompositeRayTracingSkyLight(
	FRHICommandListImmediate& RHICmdList,
	TRefCountPtr<IPooledRenderTarget>& SkyLightRT,
	TRefCountPtr<IPooledRenderTarget>& HitDistanceRT
)
#if RHI_RAYTRACING
{
	check(SkyLightRT);
	FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(RHICmdList);

	for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
	{
		const FViewInfo& View = Views[ViewIndex];
		FRDGBuilder GraphBuilder(RHICmdList);

		FSceneTextureParameters SceneTextures;
		SetupSceneTextureParameters(GraphBuilder, &SceneTextures);

		FCompositeSkyLightPS::FParameters *PassParameters = GraphBuilder.AllocParameters<FCompositeSkyLightPS::FParameters>();
		PassParameters->SkyLightTexture = GraphBuilder.RegisterExternalTexture(SkyLightRT);
		PassParameters->SkyLightTextureSampler = TStaticSamplerState<SF_Point, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
		PassParameters->ViewUniformBuffer = Views[ViewIndex].ViewUniformBuffer;
		PassParameters->RenderTargets[0] = FRenderTargetBinding(GraphBuilder.RegisterExternalTexture(SceneContext.GetSceneColor()), ERenderTargetLoadAction::ELoad);
		PassParameters->SceneTextures = SceneTextures;

		// dxr_todo: Unify with RTGI compositing workflow
		GraphBuilder.AddPass(
			RDG_EVENT_NAME("GlobalIlluminationComposite"),
			PassParameters,
			ERDGPassFlags::Raster,
			[this, &SceneContext, &View, PassParameters](FRHICommandListImmediate& RHICmdList)
		{
			TShaderMapRef<FPostProcessVS> VertexShader(View.ShaderMap);
			TShaderMapRef<FCompositeSkyLightPS> PixelShader(View.ShaderMap);
			FGraphicsPipelineStateInitializer GraphicsPSOInit;
			RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);

			// Additive blending
			GraphicsPSOInit.BlendState = TStaticBlendState<CW_RGB, BO_Add, BF_One, BF_One>::GetRHI();
			GraphicsPSOInit.RasterizerState = TStaticRasterizerState<FM_Solid, CM_None>::GetRHI();
			GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();

			GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GFilterVertexDeclaration.VertexDeclarationRHI;
			GraphicsPSOInit.BoundShaderState.VertexShaderRHI = GETSAFERHISHADER_VERTEX(*VertexShader);
			GraphicsPSOInit.BoundShaderState.PixelShaderRHI = GETSAFERHISHADER_PIXEL(*PixelShader);
			GraphicsPSOInit.PrimitiveType = PT_TriangleList;
			SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);

			SetShaderParameters(RHICmdList, *PixelShader, PixelShader->GetPixelShader(), *PassParameters);

			RHICmdList.SetViewport((float)View.ViewRect.Min.X, (float)View.ViewRect.Min.Y, 0.0f, (float)View.ViewRect.Max.X, (float)View.ViewRect.Max.Y, 1.0f);

			DrawRectangle(
				RHICmdList,
				0, 0,
				View.ViewRect.Width(), View.ViewRect.Height(),
				View.ViewRect.Min.X, View.ViewRect.Min.Y,
				View.ViewRect.Width(), View.ViewRect.Height(),
				FIntPoint(View.ViewRect.Width(), View.ViewRect.Height()),
				SceneContext.GetBufferSizeXY(),
				*VertexShader
			);
		}
		);

		GraphBuilder.Execute();
	}
}
#else
{
	unimplemented();
}
#endif

class FVisualizeSkyLightMipTreePS : public FGlobalShader
{
	DECLARE_SHADER_TYPE(FVisualizeSkyLightMipTreePS, Global);

public:
	static bool ShouldCache(EShaderPlatform Platform)
	{
		return IsFeatureLevelSupported(Platform, ERHIFeatureLevel::SM5);
	}

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return ShouldCompileRayTracingShadersForProject(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
	}

	FVisualizeSkyLightMipTreePS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{
		DimensionsParameter.Bind(Initializer.ParameterMap, TEXT("Dimensions"));
		MipTreePosXParameter.Bind(Initializer.ParameterMap, TEXT("MipTreePosX"));
		MipTreeNegXParameter.Bind(Initializer.ParameterMap, TEXT("MipTreeNegX"));
		MipTreePosYParameter.Bind(Initializer.ParameterMap, TEXT("MipTreePosY"));
		MipTreeNegYParameter.Bind(Initializer.ParameterMap, TEXT("MipTreeNegY"));
		MipTreePosZParameter.Bind(Initializer.ParameterMap, TEXT("MipTreePosZ"));
		MipTreeNegZParameter.Bind(Initializer.ParameterMap, TEXT("MipTreeNegZ"));
	}

	FVisualizeSkyLightMipTreePS() {}

	template<typename TRHICommandList>
	void SetParameters(
		TRHICommandList& RHICmdList,
		const FViewInfo& View,
		const FIntVector Dimensions,
		const FRWBuffer& MipTreePosX,
		const FRWBuffer& MipTreeNegX,
		const FRWBuffer& MipTreePosY,
		const FRWBuffer& MipTreeNegY,
		const FRWBuffer& MipTreePosZ,
		const FRWBuffer& MipTreeNegZ)
	{
		FRHIPixelShader* ShaderRHI = GetPixelShader();
		FGlobalShader::SetParameters<FViewUniformShaderParameters>(RHICmdList, ShaderRHI, View.ViewUniformBuffer);

		SetShaderValue(RHICmdList, ShaderRHI, DimensionsParameter, Dimensions);
		SetSRVParameter(RHICmdList, ShaderRHI, MipTreePosXParameter, MipTreePosX.SRV);
		SetSRVParameter(RHICmdList, ShaderRHI, MipTreeNegXParameter, MipTreeNegX.SRV);
		SetSRVParameter(RHICmdList, ShaderRHI, MipTreePosYParameter, MipTreePosY.SRV);
		SetSRVParameter(RHICmdList, ShaderRHI, MipTreeNegYParameter, MipTreeNegY.SRV);
		SetSRVParameter(RHICmdList, ShaderRHI, MipTreePosZParameter, MipTreePosZ.SRV);
		SetSRVParameter(RHICmdList, ShaderRHI, MipTreeNegZParameter, MipTreeNegZ.SRV);
	}

	virtual bool Serialize(FArchive& Ar) override
	{
		bool bShaderHasOutdatedParameters = FGlobalShader::Serialize(Ar);
		Ar << DimensionsParameter;
		Ar << MipTreePosXParameter;
		Ar << MipTreeNegXParameter;
		Ar << MipTreePosYParameter;
		Ar << MipTreeNegYParameter;
		Ar << MipTreePosZParameter;
		Ar << MipTreeNegZParameter;
		return bShaderHasOutdatedParameters;
	}

private:
	FShaderParameter DimensionsParameter;
	FShaderResourceParameter MipTreePosXParameter;
	FShaderResourceParameter MipTreeNegXParameter;
	FShaderResourceParameter MipTreePosYParameter;
	FShaderResourceParameter MipTreeNegYParameter;
	FShaderResourceParameter MipTreePosZParameter;
	FShaderResourceParameter MipTreeNegZParameter;
};

IMPLEMENT_SHADER_TYPE(, FVisualizeSkyLightMipTreePS, TEXT("/Engine/Private/RayTracing/VisualizeSkyLightMipTreePS.usf"), TEXT("VisualizeSkyLightMipTreePS"), SF_Pixel)

#if RHI_RAYTRACING
void FDeferredShadingSceneRenderer::VisualizeSkyLightMipTree(
	FRHICommandListImmediate& RHICmdList,
	const FViewInfo& View,
	FRWBuffer& SkyLightMipTreePosX,
	FRWBuffer& SkyLightMipTreeNegX,
	FRWBuffer& SkyLightMipTreePosY,
	FRWBuffer& SkyLightMipTreeNegY,
	FRWBuffer& SkyLightMipTreePosZ,
	FRWBuffer& SkyLightMipTreeNegZ,
	const FIntVector& SkyLightMipDimensions)
{
	// Allocate render target
	FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(RHICmdList);
	FPooledRenderTargetDesc Desc = SceneContext.GetSceneColor()->GetDesc();
	Desc.Flags &= ~(TexCreate_FastVRAM | TexCreate_Transient);
	TRefCountPtr<IPooledRenderTarget> SkyLightMipTreeRT;
	GRenderTargetPool.FindFreeElement(RHICmdList, Desc, SkyLightMipTreeRT, TEXT("SkyLightMipTreeRT"));

	// Define shaders
	const auto ShaderMap = GetGlobalShaderMap(View.GetFeatureLevel());
	TShaderMapRef<FPostProcessVS> VertexShader(ShaderMap);
	TShaderMapRef<FVisualizeSkyLightMipTreePS> PixelShader(ShaderMap);
	FRHITexture* RenderTargets[2] =
	{
		SceneContext.GetSceneColor()->GetRenderTargetItem().TargetableTexture,
		SkyLightMipTreeRT->GetRenderTargetItem().TargetableTexture
	};
	FRHIRenderPassInfo RenderPassInfo(2, RenderTargets, ERenderTargetActions::Load_Store);
	RHICmdList.BeginRenderPass(RenderPassInfo, TEXT("SkyLight Visualization"));

	// PSO definition
	FGraphicsPipelineStateInitializer GraphicsPSOInit;
	RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);
	GraphicsPSOInit.BlendState = TStaticBlendState<CW_RGB, BO_Add, BF_One, BF_One>::GetRHI();
	GraphicsPSOInit.RasterizerState = TStaticRasterizerState<FM_Solid, CM_None>::GetRHI();
	GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();
	GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GFilterVertexDeclaration.VertexDeclarationRHI;
	GraphicsPSOInit.BoundShaderState.VertexShaderRHI = GETSAFERHISHADER_VERTEX(*VertexShader);
	GraphicsPSOInit.BoundShaderState.PixelShaderRHI = GETSAFERHISHADER_PIXEL(*PixelShader);
	GraphicsPSOInit.PrimitiveType = PT_TriangleList;
	SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);

	// Transition to graphics
	RHICmdList.TransitionResource(EResourceTransitionAccess::EReadable, EResourceTransitionPipeline::EComputeToGfx, SkyLightMipTreePosX.UAV);
	RHICmdList.TransitionResource(EResourceTransitionAccess::EReadable, EResourceTransitionPipeline::EComputeToGfx, SkyLightMipTreeNegX.UAV);
	RHICmdList.TransitionResource(EResourceTransitionAccess::EReadable, EResourceTransitionPipeline::EComputeToGfx, SkyLightMipTreePosY.UAV);
	RHICmdList.TransitionResource(EResourceTransitionAccess::EReadable, EResourceTransitionPipeline::EComputeToGfx, SkyLightMipTreeNegY.UAV);
	RHICmdList.TransitionResource(EResourceTransitionAccess::EReadable, EResourceTransitionPipeline::EComputeToGfx, SkyLightMipTreePosZ.UAV);
	RHICmdList.TransitionResource(EResourceTransitionAccess::EReadable, EResourceTransitionPipeline::EComputeToGfx, SkyLightMipTreeNegZ.UAV);

	// Draw
	RHICmdList.SetViewport((float)View.ViewRect.Min.X, (float)View.ViewRect.Min.Y, 0.0f, (float)View.ViewRect.Max.X, (float)View.ViewRect.Max.Y, 1.0f);
	PixelShader->SetParameters(RHICmdList, View, SkyLightMipDimensions, SkyLightMipTreePosX, SkyLightMipTreeNegX, SkyLightMipTreePosY, SkyLightMipTreeNegY, SkyLightMipTreePosZ, SkyLightMipTreeNegZ);
	DrawRectangle(
		RHICmdList,
		0, 0,
		View.ViewRect.Width(), View.ViewRect.Height(),
		View.ViewRect.Min.X, View.ViewRect.Min.Y,
		View.ViewRect.Width(), View.ViewRect.Height(),
		FIntPoint(View.ViewRect.Width(), View.ViewRect.Height()),
		SceneContext.GetBufferSizeXY(),
		*VertexShader);
	ResolveSceneColor(RHICmdList);
	RHICmdList.EndRenderPass();
	GVisualizeTexture.SetCheckPoint(RHICmdList, SkyLightMipTreeRT);

	// Transition to compute
	RHICmdList.TransitionResource(EResourceTransitionAccess::ERWBarrier, EResourceTransitionPipeline::EGfxToCompute, SkyLightMipTreePosX.UAV);
	RHICmdList.TransitionResource(EResourceTransitionAccess::ERWBarrier, EResourceTransitionPipeline::EGfxToCompute, SkyLightMipTreeNegX.UAV);
	RHICmdList.TransitionResource(EResourceTransitionAccess::ERWBarrier, EResourceTransitionPipeline::EGfxToCompute, SkyLightMipTreePosY.UAV);
	RHICmdList.TransitionResource(EResourceTransitionAccess::ERWBarrier, EResourceTransitionPipeline::EGfxToCompute, SkyLightMipTreeNegY.UAV);
	RHICmdList.TransitionResource(EResourceTransitionAccess::ERWBarrier, EResourceTransitionPipeline::EGfxToCompute, SkyLightMipTreePosZ.UAV);
	RHICmdList.TransitionResource(EResourceTransitionAccess::ERWBarrier, EResourceTransitionPipeline::EGfxToCompute, SkyLightMipTreeNegZ.UAV);
}
#endif
