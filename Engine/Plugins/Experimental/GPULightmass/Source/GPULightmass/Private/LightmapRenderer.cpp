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
		SHADER_PARAMETER_UAV(RWTexture2D<float4>, TilePool)
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
	FStructuredBufferRHIRef BatchedTilesBuffer;
	FShaderResourceViewRHIRef BatchedTilesSRV;
	TResourceArray<FGPUTileDescription> BatchedTilesDesc;
};

#if RHI_RAYTRACING
FPathTracingLightData SetupPathTracingLightParameters(const GPULightmass::FLightSceneRenderState& LightScene)
{
	FPathTracingLightData LightParameters;

	FMemory::Memzero(LightParameters);

	LightParameters.Count = 0;

	// Prepend SkyLight to light buffer
	// WARNING: Until ray payload encodes Light data buffer, the execution depends on this ordering!
	uint32 SkyLightIndex = 0;
	LightParameters.Type[SkyLightIndex] = 0;
	LightParameters.Color[SkyLightIndex] = LightScene.SkyLight.IsSet() ? FVector(LightScene.SkyLight->Color) : FVector(0);
	LightParameters.Mobility[SkyLightIndex] = (LightScene.SkyLight.IsSet() && LightScene.SkyLight->bStationary) ? 1 : 0;
	uint32 Transmission = 1;
	uint8 LightingChannelMask = 0b111;
	LightParameters.Flags[SkyLightIndex] = Transmission & 0x01;
	LightParameters.Flags[SkyLightIndex] |= (LightingChannelMask & 0x7) << 1;
	LightParameters.Count++;

	uint32 MaxLightCount = RAY_TRACING_LIGHT_COUNT_MAXIMUM;

	for (auto Light : LightScene.DirectionalLights.Elements)
	{
		if (LightParameters.Count < MaxLightCount)
		{
			LightParameters.Type[LightParameters.Count] = 2;
			LightParameters.Normal[LightParameters.Count] = -Light.Direction;
			LightParameters.Color[LightParameters.Count] = FVector(Light.Color);
			LightParameters.Dimensions[LightParameters.Count] = FVector(0.0f, 0.0f, FMath::Sin(0.5f * FMath::DegreesToRadians(Light.LightSourceAngle)));
			LightParameters.Attenuation[LightParameters.Count] = 1.0;
			LightParameters.Mobility[LightParameters.Count] = Light.bStationary ? 1 : 0;

			LightParameters.Flags[LightParameters.Count] = Transmission & 0x01;
			LightParameters.Flags[LightParameters.Count] |= (LightingChannelMask & 0x7) << 1;

			LightParameters.Count++;
		}
	}

	for (auto Light : LightScene.PointLights.Elements)
	{
		if (LightParameters.Count < MaxLightCount)
		{
			LightParameters.Type[LightParameters.Count] = 1;
			LightParameters.Position[LightParameters.Count] = Light.Position;
			LightParameters.Color[LightParameters.Count] = FVector(Light.Color);
			LightParameters.Dimensions[LightParameters.Count] = FVector(0.0, 0.0, Light.SourceRadius);
			LightParameters.Attenuation[LightParameters.Count] = Light.AttenuationRadius;
			LightParameters.Mobility[LightParameters.Count] = Light.bStationary ? 1 : 0;

			LightParameters.Flags[LightParameters.Count] = Transmission & 0x01;
			LightParameters.Flags[LightParameters.Count] |= (LightingChannelMask & 0x7) << 1;

			LightParameters.Count++;
		}
	}

	for (auto Light : LightScene.SpotLights.Elements)
	{
		if (LightParameters.Count < MaxLightCount)
		{
			LightParameters.Type[LightParameters.Count] = 4;
			LightParameters.Position[LightParameters.Count] = Light.Position;
			LightParameters.Normal[LightParameters.Count] = Light.Direction;
			LightParameters.Color[LightParameters.Count] = FVector(Light.Color);
			LightParameters.Dimensions[LightParameters.Count] = FVector(Light.SpotAngles, Light.SourceRadius);
			LightParameters.Attenuation[LightParameters.Count] = Light.AttenuationRadius;
			LightParameters.Mobility[LightParameters.Count] = Light.bStationary ? 1 : 0;

			LightParameters.Flags[LightParameters.Count] = Transmission & 0x01;
			LightParameters.Flags[LightParameters.Count] |= (LightingChannelMask & 0x7) << 1;

			LightParameters.Count++;
		}
	}

	for (auto Light : LightScene.RectLights.Elements)
	{
		if (LightParameters.Count < MaxLightCount)
		{
			LightParameters.Type[LightParameters.Count] = 3;
			LightParameters.Position[LightParameters.Count] = Light.Position;
			LightParameters.Normal[LightParameters.Count] = Light.Direction;
			LightParameters.dPdu[LightParameters.Count] = FVector::CrossProduct(Light.Tangent, -Light.Direction);
			LightParameters.dPdv[LightParameters.Count] = Light.Tangent;

			FLinearColor LightColor = Light.Color;
			LightColor /= 0.5f * Light.SourceWidth * Light.SourceHeight;
			LightParameters.Color[LightParameters.Count] = FVector(LightColor);

			LightParameters.Dimensions[LightParameters.Count] = FVector(Light.SourceWidth, Light.SourceHeight, 0.0f);
			LightParameters.Attenuation[LightParameters.Count] = Light.AttenuationRadius;
			LightParameters.RectLightBarnCosAngle[LightParameters.Count] = FMath::Cos(FMath::DegreesToRadians(Light.BarnDoorAngle));
			LightParameters.RectLightBarnLength[LightParameters.Count] = Light.BarnDoorLength;

			LightParameters.Mobility[LightParameters.Count] = Light.bStationary ? 1 : 0;

			LightParameters.Flags[LightParameters.Count] = Transmission & 0x01;
			LightParameters.Flags[LightParameters.Count] |= (LightingChannelMask & 0x7) << 1;

			LightParameters.Count++;
		}
	}

	return LightParameters;
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
	DenoisingThreadPool->Create(1);

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

	PrimitiveSceneData.AddZeroed(Scene.StaticMeshInstanceRenderStates.Elements.Num());

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
				&LightmapSceneDataStartOffsets
			](int32 InstanceIndex = 0)
			{
				FStaticMeshInstanceRenderState& Instance = Scene.StaticMeshInstanceRenderStates.Elements[InstanceIndex];

				FPrimitiveUniformShaderParameters PrimitiveUniformShaderParameters = Instance.PrimitiveUniformShaderParameters;
				PrimitiveUniformShaderParameters.LightmapDataIndex = LightmapSceneDataStartOffsets[InstanceIndex];
				PrimitiveSceneData[InstanceIndex] = FPrimitiveSceneShaderData(PrimitiveUniformShaderParameters);

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
	}

	{
		TRACE_CPUPROFILER_EVENT_SCOPE(SetupViewBuffers);

		{
			TRACE_CPUPROFILER_EVENT_SCOPE(PrimitiveSceneData);

			FRHIResourceCreateInfo CreateInfo(&PrimitiveSceneData);
			if (PrimitiveSceneData.GetResourceDataSize() == 0)
			{
				PrimitiveSceneData.Add(FPrimitiveSceneShaderData(GetIdentityPrimitiveParameters()));
			}

			PrimitiveSceneDataBufferRHI = RHICreateStructuredBuffer(sizeof(FVector4), PrimitiveSceneData.GetResourceDataSize(), BUF_Static | BUF_ShaderResource, CreateInfo);
			PrimitiveSceneDataBufferSRV = RHICreateShaderResourceView(PrimitiveSceneDataBufferRHI);
		}

		{
			TRACE_CPUPROFILER_EVENT_SCOPE(LightmapSceneData);

			FRHIResourceCreateInfo CreateInfo(&LightmapSceneData);
			if (LightmapSceneData.GetResourceDataSize() == 0)
			{
				LightmapSceneData.Add(FLightmapSceneShaderData());
			}

			LightmapSceneDataBufferRHI = RHICreateStructuredBuffer(sizeof(FVector4), LightmapSceneData.GetResourceDataSize(), BUF_Static | BUF_ShaderResource, CreateInfo);
			LightmapSceneDataBufferSRV = RHICreateShaderResourceView(LightmapSceneDataBufferRHI);
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
					RayTracingInstance.GeometryRHI = InstanceGroup.ComponentUObject->GetStaticMesh()->RenderData->LODResources[0].RayTracingGeometry.RayTracingGeometryRHI;

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

	ReferenceView = MakeUnique<FViewInfo>(ViewInitOptions);
	FViewInfo& View = *ReferenceView;
	View.ViewRect = View.UnscaledViewRect;

	{
		TRACE_CPUPROFILER_EVENT_SCOPE(SetupViewBuffers);

		View.PrimitiveSceneDataOverrideSRV = CachedRayTracingScene->PrimitiveSceneDataBufferSRV;
		View.LightmapSceneDataOverrideSRV = CachedRayTracingScene->LightmapSceneDataBufferSRV;

		{
			TRACE_CPUPROFILER_EVENT_SCOPE(SetupUniformBufferParameters);

			// Expanded version of View.InitRHIResources() - need to do SetupSkyIrradianceEnvironmentMapConstants manually because the estimation of skylight is dependent on GetSkySHDiffuse
			View.CachedViewUniformShaderParameters = MakeUnique<FViewUniformShaderParameters>();

			FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(FRHICommandListExecutor::GetImmediateCommandList());

			FBox UnusedVolumeBounds[TVC_MAX];
			View.SetupUniformBufferParameters(
				SceneContext,
				UnusedVolumeBounds,
				TVC_MAX,
				*View.CachedViewUniformShaderParameters);

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

void FLightmapRenderer::Finalize(FRHICommandListImmediate& RHICmdList)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FLightmapRenderer::Finalize);

	if (PendingTileRequests.Num() == 0)
	{
		return;
	}

	FMemMark Mark(FMemStack::Get());

	// Upload & copy converged tiles directly
	{
		TArray<FLightmapTileRequest> TileUploadRequests = PendingTileRequests.FilterByPredicate(
			[CurrentRevision = CurrentRevision, bDenoiseDuringInteractiveBake = bDenoiseDuringInteractiveBake](const FLightmapTileRequest& Tile)
		{ 
			return Tile.RenderState->DoesTileHaveValidCPUData(Tile.VirtualCoordinates, CurrentRevision) || (bDenoiseDuringInteractiveBake && Tile.RenderState->RetrieveTileState(Tile.VirtualCoordinates).OngoingReadbackRevision == CurrentRevision && Tile.RenderState->RetrieveTileState(Tile.VirtualCoordinates).bCanBeDenoised);
		});

		if (TileUploadRequests.Num() > 0)
		{
			SCOPED_DRAW_EVENTF(RHICmdList, GPULightmassUploadConvergedTiles, TEXT("GPULightmass UploadConvergedTiles %d tiles"), TileUploadRequests.Num());

			int32 NewSize = FMath::CeilToInt(FMath::Sqrt(TileUploadRequests.Num()));
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
							FIntPoint SrcPixelPosition = SrcTilePosition * GPreviewLightmapVirtualTileSize + FIntPoint(X, Y) - FIntPoint(GPreviewLightmapTileBorderSize, GPreviewLightmapTileBorderSize);
							SrcPixelPosition.X = FMath::Clamp(SrcPixelPosition.X, 0, TileUploadRequests[TileIndex].RenderState->GetPaddedSizeAtMipLevel(TileUploadRequests[TileIndex].VirtualCoordinates.MipLevel).X - 1);
							SrcPixelPosition.Y = FMath::Clamp(SrcPixelPosition.Y, 0, TileUploadRequests[TileIndex].RenderState->GetPaddedSizeAtMipLevel(TileUploadRequests[TileIndex].VirtualCoordinates.MipLevel).Y - 1);
							FIntPoint DstPixelPosition = DstTilePosition * GPreviewLightmapPhysicalTileSize + FIntPoint(X, Y);

							int32 SrcLinearIndex = SrcPixelPosition.Y * SrcRowPitchInPixels + SrcPixelPosition.X;
							int32 DstLinearIndex = DstPixelPosition.Y * DstRowPitchInPixels + DstPixelPosition.X;

							Texture[0][DstLinearIndex] = TileUploadRequests[TileIndex].RenderState->CPUTextureData[0][TileUploadRequests[TileIndex].VirtualCoordinates.MipLevel][SrcLinearIndex];
							Texture[1][DstLinearIndex] = TileUploadRequests[TileIndex].RenderState->CPUTextureData[1][TileUploadRequests[TileIndex].VirtualCoordinates.MipLevel][SrcLinearIndex];
							Texture[2][DstLinearIndex] = TileUploadRequests[TileIndex].RenderState->CPUTextureData[2][TileUploadRequests[TileIndex].VirtualCoordinates.MipLevel][SrcLinearIndex];
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

				FRHIResourceCreateInfo CreateInfo;
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

			FRDGBuilder GraphBuilder(RHICmdList);

			FRDGTextureRef StagingHQLayer0 = GraphBuilder.RegisterExternalTexture(UploadTilePoolGPU->PooledRenderTargets[0], TEXT("StagingHQLayer0"));
			FRDGTextureRef StagingHQLayer1 = GraphBuilder.RegisterExternalTexture(UploadTilePoolGPU->PooledRenderTargets[1], TEXT("StagingHQLayer1"));
			FRDGTextureRef StagingShadowMask = GraphBuilder.RegisterExternalTexture(UploadTilePoolGPU->PooledRenderTargets[2], TEXT("StagingShadowMask"));

			FStructuredBufferRHIRef SrcTilePositionsBuffer;
			FShaderResourceViewRHIRef SrcTilePositionsSRV;
			FStructuredBufferRHIRef DstTilePositionsBuffer;
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
						FRHIResourceCreateInfo CreateInfo(&SrcTilePositions);
						SrcTilePositionsBuffer = RHICreateStructuredBuffer(sizeof(FIntPoint), SrcTilePositions.GetResourceDataSize(), BUF_Static | BUF_ShaderResource, CreateInfo);
						SrcTilePositionsSRV = RHICreateShaderResourceView(SrcTilePositionsBuffer);
					}

					{
						FRHIResourceCreateInfo CreateInfo(&DstTilePositions);
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
					PassParameters->SrcTilePositions = SrcTilePositionsSRV;
					PassParameters->DstTilePositions = DstTilePositionsSRV;

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
						FRHIResourceCreateInfo CreateInfo(&SrcTilePositions);
						SrcTilePositionsBuffer = RHICreateStructuredBuffer(sizeof(FIntPoint), SrcTilePositions.GetResourceDataSize(), BUF_Static | BUF_ShaderResource, CreateInfo);
						SrcTilePositionsSRV = RHICreateShaderResourceView(SrcTilePositionsBuffer);
					}

					{
						FRHIResourceCreateInfo CreateInfo(&DstTilePositions);
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
					PassParameters->SrcTilePositions = SrcTilePositionsSRV;
					PassParameters->DstTilePositions = DstTilePositionsSRV;

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
						FRHIResourceCreateInfo CreateInfo(&SrcTilePositions);
						SrcTilePositionsBuffer = RHICreateStructuredBuffer(sizeof(FIntPoint), SrcTilePositions.GetResourceDataSize(), BUF_Static | BUF_ShaderResource, CreateInfo);
						SrcTilePositionsSRV = RHICreateShaderResourceView(SrcTilePositionsBuffer);
					}

					{
						FRHIResourceCreateInfo CreateInfo(&DstTilePositions);
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
					PassParameters->SrcTilePositions = SrcTilePositionsSRV;
					PassParameters->DstTilePositions = DstTilePositionsSRV;

					TShaderMapRef<FUploadConvergedLightmapTilesCS> ComputeShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
					FComputeShaderUtils::AddPass(
						GraphBuilder,
						RDG_EVENT_NAME("UploadConvergedLightmapTiles"),
						ComputeShader,
						PassParameters,
						FComputeShaderUtils::GetGroupCount(DispatchResolution, FComputeShaderUtils::kGolden2DGroupSize));
				}
			}

			GraphBuilder.Execute();
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
				Tile.RenderState->RetrieveTileState(Tile.VirtualCoordinates).Revision = -1;
				Tile.RenderState->RetrieveTileState(Tile.VirtualCoordinates).RenderPassIndex = 0;
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

					int32 NewSize = FMath::Min(FMath::CeilToInt(FMath::Sqrt(TilesToQuery.Num())), 64);
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

	FLightmapGBufferParams LightmapGBufferParameters{};
	LightmapGBufferParameters.ScratchTilePoolLayer0 = ScratchTilePoolGPU->PooledRenderTargets[0]->GetRenderTargetItem().UAV;
	LightmapGBufferParameters.ScratchTilePoolLayer1 = ScratchTilePoolGPU->PooledRenderTargets[1]->GetRenderTargetItem().UAV;
	LightmapGBufferParameters.ScratchTilePoolLayer2 = ScratchTilePoolGPU->PooledRenderTargets[2]->GetRenderTargetItem().UAV;
	FLightmapGBufferUniformBufferRef PassUniformBuffer = FLightmapGBufferUniformBufferRef::CreateUniformBufferImmediate(LightmapGBufferParameters, UniformBuffer_SingleFrame);

	SCOPED_GPU_MASK(RHICmdList, FRHIGPUMask::GPU0());

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
			SCOPED_GPU_MASK(RHICmdList, FRHIGPUMask::All());
			FRHIRenderPassInfo RPInfo(LightmapTilePoolGPU.PooledRenderTargets[PoolLayerIndex]->GetRenderTargetItem().TargetableTexture, ERenderTargetActions::DontLoad_Store);
			RHICmdList.BeginRenderPass(RPInfo, TEXT("ClearLightmapTilePoolGPU"));
			for (auto& Tile : PendingTileRequests)
			{
				if (Tile.RenderState->RetrieveTileState(Tile.VirtualCoordinates).Revision != CurrentRevision)
				{
					RHICmdList.SetViewport(
						LightmapTilePoolGPU.GetPositionFromLinearAddress(Tile.TileAddressInWorkingSet).X * LightmapTilePoolGPU.LayerFormatAndTileSize[PoolLayerIndex].TileSize.X,
						LightmapTilePoolGPU.GetPositionFromLinearAddress(Tile.TileAddressInWorkingSet).Y * LightmapTilePoolGPU.LayerFormatAndTileSize[PoolLayerIndex].TileSize.Y,
						0.0f,
						(LightmapTilePoolGPU.GetPositionFromLinearAddress(Tile.TileAddressInWorkingSet).X + 1) * LightmapTilePoolGPU.LayerFormatAndTileSize[PoolLayerIndex].TileSize.X,
						(LightmapTilePoolGPU.GetPositionFromLinearAddress(Tile.TileAddressInWorkingSet).Y + 1) * LightmapTilePoolGPU.LayerFormatAndTileSize[PoolLayerIndex].TileSize.Y,
						1.0f);
					DrawClearQuad(RHICmdList, FLinearColor(0.0f, 0.0f, 0.0f, 0.0f));
				}
			}
			RHICmdList.EndRenderPass();
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
		TArray<FLightmapTileRequest> PendingGITileRequests = PendingTileRequests.FilterByPredicate([NumGISamples = Scene->Settings->GISamples](const FLightmapTileRequest& Tile) { return !Tile.RenderState->IsTileGIConverged(Tile.VirtualCoordinates, NumGISamples); });

		// Render GI
		for (int32 SampleIndex = 0; SampleIndex < NumSamplesPerFrame; SampleIndex++)
		{
			FMemMark PerSampleMark(FMemStack::Get());

			{
				if (PendingGITileRequests.Num() > 0)
				{
					for (int ScratchLayerIndex = 0; ScratchLayerIndex < 3; ScratchLayerIndex++)
					{
						FRDGBuilder GraphBuilder(RHICmdList);

						TResourceArray<FIntPoint> TilePositionsToClear;
						for (auto& Tile : PendingGITileRequests)
						{
							TilePositionsToClear.Add(ScratchTilePoolGPU->GetPositionFromLinearAddress(Tile.TileAddressInScratch));
						}

						FRWBuffer TilePositionsBuffer;
						TilePositionsBuffer.Initialize(TilePositionsToClear.GetTypeSize(), TilePositionsToClear.Num(), PF_R32G32_UINT, 0, TEXT("TilePositionsBufferForClear"), &TilePositionsToClear);

						FMultiTileClearCS::FParameters* Parameters = GraphBuilder.AllocParameters<FMultiTileClearCS::FParameters>();
						Parameters->NumTiles = TilePositionsToClear.Num();
						Parameters->TileSize = GPreviewLightmapPhysicalTileSize;
						Parameters->TilePositions = TilePositionsBuffer.SRV;
						Parameters->TilePool = ScratchTilePoolGPU->PooledRenderTargets[ScratchLayerIndex]->GetRenderTargetItem().UAV;

						TShaderMapRef<FMultiTileClearCS> ComputeShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
						FComputeShaderUtils::AddPass(
							GraphBuilder,
							RDG_EVENT_NAME("MultiTileClear"),
							ComputeShader,
							Parameters,
							FComputeShaderUtils::GetGroupCount(FIntPoint(GPreviewLightmapPhysicalTileSize * TilePositionsToClear.Num(), GPreviewLightmapPhysicalTileSize), FComputeShaderUtils::kGolden2DGroupSize));

						GraphBuilder.Execute();
					}

					for (int ScratchLayerIndex = 0; ScratchLayerIndex < 3; ScratchLayerIndex++)
					{
						RHICmdList.Transition(FRHITransitionInfo(ScratchTilePoolGPU->PooledRenderTargets[ScratchLayerIndex]->GetRenderTargetItem().UAV, ERHIAccess::UAVGraphics, ERHIAccess::ERWBarrier));
					}

					{
						for (uint32 GPUIndex = 0; GPUIndex < GNumExplicitGPUsForRendering; GPUIndex++)
						{
							SCOPED_GPU_MASK(RHICmdList, FRHIGPUMask::FromIndex(GPUIndex));

							FRHIRenderPassInfo RPInfo(FRHIRenderPassInfo::NoRenderTargets);
							RHICmdList.BeginRenderPass(RPInfo, TEXT("LightmapGBuffer"));

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
									*Scene->ReferenceView, RHICmdList,
									[
										View = Scene->ReferenceView.Get(), 
										PassUniformBuffer, 
										MeshBatches,
										VirtualTexturePhysicalTileCoordinateScaleAndBias,
										RenderPassIndex = Tile.RenderState->RetrieveTileState(Tile.VirtualCoordinates).RenderPassIndex,
										ScratchTilePoolOffset = ScratchTilePoolGPU->GetPositionFromLinearAddress(Tile.TileAddressInScratch) * GPreviewLightmapPhysicalTileSize
									](FDynamicPassMeshDrawListContext* DynamicMeshPassContext)
								{
									FLightmapGBufferMeshProcessor MeshProcessor(nullptr, View, DynamicMeshPassContext, PassUniformBuffer, VirtualTexturePhysicalTileCoordinateScaleAndBias, RenderPassIndex, ScratchTilePoolOffset);

									for (auto& MeshBatch : MeshBatches)
									{
										MeshProcessor.AddMeshBatch(MeshBatch, ~0ull, nullptr);
									}
								});

								GPrimitiveIdVertexBufferPool.DiscardAll();
							}

							RHICmdList.EndRenderPass();
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
								FRHIResourceCreateInfo CreateInfo;
								CreateInfo.ResourceArray = &GPUBatchedTileRequests.BatchedTilesDesc;

								GPUBatchedTileRequests.BatchedTilesBuffer = RHICreateStructuredBuffer(sizeof(FGPUTileDescription), GPUBatchedTileRequests.BatchedTilesDesc.GetResourceDataSize(), BUF_Static | BUF_ShaderResource, CreateInfo);
								GPUBatchedTileRequests.BatchedTilesSRV = RHICreateShaderResourceView(GPUBatchedTileRequests.BatchedTilesBuffer);
							}

							SCOPED_GPU_MASK(RHICmdList, FRHIGPUMask::FromIndex(GPUIndex));

							if (GPUBatchedTileRequests.BatchedTilesDesc.Num() > 0)
							{
								FRDGBuilder GraphBuilder(RHICmdList);

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
								TUniformBufferRef<FPathTracingLightData> LightDataUniformBuffer;
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
											LightDataUniformBuffer = CreateUniformBufferImmediate(SetupPathTracingLightParameters(Scene->LightSceneRenderState), EUniformBufferUsage::UniformBuffer_SingleFrame);
											PassParameters->LightParameters = LightDataUniformBuffer;
										}

										{
											SkyLightDataUniformBuffer = CreateUniformBufferImmediate(SetupSkyLightParameters(Scene->LightSceneRenderState), EUniformBufferUsage::UniformBuffer_SingleFrame);
											PassParameters->SkyLight = SkyLightDataUniformBuffer;
										}

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

								GraphBuilder.Execute();
							}
						}
					}
#endif
				}
			}
		}
	}

	for (int32 SampleIndex = 0; SampleIndex < NumSamplesPerFrame; SampleIndex++)
	{
		FMemMark PerSampleMark(FMemStack::Get());

		// Render shadow mask
		{
			TArray<FLightmapTileRequest> PendingShadowTileRequestsOnAllGPUs = PendingTileRequests.FilterByPredicate([NumShadowSamples = Scene->Settings->StationaryLightShadowSamples](const FLightmapTileRequest& Tile) { return !Tile.RenderState->IsTileShadowConverged(Tile.VirtualCoordinates, NumShadowSamples); });

			if (PendingShadowTileRequestsOnAllGPUs.Num() > 0)
			{
				for (uint32 GPUIndex = 0; GPUIndex < GNumExplicitGPUsForRendering; GPUIndex++)
				{
					SCOPED_GPU_MASK(RHICmdList, FRHIGPUMask::FromIndex(GPUIndex));

					auto PendingShadowTileRequests = PendingShadowTileRequestsOnAllGPUs.FilterByPredicate(
						[GPUIndex](const FLightmapTileRequest& Tile)
					{
						uint32 AssignedGPUIndex = (Tile.RenderState->DistributionPrefixSum + Tile.RenderState->RetrieveTileStateIndex(Tile.VirtualCoordinates)) % GNumExplicitGPUsForRendering;
						return AssignedGPUIndex == GPUIndex;
					});

					if (PendingShadowTileRequests.Num() == 0) continue;

					for (int ScratchLayerIndex = 0; ScratchLayerIndex < 3; ScratchLayerIndex++)
					{
						FRDGBuilder GraphBuilder(RHICmdList);

						TResourceArray<FIntPoint> TilePositionsToClear;
						for (auto& Tile : PendingShadowTileRequests)
						{
							TilePositionsToClear.Add(ScratchTilePoolGPU->GetPositionFromLinearAddress(Tile.TileAddressInScratch));
						}

						FRWBuffer TilePositionsBuffer;
						TilePositionsBuffer.Initialize(TilePositionsToClear.GetTypeSize(), TilePositionsToClear.Num(), PF_R32G32_UINT, 0, TEXT("TilePositionsBufferForClear"), &TilePositionsToClear);

						FMultiTileClearCS::FParameters* Parameters = GraphBuilder.AllocParameters<FMultiTileClearCS::FParameters>();
						Parameters->NumTiles = TilePositionsToClear.Num();
						Parameters->TileSize = GPreviewLightmapPhysicalTileSize;
						Parameters->TilePositions = TilePositionsBuffer.SRV;
						Parameters->TilePool = ScratchTilePoolGPU->PooledRenderTargets[ScratchLayerIndex]->GetRenderTargetItem().UAV;

						TShaderMapRef<FMultiTileClearCS> ComputeShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
						FComputeShaderUtils::AddPass(
							GraphBuilder,
							RDG_EVENT_NAME("MultiTileClear"),
							ComputeShader,
							Parameters,
							FComputeShaderUtils::GetGroupCount(FIntPoint(GPreviewLightmapPhysicalTileSize * TilePositionsToClear.Num(), GPreviewLightmapPhysicalTileSize), FComputeShaderUtils::kGolden2DGroupSize));

						GraphBuilder.Execute();
					}

					for (int ScratchLayerIndex = 0; ScratchLayerIndex < 3; ScratchLayerIndex++)
					{
						RHICmdList.Transition(FRHITransitionInfo(ScratchTilePoolGPU->PooledRenderTargets[ScratchLayerIndex]->GetRenderTargetItem().UAV, ERHIAccess::UAVGraphics, ERHIAccess::ERWBarrier));
					}

					FRDGBuilder GraphBuilder(RHICmdList);

					FRDGTextureRef GBufferWorldPosition = GraphBuilder.RegisterExternalTexture(ScratchTilePoolGPU->PooledRenderTargets[0], TEXT("GBufferWorldPosition"));
					FRDGTextureRef GBufferWorldNormal = GraphBuilder.RegisterExternalTexture(ScratchTilePoolGPU->PooledRenderTargets[1], TEXT("GBufferWorldNormal"));
					FRDGTextureRef GBufferShadingNormal = GraphBuilder.RegisterExternalTexture(ScratchTilePoolGPU->PooledRenderTargets[2], TEXT("GBufferShadingNormal"));

					FRDGTextureRef ShadowMask = GraphBuilder.RegisterExternalTexture(LightmapTilePoolGPU.PooledRenderTargets[2], TEXT("ShadowMask"));
					FRDGTextureRef ShadowMaskSampleCount = GraphBuilder.RegisterExternalTexture(LightmapTilePoolGPU.PooledRenderTargets[3], TEXT("ShadowMaskSampleCount"));

					TResourceArray<int32> LightTypeArray;
					FVertexBufferRHIRef LightTypeBuffer;
					FShaderResourceViewRHIRef LightTypeSRV;

					TResourceArray<int32> ChannelIndexArray;
					FVertexBufferRHIRef ChannelIndexBuffer;
					FShaderResourceViewRHIRef ChannelIndexSRV;

					TResourceArray<int32> LightSampleIndexArray;
					FVertexBufferRHIRef LightSampleIndexBuffer;
					FShaderResourceViewRHIRef LightSampleIndexSRV;

					TResourceArray<FLightShaderConstants> LightShaderParameterArray;
					FStructuredBufferRHIRef LightShaderParameterBuffer;
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
						FRHIResourceCreateInfo CreateInfo(&LightTypeArray);
						LightTypeBuffer = RHICreateVertexBuffer(LightTypeArray.GetResourceDataSize(), BUF_Static | BUF_ShaderResource, CreateInfo);
						LightTypeSRV = RHICreateShaderResourceView(LightTypeBuffer, sizeof(int32), PF_R32_SINT);
					}

					{
						FRHIResourceCreateInfo CreateInfo(&ChannelIndexArray);
						ChannelIndexBuffer = RHICreateVertexBuffer(ChannelIndexArray.GetResourceDataSize(), BUF_Static | BUF_ShaderResource, CreateInfo);
						ChannelIndexSRV = RHICreateShaderResourceView(ChannelIndexBuffer, sizeof(int32), PF_R32_SINT);
					}

					{
						FRHIResourceCreateInfo CreateInfo(&LightSampleIndexArray);
						LightSampleIndexBuffer = RHICreateVertexBuffer(LightSampleIndexArray.GetResourceDataSize(), BUF_Static | BUF_ShaderResource, CreateInfo);
						LightSampleIndexSRV = RHICreateShaderResourceView(LightSampleIndexBuffer, sizeof(int32), PF_R32_SINT);
					}

					{
						FRHIResourceCreateInfo CreateInfo(&LightShaderParameterArray);
						LightShaderParameterBuffer = RHICreateStructuredBuffer(sizeof(FLightShaderConstants), LightShaderParameterArray.GetResourceDataSize(), BUF_Static | BUF_ShaderResource, CreateInfo);
						LightShaderParameterSRV = RHICreateShaderResourceView(LightShaderParameterBuffer);
					}

					// Render GBuffer
					{
						FRHIRenderPassInfo RPInfo(FRHIRenderPassInfo::NoRenderTargets);
						RHICmdList.BeginRenderPass(RPInfo, TEXT("LightmapGBuffer"));

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
								*Scene->ReferenceView, RHICmdList,
								[
									View = Scene->ReferenceView.Get(),
									PassUniformBuffer,
									MeshBatches,
									VirtualTexturePhysicalTileCoordinateScaleAndBias,
									RenderPassIndex = LightSampleIndexArray[TileIndex],
									ScratchTilePoolOffset = ScratchTilePoolGPU->GetPositionFromLinearAddress(Tile.TileAddressInScratch) * GPreviewLightmapPhysicalTileSize
								](FDynamicPassMeshDrawListContext* DynamicMeshPassContext)
							{
								FLightmapGBufferMeshProcessor MeshProcessor(nullptr, View, DynamicMeshPassContext, PassUniformBuffer, VirtualTexturePhysicalTileCoordinateScaleAndBias, RenderPassIndex, ScratchTilePoolOffset);

								for (auto& MeshBatch : MeshBatches)
								{
									MeshProcessor.AddMeshBatch(MeshBatch, ~0ull, nullptr);
								}
							});

							GPrimitiveIdVertexBufferPool.DiscardAll();
						}

						RHICmdList.EndRenderPass();
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
								FRHIResourceCreateInfo CreateInfo;
								CreateInfo.ResourceArray = &GPUBatchedTileRequests.BatchedTilesDesc;

								GPUBatchedTileRequests.BatchedTilesBuffer = RHICreateStructuredBuffer(sizeof(FGPUTileDescription), GPUBatchedTileRequests.BatchedTilesDesc.GetResourceDataSize(), BUF_Static | BUF_ShaderResource, CreateInfo);
								GPUBatchedTileRequests.BatchedTilesSRV = RHICreateShaderResourceView(GPUBatchedTileRequests.BatchedTilesBuffer);
							}
						}

						FIntPoint RayTracingResolution;
						RayTracingResolution.X = GPreviewLightmapPhysicalTileSize * GPUBatchedTileRequests.BatchedTilesDesc.Num();
						RayTracingResolution.Y = GPreviewLightmapPhysicalTileSize;

						FStationaryLightShadowTracingRGS::FParameters* PassParameters = GraphBuilder.AllocParameters<FStationaryLightShadowTracingRGS::FParameters>();
						PassParameters->TLAS = Scene->RayTracingScene->GetShaderResourceView();
						PassParameters->BatchedTiles = GPUBatchedTileRequests.BatchedTilesSRV;
						PassParameters->LightTypeArray = LightTypeSRV;
						PassParameters->ChannelIndexArray = ChannelIndexSRV;
						PassParameters->LightSampleIndexArray = LightSampleIndexSRV;
						PassParameters->LightShaderParametersArray = LightShaderParameterSRV;
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
					GraphBuilder.Execute();
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
					FIntVector Min(GPURect.Min.X, GPURect.Min.Y, 0);
					FIntVector Max(GPURect.Max.X, GPURect.Max.Y, 1);

					Params.Add({ LightmapTilePoolGPU.PooledRenderTargets[RenderTargetIndex]->GetRenderTargetItem().TargetableTexture->GetTexture2D(), Min, Max, AssignedGPUIndex, 0, true });
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

		RHICmdList.TransferTextures(Params);
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

			FRHIResourceCreateInfo CreateInfo;
			CreateInfo.ResourceArray = &GPUBatchedTileRequests.BatchedTilesDesc;

			GPUBatchedTileRequests.BatchedTilesBuffer = RHICreateStructuredBuffer(sizeof(FGPUTileDescription), GPUBatchedTileRequests.BatchedTilesDesc.GetResourceDataSize(), BUF_Static | BUF_ShaderResource, CreateInfo);
			GPUBatchedTileRequests.BatchedTilesSRV = RHICreateShaderResourceView(GPUBatchedTileRequests.BatchedTilesBuffer);
		}

		{
			FRDGBuilder GraphBuilder(RHICmdList);

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

			GraphBuilder.Execute();
		}
	}

	Scene->DestroyRayTracingScene();

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
			int32 NewSize = FMath::CeilToInt(FMath::Sqrt(ConvergedTileRequests.Num()));

			for (const FLightmapTileRequest& Tile : ConvergedTileRequests)
			{
				Tile.RenderState->RetrieveTileState(Tile.VirtualCoordinates).OngoingReadbackRevision = CurrentRevision;
			}

			for (uint32 GPUIndex = 0; GPUIndex < GNumExplicitGPUsForRendering; GPUIndex++)
			{
				SCOPED_GPU_MASK(RHICmdList, FRHIGPUMask::FromIndex(GPUIndex));

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

				FRHIResourceCreateInfo CreateInfo;
				CreateInfo.ResourceArray = &GPUBatchedTileRequests.BatchedTilesDesc;

				GPUBatchedTileRequests.BatchedTilesBuffer = RHICreateStructuredBuffer(sizeof(FGPUTileDescription), GPUBatchedTileRequests.BatchedTilesDesc.GetResourceDataSize(), BUF_Static | BUF_ShaderResource, CreateInfo);
				GPUBatchedTileRequests.BatchedTilesSRV = RHICreateShaderResourceView(GPUBatchedTileRequests.BatchedTilesBuffer);


				FIntPoint DispatchResolution;
				DispatchResolution.X = GPreviewLightmapPhysicalTileSize * GPUBatchedTileRequests.BatchedTilesDesc.Num();
				DispatchResolution.Y = GPreviewLightmapPhysicalTileSize;

				FRDGBuilder GraphBuilder(RHICmdList);

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

				GraphBuilder.Execute();

				LightmapReadbackGroup.StagingHQLayer0Readback->EnqueueCopy(RHICmdList, LightmapReadbackGroup.ReadbackTilePoolGPU->PooledRenderTargets[0]->GetRenderTargetItem().TargetableTexture);
				LightmapReadbackGroup.StagingHQLayer1Readback->EnqueueCopy(RHICmdList, LightmapReadbackGroup.ReadbackTilePoolGPU->PooledRenderTargets[1]->GetRenderTargetItem().TargetableTexture);
				LightmapReadbackGroup.StagingShadowMaskReadback->EnqueueCopy(RHICmdList, LightmapReadbackGroup.ReadbackTilePoolGPU->PooledRenderTargets[2]->GetRenderTargetItem().TargetableTexture);

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

				const int32 DstRowPitchInPixels = Tile.RenderState->GetPaddedSizeAtMipLevel(Tile.VirtualCoordinates.MipLevel).X;
				const int32 SrcRowPitchInPixels = DenoiseTileProximity * GPreviewLightmapVirtualTileSize;

				for (int32 Y = 0; Y < GPreviewLightmapVirtualTileSize; Y++)
				{
					for (int32 X = 0; X < GPreviewLightmapVirtualTileSize; X++)
					{
						FIntPoint SrcPixelPosition = SrcTilePosition * GPreviewLightmapVirtualTileSize + FIntPoint(X, Y);
						FIntPoint DstPixelPosition = DstTilePosition * GPreviewLightmapVirtualTileSize + FIntPoint(X, Y);

						int32 SrcLinearIndex = SrcPixelPosition.Y * SrcRowPitchInPixels + SrcPixelPosition.X;
						int32 DstLinearIndex = DstPixelPosition.Y * DstRowPitchInPixels + DstPixelPosition.X;

						Tile.RenderState->CPUTextureData[0][Tile.VirtualCoordinates.MipLevel][DstLinearIndex] = DenoiseGroup.TextureData->Texture[0][SrcLinearIndex];
						Tile.RenderState->CPUTextureData[1][Tile.VirtualCoordinates.MipLevel][DstLinearIndex] = DenoiseGroup.TextureData->Texture[1][SrcLinearIndex];
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
				const int32 DstRowPitchInPixels = ReadbackGroup.ConvergedTileRequests[TileIndex].RenderState->GetPaddedSizeAtMipLevel(ReadbackGroup.ConvergedTileRequests[TileIndex].VirtualCoordinates.MipLevel).X;

				for (int32 Y = 0; Y < GPreviewLightmapVirtualTileSize; Y++)
				{
					for (int32 X = 0; X < GPreviewLightmapVirtualTileSize; X++)
					{
						FIntPoint SrcPixelPosition = SrcTilePosition * GPreviewLightmapPhysicalTileSize + FIntPoint(X, Y) + FIntPoint(GPreviewLightmapTileBorderSize, GPreviewLightmapTileBorderSize);
						FIntPoint DstPixelPosition = DstTilePosition * GPreviewLightmapVirtualTileSize + FIntPoint(X, Y);

						int32 SrcLinearIndex = SrcPixelPosition.Y * SrcRowPitchInPixels + SrcPixelPosition.X;
						int32 DstLinearIndex = DstPixelPosition.Y * DstRowPitchInPixels + DstPixelPosition.X;

						if (bDenoiseDuringInteractiveBake)
						{
							ReadbackGroup.ConvergedTileRequests[TileIndex].RenderState->CPUTextureRawData[0][ReadbackGroup.ConvergedTileRequests[TileIndex].VirtualCoordinates.MipLevel][DstLinearIndex] = ReadbackGroup.TextureData->Texture[0][SrcLinearIndex];
							ReadbackGroup.ConvergedTileRequests[TileIndex].RenderState->CPUTextureRawData[1][ReadbackGroup.ConvergedTileRequests[TileIndex].VirtualCoordinates.MipLevel][DstLinearIndex] = ReadbackGroup.TextureData->Texture[1][SrcLinearIndex];
						}

						// Always write into display data so we have something to show before denoising completes
						ReadbackGroup.ConvergedTileRequests[TileIndex].RenderState->CPUTextureData[0][ReadbackGroup.ConvergedTileRequests[TileIndex].VirtualCoordinates.MipLevel][DstLinearIndex] = ReadbackGroup.TextureData->Texture[0][SrcLinearIndex];
						ReadbackGroup.ConvergedTileRequests[TileIndex].RenderState->CPUTextureData[1][ReadbackGroup.ConvergedTileRequests[TileIndex].VirtualCoordinates.MipLevel][DstLinearIndex] = ReadbackGroup.TextureData->Texture[1][SrcLinearIndex];
						
						// For shadow maps, pass through
						ReadbackGroup.ConvergedTileRequests[TileIndex].RenderState->CPUTextureData[2][ReadbackGroup.ConvergedTileRequests[TileIndex].VirtualCoordinates.MipLevel][DstLinearIndex] = ReadbackGroup.TextureData->Texture[2][SrcLinearIndex];
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

					const int32 SrcRowPitchInPixels = Tile.RenderState->GetPaddedSizeAtMipLevel(Tile.VirtualCoordinates.MipLevel).X;
					const int32 DstRowPitchInPixels = DenoiseTileProximity * GPreviewLightmapVirtualTileSize;

					bool bShouldWriteZero = false;

					if (!Tile.RenderState->RetrieveTileState(FTileVirtualCoordinates(SrcTilePosition, Tile.VirtualCoordinates.MipLevel)).bCanBeDenoised)
					{
						bShouldWriteZero = true;
					}

					for (int32 Y = 0; Y < GPreviewLightmapVirtualTileSize; Y++)
					{
						for (int32 X = 0; X < GPreviewLightmapVirtualTileSize; X++)
						{
							FIntPoint SrcPixelPosition = SrcTilePosition * GPreviewLightmapVirtualTileSize + FIntPoint(X, Y);
							FIntPoint DstPixelPosition = DstTilePosition * GPreviewLightmapVirtualTileSize + FIntPoint(X, Y);

							int32 SrcLinearIndex = SrcPixelPosition.Y * SrcRowPitchInPixels + SrcPixelPosition.X;
							int32 DstLinearIndex = DstPixelPosition.Y * DstRowPitchInPixels + DstPixelPosition.X;

							DenoiseGroup.TextureData->Texture[0][DstLinearIndex] = !bShouldWriteZero ? Tile.RenderState->CPUTextureRawData[0][Tile.VirtualCoordinates.MipLevel][SrcLinearIndex] : FLinearColor(0, 0, 0, 0);
							DenoiseGroup.TextureData->Texture[1][DstLinearIndex] = !bShouldWriteZero ? Tile.RenderState->CPUTextureRawData[1][Tile.VirtualCoordinates.MipLevel][SrcLinearIndex] : FLinearColor(0, 0, 0, 0);
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
			int32 PoolSize = FMath::CeilToInt(FMath::Sqrt(NumWorkPerFrame * 3));

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

			GRenderTargetPool.FindFreeElement(RHICmdList, Desc, OutputTileAtlas, *FString::Printf(TEXT("BackgroundTilePoolGPU")));

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
	Finalize(RHICmdList);

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
	FRHICommandListImmediate& RHICmdList = *Parameters.RHICmdList;

	FVisualizeIrradianceCachePS::FParameters PassParameters;
	TUniformBufferRef<FViewUniformShaderParameters> Ref;
	*Ref.GetInitReference() = Parameters.ViewUniformBuffer;
	PassParameters.View = Ref;
	PassParameters.SceneTextures = CreateSceneTextureUniformBuffer(RHICmdList, GMaxRHIFeatureLevel, ESceneTextureSetupMode::All);
	PassParameters.IrradianceCachingParameters = Scene->IrradianceCache->IrradianceCachingParametersUniformBuffer;

	FUniformBufferStaticBindings GlobalUniformBuffers(PassParameters.SceneTextures);
	SCOPED_UNIFORM_BUFFER_GLOBAL_BINDINGS(RHICmdList, GlobalUniformBuffers);

	FRHIRenderPassInfo RPInfo(FSceneRenderTargets::Get(RHICmdList).GetSceneColor()->GetTargetableRHI(), ERenderTargetActions::Load_Store);
	RHICmdList.BeginRenderPass(RPInfo, TEXT("AP_ClearIrradiance"));

	RHICmdList.SetViewport(0, 0, 0.0f, Parameters.ViewportRect.Width(), Parameters.ViewportRect.Height(), 1.0f);

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

	SetShaderParameters(RHICmdList, PixelShader, PixelShader.GetPixelShader(), PassParameters);

	DrawRectangle(
		RHICmdList,
		0, 0,
		Parameters.ViewportRect.Width(), Parameters.ViewportRect.Height(),
		0, 0,
		Parameters.ViewportRect.Width(), Parameters.ViewportRect.Height(),
		FIntPoint(Parameters.ViewportRect.Width(), Parameters.ViewportRect.Height()),
		FSceneRenderTargets::Get(RHICmdList).GetBufferSizeXY(),
		VertexShader);

	RHICmdList.EndRenderPass();
}

}
