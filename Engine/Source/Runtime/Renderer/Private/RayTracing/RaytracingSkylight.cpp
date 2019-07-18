// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

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
#include "BlueNoise.h"

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
	TEXT("Sets the samples-per-pixel for ray tracing SkyLight (default = 1.0e7)")
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

bool ShouldRenderRayTracingSkyLight(const FSkyLightSceneProxy* SkyLightSceneProxy)
{
	if (SkyLightSceneProxy != nullptr)
	{
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

		return IsRayTracingEnabled() && bRayTracingSkyEnabled;
	}
	else
	{
		return false;
	}
}

IMPLEMENT_GLOBAL_SHADER_PARAMETER_STRUCT(FSkyLightData, "SkyLight");

void SetupSkyLightParameters(
	const FScene& Scene,
	FSkyLightData* SkyLightData
)
{
	// dxr_todo: factor out these pass constants
	SkyLightData->SamplesPerPixel = -1;
	SkyLightData->SamplingStopLevel = 0;
	SkyLightData->MaxRayDistance = 1.0e27;
	SkyLightData->MaxNormalBias = GetRaytracingMaxNormalBias();

	if (Scene.SkyLight && Scene.SkyLight->ProcessedTexture)
	{
		SkyLightData->Color = FVector(Scene.SkyLight->GetEffectiveLightColor());
		SkyLightData->Texture = Scene.SkyLight->ProcessedTexture->TextureRHI;
		SkyLightData->TextureDimensions = FIntVector(Scene.SkyLight->ProcessedTexture->GetSizeX(), Scene.SkyLight->ProcessedTexture->GetSizeY(), 0);
		SkyLightData->TextureSampler = Scene.SkyLight->ProcessedTexture->SamplerStateRHI;
		SkyLightData->MipDimensions = Scene.SkyLight->SkyLightMipDimensions;

		SkyLightData->MipTreePosX = Scene.SkyLight->SkyLightMipTreePosX.SRV;
		SkyLightData->MipTreeNegX = Scene.SkyLight->SkyLightMipTreeNegX.SRV;
		SkyLightData->MipTreePosY = Scene.SkyLight->SkyLightMipTreePosY.SRV;
		SkyLightData->MipTreeNegY = Scene.SkyLight->SkyLightMipTreeNegY.SRV;
		SkyLightData->MipTreePosZ = Scene.SkyLight->SkyLightMipTreePosZ.SRV;
		SkyLightData->MipTreeNegZ = Scene.SkyLight->SkyLightMipTreeNegZ.SRV;

		SkyLightData->MipTreePdfPosX = Scene.SkyLight->SkyLightMipTreePdfPosX.SRV;
		SkyLightData->MipTreePdfNegX = Scene.SkyLight->SkyLightMipTreePdfNegX.SRV;
		SkyLightData->MipTreePdfPosY = Scene.SkyLight->SkyLightMipTreePdfPosY.SRV;
		SkyLightData->MipTreePdfNegY = Scene.SkyLight->SkyLightMipTreePdfNegY.SRV;
		SkyLightData->MipTreePdfPosZ = Scene.SkyLight->SkyLightMipTreePdfPosZ.SRV;
		SkyLightData->MipTreePdfNegZ = Scene.SkyLight->SkyLightMipTreePdfNegZ.SRV;
		SkyLightData->SolidAnglePdf = Scene.SkyLight->SolidAnglePdf.SRV;
	}
	else
	{
		SkyLightData->Color = FVector(0.0);
		SkyLightData->Texture = GBlackTextureCube->TextureRHI;
		SkyLightData->TextureSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
		SkyLightData->MipDimensions = FIntVector(0);

		auto BlackTextureBuffer = RHICreateShaderResourceView(GBlackTexture->TextureRHI->GetTexture2D(), 0);
		SkyLightData->MipTreePosX = BlackTextureBuffer;
		SkyLightData->MipTreeNegX = BlackTextureBuffer;
		SkyLightData->MipTreePosY = BlackTextureBuffer;
		SkyLightData->MipTreeNegY = BlackTextureBuffer;
		SkyLightData->MipTreePosZ = BlackTextureBuffer;
		SkyLightData->MipTreeNegZ = BlackTextureBuffer;

		SkyLightData->MipTreePdfPosX = BlackTextureBuffer;
		SkyLightData->MipTreePdfNegX = BlackTextureBuffer;
		SkyLightData->MipTreePdfPosY = BlackTextureBuffer;
		SkyLightData->MipTreePdfNegY = BlackTextureBuffer;
		SkyLightData->MipTreePdfPosZ = BlackTextureBuffer;
		SkyLightData->MipTreePdfNegZ = BlackTextureBuffer;
		SkyLightData->SolidAnglePdf = BlackTextureBuffer;
	}
}

DECLARE_GPU_STAT_NAMED(RayTracingSkyLight, TEXT("Ray Tracing SkyLight"));
DECLARE_GPU_STAT_NAMED(BuildSkyLightMipTree, TEXT("Build SkyLight Mip Tree"));

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
		SHADER_PARAMETER_STRUCT_REF(FHaltonIteration, HaltonIteration)
		SHADER_PARAMETER_STRUCT_REF(FHaltonPrimes, HaltonPrimes)
		SHADER_PARAMETER_STRUCT_REF(FBlueNoise, BlueNoise)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<SkyLightVisibilityRays>, SkyLightVisibilityRays)
		SHADER_PARAMETER(FIntVector, SkyLightVisibilityRaysDimensions)

		SHADER_PARAMETER_STRUCT_INCLUDE(FSceneTextureParameters, SceneTextures)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SSProfilesTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, TransmissionProfilesLinearSampler)
	END_SHADER_PARAMETER_STRUCT()
};

IMPLEMENT_GLOBAL_SHADER(FRayTracingSkyLightRGS, "/Engine/Private/Raytracing/RaytracingSkylightRGS.usf", "SkyLightRGS", SF_RayGen);

void FDeferredShadingSceneRenderer::BuildSkyLightCdfs(FRHICommandListImmediate& RHICmdList, FSkyLightSceneProxy* SkyLight)
{
	SCOPED_DRAW_EVENT(RHICmdList, BuildSkyLightMipTree);
	SCOPED_GPU_STAT(RHICmdList, BuildSkyLightMipTree);

	BuildSkyLightMipTree(RHICmdList, SkyLight->ProcessedTexture->TextureRHI, SkyLight->SkyLightMipTreePosX, SkyLight->SkyLightMipTreeNegX, SkyLight->SkyLightMipTreePosY, SkyLight->SkyLightMipTreeNegY, SkyLight->SkyLightMipTreePosZ, SkyLight->SkyLightMipTreeNegZ, SkyLight->SkyLightMipDimensions);
	BuildSkyLightMipTreePdf(RHICmdList, SkyLight->SkyLightMipTreePosX, SkyLight->SkyLightMipTreeNegX, SkyLight->SkyLightMipTreePosY, SkyLight->SkyLightMipTreeNegY, SkyLight->SkyLightMipTreePosZ, SkyLight->SkyLightMipTreeNegZ, SkyLight->SkyLightMipDimensions,
		SkyLight->SkyLightMipTreePdfPosX, SkyLight->SkyLightMipTreePdfNegX, SkyLight->SkyLightMipTreePdfPosY, SkyLight->SkyLightMipTreePdfNegY, SkyLight->SkyLightMipTreePdfPosZ, SkyLight->SkyLightMipTreePdfNegZ);
	BuildSolidAnglePdf(RHICmdList, SkyLight->SkyLightMipDimensions, SkyLight->SolidAnglePdf);
	Scene->SkyLight->IsDirtyImportanceSamplingData = false;
}

class FBuildMipTreeCS : public FGlobalShader
{
	DECLARE_SHADER_TYPE(FBuildMipTreeCS, Global)

public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return ShouldCompileRayTracingShadersForProject(Parameters.Platform);
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

	FBuildMipTreeCS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{
		TextureParameter.Bind(Initializer.ParameterMap, TEXT("Texture"));
		TextureSamplerParameter.Bind(Initializer.ParameterMap, TEXT("TextureSampler"));
		DimensionsParameter.Bind(Initializer.ParameterMap, TEXT("Dimensions"));
		FaceIndexParameter.Bind(Initializer.ParameterMap, TEXT("FaceIndex"));
		MipLevelParameter.Bind(Initializer.ParameterMap, TEXT("MipLevel"));
		MipTreeParameter.Bind(Initializer.ParameterMap, TEXT("MipTree"));
	}

	FBuildMipTreeCS() {}

	void SetParameters(
		FRHICommandList& RHICmdList,
		FTextureRHIRef Texture,
		const FIntVector& Dimensions,
		uint32 FaceIndex,
		uint32 MipLevel,
		FRWBuffer& MipTree)
	{
		FRHIComputeShader* ShaderRHI = GetComputeShader();

		SetShaderValue(RHICmdList, ShaderRHI, DimensionsParameter, Dimensions);
		SetShaderValue(RHICmdList, ShaderRHI, FaceIndexParameter, FaceIndex);
		SetShaderValue(RHICmdList, ShaderRHI, MipLevelParameter, MipLevel);
		SetTextureParameter(RHICmdList, ShaderRHI, TextureParameter, TextureSamplerParameter, TStaticSamplerState<SF_Bilinear>::GetRHI(), Texture);

		check(MipTreeParameter.IsBound());
		MipTreeParameter.SetBuffer(RHICmdList, ShaderRHI, MipTree);
	}

	void UnsetParameters(
		FRHICommandList& RHICmdList,
		EResourceTransitionAccess TransitionAccess,
		EResourceTransitionPipeline TransitionPipeline,
		FRWBuffer& MipTree)
	{
		FRHIComputeShader* ShaderRHI = GetComputeShader();

		MipTreeParameter.UnsetUAV(RHICmdList, ShaderRHI);
		RHICmdList.TransitionResource(TransitionAccess, TransitionPipeline, MipTree.UAV);
	}

	virtual bool Serialize(FArchive& Ar)
	{
		bool bShaderHasOutdatedParameters = FGlobalShader::Serialize(Ar);
		Ar << TextureParameter;
		Ar << TextureSamplerParameter;
		Ar << DimensionsParameter;
		Ar << FaceIndexParameter;
		Ar << MipLevelParameter;
		Ar << MipTreeParameter;
		return bShaderHasOutdatedParameters;
	}

private:
	FShaderResourceParameter TextureParameter;
	FShaderResourceParameter TextureSamplerParameter;

	FShaderParameter DimensionsParameter;
	FShaderParameter FaceIndexParameter;
	FShaderParameter MipLevelParameter;
	FRWShaderParameter MipTreeParameter;
};

IMPLEMENT_SHADER_TYPE(, FBuildMipTreeCS, TEXT("/Engine/Private/Raytracing/BuildMipTreeCS.usf"), TEXT("BuildMipTreeCS"), SF_Compute)

void FDeferredShadingSceneRenderer::BuildSkyLightMipTree(
	FRHICommandListImmediate& RHICmdList,
	FTextureRHIRef SkyLightTexture,
	FRWBuffer& SkyLightMipTreePosX,
	FRWBuffer& SkyLightMipTreeNegX,
	FRWBuffer& SkyLightMipTreePosY,
	FRWBuffer& SkyLightMipTreeNegY,
	FRWBuffer& SkyLightMipTreePosZ,
	FRWBuffer& SkyLightMipTreeNegZ,
	FIntVector& SkyLightMipTreeDimensions
)
{
	const auto ShaderMap = GetGlobalShaderMap(FeatureLevel);
	TShaderMapRef<FBuildMipTreeCS> BuildSkyLightMipTreeComputeShader(ShaderMap);
	RHICmdList.SetComputeShader(BuildSkyLightMipTreeComputeShader->GetComputeShader());

	FRWBuffer* MipTrees[] = {
		&SkyLightMipTreePosX,
		&SkyLightMipTreeNegX,
		&SkyLightMipTreePosY,
		&SkyLightMipTreeNegY,
		&SkyLightMipTreePosZ,
		&SkyLightMipTreeNegZ
	};

	// Allocate MIP tree
	FIntVector TextureSize = SkyLightTexture->GetSizeXYZ();
	uint32 MipLevelCount = FMath::Min(FMath::CeilLogTwo(TextureSize.X), FMath::CeilLogTwo(TextureSize.Y));
	SkyLightMipTreeDimensions = FIntVector(1 << MipLevelCount, 1 << MipLevelCount, 1);
	uint32 NumElements = SkyLightMipTreeDimensions.X * SkyLightMipTreeDimensions.Y;
	for (uint32 MipLevel = 1; MipLevel <= MipLevelCount; ++MipLevel)
	{
		uint32 NumElementsInLevel = (SkyLightMipTreeDimensions.X >> MipLevel) * (SkyLightMipTreeDimensions.Y >> MipLevel);
		NumElements += NumElementsInLevel;
	}

	for (uint32 FaceIndex = 0; FaceIndex < 6; ++FaceIndex)
	{
		MipTrees[FaceIndex]->Initialize(sizeof(float), NumElements, PF_R32_FLOAT, BUF_UnorderedAccess | BUF_ShaderResource);
	}

	// Execute hierarchical build
	for (uint32 MipLevel = 0; MipLevel <= MipLevelCount; ++MipLevel)
	{
		for (uint32 FaceIndex = 0; FaceIndex < 6; ++FaceIndex)
		{
			BuildSkyLightMipTreeComputeShader->SetParameters(RHICmdList, SkyLightTexture, SkyLightMipTreeDimensions, FaceIndex, MipLevel, *MipTrees[FaceIndex]);
			FIntVector MipLevelDimensions = FIntVector(SkyLightMipTreeDimensions.X >> MipLevel, SkyLightMipTreeDimensions.Y >> MipLevel, 1);
			FIntVector NumGroups = FIntVector::DivideAndRoundUp(MipLevelDimensions, FBuildMipTreeCS::GetGroupSize());
			DispatchComputeShader(RHICmdList, *BuildSkyLightMipTreeComputeShader, NumGroups.X, NumGroups.Y, 1);
			BuildSkyLightMipTreeComputeShader->UnsetParameters(RHICmdList, EResourceTransitionAccess::ERWBarrier, EResourceTransitionPipeline::EComputeToCompute, *MipTrees[FaceIndex]);
		}

		FComputeFenceRHIRef Fence = RHICmdList.CreateComputeFence(TEXT("SkyLightMipTree"));
		RHICmdList.TransitionResource(EResourceTransitionAccess::ERWBarrier, EResourceTransitionPipeline::EComputeToCompute, MipTrees[0]->UAV);
		RHICmdList.TransitionResource(EResourceTransitionAccess::ERWBarrier, EResourceTransitionPipeline::EComputeToCompute, MipTrees[1]->UAV);
		RHICmdList.TransitionResource(EResourceTransitionAccess::ERWBarrier, EResourceTransitionPipeline::EComputeToCompute, MipTrees[2]->UAV);
		RHICmdList.TransitionResource(EResourceTransitionAccess::ERWBarrier, EResourceTransitionPipeline::EComputeToCompute, MipTrees[3]->UAV);
		RHICmdList.TransitionResource(EResourceTransitionAccess::ERWBarrier, EResourceTransitionPipeline::EComputeToCompute, MipTrees[4]->UAV);
		RHICmdList.TransitionResource(EResourceTransitionAccess::ERWBarrier, EResourceTransitionPipeline::EComputeToCompute, MipTrees[5]->UAV, Fence);
	}
}

class FBuildSolidAnglePdfCS : public FGlobalShader
{
	DECLARE_SHADER_TYPE(FBuildSolidAnglePdfCS, Global)

public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return ShouldCompileRayTracingShadersForProject(Parameters.Platform);
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

	FBuildSolidAnglePdfCS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{
		MipLevelParameter.Bind(Initializer.ParameterMap, TEXT("MipLevel"));
		DimensionsParameter.Bind(Initializer.ParameterMap, TEXT("Dimensions"));
		SolidAnglePdfParameter.Bind(Initializer.ParameterMap, TEXT("SolidAnglePdf"));
	}

	FBuildSolidAnglePdfCS() {}

	void SetParameters(
		FRHICommandList& RHICmdList,
		uint32 MipLevel,
		const FIntVector& Dimensions,
		FRWBuffer& SolidAnglePdf
	)
	{
		FRHIComputeShader* ShaderRHI = GetComputeShader();

		SetShaderValue(RHICmdList, ShaderRHI, MipLevelParameter, MipLevel);
		SetShaderValue(RHICmdList, ShaderRHI, DimensionsParameter, Dimensions);
		check(SolidAnglePdfParameter.IsBound());
		SolidAnglePdfParameter.SetBuffer(RHICmdList, ShaderRHI, SolidAnglePdf);
	}

	void UnsetParameters(
		FRHICommandList& RHICmdList,
		EResourceTransitionAccess TransitionAccess,
		EResourceTransitionPipeline TransitionPipeline,
		FRWBuffer& MipTreePdf,
		FRHIComputeFence* Fence
	)
	{
		FRHIComputeShader* ShaderRHI = GetComputeShader();

		SolidAnglePdfParameter.UnsetUAV(RHICmdList, ShaderRHI);
		RHICmdList.TransitionResource(TransitionAccess, TransitionPipeline, MipTreePdf.UAV, Fence);
	}

	virtual bool Serialize(FArchive& Ar)
	{
		bool bShaderHasOutdatedParameters = FGlobalShader::Serialize(Ar);
		Ar << MipLevelParameter;
		Ar << DimensionsParameter;
		Ar << SolidAnglePdfParameter;
		return bShaderHasOutdatedParameters;
	}

private:
	FShaderParameter MipLevelParameter;
	FShaderParameter DimensionsParameter;
	FRWShaderParameter SolidAnglePdfParameter;
};

IMPLEMENT_SHADER_TYPE(, FBuildSolidAnglePdfCS, TEXT("/Engine/Private/Raytracing/BuildSolidAnglePdfCS.usf"), TEXT("BuildSolidAnglePdfCS"), SF_Compute)

void FDeferredShadingSceneRenderer::BuildSolidAnglePdf(
	FRHICommandListImmediate& RHICmdList,
	const FIntVector& Dimensions,
	FRWBuffer& SolidAnglePdf
)
{
	const auto ShaderMap = GetGlobalShaderMap(FeatureLevel);
	TShaderMapRef<FBuildSolidAnglePdfCS> BuildSolidAnglePdfComputeShader(ShaderMap);
	RHICmdList.SetComputeShader(BuildSolidAnglePdfComputeShader->GetComputeShader());

	uint32 NumElements = Dimensions.X * Dimensions.Y;
	uint32 MipLevelCount = FMath::Log2(Dimensions.X);
	for (uint32 MipLevel = 1; MipLevel <= MipLevelCount; ++MipLevel)
	{
		NumElements += (Dimensions.X >> MipLevel) * (Dimensions.Y >> MipLevel);
	}
	SolidAnglePdf.Initialize(sizeof(float), NumElements, PF_R32_FLOAT, BUF_UnorderedAccess | BUF_ShaderResource);

	for (uint32 MipLevel = 0; MipLevel <= MipLevelCount; ++MipLevel)
	{
		FComputeFenceRHIRef ComputeFence = RHICmdList.CreateComputeFence(TEXT("SkyLight SolidAnglePdf Build"));
		BuildSolidAnglePdfComputeShader->SetParameters(RHICmdList, MipLevel, Dimensions, SolidAnglePdf);
		FIntVector NumGroups = FIntVector::DivideAndRoundUp(Dimensions, FBuildSolidAnglePdfCS::GetGroupSize());
		DispatchComputeShader(RHICmdList, *BuildSolidAnglePdfComputeShader, NumGroups.X, NumGroups.Y, 1);
		BuildSolidAnglePdfComputeShader->UnsetParameters(RHICmdList, EResourceTransitionAccess::ERWBarrier, EResourceTransitionPipeline::EComputeToCompute, SolidAnglePdf, ComputeFence);
	}
}

class FBuildMipTreePdfCS : public FGlobalShader
{
	DECLARE_SHADER_TYPE(FBuildMipTreePdfCS, Global)

public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return ShouldCompileRayTracingShadersForProject(Parameters.Platform);
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

	FBuildMipTreePdfCS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{
		MipTreeParameter.Bind(Initializer.ParameterMap, TEXT("MipTree"));
		DimensionsParameter.Bind(Initializer.ParameterMap, TEXT("Dimensions"));
		MipLevelParameter.Bind(Initializer.ParameterMap, TEXT("MipLevel"));
		MipTreePdfParameter.Bind(Initializer.ParameterMap, TEXT("MipTreePdf"));
	}

	FBuildMipTreePdfCS() {}

	void SetParameters(
		FRHICommandList& RHICmdList,
		const FRWBuffer& MipTree,
		const FIntVector& Dimensions,
		uint32 MipLevel,
		FRWBuffer& MipTreePdf)
	{
		FRHIComputeShader* ShaderRHI = GetComputeShader();

		SetSRVParameter(RHICmdList, ShaderRHI, MipTreeParameter, MipTree.SRV);
		SetShaderValue(RHICmdList, ShaderRHI, DimensionsParameter, Dimensions);
		SetShaderValue(RHICmdList, ShaderRHI, MipLevelParameter, MipLevel);

		check(MipTreePdfParameter.IsBound());
		MipTreePdfParameter.SetBuffer(RHICmdList, ShaderRHI, MipTreePdf);
	}

	void UnsetParameters(
		FRHICommandList& RHICmdList,
		EResourceTransitionAccess TransitionAccess,
		EResourceTransitionPipeline TransitionPipeline,
		FRWBuffer& MipTreePdf)
	{
		FRHIComputeShader* ShaderRHI = GetComputeShader();

		MipTreePdfParameter.UnsetUAV(RHICmdList, ShaderRHI);
		RHICmdList.TransitionResource(TransitionAccess, TransitionPipeline, MipTreePdf.UAV);
	}

	virtual bool Serialize(FArchive& Ar)
	{
		bool bShaderHasOutdatedParameters = FGlobalShader::Serialize(Ar);
		Ar << MipTreeParameter;
		Ar << DimensionsParameter;
		Ar << MipLevelParameter;
		Ar << MipTreePdfParameter;
		return bShaderHasOutdatedParameters;
	}

private:
	FShaderResourceParameter MipTreeParameter;
	FShaderParameter DimensionsParameter;
	FShaderParameter MipLevelParameter;

	FRWShaderParameter MipTreePdfParameter;
};

IMPLEMENT_SHADER_TYPE(, FBuildMipTreePdfCS, TEXT("/Engine/Private/Raytracing/BuildMipTreePdfCS.usf"), TEXT("BuildMipTreePdfCS"), SF_Compute)

void FDeferredShadingSceneRenderer::BuildSkyLightMipTreePdf(
	FRHICommandListImmediate& RHICmdList,
	const FRWBuffer& SkyLightMipTreePosX,
	const FRWBuffer& SkyLightMipTreeNegX,
	const FRWBuffer& SkyLightMipTreePosY,
	const FRWBuffer& SkyLightMipTreeNegY,
	const FRWBuffer& SkyLightMipTreePosZ,
	const FRWBuffer& SkyLightMipTreeNegZ,
	const FIntVector& SkyLightMipTreeDimensions,
	FRWBuffer& SkyLightMipTreePdfPosX,
	FRWBuffer& SkyLightMipTreePdfNegX,
	FRWBuffer& SkyLightMipTreePdfPosY,
	FRWBuffer& SkyLightMipTreePdfNegY,
	FRWBuffer& SkyLightMipTreePdfPosZ,
	FRWBuffer& SkyLightMipTreePdfNegZ
)
{
	const FRWBuffer* MipTrees[] = {
		&SkyLightMipTreePosX,
		&SkyLightMipTreeNegX,
		&SkyLightMipTreePosY,
		&SkyLightMipTreeNegY,
		&SkyLightMipTreePosZ,
		&SkyLightMipTreeNegZ
	};

	FRWBuffer* MipTreePdfs[] = {
		&SkyLightMipTreePdfPosX,
		&SkyLightMipTreePdfNegX,
		&SkyLightMipTreePdfPosY,
		&SkyLightMipTreePdfNegY,
		&SkyLightMipTreePdfPosZ,
		&SkyLightMipTreePdfNegZ
	};

	const auto ShaderMap = GetGlobalShaderMap(FeatureLevel);
	TShaderMapRef<FBuildMipTreePdfCS> BuildSkyLightMipTreePdfComputeShader(ShaderMap);
	RHICmdList.SetComputeShader(BuildSkyLightMipTreePdfComputeShader->GetComputeShader());

	uint32 NumElements = SkyLightMipTreePosX.NumBytes / sizeof(float);
	uint32 MipLevelCount = FMath::Log2(SkyLightMipTreeDimensions.X);
	for (uint32 FaceIndex = 0; FaceIndex < 6; ++FaceIndex)
	{
		MipTreePdfs[FaceIndex]->Initialize(sizeof(float), NumElements, PF_R32_FLOAT, BUF_UnorderedAccess | BUF_ShaderResource);

		// Execute hierarchical build
		uint32 MipLevel = 0;
		{
			BuildSkyLightMipTreePdfComputeShader->SetParameters(RHICmdList, *MipTrees[FaceIndex], SkyLightMipTreeDimensions, MipLevel, *MipTreePdfs[FaceIndex]);
			FIntVector MipLevelDimensions = FIntVector(SkyLightMipTreeDimensions.X >> MipLevel, SkyLightMipTreeDimensions.Y >> MipLevel, 1);
			FIntVector NumGroups = FIntVector::DivideAndRoundUp(MipLevelDimensions, FBuildMipTreeCS::GetGroupSize());
			DispatchComputeShader(RHICmdList, *BuildSkyLightMipTreePdfComputeShader, NumGroups.X, NumGroups.Y, 1);
		}
		BuildSkyLightMipTreePdfComputeShader->UnsetParameters(RHICmdList, EResourceTransitionAccess::ERWBarrier, EResourceTransitionPipeline::EComputeToCompute, *MipTreePdfs[FaceIndex]);
	}

	FComputeFenceRHIRef Fence = RHICmdList.CreateComputeFence(TEXT("SkyLightMipTreePdf"));
	RHICmdList.TransitionResource(EResourceTransitionAccess::ERWBarrier, EResourceTransitionPipeline::EComputeToCompute, MipTreePdfs[0]->UAV);
	RHICmdList.TransitionResource(EResourceTransitionAccess::ERWBarrier, EResourceTransitionPipeline::EComputeToCompute, MipTreePdfs[1]->UAV);
	RHICmdList.TransitionResource(EResourceTransitionAccess::ERWBarrier, EResourceTransitionPipeline::EComputeToCompute, MipTreePdfs[2]->UAV);
	RHICmdList.TransitionResource(EResourceTransitionAccess::ERWBarrier, EResourceTransitionPipeline::EComputeToCompute, MipTreePdfs[3]->UAV);
	RHICmdList.TransitionResource(EResourceTransitionAccess::ERWBarrier, EResourceTransitionPipeline::EComputeToCompute, MipTreePdfs[4]->UAV);
	RHICmdList.TransitionResource(EResourceTransitionAccess::ERWBarrier, EResourceTransitionPipeline::EComputeToCompute, MipTreePdfs[5]->UAV, Fence);
}

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
	const auto ShaderMap = GetGlobalShaderMap(FeatureLevel);
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
	RHICmdList.SetViewport(View.ViewRect.Min.X, View.ViewRect.Min.Y, 0.0f, View.ViewRect.Max.X, View.ViewRect.Max.Y, 1.0f);
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

struct FSkyLightVisibilityRays
{
	FVector4 DirectionAndPdf;
};

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
		// Input
		SHADER_PARAMETER(FIntVector, Dimensions)
		SHADER_PARAMETER(int32, SamplesPerPixel)
		SHADER_PARAMETER_STRUCT_REF(FSkyLightData, SkyLightData)

		SHADER_PARAMETER_STRUCT_REF(FHaltonIteration, HaltonIteration)
		SHADER_PARAMETER_STRUCT_REF(FHaltonPrimes, HaltonPrimes)
		SHADER_PARAMETER_STRUCT_REF(FBlueNoise, BlueNoise)

		// Output
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<FSkyLightVisibilityRays>, OutSkyLightVisibilityRays)
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
	uint32 SamplesPerPixel = GRayTracingSkyLightSamplesPerPixel >= 0 ? GRayTracingSkyLightSamplesPerPixel : Scene->SkyLight->SamplesPerPixel;
	SkyLightData.SamplesPerPixel = SamplesPerPixel;
	SkyLightData.MaxRayDistance = GRayTracingSkyLightMaxRayDistance;
	SkyLightData.SamplingStopLevel = GRayTracingSkyLightSamplingStopLevel;

	// Sampling state
	FHaltonPrimes HaltonPrimes;
	InitializeHaltonPrimes(Scene->HaltonPrimesResource, HaltonPrimes);

	FBlueNoise BlueNoise;
	InitializeBlueNoise(BlueNoise);
	Dimensions = FIntVector(BlueNoise.Dimensions.X, BlueNoise.Dimensions.Y, 0);

	// Halton iteration
	uint32 IterationCount = FMath::Max(SamplesPerPixel, 1u);
	uint32 SequenceCount = 1;
	uint32 DimensionCount = 3;
	FHaltonSequenceIteration HaltonSequenceIteration(Scene->HaltonSequence, IterationCount, SequenceCount, DimensionCount, Views[0].ViewState ? (Views[0].ViewState->FrameIndex % 1024) : 0);

	FHaltonIteration HaltonIteration;
	InitializeHaltonSequenceIteration(HaltonSequenceIteration, HaltonIteration);

	// Output structured buffer creation
	FRDGBufferDesc BufferDesc = FRDGBufferDesc::CreateStructuredDesc(sizeof(FSkyLightVisibilityRays), Dimensions.X * Dimensions.Y * SkyLightData.SamplesPerPixel);
	SkyLightVisibilityRaysBuffer = GraphBuilder.CreateBuffer(BufferDesc, TEXT("SkyLightVisibilityRays"));

	// Compute Pass parameter definition
	FGenerateSkyLightVisibilityRaysCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FGenerateSkyLightVisibilityRaysCS::FParameters>();
	PassParameters->Dimensions = FIntVector(Dimensions.X, Dimensions.Y, 0);
	PassParameters->SkyLightData = CreateUniformBufferImmediate(SkyLightData, EUniformBufferUsage::UniformBuffer_SingleDraw);
	PassParameters->HaltonPrimes = CreateUniformBufferImmediate(HaltonPrimes, EUniformBufferUsage::UniformBuffer_SingleDraw);
	PassParameters->BlueNoise = CreateUniformBufferImmediate(BlueNoise, EUniformBufferUsage::UniformBuffer_SingleDraw);
	PassParameters->HaltonIteration = CreateUniformBufferImmediate(HaltonIteration, EUniformBufferUsage::UniformBuffer_SingleDraw);
	PassParameters->OutSkyLightVisibilityRays = GraphBuilder.CreateUAV(SkyLightVisibilityRaysBuffer, EPixelFormat::PF_R32_UINT);

	TShaderMapRef<FGenerateSkyLightVisibilityRaysCS> ComputeShader(GetGlobalShaderMap(FeatureLevel));

	FComputeShaderUtils::AddPass(
		GraphBuilder,
		RDG_EVENT_NAME("GenerateSkyLightVisibilityRays"),
		*ComputeShader,
		PassParameters,
		FComputeShaderUtils::GetGroupCount(FIntPoint(Dimensions.X, Dimensions.Y), FGenerateSkyLightVisibilityRaysCS::kGroupSize)
	);
}

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

	if (Scene->SkyLight->ShouldRebuildCdf())
	{
		BuildSkyLightCdfs(RHICmdList, Scene->SkyLight);
	}

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

	// Sampling state
	FHaltonPrimes HaltonPrimes;
	InitializeHaltonPrimes(Scene->HaltonPrimesResource, HaltonPrimes);

	FBlueNoise BlueNoise;
	InitializeBlueNoise(BlueNoise);

	// Fill SkyLight parameters
	FSkyLightData SkyLightData;
	SetupSkyLightParameters(*Scene, &SkyLightData);
	uint32 SamplesPerPixel = GRayTracingSkyLightSamplesPerPixel >= 0 ? GRayTracingSkyLightSamplesPerPixel : Scene->SkyLight->SamplesPerPixel;
	SkyLightData.SamplesPerPixel = SamplesPerPixel;
	SkyLightData.MaxRayDistance = GRayTracingSkyLightMaxRayDistance;
	SkyLightData.SamplingStopLevel = GRayTracingSkyLightSamplingStopLevel;

	FSceneTextureParameters SceneTextures;
	SetupSceneTextureParameters(GraphBuilder, &SceneTextures);

	TRefCountPtr<IPooledRenderTarget> SubsurfaceProfileRT((IPooledRenderTarget*)GetSubsufaceProfileTexture_RT(RHICmdList));
	if (!SubsurfaceProfileRT)
	{
		SubsurfaceProfileRT = GSystemTextures.BlackDummy;
	}

	// View-invariant parameters
	FRayTracingSkyLightRGS::FParameters *PassParameters = GraphBuilder.AllocParameters<FRayTracingSkyLightRGS::FParameters>();
	PassParameters->RWOcclusionMaskUAV = GraphBuilder.CreateUAV(SkyLightTexture);
	PassParameters->RWRayDistanceUAV = GraphBuilder.CreateUAV(RayDistanceTexture);
	PassParameters->SkyLightData = CreateUniformBufferImmediate(SkyLightData, EUniformBufferUsage::UniformBuffer_SingleDraw);
	PassParameters->HaltonPrimes = CreateUniformBufferImmediate(HaltonPrimes, EUniformBufferUsage::UniformBuffer_SingleDraw);
	PassParameters->BlueNoise = CreateUniformBufferImmediate(BlueNoise, EUniformBufferUsage::UniformBuffer_SingleDraw);
	PassParameters->SkyLightVisibilityRaysDimensions = SkyLightVisibilityRaysDimensions;
	if (CVarRayTracingSkyLightDecoupleSampleGeneration.GetValueOnRenderThread() == 1)
	{
		PassParameters->SkyLightVisibilityRays = GraphBuilder.CreateSRV(SkyLightVisibilityRaysBuffer, EPixelFormat::PF_R32_UINT);
	}
	PassParameters->SSProfilesTexture = GraphBuilder.RegisterExternalTexture(SubsurfaceProfileRT);
	PassParameters->TransmissionProfilesLinearSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
	PassParameters->SceneTextures = SceneTextures;

	for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ++ViewIndex)
	{
		FViewInfo& View = Views[ViewIndex];

		// Halton iteration
		uint32 IterationCount = FMath::Max(SamplesPerPixel, 1u);
		uint32 SequenceCount = 1;
		uint32 DimensionCount = 3;
		FHaltonSequenceIteration HaltonSequenceIteration(Scene->HaltonSequence, IterationCount, SequenceCount, DimensionCount, View.ViewState ? (View.ViewState->FrameIndex % 1024) : 0);

		FHaltonIteration HaltonIteration;
		InitializeHaltonSequenceIteration(HaltonSequenceIteration, HaltonIteration);

		//  View-dependent parameters
		PassParameters->TLAS = View.RayTracingScene.RayTracingSceneRHI->GetShaderResourceView();
		PassParameters->ViewUniformBuffer = View.ViewUniformBuffer;
		PassParameters->HaltonIteration = CreateUniformBufferImmediate(HaltonIteration, EUniformBufferUsage::UniformBuffer_SingleDraw);

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
		if (GRayTracingSkyLightDenoiser != 0 && SamplesPerPixel > 0)
		{
			const IScreenSpaceDenoiser* DefaultDenoiser = IScreenSpaceDenoiser::GetDefaultDenoiser();
			const IScreenSpaceDenoiser* DenoiserToUse = DefaultDenoiser;// GRayTracingGlobalIlluminationDenoiser == 1 ? DefaultDenoiser : GScreenSpaceDenoiser;

			IScreenSpaceDenoiser::FDiffuseIndirectInputs DenoiserInputs;
			DenoiserInputs.Color = SkyLightTexture;
			DenoiserInputs.RayHitDistance = RayDistanceTexture;

			{
				IScreenSpaceDenoiser::FAmbientOcclusionRayTracingConfig RayTracingConfig;
				RayTracingConfig.ResolutionFraction = 1.0;
				RayTracingConfig.RayCountPerPixel = SamplesPerPixel;

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
		PassParameters->RenderTargets[0] = FRenderTargetBinding(GraphBuilder.RegisterExternalTexture(SceneContext.GetSceneColor()), ERenderTargetLoadAction::ELoad, ERenderTargetStoreAction::EStore);
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
			GraphicsPSOInit.BlendState = TStaticBlendState<CW_RGBA, BO_Add, BF_One, BF_One, BO_Add, BF_One, BF_One>::GetRHI();
			GraphicsPSOInit.RasterizerState = TStaticRasterizerState<FM_Solid, CM_None>::GetRHI();
			GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();

			GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GFilterVertexDeclaration.VertexDeclarationRHI;
			GraphicsPSOInit.BoundShaderState.VertexShaderRHI = GETSAFERHISHADER_VERTEX(*VertexShader);
			GraphicsPSOInit.BoundShaderState.PixelShaderRHI = GETSAFERHISHADER_PIXEL(*PixelShader);
			GraphicsPSOInit.PrimitiveType = PT_TriangleList;
			SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);

			SetShaderParameters(RHICmdList, *PixelShader, PixelShader->GetPixelShader(), *PassParameters);

			RHICmdList.SetViewport(View.ViewRect.Min.X, View.ViewRect.Min.Y, 0.0f, View.ViewRect.Max.X, View.ViewRect.Max.Y, 1.0f);

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
