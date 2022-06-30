// Copyright Epic Games, Inc. All Rights Reserved.

#include "VolumetricLightmap.h"
#include "VolumetricLightmapVoxelization.h"
#include "ScenePrivate.h"
#include "Containers/DynamicRHIResourceArray.h"
#include "LightmapRayTracing.h"
#include "PathTracingLightParameters.inl"
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

IMPLEMENT_STATIC_UNIFORM_BUFFER_STRUCT(FVLMVoxelizationParams, "VLMVoxelizationParams", SceneTextures);

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

	BrickData.SkyBentNormal.CreateTargetTexture(BrickDataDimensions);
	BrickData.SkyBentNormal.CreateUAV();

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

namespace GPULightmass
{

const int32 BrickSize = 4;
const int32 MaxRefinementLevels = 3;

FVolumetricLightmapRenderer::FVolumetricLightmapRenderer(FSceneRenderState* Scene)
	: Scene(Scene)
{
	VolumetricLightmap.Data = &VolumetricLightmapData;
	
	NumTotalPassesToRender = Scene->Settings->GISamples;
	
	if (Scene->Settings->bUseIrradianceCaching)
	{
		NumTotalPassesToRender += Scene->Settings->IrradianceCacheQuality;	
	}

	NumTotalPassesToRender *= Scene->Settings->VolumetricLightmapQualityMultiplier;
}

FPrecomputedVolumetricLightmap* FVolumetricLightmapRenderer::GetPrecomputedVolumetricLightmapForPreview()
{
	return &VolumetricLightmap;
}

BEGIN_SHADER_PARAMETER_STRUCT(FVoxelizeMeshPassParameters, )
	SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
	SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FVLMVoxelizationParams, PassUniformBuffer)
	SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FInstanceCullingGlobalUniforms, InstanceCulling)
	RENDER_TARGET_BINDING_SLOTS()
END_SHADER_PARAMETER_STRUCT()

void FVolumetricLightmapRenderer::VoxelizeScene()
{
	FGlobalShaderMap* GlobalShaderMap = GetGlobalShaderMap(Scene->FeatureLevel);

	for (int32 MipLevel = 0; MipLevel < VoxelizationVolumeMips.Num(); MipLevel++)
	{
		VoxelizationVolumeMips[MipLevel].SafeRelease();
	}

	IndirectionTexture.SafeRelease();

	ReleaseBrickData(VolumetricLightmapData.BrickData);
	ReleaseBrickData(AccumulationBrickData);

	FRHICommandListExecutor::GetImmediateCommandList().ImmediateFlush(EImmediateFlushType::FlushRHIThreadFlushResources);

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

	VoxelizationVolumeMips.Empty();

	FRHICommandListImmediate& RHICmdList = FRHICommandListExecutor::GetImmediateCommandList();

	{
		FRDGBuilder GraphBuilder(RHICmdList);
		ON_SCOPE_EXIT{ GraphBuilder.Execute(); };

		FRDGTextureUAV* IndirectionTextureUAV = nullptr;

		{
			const FRDGTextureDesc Desc = FRDGTextureDesc::Create3D(
				FIntVector(IndirectionTextureDimensions.X, IndirectionTextureDimensions.Y, IndirectionTextureDimensions.Z),
				PF_R8G8B8A8_UINT,
				FClearValueBinding::Black,
				ETextureCreateFlags::ShaderResource | ETextureCreateFlags::RenderTargetable | ETextureCreateFlags::UAV);

			FRDGTexture* Texture = GraphBuilder.CreateTexture(Desc, TEXT("GPULightmassVLMIndirectionTexture"));
			IndirectionTexture = GraphBuilder.ConvertToExternalTexture(Texture);
			IndirectionTextureUAV = GraphBuilder.CreateUAV(Texture);
		}

		TArray<FRDGTextureUAV*, FRDGArrayAllocator> VoxelizationVolumeMipUAVs;

		for (int32 Level = 0; Level < MaxRefinementLevels; Level++)
		{
			const FRDGTextureDesc Desc = FRDGTextureDesc::Create3D(
				FIntVector(IndirectionTextureDimensions.X >> (Level * BrickSizeLog2), IndirectionTextureDimensions.Y >> (Level * BrickSizeLog2), IndirectionTextureDimensions.Z >> (Level * BrickSizeLog2)),
				PF_R32_UINT,
				FClearValueBinding::Black,
				ETextureCreateFlags::ShaderResource | ETextureCreateFlags::RenderTargetable | ETextureCreateFlags::UAV);

			FRDGTexture* Texture = GraphBuilder.CreateTexture(Desc, TEXT("GPULightmassVLMVoxelizationVolumeMips"));

			VoxelizationVolumeMips.Emplace(GraphBuilder.ConvertToExternalTexture(Texture));
			VoxelizationVolumeMipUAVs.Emplace(GraphBuilder.CreateUAV(Texture));
		}

		VolumetricLightmapData.Bounds = FinalVolume;
		VolumetricLightmapData.IndirectionTexture.Texture = IndirectionTexture->GetRHI();
		VolumetricLightmapData.IndirectionTexture.Format = PF_R8G8B8A8_UINT;
		VolumetricLightmapData.IndirectionTextureDimensions = FIntVector(IndirectionTextureDimensions);
		VolumetricLightmapData.BrickSize = 4;

		FBox CubeVolume(VolumeMin, VolumeMin + FVector(FMath::Max3(VolumeSize.X, VolumeSize.Y, VolumeSize.Z)));
		int32 CubeMaxDim = FMath::Max3(IndirectionTextureDimensions.X, IndirectionTextureDimensions.Y, IndirectionTextureDimensions.Z);

		FRDGTexture* VoxelizationVolumeMipsRDG = GraphBuilder.RegisterExternalTexture(VoxelizationVolumeMips[0]);
		FRDGTexture* IndirectTextureRDG = GraphBuilder.RegisterExternalTexture(IndirectionTexture);

		FVLMVoxelizationParams* VLMVoxelizationParams = GraphBuilder.AllocParameters<FVLMVoxelizationParams>();
		VLMVoxelizationParams->VolumeCenter = (FVector3f)CubeVolume.GetCenter(); // LWC_TODO: precision loss
		VLMVoxelizationParams->VolumeExtent = (FVector3f)CubeVolume.GetExtent(); // LWC_TODO: precision loss
		VLMVoxelizationParams->VolumeMaxDim = CubeMaxDim;
		VLMVoxelizationParams->VoxelizeVolume = VoxelizationVolumeMipUAVs[0];
		VLMVoxelizationParams->IndirectionTexture = IndirectionTextureUAV;
		TRDGUniformBufferRef<FVLMVoxelizationParams> PassUniformBuffer = GraphBuilder.CreateUniformBuffer(VLMVoxelizationParams);

		for (int32 MipLevel = 0; MipLevel < VoxelizationVolumeMips.Num(); MipLevel++)
		{
			FClearVolumeCS::FParameters* Parameters = GraphBuilder.AllocParameters<FClearVolumeCS::FParameters>();
			Parameters->VolumeSize = VoxelizationVolumeMips[MipLevel]->GetDesc().GetSize();
			Parameters->VoxelizeVolume = VoxelizationVolumeMipUAVs[MipLevel];

			TShaderMapRef<FClearVolumeCS> ComputeShader(GlobalShaderMap);
			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("ClearVolume"),
				ComputeShader,
				Parameters,
				FComputeShaderUtils::GetGroupCount(VoxelizationVolumeMips[MipLevel]->GetDesc().GetSize(), FIntVector(4)));
		}

		for (FBox ImportanceVolume : ImportanceVolumes)
		{
			TShaderMapRef<FVoxelizeImportanceVolumeCS> ComputeShader(GlobalShaderMap);

			FVoxelizeImportanceVolumeCS::FParameters* Parameters = GraphBuilder.AllocParameters<FVoxelizeImportanceVolumeCS::FParameters>();
			Parameters->VolumeSize = VoxelizationVolumeMips[0]->GetDesc().GetSize();
			Parameters->ImportanceVolumeMin = (FVector3f)ImportanceVolume.Min;
			Parameters->ImportanceVolumeMax = (FVector3f)ImportanceVolume.Max;
			Parameters->VLMVoxelizationParams = PassUniformBuffer;
			Parameters->VoxelizeVolume = VoxelizationVolumeMipUAVs[0];

			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("VoxelizeImportanceVolume"),
				ComputeShader,
				Parameters,
				FComputeShaderUtils::GetGroupCount(VoxelizationVolumeMips[0]->GetDesc().GetSize(), FIntVector(4)));
		}

		// Setup ray tracing scene with LOD 0
		if (!Scene->SetupRayTracingScene())
		{
			return;
		}

		auto* PassParameters = GraphBuilder.AllocParameters<FVoxelizeMeshPassParameters>();
		PassParameters->View = Scene->ReferenceView->ViewUniformBuffer;
		PassParameters->PassUniformBuffer = GraphBuilder.CreateUniformBuffer(VLMVoxelizationParams);
		PassParameters->InstanceCulling = FInstanceCullingContext::CreateDummyInstanceCullingUniformBuffer(GraphBuilder);

		GraphBuilder.AddPass(
			RDG_EVENT_NAME("VLM Mesh Voxelization"),
			PassParameters,
			ERDGPassFlags::Raster | ERDGPassFlags::NeverCull,
			[this, CubeMaxDim, PassUniformBuffer, PassParameters](FRHICommandList& RHICmdList)
		{
			RHICmdList.SetViewport(0, 0, 0, CubeMaxDim, CubeMaxDim, 1);

			SCOPED_DRAW_EVENTF(RHICmdList, GPULightmassVoxelizeScene, TEXT("GPULightmass VoxelizeScene"));

			DrawDynamicMeshPass(
				*Scene->ReferenceView,
				RHICmdList,
				[&Scene = Scene, View = Scene->ReferenceView.Get(), ImportanceVolumes = ImportanceVolumes](FDynamicPassMeshDrawListContext* DynamicMeshPassContext)
			{
				FVLMVoxelizationMeshProcessor MeshProcessor(nullptr, View, DynamicMeshPassContext);

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
						MeshBatch.Elements[0].DynamicPrimitiveIndex = InstanceIndex;
						MeshProcessor.AddMeshBatch(MeshBatch, ~0ull, nullptr);
					};
				}

				for (int32 InstanceGroupIndex = 0; InstanceGroupIndex < Scene->InstanceGroupRenderStates.Elements.Num(); InstanceGroupIndex++)
				{
					FInstanceGroupRenderState& InstanceGroup = Scene->InstanceGroupRenderStates.Elements[InstanceGroupIndex];

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
						MeshBatch.Elements[0].DynamicPrimitiveIndex = Scene->StaticMeshInstanceRenderStates.Elements.Num() + InstanceGroupIndex;
						MeshProcessor.AddMeshBatch(MeshBatch, ~0ull, nullptr);
					};
				}

				for (int32 LandscapeIndex = 0; LandscapeIndex < Scene->LandscapeRenderStates.Elements.Num(); LandscapeIndex++)
				{
					FLandscapeRenderState& Landscape = Scene->LandscapeRenderStates.Elements[LandscapeIndex];

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
						MeshBatch.Elements[0].DynamicPrimitiveIndex = Scene->StaticMeshInstanceRenderStates.Elements.Num() + Scene->InstanceGroupRenderStates.Elements.Num() + LandscapeIndex;
						MeshProcessor.AddMeshBatch(MeshBatch, ~0ull, nullptr);
					};
				}
			});
		});

		{
			TShaderMapRef<FDilateVolumeCS> ComputeShader(GlobalShaderMap);

			FDilateVolumeCS::FParameters* Parameters = GraphBuilder.AllocParameters<FDilateVolumeCS::FParameters>();
			Parameters->VolumeSize = VoxelizationVolumeMips[0]->GetDesc().GetSize();
			Parameters->VoxelizeVolume = VoxelizationVolumeMipUAVs[0];

			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("DilateVolume"),
				ComputeShader,
				Parameters,
				FComputeShaderUtils::GetGroupCount(VoxelizationVolumeMips[0]->GetDesc().GetSize(), FIntVector(4)));
		}

		for (int32 MipLevel = 1; MipLevel < VoxelizationVolumeMips.Num(); MipLevel++)
		{
			TShaderMapRef<FDownsampleVolumeCS> ComputeShader(GlobalShaderMap);

			FDownsampleVolumeCS::FParameters* Parameters = GraphBuilder.AllocParameters<FDownsampleVolumeCS::FParameters>();
			Parameters->bIsHighestMip = (MipLevel == VoxelizationVolumeMips.Num() - 1) ? 1 : 0;
			Parameters->VoxelizeVolume = VoxelizationVolumeMipUAVs[MipLevel];
			Parameters->VoxelizeVolumePrevMip = VoxelizationVolumeMipUAVs[MipLevel - 1];

			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("DownsampleVolume"),
				ComputeShader,
				Parameters,
				VoxelizationVolumeMips[MipLevel]->GetDesc().GetSize());
		}

		{
			TResourceArray<int32> InitialBrickAllocatorParams;
			InitialBrickAllocatorParams.Add(0);
			InitialBrickAllocatorParams.Add(0);
			BrickAllocatorParameters.Initialize(TEXT("VolumetricLightmapBrickAllocatorParameters"), 4, 2, PF_R32_SINT, BUF_UnorderedAccess | BUF_SourceCopy, &InitialBrickAllocatorParams);

			RHICmdList.Transition(FRHITransitionInfo(BrickAllocatorParameters.UAV, ERHIAccess::Unknown, ERHIAccess::UAVCompute));
		}

		for (int32 MipLevel = VoxelizationVolumeMips.Num() - 1; MipLevel >= 0; MipLevel--)
		{
			TShaderMapRef<FCountNumBricksCS> ComputeShader(GlobalShaderMap);

			FCountNumBricksCS::FParameters* Parameters = GraphBuilder.AllocParameters<FCountNumBricksCS::FParameters>();
			Parameters->VolumeSize = VoxelizationVolumeMips[MipLevel]->GetDesc().GetSize();
			Parameters->VoxelizeVolume = VoxelizationVolumeMipUAVs[MipLevel];
			Parameters->BrickAllocatorParameters = BrickAllocatorParameters.UAV;

			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("CountNumBricks"),
				ComputeShader,
				Parameters,
				FComputeShaderUtils::GetGroupCount(VoxelizationVolumeMips[MipLevel]->GetDesc().GetSize(), FIntVector(4)));
		}
	}

	{
		FRHIGPUBufferReadback NumBricksReadback(TEXT("NumBricksReadback"));
		NumBricksReadback.EnqueueCopy(RHICmdList, BrickAllocatorParameters.Buffer);
		RHICmdList.BlockUntilGPUIdle();
		check(NumBricksReadback.IsReady());

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
	BrickRequests.Initialize(TEXT("BrickRequests"), 16, NumTotalBricks, PF_R32G32B32A32_UINT, BUF_UnorderedAccess);

	VolumetricLightmapData.BrickDataDimensions = BrickLayoutDimensions * 5;

	{
		FRDGBuilder GraphBuilder(RHICmdList);

		FRDGTextureUAV* IndirectionTextureUAV = GraphBuilder.CreateUAV(GraphBuilder.RegisterExternalTexture(IndirectionTexture));

		TArray<FRDGTextureUAV*, FRDGArrayAllocator> VoxelizationVolumeMipUAVs;

		for (const TRefCountPtr<IPooledRenderTarget>& Mip : VoxelizationVolumeMips)
		{
			VoxelizationVolumeMipUAVs.Emplace(GraphBuilder.CreateUAV(GraphBuilder.RegisterExternalTexture(Mip)));
		}

		for (int32 MipLevel = VoxelizationVolumeMips.Num() - 1; MipLevel >= 0; MipLevel--)
		{
			TShaderMapRef<FGatherBrickRequestsCS> ComputeShader(GlobalShaderMap);

			FGatherBrickRequestsCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FGatherBrickRequestsCS::FParameters>();
			PassParameters->VolumeSize = VoxelizationVolumeMips[MipLevel]->GetDesc().GetSize();
			PassParameters->BrickSize = 1 << (MipLevel * BrickSizeLog2);
			PassParameters->VoxelizeVolume = VoxelizationVolumeMipUAVs[MipLevel];
			PassParameters->BrickAllocatorParameters = BrickAllocatorParameters.UAV;
			PassParameters->BrickRequests = BrickRequests.UAV;

			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("GatherBrickRequests"),
				ComputeShader,
				PassParameters,
				FComputeShaderUtils::GetGroupCount(VoxelizationVolumeMips[MipLevel]->GetDesc().GetSize(), FIntVector(4)));

			RHICmdList.Transition(FRHITransitionInfo(BrickRequests.UAV, ERHIAccess::Unknown, ERHIAccess::UAVCompute));
		}

		for (int32 MipLevel = VoxelizationVolumeMips.Num() - 1; MipLevel >= 0; MipLevel--)
		{
			TShaderMapRef<FSplatVolumeCS> ComputeShader(GlobalShaderMap);

			FSplatVolumeCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FSplatVolumeCS::FParameters>();
			PassParameters->VolumeSize = IndirectionTextureDimensions;
			PassParameters->BrickSize = 1 << (MipLevel * BrickSizeLog2);
			PassParameters->bIsHighestMip = MipLevel == VoxelizationVolumeMips.Num() - 1;
			PassParameters->VoxelizeVolume = VoxelizationVolumeMipUAVs[MipLevel];
			PassParameters->IndirectionTexture = IndirectionTextureUAV;
			PassParameters->BrickAllocatorParameters = BrickAllocatorParameters.UAV;

			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("SplatVolume"),
				ComputeShader,
				PassParameters,
				FComputeShaderUtils::GetGroupCount(IndirectionTextureDimensions, FIntVector(4)));
		}

		GraphBuilder.Execute();
	}

	Scene->DestroyRayTracingScene();
}

void FVolumetricLightmapRenderer::BackgroundTick()
{
	if (NumTotalBricks == 0)
	{
		return;
	}

	int32 NumCellsPerBrick = 5 * 5 * 5;
	if (SamplesTaken >= (uint64)NumTotalBricks * NumCellsPerBrick * NumTotalPassesToRender)
	{
		return;
	}

	TRACE_CPUPROFILER_EVENT_SCOPE(FVolumetricLightmapRenderer::BackgroundTick);

	if (IsRayTracingEnabled())
	{
		if (!Scene->SetupRayTracingScene())
		{
			return;
		}
	}

	FRDGBuilder GraphBuilder(FRHICommandListExecutor::GetImmediateCommandList());
	{
		RDG_EVENT_SCOPE(GraphBuilder, "Volumetric Lightmap Path Tracing");

		FGlobalShaderMap* GlobalShaderMap = GetGlobalShaderMap(Scene->FeatureLevel);

		bool bLastFewFramesIdle = !GCurrentLevelEditingViewportClient || !GCurrentLevelEditingViewportClient->IsRealtime();

		int32 NumSamplesThisFrame = !bLastFewFramesIdle ? 1 : 32;

		// manually handle transitions since the buffers are not (yet) managed by RDG
		GraphBuilder.AddPass(RDG_EVENT_NAME("Transition Buffers"), ERDGPassFlags::None,
			[this](FRHICommandList& RHICmdList) {
			RHICmdList.Transition({
				FRHITransitionInfo(VolumetricLightmapData.BrickData.AmbientVector.UAV, ERHIAccess::Unknown, ERHIAccess::UAVCompute),
				FRHITransitionInfo(VolumetricLightmapData.BrickData.SHCoefficients[0].UAV, ERHIAccess::Unknown, ERHIAccess::UAVCompute),
				FRHITransitionInfo(VolumetricLightmapData.BrickData.SHCoefficients[1].UAV, ERHIAccess::Unknown, ERHIAccess::UAVCompute),
				FRHITransitionInfo(VolumetricLightmapData.BrickData.SHCoefficients[2].UAV, ERHIAccess::Unknown, ERHIAccess::UAVCompute),
				FRHITransitionInfo(VolumetricLightmapData.BrickData.SHCoefficients[3].UAV, ERHIAccess::Unknown, ERHIAccess::UAVCompute),
				FRHITransitionInfo(VolumetricLightmapData.BrickData.SHCoefficients[4].UAV, ERHIAccess::Unknown, ERHIAccess::UAVCompute),
				FRHITransitionInfo(VolumetricLightmapData.BrickData.SHCoefficients[5].UAV, ERHIAccess::Unknown, ERHIAccess::UAVCompute),
				FRHITransitionInfo(VolumetricLightmapData.BrickData.SkyBentNormal.UAV, ERHIAccess::Unknown, ERHIAccess::UAVCompute),
				FRHITransitionInfo(VolumetricLightmapData.BrickData.DirectionalLightShadowing.UAV, ERHIAccess::Unknown, ERHIAccess::UAVCompute),
				}
			);
		}
		);

		FVolumetricLightmapPathTracingRGS::FParameters* PreviousPassParameters = nullptr;
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
				{
					FVolumetricLightmapPathTracingRGS::FPermutationDomain PermutationVector;
					PermutationVector.Set<FVolumetricLightmapPathTracingRGS::FUseIrradianceCaching>(Scene->Settings->bUseIrradianceCaching);
					TShaderRef<FVolumetricLightmapPathTracingRGS> RayGenShader = GlobalShaderMap->GetShader<FVolumetricLightmapPathTracingRGS>(PermutationVector);

					FVolumetricLightmapPathTracingRGS::FParameters* PassParameters = GraphBuilder.AllocParameters<FVolumetricLightmapPathTracingRGS::FParameters>();
					CA_ASSUME(PassParameters);

					PassParameters->FrameNumber = FrameNumber / NumFramesOneRound;
					PassParameters->VolumeMin = (FVector3f)VolumeMin; // LWC_TODO: precision loss
					PassParameters->VolumeSize = (FVector3f)VolumeSize; // LWC_TODO: precision loss
					PassParameters->IndirectionTextureDim = IndirectionTextureDimensions;
					PassParameters->TLAS = Scene->RayTracingSceneSRV;
					PassParameters->BrickRequests = BrickRequests.SRV;
					PassParameters->NumTotalBricks = NumTotalBricks;
					PassParameters->BrickBatchOffset = BrickBatchOffset;
					PassParameters->VolumetricLightmapQualityMultiplier = Scene->Settings->VolumetricLightmapQualityMultiplier;
					PassParameters->AmbientVector = AccumulationBrickData.AmbientVector.UAV;
					PassParameters->SHCoefficients0R = AccumulationBrickData.SHCoefficients[0].UAV;
					PassParameters->SHCoefficients1R = AccumulationBrickData.SHCoefficients[1].UAV;
					PassParameters->SHCoefficients0G = AccumulationBrickData.SHCoefficients[2].UAV;
					PassParameters->SHCoefficients1G = AccumulationBrickData.SHCoefficients[3].UAV;
					PassParameters->SHCoefficients0B = AccumulationBrickData.SHCoefficients[4].UAV;
					PassParameters->SHCoefficients1B = AccumulationBrickData.SHCoefficients[5].UAV;
					PassParameters->SkyBentNormal = AccumulationBrickData.SkyBentNormal.UAV;
					PassParameters->DirectionalLightShadowing = AccumulationBrickData.DirectionalLightShadowing.UAV;
					PassParameters->ViewUniformBuffer = Scene->ReferenceView->ViewUniformBuffer;
					PassParameters->IrradianceCachingParameters = Scene->IrradianceCache->IrradianceCachingParametersUniformBuffer;

					if (PreviousPassParameters == nullptr)
					{
						SetupPathTracingLightParameters(Scene->LightSceneRenderState, GraphBuilder, *Scene->ReferenceView, PassParameters);
						PreviousPassParameters = PassParameters;
					}
					else
					{
						PassParameters->LightGridParameters = PreviousPassParameters->LightGridParameters;
						PassParameters->SceneLightCount = PreviousPassParameters->SceneLightCount;
						PassParameters->SceneVisibleLightCount = PreviousPassParameters->SceneVisibleLightCount;
						PassParameters->SceneLights = PreviousPassParameters->SceneLights;
						PassParameters->SkylightTexture = PreviousPassParameters->SkylightTexture;
						PassParameters->SkylightTextureSampler = PreviousPassParameters->SkylightTextureSampler;
						PassParameters->SkylightPdf = PreviousPassParameters->SkylightPdf;
						PassParameters->SkylightInvResolution = PreviousPassParameters->SkylightInvResolution;
						PassParameters->SkylightMipCount = PreviousPassParameters->SkylightMipCount;
						PassParameters->IESTexture = PreviousPassParameters->IESTexture;
						PassParameters->IESTextureSampler = PreviousPassParameters->IESTextureSampler;
					}

					PassParameters->SSProfilesTexture = GetSubsurfaceProfileTexture();

					TArray<FLightShaderConstants> OptionalStationaryDirectionalLightShadowing;
					for (FDirectionalLightRenderState& DirectionalLight : Scene->LightSceneRenderState.DirectionalLights.Elements)
					{
						if (DirectionalLight.bStationary)
						{
							OptionalStationaryDirectionalLightShadowing.Add(DirectionalLight.GetLightShaderParameters());
							break;
						}
					}
					if (OptionalStationaryDirectionalLightShadowing.Num() == 0)
					{
						OptionalStationaryDirectionalLightShadowing.AddZeroed();
					}
					PassParameters->LightShaderParametersArray = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(CreateStructuredBuffer(GraphBuilder, TEXT("OptionalStationaryDirectionalLightShadowing"), sizeof(FLightShaderConstants),
						OptionalStationaryDirectionalLightShadowing.Num(), 
						OptionalStationaryDirectionalLightShadowing.GetData(),
						sizeof(FLightShaderConstants) * OptionalStationaryDirectionalLightShadowing.Num()
					)));

					FSceneRenderState* SceneRenderState = Scene; // capture member variable
					GraphBuilder.AddPass(
						RDG_EVENT_NAME("VolumetricLightmapPathTracing %d bricks %d rays", BricksToCalcThisFrame, BricksToCalcThisFrame * (BrickSize + 1) * (BrickSize + 1) * (BrickSize + 1)),
						PassParameters,
						ERDGPassFlags::Compute,
						[PassParameters, RayGenShader, SceneRenderState, BricksToCalcThisFrame](FRHIRayTracingCommandList& RHICmdList)
					{
						FRayTracingShaderBindingsWriter GlobalResources;
						SetShaderParameters(GlobalResources, RayGenShader, *PassParameters);

						FRHIRayTracingScene* RayTracingSceneRHI = SceneRenderState->RayTracingScene;
						RHICmdList.RayTraceDispatch(SceneRenderState->RayTracingPipelineState, RayGenShader.GetRayTracingShader(), RayTracingSceneRHI, GlobalResources, BricksToCalcThisFrame * (BrickSize + 1) * (BrickSize + 1) * (BrickSize + 1), 1);
					}
					);


				}
			}
#endif
			{
				// manually handle transitions since the buffers are not (yet) managed by RDG
				GraphBuilder.AddPass(RDG_EVENT_NAME("Transition Buffers"), ERDGPassFlags::None,
					[this](FRHICommandList& RHICmdList) {
					RHICmdList.Transition({
						FRHITransitionInfo(AccumulationBrickData.AmbientVector.UAV, ERHIAccess::UAVCompute, ERHIAccess::UAVCompute),
						FRHITransitionInfo(AccumulationBrickData.SHCoefficients[0].UAV, ERHIAccess::UAVCompute, ERHIAccess::UAVCompute),
						FRHITransitionInfo(AccumulationBrickData.SHCoefficients[1].UAV, ERHIAccess::UAVCompute, ERHIAccess::UAVCompute),
						FRHITransitionInfo(AccumulationBrickData.SHCoefficients[2].UAV, ERHIAccess::UAVCompute, ERHIAccess::UAVCompute),
						FRHITransitionInfo(AccumulationBrickData.SHCoefficients[3].UAV, ERHIAccess::UAVCompute, ERHIAccess::UAVCompute),
						FRHITransitionInfo(AccumulationBrickData.SHCoefficients[4].UAV, ERHIAccess::UAVCompute, ERHIAccess::UAVCompute),
						FRHITransitionInfo(AccumulationBrickData.SHCoefficients[5].UAV, ERHIAccess::UAVCompute, ERHIAccess::UAVCompute),
						FRHITransitionInfo(AccumulationBrickData.SkyBentNormal.UAV, ERHIAccess::UAVCompute, ERHIAccess::UAVCompute),
						FRHITransitionInfo(AccumulationBrickData.DirectionalLightShadowing.UAV, ERHIAccess::UAVCompute, ERHIAccess::UAVCompute),
						}
					);
				}
				);

				TShaderMapRef<FFinalizeBrickResultsCS> ComputeShader(GlobalShaderMap);

				FFinalizeBrickResultsCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FFinalizeBrickResultsCS::FParameters>();
				PassParameters->NumTotalBricks = NumTotalBricks;
				PassParameters->BrickBatchOffset = BrickBatchOffset;
				PassParameters->BrickRequests = BrickRequests.UAV;
				PassParameters->AmbientVector = AccumulationBrickData.AmbientVector.Texture;
				PassParameters->SHCoefficients0R = AccumulationBrickData.SHCoefficients[0].Texture;
				PassParameters->SHCoefficients1R = AccumulationBrickData.SHCoefficients[1].Texture;
				PassParameters->SHCoefficients0G = AccumulationBrickData.SHCoefficients[2].Texture;
				PassParameters->SHCoefficients1G = AccumulationBrickData.SHCoefficients[3].Texture;
				PassParameters->SHCoefficients0B = AccumulationBrickData.SHCoefficients[4].Texture;
				PassParameters->SHCoefficients1B = AccumulationBrickData.SHCoefficients[5].Texture;
				PassParameters->SkyBentNormal = AccumulationBrickData.SkyBentNormal.Texture;
				PassParameters->DirectionalLightShadowing = AccumulationBrickData.DirectionalLightShadowing.Texture;
				PassParameters->OutAmbientVector = VolumetricLightmapData.BrickData.AmbientVector.UAV;
				PassParameters->OutSHCoefficients0R = VolumetricLightmapData.BrickData.SHCoefficients[0].UAV;
				PassParameters->OutSHCoefficients1R = VolumetricLightmapData.BrickData.SHCoefficients[1].UAV;
				PassParameters->OutSHCoefficients0G = VolumetricLightmapData.BrickData.SHCoefficients[2].UAV;
				PassParameters->OutSHCoefficients1G = VolumetricLightmapData.BrickData.SHCoefficients[3].UAV;
				PassParameters->OutSHCoefficients0B = VolumetricLightmapData.BrickData.SHCoefficients[4].UAV;
				PassParameters->OutSHCoefficients1B = VolumetricLightmapData.BrickData.SHCoefficients[5].UAV;
				PassParameters->OutSkyBentNormal = VolumetricLightmapData.BrickData.SkyBentNormal.UAV;
				PassParameters->OutDirectionalLightShadowing = VolumetricLightmapData.BrickData.DirectionalLightShadowing.UAV;

				FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("FinalizeBrickResults"), ComputeShader, PassParameters, FIntVector(BricksToCalcThisFrame, 1, 1));

				// manually handle transitions since the buffers are not (yet) managed by RDG
				GraphBuilder.AddPass(RDG_EVENT_NAME("Transition Buffers"), ERDGPassFlags::None,
					[this](FRHICommandList& RHICmdList) {
					RHICmdList.Transition({
						FRHITransitionInfo(AccumulationBrickData.AmbientVector.UAV, ERHIAccess::UAVCompute, ERHIAccess::UAVCompute),
						FRHITransitionInfo(AccumulationBrickData.SHCoefficients[0].UAV, ERHIAccess::UAVCompute, ERHIAccess::UAVCompute),
						FRHITransitionInfo(AccumulationBrickData.SHCoefficients[1].UAV, ERHIAccess::UAVCompute, ERHIAccess::UAVCompute),
						FRHITransitionInfo(AccumulationBrickData.SHCoefficients[2].UAV, ERHIAccess::UAVCompute, ERHIAccess::UAVCompute),
						FRHITransitionInfo(AccumulationBrickData.SHCoefficients[3].UAV, ERHIAccess::UAVCompute, ERHIAccess::UAVCompute),
						FRHITransitionInfo(AccumulationBrickData.SHCoefficients[4].UAV, ERHIAccess::UAVCompute, ERHIAccess::UAVCompute),
						FRHITransitionInfo(AccumulationBrickData.SHCoefficients[5].UAV, ERHIAccess::UAVCompute, ERHIAccess::UAVCompute),
						FRHITransitionInfo(AccumulationBrickData.SkyBentNormal.UAV, ERHIAccess::UAVCompute, ERHIAccess::UAVCompute),
						FRHITransitionInfo(AccumulationBrickData.DirectionalLightShadowing.UAV, ERHIAccess::UAVCompute, ERHIAccess::UAVCompute),
						}
					);
				}
				);
			}

			{
				FRDGTextureUAV* IndirectionTextureUAV = GraphBuilder.CreateUAV(GraphBuilder.RegisterExternalTexture(IndirectionTexture));

				// Doing 2 passes no longer makes sense in an amortized setup
				// for (int32 StitchPass = 0; StitchPass < 2; StitchPass++)
				{
					TShaderMapRef<FStitchBorderCS> ComputeShader(GlobalShaderMap);

					FStitchBorderCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FStitchBorderCS::FParameters>();
					PassParameters->BrickDataDimensions = VolumetricLightmapData.BrickDataDimensions;
					PassParameters->IndirectionTextureDim = IndirectionTextureDimensions;
					PassParameters->FrameNumber = FrameNumber / NumFramesOneRound;
					PassParameters->NumTotalBricks = NumTotalBricks;
					PassParameters->BrickBatchOffset = BrickBatchOffset;
					PassParameters->IndirectionTexture = IndirectionTextureUAV;
					PassParameters->BrickRequests = BrickRequests.UAV;
					PassParameters->AmbientVector = AccumulationBrickData.AmbientVector.Texture;
					PassParameters->OutAmbientVector = VolumetricLightmapData.BrickData.AmbientVector.UAV;
					PassParameters->OutSHCoefficients0R = VolumetricLightmapData.BrickData.SHCoefficients[0].UAV;
					PassParameters->OutSHCoefficients1R = VolumetricLightmapData.BrickData.SHCoefficients[1].UAV;
					PassParameters->OutSHCoefficients0G = VolumetricLightmapData.BrickData.SHCoefficients[2].UAV;
					PassParameters->OutSHCoefficients1G = VolumetricLightmapData.BrickData.SHCoefficients[3].UAV;
					PassParameters->OutSHCoefficients0B = VolumetricLightmapData.BrickData.SHCoefficients[4].UAV;
					PassParameters->OutSHCoefficients1B = VolumetricLightmapData.BrickData.SHCoefficients[5].UAV;
					PassParameters->OutSkyBentNormal = VolumetricLightmapData.BrickData.SkyBentNormal.UAV;
					PassParameters->OutDirectionalLightShadowing = VolumetricLightmapData.BrickData.DirectionalLightShadowing.UAV;

					FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("VolumetricLightmapStitching %d bricks", BricksToCalcThisFrame), ComputeShader, PassParameters, FIntVector(BricksToCalcThisFrame, 1, 1));

					// manually handle transitions since the buffers are not (yet) managed by RDG
					GraphBuilder.AddPass(RDG_EVENT_NAME("Transition Buffers"), ERDGPassFlags::None,
						[this](FRHICommandList& RHICmdList) {
						RHICmdList.Transition({
							FRHITransitionInfo(AccumulationBrickData.AmbientVector.UAV, ERHIAccess::UAVCompute, ERHIAccess::UAVCompute),
							FRHITransitionInfo(AccumulationBrickData.SHCoefficients[0].UAV, ERHIAccess::UAVCompute, ERHIAccess::UAVCompute),
							FRHITransitionInfo(AccumulationBrickData.SHCoefficients[1].UAV, ERHIAccess::UAVCompute, ERHIAccess::UAVCompute),
							FRHITransitionInfo(AccumulationBrickData.SHCoefficients[2].UAV, ERHIAccess::UAVCompute, ERHIAccess::UAVCompute),
							FRHITransitionInfo(AccumulationBrickData.SHCoefficients[3].UAV, ERHIAccess::UAVCompute, ERHIAccess::UAVCompute),
							FRHITransitionInfo(AccumulationBrickData.SHCoefficients[4].UAV, ERHIAccess::UAVCompute, ERHIAccess::UAVCompute),
							FRHITransitionInfo(AccumulationBrickData.SHCoefficients[5].UAV, ERHIAccess::UAVCompute, ERHIAccess::UAVCompute),
							FRHITransitionInfo(AccumulationBrickData.SkyBentNormal.UAV, ERHIAccess::UAVCompute, ERHIAccess::UAVCompute),
							FRHITransitionInfo(AccumulationBrickData.DirectionalLightShadowing.UAV, ERHIAccess::UAVCompute, ERHIAccess::UAVCompute),
							}
						);
					}
					);
				}
			}

			FrameNumber++;

			SamplesTaken += BricksToCalcThisFrame * NumCellsPerBrick;

			if (SamplesTaken >= (uint64)NumTotalBricks * NumCellsPerBrick * NumTotalPassesToRender)
			{
				break;
			}
		}
	}
	GraphBuilder.Execute();

	if (IsRayTracingEnabled())
	{
		Scene->DestroyRayTracingScene();
	}
}

}
