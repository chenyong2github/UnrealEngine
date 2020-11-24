// Copyright Epic Games, Inc. All Rights Reserved.

#include "VolumetricLightmap.h"
#include "VolumetricLightmapVoxelization.h"
#include "ScenePrivate.h"
#include "Containers/DynamicRHIResourceArray.h"
#include "LightmapRayTracing.h"
#include "GPULightmassModule.h"
#include "RHIGPUReadback.h"
#include "LevelEditorViewport.h"
#include "Editor.h"

IMPLEMENT_MATERIAL_SHADER_TYPE(, FVLMVoxelizationVS, TEXT("/Plugin/GPULightmass/Private/VolumetricLightmapVoxelization.usf"), TEXT("VLMVoxelizationVS"), SF_Vertex);
IMPLEMENT_MATERIAL_SHADER_TYPE(, FVLMVoxelizationGS, TEXT("/Plugin/GPULightmass/Private/VolumetricLightmapVoxelization.usf"), TEXT("VLMVoxelizationGS"), SF_Geometry);
IMPLEMENT_MATERIAL_SHADER_TYPE(, FVLMVoxelizationPS, TEXT("/Plugin/GPULightmass/Private/VolumetricLightmapVoxelization.usf"), TEXT("VLMVoxelizationPS"), SF_Pixel);
IMPLEMENT_GLOBAL_SHADER(FClearVolumeCS, "/Plugin/GPULightmass/Private/BrickAllocationManagement.usf", "ClearVolumeCS", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FVoxelizeImportanceVolumeCS, "/Plugin/GPULightmass/Private/BrickAllocationManagement.usf", "VoxelizeImportanceVolumeCS", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FDilateVolumeCS, "/Plugin/GPULightmass/Private/BrickAllocationManagement.usf", "DilateVolumeCS", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FDownsampleVolumeCS, "/Plugin/GPULightmass/Private/BrickAllocationManagement.usf", "DownsampleVolumeCS", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FCountNumBricksCS, "/Plugin/GPULightmass/Private/BrickAllocationManagement.usf", "CountNumBricksCS", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FGatherBrickRequestsCS, "/Plugin/GPULightmass/Private/BrickAllocationManagement.usf", "GatherBrickRequestsCS", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FSplatVolumeCS, "/Plugin/GPULightmass/Private/BrickAllocationManagement.usf", "SplatVolumeCS", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FStitchBorderCS, "/Plugin/GPULightmass/Private/BrickAllocationManagement.usf", "StitchBorderCS", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FFinalizeBrickResultsCS, "/Plugin/GPULightmass/Private/BrickAllocationManagement.usf", "FinalizeBrickResultsCS", SF_Compute);

IMPLEMENT_GLOBAL_SHADER_PARAMETER_STRUCT(FVLMVoxelizationParams, "VLMVoxelizationParams");

void InitializeBrickData(FIntVector BrickDataDimensions, FVolumetricLightmapBrickData& BrickData, const bool bForAccumulation)
{
	BrickData.AmbientVector.Format = bForAccumulation ? PF_A32B32G32R32F : PF_FloatR11G11B10;
	BrickData.SkyBentNormal.Format = bForAccumulation ? PF_A32B32G32R32F : PF_R8G8B8A8;
	BrickData.DirectionalLightShadowing.Format = PF_G8;

	for (int32 i = 0; i < UE_ARRAY_COUNT(BrickData.SHCoefficients); i++)
	{
		BrickData.SHCoefficients[i].Format = bForAccumulation ? PF_A32B32G32R32F : PF_R8G8B8A8;
	}

	BrickData.AmbientVector.CreateTargetTexture(BrickDataDimensions);
	BrickData.AmbientVector.CreateUAV();

	for (int32 i = 0; i < UE_ARRAY_COUNT(BrickData.SHCoefficients); i++)
	{
		BrickData.SHCoefficients[i].CreateTargetTexture(BrickDataDimensions);
		BrickData.SHCoefficients[i].CreateUAV();
	}

	//if (BrickData.SkyBentNormal.Texture.IsValid())
	//{
	//	SkyBentNormal.CreateTargetTexture(BrickDataDimensions);
	//	SkyBentNormal.CreateUAV();
	//}

	BrickData.DirectionalLightShadowing.CreateTargetTexture(BrickDataDimensions);
	BrickData.DirectionalLightShadowing.CreateUAV();
}

void ReleaseBrickData(FVolumetricLightmapBrickData& BrickData)
{
	BrickData.AmbientVector.Texture.SafeRelease();
	for (int32 i = 0; i < UE_ARRAY_COUNT(BrickData.SHCoefficients); i++)
	{
		BrickData.SHCoefficients[i].Texture.SafeRelease();
	}
	BrickData.SkyBentNormal.Texture.SafeRelease();
	BrickData.DirectionalLightShadowing.Texture.SafeRelease();

	BrickData.AmbientVector.UAV.SafeRelease();
	for (int32 i = 0; i < UE_ARRAY_COUNT(BrickData.SHCoefficients); i++)
	{
		BrickData.SHCoefficients[i].UAV.SafeRelease();
	}
	BrickData.SkyBentNormal.UAV.SafeRelease();
	BrickData.DirectionalLightShadowing.UAV.SafeRelease();
}

FPathTracingLightData SetupPathTracingLightParameters(const GPULightmass::FLightSceneRenderState& LightScene);
FSkyLightData SetupSkyLightParameters(const GPULightmass::FLightSceneRenderState& LightScene);

namespace GPULightmass
{

const int32 BrickSize = 4;
const int32 MaxRefinementLevels = 3;

FVolumetricLightmapRenderer::FVolumetricLightmapRenderer(FSceneRenderState* Scene)
	: Scene(Scene)
{
	VolumetricLightmap.Data = &VolumetricLightmapData;
}

FPrecomputedVolumetricLightmap* FVolumetricLightmapRenderer::GetPrecomputedVolumetricLightmapForPreview()
{
	return &VolumetricLightmap;
}

void FVolumetricLightmapRenderer::VoxelizeScene()
{
	for (int32 MipLevel = 0; MipLevel < VoxelizationVolumeMips.Num(); MipLevel++)
	{
		VoxelizationVolumeMips[MipLevel].SafeRelease();
	}

	IndirectionTexture.SafeRelease();

	ReleaseBrickData(VolumetricLightmapData.BrickData);
	ReleaseBrickData( AccumulationBrickData);

	FRHICommandListExecutor::GetImmediateCommandList().ImmediateFlush(EImmediateFlushType::FlushRHIThreadFlushResourcesFlushDeferredDeletes);

	VolumeMin = CombinedImportanceVolume.Min;

	FIntVector FullGridSize(
		FMath::TruncToInt(2 * CombinedImportanceVolume.GetExtent().X / TargetDetailCellSize) + 1,
		FMath::TruncToInt(2 * CombinedImportanceVolume.GetExtent().Y / TargetDetailCellSize) + 1,
		FMath::TruncToInt(2 * CombinedImportanceVolume.GetExtent().Z / TargetDetailCellSize) + 1);

	const int32 BrickSizeLog2 = FMath::FloorLog2(BrickSize);
	const int32 DetailCellsPerTopLevelBrick = 1 << (MaxRefinementLevels * BrickSizeLog2);

	FIntVector TopLevelGridSize = FIntVector::DivideAndRoundUp(FullGridSize, DetailCellsPerTopLevelBrick);

	VolumeSize = FVector(TopLevelGridSize) * DetailCellsPerTopLevelBrick * TargetDetailCellSize;
	FBox FinalVolume(VolumeMin, VolumeMin + VolumeSize);

	UE_LOG(LogGPULightmass, Log, TEXT("Volumetric lightmap voxelization bounds set to (%.2f, %.2f, %.2f) - (%.2f, %.2f, %.2f)"),
		FinalVolume.Min.X,
		FinalVolume.Min.Y,
		FinalVolume.Min.Z,
		FinalVolume.Max.X,
		FinalVolume.Max.Y,
		FinalVolume.Max.Z
	);

	const int32 IndirectionCellsPerTopLevelCell = DetailCellsPerTopLevelBrick / BrickSize;

	IndirectionTextureDimensions = TopLevelGridSize * IndirectionCellsPerTopLevelCell;

	{
		const FPooledRenderTargetDesc Desc = FPooledRenderTargetDesc::CreateVolumeDesc(
			IndirectionTextureDimensions.X, IndirectionTextureDimensions.Y, IndirectionTextureDimensions.Z,
			PF_R8G8B8A8_UINT,
			FClearValueBinding::Black,
			TexCreate_None,
			TexCreate_ShaderResource | TexCreate_RenderTargetable | TexCreate_UAV,
			false);

		GRenderTargetPool.FindFreeElement(FRHICommandListExecutor::GetImmediateCommandList(), Desc, IndirectionTexture, *FString::Printf(TEXT("GPULightmassVLMIndirectionTexture")));
	}

	VoxelizationVolumeMips.Empty();

	for (int32 Level = 0; Level < MaxRefinementLevels; Level++)
	{
		const FPooledRenderTargetDesc Desc = FPooledRenderTargetDesc::CreateVolumeDesc(
			IndirectionTextureDimensions.X >> (Level * BrickSizeLog2), IndirectionTextureDimensions.Y >> (Level * BrickSizeLog2), IndirectionTextureDimensions.Z >> (Level * BrickSizeLog2),
			PF_R32_UINT,
			FClearValueBinding::Black,
			TexCreate_None,
			TexCreate_ShaderResource | TexCreate_RenderTargetable | TexCreate_UAV,
			false);

		VoxelizationVolumeMips.AddDefaulted(1);

		// UE_LOG(LogGPULightmass, Log, TEXT("Creating voxelization mip%d %d %d %d"), Level, IndirectionTextureDimensions.X >> (Level * BrickSizeLog2), IndirectionTextureDimensions.Y >> (Level * BrickSizeLog2), IndirectionTextureDimensions.Z >> (Level * BrickSizeLog2));

		GRenderTargetPool.FindFreeElement(FRHICommandListExecutor::GetImmediateCommandList(), Desc, VoxelizationVolumeMips.Last(0), *FString::Printf(TEXT("GPULightmassVLMVoxelizationVolumeMips%d"), Level));
	}

	VolumetricLightmapData.Bounds = FinalVolume;
	VolumetricLightmapData.IndirectionTexture.Texture = IndirectionTexture->GetRenderTargetItem().ShaderResourceTexture->GetTexture3D();
	VolumetricLightmapData.IndirectionTexture.Format = PF_R8G8B8A8_UINT;
	VolumetricLightmapData.IndirectionTextureDimensions = FIntVector(IndirectionTextureDimensions);
	VolumetricLightmapData.BrickSize = 4;

	FBox CubeVolume(VolumeMin, VolumeMin + FVector(FMath::Max3(VolumeSize.X, VolumeSize.Y, VolumeSize.Z)));
	int32 CubeMaxDim = FMath::Max3(IndirectionTextureDimensions.X, IndirectionTextureDimensions.Y, IndirectionTextureDimensions.Z);

	FVLMVoxelizationParams VLMVoxelizationParams;
	VLMVoxelizationParams.VolumeCenter = CubeVolume.GetCenter();
	VLMVoxelizationParams.VolumeExtent = CubeVolume.GetExtent();
	VLMVoxelizationParams.VolumeMaxDim = CubeMaxDim;
	VLMVoxelizationParams.VoxelizeVolume = VoxelizationVolumeMips[0]->GetRenderTargetItem().UAV;
	VLMVoxelizationParams.IndirectionTexture = IndirectionTexture->GetRenderTargetItem().UAV;
	FVLMVoxelizationUniformBufferRef PassUniformBuffer = FVLMVoxelizationUniformBufferRef::CreateUniformBufferImmediate(VLMVoxelizationParams, UniformBuffer_SingleFrame);

	FRHICommandListImmediate& RHICmdList = FRHICommandListExecutor::GetImmediateCommandList();

	for (int32 MipLevel = 0; MipLevel < VoxelizationVolumeMips.Num(); MipLevel++)
	{
		FGlobalShaderMap* GlobalShaderMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);

		TShaderMapRef<FClearVolumeCS> ComputeShader(GlobalShaderMap);

		FClearVolumeCS::FParameters Parameters;
		Parameters.VolumeSize = VoxelizationVolumeMips[MipLevel]->GetDesc().GetSize();
		Parameters.VoxelizeVolume = VoxelizationVolumeMips[MipLevel]->GetRenderTargetItem().UAV;

		FComputeShaderUtils::Dispatch(RHICmdList, ComputeShader, Parameters, FComputeShaderUtils::GetGroupCount(VoxelizationVolumeMips[MipLevel]->GetDesc().GetSize(), FIntVector(4)));

		RHICmdList.Transition(FRHITransitionInfo(VoxelizationVolumeMips[MipLevel]->GetRenderTargetItem().UAV, ERHIAccess::Unknown, ERHIAccess::UAVCompute));
	}

	{
		for (FBox ImportanceVolume : ImportanceVolumes)
		{
			FGlobalShaderMap* GlobalShaderMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);

			TShaderMapRef<FVoxelizeImportanceVolumeCS> ComputeShader(GlobalShaderMap);

			FVoxelizeImportanceVolumeCS::FParameters Parameters;
			Parameters.VolumeSize = VoxelizationVolumeMips[0]->GetDesc().GetSize();
			Parameters.ImportanceVolumeMin = ImportanceVolume.Min;
			Parameters.ImportanceVolumeMax = ImportanceVolume.Max;
			Parameters.VLMVoxelizationParams = PassUniformBuffer;
			Parameters.VoxelizeVolume = VoxelizationVolumeMips[0]->GetRenderTargetItem().UAV;

			FComputeShaderUtils::Dispatch(RHICmdList, ComputeShader, Parameters, FComputeShaderUtils::GetGroupCount(VoxelizationVolumeMips[0]->GetDesc().GetSize(), FIntVector(4)));
		}

		RHICmdList.Transition(FRHITransitionInfo(VoxelizationVolumeMips[0]->GetRenderTargetItem().UAV, ERHIAccess::UAVCompute, ERHIAccess::UAVGraphics));
	}


	Scene->SetupRayTracingScene();

	FMemMark Mark(FMemStack::Get());

	FRHIRenderPassInfo RPInfo(FRHIRenderPassInfo::NoRenderTargets);

	RHICmdList.BeginRenderPass(RPInfo, TEXT("VolumetricLightmapVoxelization"));

	RHICmdList.SetViewport(0, 0, 0, CubeMaxDim, CubeMaxDim, 1);

	SCOPED_DRAW_EVENTF(RHICmdList, GPULightmassVoxelizeScene, TEXT("GPULightmass VoxelizeScene"));

	DrawDynamicMeshPass(
		*Scene->ReferenceView,
		RHICmdList,
		[&Scene = Scene, View = Scene->ReferenceView.Get(), PassUniformBuffer, ImportanceVolumes = ImportanceVolumes](FDynamicPassMeshDrawListContext* DynamicMeshPassContext)
	{
		FVLMVoxelizationMeshProcessor MeshProcessor(nullptr, View, DynamicMeshPassContext, PassUniformBuffer);

		for (int32 InstanceIndex = 0; InstanceIndex < Scene->StaticMeshInstanceRenderStates.Elements.Num(); InstanceIndex++)
		{
			FStaticMeshInstanceRenderState& Instance = Scene->StaticMeshInstanceRenderStates.Elements[InstanceIndex];

			bool bIntersectsAnyImportanceVolume = false;

			for (FBox ImportanceVolume : ImportanceVolumes)
			{
				if (Instance.WorldBounds.GetBox().Intersect(ImportanceVolume))
				{
					bIntersectsAnyImportanceVolume = true;
					break;
				}
			}

			if (!bIntersectsAnyImportanceVolume) continue;

			TArray<FMeshBatch> MeshBatches = Instance.GetMeshBatchesForGBufferRendering(0);

			for (auto& MeshBatch : MeshBatches)
			{
				MeshBatch.Elements[0].DynamicPrimitiveShaderDataIndex = InstanceIndex;
				MeshProcessor.AddMeshBatch(MeshBatch, ~0ull, nullptr);
			};
		}

		for (FInstanceGroupRenderState& InstanceGroup : Scene->InstanceGroupRenderStates.Elements)
		{
			bool bIntersectsAnyImportanceVolume = false;

			for (FBox ImportanceVolume : ImportanceVolumes)
			{
				if (InstanceGroup.WorldBounds.GetBox().Intersect(ImportanceVolume))
				{
					bIntersectsAnyImportanceVolume = true;
					break;
				}
			}

			if (!bIntersectsAnyImportanceVolume) continue;

			TArray<FMeshBatch> MeshBatches = InstanceGroup.GetMeshBatchesForGBufferRendering(0, FTileVirtualCoordinates{});

			for (auto& MeshBatch : MeshBatches)
			{
				MeshProcessor.AddMeshBatch(MeshBatch, ~0ull, nullptr);
			};
		}


		for (FLandscapeRenderState& Landscape : Scene->LandscapeRenderStates.Elements)
		{
			bool bIntersectsAnyImportanceVolume = false;

			for (FBox ImportanceVolume : ImportanceVolumes)
			{
				if (Landscape.WorldBounds.GetBox().Intersect(ImportanceVolume))
				{
					bIntersectsAnyImportanceVolume = true;
					break;
				}
			}

			if (!bIntersectsAnyImportanceVolume) continue;

			TArray<FMeshBatch> MeshBatches = Landscape.GetMeshBatchesForGBufferRendering(0);

			for (auto& MeshBatch : MeshBatches)
			{
				MeshProcessor.AddMeshBatch(MeshBatch, ~0ull, nullptr);
			};
		}
	});

	RHICmdList.EndRenderPass();

	RHICmdList.Transition(FRHITransitionInfo(VoxelizationVolumeMips[0]->GetRenderTargetItem().UAV, ERHIAccess::UAVGraphics, ERHIAccess::UAVCompute));

	{
		FGlobalShaderMap* GlobalShaderMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);

		TShaderMapRef<FDilateVolumeCS> ComputeShader(GlobalShaderMap);

		FDilateVolumeCS::FParameters Parameters;
		Parameters.VolumeSize = VoxelizationVolumeMips[0]->GetDesc().GetSize();
		Parameters.VoxelizeVolume = VoxelizationVolumeMips[0]->GetRenderTargetItem().UAV;

		FComputeShaderUtils::Dispatch(RHICmdList, ComputeShader, Parameters, FComputeShaderUtils::GetGroupCount(VoxelizationVolumeMips[0]->GetDesc().GetSize(), FIntVector(4)));
	}

	RHICmdList.Transition(FRHITransitionInfo(VoxelizationVolumeMips[0]->GetRenderTargetItem().UAV, ERHIAccess::UAVCompute, ERHIAccess::UAVCompute));

	for (int32 MipLevel = 1; MipLevel < VoxelizationVolumeMips.Num(); MipLevel++)
	{
		TShaderMapRef<FDownsampleVolumeCS> ComputeShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));

		FDownsampleVolumeCS::FParameters Parameters;
		Parameters.bIsHighestMip = (MipLevel == VoxelizationVolumeMips.Num() - 1) ? 1 : 0;
		Parameters.VoxelizeVolume = VoxelizationVolumeMips[MipLevel]->GetRenderTargetItem().UAV;
		Parameters.VoxelizeVolumePrevMip = VoxelizationVolumeMips[MipLevel - 1]->GetRenderTargetItem().UAV;

		FComputeShaderUtils::Dispatch(RHICmdList, ComputeShader, Parameters, VoxelizationVolumeMips[MipLevel]->GetDesc().GetSize());

		RHICmdList.Transition(FRHITransitionInfo(VoxelizationVolumeMips[MipLevel]->GetRenderTargetItem().UAV, ERHIAccess::UAVCompute, ERHIAccess::UAVCompute));
	}

	{
		TResourceArray<int32> InitialBrickAllocatorParams;
		InitialBrickAllocatorParams.Add(0);
		InitialBrickAllocatorParams.Add(0);
		BrickAllocatorParameters.Initialize(4, 2, PF_R32_SINT, BUF_UnorderedAccess | BUF_SourceCopy, TEXT("VolumetricLightmapBrickAllocatorParameters"), &InitialBrickAllocatorParams);
	}

	for (int32 MipLevel = VoxelizationVolumeMips.Num() - 1; MipLevel >= 0; MipLevel--)
	{
		FGlobalShaderMap* GlobalShaderMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);

		TShaderMapRef<FCountNumBricksCS> ComputeShader(GlobalShaderMap);

		FCountNumBricksCS::FParameters Parameters;
		Parameters.VolumeSize = VoxelizationVolumeMips[MipLevel]->GetDesc().GetSize();
		Parameters.VoxelizeVolume = VoxelizationVolumeMips[MipLevel]->GetRenderTargetItem().UAV;
		Parameters.BrickAllocatorParameters = BrickAllocatorParameters.UAV;

		FComputeShaderUtils::Dispatch(RHICmdList, ComputeShader, Parameters, FComputeShaderUtils::GetGroupCount(VoxelizationVolumeMips[MipLevel]->GetDesc().GetSize(), FIntVector(4)));

		RHICmdList.Transition(FRHITransitionInfo(VoxelizationVolumeMips[MipLevel]->GetRenderTargetItem().UAV, ERHIAccess::UAVCompute, ERHIAccess::UAVCompute));
		RHICmdList.Transition(FRHITransitionInfo(BrickAllocatorParameters.UAV, ERHIAccess::Unknown, ERHIAccess::UAVCompute));
	}

	FRHIGPUBufferReadback NumBricksReadback(TEXT("NumBricksReadback"));
	NumBricksReadback.EnqueueCopy(RHICmdList, BrickAllocatorParameters.Buffer);
	RHICmdList.BlockUntilGPUIdle();
	check(NumBricksReadback.IsReady());
	{
		int32* Buffer = (int32*)NumBricksReadback.Lock(8);
		NumTotalBricks = Buffer[0];
		UE_LOG(LogGPULightmass, Log, TEXT("Volumetric lightmap NumTotalBricks = %d"), NumTotalBricks);
		NumBricksReadback.Unlock();
	}

	if (NumTotalBricks == 0)
	{
		return;
	}

	int32 MaxBricksInLayoutOneDim = 256;

	FIntVector BrickLayoutDimensions;

	{
		int32 BrickTextureLinearAllocator = NumTotalBricks;
		BrickLayoutDimensions.X = FMath::Min(BrickTextureLinearAllocator, MaxBricksInLayoutOneDim);
		BrickTextureLinearAllocator = FMath::DivideAndRoundUp(BrickTextureLinearAllocator, BrickLayoutDimensions.X);
		BrickLayoutDimensions.Y = FMath::Min(BrickTextureLinearAllocator, MaxBricksInLayoutOneDim);
		BrickTextureLinearAllocator = FMath::DivideAndRoundUp(BrickTextureLinearAllocator, BrickLayoutDimensions.Y);
		BrickLayoutDimensions.Z = FMath::Min(BrickTextureLinearAllocator, MaxBricksInLayoutOneDim);
	}

	InitializeBrickData(BrickLayoutDimensions * 5, VolumetricLightmapData.BrickData, false);
	InitializeBrickData(BrickLayoutDimensions * 5, AccumulationBrickData, true);
	BrickRequests.Initialize(16, NumTotalBricks, PF_R32G32B32A32_UINT, BUF_UnorderedAccess);

	VolumetricLightmapData.BrickDataDimensions = BrickLayoutDimensions * 5;

	for (int32 MipLevel = VoxelizationVolumeMips.Num() - 1; MipLevel >= 0; MipLevel--)
	{
		FGlobalShaderMap* GlobalShaderMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);

		TShaderMapRef<FGatherBrickRequestsCS> ComputeShader(GlobalShaderMap);

		FGatherBrickRequestsCS::FParameters Parameters;
		Parameters.VolumeSize = VoxelizationVolumeMips[MipLevel]->GetDesc().GetSize();
		Parameters.BrickSize = 1 << (MipLevel * BrickSizeLog2);
		Parameters.VoxelizeVolume = VoxelizationVolumeMips[MipLevel]->GetRenderTargetItem().UAV;
		Parameters.BrickAllocatorParameters = BrickAllocatorParameters.UAV;
		Parameters.BrickRequests = BrickRequests.UAV;

		FComputeShaderUtils::Dispatch(RHICmdList, ComputeShader, Parameters, FComputeShaderUtils::GetGroupCount(VoxelizationVolumeMips[MipLevel]->GetDesc().GetSize(), FIntVector(4)));

		RHICmdList.Transition(FRHITransitionInfo(BrickRequests.UAV, ERHIAccess::Unknown, ERHIAccess::UAVCompute));
	}

	for (int32 MipLevel = VoxelizationVolumeMips.Num() - 1; MipLevel >= 0; MipLevel--)
	{
		FGlobalShaderMap* GlobalShaderMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);

		TShaderMapRef<FSplatVolumeCS> ComputeShader(GlobalShaderMap);

		FSplatVolumeCS::FParameters Parameters;
		Parameters.VolumeSize = IndirectionTextureDimensions;
		Parameters.BrickSize = 1 << (MipLevel * BrickSizeLog2);
		Parameters.bIsHighestMip = MipLevel == VoxelizationVolumeMips.Num() - 1;
		Parameters.VoxelizeVolume = VoxelizationVolumeMips[MipLevel]->GetRenderTargetItem().UAV;
		Parameters.IndirectionTexture = IndirectionTexture->GetRenderTargetItem().UAV;
		Parameters.BrickAllocatorParameters = BrickAllocatorParameters.UAV;

		FComputeShaderUtils::Dispatch(RHICmdList, ComputeShader, Parameters, FComputeShaderUtils::GetGroupCount(IndirectionTextureDimensions, FIntVector(4)));

		RHICmdList.Transition(FRHITransitionInfo(IndirectionTexture->GetRenderTargetItem().UAV, ERHIAccess::UAVCompute, ERHIAccess::UAVCompute));
	}

	Scene->DestroyRayTracingScene();
}

int32 FVolumetricLightmapRenderer::GetGISamplesMultiplier()
{
	if (Scene->Settings->GISamples < 1024)
	{
		return 4;
	}

	if (Scene->Settings->GISamples < 4096)
	{
		return 2;
	}

	return 1;
}
void FVolumetricLightmapRenderer::BackgroundTick()
{
	if (NumTotalBricks == 0)
	{
		return;
	}

	int32 NumCellsPerBrick = 5 * 5 * 5;
	if (SamplesTaken >= (uint64)NumTotalBricks * NumCellsPerBrick * Scene->Settings->GISamples * GetGISamplesMultiplier())
	{
		return;
	}

	TRACE_CPUPROFILER_EVENT_SCOPE(FVolumetricLightmapRenderer::BackgroundTick);

	FMemMark Mark(FMemStack::Get());

	if (IsRayTracingEnabled())
	{
		Scene->SetupRayTracingScene();
	}

	FRHICommandListImmediate& RHICmdList = FRHICommandListExecutor::GetImmediateCommandList();

	bool bLastFewFramesIdle = !GCurrentLevelEditingViewportClient || !GCurrentLevelEditingViewportClient->IsRealtime();

	int32 NumSamplesThisFrame = !bLastFewFramesIdle ? 1 : 32;

	RHICmdList.Transition({
		FRHITransitionInfo(VolumetricLightmapData.BrickData.AmbientVector.UAV, ERHIAccess::Unknown, ERHIAccess::UAVCompute),
		FRHITransitionInfo(VolumetricLightmapData.BrickData.SHCoefficients[0].UAV, ERHIAccess::Unknown, ERHIAccess::UAVCompute),
		FRHITransitionInfo(VolumetricLightmapData.BrickData.SHCoefficients[1].UAV, ERHIAccess::Unknown, ERHIAccess::UAVCompute),
		FRHITransitionInfo(VolumetricLightmapData.BrickData.SHCoefficients[2].UAV, ERHIAccess::Unknown, ERHIAccess::UAVCompute),
		FRHITransitionInfo(VolumetricLightmapData.BrickData.SHCoefficients[3].UAV, ERHIAccess::Unknown, ERHIAccess::UAVCompute),
		FRHITransitionInfo(VolumetricLightmapData.BrickData.SHCoefficients[4].UAV, ERHIAccess::Unknown, ERHIAccess::UAVCompute),
		FRHITransitionInfo(VolumetricLightmapData.BrickData.SHCoefficients[5].UAV, ERHIAccess::Unknown, ERHIAccess::UAVCompute)
	});

	for (int32 SampleIndex = 0; SampleIndex < NumSamplesThisFrame; SampleIndex++)
	{
		int32 MaxBricksPerFrame = FMath::Min(512, NumTotalBricks);
		int32 NumFramesOneRound = FMath::DivideAndRoundUp(NumTotalBricks, MaxBricksPerFrame);
		int32 BrickBatchOffset = MaxBricksPerFrame * (FrameNumber % NumFramesOneRound);
		int32 BricksToCalcThisFrame = FMath::Min(MaxBricksPerFrame, NumTotalBricks - BrickBatchOffset);
		if (BricksToCalcThisFrame <= 0) continue;

#if RHI_RAYTRACING
		if (IsRayTracingEnabled())
		{
			SCOPED_DRAW_EVENTF(RHICmdList, VolumetricLightmapPathTracing, TEXT("VolumetricLightmapPathTracing %d bricks %d rays"), BricksToCalcThisFrame, BricksToCalcThisFrame * (BrickSize + 1) * (BrickSize + 1) * (BrickSize + 1));

			// These two buffers must have lifetime extended beyond RHICmdList.RayTraceDispatch()
			TUniformBufferRef<FPathTracingLightData> LightDataUniformBuffer;
			TUniformBufferRef<FSkyLightData> SkyLightDataUniformBuffer;

			FGlobalShaderMap* GlobalShaderMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);

			{
				FVolumetricLightmapPathTracingRGS::FPermutationDomain PermutationVector;
				PermutationVector.Set<FVolumetricLightmapPathTracingRGS::FUseIrradianceCaching>(Scene->Settings->bUseIrradianceCaching);
				TShaderRef<FVolumetricLightmapPathTracingRGS> RayGenShader = GlobalShaderMap->GetShader<FVolumetricLightmapPathTracingRGS>(PermutationVector);

				FVolumetricLightmapPathTracingRGS::FParameters Parameters;
				Parameters.FrameNumber = FrameNumber / NumFramesOneRound;
				Parameters.VolumeMin = VolumeMin;
				Parameters.VolumeSize = VolumeSize;
				Parameters.IndirectionTextureDim = IndirectionTextureDimensions;
				Parameters.TLAS = Scene->RayTracingScene->GetShaderResourceView();
				Parameters.BrickRequests = BrickRequests.SRV;
				Parameters.NumTotalBricks = NumTotalBricks;
				Parameters.BrickBatchOffset = BrickBatchOffset;
				Parameters.AmbientVector = AccumulationBrickData.AmbientVector.UAV;
				Parameters.SHCoefficients0R = AccumulationBrickData.SHCoefficients[0].UAV;
				Parameters.SHCoefficients1R = AccumulationBrickData.SHCoefficients[1].UAV;
				Parameters.SHCoefficients0G = AccumulationBrickData.SHCoefficients[2].UAV;
				Parameters.SHCoefficients1G = AccumulationBrickData.SHCoefficients[3].UAV;
				Parameters.SHCoefficients0B = AccumulationBrickData.SHCoefficients[4].UAV;
				Parameters.SHCoefficients1B = AccumulationBrickData.SHCoefficients[5].UAV;
				Parameters.OutAmbientVector = VolumetricLightmapData.BrickData.AmbientVector.UAV;
				Parameters.OutSHCoefficients0R = VolumetricLightmapData.BrickData.SHCoefficients[0].UAV;
				Parameters.OutSHCoefficients1R = VolumetricLightmapData.BrickData.SHCoefficients[1].UAV;
				Parameters.OutSHCoefficients0G = VolumetricLightmapData.BrickData.SHCoefficients[2].UAV;
				Parameters.OutSHCoefficients1G = VolumetricLightmapData.BrickData.SHCoefficients[3].UAV;
				Parameters.OutSHCoefficients0B = VolumetricLightmapData.BrickData.SHCoefficients[4].UAV;
				Parameters.OutSHCoefficients1B = VolumetricLightmapData.BrickData.SHCoefficients[5].UAV;
				Parameters.ViewUniformBuffer = Scene->ReferenceView->ViewUniformBuffer;
				Parameters.IrradianceCachingParameters = Scene->IrradianceCache->IrradianceCachingParametersUniformBuffer;

				{
					LightDataUniformBuffer = CreateUniformBufferImmediate(SetupPathTracingLightParameters(Scene->LightSceneRenderState), EUniformBufferUsage::UniformBuffer_SingleFrame);
					Parameters.LightParameters = LightDataUniformBuffer;
				}

				{
					SkyLightDataUniformBuffer = CreateUniformBufferImmediate(SetupSkyLightParameters(Scene->LightSceneRenderState), EUniformBufferUsage::UniformBuffer_SingleFrame);
					Parameters.SkyLight = SkyLightDataUniformBuffer;
				}

				FRayTracingShaderBindingsWriter GlobalResources;
				SetShaderParameters(GlobalResources, RayGenShader, Parameters);

				FRHIRayTracingScene* RayTracingSceneRHI = Scene->RayTracingScene;
				RHICmdList.RayTraceDispatch(Scene->RayTracingPipelineState, RayGenShader.GetRayTracingShader(), RayTracingSceneRHI, GlobalResources, BricksToCalcThisFrame * (BrickSize + 1) * (BrickSize + 1) * (BrickSize + 1), 1);
			}
		}
#endif
		{
			RHICmdList.Transition({
				FRHITransitionInfo(AccumulationBrickData.AmbientVector.UAV, ERHIAccess::UAVCompute, ERHIAccess::UAVCompute),
				FRHITransitionInfo(AccumulationBrickData.SHCoefficients[0].UAV, ERHIAccess::UAVCompute, ERHIAccess::UAVCompute),
				FRHITransitionInfo(AccumulationBrickData.SHCoefficients[1].UAV, ERHIAccess::UAVCompute, ERHIAccess::UAVCompute),
				FRHITransitionInfo(AccumulationBrickData.SHCoefficients[2].UAV, ERHIAccess::UAVCompute, ERHIAccess::UAVCompute),
				FRHITransitionInfo(AccumulationBrickData.SHCoefficients[3].UAV, ERHIAccess::UAVCompute, ERHIAccess::UAVCompute),
				FRHITransitionInfo(AccumulationBrickData.SHCoefficients[4].UAV, ERHIAccess::UAVCompute, ERHIAccess::UAVCompute),
				FRHITransitionInfo(AccumulationBrickData.SHCoefficients[5].UAV, ERHIAccess::UAVCompute, ERHIAccess::UAVCompute),
			});

			FGlobalShaderMap* GlobalShaderMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);

			TShaderMapRef<FFinalizeBrickResultsCS> ComputeShader(GlobalShaderMap);

			FFinalizeBrickResultsCS::FParameters Parameters;
			Parameters.NumTotalBricks = NumTotalBricks;
			Parameters.BrickBatchOffset = BrickBatchOffset;
			Parameters.BrickRequests = BrickRequests.UAV;
			Parameters.AmbientVector = AccumulationBrickData.AmbientVector.Texture;
			Parameters.SHCoefficients0R = AccumulationBrickData.SHCoefficients[0].Texture;
			Parameters.SHCoefficients1R = AccumulationBrickData.SHCoefficients[1].Texture;
			Parameters.SHCoefficients0G = AccumulationBrickData.SHCoefficients[2].Texture;
			Parameters.SHCoefficients1G = AccumulationBrickData.SHCoefficients[3].Texture;
			Parameters.SHCoefficients0B = AccumulationBrickData.SHCoefficients[4].Texture;
			Parameters.SHCoefficients1B = AccumulationBrickData.SHCoefficients[5].Texture;
			Parameters.OutAmbientVector = VolumetricLightmapData.BrickData.AmbientVector.UAV;
			Parameters.OutSHCoefficients0R = VolumetricLightmapData.BrickData.SHCoefficients[0].UAV;
			Parameters.OutSHCoefficients1R = VolumetricLightmapData.BrickData.SHCoefficients[1].UAV;
			Parameters.OutSHCoefficients0G = VolumetricLightmapData.BrickData.SHCoefficients[2].UAV;
			Parameters.OutSHCoefficients1G = VolumetricLightmapData.BrickData.SHCoefficients[3].UAV;
			Parameters.OutSHCoefficients0B = VolumetricLightmapData.BrickData.SHCoefficients[4].UAV;
			Parameters.OutSHCoefficients1B = VolumetricLightmapData.BrickData.SHCoefficients[5].UAV;

			FComputeShaderUtils::Dispatch(RHICmdList, ComputeShader, Parameters, FIntVector(BricksToCalcThisFrame, 1, 1));

			RHICmdList.Transition({
				FRHITransitionInfo(AccumulationBrickData.AmbientVector.UAV, ERHIAccess::UAVCompute, ERHIAccess::UAVCompute),
				FRHITransitionInfo(AccumulationBrickData.SHCoefficients[0].UAV, ERHIAccess::UAVCompute, ERHIAccess::UAVCompute),
				FRHITransitionInfo(AccumulationBrickData.SHCoefficients[1].UAV, ERHIAccess::UAVCompute, ERHIAccess::UAVCompute),
				FRHITransitionInfo(AccumulationBrickData.SHCoefficients[2].UAV, ERHIAccess::UAVCompute, ERHIAccess::UAVCompute),
				FRHITransitionInfo(AccumulationBrickData.SHCoefficients[3].UAV, ERHIAccess::UAVCompute, ERHIAccess::UAVCompute),
				FRHITransitionInfo(AccumulationBrickData.SHCoefficients[4].UAV, ERHIAccess::UAVCompute, ERHIAccess::UAVCompute),
				FRHITransitionInfo(AccumulationBrickData.SHCoefficients[5].UAV, ERHIAccess::UAVCompute, ERHIAccess::UAVCompute),
			});
		}

		{
			SCOPED_DRAW_EVENTF(RHICmdList, VolumetricLightmapStitching, TEXT("VolumetricLightmapStitching %d bricks"), BricksToCalcThisFrame);

			// Doing 2 passes no longer makes sense in an amortized setup
			// for (int32 StitchPass = 0; StitchPass < 2; StitchPass++)
			{
				FGlobalShaderMap* GlobalShaderMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);

				TShaderMapRef<FStitchBorderCS> ComputeShader(GlobalShaderMap);

				FStitchBorderCS::FParameters Parameters;
				Parameters.BrickDataDimensions = VolumetricLightmapData.BrickDataDimensions;
				Parameters.IndirectionTextureDim = IndirectionTextureDimensions;
				Parameters.FrameNumber = FrameNumber / NumFramesOneRound;
				Parameters.NumTotalBricks = NumTotalBricks;
				Parameters.BrickBatchOffset = BrickBatchOffset;
				Parameters.IndirectionTexture = IndirectionTexture->GetRenderTargetItem().UAV;
				Parameters.BrickRequests = BrickRequests.UAV;
				Parameters.AmbientVector = AccumulationBrickData.AmbientVector.Texture;
				Parameters.OutAmbientVector = VolumetricLightmapData.BrickData.AmbientVector.UAV;
				Parameters.OutSHCoefficients0R = VolumetricLightmapData.BrickData.SHCoefficients[0].UAV;
				Parameters.OutSHCoefficients1R = VolumetricLightmapData.BrickData.SHCoefficients[1].UAV;
				Parameters.OutSHCoefficients0G = VolumetricLightmapData.BrickData.SHCoefficients[2].UAV;
				Parameters.OutSHCoefficients1G = VolumetricLightmapData.BrickData.SHCoefficients[3].UAV;
				Parameters.OutSHCoefficients0B = VolumetricLightmapData.BrickData.SHCoefficients[4].UAV;
				Parameters.OutSHCoefficients1B = VolumetricLightmapData.BrickData.SHCoefficients[5].UAV;

				FComputeShaderUtils::Dispatch(RHICmdList, ComputeShader, Parameters, FIntVector(BricksToCalcThisFrame, 1, 1));

				RHICmdList.Transition({
					FRHITransitionInfo(AccumulationBrickData.AmbientVector.UAV, ERHIAccess::UAVCompute, ERHIAccess::UAVCompute),
					FRHITransitionInfo(AccumulationBrickData.SHCoefficients[0].UAV, ERHIAccess::UAVCompute, ERHIAccess::UAVCompute),
					FRHITransitionInfo(AccumulationBrickData.SHCoefficients[1].UAV, ERHIAccess::UAVCompute, ERHIAccess::UAVCompute),
					FRHITransitionInfo(AccumulationBrickData.SHCoefficients[2].UAV, ERHIAccess::UAVCompute, ERHIAccess::UAVCompute),
					FRHITransitionInfo(AccumulationBrickData.SHCoefficients[3].UAV, ERHIAccess::UAVCompute, ERHIAccess::UAVCompute),
					FRHITransitionInfo(AccumulationBrickData.SHCoefficients[4].UAV, ERHIAccess::UAVCompute, ERHIAccess::UAVCompute),
					FRHITransitionInfo(AccumulationBrickData.SHCoefficients[5].UAV, ERHIAccess::UAVCompute, ERHIAccess::UAVCompute)
				});
			}
		}

		FrameNumber++;

		SamplesTaken += BricksToCalcThisFrame * NumCellsPerBrick;

		if (SamplesTaken >= (uint64)NumTotalBricks * NumCellsPerBrick * Scene->Settings->GISamples * GetGISamplesMultiplier())
		{
			break;
		}
	}

	if (IsRayTracingEnabled())
	{
		Scene->DestroyRayTracingScene();
	}
}

}
