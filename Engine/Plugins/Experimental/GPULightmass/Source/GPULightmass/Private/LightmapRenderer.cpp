// Copyright Epic Games, Inc. All Rights Reserved.

#include "LightmapRenderer.h"
#include "GPULightmassModule.h"
#include "GPULightmassCommon.h"
#include "SceneRendering.h"
#include "Scene/Scene.h"
#include "Scene/StaticMesh.h"
#include "LightmapGBuffer.h"
#include "LightmapRayTracing.h"
#include "ClearQuad.h"
#include "RayTracing/RayTracingMaterialHitShaders.h"
#include "Async/ParallelFor.h"
#include "Async/Async.h"
#include "Rendering/SkyLightImportanceSampling.h"
#include "LightmapPreviewVirtualTexture.h"
#include "RHIGPUReadback.h"
#include "LightmapStorage.h"
#include "LevelEditorViewport.h"
#include "Editor.h"
#include "CanvasTypes.h"
#include "LightmapDenoising.h"
#include "EngineModule.h"
#include "PostProcess/PostProcessing.h"
#include "RayTracingGeometryManager.h"
#include "ScreenPass.h"

class FCopyConvergedLightmapTilesCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FCopyConvergedLightmapTilesCS)
	SHADER_USE_PARAMETER_STRUCT(FCopyConvergedLightmapTilesCS, FGlobalShader)

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return RHISupportsRayTracingShaders(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		OutEnvironment.SetDefine(TEXT("GPreviewLightmapPhysicalTileSize"), GPreviewLightmapPhysicalTileSize);
		OutEnvironment.CompilerFlags.Add(CFLAG_ForceDXC);
	}

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(int32, NumBatchedTiles)
		SHADER_PARAMETER(uint32, StagingPoolSizeX)
		SHADER_PARAMETER_SRV(StructuredBuffer<FGPUTileDescription>, BatchedTiles)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, IrradianceAndSampleCount)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, SHDirectionality)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, SHCorrectionAndStationarySkyLightBentNormal)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, ShadowMask)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, ShadowMaskSampleCount)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, StagingHQLayer0)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, StagingHQLayer1)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, StagingShadowMask)
	END_SHADER_PARAMETER_STRUCT()
};

class FUploadConvergedLightmapTilesCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FUploadConvergedLightmapTilesCS)
	SHADER_USE_PARAMETER_STRUCT(FUploadConvergedLightmapTilesCS, FGlobalShader)

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return RHISupportsRayTracingShaders(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		OutEnvironment.SetDefine(TEXT("GPreviewLightmapPhysicalTileSize"), GPreviewLightmapPhysicalTileSize);
		OutEnvironment.CompilerFlags.Add(CFLAG_ForceDXC);
	}

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(int32, NumBatchedTiles)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, SrcTexture)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, DstTexture)
		SHADER_PARAMETER_SRV(StructuredBuffer<int2>, SrcTilePositions)
		SHADER_PARAMETER_SRV(StructuredBuffer<int2>, DstTilePositions)
	END_SHADER_PARAMETER_STRUCT()
};

class FSelectiveLightmapOutputCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FSelectiveLightmapOutputCS)
	SHADER_USE_PARAMETER_STRUCT(FSelectiveLightmapOutputCS, FGlobalShader)

	class FOutputLayerDim : SHADER_PERMUTATION_INT("DIM_OUTPUT_LAYER", 3);
	class FDrawProgressBars : SHADER_PERMUTATION_BOOL("DRAW_PROGRESS_BARS");
	using FPermutationDomain = TShaderPermutationDomain<FOutputLayerDim, FDrawProgressBars>;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return RHISupportsRayTracingShaders(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		OutEnvironment.SetDefine(TEXT("GPreviewLightmapPhysicalTileSize"), GPreviewLightmapPhysicalTileSize);
		OutEnvironment.CompilerFlags.Add(CFLAG_ForceDXC);
	}

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(int32, NumBatchedTiles)
		SHADER_PARAMETER(int32, NumTotalSamples)
		SHADER_PARAMETER(int32, NumRayGuidingTrialSamples)
		SHADER_PARAMETER_SRV(StructuredBuffer<FGPUTileDescription>, BatchedTiles)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutputTileAtlas)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, IrradianceAndSampleCount)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, SHDirectionality)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, ShadowMask)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, ShadowMaskSampleCount)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, SHCorrectionAndStationarySkyLightBentNormal)
	END_SHADER_PARAMETER_STRUCT()
};

class FMultiTileClearCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FMultiTileClearCS)
	SHADER_USE_PARAMETER_STRUCT(FMultiTileClearCS, FGlobalShader)

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return RHISupportsRayTracingShaders(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		OutEnvironment.SetDefine(TEXT("GPreviewLightmapPhysicalTileSize"), GPreviewLightmapPhysicalTileSize);
		OutEnvironment.CompilerFlags.Add(CFLAG_ForceDXC);
	}

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(int32, NumTiles)
		SHADER_PARAMETER(int32, TileSize)
		SHADER_PARAMETER_SRV(Buffer<int2>, TilePositions)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, TilePool)
	END_SHADER_PARAMETER_STRUCT()
};

IMPLEMENT_GLOBAL_SHADER(FCopyConvergedLightmapTilesCS, "/Plugin/GPULightmass/Private/LightmapBufferClear.usf", "CopyConvergedLightmapTilesCS", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FUploadConvergedLightmapTilesCS, "/Plugin/GPULightmass/Private/LightmapBufferClear.usf", "UploadConvergedLightmapTilesCS", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FSelectiveLightmapOutputCS, "/Plugin/GPULightmass/Private/LightmapOutput.usf", "SelectiveLightmapOutputCS", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FMultiTileClearCS, "/Plugin/GPULightmass/Private/LightmapBufferClear.usf", "MultiTileClearCS", SF_Compute);

struct FGPUTileDescription
{
	FIntPoint LightmapSize;
	FIntPoint VirtualTilePosition;
	FIntPoint WorkingSetPosition;
	FIntPoint ScratchPosition;
	FIntPoint OutputLayer0Position;
	FIntPoint OutputLayer1Position;
	FIntPoint OutputLayer2Position;
	int32 FrameIndex;
	int32 RenderPassIndex;
};

struct FGPUBatchedTileRequests
{
	FBufferRHIRef BatchedTilesBuffer;
	FShaderResourceViewRHIRef BatchedTilesSRV;
	TResourceArray<FGPUTileDescription> BatchedTilesDesc;
};

#if RHI_RAYTRACING
void SetupPathTracingLightParameters(const GPULightmass::FLightSceneRenderState& LightScene, FRDGBuilder& GraphBuilder, FRDGBufferSRV** OutLightBuffer, unsigned* OutLightCount)
{
	const uint32 MaxLightCount = RAY_TRACING_LIGHT_COUNT_MAXIMUM;
	FPathTracingLight Lights[RAY_TRACING_LIGHT_COUNT_MAXIMUM] = {};

	unsigned LightCount = 0;

	if (LightScene.SkyLight.IsSet())
	{
		FPathTracingLight& DestLight = Lights[LightCount++];
		DestLight.Color = FVector(LightScene.SkyLight->Color);
		DestLight.Flags = PATHTRACER_FLAG_TRANSMISSION_MASK;
		DestLight.Flags |= PATHTRACER_FLAG_LIGHTING_CHANNEL_MASK;
		bool SkyLightIsStationary = LightScene.SkyLight->bStationary;
		DestLight.Flags |= SkyLightIsStationary ? PATHTRACER_FLAG_STATIONARY_MASK : 0;
		DestLight.Flags |= PATHTRACING_LIGHT_SKY;
	}

	for (auto Light : LightScene.DirectionalLights.Elements)
	{
		if (LightCount < MaxLightCount)
		{
			FPathTracingLight& DestLight = Lights[LightCount++];

			DestLight.Normal = -Light.Direction;
			DestLight.Color = FVector(Light.Color);
			DestLight.Dimensions = FVector(
				FMath::Sin(0.5f * FMath::DegreesToRadians(Light.LightSourceAngle)),
				FMath::Sin(0.5f * FMath::DegreesToRadians(Light.LightSourceSoftAngle)),
				0.0f);
			DestLight.Attenuation = 1.0;
			DestLight.IESTextureSlice = -1;

			DestLight.Flags = PATHTRACER_FLAG_TRANSMISSION_MASK;
			DestLight.Flags |= PATHTRACER_FLAG_LIGHTING_CHANNEL_MASK;
			DestLight.Flags |= Light.bStationary ? PATHTRACER_FLAG_STATIONARY_MASK : 0;
			DestLight.Flags |= PATHTRACING_LIGHT_DIRECTIONAL;
		}
	}

	for (auto Light : LightScene.PointLights.Elements)
	{
		if (LightCount < MaxLightCount)
		{
			FPathTracingLight& DestLight = Lights[LightCount++];

			DestLight.Position = Light.Position;
			DestLight.Color = FVector(Light.Color);
			DestLight.Normal = Light.Direction;
			DestLight.dPdu = FVector::CrossProduct(Light.Tangent, Light.Direction);
			DestLight.dPdv = Light.Tangent;

			DestLight.Dimensions = FVector(Light.SourceRadius, Light.SourceSoftRadius, Light.SourceLength);
			DestLight.Attenuation = 1.0f / Light.AttenuationRadius;
			DestLight.FalloffExponent = Light.FalloffExponent;
			DestLight.IESTextureSlice = -1;

			DestLight.Flags = PATHTRACER_FLAG_TRANSMISSION_MASK;
			DestLight.Flags |= PATHTRACER_FLAG_LIGHTING_CHANNEL_MASK;
			DestLight.Flags |= Light.IsInverseSquared ? 0 : PATHTRACER_FLAG_NON_INVERSE_SQUARE_FALLOFF_MASK;
			DestLight.Flags |= Light.bStationary ? PATHTRACER_FLAG_STATIONARY_MASK : 0;
			DestLight.Flags |= PATHTRACING_LIGHT_POINT;
		}
	}

	for (auto Light : LightScene.SpotLights.Elements)
	{
		if (LightCount < MaxLightCount)
		{
			FPathTracingLight& DestLight = Lights[LightCount++];

			DestLight.Position = Light.Position;
			DestLight.Normal = Light.Direction;
			DestLight.dPdu = FVector::CrossProduct(Light.Tangent, Light.Direction);
			DestLight.dPdv = Light.Tangent;
			DestLight.Color = FVector(Light.Color);
			DestLight.Dimensions = FVector(Light.SourceRadius, Light.SourceSoftRadius, Light.SourceLength);
			DestLight.Shaping = Light.SpotAngles;
			DestLight.Attenuation = 1.0f / Light.AttenuationRadius;
			DestLight.FalloffExponent = Light.FalloffExponent;
			DestLight.IESTextureSlice = -1;

			DestLight.Flags = PATHTRACER_FLAG_TRANSMISSION_MASK;
			DestLight.Flags |= PATHTRACER_FLAG_LIGHTING_CHANNEL_MASK;
			DestLight.Flags |= Light.IsInverseSquared ? 0 : PATHTRACER_FLAG_NON_INVERSE_SQUARE_FALLOFF_MASK;
			DestLight.Flags |= Light.bStationary ? PATHTRACER_FLAG_STATIONARY_MASK : 0;
			DestLight.Flags |= PATHTRACING_LIGHT_SPOT;
		}
	}

	for (auto Light : LightScene.RectLights.Elements)
	{
		if (LightCount < MaxLightCount)
		{
			FPathTracingLight& DestLight = Lights[LightCount++];

			DestLight.Position = Light.Position;
			DestLight.Normal = Light.Direction;
			DestLight.dPdu = FVector::CrossProduct(Light.Tangent, -Light.Direction);
			DestLight.dPdv = Light.Tangent;

			FLinearColor LightColor = Light.Color;
			LightColor /= 0.5f * Light.SourceWidth * Light.SourceHeight;
			DestLight.Color = FVector(LightColor);

			DestLight.Dimensions = FVector(Light.SourceWidth, Light.SourceHeight, 0.0f);
			DestLight.Attenuation = 1.0f / Light.AttenuationRadius;
			DestLight.Shaping = FVector2D(FMath::Cos(FMath::DegreesToRadians(Light.BarnDoorAngle)), Light.BarnDoorLength);

			DestLight.IESTextureSlice = -1;

			DestLight.Flags = PATHTRACER_FLAG_TRANSMISSION_MASK;
			DestLight.Flags |= PATHTRACER_FLAG_LIGHTING_CHANNEL_MASK;
			DestLight.Flags |= Light.bStationary ? PATHTRACER_FLAG_STATIONARY_MASK : 0;
			DestLight.Flags |= PATHTRACING_LIGHT_RECT;
		}
	}

	{
		// Upload the buffer of lights to the GPU
		size_t DataSize = sizeof(FPathTracingLight) * FMath::Max(LightCount, 1u);
		*OutLightBuffer = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(CreateStructuredBuffer(GraphBuilder, TEXT("PathTracingLightsBuffer"), sizeof(FPathTracingLight), FMath::Max(LightCount, 1u), Lights, DataSize)));
		*OutLightCount = LightCount;
	}
}

FSkyLightData SetupSkyLightParameters(const GPULightmass::FLightSceneRenderState& LightScene)
{
	FSkyLightData SkyLightData;
	// Check if parameters should be set based on if the sky light's texture has been processed and if its mip tree has been built yet
	if (LightScene.SkyLight.IsSet())
	{
		check(LightScene.SkyLight->ProcessedTexture);
		check(LightScene.SkyLight->ImportanceSamplingData->bIsValid);

		SkyLightData.SamplesPerPixel = 1;
		SkyLightData.SamplingStopLevel = 0;
		SkyLightData.MaxRayDistance = 1.0e7;
		SkyLightData.MaxNormalBias = 0.1f;
		SkyLightData.MaxShadowThickness = 1.0e3;

		SkyLightData.Color = FVector(LightScene.SkyLight->Color);
		SkyLightData.Texture = LightScene.SkyLight->ProcessedTexture;
		SkyLightData.TextureDimensions = FIntVector(LightScene.SkyLight->TextureDimensions.X, LightScene.SkyLight->TextureDimensions.Y, 1);
		SkyLightData.TextureSampler = LightScene.SkyLight->ProcessedTextureSampler;
		SkyLightData.MipDimensions = LightScene.SkyLight->ImportanceSamplingData->MipDimensions;

		SkyLightData.MipTreePosX = LightScene.SkyLight->ImportanceSamplingData->MipTreePosX.SRV;
		SkyLightData.MipTreeNegX = LightScene.SkyLight->ImportanceSamplingData->MipTreeNegX.SRV;
		SkyLightData.MipTreePosY = LightScene.SkyLight->ImportanceSamplingData->MipTreePosY.SRV;
		SkyLightData.MipTreeNegY = LightScene.SkyLight->ImportanceSamplingData->MipTreeNegY.SRV;
		SkyLightData.MipTreePosZ = LightScene.SkyLight->ImportanceSamplingData->MipTreePosZ.SRV;
		SkyLightData.MipTreeNegZ = LightScene.SkyLight->ImportanceSamplingData->MipTreeNegZ.SRV;

		SkyLightData.MipTreePdfPosX = LightScene.SkyLight->ImportanceSamplingData->MipTreePdfPosX.SRV;
		SkyLightData.MipTreePdfNegX = LightScene.SkyLight->ImportanceSamplingData->MipTreePdfNegX.SRV;
		SkyLightData.MipTreePdfPosY = LightScene.SkyLight->ImportanceSamplingData->MipTreePdfPosY.SRV;
		SkyLightData.MipTreePdfNegY = LightScene.SkyLight->ImportanceSamplingData->MipTreePdfNegY.SRV;
		SkyLightData.MipTreePdfPosZ = LightScene.SkyLight->ImportanceSamplingData->MipTreePdfPosZ.SRV;
		SkyLightData.MipTreePdfNegZ = LightScene.SkyLight->ImportanceSamplingData->MipTreePdfNegZ.SRV;
		SkyLightData.SolidAnglePdf = LightScene.SkyLight->ImportanceSamplingData->SolidAnglePdf.SRV;
	}
	else
	{
		SkyLightData.SamplesPerPixel = -1;
		SkyLightData.SamplingStopLevel = 0;
		SkyLightData.MaxRayDistance = 0.0f;
		SkyLightData.MaxNormalBias = 0.0f;
		SkyLightData.MaxShadowThickness = 0.0f;

		SkyLightData.Color = FVector(0.0);
		SkyLightData.Texture = GBlackTextureCube->TextureRHI;
		SkyLightData.TextureSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
		SkyLightData.MipDimensions = FIntVector(0);

		SkyLightData.MipTreePosX = GBlackTextureWithSRV->ShaderResourceViewRHI;
		SkyLightData.MipTreeNegX = GBlackTextureWithSRV->ShaderResourceViewRHI;
		SkyLightData.MipTreePosY = GBlackTextureWithSRV->ShaderResourceViewRHI;
		SkyLightData.MipTreeNegY = GBlackTextureWithSRV->ShaderResourceViewRHI;
		SkyLightData.MipTreePosZ = GBlackTextureWithSRV->ShaderResourceViewRHI;
		SkyLightData.MipTreeNegZ = GBlackTextureWithSRV->ShaderResourceViewRHI;

		SkyLightData.MipTreePdfPosX = GBlackTextureWithSRV->ShaderResourceViewRHI;
		SkyLightData.MipTreePdfNegX = GBlackTextureWithSRV->ShaderResourceViewRHI;
		SkyLightData.MipTreePdfPosY = GBlackTextureWithSRV->ShaderResourceViewRHI;
		SkyLightData.MipTreePdfNegY = GBlackTextureWithSRV->ShaderResourceViewRHI;
		SkyLightData.MipTreePdfPosZ = GBlackTextureWithSRV->ShaderResourceViewRHI;
		SkyLightData.MipTreePdfNegZ = GBlackTextureWithSRV->ShaderResourceViewRHI;
		SkyLightData.SolidAnglePdf = GBlackTextureWithSRV->ShaderResourceViewRHI;
	}

	return SkyLightData;
}
#endif
namespace GPULightmass
{

FLightmapRenderer::FLightmapRenderer(FSceneRenderState* InScene)
	: Scene(InScene)
	, LightmapTilePoolGPU(FIntPoint(Scene->Settings->LightmapTilePoolSize))
{
	bUseFirstBounceRayGuiding = Scene->Settings->bUseFirstBounceRayGuiding;
	if (bUseFirstBounceRayGuiding)
	{
		NumFirstBounceRayGuidingTrialSamples = Scene->Settings->FirstBounceRayGuidingTrialSamples;
	}

	if (!bUseFirstBounceRayGuiding)
	{
		LightmapTilePoolGPU.Initialize(
			{
				{ PF_A32B32G32R32F, FIntPoint(GPreviewLightmapPhysicalTileSize) }, // IrradianceAndSampleCount
				{ PF_A32B32G32R32F, FIntPoint(GPreviewLightmapPhysicalTileSize) }, // SHDirectionality
				{ PF_A32B32G32R32F, FIntPoint(GPreviewLightmapPhysicalTileSize) }, // ShadowMask
				{ PF_A32B32G32R32F, FIntPoint(GPreviewLightmapPhysicalTileSize) }, // ShadowMaskSampleCount
				{ PF_A32B32G32R32F, FIntPoint(GPreviewLightmapPhysicalTileSize) }, // SHCorrectionAndStationarySkyLightBentNormal
			});
	}
	else
	{
		LightmapTilePoolGPU.Initialize(
			{
				{ PF_A32B32G32R32F, FIntPoint(GPreviewLightmapPhysicalTileSize) }, // IrradianceAndSampleCount
				{ PF_A32B32G32R32F, FIntPoint(GPreviewLightmapPhysicalTileSize) }, // SHDirectionality
				{ PF_A32B32G32R32F, FIntPoint(GPreviewLightmapPhysicalTileSize) }, // ShadowMask
				{ PF_A32B32G32R32F, FIntPoint(GPreviewLightmapPhysicalTileSize) }, // ShadowMaskSampleCount
				{ PF_A32B32G32R32F, FIntPoint(GPreviewLightmapPhysicalTileSize) }, // SHCorrectionAndStationarySkyLightBentNormal
				{ PF_R32_UINT, FIntPoint(128) }, // RayGuidingLuminance
				{ PF_R32_FLOAT, FIntPoint(128) }, // RayGuidingCDFX
				{ PF_R32_FLOAT, FIntPoint(32) }, // RayGuidingCDFY
			});
	}

	bDenoiseDuringInteractiveBake = Scene->Settings->DenoisingOptions == EGPULightmassDenoisingOptions::DuringInteractivePreview;
	bOnlyBakeWhatYouSee = Scene->Settings->Mode == EGPULightmassMode::BakeWhatYouSee;
	DenoisingThreadPool = FQueuedThreadPool::Allocate();
	DenoisingThreadPool->Create(1, 64 * 1024 * 1024);

	if (bOnlyBakeWhatYouSee)
	{
		TilesVisibleLastFewFrames.AddDefaulted(60);
	}

	if (Scene->Settings->bVisualizeIrradianceCache)
	{
		IrradianceCacheVisualizationDelegateHandle = GetRendererModule().RegisterPostOpaqueRenderDelegate(FPostOpaqueRenderDelegate::CreateRaw(this, &FLightmapRenderer::RenderIrradianceCacheVisualization));
	}
}

FLightmapRenderer::~FLightmapRenderer()
{
	delete DenoisingThreadPool;

	GetRendererModule().RemovePostOpaqueRenderDelegate(IrradianceCacheVisualizationDelegateHandle);
}

void FLightmapRenderer::AddRequest(FLightmapTileRequest TileRequest)
{
	PendingTileRequests.AddUnique(TileRequest);
}

void FCachedRayTracingSceneData::SetupViewUniformBufferFromSceneRenderState(FSceneRenderState& Scene)
{
	TResourceArray<FPrimitiveSceneShaderData> PrimitiveSceneData;
	TResourceArray<FLightmapSceneShaderData> LightmapSceneData;
	TResourceArray<FInstanceSceneShaderData> InstanceSceneData;

	PrimitiveSceneData.AddZeroed(Scene.StaticMeshInstanceRenderStates.Elements.Num());
	InstanceSceneData.AddZeroed(Scene.StaticMeshInstanceRenderStates.Elements.Num());

	TArray<int32> LightmapSceneDataStartOffsets;
	LightmapSceneDataStartOffsets.AddZeroed(Scene.StaticMeshInstanceRenderStates.Elements.Num());

	{
		TRACE_CPUPROFILER_EVENT_SCOPE(ComputePrefixSum);

		int32 ConservativeLightmapEntriesNum = 0;

		for (int32 InstanceIndex = 0; InstanceIndex < Scene.StaticMeshInstanceRenderStates.Elements.Num(); InstanceIndex++)
		{
			FStaticMeshInstanceRenderState& Instance = Scene.StaticMeshInstanceRenderStates.Elements[InstanceIndex];
			LightmapSceneDataStartOffsets[InstanceIndex] = ConservativeLightmapEntriesNum;
			ConservativeLightmapEntriesNum += Instance.LODLightmapRenderStates.Num();
		}

		for (int32 InstanceGroupIndex = 0; InstanceGroupIndex < Scene.InstanceGroupRenderStates.Elements.Num(); InstanceGroupIndex++)
		{
			FInstanceGroupRenderState& InstanceGroup = Scene.InstanceGroupRenderStates.Elements[InstanceGroupIndex];
			LightmapSceneDataStartOffsets[InstanceGroupIndex] = ConservativeLightmapEntriesNum;
			ConservativeLightmapEntriesNum += InstanceGroup.LODLightmapRenderStates.Num();
		}

		LightmapSceneData.AddZeroed(ConservativeLightmapEntriesNum);
	}

	{
		TRACE_CPUPROFILER_EVENT_SCOPE(SetupGPUScene);

		ParallelFor(Scene.StaticMeshInstanceRenderStates.Elements.Num(),
			[
				this,
				&Scene,
				&PrimitiveSceneData,
				&LightmapSceneData,
				&LightmapSceneDataStartOffsets,
				&InstanceSceneData
			](int32 InstanceIndex = 0)
			{
				FStaticMeshInstanceRenderState& Instance = Scene.StaticMeshInstanceRenderStates.Elements[InstanceIndex];

				FPrimitiveUniformShaderParameters PrimitiveUniformShaderParameters = Instance.PrimitiveUniformShaderParameters;
				PrimitiveUniformShaderParameters.LightmapDataIndex = LightmapSceneDataStartOffsets[InstanceIndex];
				PrimitiveUniformShaderParameters.LightmapUVIndex = 0; // TODO: LightmapUVIndex
				PrimitiveUniformShaderParameters.InstanceDataOffset = InstanceIndex;
				PrimitiveUniformShaderParameters.NumInstanceDataEntries = 1;
				PrimitiveSceneData[InstanceIndex] = FPrimitiveSceneShaderData(PrimitiveUniformShaderParameters);
				InstanceSceneData[InstanceIndex] = FInstanceSceneShaderData(
					GetInstanceUniformShaderParameters(
						PrimitiveUniformShaderParameters.LocalToWorld, 
						PrimitiveUniformShaderParameters.PreviousLocalToWorld,
						(PrimitiveUniformShaderParameters.LocalObjectBoundsMin + PrimitiveUniformShaderParameters.LocalObjectBoundsMax) * 0.5f,
						PrimitiveUniformShaderParameters.LocalObjectBoundsMax - PrimitiveUniformShaderParameters.LocalObjectBoundsMin,
						PrimitiveUniformShaderParameters.NonUniformScale, PrimitiveUniformShaderParameters.InvNonUniformScaleAndDeterminantSign,
						FVector4(ForceInitToZero),
						FNaniteInfo(),
						InstanceIndex,
						0xFFFFFFFFu,
						0.0f,
						true
					));

				for (int32 LODIndex = 0; LODIndex < Instance.LODLightmapRenderStates.Num(); LODIndex++)
				{
					FPrecomputedLightingUniformParameters LightmapParams;
					GetDefaultPrecomputedLightingParameters(LightmapParams);

					if (Instance.LODLightmapRenderStates[LODIndex].IsValid())
					{
						LightmapParams.LightmapVTPackedPageTableUniform[0] = Instance.LODLightmapRenderStates[LODIndex]->LightmapVTPackedPageTableUniform[0];
						for (uint32 LayerIndex = 0u; LayerIndex < 5u; ++LayerIndex)
						{
							LightmapParams.LightmapVTPackedUniform[LayerIndex] = Instance.LODLightmapRenderStates[LODIndex]->LightmapVTPackedUniform[LayerIndex];
						}

						LightmapParams.LightMapCoordinateScaleBias = Instance.LODLightmapRenderStates[LODIndex]->LightmapCoordinateScaleBias;
					}

					LightmapSceneData[LightmapSceneDataStartOffsets[InstanceIndex] + LODIndex] = FLightmapSceneShaderData(LightmapParams);
				}
			});

		for (int32 InstanceGroupIndex = 0; InstanceGroupIndex < Scene.InstanceGroupRenderStates.Elements.Num(); InstanceGroupIndex++)
		{
			FInstanceGroupRenderState& InstanceGroup = Scene.InstanceGroupRenderStates.Elements[InstanceGroupIndex];

			int32 NumInstancesThisGroup = (int32)InstanceGroup.InstancedRenderData->PerInstanceRenderData->InstanceBuffer.GetNumInstances();

			FPrimitiveUniformShaderParameters PrimitiveUniformShaderParameters = GetPrimitiveUniformShaderParameters(
				InstanceGroup.LocalToWorld,
				InstanceGroup.LocalToWorld,
				InstanceGroup.ActorPosition,
				InstanceGroup.WorldBounds,
				InstanceGroup.LocalBounds,
				InstanceGroup.LocalBounds,
				false,
				false,
				false,
				false,
				false,
				false,
				0b111,
				LightmapSceneDataStartOffsets[Scene.StaticMeshInstanceRenderStates.Elements.Num() + InstanceGroupIndex],
				INDEX_NONE,
				INDEX_NONE,
				/* bOutputVelocity = */ false,
				nullptr,
				/* bCastContactShadow = */ true,
				InstanceSceneData.Num(),
				NumInstancesThisGroup,
				/* bCastShadow = */ true
			);

			for (int32 InstanceIdx = 0; InstanceIdx < NumInstancesThisGroup; InstanceIdx++)
			{
				FMatrix Transform;
				InstanceGroup.InstancedRenderData->PerInstanceRenderData->InstanceBuffer.GetInstanceTransform(InstanceIdx, Transform);
				Transform.M[3][3] = 1.0f;
				FMatrix InstanceTransform = Transform * InstanceGroup.LocalToWorld;

				FPrimitiveInstance Instance;
				Instance.PrimitiveId = PrimitiveSceneData.Num();
				Instance.InstanceToLocal = Transform;
				// GPUCULL_TODO: not sure this is needed either - might be better to delegate to later anyway since inverse can then be threaded, plus some platforms might not need it at all.
				Instance.LocalToInstance = Instance.InstanceToLocal.Inverse();
				// Filled in during GPU Scene update...
				Instance.LocalToWorld = InstanceTransform;
				Instance.PrevLocalToWorld = InstanceTransform;
				Instance.RenderBounds = InstanceGroup.RenderData->Bounds;
				Instance.LocalBounds = Instance.RenderBounds.TransformBy(Instance.InstanceToLocal);

				InstanceSceneData.Add(FInstanceSceneShaderData(Instance));
			}

			InstanceGroup.UniformBuffer = TUniformBufferRef<FPrimitiveUniformShaderParameters>::CreateUniformBufferImmediate(PrimitiveUniformShaderParameters, UniformBuffer_MultiFrame);
			PrimitiveSceneData.Add(FPrimitiveSceneShaderData(PrimitiveUniformShaderParameters));
		}
	}

	{
		TRACE_CPUPROFILER_EVENT_SCOPE(SetupViewBuffers);

		{
			TRACE_CPUPROFILER_EVENT_SCOPE(PrimitiveSceneData);

			FRHIResourceCreateInfo CreateInfo(TEXT("PrimitiveSceneDataBuffer"), &PrimitiveSceneData);
			if (PrimitiveSceneData.GetResourceDataSize() == 0)
			{
				PrimitiveSceneData.Add(FPrimitiveSceneShaderData(GetIdentityPrimitiveParameters()));
			}

			PrimitiveSceneDataBufferRHI = RHICreateStructuredBuffer(sizeof(FVector4), PrimitiveSceneData.GetResourceDataSize(), BUF_Static | BUF_ShaderResource, CreateInfo);
			PrimitiveSceneDataBufferSRV = RHICreateShaderResourceView(PrimitiveSceneDataBufferRHI);
		}

		{
			TRACE_CPUPROFILER_EVENT_SCOPE(LightmapSceneData);

			FRHIResourceCreateInfo CreateInfo(TEXT("LightmapSceneDataBuffer"), &LightmapSceneData);
			if (LightmapSceneData.GetResourceDataSize() == 0)
			{
				LightmapSceneData.Add(FLightmapSceneShaderData());
			}

			LightmapSceneDataBufferRHI = RHICreateStructuredBuffer(sizeof(FVector4), LightmapSceneData.GetResourceDataSize(), BUF_Static | BUF_ShaderResource, CreateInfo);
			LightmapSceneDataBufferSRV = RHICreateShaderResourceView(LightmapSceneDataBufferRHI);
		}

		{
			TRACE_CPUPROFILER_EVENT_SCOPE(InstanceSceneData);

			TResourceArray<FVector4> InstanceSceneDataSOA;
			InstanceSceneDataSOA.AddZeroed(FInstanceSceneShaderData::InstanceDataStrideInFloat4s * InstanceSceneData.Num());
			for (int32 ArrayIndex = 0; ArrayIndex < FInstanceSceneShaderData::InstanceDataStrideInFloat4s; ArrayIndex++)
			{
				for (int32 DataIndex = 0; DataIndex < InstanceSceneData.Num(); DataIndex++)
				{
					InstanceSceneDataSOA[ArrayIndex * InstanceSceneData.Num() + DataIndex] = InstanceSceneData[DataIndex].Data[ArrayIndex];
				}
			}

			FRHIResourceCreateInfo CreateInfo(TEXT("InstanceSceneDataBuffer"), &InstanceSceneDataSOA);
			if (InstanceSceneDataSOA.GetResourceDataSize() == 0)
			{
				InstanceSceneDataSOA.AddZeroed(FInstanceSceneShaderData::InstanceDataStrideInFloat4s);
			}

			InstanceSceneDataBufferRHI = RHICreateStructuredBuffer(sizeof(FVector4), InstanceSceneDataSOA.GetResourceDataSize(), BUF_Static | BUF_ShaderResource, CreateInfo);
			InstanceSceneDataBufferSRV = RHICreateShaderResourceView(InstanceSceneDataBufferRHI);
			InstanceSceneDataSOAStride = InstanceSceneData.Num();
		}

		FViewUniformShaderParameters ViewUniformBufferParameters;
		CachedViewUniformBuffer = TUniformBufferRef<FViewUniformShaderParameters>::CreateUniformBufferImmediate(ViewUniformBufferParameters, UniformBuffer_MultiFrame, EUniformBufferValidation::None);
	}
}

void FCachedRayTracingSceneData::SetupFromSceneRenderState(FSceneRenderState& Scene)
{
#if RHI_RAYTRACING
	FMaterialRenderProxy::UpdateDeferredCachedUniformExpressions();

	{
		RayTracingGeometryInstances.Reserve(Scene.StaticMeshInstanceRenderStates.Elements.Num());

		for (int32 StaticMeshIndex = 0; StaticMeshIndex < Scene.StaticMeshInstanceRenderStates.Elements.Num();  StaticMeshIndex++)
		{
			FStaticMeshInstanceRenderState& Instance = Scene.StaticMeshInstanceRenderStates.Elements[StaticMeshIndex];

			TArray<FMeshBatch> MeshBatches = Instance.GetMeshBatchesForGBufferRendering(0);

			bool bAllSegmentsUnlit = true;
			bool bAllSegmentsOpaque = true;

			for (int32 SegmentIndex = 0; SegmentIndex < MeshBatches.Num(); SegmentIndex++)
			{
				const FMaterialRenderProxy* FallbackMaterialRenderProxyPtr = nullptr;
				const FMaterial& Material = MeshBatches[SegmentIndex].MaterialRenderProxy->GetMaterialWithFallback(GMaxRHIFeatureLevel, FallbackMaterialRenderProxyPtr);

				bAllSegmentsUnlit &= Material.GetShadingModels().HasOnlyShadingModel(MSM_Unlit) || !MeshBatches[SegmentIndex].CastShadow;
				bAllSegmentsOpaque &= Material.GetBlendMode() == EBlendMode::BLEND_Opaque;
			}

			if (!bAllSegmentsUnlit)
			{
				int32 InstanceIndex = RayTracingGeometryInstances.AddDefaulted(1);
				FRayTracingGeometryInstance& RayTracingInstance = RayTracingGeometryInstances[InstanceIndex];
				RayTracingInstance.GeometryRHI = Instance.RenderData->LODResources[0].RayTracingGeometry.RayTracingGeometryRHI;
				RayTracingInstance.Transforms.Add(Instance.LocalToWorld);
				RayTracingInstance.NumTransforms = 1;
				RayTracingInstance.UserData.Add((uint32)StaticMeshIndex);
				RayTracingInstance.Mask = 0xFF;
				RayTracingInstance.bForceOpaque = bAllSegmentsOpaque;

				for (int32 SegmentIndex = 0; SegmentIndex < MeshBatches.Num(); SegmentIndex++)
				{
					FFullyCachedRayTracingMeshCommandContext CommandContext(MeshCommandStorage, VisibleRayTracingMeshCommands, SegmentIndex, InstanceIndex);
					FMeshPassProcessorRenderState PassDrawRenderState(CachedViewUniformBuffer, CachedViewUniformBuffer);
					FLightmapRayTracingMeshProcessor RayTracingMeshProcessor(&CommandContext, PassDrawRenderState);

					RayTracingMeshProcessor.AddMeshBatch(MeshBatches[SegmentIndex], 1, nullptr);
				}
			}
		}

		RayTracingGeometryInstances.Reserve(Scene.InstanceGroupRenderStates.Elements.Num());

		{
			for (int32 InstanceGroupIndex = 0; InstanceGroupIndex < Scene.InstanceGroupRenderStates.Elements.Num(); InstanceGroupIndex++)
			{
				FInstanceGroupRenderState& InstanceGroup = Scene.InstanceGroupRenderStates.Elements[InstanceGroupIndex];

				TArray<FMeshBatch> MeshBatches = InstanceGroup.GetMeshBatchesForGBufferRendering(0, FTileVirtualCoordinates{});

				bool bAllSegmentsUnlit = true;
				bool bAllSegmentsOpaque = true;

				for (int32 SegmentIndex = 0; SegmentIndex < MeshBatches.Num(); SegmentIndex++)
				{
					const FMaterialRenderProxy* FallbackMaterialRenderProxyPtr = nullptr;
					const FMaterial& Material = MeshBatches[SegmentIndex].MaterialRenderProxy->GetMaterialWithFallback(GMaxRHIFeatureLevel, FallbackMaterialRenderProxyPtr);

					bAllSegmentsUnlit &= Material.GetShadingModels().HasOnlyShadingModel(MSM_Unlit) || !MeshBatches[SegmentIndex].CastShadow;
					bAllSegmentsOpaque &= Material.GetBlendMode() == EBlendMode::BLEND_Opaque;
				}

				if (!bAllSegmentsUnlit)
				{
					int32 InstanceIndex = RayTracingGeometryInstances.AddDefaulted(1);
					FRayTracingGeometryInstance& RayTracingInstance = RayTracingGeometryInstances[InstanceIndex];
					RayTracingInstance.GeometryRHI = InstanceGroup.ComponentUObject->GetStaticMesh()->GetRenderData()->LODResources[0].RayTracingGeometry.RayTracingGeometryRHI;

					RayTracingInstance.Transforms.AddZeroed(InstanceGroup.InstancedRenderData->PerInstanceRenderData->InstanceBuffer.GetNumInstances());

					for (int32 InstanceIdx = 0; InstanceIdx < (int32)InstanceGroup.InstancedRenderData->PerInstanceRenderData->InstanceBuffer.GetNumInstances(); InstanceIdx++)
					{
						FMatrix Transform;
						InstanceGroup.InstancedRenderData->PerInstanceRenderData->InstanceBuffer.GetInstanceTransform(InstanceIdx, Transform);
						Transform.M[3][3] = 1.0f;
						FMatrix InstanceTransform = Transform * InstanceGroup.LocalToWorld;

						RayTracingInstance.Transforms[InstanceIdx] = InstanceTransform;
					}

					RayTracingInstance.NumTransforms = RayTracingInstance.Transforms.Num();

					RayTracingInstance.UserData.Add((uint32)(Scene.StaticMeshInstanceRenderStates.Elements.Num() + InstanceGroupIndex));
					RayTracingInstance.Mask = 0xFF;
					RayTracingInstance.bForceOpaque = bAllSegmentsOpaque;

					for (int32 SegmentIndex = 0; SegmentIndex < MeshBatches.Num(); SegmentIndex++)
					{
						FFullyCachedRayTracingMeshCommandContext CommandContext(MeshCommandStorage, VisibleRayTracingMeshCommands, SegmentIndex, InstanceIndex);
						FMeshPassProcessorRenderState PassDrawRenderState(CachedViewUniformBuffer, CachedViewUniformBuffer);
						FLightmapRayTracingMeshProcessor RayTracingMeshProcessor(&CommandContext, PassDrawRenderState);

						RayTracingMeshProcessor.AddMeshBatch(MeshBatches[SegmentIndex], 1, nullptr);
					}
				}
			}
		}
	}
#else // RHI_RAYTRACING
	checkNoEntry();
#endif // RHI_RAYTRACING
}

void FSceneRenderState::SetupRayTracingScene()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(SetupRayTracingScene);

	FRHICommandListImmediate& RHICmdList = FRHICommandListExecutor::GetImmediateCommandList();

#ifdef RHI_RAYTRACING
	// Force build all the open build requests
	bool bBuildAll = true;
	GRayTracingGeometryManager.ProcessBuildRequests(RHICmdList, bBuildAll);
#endif // RHI_RAYTRACING

	if (!CachedRayTracingScene.IsValid())
	{
		CachedRayTracingScene = MakeUnique<FCachedRayTracingSceneData>();
		CachedRayTracingScene->SetupViewUniformBufferFromSceneRenderState(*this);
		CachedRayTracingScene->SetupFromSceneRenderState(*this);
		
		CalculateDistributionPrefixSumForAllLightmaps();
	}

#if 0 // Debug: verify cached ray tracing scene has up-to-date shader bindings
	TUniquePtr<FCachedRayTracingSceneData> VerificationRayTracingScene = MakeUnique<FCachedRayTracingSceneData>();
	VerificationRayTracingScene->CachedViewUniformBuffer = CachedRayTracingScene->CachedViewUniformBuffer;
	VerificationRayTracingScene->SetupFromSceneRenderState(*this);

	check(CachedRayTracingScene->VisibleRayTracingMeshCommands.Num() == VerificationRayTracingScene->VisibleRayTracingMeshCommands.Num());
	check(CachedRayTracingScene->MeshCommandStorage.Num() == VerificationRayTracingScene->MeshCommandStorage.Num());

	for (int32 CommandIndex = 0; CommandIndex < CachedRayTracingScene->VisibleRayTracingMeshCommands.Num(); CommandIndex++)
	{
		const FVisibleRayTracingMeshCommand& VisibleMeshCommand = CachedRayTracingScene->VisibleRayTracingMeshCommands[CommandIndex];
		const FRayTracingMeshCommand& MeshCommand = *VisibleMeshCommand.RayTracingMeshCommand;
		const FRayTracingMeshCommand& VerificationMeshCommand = *VerificationRayTracingScene->VisibleRayTracingMeshCommands[CommandIndex].RayTracingMeshCommand;
		check(MeshCommand.ShaderBindings.GetDynamicInstancingHash() == VerificationMeshCommand.ShaderBindings.GetDynamicInstancingHash());
		MeshCommand.ShaderBindings.MatchesForDynamicInstancing(VerificationMeshCommand.ShaderBindings);
	}
#endif

	FSceneViewFamily ViewFamily(FSceneViewFamily::ConstructionValues(
		nullptr,
		nullptr,
		FEngineShowFlags(ESFIM_Game))
		.SetWorldTimes(0, 0, 0)
		.SetGammaCorrection(1.0f));

	const FIntRect ViewRect(FIntPoint(0, 0), FIntPoint(GPreviewLightmapPhysicalTileSize, GPreviewLightmapPhysicalTileSize));

	// make a temporary view
	FSceneViewInitOptions ViewInitOptions;
	ViewInitOptions.ViewFamily = &ViewFamily;
	ViewInitOptions.SetViewRectangle(ViewRect);
	ViewInitOptions.ViewOrigin = FVector::ZeroVector;
	ViewInitOptions.ViewRotationMatrix = FMatrix::Identity;
	ViewInitOptions.ProjectionMatrix = FCanvas::CalcBaseTransform2D(GPreviewLightmapPhysicalTileSize, GPreviewLightmapPhysicalTileSize);
	ViewInitOptions.BackgroundColor = FLinearColor::Black;
	ViewInitOptions.OverlayColor = FLinearColor::White;

	ReferenceView = MakeShared<FViewInfo>(ViewInitOptions);
	FViewInfo& View = *ReferenceView;
	View.ViewRect = View.UnscaledViewRect;

	{
		TRACE_CPUPROFILER_EVENT_SCOPE(SetupViewBuffers);

		View.PrimitiveSceneDataOverrideSRV = CachedRayTracingScene->PrimitiveSceneDataBufferSRV;
		View.LightmapSceneDataOverrideSRV = CachedRayTracingScene->LightmapSceneDataBufferSRV;
		View.InstanceSceneDataOverrideSRV = CachedRayTracingScene->InstanceSceneDataBufferSRV;

		{
			TRACE_CPUPROFILER_EVENT_SCOPE(SetupUniformBufferParameters);

			// Expanded version of View.InitRHIResources() - need to do SetupSkyIrradianceEnvironmentMapConstants manually because the estimation of skylight is dependent on GetSkySHDiffuse
			View.CachedViewUniformShaderParameters = MakeUnique<FViewUniformShaderParameters>();

			FBox UnusedVolumeBounds[TVC_MAX];
			View.SetupUniformBufferParameters(
				UnusedVolumeBounds,
				TVC_MAX,
				*View.CachedViewUniformShaderParameters);

			View.CachedViewUniformShaderParameters->InstanceDataSOAStride = CachedRayTracingScene->InstanceSceneDataSOAStride;

			if (LightSceneRenderState.SkyLight.IsSet())
			{
				View.CachedViewUniformShaderParameters->SkyIrradianceEnvironmentMap = LightSceneRenderState.SkyLight->SkyIrradianceEnvironmentMap.SRV;
			}
			else
			{
				View.CachedViewUniformShaderParameters->SkyIrradianceEnvironmentMap = GIdentityPrimitiveBuffer.SkyIrradianceEnvironmentMapSRV;
			}

			View.ViewUniformBuffer = TUniformBufferRef<FViewUniformShaderParameters>::CreateUniformBufferImmediate(*View.CachedViewUniformShaderParameters, UniformBuffer_SingleFrame);

			CachedRayTracingScene->CachedViewUniformBuffer.UpdateUniformBufferImmediate(*View.CachedViewUniformShaderParameters);
		}

		const_cast<TRange<int32>&>(View.DynamicPrimitiveCollector.GetPrimitiveIdRange()) = TRange<int32>(0,
			FMath::Max(StaticMeshInstanceRenderStates.Elements.Num(), FMath::Max(InstanceGroupRenderStates.Elements.Num(), LandscapeRenderStates.Elements.Num()))
			);
		View.DynamicPrimitiveCollector.Commit();
	}

#if RHI_RAYTRACING

	{
		TRACE_CPUPROFILER_EVENT_SCOPE(RayTracingScene);

		SCOPED_DRAW_EVENTF(RHICmdList, GPULightmassUpdateRayTracingScene, TEXT("GPULightmass UpdateRayTracingScene %d Instances"), StaticMeshInstanceRenderStates.Elements.Num());

		TArray<FRayTracingGeometryInstance> RayTracingGeometryInstances;
		RayTracingGeometryInstances.Append(CachedRayTracingScene->RayTracingGeometryInstances);

		int32 LandscapeStartOffset = RayTracingGeometryInstances.Num();
		for (FLandscapeRenderState& Landscape : LandscapeRenderStates.Elements)
		{
			for (int32 SubY = 0; SubY < Landscape.NumSubsections; SubY++)
			{
				for (int32 SubX = 0; SubX < Landscape.NumSubsections; SubX++)
				{
					RayTracingGeometryInstances.AddDefaulted(1);
				}
			}
		}

		FMemMark Mark(FMemStack::Get());

		FRayTracingMeshCommandOneFrameArray VisibleRayTracingMeshCommands;
		FDynamicRayTracingMeshCommandStorage DynamicRayTracingMeshCommandStorage;

		{
			TRACE_CPUPROFILER_EVENT_SCOPE(Landscapes);

			int32 NumLandscapeInstances = 0;

			for (FLandscapeRenderState& Landscape : LandscapeRenderStates.Elements)
			{
				for (int32 SubY = 0; SubY < Landscape.NumSubsections; SubY++)
				{
					for (int32 SubX = 0; SubX < Landscape.NumSubsections; SubX++)
					{
						const int8 SubSectionIdx = SubX + SubY * Landscape.NumSubsections;
						uint32 NumPrimitives = FMath::Square(Landscape.SubsectionSizeVerts - 1) * 2;

						int32 InstanceIndex = LandscapeStartOffset + NumLandscapeInstances;
						NumLandscapeInstances++;

						FRayTracingGeometryInstance& RayTracingInstance = RayTracingGeometryInstances[InstanceIndex];
						RayTracingInstance.GeometryRHI = Landscape.SectionRayTracingStates[SubSectionIdx]->Geometry.RayTracingGeometryRHI;
						RayTracingInstance.Transforms.Add(FMatrix::Identity);
						RayTracingInstance.NumTransforms = 1;
						RayTracingInstance.UserData.Add((uint32)InstanceIndex);
						RayTracingInstance.Mask = 0xFF;

						TArray<FMeshBatch> MeshBatches = Landscape.GetMeshBatchesForGBufferRendering(0);

						FLandscapeBatchElementParams& BatchElementParams = *(FLandscapeBatchElementParams*)MeshBatches[0].Elements[0].UserData;
						BatchElementParams.LandscapeVertexFactoryMVFUniformBuffer = Landscape.SectionRayTracingStates[SubSectionIdx]->UniformBuffer;

						MeshBatches[0].Elements[0].IndexBuffer = Landscape.SharedBuffers->ZeroOffsetIndexBuffers[0];
						MeshBatches[0].Elements[0].FirstIndex = 0;
						MeshBatches[0].Elements[0].NumPrimitives = NumPrimitives;
						MeshBatches[0].Elements[0].MinVertexIndex = 0;
						MeshBatches[0].Elements[0].MaxVertexIndex = 0;

						bool bAllSegmentsUnlit = true;
						bool bAllSegmentsOpaque = true;

						for (int32 SegmentIndex = 0; SegmentIndex < MeshBatches.Num(); SegmentIndex++)
						{
							FDynamicRayTracingMeshCommandContext CommandContext(DynamicRayTracingMeshCommandStorage, VisibleRayTracingMeshCommands, SegmentIndex, InstanceIndex);
							FMeshPassProcessorRenderState PassDrawRenderState(View.ViewUniformBuffer, View.ViewUniformBuffer);
							FLightmapRayTracingMeshProcessor RayTracingMeshProcessor(&CommandContext, PassDrawRenderState);

							RayTracingMeshProcessor.AddMeshBatch(MeshBatches[SegmentIndex], 1, nullptr);

							const FMaterialRenderProxy* FallbackMaterialRenderProxyPtr = nullptr;
							const FMaterial& Material = MeshBatches[SegmentIndex].MaterialRenderProxy->GetMaterialWithFallback(GMaxRHIFeatureLevel, FallbackMaterialRenderProxyPtr);

							bAllSegmentsUnlit &= Material.GetShadingModels().HasOnlyShadingModel(MSM_Unlit) || !MeshBatches[SegmentIndex].CastShadow;
							bAllSegmentsOpaque &= Material.GetBlendMode() == EBlendMode::BLEND_Opaque;
						}

						if (bAllSegmentsUnlit)
						{
							RayTracingInstance.Mask = 0;
						}

						RayTracingInstance.bForceOpaque = bAllSegmentsOpaque;
					}
				}
			}
		}

		FRayTracingSceneInitializer Initializer;
		Initializer.Instances = RayTracingGeometryInstances;
		Initializer.ShaderSlotsPerGeometrySegment = RAY_TRACING_NUM_SHADER_SLOTS;
		if (IsRayTracingEnabled())
		{
			SCOPED_GPU_MASK(RHICmdList, FRHIGPUMask::All());

			RayTracingScene = RHICreateRayTracingScene(Initializer);
			RHICmdList.BuildAccelerationStructure(RayTracingScene);

			FRayTracingPipelineStateInitializer PSOInitializer;

			PSOInitializer.MaxPayloadSizeInBytes = 64;
			PSOInitializer.bAllowHitGroupIndexing = true;

			TArray<FRHIRayTracingShader*> RayGenShaderTable;
			{
				FLightmapPathTracingRGS::FPermutationDomain PermutationVector;
				PermutationVector.Set<FLightmapPathTracingRGS::FUseFirstBounceRayGuiding>(LightmapRenderer->bUseFirstBounceRayGuiding);
				PermutationVector.Set<FLightmapPathTracingRGS::FUseIrradianceCaching>(Settings->bUseIrradianceCaching);
				RayGenShaderTable.Add(GetGlobalShaderMap(GMaxRHIFeatureLevel)->GetShader<FLightmapPathTracingRGS>(PermutationVector).GetRayTracingShader());
			}
			{
				RayGenShaderTable.Add(GetGlobalShaderMap(GMaxRHIFeatureLevel)->GetShader<FStationaryLightShadowTracingRGS>().GetRayTracingShader());
			}
			{
				FVolumetricLightmapPathTracingRGS::FPermutationDomain PermutationVector;
				PermutationVector.Set<FVolumetricLightmapPathTracingRGS::FUseIrradianceCaching>(Settings->bUseIrradianceCaching);
				RayGenShaderTable.Add(GetGlobalShaderMap(GMaxRHIFeatureLevel)->GetShader<FVolumetricLightmapPathTracingRGS>(PermutationVector).GetRayTracingShader());
			}
			PSOInitializer.SetRayGenShaderTable(RayGenShaderTable);

			auto DefaultClosestHitShader = GetGlobalShaderMap(ERHIFeatureLevel::SM5)->GetShader<FOpaqueShadowHitGroup>().GetRayTracingShader();
			TArray<FRHIRayTracingShader*> RayTracingMaterialLibrary;
			FShaderMapResource::GetRayTracingMaterialLibrary(RayTracingMaterialLibrary, DefaultClosestHitShader);

			PSOInitializer.SetHitGroupTable(RayTracingMaterialLibrary);

			RayTracingPipelineState = PipelineStateCache::GetAndOrCreateRayTracingPipelineState(RHICmdList, PSOInitializer);

			TUniquePtr<FRayTracingLocalShaderBindingWriter> BindingWriter = MakeUnique<FRayTracingLocalShaderBindingWriter>();

			{
				TRACE_CPUPROFILER_EVENT_SCOPE(SetRayTracingShaderBindings);

				for (const FVisibleRayTracingMeshCommand VisibleMeshCommand : CachedRayTracingScene->VisibleRayTracingMeshCommands)
				{
					const FRayTracingMeshCommand& MeshCommand = *VisibleMeshCommand.RayTracingMeshCommand;

					MeshCommand.ShaderBindings.SetRayTracingShaderBindingsForHitGroup(BindingWriter.Get(),
						VisibleMeshCommand.InstanceIndex,
						MeshCommand.GeometrySegmentIndex,
						MeshCommand.MaterialShaderIndex,
						RAY_TRACING_SHADER_SLOT_MATERIAL);

					MeshCommand.ShaderBindings.SetRayTracingShaderBindingsForHitGroup(BindingWriter.Get(),
						VisibleMeshCommand.InstanceIndex,
						MeshCommand.GeometrySegmentIndex,
						MeshCommand.MaterialShaderIndex,
						RAY_TRACING_SHADER_SLOT_SHADOW);
				}

				for (const FVisibleRayTracingMeshCommand VisibleMeshCommand : VisibleRayTracingMeshCommands)
				{
					const FRayTracingMeshCommand& MeshCommand = *VisibleMeshCommand.RayTracingMeshCommand;

					MeshCommand.ShaderBindings.SetRayTracingShaderBindingsForHitGroup(BindingWriter.Get(),
						VisibleMeshCommand.InstanceIndex,
						MeshCommand.GeometrySegmentIndex,
						MeshCommand.MaterialShaderIndex,
						RAY_TRACING_SHADER_SLOT_MATERIAL);

					MeshCommand.ShaderBindings.SetRayTracingShaderBindingsForHitGroup(BindingWriter.Get(),
						VisibleMeshCommand.InstanceIndex,
						MeshCommand.GeometrySegmentIndex,
						MeshCommand.MaterialShaderIndex,
						RAY_TRACING_SHADER_SLOT_SHADOW);
				}

				{
					// Data is kept alive at the high level and explicitly deleted on RHI timeline,
					// so we can avoid copying parameters to the command list and simply pass raw pointers around.
					const bool bCopyDataToInlineStorage = false;
					BindingWriter->Commit(
						RHICmdList,
						RayTracingScene,
						RayTracingPipelineState,
						bCopyDataToInlineStorage);
				}

				// Move the ray tracing binding container ownership to the command list, so that memory will be
				// released on the RHI thread timeline, after the commands that reference it are processed.
				// TUniquePtr<> auto destructs when exiting the lambda
				RHICmdList.EnqueueLambda([BindingWriter = MoveTemp(BindingWriter)](FRHICommandListImmediate&) {});
			}
		}
	}
#endif
}

void FSceneRenderState::DestroyRayTracingScene()
{
	ReferenceView.Reset();

#if RHI_RAYTRACING
	if (IsRayTracingEnabled() && RayTracingScene.IsValid())
	{
		FRHICommandListImmediate& RHICmdList = FRHICommandListExecutor::GetImmediateCommandList();
		SCOPED_GPU_MASK(RHICmdList, FRHIGPUMask::All());
		RHICmdList.ClearRayTracingBindings(RayTracingScene);

		RayTracingScene.SafeRelease();
	}
#endif
}

void FSceneRenderState::CalculateDistributionPrefixSumForAllLightmaps()
{
	uint32 PrefixSum = 0;

	for (FLightmapRenderState& Lightmap : LightmapRenderStates.Elements)
	{
		Lightmap.DistributionPrefixSum = PrefixSum;
		PrefixSum += Lightmap.GetNumTilesAcrossAllMipmapLevels();
	}
}

BEGIN_SHADER_PARAMETER_STRUCT(FLightmapGBufferPassParameters, )
	SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FLightmapGBufferParams, PassUniformBuffer)
	RENDER_TARGET_BINDING_SLOTS()
END_SHADER_PARAMETER_STRUCT()

bool ClampTexelPositionAndOffsetTile(FIntPoint& SrcVirtualTexelPosition, FIntPoint& SrcTileToLoad, FIntPoint SizeInTiles)
{
	bool bLoadingOutOfBounds = false;

	if (SrcVirtualTexelPosition.X < 0)
	{
		SrcTileToLoad.X -= 1;

		if (SrcTileToLoad.X < 0)
		{
			bLoadingOutOfBounds = true;
		}

		SrcVirtualTexelPosition.X += GPreviewLightmapVirtualTileSize;
	}
	else if (SrcVirtualTexelPosition.X >= GPreviewLightmapVirtualTileSize)
	{
		SrcTileToLoad.X += 1;

		if (SrcTileToLoad.X >= SizeInTiles.X)
		{
			bLoadingOutOfBounds = true;
		}

		SrcVirtualTexelPosition.X -= GPreviewLightmapVirtualTileSize;
	}

	if (SrcVirtualTexelPosition.Y < 0)
	{
		SrcTileToLoad.Y -= 1;

		if (SrcTileToLoad.Y < 0)
		{
			bLoadingOutOfBounds = true;
		}

		SrcVirtualTexelPosition.Y += GPreviewLightmapVirtualTileSize;
	}
	else if (SrcVirtualTexelPosition.Y >= GPreviewLightmapVirtualTileSize)
	{
		SrcTileToLoad.Y += 1;

		if (SrcTileToLoad.Y >= SizeInTiles.Y)
		{
			bLoadingOutOfBounds = true;
		}

		SrcVirtualTexelPosition.Y -= GPreviewLightmapVirtualTileSize;
	}

	return bLoadingOutOfBounds;
}

void FLightmapRenderer::Finalize(FRDGBuilder& GraphBuilder)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FLightmapRenderer::Finalize);

	if (PendingTileRequests.Num() == 0)
	{
		return;
	}

	const auto HoldReference = [](FRDGBuilder& GraphBuilder, const FShaderResourceViewRHIRef& View) -> const FShaderResourceViewRHIRef&
	{
		return *GraphBuilder.AllocObject<FShaderResourceViewRHIRef>(View);
	};

	// Upload & copy converged tiles directly
	{
		TArray<FLightmapTileRequest> TileUploadRequests = PendingTileRequests.FilterByPredicate(
			[CurrentRevision = CurrentRevision, bDenoiseDuringInteractiveBake = bDenoiseDuringInteractiveBake](const FLightmapTileRequest& Tile)
		{ 
			return Tile.RenderState->DoesTileHaveValidCPUData(Tile.VirtualCoordinates, CurrentRevision) || (bDenoiseDuringInteractiveBake && Tile.RenderState->RetrieveTileState(Tile.VirtualCoordinates).OngoingReadbackRevision == CurrentRevision && Tile.RenderState->RetrieveTileState(Tile.VirtualCoordinates).bCanBeDenoised);
		});

		if (TileUploadRequests.Num() > 0)
		{
			FRHICommandListImmediate& RHICmdList = GraphBuilder.RHICmdList;
			SCOPED_DRAW_EVENTF(RHICmdList, GPULightmassUploadConvergedTiles, TEXT("GPULightmass UploadConvergedTiles %d tiles"), TileUploadRequests.Num());

			int32 NewSize = FMath::CeilToInt(FMath::Sqrt(static_cast<float>(TileUploadRequests.Num())));
			if (!UploadTilePoolGPU.IsValid() || UploadTilePoolGPU->SizeInTiles.X < NewSize)
			{
				UploadTilePoolGPU = MakeUnique<FLightmapTilePoolGPU>(3, FIntPoint(NewSize, NewSize), FIntPoint(GPreviewLightmapPhysicalTileSize, GPreviewLightmapPhysicalTileSize));
				UE_LOG(LogGPULightmass, Log, TEXT("Resizing GPULightmass upload tile pool to (%d, %d) %dx%d"), NewSize, NewSize, NewSize * GPreviewLightmapPhysicalTileSize, NewSize * GPreviewLightmapPhysicalTileSize);
			}

			{
				uint32 DstRowPitch;
				FLinearColor* Texture[3];
				Texture[0] = (FLinearColor*)RHICmdList.LockTexture2D(UploadTilePoolGPU->PooledRenderTargets[0]->GetRenderTargetItem().TargetableTexture->GetTexture2D(), 0, RLM_WriteOnly, DstRowPitch, false);
				Texture[1] = (FLinearColor*)RHICmdList.LockTexture2D(UploadTilePoolGPU->PooledRenderTargets[1]->GetRenderTargetItem().TargetableTexture->GetTexture2D(), 0, RLM_WriteOnly, DstRowPitch, false);
				Texture[2] = (FLinearColor*)RHICmdList.LockTexture2D(UploadTilePoolGPU->PooledRenderTargets[2]->GetRenderTargetItem().TargetableTexture->GetTexture2D(), 0, RLM_WriteOnly, DstRowPitch, false);

				TSet<FVirtualTile> TilesToDecompress;

				FTileDataLayer::Evict();

				for (FLightmapTileRequest& Tile : TileUploadRequests)
				{
					FIntPoint Positions[] = {
						FIntPoint(0, 0),
						FIntPoint(0, GPreviewLightmapPhysicalTileSize - 1),
						FIntPoint(GPreviewLightmapPhysicalTileSize - 1, 0),
						FIntPoint(GPreviewLightmapPhysicalTileSize - 1, GPreviewLightmapPhysicalTileSize - 1),
						FIntPoint(GPreviewLightmapPhysicalTileSize / 2, GPreviewLightmapPhysicalTileSize / 2),
						FIntPoint(GPreviewLightmapPhysicalTileSize / 2, 0),
						FIntPoint(0, GPreviewLightmapPhysicalTileSize / 2),
						FIntPoint(GPreviewLightmapPhysicalTileSize / 2, GPreviewLightmapPhysicalTileSize - 1),
						FIntPoint(GPreviewLightmapPhysicalTileSize - 1, GPreviewLightmapPhysicalTileSize / 2)
					};

					for (FIntPoint Position : Positions)
					{
						FIntPoint SrcVirtualTexelPosition = Position - FIntPoint(GPreviewLightmapTileBorderSize, GPreviewLightmapTileBorderSize);
						FIntPoint SrcTileToLoad = Tile.VirtualCoordinates.Position;

						bool bLoadingOutOfBounds = ClampTexelPositionAndOffsetTile(SrcVirtualTexelPosition, SrcTileToLoad, Tile.RenderState->GetPaddedSizeInTilesAtMipLevel(Tile.VirtualCoordinates.MipLevel));

						FTileVirtualCoordinates SrcTileCoords(SrcTileToLoad, Tile.VirtualCoordinates.MipLevel);

						if (!bLoadingOutOfBounds)
						{
							if (!Tile.RenderState->DoesTileHaveValidCPUData(SrcTileCoords, CurrentRevision))
							{
								if (!bDenoiseDuringInteractiveBake)
								{
									bLoadingOutOfBounds = true;
								}
								else if (Tile.RenderState->RetrieveTileState(SrcTileCoords).OngoingReadbackRevision != CurrentRevision || !Tile.RenderState->RetrieveTileState(SrcTileCoords).bCanBeDenoised)
								{
									bLoadingOutOfBounds = true;
								}
							}
						}

						if (!bLoadingOutOfBounds)
						{
							Tile.RenderState->TileStorage[SrcTileCoords].CPUTextureData[0]->Decompress();
							Tile.RenderState->TileStorage[SrcTileCoords].CPUTextureData[1]->Decompress();
							Tile.RenderState->TileStorage[SrcTileCoords].CPUTextureData[2]->Decompress();
						}
					}
				}

				ParallelFor(TileUploadRequests.Num(), [&](int32 TileIndex)
				{
					FIntPoint SrcTilePosition(TileUploadRequests[TileIndex].VirtualCoordinates.Position);
					FIntPoint DstTilePosition(TileIndex % UploadTilePoolGPU->SizeInTiles.X, TileIndex / UploadTilePoolGPU->SizeInTiles.X);

					const int32 SrcRowPitchInPixels = TileUploadRequests[TileIndex].RenderState->GetPaddedSizeAtMipLevel(TileUploadRequests[TileIndex].VirtualCoordinates.MipLevel).X;
					const int32 DstRowPitchInPixels = DstRowPitch / sizeof(FLinearColor);

					for (int32 Y = 0; Y < GPreviewLightmapPhysicalTileSize; Y++)
					{
						for (int32 X = 0; X < GPreviewLightmapPhysicalTileSize; X++)
						{
							bool bLoadingOutOfBounds = false;

							FIntPoint SrcVirtualTexelPosition = FIntPoint(X, Y) - FIntPoint(GPreviewLightmapTileBorderSize, GPreviewLightmapTileBorderSize);
							FIntPoint SrcTileToLoad = SrcTilePosition;

							bLoadingOutOfBounds = ClampTexelPositionAndOffsetTile(SrcVirtualTexelPosition, SrcTileToLoad, TileUploadRequests[TileIndex].RenderState->GetPaddedSizeInTilesAtMipLevel(TileUploadRequests[TileIndex].VirtualCoordinates.MipLevel));

							int32 SrcLinearIndex = SrcVirtualTexelPosition.Y * GPreviewLightmapVirtualTileSize + SrcVirtualTexelPosition.X;
							FIntPoint DstPixelPosition = DstTilePosition * GPreviewLightmapPhysicalTileSize + FIntPoint(X, Y);
							int32 DstLinearIndex = DstPixelPosition.Y * DstRowPitchInPixels + DstPixelPosition.X;

							FTileVirtualCoordinates SrcTileCoords(SrcTileToLoad, TileUploadRequests[TileIndex].VirtualCoordinates.MipLevel);

							if (!bLoadingOutOfBounds)
							{
								if (!TileUploadRequests[TileIndex].RenderState->DoesTileHaveValidCPUData(SrcTileCoords, CurrentRevision))
								{
									if (!bDenoiseDuringInteractiveBake)
									{
										bLoadingOutOfBounds = true;
									}
									else if (TileUploadRequests[TileIndex].RenderState->RetrieveTileState(SrcTileCoords).OngoingReadbackRevision != CurrentRevision || !TileUploadRequests[TileIndex].RenderState->RetrieveTileState(SrcTileCoords).bCanBeDenoised)
									{
										bLoadingOutOfBounds = true;
									}
								}
							}

							Texture[0][DstLinearIndex] = !bLoadingOutOfBounds ? TileUploadRequests[TileIndex].RenderState->TileStorage[SrcTileCoords].CPUTextureData[0]->Data[SrcLinearIndex] : FLinearColor(0, 0, 0, 0);
							Texture[1][DstLinearIndex] = !bLoadingOutOfBounds ? TileUploadRequests[TileIndex].RenderState->TileStorage[SrcTileCoords].CPUTextureData[1]->Data[SrcLinearIndex] : FLinearColor(0, 0, 0, 0);
							Texture[2][DstLinearIndex] = !bLoadingOutOfBounds ? TileUploadRequests[TileIndex].RenderState->TileStorage[SrcTileCoords].CPUTextureData[2]->Data[SrcLinearIndex] : FLinearColor(0, 0, 0, 0);
						}
					}
				});

				RHICmdList.UnlockTexture2D(UploadTilePoolGPU->PooledRenderTargets[0]->GetRenderTargetItem().TargetableTexture->GetTexture2D(), 0, false);
				RHICmdList.UnlockTexture2D(UploadTilePoolGPU->PooledRenderTargets[1]->GetRenderTargetItem().TargetableTexture->GetTexture2D(), 0, false);
				RHICmdList.UnlockTexture2D(UploadTilePoolGPU->PooledRenderTargets[2]->GetRenderTargetItem().TargetableTexture->GetTexture2D(), 0, false);
			}

			FGPUBatchedTileRequests GPUBatchedTileRequests;

			{
				for (const FLightmapTileRequest& Tile : TileUploadRequests)
				{
					FGPUTileDescription TileDesc;
					TileDesc.LightmapSize = Tile.RenderState->GetSize();
					TileDesc.VirtualTilePosition = Tile.VirtualCoordinates.Position * GPreviewLightmapVirtualTileSize;
					TileDesc.WorkingSetPosition = LightmapTilePoolGPU.GetPositionFromLinearAddress(Tile.TileAddressInWorkingSet) * GPreviewLightmapPhysicalTileSize;
					TileDesc.ScratchPosition = ScratchTilePoolGPU->GetPositionFromLinearAddress(Tile.TileAddressInScratch) * GPreviewLightmapPhysicalTileSize;
					TileDesc.OutputLayer0Position = Tile.OutputPhysicalCoordinates[0] * GPreviewLightmapPhysicalTileSize;
					TileDesc.OutputLayer1Position = Tile.OutputPhysicalCoordinates[1] * GPreviewLightmapPhysicalTileSize;
					TileDesc.OutputLayer2Position = Tile.OutputPhysicalCoordinates[2] * GPreviewLightmapPhysicalTileSize;
					TileDesc.FrameIndex = Tile.RenderState->RetrieveTileState(Tile.VirtualCoordinates).Revision;
					TileDesc.RenderPassIndex = Tile.RenderState->RetrieveTileState(Tile.VirtualCoordinates).RenderPassIndex;
					GPUBatchedTileRequests.BatchedTilesDesc.Add(TileDesc);
				}

				FRHIResourceCreateInfo CreateInfo(TEXT("BatchedTilesBuffer"));
				CreateInfo.ResourceArray = &GPUBatchedTileRequests.BatchedTilesDesc;

				GPUBatchedTileRequests.BatchedTilesBuffer = RHICreateStructuredBuffer(sizeof(FGPUTileDescription), GPUBatchedTileRequests.BatchedTilesDesc.GetResourceDataSize(), BUF_Static | BUF_ShaderResource, CreateInfo);
				GPUBatchedTileRequests.BatchedTilesSRV = RHICreateShaderResourceView(GPUBatchedTileRequests.BatchedTilesBuffer);
			}

			IPooledRenderTarget* OutputRenderTargets[3] = { nullptr, nullptr, nullptr };

			for (auto& Tile : TileUploadRequests)
			{
				for (int32 RenderTargetIndex = 0; RenderTargetIndex < 3; RenderTargetIndex++)
				{
					if (Tile.OutputRenderTargets[RenderTargetIndex] != nullptr)
					{
						if (OutputRenderTargets[RenderTargetIndex] == nullptr)
						{
							OutputRenderTargets[RenderTargetIndex] = Tile.OutputRenderTargets[RenderTargetIndex];
						}
						else
						{
							ensure(OutputRenderTargets[RenderTargetIndex] == Tile.OutputRenderTargets[RenderTargetIndex]);
						}
					}
				}
			}

			FIntPoint DispatchResolution;
			DispatchResolution.X = GPreviewLightmapPhysicalTileSize * GPUBatchedTileRequests.BatchedTilesDesc.Num();
			DispatchResolution.Y = GPreviewLightmapPhysicalTileSize;

			FRDGTextureRef StagingHQLayer0 = GraphBuilder.RegisterExternalTexture(UploadTilePoolGPU->PooledRenderTargets[0], TEXT("StagingHQLayer0"));
			FRDGTextureRef StagingHQLayer1 = GraphBuilder.RegisterExternalTexture(UploadTilePoolGPU->PooledRenderTargets[1], TEXT("StagingHQLayer1"));
			FRDGTextureRef StagingShadowMask = GraphBuilder.RegisterExternalTexture(UploadTilePoolGPU->PooledRenderTargets[2], TEXT("StagingShadowMask"));

			FBufferRHIRef SrcTilePositionsBuffer;
			FShaderResourceViewRHIRef SrcTilePositionsSRV;
			FBufferRHIRef DstTilePositionsBuffer;
			FShaderResourceViewRHIRef DstTilePositionsSRV;

			if (OutputRenderTargets[0] != nullptr)
			{
				{
					TResourceArray<FIntPoint> SrcTilePositions;
					TResourceArray<FIntPoint> DstTilePositions;

					for (int32 TileIndex = 0; TileIndex < TileUploadRequests.Num(); TileIndex++)
					{
						SrcTilePositions.Add(FIntPoint(TileIndex % UploadTilePoolGPU->SizeInTiles.X, TileIndex / UploadTilePoolGPU->SizeInTiles.X) * GPreviewLightmapPhysicalTileSize);
						DstTilePositions.Add(TileUploadRequests[TileIndex].OutputPhysicalCoordinates[0] * GPreviewLightmapPhysicalTileSize);
					}

					{
						FRHIResourceCreateInfo CreateInfo(TEXT("SrcTilePositionsBuffer"), &SrcTilePositions);
						SrcTilePositionsBuffer = RHICreateStructuredBuffer(sizeof(FIntPoint), SrcTilePositions.GetResourceDataSize(), BUF_Static | BUF_ShaderResource, CreateInfo);
						SrcTilePositionsSRV = RHICreateShaderResourceView(SrcTilePositionsBuffer);
					}

					{
						FRHIResourceCreateInfo CreateInfo(TEXT("DstTilePositionsBuffer"), &DstTilePositions);
						DstTilePositionsBuffer = RHICreateStructuredBuffer(sizeof(FIntPoint), DstTilePositions.GetResourceDataSize(), BUF_Static | BUF_ShaderResource, CreateInfo);
						DstTilePositionsSRV = RHICreateShaderResourceView(DstTilePositionsBuffer);
					}
				}

				{
					FRDGTextureRef RenderTargetTileAtlas0 = GraphBuilder.RegisterExternalTexture(OutputRenderTargets[0], TEXT("GPULightmassRenderTargetTileAtlas0"));

					FUploadConvergedLightmapTilesCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FUploadConvergedLightmapTilesCS::FParameters>();

					PassParameters->NumBatchedTiles = GPUBatchedTileRequests.BatchedTilesDesc.Num();
					PassParameters->SrcTexture = GraphBuilder.CreateUAV(StagingHQLayer0);
					PassParameters->DstTexture = GraphBuilder.CreateUAV(RenderTargetTileAtlas0);
					PassParameters->SrcTilePositions = HoldReference(GraphBuilder, SrcTilePositionsSRV);
					PassParameters->DstTilePositions = HoldReference(GraphBuilder, DstTilePositionsSRV);

					TShaderMapRef<FUploadConvergedLightmapTilesCS> ComputeShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
					FComputeShaderUtils::AddPass(
						GraphBuilder,
						RDG_EVENT_NAME("UploadConvergedLightmapTiles"),
						ComputeShader,
						PassParameters,
						FComputeShaderUtils::GetGroupCount(DispatchResolution, FComputeShaderUtils::kGolden2DGroupSize));
				}
			}

			if (OutputRenderTargets[1] != nullptr)
			{
				{
					TResourceArray<FIntPoint> SrcTilePositions;
					TResourceArray<FIntPoint> DstTilePositions;

					for (int32 TileIndex = 0; TileIndex < TileUploadRequests.Num(); TileIndex++)
					{
						SrcTilePositions.Add(FIntPoint(TileIndex % UploadTilePoolGPU->SizeInTiles.X, TileIndex / UploadTilePoolGPU->SizeInTiles.X) * GPreviewLightmapPhysicalTileSize);
						DstTilePositions.Add(TileUploadRequests[TileIndex].OutputPhysicalCoordinates[1] * GPreviewLightmapPhysicalTileSize);
					}

					{
						FRHIResourceCreateInfo CreateInfo(TEXT("SrcTilePositionsBuffer"), &SrcTilePositions);
						SrcTilePositionsBuffer = RHICreateStructuredBuffer(sizeof(FIntPoint), SrcTilePositions.GetResourceDataSize(), BUF_Static | BUF_ShaderResource, CreateInfo);
						SrcTilePositionsSRV = RHICreateShaderResourceView(SrcTilePositionsBuffer);
					}

					{
						FRHIResourceCreateInfo CreateInfo(TEXT("DstTilePositionsBuffer"), &DstTilePositions);
						DstTilePositionsBuffer = RHICreateStructuredBuffer(sizeof(FIntPoint), DstTilePositions.GetResourceDataSize(), BUF_Static | BUF_ShaderResource, CreateInfo);
						DstTilePositionsSRV = RHICreateShaderResourceView(DstTilePositionsBuffer);
					}
				}

				{
					FRDGTextureRef RenderTargetTileAtlas1 = GraphBuilder.RegisterExternalTexture(OutputRenderTargets[1], TEXT("GPULightmassRenderTargetTileAtlas1"));

					FUploadConvergedLightmapTilesCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FUploadConvergedLightmapTilesCS::FParameters>();

					PassParameters->NumBatchedTiles = GPUBatchedTileRequests.BatchedTilesDesc.Num();
					PassParameters->SrcTexture = GraphBuilder.CreateUAV(StagingHQLayer1);
					PassParameters->DstTexture = GraphBuilder.CreateUAV(RenderTargetTileAtlas1);
					PassParameters->SrcTilePositions = HoldReference(GraphBuilder, SrcTilePositionsSRV);
					PassParameters->DstTilePositions = HoldReference(GraphBuilder, DstTilePositionsSRV);

					TShaderMapRef<FUploadConvergedLightmapTilesCS> ComputeShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
					FComputeShaderUtils::AddPass(
						GraphBuilder,
						RDG_EVENT_NAME("UploadConvergedLightmapTiles"),
						ComputeShader,
						PassParameters,
						FComputeShaderUtils::GetGroupCount(DispatchResolution, FComputeShaderUtils::kGolden2DGroupSize));
				}
			}

			if (OutputRenderTargets[2] != nullptr)
			{
				{
					TResourceArray<FIntPoint> SrcTilePositions;
					TResourceArray<FIntPoint> DstTilePositions;

					for (int32 TileIndex = 0; TileIndex < TileUploadRequests.Num(); TileIndex++)
					{
						SrcTilePositions.Add(FIntPoint(TileIndex % UploadTilePoolGPU->SizeInTiles.X, TileIndex / UploadTilePoolGPU->SizeInTiles.X) * GPreviewLightmapPhysicalTileSize);
						DstTilePositions.Add(TileUploadRequests[TileIndex].OutputPhysicalCoordinates[2] * GPreviewLightmapPhysicalTileSize);
					}

					{
						FRHIResourceCreateInfo CreateInfo(TEXT("SrcTilePositionsBuffer"), &SrcTilePositions);
						SrcTilePositionsBuffer = RHICreateStructuredBuffer(sizeof(FIntPoint), SrcTilePositions.GetResourceDataSize(), BUF_Static | BUF_ShaderResource, CreateInfo);
						SrcTilePositionsSRV = RHICreateShaderResourceView(SrcTilePositionsBuffer);
					}

					{
						FRHIResourceCreateInfo CreateInfo(TEXT("DstTilePositionsBuffer"), &DstTilePositions);
						DstTilePositionsBuffer = RHICreateStructuredBuffer(sizeof(FIntPoint), DstTilePositions.GetResourceDataSize(), BUF_Static | BUF_ShaderResource, CreateInfo);
						DstTilePositionsSRV = RHICreateShaderResourceView(DstTilePositionsBuffer);
					}
				}

				{
					FRDGTextureRef RenderTargetTileAtlas2 = GraphBuilder.RegisterExternalTexture(OutputRenderTargets[2], TEXT("GPULightmassRenderTargetTileAtlas1"));

					FUploadConvergedLightmapTilesCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FUploadConvergedLightmapTilesCS::FParameters>();

					PassParameters->NumBatchedTiles = GPUBatchedTileRequests.BatchedTilesDesc.Num();
					PassParameters->SrcTexture = GraphBuilder.CreateUAV(StagingShadowMask);
					PassParameters->DstTexture = GraphBuilder.CreateUAV(RenderTargetTileAtlas2);
					PassParameters->SrcTilePositions = HoldReference(GraphBuilder, SrcTilePositionsSRV);
					PassParameters->DstTilePositions = HoldReference(GraphBuilder, DstTilePositionsSRV);

					TShaderMapRef<FUploadConvergedLightmapTilesCS> ComputeShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
					FComputeShaderUtils::AddPass(
						GraphBuilder,
						RDG_EVENT_NAME("UploadConvergedLightmapTiles"),
						ComputeShader,
						PassParameters,
						FComputeShaderUtils::GetGroupCount(DispatchResolution, FComputeShaderUtils::kGolden2DGroupSize));
				}
			}
		}

		// Drop these converged requests, critical so that we won't perform readback repeatedly
		PendingTileRequests = PendingTileRequests.FilterByPredicate([CurrentRevision = CurrentRevision](const FLightmapTileRequest& Tile) { return !Tile.RenderState->DoesTileHaveValidCPUData(Tile.VirtualCoordinates, CurrentRevision); });
	}

	PendingTileRequests = PendingTileRequests.FilterByPredicate([CurrentRevision = CurrentRevision](const FLightmapTileRequest& Tile) { return Tile.RenderState->RetrieveTileState(Tile.VirtualCoordinates).OngoingReadbackRevision != CurrentRevision; });

	if (!bInsideBackgroundTick && !bOnlyBakeWhatYouSee)
	{
		TArray<FLightmapTileRequest> RoundRobinFilteredRequests;
		if (PendingTileRequests.Num() > (128 * (int32)GNumExplicitGPUsForRendering))
		{
			int32 RoundRobinDivisor = PendingTileRequests.Num() / (128 * (int32)GNumExplicitGPUsForRendering);

			for (int32 Index = 0; Index < PendingTileRequests.Num(); Index++)
			{
				if (Index % RoundRobinDivisor == FrameNumber % RoundRobinDivisor)
				{
					RoundRobinFilteredRequests.Add(PendingTileRequests[Index]);
				}
			}

			PendingTileRequests = RoundRobinFilteredRequests;
		}
	}

	if (!bInsideBackgroundTick && bOnlyBakeWhatYouSee)
	{
		TArray<FLightmapTileRequest> ScreenOutputTiles = PendingTileRequests.FilterByPredicate([](const FLightmapTileRequest& Tile) { return Tile.IsScreenOutputTile(); });
		if (ScreenOutputTiles.Num() > 0)
		{
			TilesVisibleLastFewFrames[(FrameNumber - 1 + TilesVisibleLastFewFrames.Num()) % TilesVisibleLastFewFrames.Num()] = ScreenOutputTiles;
			
			if (bIsRecordingTileRequests)
			{
				for (FLightmapTileRequest& Tile : ScreenOutputTiles)
				{
					RecordedTileRequests.AddUnique(Tile);
				}
			}
		}
	}

	PendingTileRequests.Sort([](const FLightmapTileRequest& A, const FLightmapTileRequest& B) { 
		return
			A.RenderState.GetElementId() < B.RenderState.GetElementId() || 
			A.RenderState.GetElementId() == B.RenderState.GetElementId() && A.VirtualCoordinates.GetVirtualAddress() < B.VirtualCoordinates.GetVirtualAddress();
	});

	// Alloc for tiles that need work
	{
		// Find which tiles are already resident
		TArray<FVirtualTile> TilesToQuery;
		for (auto& Tile : PendingTileRequests)
		{
			checkSlow(TilesToQuery.Find(FVirtualTile{ Tile.RenderState, Tile.VirtualCoordinates.MipLevel, (int32)Tile.VirtualCoordinates.GetVirtualAddress() }) == INDEX_NONE);
			TilesToQuery.Add(FVirtualTile{ Tile.RenderState, Tile.VirtualCoordinates.MipLevel, (int32)Tile.VirtualCoordinates.GetVirtualAddress() });
		}
		TArray<uint32> TileAddressIfResident;
		LightmapTilePoolGPU.QueryResidency(TilesToQuery, TileAddressIfResident);

		// We lock tiles that are resident and requested for current frame so that they won't be evicted by the following AllocAndLock
		TArray<FVirtualTile> NonResidentTilesToAllocate;
		TArray<int32> NonResidentTileRequestIndices;
		TArray<int32> ResidentTilesToLock;
		for (int32 TileIndex = 0; TileIndex < TileAddressIfResident.Num(); TileIndex++)
		{
			if (TileAddressIfResident[TileIndex] == ~0u)
			{
				NonResidentTilesToAllocate.Add(TilesToQuery[TileIndex]);
				NonResidentTileRequestIndices.Add(TileIndex);
			}
			else
			{
				ResidentTilesToLock.Add(TileAddressIfResident[TileIndex]);
				PendingTileRequests[TileIndex].TileAddressInWorkingSet = TileAddressIfResident[TileIndex];
			}
		}

		// All non-resident tiles need to be invalidated, whether they are successfully allocated later or not
		for (const FVirtualTile& Tile : NonResidentTilesToAllocate)
		{
			if (Tile.RenderState.IsValid())
			{
				Tile.RenderState->RetrieveTileState(FTileVirtualCoordinates(Tile.VirtualAddress, Tile.MipLevel)).Revision = -1;
				Tile.RenderState->RetrieveTileState(FTileVirtualCoordinates(Tile.VirtualAddress, Tile.MipLevel)).RenderPassIndex = 0;
			}
		}

		LightmapTilePoolGPU.Lock(ResidentTilesToLock);

		{
			TArray<int32> SuccessfullyAllocatedTiles;
			LightmapTilePoolGPU.AllocAndLock(NonResidentTilesToAllocate.Num(), SuccessfullyAllocatedTiles);

			// Map successfully allocated tiles, potentially evict some resident tiles to the lower cache tiers
			TArray<FVirtualTile> TilesToMap;
			for (int32 TileIndex = 0; TileIndex < SuccessfullyAllocatedTiles.Num(); TileIndex++)
			{
				TilesToMap.Add(NonResidentTilesToAllocate[TileIndex]);

				auto& Tile = PendingTileRequests[NonResidentTileRequestIndices[TileIndex]];
				Tile.TileAddressInWorkingSet = SuccessfullyAllocatedTiles[TileIndex];
			}

			// Till this point there might still be tiles with ~0u (which have failed allocation), they will be dropped later

			TArray<FVirtualTile> TilesEvicted;
			LightmapTilePoolGPU.Map(TilesToMap, SuccessfullyAllocatedTiles, TilesEvicted);

			// Invalidate evicted tiles' state as they can't be read back anymore
			// TODO: save to CPU and reload when appropriate
			for (const FVirtualTile& Tile : TilesEvicted)
			{
				if (Tile.RenderState.IsValid())
				{
					Tile.RenderState->RetrieveTileState(FTileVirtualCoordinates(Tile.VirtualAddress, Tile.MipLevel)).Revision = -1;
					Tile.RenderState->RetrieveTileState(FTileVirtualCoordinates(Tile.VirtualAddress, Tile.MipLevel)).RenderPassIndex = 0;
				}
			}

			LightmapTilePoolGPU.MakeAvailable(SuccessfullyAllocatedTiles, FrameNumber);
		}

		LightmapTilePoolGPU.MakeAvailable(ResidentTilesToLock, FrameNumber);

		{
			bool bScratchAllocationSucceeded = false;

			while (!bScratchAllocationSucceeded)
			{
				if (ScratchTilePoolGPU.IsValid())
				{
					TArray<int32> SuccessfullyAllocatedTiles;
					ScratchTilePoolGPU->AllocAndLock(TilesToQuery.Num(), SuccessfullyAllocatedTiles);

					if (SuccessfullyAllocatedTiles.Num() == TilesToQuery.Num())
					{
						for (int32 TileIndex = 0; TileIndex < SuccessfullyAllocatedTiles.Num(); TileIndex++)
						{
							auto& Tile = PendingTileRequests[TileIndex];
							Tile.TileAddressInScratch = SuccessfullyAllocatedTiles[TileIndex];
						}

						bScratchAllocationSucceeded = true;
					}

					ScratchTilePoolGPU->MakeAvailable(SuccessfullyAllocatedTiles, FrameNumber);
				}

				if (!bScratchAllocationSucceeded)
				{
					if (ScratchTilePoolGPU.IsValid() && ScratchTilePoolGPU->SizeInTiles.X >= 64)
					{
						// If we have reached our limit, don't retry and drop the requests.
						// Till this point there might still be tiles with ~0u (which have failed allocation), they will be dropped later
						break;
					}

					int32 NewSize = FMath::Min(FMath::CeilToInt(FMath::Sqrt(static_cast<float>(TilesToQuery.Num()))), 64);
					ScratchTilePoolGPU = MakeUnique<FLightmapTilePoolGPU>(3, FIntPoint(NewSize, NewSize), FIntPoint(GPreviewLightmapPhysicalTileSize, GPreviewLightmapPhysicalTileSize));
					UE_LOG(LogGPULightmass, Log, TEXT("Resizing GPULightmass scratch tile pool to (%d, %d) %dx%d"), NewSize, NewSize, NewSize * GPreviewLightmapPhysicalTileSize, NewSize * GPreviewLightmapPhysicalTileSize);
				}
			}
		}

		// Drop requests that have failed allocation
		PendingTileRequests = PendingTileRequests.FilterByPredicate([](const FLightmapTileRequest& TileRequest) { return TileRequest.TileAddressInWorkingSet != ~0u && TileRequest.TileAddressInScratch != ~0u; });
	}

	// If all tiles have failed allocation (unlikely but possible), return immediately
	if (PendingTileRequests.Num() == 0)
	{
		return;
	}

	Scene->SetupRayTracingScene();

	TStaticArray<FRDGTextureUAVRef, 3> ScratchTilePoolLayerUAVs;

	for (int32 Index = 0; Index < ScratchTilePoolLayerUAVs.Num(); ++Index)
	{
		ScratchTilePoolLayerUAVs[Index] = GraphBuilder.CreateUAV(GraphBuilder.RegisterExternalTexture(ScratchTilePoolGPU->PooledRenderTargets[Index]));
	}

	TRDGUniformBufferRef<FLightmapGBufferParams> PassUniformBuffer = nullptr;

	{
		auto* LightmapGBufferParameters = GraphBuilder.AllocParameters<FLightmapGBufferParams>();
		LightmapGBufferParameters->ScratchTilePoolLayer0 = ScratchTilePoolLayerUAVs[0];
		LightmapGBufferParameters->ScratchTilePoolLayer1 = ScratchTilePoolLayerUAVs[1];
		LightmapGBufferParameters->ScratchTilePoolLayer2 = ScratchTilePoolLayerUAVs[2];
		PassUniformBuffer = GraphBuilder.CreateUniformBuffer(LightmapGBufferParameters);
	}

	RDG_GPU_MASK_SCOPE(GraphBuilder, FRHIGPUMask::GPU0());

	IPooledRenderTarget* OutputRenderTargets[3] = { nullptr, nullptr, nullptr };

	for (auto& Tile : PendingTileRequests)
	{
		for (int32 RenderTargetIndex = 0; RenderTargetIndex < 3; RenderTargetIndex++)
		{
			if (Tile.OutputRenderTargets[RenderTargetIndex] != nullptr)
			{
				if (OutputRenderTargets[RenderTargetIndex] == nullptr)
				{
					OutputRenderTargets[RenderTargetIndex] = Tile.OutputRenderTargets[RenderTargetIndex];
				}
				else
				{
					ensure(OutputRenderTargets[RenderTargetIndex] == Tile.OutputRenderTargets[RenderTargetIndex]);
				}
			}
		}
	}

	// Perform deferred invalidation
	{
		// Clear working set pools
		for (int PoolLayerIndex = 0; PoolLayerIndex < LightmapTilePoolGPU.PooledRenderTargets.Num(); PoolLayerIndex++)
		{
			RDG_GPU_MASK_SCOPE(GraphBuilder, FRHIGPUMask::All());

			TArray<FVector4, SceneRenderingAllocator> ViewportsToClear;

			for (auto& Tile : PendingTileRequests)
			{
				if (Tile.RenderState->RetrieveTileState(Tile.VirtualCoordinates).Revision != CurrentRevision)
				{
					ViewportsToClear.Emplace(
						LightmapTilePoolGPU.GetPositionFromLinearAddress(Tile.TileAddressInWorkingSet).X * LightmapTilePoolGPU.LayerFormatAndTileSize[PoolLayerIndex].TileSize.X,
						LightmapTilePoolGPU.GetPositionFromLinearAddress(Tile.TileAddressInWorkingSet).Y * LightmapTilePoolGPU.LayerFormatAndTileSize[PoolLayerIndex].TileSize.Y,
						(LightmapTilePoolGPU.GetPositionFromLinearAddress(Tile.TileAddressInWorkingSet).X + 1) * LightmapTilePoolGPU.LayerFormatAndTileSize[PoolLayerIndex].TileSize.X,
						(LightmapTilePoolGPU.GetPositionFromLinearAddress(Tile.TileAddressInWorkingSet).Y + 1) * LightmapTilePoolGPU.LayerFormatAndTileSize[PoolLayerIndex].TileSize.Y);
				}
			}

			if (ViewportsToClear.Num())
			{
				FRDGTextureRef Texture = GraphBuilder.RegisterExternalTexture(LightmapTilePoolGPU.PooledRenderTargets[PoolLayerIndex]);

				auto* PassParameters = GraphBuilder.AllocParameters<FRenderTargetParameters>();
				PassParameters->RenderTargets[0] = FRenderTargetBinding(Texture, ERenderTargetLoadAction::ENoAction);

				GraphBuilder.AddPass(
					RDG_EVENT_NAME("ClearLightmapTilePoolGPU"),
					PassParameters,
					ERDGPassFlags::Raster,
					[LocalViewportsToClear = MoveTemp(ViewportsToClear)](FRHICommandList& RHICmdList)
				{
					for (FVector4 Viewport : LocalViewportsToClear)
					{
						RHICmdList.SetViewport(Viewport.X, Viewport.Y, 0.0f, Viewport.Z, Viewport.W, 1.0f);
						DrawClearQuad(RHICmdList, FLinearColor(0.0f, 0.0f, 0.0f, 0.0f));
					}
				});
			}
		}

		for (auto& Tile : PendingTileRequests)
		{
			if (Tile.RenderState->RetrieveTileState(Tile.VirtualCoordinates).Revision != CurrentRevision)
			{
				{
					// Reset GI sample states
					Tile.RenderState->RetrieveTileState(Tile.VirtualCoordinates).Invalidate();
				}

				{
					// Clear stationary light sample states
					Tile.RenderState->RetrieveTileRelevantLightSampleState(Tile.VirtualCoordinates).RelevantDirectionalLightSampleCount.Empty();
					Tile.RenderState->RetrieveTileRelevantLightSampleState(Tile.VirtualCoordinates).RelevantPointLightSampleCount.Empty();
					Tile.RenderState->RetrieveTileRelevantLightSampleState(Tile.VirtualCoordinates).RelevantSpotLightSampleCount.Empty();
					Tile.RenderState->RetrieveTileRelevantLightSampleState(Tile.VirtualCoordinates).RelevantRectLightSampleCount.Empty();

					for (FDirectionalLightRenderState& DirectionalLight : Scene->LightSceneRenderState.DirectionalLights.Elements)
					{
						if (DirectionalLight.bStationary)
						{
							Tile.RenderState->RetrieveTileRelevantLightSampleState(Tile.VirtualCoordinates).RelevantDirectionalLightSampleCount.Add(FDirectionalLightRenderStateRef(DirectionalLight, Scene->LightSceneRenderState.DirectionalLights), 0);
						}
					}

					for (FPointLightRenderStateRef& PointLight : Tile.RenderState->RelevantPointLights)
					{
						check(PointLight->bStationary);

						Tile.RenderState->RetrieveTileRelevantLightSampleState(Tile.VirtualCoordinates).RelevantPointLightSampleCount.Add(PointLight, 0);
					}

					for (FSpotLightRenderStateRef& SpotLight : Tile.RenderState->RelevantSpotLights)
					{
						check(SpotLight->bStationary);

						Tile.RenderState->RetrieveTileRelevantLightSampleState(Tile.VirtualCoordinates).RelevantSpotLightSampleCount.Add(SpotLight, 0);
					}

					for (FRectLightRenderStateRef& RectLight : Tile.RenderState->RelevantRectLights)
					{
						check(RectLight->bStationary);

						Tile.RenderState->RetrieveTileRelevantLightSampleState(Tile.VirtualCoordinates).RelevantRectLightSampleCount.Add(RectLight, 0);
					}
				}

				{
					// Last step: set invalidation state to 'valid'
					Tile.RenderState->RetrieveTileState(Tile.VirtualCoordinates).Revision = CurrentRevision;
				}
			}
		}
	}

	bool bLastFewFramesIdle = GCurrentLevelEditingViewportClient && !GCurrentLevelEditingViewportClient->IsRealtime();
	int32 NumSamplesPerFrame = (bInsideBackgroundTick && bLastFewFramesIdle) ? Scene->Settings->TilePassesInFullSpeedMode : Scene->Settings->TilePassesInSlowMode;

	{
		auto& PendingGITileRequests = *GraphBuilder.AllocObject<TArray<FLightmapTileRequest>>(
			PendingTileRequests.FilterByPredicate([NumGISamples = Scene->Settings->GISamples](const FLightmapTileRequest& Tile)
			{
				return !Tile.RenderState->IsTileGIConverged(Tile.VirtualCoordinates, NumGISamples);
			}));

		// Render GI
		for (int32 SampleIndex = 0; SampleIndex < NumSamplesPerFrame; SampleIndex++)
		{
			if (PendingGITileRequests.Num() > 0)
			{
				const int32 AAvsGIMultiplier = 8;

				if (SampleIndex % AAvsGIMultiplier == 0)
				{
					for (int ScratchLayerIndex = 0; ScratchLayerIndex < 3; ScratchLayerIndex++)
					{
						TResourceArray<FIntPoint> TilePositionsToClear;
						for (auto& Tile : PendingGITileRequests)
						{
							TilePositionsToClear.Add(ScratchTilePoolGPU->GetPositionFromLinearAddress(Tile.TileAddressInScratch));
						}

						FRWBuffer TilePositionsBuffer;
						TilePositionsBuffer.Initialize(TEXT("TilePositionsBufferForClear"), TilePositionsToClear.GetTypeSize(), TilePositionsToClear.Num(), PF_R32G32_UINT, 0, &TilePositionsToClear);

						FMultiTileClearCS::FParameters* Parameters = GraphBuilder.AllocParameters<FMultiTileClearCS::FParameters>();
						Parameters->NumTiles = TilePositionsToClear.Num();
						Parameters->TileSize = GPreviewLightmapPhysicalTileSize;
						Parameters->TilePositions = HoldReference(GraphBuilder, TilePositionsBuffer.SRV);
						Parameters->TilePool = ScratchTilePoolLayerUAVs[ScratchLayerIndex];

						TShaderMapRef<FMultiTileClearCS> ComputeShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
						FComputeShaderUtils::AddPass(
							GraphBuilder,
							RDG_EVENT_NAME("MultiTileClear"),
							ComputeShader,
							Parameters,
							FComputeShaderUtils::GetGroupCount(FIntPoint(GPreviewLightmapPhysicalTileSize * TilePositionsToClear.Num(), GPreviewLightmapPhysicalTileSize), FComputeShaderUtils::kGolden2DGroupSize));
					}

					{
						for (uint32 GPUIndex = 0; GPUIndex < GNumExplicitGPUsForRendering; GPUIndex++)
						{
							RDG_GPU_MASK_SCOPE(GraphBuilder, FRHIGPUMask::FromIndex(GPUIndex));

							auto* PassParameters = GraphBuilder.AllocParameters<FLightmapGBufferPassParameters>();
							PassParameters->PassUniformBuffer = PassUniformBuffer;

							GraphBuilder.AddPass(
								RDG_EVENT_NAME("LightmapGBuffer"),
								PassParameters,
								ERDGPassFlags::Raster,
								[this, ReferenceView = Scene->ReferenceView, &PendingGITileRequests, GPUIndex, AAvsGIMultiplier](FRHICommandList& RHICmdList)
							{
								for (const FLightmapTileRequest& Tile : PendingGITileRequests)
								{
									if (Tile.RenderState->IsTileGIConverged(Tile.VirtualCoordinates, Scene->Settings->GISamples)) continue;
									uint32 AssignedGPUIndex = (Tile.RenderState->DistributionPrefixSum + Tile.RenderState->RetrieveTileStateIndex(Tile.VirtualCoordinates)) % GNumExplicitGPUsForRendering;
									if (AssignedGPUIndex != GPUIndex) continue;

									RHICmdList.SetViewport(0, 0, 0.0f, GPreviewLightmapPhysicalTileSize, GPreviewLightmapPhysicalTileSize, 1.0f);

									float ScaleX = Tile.RenderState->GetPaddedSizeInTiles().X * GPreviewLightmapVirtualTileSize * 1.0f / (1 << Tile.VirtualCoordinates.MipLevel) / GPreviewLightmapPhysicalTileSize;
									float ScaleY = Tile.RenderState->GetPaddedSizeInTiles().Y * GPreviewLightmapVirtualTileSize * 1.0f / (1 << Tile.VirtualCoordinates.MipLevel) / GPreviewLightmapPhysicalTileSize;
									float BiasX = (1.0f * (-Tile.VirtualCoordinates.Position.X * GPreviewLightmapVirtualTileSize) - (-GPreviewLightmapTileBorderSize)) / GPreviewLightmapPhysicalTileSize;
									float BiasY = (1.0f * (-Tile.VirtualCoordinates.Position.Y * GPreviewLightmapVirtualTileSize) - (-GPreviewLightmapTileBorderSize)) / GPreviewLightmapPhysicalTileSize;

									FVector4 VirtualTexturePhysicalTileCoordinateScaleAndBias = FVector4(ScaleX, ScaleY, BiasX, BiasY);

									TArray<FMeshBatch> MeshBatches = Tile.RenderState->GeometryInstanceRef.GetMeshBatchesForGBufferRendering(Tile.VirtualCoordinates);

									for (auto& MeshBatch : MeshBatches)
									{
										FMeshBatchElement& Element = MeshBatch.Elements[0];

										Element.DynamicPrimitiveShaderDataIndex = Tile.RenderState->GeometryInstanceRef.GetElementId();
									}

									DrawDynamicMeshPass(
										*ReferenceView, RHICmdList,
										[
											View = ReferenceView.Get(),
											MeshBatches,
											VirtualTexturePhysicalTileCoordinateScaleAndBias,
											RenderPassIndex = Tile.RenderState->RetrieveTileState(Tile.VirtualCoordinates).RenderPassIndex / AAvsGIMultiplier,
											ScratchTilePoolOffset = ScratchTilePoolGPU->GetPositionFromLinearAddress(Tile.TileAddressInScratch) * GPreviewLightmapPhysicalTileSize
										](FDynamicPassMeshDrawListContext* DynamicMeshPassContext)
									{
										FLightmapGBufferMeshProcessor MeshProcessor(nullptr, View, DynamicMeshPassContext, VirtualTexturePhysicalTileCoordinateScaleAndBias, RenderPassIndex, ScratchTilePoolOffset);

										for (auto& MeshBatch : MeshBatches)
										{
											MeshProcessor.AddMeshBatch(MeshBatch, ~0ull, nullptr);
										}
									});

									GPrimitiveIdVertexBufferPool.DiscardAll();
								}
							});
						}
					}
				}

#if RHI_RAYTRACING
				if (IsRayTracingEnabled())
				{
					for (uint32 GPUIndex = 0; GPUIndex < GNumExplicitGPUsForRendering; GPUIndex++)
					{
						FGPUBatchedTileRequests GPUBatchedTileRequests;

						for (const FLightmapTileRequest& Tile : PendingGITileRequests)
						{
							uint32 AssignedGPUIndex = (Tile.RenderState->DistributionPrefixSum + Tile.RenderState->RetrieveTileStateIndex(Tile.VirtualCoordinates)) % GNumExplicitGPUsForRendering;
							if (AssignedGPUIndex != GPUIndex) continue;

							FGPUTileDescription TileDesc;
							TileDesc.LightmapSize = Tile.RenderState->GetSize();
							TileDesc.VirtualTilePosition = Tile.VirtualCoordinates.Position * GPreviewLightmapVirtualTileSize;
							TileDesc.WorkingSetPosition = LightmapTilePoolGPU.GetPositionFromLinearAddress(Tile.TileAddressInWorkingSet) * GPreviewLightmapPhysicalTileSize;
							TileDesc.ScratchPosition = ScratchTilePoolGPU->GetPositionFromLinearAddress(Tile.TileAddressInScratch) * GPreviewLightmapPhysicalTileSize;
							TileDesc.OutputLayer0Position = Tile.OutputPhysicalCoordinates[0] * GPreviewLightmapPhysicalTileSize;
							TileDesc.OutputLayer1Position = Tile.OutputPhysicalCoordinates[1] * GPreviewLightmapPhysicalTileSize;
							TileDesc.OutputLayer2Position = Tile.OutputPhysicalCoordinates[2] * GPreviewLightmapPhysicalTileSize;
							TileDesc.FrameIndex = Tile.RenderState->RetrieveTileState(Tile.VirtualCoordinates).Revision;
							TileDesc.RenderPassIndex = Tile.RenderState->RetrieveTileState(Tile.VirtualCoordinates).RenderPassIndex;
							if (!Tile.RenderState->IsTileGIConverged(Tile.VirtualCoordinates, Scene->Settings->GISamples))
							{
								Tile.RenderState->RetrieveTileState(Tile.VirtualCoordinates).RenderPassIndex++;

								if (/*Tile.VirtualCoordinates.MipLevel == 0 && */SampleIndex == 0)
								{
									if (!bInsideBackgroundTick)
									{
										Mip0WorkDoneLastFrame++;
									}
								}

								GPUBatchedTileRequests.BatchedTilesDesc.Add(TileDesc);
							}
						}

						if (GPUBatchedTileRequests.BatchedTilesDesc.Num() > 0)
						{
							FRHIResourceCreateInfo CreateInfo(TEXT("BatchedTilesBuffer"));
							CreateInfo.ResourceArray = &GPUBatchedTileRequests.BatchedTilesDesc;

							GPUBatchedTileRequests.BatchedTilesBuffer = RHICreateStructuredBuffer(sizeof(FGPUTileDescription), GPUBatchedTileRequests.BatchedTilesDesc.GetResourceDataSize(), BUF_Static | BUF_ShaderResource, CreateInfo);
							GPUBatchedTileRequests.BatchedTilesSRV = HoldReference(GraphBuilder, RHICreateShaderResourceView(GPUBatchedTileRequests.BatchedTilesBuffer));
						}

						RDG_GPU_MASK_SCOPE(GraphBuilder, FRHIGPUMask::FromIndex(GPUIndex));

						if (GPUBatchedTileRequests.BatchedTilesDesc.Num() > 0)
						{
							FRDGTextureRef GBufferWorldPosition = GraphBuilder.RegisterExternalTexture(ScratchTilePoolGPU->PooledRenderTargets[0], TEXT("GBufferWorldPosition"));
							FRDGTextureRef GBufferWorldNormal = GraphBuilder.RegisterExternalTexture(ScratchTilePoolGPU->PooledRenderTargets[1], TEXT("GBufferWorldNormal"));
							FRDGTextureRef GBufferShadingNormal = GraphBuilder.RegisterExternalTexture(ScratchTilePoolGPU->PooledRenderTargets[2], TEXT("GBufferShadingNormal"));
							FRDGTextureRef IrradianceAndSampleCount = GraphBuilder.RegisterExternalTexture(LightmapTilePoolGPU.PooledRenderTargets[0], TEXT("IrradianceAndSampleCount"));
							FRDGTextureRef SHDirectionality = GraphBuilder.RegisterExternalTexture(LightmapTilePoolGPU.PooledRenderTargets[1], TEXT("SHDirectionality"));
							FRDGTextureRef SHCorrectionAndStationarySkyLightBentNormal = GraphBuilder.RegisterExternalTexture(LightmapTilePoolGPU.PooledRenderTargets[4], TEXT("SHCorrectionAndStationarySkyLightBentNormal"));

							FRDGTextureRef RayGuidingLuminance = nullptr;
							FRDGTextureRef RayGuidingCDFX = nullptr;
							FRDGTextureRef RayGuidingCDFY = nullptr;

							if (bUseFirstBounceRayGuiding)
							{
								RayGuidingLuminance = GraphBuilder.RegisterExternalTexture(LightmapTilePoolGPU.PooledRenderTargets[5], TEXT("RayGuidingLuminance"));
								RayGuidingCDFX = GraphBuilder.RegisterExternalTexture(LightmapTilePoolGPU.PooledRenderTargets[6], TEXT("RayGuidingCDFX"));
								RayGuidingCDFY = GraphBuilder.RegisterExternalTexture(LightmapTilePoolGPU.PooledRenderTargets[7], TEXT("RayGuidingCDFY"));
							}

							// These two buffers must have lifetime extended beyond GraphBuilder.Execute()
							TUniformBufferRef<FSkyLightData> SkyLightDataUniformBuffer;

							FIntPoint RayTracingResolution;
							RayTracingResolution.X = GPreviewLightmapPhysicalTileSize * GPUBatchedTileRequests.BatchedTilesDesc.Num();
							RayTracingResolution.Y = GPreviewLightmapPhysicalTileSize;

							// Path Tracing GI
							{
								{
									FLightmapPathTracingRGS::FParameters* PassParameters = GraphBuilder.AllocParameters<FLightmapPathTracingRGS::FParameters>();
									PassParameters->LastInvalidationFrame = LastInvalidationFrame;
									PassParameters->NumTotalSamples = Scene->Settings->GISamples;
									PassParameters->TLAS = Scene->RayTracingScene->GetShaderResourceView();
									PassParameters->GBufferWorldPosition = GBufferWorldPosition;
									PassParameters->GBufferWorldNormal = GBufferWorldNormal;
									PassParameters->GBufferShadingNormal = GBufferShadingNormal;
									PassParameters->IrradianceAndSampleCount = GraphBuilder.CreateUAV(IrradianceAndSampleCount);
									PassParameters->SHCorrectionAndStationarySkyLightBentNormal = GraphBuilder.CreateUAV(SHCorrectionAndStationarySkyLightBentNormal);
									PassParameters->SHDirectionality = GraphBuilder.CreateUAV(SHDirectionality);

									if (bUseFirstBounceRayGuiding)
									{
										PassParameters->RayGuidingLuminance = GraphBuilder.CreateUAV(RayGuidingLuminance);
										PassParameters->RayGuidingCDFX = RayGuidingCDFX;
										PassParameters->RayGuidingCDFY = RayGuidingCDFY;
										PassParameters->NumRayGuidingTrialSamples = NumFirstBounceRayGuidingTrialSamples;
									}

									PassParameters->BatchedTiles = GPUBatchedTileRequests.BatchedTilesSRV;
									PassParameters->ViewUniformBuffer = Scene->ReferenceView->ViewUniformBuffer;
									PassParameters->IrradianceCachingParameters = Scene->IrradianceCache->IrradianceCachingParametersUniformBuffer;

									{
										SetupPathTracingLightParameters(Scene->LightSceneRenderState, GraphBuilder, &PassParameters->SceneLights, &PassParameters->SceneLightCount);
									}

									{
										SkyLightDataUniformBuffer = CreateUniformBufferImmediate(SetupSkyLightParameters(Scene->LightSceneRenderState), EUniformBufferUsage::UniformBuffer_SingleFrame);
										PassParameters->SkyLight = SkyLightDataUniformBuffer;
									}

									// TODO: find a way to share IES atlas with path tracer ...
									PassParameters->IESTexture = GWhiteTexture->TextureRHI;
									PassParameters->IESTextureSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();

									FLightmapPathTracingRGS::FPermutationDomain PermutationVector;
									PermutationVector.Set<FLightmapPathTracingRGS::FUseFirstBounceRayGuiding>(bUseFirstBounceRayGuiding);
									PermutationVector.Set<FLightmapPathTracingRGS::FUseIrradianceCaching>(Scene->Settings->bUseIrradianceCaching);
									auto RayGenerationShader = GetGlobalShaderMap(GMaxRHIFeatureLevel)->GetShader<FLightmapPathTracingRGS>(PermutationVector);
									ClearUnusedGraphResources(RayGenerationShader, PassParameters);

									GraphBuilder.AddPass(
										RDG_EVENT_NAME("LightmapPathTracing %dx%d", RayTracingResolution.X, RayTracingResolution.Y),
										PassParameters,
										ERDGPassFlags::Compute,
										[PassParameters, this, RayTracingScene = Scene->RayTracingScene, PipelineState = Scene->RayTracingPipelineState, RayGenerationShader, RayTracingResolution, GPUIndex](FRHICommandList& RHICmdList)
									{
										FRayTracingShaderBindingsWriter GlobalResources;
										SetShaderParameters(GlobalResources, RayGenerationShader, *PassParameters);

										check(RHICmdList.GetGPUMask().HasSingleIndex());

										RHICmdList.RayTraceDispatch(PipelineState, RayGenerationShader.GetRayTracingShader(), RayTracingScene, GlobalResources, RayTracingResolution.X, RayTracingResolution.Y);
									});
								}

								if (bUseFirstBounceRayGuiding)
								{
									FFirstBounceRayGuidingCDFBuildCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FFirstBounceRayGuidingCDFBuildCS::FParameters>();

									PassParameters->BatchedTiles = GPUBatchedTileRequests.BatchedTilesSRV;
									PassParameters->RayGuidingLuminance = GraphBuilder.CreateUAV(RayGuidingLuminance);
									PassParameters->RayGuidingCDFX = GraphBuilder.CreateUAV(RayGuidingCDFX);
									PassParameters->RayGuidingCDFY = GraphBuilder.CreateUAV(RayGuidingCDFY);
									PassParameters->NumRayGuidingTrialSamples = NumFirstBounceRayGuidingTrialSamples;

									TShaderMapRef<FFirstBounceRayGuidingCDFBuildCS> ComputeShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
									FComputeShaderUtils::AddPass(
										GraphBuilder,
										RDG_EVENT_NAME("FirstBounceRayGuidingCDFBuild"),
										ComputeShader,
										PassParameters,
										FIntVector(GPUBatchedTileRequests.BatchedTilesDesc.Num() * 256, 1, 1));
								}
							}
						}
					}
				}
#endif
			}
		}
	}

	for (int32 SampleIndex = 0; SampleIndex < NumSamplesPerFrame; SampleIndex++)
	{
		// Render shadow mask
		{
			TArray<FLightmapTileRequest> PendingShadowTileRequestsOnAllGPUs = PendingTileRequests.FilterByPredicate([NumShadowSamples = Scene->Settings->StationaryLightShadowSamples](const FLightmapTileRequest& Tile) { return !Tile.RenderState->IsTileShadowConverged(Tile.VirtualCoordinates, NumShadowSamples); });

			if (PendingShadowTileRequestsOnAllGPUs.Num() > 0)
			{
				for (uint32 GPUIndex = 0; GPUIndex < GNumExplicitGPUsForRendering; GPUIndex++)
				{
					RDG_GPU_MASK_SCOPE(GraphBuilder, FRHIGPUMask::FromIndex(GPUIndex));

					auto& PendingShadowTileRequests = *GraphBuilder.AllocObject<TArray<FLightmapTileRequest>>(
						PendingShadowTileRequestsOnAllGPUs.FilterByPredicate(
						[GPUIndex](const FLightmapTileRequest& Tile)
					{
						uint32 AssignedGPUIndex = (Tile.RenderState->DistributionPrefixSum + Tile.RenderState->RetrieveTileStateIndex(Tile.VirtualCoordinates)) % GNumExplicitGPUsForRendering;
						return AssignedGPUIndex == GPUIndex;
					}));

					if (PendingShadowTileRequests.Num() == 0) continue;

					for (int ScratchLayerIndex = 0; ScratchLayerIndex < 3; ScratchLayerIndex++)
					{
						TResourceArray<FIntPoint> TilePositionsToClear;
						for (auto& Tile : PendingShadowTileRequests)
						{
							TilePositionsToClear.Add(ScratchTilePoolGPU->GetPositionFromLinearAddress(Tile.TileAddressInScratch));
						}

						FRWBuffer TilePositionsBuffer;
						TilePositionsBuffer.Initialize(TEXT("TilePositionsBufferForClear"), TilePositionsToClear.GetTypeSize(), TilePositionsToClear.Num(), PF_R32G32_UINT, 0, &TilePositionsToClear);

						FMultiTileClearCS::FParameters* Parameters = GraphBuilder.AllocParameters<FMultiTileClearCS::FParameters>();
						Parameters->NumTiles = TilePositionsToClear.Num();
						Parameters->TileSize = GPreviewLightmapPhysicalTileSize;
						Parameters->TilePositions = HoldReference(GraphBuilder, TilePositionsBuffer.SRV);
						Parameters->TilePool = ScratchTilePoolLayerUAVs[ScratchLayerIndex];

						TShaderMapRef<FMultiTileClearCS> ComputeShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
						FComputeShaderUtils::AddPass(
							GraphBuilder,
							RDG_EVENT_NAME("MultiTileClear"),
							ComputeShader,
							Parameters,
							FComputeShaderUtils::GetGroupCount(FIntPoint(GPreviewLightmapPhysicalTileSize * TilePositionsToClear.Num(), GPreviewLightmapPhysicalTileSize), FComputeShaderUtils::kGolden2DGroupSize));
					}

					FRDGTextureRef GBufferWorldPosition = GraphBuilder.RegisterExternalTexture(ScratchTilePoolGPU->PooledRenderTargets[0], TEXT("GBufferWorldPosition"));
					FRDGTextureRef GBufferWorldNormal = GraphBuilder.RegisterExternalTexture(ScratchTilePoolGPU->PooledRenderTargets[1], TEXT("GBufferWorldNormal"));
					FRDGTextureRef GBufferShadingNormal = GraphBuilder.RegisterExternalTexture(ScratchTilePoolGPU->PooledRenderTargets[2], TEXT("GBufferShadingNormal"));

					FRDGTextureRef ShadowMask = GraphBuilder.RegisterExternalTexture(LightmapTilePoolGPU.PooledRenderTargets[2], TEXT("ShadowMask"));
					FRDGTextureRef ShadowMaskSampleCount = GraphBuilder.RegisterExternalTexture(LightmapTilePoolGPU.PooledRenderTargets[3], TEXT("ShadowMaskSampleCount"));

					TResourceArray<int32> LightTypeArray;
					FBufferRHIRef LightTypeBuffer;
					FShaderResourceViewRHIRef LightTypeSRV;

					TResourceArray<int32> ChannelIndexArray;
					FBufferRHIRef ChannelIndexBuffer;
					FShaderResourceViewRHIRef ChannelIndexSRV;

					auto& LightSampleIndexArray = *GraphBuilder.AllocObject<TResourceArray<int32>>();
					FBufferRHIRef LightSampleIndexBuffer;
					FShaderResourceViewRHIRef LightSampleIndexSRV;

					TResourceArray<FLightShaderConstants> LightShaderParameterArray;
					FBufferRHIRef LightShaderParameterBuffer;
					FShaderResourceViewRHIRef LightShaderParameterSRV;

					for (const FLightmapTileRequest& Tile : PendingShadowTileRequests)
					{
						// Gather all unconverged lights, then pick one based on RoundRobinIndex
						TArray<int32> UnconvergedLightTypeArray;
						TArray<int32> UnconvergedChannelIndexArray;
						TArray<int32> UnconvergedLightSampleIndexArray;
						TArray<FLightShaderConstants> UnconvergedLightShaderParameterArray;

						for (auto& Pair : Tile.RenderState->RetrieveTileRelevantLightSampleState(Tile.VirtualCoordinates).RelevantDirectionalLightSampleCount)
						{
							if (Pair.Value < Scene->Settings->StationaryLightShadowSamples)
							{
								UnconvergedLightTypeArray.Add(0);
								UnconvergedChannelIndexArray.Add(Pair.Key->ShadowMapChannel);
								UnconvergedLightShaderParameterArray.Add(FLightShaderConstants(Pair.Key->GetLightShaderParameters()));
								UnconvergedLightSampleIndexArray.Add(Pair.Value);
							}
						}

						for (auto& Pair : Tile.RenderState->RetrieveTileRelevantLightSampleState(Tile.VirtualCoordinates).RelevantPointLightSampleCount)
						{
							if (Pair.Value < Scene->Settings->StationaryLightShadowSamples)
							{
								UnconvergedLightTypeArray.Add(1);
								UnconvergedChannelIndexArray.Add(Pair.Key->ShadowMapChannel);
								UnconvergedLightShaderParameterArray.Add(FLightShaderConstants(Pair.Key->GetLightShaderParameters()));
								UnconvergedLightSampleIndexArray.Add(Pair.Value);
							}
						}

						for (auto& Pair : Tile.RenderState->RetrieveTileRelevantLightSampleState(Tile.VirtualCoordinates).RelevantSpotLightSampleCount)
						{
							if (Pair.Value < Scene->Settings->StationaryLightShadowSamples)
							{
								UnconvergedLightTypeArray.Add(2);
								UnconvergedChannelIndexArray.Add(Pair.Key->ShadowMapChannel);
								UnconvergedLightShaderParameterArray.Add(FLightShaderConstants(Pair.Key->GetLightShaderParameters()));
								UnconvergedLightSampleIndexArray.Add(Pair.Value);
							}
						}

						for (auto& Pair : Tile.RenderState->RetrieveTileRelevantLightSampleState(Tile.VirtualCoordinates).RelevantRectLightSampleCount)
						{
							if (Pair.Value < Scene->Settings->StationaryLightShadowSamples)
							{
								UnconvergedLightTypeArray.Add(3);
								UnconvergedChannelIndexArray.Add(Pair.Key->ShadowMapChannel);
								UnconvergedLightShaderParameterArray.Add(FLightShaderConstants(Pair.Key->GetLightShaderParameters()));
								UnconvergedLightSampleIndexArray.Add(Pair.Value);
							}
						}

						int32 PickedLightIndex = Tile.RenderState->RetrieveTileRelevantLightSampleState(Tile.VirtualCoordinates).RoundRobinIndex % UnconvergedLightTypeArray.Num();

						LightTypeArray.Add(UnconvergedLightTypeArray[PickedLightIndex]);
						ChannelIndexArray.Add(UnconvergedChannelIndexArray[PickedLightIndex]);
						LightSampleIndexArray.Add(UnconvergedLightSampleIndexArray[PickedLightIndex]);
						LightShaderParameterArray.Add(UnconvergedLightShaderParameterArray[PickedLightIndex]);

						Tile.RenderState->RetrieveTileRelevantLightSampleState(Tile.VirtualCoordinates).RoundRobinIndex++;

						{
							int32 LightIndex = 0;
							bool bFoundPickedLight = false;

							for (auto& Pair : Tile.RenderState->RetrieveTileRelevantLightSampleState(Tile.VirtualCoordinates).RelevantDirectionalLightSampleCount)
							{
								if (Pair.Value < Scene->Settings->StationaryLightShadowSamples)
								{
									if (LightIndex == PickedLightIndex)
									{
										Pair.Value++;
										bFoundPickedLight = true;
										break;
									}
									LightIndex++;
								}
							}

							for (auto& Pair : Tile.RenderState->RetrieveTileRelevantLightSampleState(Tile.VirtualCoordinates).RelevantPointLightSampleCount)
							{
								if (Pair.Value < Scene->Settings->StationaryLightShadowSamples)
								{
									if (LightIndex == PickedLightIndex)
									{
										Pair.Value++;
										bFoundPickedLight = true;
										break;
									}
									LightIndex++;
								}
							}

							for (auto& Pair : Tile.RenderState->RetrieveTileRelevantLightSampleState(Tile.VirtualCoordinates).RelevantSpotLightSampleCount)
							{
								if (Pair.Value < Scene->Settings->StationaryLightShadowSamples)
								{
									if (LightIndex == PickedLightIndex)
									{
										Pair.Value++;
										bFoundPickedLight = true;
										break;
									}
									LightIndex++;
								}
							}

							for (auto& Pair : Tile.RenderState->RetrieveTileRelevantLightSampleState(Tile.VirtualCoordinates).RelevantRectLightSampleCount)
							{
								if (Pair.Value < Scene->Settings->StationaryLightShadowSamples)
								{
									if (LightIndex == PickedLightIndex)
									{
										Pair.Value++;
										bFoundPickedLight = true;
										break;
									}
									LightIndex++;
								}
							}

							check(bFoundPickedLight);
						}
					}

					check(PendingShadowTileRequests.Num() == LightTypeArray.Num());

					{
						FRHIResourceCreateInfo CreateInfo(TEXT("LightTypeBuffer"), &LightTypeArray);
						LightTypeBuffer = RHICreateVertexBuffer(LightTypeArray.GetResourceDataSize(), BUF_Static | BUF_ShaderResource, CreateInfo);
						LightTypeSRV = RHICreateShaderResourceView(LightTypeBuffer, sizeof(int32), PF_R32_SINT);
					}

					{
						FRHIResourceCreateInfo CreateInfo(TEXT("ChannelIndexBuffer"), &ChannelIndexArray);
						ChannelIndexBuffer = RHICreateVertexBuffer(ChannelIndexArray.GetResourceDataSize(), BUF_Static | BUF_ShaderResource, CreateInfo);
						ChannelIndexSRV = RHICreateShaderResourceView(ChannelIndexBuffer, sizeof(int32), PF_R32_SINT);
					}

					{
						FRHIResourceCreateInfo CreateInfo(TEXT("LightSampleIndexSRV"), &LightSampleIndexArray);
						LightSampleIndexBuffer = RHICreateVertexBuffer(LightSampleIndexArray.GetResourceDataSize(), BUF_Static | BUF_ShaderResource, CreateInfo);
						LightSampleIndexSRV = RHICreateShaderResourceView(LightSampleIndexBuffer, sizeof(int32), PF_R32_SINT);
					}

					{
						FRHIResourceCreateInfo CreateInfo(TEXT("LightShaderParameterBuffer"), &LightShaderParameterArray);
						LightShaderParameterBuffer = RHICreateStructuredBuffer(sizeof(FLightShaderConstants), LightShaderParameterArray.GetResourceDataSize(), BUF_Static | BUF_ShaderResource, CreateInfo);
						LightShaderParameterSRV = RHICreateShaderResourceView(LightShaderParameterBuffer);
					}

					// Render GBuffer
					{
						auto* PassParameters = GraphBuilder.AllocParameters<FLightmapGBufferPassParameters>();
						PassParameters->PassUniformBuffer = PassUniformBuffer;

						GraphBuilder.AddPass(
							RDG_EVENT_NAME("LightmapGBuffer"),
							PassParameters,
							ERDGPassFlags::Raster,
							[this, ReferenceView = Scene->ReferenceView, &PendingShadowTileRequests, &LightSampleIndexArray](FRHICommandList& RHICmdList)
						{
							for (int32 TileIndex = 0; TileIndex < PendingShadowTileRequests.Num(); TileIndex++)
							{
								const FLightmapTileRequest& Tile = PendingShadowTileRequests[TileIndex];

								RHICmdList.SetViewport(0, 0, 0.0f, GPreviewLightmapPhysicalTileSize, GPreviewLightmapPhysicalTileSize, 1.0f);

								float ScaleX = Tile.RenderState->GetPaddedSizeInTiles().X * GPreviewLightmapVirtualTileSize * 1.0f / (1 << Tile.VirtualCoordinates.MipLevel) / GPreviewLightmapPhysicalTileSize;
								float ScaleY = Tile.RenderState->GetPaddedSizeInTiles().Y * GPreviewLightmapVirtualTileSize * 1.0f / (1 << Tile.VirtualCoordinates.MipLevel) / GPreviewLightmapPhysicalTileSize;
								float BiasX = (1.0f * (-Tile.VirtualCoordinates.Position.X * GPreviewLightmapVirtualTileSize) - (-GPreviewLightmapTileBorderSize)) / GPreviewLightmapPhysicalTileSize;
								float BiasY = (1.0f * (-Tile.VirtualCoordinates.Position.Y * GPreviewLightmapVirtualTileSize) - (-GPreviewLightmapTileBorderSize)) / GPreviewLightmapPhysicalTileSize;

								FVector4 VirtualTexturePhysicalTileCoordinateScaleAndBias = FVector4(ScaleX, ScaleY, BiasX, BiasY);

								TArray<FMeshBatch> MeshBatches = Tile.RenderState->GeometryInstanceRef.GetMeshBatchesForGBufferRendering(Tile.VirtualCoordinates);

								for (auto& MeshBatch : MeshBatches)
								{
									FMeshBatchElement& Element = MeshBatch.Elements[0];

									Element.DynamicPrimitiveShaderDataIndex = Tile.RenderState->GeometryInstanceRef.GetElementId();
								}

								DrawDynamicMeshPass(
									*ReferenceView, RHICmdList,
									[
										View = ReferenceView.Get(),
										MeshBatches,
										VirtualTexturePhysicalTileCoordinateScaleAndBias,
										RenderPassIndex = LightSampleIndexArray[TileIndex],
										ScratchTilePoolOffset = ScratchTilePoolGPU->GetPositionFromLinearAddress(Tile.TileAddressInScratch) * GPreviewLightmapPhysicalTileSize
									](FDynamicPassMeshDrawListContext* DynamicMeshPassContext)
								{
									FLightmapGBufferMeshProcessor MeshProcessor(nullptr, View, DynamicMeshPassContext, VirtualTexturePhysicalTileCoordinateScaleAndBias, RenderPassIndex, ScratchTilePoolOffset);

									for (auto& MeshBatch : MeshBatches)
									{
										MeshProcessor.AddMeshBatch(MeshBatch, ~0ull, nullptr);
									}
								});

								GPrimitiveIdVertexBufferPool.DiscardAll();
							}
						});
					}

#if RHI_RAYTRACING
					if (IsRayTracingEnabled())
					{
						FGPUBatchedTileRequests GPUBatchedTileRequests;

						{
							for (int32 TileIndex = 0; TileIndex < PendingShadowTileRequests.Num(); TileIndex++)
							{
								const FLightmapTileRequest& Tile = PendingShadowTileRequests[TileIndex];
								FGPUTileDescription TileDesc;
								TileDesc.LightmapSize = Tile.RenderState->GetSize();
								TileDesc.VirtualTilePosition = Tile.VirtualCoordinates.Position * GPreviewLightmapVirtualTileSize;
								TileDesc.WorkingSetPosition = LightmapTilePoolGPU.GetPositionFromLinearAddress(Tile.TileAddressInWorkingSet) * GPreviewLightmapPhysicalTileSize;
								TileDesc.ScratchPosition = ScratchTilePoolGPU->GetPositionFromLinearAddress(Tile.TileAddressInScratch) * GPreviewLightmapPhysicalTileSize;
								TileDesc.OutputLayer0Position = Tile.OutputPhysicalCoordinates[0] * GPreviewLightmapPhysicalTileSize;
								TileDesc.OutputLayer1Position = Tile.OutputPhysicalCoordinates[1] * GPreviewLightmapPhysicalTileSize;
								TileDesc.OutputLayer2Position = Tile.OutputPhysicalCoordinates[2] * GPreviewLightmapPhysicalTileSize;
								TileDesc.FrameIndex = Tile.RenderState->RetrieveTileState(Tile.VirtualCoordinates).Revision;
								TileDesc.RenderPassIndex = LightSampleIndexArray[TileIndex];
								GPUBatchedTileRequests.BatchedTilesDesc.Add(TileDesc);
							}

							{
								FRHIResourceCreateInfo CreateInfo(TEXT("BatchedTilesBuffer"));
								CreateInfo.ResourceArray = &GPUBatchedTileRequests.BatchedTilesDesc;

								GPUBatchedTileRequests.BatchedTilesBuffer = RHICreateStructuredBuffer(sizeof(FGPUTileDescription), GPUBatchedTileRequests.BatchedTilesDesc.GetResourceDataSize(), BUF_Static | BUF_ShaderResource, CreateInfo);
								GPUBatchedTileRequests.BatchedTilesSRV = HoldReference(GraphBuilder, RHICreateShaderResourceView(GPUBatchedTileRequests.BatchedTilesBuffer));
							}
						}

						FIntPoint RayTracingResolution;
						RayTracingResolution.X = GPreviewLightmapPhysicalTileSize * GPUBatchedTileRequests.BatchedTilesDesc.Num();
						RayTracingResolution.Y = GPreviewLightmapPhysicalTileSize;

						FStationaryLightShadowTracingRGS::FParameters* PassParameters = GraphBuilder.AllocParameters<FStationaryLightShadowTracingRGS::FParameters>();
						PassParameters->TLAS = Scene->RayTracingScene->GetShaderResourceView();
						PassParameters->BatchedTiles = GPUBatchedTileRequests.BatchedTilesSRV;
						PassParameters->LightTypeArray = HoldReference(GraphBuilder, LightTypeSRV);
						PassParameters->ChannelIndexArray = HoldReference(GraphBuilder, ChannelIndexSRV);
						PassParameters->LightSampleIndexArray = HoldReference(GraphBuilder, LightSampleIndexSRV);
						PassParameters->LightShaderParametersArray = HoldReference(GraphBuilder, LightShaderParameterSRV);
						PassParameters->GBufferWorldPosition = GBufferWorldPosition;
						PassParameters->GBufferWorldNormal = GBufferWorldNormal;
						PassParameters->GBufferShadingNormal = GBufferShadingNormal;
						PassParameters->ShadowMask = GraphBuilder.CreateUAV(ShadowMask);
						PassParameters->ShadowMaskSampleCount = GraphBuilder.CreateUAV(ShadowMaskSampleCount);

						auto RayGenerationShader = GetGlobalShaderMap(GMaxRHIFeatureLevel)->GetShader<FStationaryLightShadowTracingRGS>();
						ClearUnusedGraphResources(RayGenerationShader, PassParameters);

						GraphBuilder.AddPass(
							RDG_EVENT_NAME("StationaryLightShadowTracing %dx%d", RayTracingResolution.X, RayTracingResolution.Y),
							PassParameters,
							ERDGPassFlags::Compute,
							[PassParameters, this, RayTracingScene = Scene->RayTracingScene, PipelineState = Scene->RayTracingPipelineState, RayGenerationShader, RayTracingResolution](FRHICommandList& RHICmdList)
						{
							FRayTracingShaderBindingsWriter GlobalResources;
							SetShaderParameters(GlobalResources, RayGenerationShader, *PassParameters);

							RHICmdList.RayTraceDispatch(PipelineState, RayGenerationShader.GetRayTracingShader(), RayTracingScene, GlobalResources, RayTracingResolution.X, RayTracingResolution.Y);
						});
					}
#endif
				}
			}
		}
	}

	// Pull results from other GPUs using batched transfer if realtime
	if (!bInsideBackgroundTick)
	{
		TArray<FTransferTextureParams> Params;

		for (const FLightmapTileRequest& Tile : PendingTileRequests)
		{
			uint32 AssignedGPUIndex = (Tile.RenderState->DistributionPrefixSum + Tile.RenderState->RetrieveTileStateIndex(Tile.VirtualCoordinates)) % GNumExplicitGPUsForRendering;
			if (AssignedGPUIndex != 0)
			{
				auto TransferTexture = [&](int32 RenderTargetIndex) {
					FIntRect GPURect;
					GPURect.Min = LightmapTilePoolGPU.GetPositionFromLinearAddress(Tile.TileAddressInWorkingSet) * LightmapTilePoolGPU.LayerFormatAndTileSize[RenderTargetIndex].TileSize;
					GPURect.Max = GPURect.Min + LightmapTilePoolGPU.LayerFormatAndTileSize[RenderTargetIndex].TileSize;
					Params.Add(FTransferTextureParams(LightmapTilePoolGPU.PooledRenderTargets[RenderTargetIndex]->GetRenderTargetItem().TargetableTexture->GetTexture2D(), GPURect, AssignedGPUIndex, 0, true, true));
				};

				TransferTexture(0);
				TransferTexture(1);
				TransferTexture(2);
				TransferTexture(3);
				TransferTexture(4);

				if (bUseFirstBounceRayGuiding)
				{
					TransferTexture(5);
					TransferTexture(6);
					TransferTexture(7);
				}
			}
		}

		AddPass(GraphBuilder, [LocalParams = MoveTemp(Params)](FRHICommandListImmediate& RHICmdList)
		{
			RHICmdList.TransferTextures(LocalParams);
		});
	}

	// Output from working set to VT layers
	{
		FGPUBatchedTileRequests GPUBatchedTileRequests;

		{
			for (const FLightmapTileRequest& Tile : PendingTileRequests)
			{
				FGPUTileDescription TileDesc;
				TileDesc.LightmapSize = Tile.RenderState->GetSize();
				TileDesc.VirtualTilePosition = Tile.VirtualCoordinates.Position * GPreviewLightmapVirtualTileSize;
				TileDesc.WorkingSetPosition = LightmapTilePoolGPU.GetPositionFromLinearAddress(Tile.TileAddressInWorkingSet) * GPreviewLightmapPhysicalTileSize;
				TileDesc.ScratchPosition = ScratchTilePoolGPU->GetPositionFromLinearAddress(Tile.TileAddressInScratch) * GPreviewLightmapPhysicalTileSize;
				TileDesc.OutputLayer0Position = Tile.OutputPhysicalCoordinates[0] * GPreviewLightmapPhysicalTileSize;
				TileDesc.OutputLayer1Position = Tile.OutputPhysicalCoordinates[1] * GPreviewLightmapPhysicalTileSize;
				TileDesc.OutputLayer2Position = Tile.OutputPhysicalCoordinates[2] * GPreviewLightmapPhysicalTileSize;
				TileDesc.FrameIndex = Tile.RenderState->RetrieveTileState(Tile.VirtualCoordinates).Revision;
				TileDesc.RenderPassIndex = Tile.RenderState->RetrieveTileState(Tile.VirtualCoordinates).RenderPassIndex;
				GPUBatchedTileRequests.BatchedTilesDesc.Add(TileDesc);
			}

			FRHIResourceCreateInfo CreateInfo(TEXT("BatchedTilesBuffer"));
			CreateInfo.ResourceArray = &GPUBatchedTileRequests.BatchedTilesDesc;

			GPUBatchedTileRequests.BatchedTilesBuffer = RHICreateStructuredBuffer(sizeof(FGPUTileDescription), GPUBatchedTileRequests.BatchedTilesDesc.GetResourceDataSize(), BUF_Static | BUF_ShaderResource, CreateInfo);
			GPUBatchedTileRequests.BatchedTilesSRV = RHICreateShaderResourceView(GPUBatchedTileRequests.BatchedTilesBuffer);
		}

		{
			FRDGTextureRef IrradianceAndSampleCount = GraphBuilder.RegisterExternalTexture(LightmapTilePoolGPU.PooledRenderTargets[0], TEXT("IrradianceAndSampleCount"));
			FRDGTextureRef SHDirectionality = GraphBuilder.RegisterExternalTexture(LightmapTilePoolGPU.PooledRenderTargets[1], TEXT("SHDirectionality"));
			FRDGTextureRef ShadowMask = GraphBuilder.RegisterExternalTexture(LightmapTilePoolGPU.PooledRenderTargets[2], TEXT("ShadowMask"));
			FRDGTextureRef ShadowMaskSampleCount = GraphBuilder.RegisterExternalTexture(LightmapTilePoolGPU.PooledRenderTargets[3], TEXT("ShadowMaskSampleCount"));
			FRDGTextureRef SHCorrectionAndStationarySkyLightBentNormal = GraphBuilder.RegisterExternalTexture(LightmapTilePoolGPU.PooledRenderTargets[4], TEXT("SHCorrectionAndStationarySkyLightBentNormal"));

			FIntPoint RayTracingResolution;
			RayTracingResolution.X = GPreviewLightmapPhysicalTileSize * GPUBatchedTileRequests.BatchedTilesDesc.Num();
			RayTracingResolution.Y = GPreviewLightmapPhysicalTileSize;

			if (OutputRenderTargets[0] != nullptr || OutputRenderTargets[1] != nullptr)
			{
				FRDGTextureRef RenderTargetTileAtlas = GraphBuilder.RegisterExternalTexture(OutputRenderTargets[0] != nullptr ? OutputRenderTargets[0] : OutputRenderTargets[1], TEXT("GPULightmassRenderTargetTileAtlas0"));

				FSelectiveLightmapOutputCS::FPermutationDomain PermutationVector;
				PermutationVector.Set<FSelectiveLightmapOutputCS::FOutputLayerDim>(0);
				PermutationVector.Set<FSelectiveLightmapOutputCS::FDrawProgressBars>(Scene->Settings->bShowProgressBars);

				auto Shader = GetGlobalShaderMap(GMaxRHIFeatureLevel)->GetShader<FSelectiveLightmapOutputCS>(PermutationVector);

				FSelectiveLightmapOutputCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FSelectiveLightmapOutputCS::FParameters>();
				PassParameters->NumBatchedTiles = GPUBatchedTileRequests.BatchedTilesDesc.Num();
				PassParameters->NumTotalSamples = Scene->Settings->GISamples;
				PassParameters->NumRayGuidingTrialSamples = NumFirstBounceRayGuidingTrialSamples;
				PassParameters->BatchedTiles = GPUBatchedTileRequests.BatchedTilesSRV;
				PassParameters->OutputTileAtlas = GraphBuilder.CreateUAV(RenderTargetTileAtlas);
				PassParameters->IrradianceAndSampleCount = GraphBuilder.CreateUAV(IrradianceAndSampleCount);
				PassParameters->SHDirectionality = GraphBuilder.CreateUAV(SHDirectionality);
				PassParameters->SHCorrectionAndStationarySkyLightBentNormal = GraphBuilder.CreateUAV(SHCorrectionAndStationarySkyLightBentNormal);

				FComputeShaderUtils::AddPass(
					GraphBuilder,
					RDG_EVENT_NAME("SelectiveLightmapOutput 0"),
					Shader,
					PassParameters,
					FComputeShaderUtils::GetGroupCount(RayTracingResolution, FComputeShaderUtils::kGolden2DGroupSize));
			}

			if (OutputRenderTargets[2] != nullptr)
			{
				FRDGTextureRef RenderTargetTileAtlas = GraphBuilder.RegisterExternalTexture(OutputRenderTargets[2], TEXT("GPULightmassRenderTargetTileAtlas2"));

				FSelectiveLightmapOutputCS::FPermutationDomain PermutationVector;
				PermutationVector.Set<FSelectiveLightmapOutputCS::FOutputLayerDim>(2);
				PermutationVector.Set<FSelectiveLightmapOutputCS::FDrawProgressBars>(Scene->Settings->bShowProgressBars);

				auto Shader = GetGlobalShaderMap(GMaxRHIFeatureLevel)->GetShader<FSelectiveLightmapOutputCS>(PermutationVector);

				FSelectiveLightmapOutputCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FSelectiveLightmapOutputCS::FParameters>();
				PassParameters->NumBatchedTiles = GPUBatchedTileRequests.BatchedTilesDesc.Num();
				PassParameters->NumTotalSamples = Scene->Settings->GISamples;
				PassParameters->BatchedTiles = GPUBatchedTileRequests.BatchedTilesSRV;
				PassParameters->OutputTileAtlas = GraphBuilder.CreateUAV(RenderTargetTileAtlas);
				PassParameters->ShadowMask = GraphBuilder.CreateUAV(ShadowMask);
				PassParameters->ShadowMaskSampleCount = GraphBuilder.CreateUAV(ShadowMaskSampleCount);

				FComputeShaderUtils::AddPass(
					GraphBuilder,
					RDG_EVENT_NAME("SelectiveLightmapOutput 2"),
					Shader,
					PassParameters,
					FComputeShaderUtils::GetGroupCount(RayTracingResolution, FComputeShaderUtils::kGolden2DGroupSize));
			}
		}
	}

	AddPass(GraphBuilder, [this](FRHICommandList&)
	{
		Scene->DestroyRayTracingScene();
	});

	// Perform readback on any potential converged tiles
	{
		auto ConvergedTileRequests = PendingTileRequests.FilterByPredicate(
			[
				NumGISamples = Scene->Settings->GISamples,
				NumShadowSamples = Scene->Settings->StationaryLightShadowSamples,
				bOnlyBakeWhatYouSee = bOnlyBakeWhatYouSee, 
				bDenoiseDuringInteractiveBake = bDenoiseDuringInteractiveBake
			](const FLightmapTileRequest& TileRequest)
		{
			return
				(TileRequest.VirtualCoordinates.MipLevel == 0 || bDenoiseDuringInteractiveBake || bOnlyBakeWhatYouSee) && // Only mip 0 tiles will be saved
				TileRequest.RenderState->IsTileGIConverged(TileRequest.VirtualCoordinates, NumGISamples) && TileRequest.RenderState->IsTileShadowConverged(TileRequest.VirtualCoordinates, NumShadowSamples);
		}
		);

		if (ConvergedTileRequests.Num() > 0)
		{
			int32 NewSize = FMath::CeilToInt(FMath::Sqrt(static_cast<float>(ConvergedTileRequests.Num())));

			for (const FLightmapTileRequest& Tile : ConvergedTileRequests)
			{
				Tile.RenderState->RetrieveTileState(Tile.VirtualCoordinates).OngoingReadbackRevision = CurrentRevision;
			}

			for (uint32 GPUIndex = 0; GPUIndex < GNumExplicitGPUsForRendering; GPUIndex++)
			{
				RDG_GPU_MASK_SCOPE(GraphBuilder, FRHIGPUMask::FromIndex(GPUIndex));

				auto ConvergedTileRequestsOnCurrentGPU = ConvergedTileRequests.FilterByPredicate(
					[GPUIndex](const FLightmapTileRequest& Tile)
				{
					uint32 AssignedGPUIndex = (Tile.RenderState->DistributionPrefixSum + Tile.RenderState->RetrieveTileStateIndex(Tile.VirtualCoordinates)) % GNumExplicitGPUsForRendering;
					return AssignedGPUIndex == GPUIndex;
				});

				if (ConvergedTileRequestsOnCurrentGPU.Num() == 0) continue;

				FLightmapReadbackGroup* ReadbackGroupToUse = nullptr;

				for (TUniquePtr<FLightmapReadbackGroup>& ReadbackGroup : RecycledReadbacks)
				{
					if (ReadbackGroup->bIsFree && ReadbackGroup->ReadbackTilePoolGPU->SizeInTiles.X >= NewSize)
					{
						ReadbackGroupToUse = ReadbackGroup.Get();
						break;
					}
				}

				if (ReadbackGroupToUse == nullptr)
				{
					int32 NewIndex = RecycledReadbacks.Add(MakeUnique<FLightmapReadbackGroup>());
					ReadbackGroupToUse = RecycledReadbacks[NewIndex].Get();
				}

				FLightmapReadbackGroup& LightmapReadbackGroup = *ReadbackGroupToUse;
				LightmapReadbackGroup.bIsFree = false;
				LightmapReadbackGroup.Revision = CurrentRevision;
				LightmapReadbackGroup.GPUIndex = GPUIndex;
				LightmapReadbackGroup.ConvergedTileRequests = ConvergedTileRequestsOnCurrentGPU;
				if (!LightmapReadbackGroup.ReadbackTilePoolGPU.IsValid())
				{
					LightmapReadbackGroup.ReadbackTilePoolGPU = MakeUnique<FLightmapTilePoolGPU>(3, FIntPoint(NewSize, NewSize), FIntPoint(GPreviewLightmapPhysicalTileSize, GPreviewLightmapPhysicalTileSize));
					LightmapReadbackGroup.StagingHQLayer0Readback = MakeUnique<FRHIGPUTextureReadback>(TEXT("StagingHQLayer0Readback"));
					LightmapReadbackGroup.StagingHQLayer1Readback = MakeUnique<FRHIGPUTextureReadback>(TEXT("StagingHQLayer1Readback"));
					LightmapReadbackGroup.StagingShadowMaskReadback = MakeUnique<FRHIGPUTextureReadback>(TEXT("StagingShadowMaskReadback"));
				}

				FGPUBatchedTileRequests GPUBatchedTileRequests;

				for (const auto& Tile : LightmapReadbackGroup.ConvergedTileRequests)
				{
					uint32 AssignedGPUIndex = (Tile.RenderState->DistributionPrefixSum + Tile.RenderState->RetrieveTileStateIndex(Tile.VirtualCoordinates)) % GNumExplicitGPUsForRendering;
					check(AssignedGPUIndex == GPUIndex);

					FGPUTileDescription TileDesc;
					TileDesc.LightmapSize = Tile.RenderState->GetSize();
					TileDesc.VirtualTilePosition = Tile.VirtualCoordinates.Position * GPreviewLightmapVirtualTileSize;
					TileDesc.WorkingSetPosition = LightmapTilePoolGPU.GetPositionFromLinearAddress(Tile.TileAddressInWorkingSet) * GPreviewLightmapPhysicalTileSize;
					TileDesc.ScratchPosition = ScratchTilePoolGPU->GetPositionFromLinearAddress(Tile.TileAddressInScratch) * GPreviewLightmapPhysicalTileSize;
					TileDesc.OutputLayer0Position = Tile.OutputPhysicalCoordinates[0] * GPreviewLightmapPhysicalTileSize;
					TileDesc.OutputLayer1Position = Tile.OutputPhysicalCoordinates[1] * GPreviewLightmapPhysicalTileSize;
					TileDesc.OutputLayer2Position = Tile.OutputPhysicalCoordinates[2] * GPreviewLightmapPhysicalTileSize;
					TileDesc.FrameIndex = Tile.RenderState->RetrieveTileState(Tile.VirtualCoordinates).Revision;
					TileDesc.RenderPassIndex = Tile.RenderState->RetrieveTileState(Tile.VirtualCoordinates).RenderPassIndex;
					GPUBatchedTileRequests.BatchedTilesDesc.Add(TileDesc);
				}

				FRHIResourceCreateInfo CreateInfo(TEXT("BatchedTilesBuffer"));
				CreateInfo.ResourceArray = &GPUBatchedTileRequests.BatchedTilesDesc;

				GPUBatchedTileRequests.BatchedTilesBuffer = RHICreateStructuredBuffer(sizeof(FGPUTileDescription), GPUBatchedTileRequests.BatchedTilesDesc.GetResourceDataSize(), BUF_Static | BUF_ShaderResource, CreateInfo);
				GPUBatchedTileRequests.BatchedTilesSRV = HoldReference(GraphBuilder, RHICreateShaderResourceView(GPUBatchedTileRequests.BatchedTilesBuffer));


				FIntPoint DispatchResolution;
				DispatchResolution.X = GPreviewLightmapPhysicalTileSize * GPUBatchedTileRequests.BatchedTilesDesc.Num();
				DispatchResolution.Y = GPreviewLightmapPhysicalTileSize;

				FRDGTextureRef IrradianceAndSampleCount = GraphBuilder.RegisterExternalTexture(LightmapTilePoolGPU.PooledRenderTargets[0], TEXT("IrradianceAndSampleCount"));
				FRDGTextureRef SHDirectionality = GraphBuilder.RegisterExternalTexture(LightmapTilePoolGPU.PooledRenderTargets[1], TEXT("SHDirectionality"));
				FRDGTextureRef ShadowMask = GraphBuilder.RegisterExternalTexture(LightmapTilePoolGPU.PooledRenderTargets[2], TEXT("ShadowMask"));
				FRDGTextureRef ShadowMaskSampleCount = GraphBuilder.RegisterExternalTexture(LightmapTilePoolGPU.PooledRenderTargets[3], TEXT("ShadowMaskSampleCount"));
				FRDGTextureRef SHCorrectionAndStationarySkyLightBentNormal = GraphBuilder.RegisterExternalTexture(LightmapTilePoolGPU.PooledRenderTargets[4], TEXT("SHCorrectionAndStationarySkyLightBentNormal"));

				FRDGTextureRef StagingHQLayer0 = GraphBuilder.RegisterExternalTexture(LightmapReadbackGroup.ReadbackTilePoolGPU->PooledRenderTargets[0], TEXT("StagingHQLayer0"));
				FRDGTextureRef StagingHQLayer1 = GraphBuilder.RegisterExternalTexture(LightmapReadbackGroup.ReadbackTilePoolGPU->PooledRenderTargets[1], TEXT("StagingHQLayer1"));
				FRDGTextureRef StagingShadowMask = GraphBuilder.RegisterExternalTexture(LightmapReadbackGroup.ReadbackTilePoolGPU->PooledRenderTargets[2], TEXT("StagingShadowMask"));

				{
					FCopyConvergedLightmapTilesCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FCopyConvergedLightmapTilesCS::FParameters>();

					PassParameters->NumBatchedTiles = GPUBatchedTileRequests.BatchedTilesDesc.Num();
					PassParameters->StagingPoolSizeX = LightmapReadbackGroup.ReadbackTilePoolGPU->SizeInTiles.X;
					PassParameters->BatchedTiles = GPUBatchedTileRequests.BatchedTilesSRV;
					PassParameters->IrradianceAndSampleCount = GraphBuilder.CreateUAV(IrradianceAndSampleCount);
					PassParameters->SHDirectionality = GraphBuilder.CreateUAV(SHDirectionality);
					PassParameters->SHCorrectionAndStationarySkyLightBentNormal = GraphBuilder.CreateUAV(SHCorrectionAndStationarySkyLightBentNormal);
					PassParameters->ShadowMask = GraphBuilder.CreateUAV(ShadowMask);
					PassParameters->ShadowMaskSampleCount = GraphBuilder.CreateUAV(ShadowMaskSampleCount);
					PassParameters->StagingHQLayer0 = GraphBuilder.CreateUAV(StagingHQLayer0);
					PassParameters->StagingHQLayer1 = GraphBuilder.CreateUAV(StagingHQLayer1);
					PassParameters->StagingShadowMask = GraphBuilder.CreateUAV(StagingShadowMask);

					TShaderMapRef<FCopyConvergedLightmapTilesCS> ComputeShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
					FComputeShaderUtils::AddPass(
						GraphBuilder,
						RDG_EVENT_NAME("CopyConvergedLightmapTiles"),
						ComputeShader,
						PassParameters,
						FComputeShaderUtils::GetGroupCount(DispatchResolution, FComputeShaderUtils::kGolden2DGroupSize));
				}

				AddEnqueueCopyPass(GraphBuilder, LightmapReadbackGroup.StagingHQLayer0Readback.Get(), StagingHQLayer0);
				AddEnqueueCopyPass(GraphBuilder, LightmapReadbackGroup.StagingHQLayer1Readback.Get(), StagingHQLayer1);
				AddEnqueueCopyPass(GraphBuilder, LightmapReadbackGroup.StagingShadowMaskReadback.Get(), StagingShadowMask);

				OngoingReadbacks.Emplace(ReadbackGroupToUse);
			}
		}
	}

	PendingTileRequests.Empty();

	FrameNumber++;
}

const int32 DenoiseTileProximity = 3;

void FLightmapTileDenoiseAsyncTask::DoThreadedWork()
{
	static FDenoiserContext DenoiserContext;

	DenoiseRawData(
		Size,
		TextureData->Texture[0],
		TextureData->Texture[1],
		DenoiserContext);

	FPlatformAtomics::AtomicStore(&TextureData->bDenoisingFinished, 1);
}

void FLightmapRenderer::BackgroundTick()
{
	{
		TArray<FLightmapTileDenoiseGroup> FilteredDenoiseGroups;

		FTileDataLayer::Evict();

		for (FLightmapTileDenoiseGroup& DenoiseGroup : OngoingDenoiseGroups)
		{
			bool bPipelineFinished = false;

			if (DenoiseGroup.Revision != CurrentRevision)
			{
				bPipelineFinished = true;
				continue;
			}

			if (DenoiseGroup.bShouldBeCancelled)
			{
				if (DenoisingThreadPool->RetractQueuedWork(DenoiseGroup.AsyncDenoisingWork))
				{
					delete DenoiseGroup.AsyncDenoisingWork;
					bPipelineFinished = true;
					continue;
				}
				else
				{
					// Failed to cancel async work, proceed as usual
					DenoiseGroup.bShouldBeCancelled = false;
				}
			}

			if (FPlatformAtomics::AtomicRead(&DenoiseGroup.TextureData->bDenoisingFinished) == 1)
			{
				FLightmapTileRequest& Tile = DenoiseGroup.TileRequest;

				FIntPoint SrcTilePosition(DenoiseTileProximity / 2, DenoiseTileProximity / 2);
				FIntPoint DstTilePosition(Tile.VirtualCoordinates.Position.X, Tile.VirtualCoordinates.Position.Y);

				const int32 DstRowPitchInPixels = GPreviewLightmapVirtualTileSize;
				const int32 SrcRowPitchInPixels = DenoiseTileProximity * GPreviewLightmapVirtualTileSize;

				// While the data will be overwritten immediately, we still need to decompress to inform the LRU cache management
				Tile.RenderState->TileStorage[Tile.VirtualCoordinates].CPUTextureData[0]->Decompress();
				Tile.RenderState->TileStorage[Tile.VirtualCoordinates].CPUTextureData[1]->Decompress();

				for (int32 Y = 0; Y < GPreviewLightmapVirtualTileSize; Y++)
				{
					for (int32 X = 0; X < GPreviewLightmapVirtualTileSize; X++)
					{
						FIntPoint SrcPixelPosition = SrcTilePosition * GPreviewLightmapVirtualTileSize + FIntPoint(X, Y);
						FIntPoint DstPixelPosition = FIntPoint(X, Y);

						int32 SrcLinearIndex = SrcPixelPosition.Y * SrcRowPitchInPixels + SrcPixelPosition.X;
						int32 DstLinearIndex = DstPixelPosition.Y * DstRowPitchInPixels + DstPixelPosition.X;

						Tile.RenderState->TileStorage[Tile.VirtualCoordinates].CPUTextureData[0]->Data[DstLinearIndex] = DenoiseGroup.TextureData->Texture[0][SrcLinearIndex];
						Tile.RenderState->TileStorage[Tile.VirtualCoordinates].CPUTextureData[1]->Data[DstLinearIndex] = DenoiseGroup.TextureData->Texture[1][SrcLinearIndex];
					}
				}

				DenoiseGroup.TileRequest.RenderState->RetrieveTileState(DenoiseGroup.TileRequest.VirtualCoordinates).CPURevision = CurrentRevision;
				DenoiseGroup.TileRequest.RenderState->RetrieveTileState(DenoiseGroup.TileRequest.VirtualCoordinates).OngoingReadbackRevision = -1;

				delete DenoiseGroup.AsyncDenoisingWork;

				bPipelineFinished = true;
			}

			if (!bPipelineFinished)
			{
				FilteredDenoiseGroups.Emplace(MoveTemp(DenoiseGroup));
			}
		}

		OngoingDenoiseGroups = MoveTemp(FilteredDenoiseGroups);
	}

	TArray<FLightmapReadbackGroup*> FilteredReadbackGroups;

	TArray<FLightmapTileRequest> TilesWaitingForDenoising;

	FTileDataLayer::Evict();

	for (int32 Index = 0; Index < OngoingReadbacks.Num(); Index++)
	{
		FLightmapReadbackGroup& ReadbackGroup = *OngoingReadbacks[Index];

		if (ReadbackGroup.Revision != CurrentRevision)
		{
			continue;
		}

		bool bPipelineFinished = false;

		if (ReadbackGroup.StagingHQLayer0Readback->IsReady(FRHIGPUMask::FromIndex(ReadbackGroup.GPUIndex)) &&
			ReadbackGroup.StagingHQLayer1Readback->IsReady(FRHIGPUMask::FromIndex(ReadbackGroup.GPUIndex)) &&
			ReadbackGroup.StagingShadowMaskReadback->IsReady(FRHIGPUMask::FromIndex(ReadbackGroup.GPUIndex)))
		{
			FRHICommandListImmediate& RHICmdList = FRHICommandListExecutor::GetImmediateCommandList();
			SCOPED_GPU_MASK(RHICmdList, FRHIGPUMask::FromIndex(ReadbackGroup.GPUIndex));

			ReadbackGroup.TextureData = MakeUnique<FLightmapReadbackGroup::FTextureData>();

			ReadbackGroup.TextureData->SizeInTiles = ReadbackGroup.ReadbackTilePoolGPU->SizeInTiles;

			// FLinearColor is in RGBA while the GPU texture is in ABGR
			// TODO: apply swizzling in the copy compute shader if this becomes a problem
			void* LockedData[3];
			ReadbackGroup.StagingHQLayer0Readback->LockTexture(RHICmdList, LockedData[0], ReadbackGroup.TextureData->RowPitchInPixels[0]); // This forces a GPU stall
			ReadbackGroup.StagingHQLayer1Readback->LockTexture(RHICmdList, LockedData[1], ReadbackGroup.TextureData->RowPitchInPixels[1]); // This forces a GPU stall
			ReadbackGroup.StagingShadowMaskReadback->LockTexture(RHICmdList, LockedData[2], ReadbackGroup.TextureData->RowPitchInPixels[2]); // This forces a GPU stall

			ReadbackGroup.TextureData->Texture[0].AddZeroed(ReadbackGroup.ReadbackTilePoolGPU->SizeInTiles.Y * GPreviewLightmapPhysicalTileSize * ReadbackGroup.TextureData->RowPitchInPixels[0]);
			ReadbackGroup.TextureData->Texture[1].AddZeroed(ReadbackGroup.ReadbackTilePoolGPU->SizeInTiles.Y * GPreviewLightmapPhysicalTileSize * ReadbackGroup.TextureData->RowPitchInPixels[1]);
			ReadbackGroup.TextureData->Texture[2].AddZeroed(ReadbackGroup.ReadbackTilePoolGPU->SizeInTiles.Y * GPreviewLightmapPhysicalTileSize * ReadbackGroup.TextureData->RowPitchInPixels[2]);
			FMemory::Memcpy(ReadbackGroup.TextureData->Texture[0].GetData(), LockedData[0], ReadbackGroup.ReadbackTilePoolGPU->SizeInTiles.Y * GPreviewLightmapPhysicalTileSize * ReadbackGroup.TextureData->RowPitchInPixels[0] * sizeof(FLinearColor));
			FMemory::Memcpy(ReadbackGroup.TextureData->Texture[1].GetData(), LockedData[1], ReadbackGroup.ReadbackTilePoolGPU->SizeInTiles.Y * GPreviewLightmapPhysicalTileSize * ReadbackGroup.TextureData->RowPitchInPixels[1] * sizeof(FLinearColor));
			FMemory::Memcpy(ReadbackGroup.TextureData->Texture[2].GetData(), LockedData[2], ReadbackGroup.ReadbackTilePoolGPU->SizeInTiles.Y * GPreviewLightmapPhysicalTileSize * ReadbackGroup.TextureData->RowPitchInPixels[2] * sizeof(FLinearColor));

			ReadbackGroup.StagingHQLayer0Readback->Unlock();
			ReadbackGroup.StagingHQLayer1Readback->Unlock();
			ReadbackGroup.StagingShadowMaskReadback->Unlock();

			for (int32 TileIndex = 0; TileIndex < ReadbackGroup.ConvergedTileRequests.Num(); TileIndex++)
			{
				FIntPoint SrcTilePosition(TileIndex % ReadbackGroup.ReadbackTilePoolGPU->SizeInTiles.X, TileIndex / ReadbackGroup.ReadbackTilePoolGPU->SizeInTiles.X);
				FIntPoint DstTilePosition(ReadbackGroup.ConvergedTileRequests[TileIndex].VirtualCoordinates.Position);

				check(ReadbackGroup.TextureData->RowPitchInPixels[0] == ReadbackGroup.TextureData->RowPitchInPixels[1]);
				const int32 SrcRowPitchInPixels = ReadbackGroup.TextureData->RowPitchInPixels[0];
				const int32 DstRowPitchInPixels = GPreviewLightmapVirtualTileSize;

				for (int32 Y = 0; Y < GPreviewLightmapVirtualTileSize; Y++)
				{
					for (int32 X = 0; X < GPreviewLightmapVirtualTileSize; X++)
					{
						FIntPoint SrcPixelPosition = SrcTilePosition * GPreviewLightmapPhysicalTileSize + FIntPoint(X, Y) + FIntPoint(GPreviewLightmapTileBorderSize, GPreviewLightmapTileBorderSize);
						FIntPoint DstPixelPosition = FIntPoint(X, Y);

						int32 SrcLinearIndex = SrcPixelPosition.Y * SrcRowPitchInPixels + SrcPixelPosition.X;
						int32 DstLinearIndex = DstPixelPosition.Y * DstRowPitchInPixels + DstPixelPosition.X;

						if (!ReadbackGroup.ConvergedTileRequests[TileIndex].RenderState->TileStorage.Contains(ReadbackGroup.ConvergedTileRequests[TileIndex].VirtualCoordinates))
						{
							ReadbackGroup.ConvergedTileRequests[TileIndex].RenderState->TileStorage.Add(ReadbackGroup.ConvergedTileRequests[TileIndex].VirtualCoordinates, FTileStorage{});
						}

						if (bDenoiseDuringInteractiveBake)
						{
							ReadbackGroup.ConvergedTileRequests[TileIndex].RenderState->TileStorage[ReadbackGroup.ConvergedTileRequests[TileIndex].VirtualCoordinates].CPUTextureRawData[0]->Decompress();
							ReadbackGroup.ConvergedTileRequests[TileIndex].RenderState->TileStorage[ReadbackGroup.ConvergedTileRequests[TileIndex].VirtualCoordinates].CPUTextureRawData[1]->Decompress();

							ReadbackGroup.ConvergedTileRequests[TileIndex].RenderState->TileStorage[ReadbackGroup.ConvergedTileRequests[TileIndex].VirtualCoordinates].CPUTextureRawData[0]->Data[DstLinearIndex] = ReadbackGroup.TextureData->Texture[0][SrcLinearIndex];
							ReadbackGroup.ConvergedTileRequests[TileIndex].RenderState->TileStorage[ReadbackGroup.ConvergedTileRequests[TileIndex].VirtualCoordinates].CPUTextureRawData[1]->Data[DstLinearIndex] = ReadbackGroup.TextureData->Texture[1][SrcLinearIndex];
						}

						// Always write into display data so we have something to show before denoising completes
						ReadbackGroup.ConvergedTileRequests[TileIndex].RenderState->TileStorage[ReadbackGroup.ConvergedTileRequests[TileIndex].VirtualCoordinates].CPUTextureData[0]->Decompress();
						ReadbackGroup.ConvergedTileRequests[TileIndex].RenderState->TileStorage[ReadbackGroup.ConvergedTileRequests[TileIndex].VirtualCoordinates].CPUTextureData[1]->Decompress();
						ReadbackGroup.ConvergedTileRequests[TileIndex].RenderState->TileStorage[ReadbackGroup.ConvergedTileRequests[TileIndex].VirtualCoordinates].CPUTextureData[0]->Data[DstLinearIndex] = ReadbackGroup.TextureData->Texture[0][SrcLinearIndex];
						ReadbackGroup.ConvergedTileRequests[TileIndex].RenderState->TileStorage[ReadbackGroup.ConvergedTileRequests[TileIndex].VirtualCoordinates].CPUTextureData[1]->Data[DstLinearIndex] = ReadbackGroup.TextureData->Texture[1][SrcLinearIndex];
						
						// For shadow maps, pass through
						ReadbackGroup.ConvergedTileRequests[TileIndex].RenderState->TileStorage[ReadbackGroup.ConvergedTileRequests[TileIndex].VirtualCoordinates].CPUTextureData[2]->Decompress();
						ReadbackGroup.ConvergedTileRequests[TileIndex].RenderState->TileStorage[ReadbackGroup.ConvergedTileRequests[TileIndex].VirtualCoordinates].CPUTextureData[2]->Data[DstLinearIndex] = ReadbackGroup.TextureData->Texture[2][SrcLinearIndex];
					}
				}
			}

			for (FLightmapTileRequest& Tile : ReadbackGroup.ConvergedTileRequests)
			{
				Tile.RenderState->RetrieveTileState(Tile.VirtualCoordinates).bCanBeDenoised = true;

				if (!bDenoiseDuringInteractiveBake)
				{
					Tile.RenderState->RetrieveTileState(Tile.VirtualCoordinates).CPURevision = CurrentRevision;
					Tile.RenderState->RetrieveTileState(Tile.VirtualCoordinates).OngoingReadbackRevision = -1;
				}
				else
				{
					TilesWaitingForDenoising.Add(Tile);

					for (int Dx = -(DenoiseTileProximity / 2); Dx <= (DenoiseTileProximity / 2); Dx++)
					{
						for (int Dy = -(DenoiseTileProximity / 2); Dy <= (DenoiseTileProximity / 2); Dy++)
						{
							FIntPoint TilePositionToLookAt(Tile.VirtualCoordinates.Position.X + Dx, Tile.VirtualCoordinates.Position.Y + Dy);
							TilePositionToLookAt.X = FMath::Clamp(TilePositionToLookAt.X, 0, Tile.RenderState->GetPaddedSizeInTilesAtMipLevel(Tile.VirtualCoordinates.MipLevel).X - 1);
							TilePositionToLookAt.Y = FMath::Clamp(TilePositionToLookAt.Y, 0, Tile.RenderState->GetPaddedSizeInTilesAtMipLevel(Tile.VirtualCoordinates.MipLevel).Y - 1);

							if (Tile.RenderState->RetrieveTileState(FTileVirtualCoordinates(TilePositionToLookAt, Tile.VirtualCoordinates.MipLevel)).bWasDenoisedWithoutProximity)
							{
								FLightmapTileRequest TileToDenoise(Tile.RenderState, FTileVirtualCoordinates(TilePositionToLookAt, Tile.VirtualCoordinates.MipLevel));

								TilesWaitingForDenoising.Add(TileToDenoise);

								Tile.RenderState->RetrieveTileState(TileToDenoise.VirtualCoordinates).CPURevision = -1;
								Tile.RenderState->RetrieveTileState(TileToDenoise.VirtualCoordinates).OngoingReadbackRevision = CurrentRevision;
							}
						}
					}
				}
			}
			
			ReadbackGroup.bIsFree = true;

			bPipelineFinished = true;
		}

		if (!bPipelineFinished)
		{
			FilteredReadbackGroups.Emplace(OngoingReadbacks[Index]);
		}
	}

	OngoingReadbacks = MoveTemp(FilteredReadbackGroups);

	{
		int32 NumFreeReadbackGroups = 0;

		for (int32 Index = 0; Index < RecycledReadbacks.Num(); Index++)
		{
			if (RecycledReadbacks[Index]->bIsFree)
			{
				NumFreeReadbackGroups++;
			}
		}

		const int32 MaxPooledFreeReadbackGroups = 100;
		int32 FreeReadbackGroupsToRemove = NumFreeReadbackGroups - MaxPooledFreeReadbackGroups;
		if (FreeReadbackGroupsToRemove > 0)
		{
			for (int32 Index = 0; Index < RecycledReadbacks.Num(); Index++)
			{
				if (RecycledReadbacks[Index]->bIsFree)
				{
					RecycledReadbacks.RemoveAt(Index);
					Index--;
					FreeReadbackGroupsToRemove--;

					if (FreeReadbackGroupsToRemove == 0) break;
				}
			}
		}
	}

	FTileDataLayer::Evict();

	{
		for (FLightmapTileRequest& Tile : TilesWaitingForDenoising)
		{
			auto AllTilesInProximityDenoised = [&Lightmap = Tile.RenderState, &PendingTileRequests = PendingTileRequests, &Tile = Tile](FTileVirtualCoordinates Coords) -> bool
			{
				bool bAll3x3TilesHaveBeenReadback = true;

				for (int Dx = -(DenoiseTileProximity / 2); Dx <= (DenoiseTileProximity / 2); Dx++)
				{
					for (int Dy = -(DenoiseTileProximity / 2); Dy <= (DenoiseTileProximity / 2); Dy++)
					{
						FIntPoint TilePositionToLookAt(Coords.Position.X + Dx, Coords.Position.Y + Dy);
						TilePositionToLookAt.X = FMath::Clamp(TilePositionToLookAt.X, 0, Lightmap->GetPaddedSizeInTilesAtMipLevel(Coords.MipLevel).X - 1);
						TilePositionToLookAt.Y = FMath::Clamp(TilePositionToLookAt.Y, 0, Lightmap->GetPaddedSizeInTilesAtMipLevel(Coords.MipLevel).Y - 1);

						if (!Lightmap->RetrieveTileState(FTileVirtualCoordinates(TilePositionToLookAt, Coords.MipLevel)).bCanBeDenoised)
						{
							bAll3x3TilesHaveBeenReadback = false;

							break;
						}
					}
				}

				return bAll3x3TilesHaveBeenReadback;
			};

			for (FLightmapTileDenoiseGroup& DenoiseGroup : OngoingDenoiseGroups)
			{
				if (DenoiseGroup.TileRequest == Tile)
				{
					DenoiseGroup.bShouldBeCancelled = true;
				}
			}

			FLightmapTileDenoiseGroup DenoiseGroup(Tile);
			DenoiseGroup.Revision = CurrentRevision;
			DenoiseGroup.TextureData = MakeShared<FLightmapTileDenoiseGroup::FTextureData, ESPMode::ThreadSafe>();

			DenoiseGroup.TextureData->Texture[0].AddZeroed(DenoiseTileProximity * DenoiseTileProximity * GPreviewLightmapVirtualTileSize * GPreviewLightmapVirtualTileSize);
			DenoiseGroup.TextureData->Texture[1].AddZeroed(DenoiseTileProximity * DenoiseTileProximity * GPreviewLightmapVirtualTileSize * GPreviewLightmapVirtualTileSize);

			for (int Dx = -(DenoiseTileProximity / 2); Dx <= (DenoiseTileProximity / 2); Dx++)
			{
				for (int Dy = -(DenoiseTileProximity / 2); Dy <= (DenoiseTileProximity / 2); Dy++)
				{
					FIntPoint SrcTilePosition(Tile.VirtualCoordinates.Position.X + Dx, Tile.VirtualCoordinates.Position.Y + Dy);
					SrcTilePosition.X = FMath::Clamp(SrcTilePosition.X, 0, Tile.RenderState->GetPaddedSizeInTilesAtMipLevel(Tile.VirtualCoordinates.MipLevel).X - 1);
					SrcTilePosition.Y = FMath::Clamp(SrcTilePosition.Y, 0, Tile.RenderState->GetPaddedSizeInTilesAtMipLevel(Tile.VirtualCoordinates.MipLevel).Y - 1);
					FIntPoint DstTilePosition(Dx + (DenoiseTileProximity / 2), Dy + (DenoiseTileProximity / 2));

					const int32 SrcRowPitchInPixels = GPreviewLightmapVirtualTileSize;
					const int32 DstRowPitchInPixels = DenoiseTileProximity * GPreviewLightmapVirtualTileSize;

					bool bShouldWriteZero = false;

					if (!Tile.RenderState->RetrieveTileState(FTileVirtualCoordinates(SrcTilePosition, Tile.VirtualCoordinates.MipLevel)).bCanBeDenoised)
					{
						bShouldWriteZero = true;
					}

					if (!bShouldWriteZero)
					{
						Tile.RenderState->TileStorage[FTileVirtualCoordinates(SrcTilePosition, Tile.VirtualCoordinates.MipLevel)].CPUTextureRawData[0]->Decompress();
						Tile.RenderState->TileStorage[FTileVirtualCoordinates(SrcTilePosition, Tile.VirtualCoordinates.MipLevel)].CPUTextureRawData[1]->Decompress();
					}

					for (int32 Y = 0; Y < GPreviewLightmapVirtualTileSize; Y++)
					{
						for (int32 X = 0; X < GPreviewLightmapVirtualTileSize; X++)
						{
							FIntPoint SrcPixelPosition = FIntPoint(X, Y);
							FIntPoint DstPixelPosition = DstTilePosition * GPreviewLightmapVirtualTileSize + FIntPoint(X, Y);

							int32 SrcLinearIndex = SrcPixelPosition.Y * SrcRowPitchInPixels + SrcPixelPosition.X;
							int32 DstLinearIndex = DstPixelPosition.Y * DstRowPitchInPixels + DstPixelPosition.X;

							DenoiseGroup.TextureData->Texture[0][DstLinearIndex] = !bShouldWriteZero ? Tile.RenderState->TileStorage[FTileVirtualCoordinates(SrcTilePosition, Tile.VirtualCoordinates.MipLevel)].CPUTextureRawData[0]->Data[SrcLinearIndex] : FLinearColor(0, 0, 0, 0);
							DenoiseGroup.TextureData->Texture[1][DstLinearIndex] = !bShouldWriteZero ? Tile.RenderState->TileStorage[FTileVirtualCoordinates(SrcTilePosition, Tile.VirtualCoordinates.MipLevel)].CPUTextureRawData[1]->Data[SrcLinearIndex] : FLinearColor(0, 0, 0, 0);
						}
					}
				}
			}

			DenoiseGroup.AsyncDenoisingWork = new FLightmapTileDenoiseAsyncTask();
			DenoiseGroup.AsyncDenoisingWork->Size = FIntPoint(DenoiseTileProximity * GPreviewLightmapVirtualTileSize, DenoiseTileProximity * GPreviewLightmapVirtualTileSize);
			DenoiseGroup.AsyncDenoisingWork->TextureData = DenoiseGroup.TextureData;
			DenoisingThreadPool->AddQueuedWork(DenoiseGroup.AsyncDenoisingWork);

			OngoingDenoiseGroups.Add(MoveTemp(DenoiseGroup));

			Tile.RenderState->RetrieveTileState(Tile.VirtualCoordinates).bWasDenoisedWithoutProximity = !AllTilesInProximityDenoised(Tile.VirtualCoordinates);
		}
	}

	bool bLastFewFramesIdle = GCurrentLevelEditingViewportClient && !GCurrentLevelEditingViewportClient->IsRealtime();

	if (bLastFewFramesIdle && !bWasRunningAtFullSpeed)
	{
		bWasRunningAtFullSpeed = true;
		UE_LOG(LogGPULightmass, Log, TEXT("GPULightmass is now running at full speed"));
	}

	if (!bLastFewFramesIdle && bWasRunningAtFullSpeed)
	{
		bWasRunningAtFullSpeed = false;
		UE_LOG(LogGPULightmass, Log, TEXT("GPULightmass is now throttled for realtime preview"));
	}

	FRHICommandListImmediate& RHICmdList = FRHICommandListExecutor::GetImmediateCommandList();

	if (!bOnlyBakeWhatYouSee)
	{
		const int32 NumWorkPerFrame = !bLastFewFramesIdle ? 32 : 128;

		if (Mip0WorkDoneLastFrame < NumWorkPerFrame)
		{
			int32 PoolSize = FMath::CeilToInt(FMath::Sqrt(static_cast<float>(NumWorkPerFrame * 3.f)));

			FIntPoint TextureSize(PoolSize * GPreviewLightmapPhysicalTileSize, PoolSize * GPreviewLightmapPhysicalTileSize);

			EPixelFormat RenderTargetFormat = PF_A32B32G32R32F;

			TRefCountPtr<IPooledRenderTarget> OutputTileAtlas;

			const FPooledRenderTargetDesc Desc = FPooledRenderTargetDesc::Create2DDesc(
				TextureSize,
				RenderTargetFormat,
				FClearValueBinding::None,
				TexCreate_None,
				TexCreate_ShaderResource | TexCreate_RenderTargetable | TexCreate_UAV,
				false);

			GRenderTargetPool.FindFreeElement(RHICmdList, Desc, OutputTileAtlas, TEXT("BackgroundTilePoolGPU"));

			ensure(OutputTileAtlas.IsValid());

			int32 WorkToGenerate = NumWorkPerFrame - Mip0WorkDoneLastFrame;
			int32 WorkGenerated = 0;
			int32 TileLinearIndexInAtlas = 0;

			TArray<FString> SelectedLightmapNames;

			for (FLightmapRenderState& Lightmap : Scene->LightmapRenderStates.Elements)
			{
				bool bAnyTileSelected = false;

				for (int32 Y = 0; Y < Lightmap.GetPaddedSizeInTiles().Y; Y++)
				{
					for (int32 X = 0; X < Lightmap.GetPaddedSizeInTiles().X; X++)
					{
						FTileVirtualCoordinates VirtualCoordinates(FIntPoint(X, Y), 0);

						if (!Lightmap.DoesTileHaveValidCPUData(VirtualCoordinates, CurrentRevision) && Lightmap.RetrieveTileState(VirtualCoordinates).OngoingReadbackRevision != CurrentRevision)
						{
							bAnyTileSelected = true;

							FVTProduceTargetLayer TargetLayers[3];
							TargetLayers[0].pPageLocation = FIntVector(TileLinearIndexInAtlas % PoolSize, TileLinearIndexInAtlas / PoolSize, 0);
							TargetLayers[0].PooledRenderTarget = OutputTileAtlas;
							TileLinearIndexInAtlas++;
							TargetLayers[1].pPageLocation = FIntVector(TileLinearIndexInAtlas % PoolSize, TileLinearIndexInAtlas / PoolSize, 0);
							TargetLayers[1].PooledRenderTarget = OutputTileAtlas;
							TileLinearIndexInAtlas++;
							TargetLayers[2].pPageLocation = FIntVector(TileLinearIndexInAtlas % PoolSize, TileLinearIndexInAtlas / PoolSize, 0);
							TargetLayers[2].PooledRenderTarget = OutputTileAtlas;
							TileLinearIndexInAtlas++;
							check(TileLinearIndexInAtlas <= PoolSize * PoolSize);

							Lightmap.LightmapPreviewVirtualTexture->ProducePageData(
								RHICmdList,
								ERHIFeatureLevel::SM5,
								EVTProducePageFlags::None,
								FVirtualTextureProducerHandle(),
								0b111,
								0,
								FMath::MortonCode2(X) | (FMath::MortonCode2(Y) << 1),
								0,
								TargetLayers);

							WorkGenerated++;

							if (WorkGenerated >= WorkToGenerate)
							{
								break;
							}
						}
					}

					if (WorkGenerated >= WorkToGenerate)
					{
						break;
					}
				}

				if (bAnyTileSelected)
				{
					SelectedLightmapNames.Add(Lightmap.Name);
				}

				if (WorkGenerated >= WorkToGenerate)
				{
					break;
				}
			}

			if (bLastFewFramesIdle && FrameNumber % 100 == 0)
			{
				FString AllNames;
				for (FString& Name : SelectedLightmapNames)
				{
					AllNames += Name.RightChop(FString(TEXT("Lightmap_")).Len()) + TEXT(" ");
				}
				UE_LOG(LogGPULightmass, Log, TEXT("Working on: %s"), *AllNames);
			}
		}

		Mip0WorkDoneLastFrame = 0;
	}
	else
	{
		if (bLastFewFramesIdle)
		{
			int32 WorkGenerated = 0;

			const int32 WorkToGenerate = 512;

			if (RecordedTileRequests.Num() > 0)
			{
				for (FLightmapTileRequest& Tile : RecordedTileRequests)
				{
					if (!Tile.RenderState->DoesTileHaveValidCPUData(Tile.VirtualCoordinates, CurrentRevision) && Tile.RenderState->RetrieveTileState(Tile.VirtualCoordinates).OngoingReadbackRevision != CurrentRevision)
					{
						PendingTileRequests.AddUnique(Tile);

						WorkGenerated++;

						if (WorkGenerated >= WorkToGenerate)
						{
							break;
						}
					}
				}
			}
			else
			{
				for (TArray<FLightmapTileRequest>& FrameRequests : TilesVisibleLastFewFrames)
				{
					for (FLightmapTileRequest& Tile : FrameRequests)
					{
						if (!Tile.RenderState->DoesTileHaveValidCPUData(Tile.VirtualCoordinates, CurrentRevision) && Tile.RenderState->RetrieveTileState(Tile.VirtualCoordinates).OngoingReadbackRevision != CurrentRevision)
						{
							PendingTileRequests.AddUnique(Tile);

							WorkGenerated++;

							if (WorkGenerated >= WorkToGenerate)
							{
								break;
							}
						}
					}
				}
			}
		}
	}

	bInsideBackgroundTick = true;

	// Render lightmap tiles
	{
		FMemMark MemMark(FMemStack::Get());
		FRDGBuilder GraphBuilder(RHICmdList);
		Finalize(GraphBuilder);
		GraphBuilder.Execute();
	}

	bInsideBackgroundTick = false;

	if (bLastFewFramesIdle) // Indicates that the viewport is non-realtime
	{
		// Purge resources when 'realtime' is not checked on editor viewport to avoid leak & slowing down
		RHICmdList.ImmediateFlush(EImmediateFlushType::FlushRHIThreadFlushResources);
	}

	if (Scene->Settings->bVisualizeIrradianceCache && !IrradianceCacheVisualizationDelegateHandle.IsValid())
	{
		IrradianceCacheVisualizationDelegateHandle = GetRendererModule().RegisterPostOpaqueRenderDelegate(FPostOpaqueRenderDelegate::CreateRaw(this, &FLightmapRenderer::RenderIrradianceCacheVisualization));
	}
	else if (!Scene->Settings->bVisualizeIrradianceCache && IrradianceCacheVisualizationDelegateHandle.IsValid())
	{
		GetRendererModule().RemovePostOpaqueRenderDelegate(IrradianceCacheVisualizationDelegateHandle);
		IrradianceCacheVisualizationDelegateHandle.Reset();
	}
}

void FLightmapRenderer::BumpRevision()
{
	CurrentRevision++;

	for (TArray<FLightmapTileRequest>& FrameRequests : TilesVisibleLastFewFrames)
	{
		FrameRequests.Empty();
	}	

	RecordedTileRequests.Empty();
}

void FLightmapRenderer::DeduplicateRecordedTileRequests()
{
	RecordedTileRequests.Sort([](const FLightmapTileRequest& A, const FLightmapTileRequest& B) { return A.VirtualCoordinates.MipLevel > B.VirtualCoordinates.MipLevel; });

	for (int32 Index = 0; Index < RecordedTileRequests.Num(); Index++)
	{
		FLightmapTileRequest& Tile = RecordedTileRequests[Index];
		if (RecordedTileRequests.FindByPredicate([&Tile](const FLightmapTileRequest& Entry) { 
			return Entry.VirtualCoordinates.MipLevel == Tile.VirtualCoordinates.MipLevel - 1
				&& Entry.VirtualCoordinates.Position.X >= Tile.VirtualCoordinates.Position.X * 2
				&& Entry.VirtualCoordinates.Position.Y >= Tile.VirtualCoordinates.Position.Y * 2
				&& Entry.VirtualCoordinates.Position.X < (Tile.VirtualCoordinates.Position.X + 1) * 2
				&& Entry.VirtualCoordinates.Position.Y < (Tile.VirtualCoordinates.Position.Y + 1) * 2
				;
		}))
		{
			RecordedTileRequests.RemoveAt(Index);
			Index--;
		}
	}
}

void FLightmapRenderer::RenderIrradianceCacheVisualization(FPostOpaqueRenderParameters& Parameters)
{
	FRDGBuilder& GraphBuilder = *Parameters.GraphBuilder;

	auto* PassParameters = GraphBuilder.AllocParameters<FVisualizeIrradianceCachePS::FParameters>();
	TUniformBufferRef<FViewUniformShaderParameters> Ref;
	*Ref.GetInitReference() = Parameters.ViewUniformBuffer;
	PassParameters->View = Ref;
	PassParameters->SceneTextures = Parameters.SceneTexturesUniformParams;
	PassParameters->IrradianceCachingParameters = Scene->IrradianceCache->IrradianceCachingParametersUniformBuffer;
	PassParameters->RenderTargets[0] = FRenderTargetBinding(Parameters.ColorTexture, ERenderTargetLoadAction::ELoad);

	const FIntRect ViewportRect = Parameters.ViewportRect;
	const FIntPoint TextureExtent = Parameters.ColorTexture->Desc.Extent;

	GraphBuilder.AddPass(
		RDG_EVENT_NAME("ClearIrradiance"),
		PassParameters,
		ERDGPassFlags::Raster,
		[ViewportRect, PassParameters, TextureExtent] (FRHICommandList& RHICmdList)
	{
		RHICmdList.SetViewport(0, 0, 0.0f, ViewportRect.Width(), ViewportRect.Height(), 1.0f);

		TShaderMapRef<FPostProcessVS> VertexShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
		TShaderMapRef<FVisualizeIrradianceCachePS> PixelShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));

		FGraphicsPipelineStateInitializer GraphicsPSOInit;
		RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);
		GraphicsPSOInit.RasterizerState = TStaticRasterizerState<FM_Solid, CM_None>::GetRHI();
		GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();
		GraphicsPSOInit.BlendState = TStaticBlendState<>::GetRHI();
		GraphicsPSOInit.PrimitiveType = PT_TriangleList;
		GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GFilterVertexDeclaration.VertexDeclarationRHI;
		GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
		GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();

		SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);

		SetShaderParameters(RHICmdList, PixelShader, PixelShader.GetPixelShader(), *PassParameters);

		DrawRectangle(
			RHICmdList,
			0, 0,
			ViewportRect.Width(), ViewportRect.Height(),
			0, 0,
			ViewportRect.Width(), ViewportRect.Height(),
			FIntPoint(ViewportRect.Width(), ViewportRect.Height()),
			TextureExtent,
			VertexShader);
	});
}

}
