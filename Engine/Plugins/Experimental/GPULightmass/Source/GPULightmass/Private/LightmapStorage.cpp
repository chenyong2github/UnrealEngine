// Copyright Epic Games, Inc. All Rights Reserved.

#include "LightmapStorage.h"
#include "VT/VirtualTexture.h"
#include "GPULightmassCommon.h"
#include "EngineModule.h"
#include "Async/Async.h"
#include "Scene/Lights.h"
#include "LightmapRenderer.h"

namespace GPULightmass
{

FLightmap::FLightmap(FString InName, FIntPoint InSize)
	: Name(InName)
	, Size(InSize)
{
	check(IsInGameThread());
}

void FLightmap::CreateGameThreadResources()
{
	TextureUObject = NewObject<ULightMapVirtualTexture2D>(GetTransientPackage(), FName(*Name));
	TextureUObject->VirtualTextureStreaming = true;
	TextureUObject->bPreviewLightmap = true;
	
	// TODO: add more layers based on layer settings
	{
		TextureUObject->SetLayerForType(ELightMapVirtualTextureType::HqLayer0, 0);
		TextureUObject->SetLayerForType(ELightMapVirtualTextureType::HqLayer1, 1);
		TextureUObject->SetLayerForType(ELightMapVirtualTextureType::ShadowMask, 2);
	}

	TextureUObjectGuard = MakeUnique<FGCObjectScopeGuard>(TextureUObject);

	LightmapObject = new FLightMap2D();
	{
		LightmapObject->CoordinateScale.X = (float)(Size.X - 2) / (GetPaddedSizeInTiles().X * GPreviewLightmapVirtualTileSize);
		LightmapObject->CoordinateScale.Y = (float)(Size.Y - 2) / (GetPaddedSizeInTiles().Y * GPreviewLightmapVirtualTileSize);
		LightmapObject->CoordinateBias.X = 1.0f / (GetPaddedSizeInTiles().X * GPreviewLightmapVirtualTileSize);
		LightmapObject->CoordinateBias.Y = 1.0f / (GetPaddedSizeInTiles().Y * GPreviewLightmapVirtualTileSize);

		for (int32 CoefIndex = 0; CoefIndex < NUM_STORED_LIGHTMAP_COEF; CoefIndex++)
		{
			LightmapObject->ScaleVectors[CoefIndex] = FVector4(1.0f, 1.0f, 1.0f, 1.0f);
			LightmapObject->AddVectors[CoefIndex] = FVector4(0.0f, 0.0f, 0.0f, 0.0f);
		}
	}
	LightmapObject->VirtualTexture = TextureUObject;

	ResourceCluster = MakeUnique<FLightmapResourceCluster>();
	ResourceCluster->Input.LightMapVirtualTexture = TextureUObject;

	MeshMapBuildData = MakeUnique<FMeshMapBuildData>();
	MeshMapBuildData->LightMap = LightmapObject;
	MeshMapBuildData->ResourceCluster = ResourceCluster.Get();
}

FLightmapRenderState::FLightmapRenderState(Initializer InInitializer, FGeometryInstanceRenderStateRef GeometryInstanceRef)
	: Name(InInitializer.Name)
	, ResourceCluster(InInitializer.ResourceCluster)
	, LightmapCoordinateScaleBias(InInitializer.LightmapCoordinateScaleBias)
	, GeometryInstanceRef(GeometryInstanceRef)
	, Size(InInitializer.Size)
	, MaxLevel(InInitializer.MaxLevel)
{
	for (int32 MipLevel = 0; MipLevel <= MaxLevel; MipLevel++)
	{
		TileStates.AddDefaulted(GetPaddedSizeInTilesAtMipLevel(MipLevel).X * GetPaddedSizeInTilesAtMipLevel(MipLevel).Y);
	}

	for (int32 MipLevel = 0; MipLevel <= MaxLevel; MipLevel++)
	{
		TileRelevantLightSampleCountStates.AddDefaulted(GetPaddedSizeInTilesAtMipLevel(MipLevel).X * GetPaddedSizeInTilesAtMipLevel(MipLevel).Y);
	}

	{
		// Store converged tiles for re-uploading to GPU / encoding & saving to disk
		// Store physical tiles for easier GPU upload, which however requires further physical -> virtual conversion when saving to disk
		CPUTextureData[0].AddDefaulted(MaxLevel + 1);
		CPUTextureData[1].AddDefaulted(MaxLevel + 1);
		CPUTextureData[2].AddDefaulted(MaxLevel + 1);
		for (int32 MipLevel = 0; MipLevel <= MaxLevel; MipLevel++)
		{
			CPUTextureData[0][MipLevel].AddUninitialized(GetPaddedSize().X * GetPaddedSize().Y);
			CPUTextureData[1][MipLevel].AddUninitialized(GetPaddedSize().X * GetPaddedSize().Y);
			CPUTextureData[2][MipLevel].AddUninitialized(GetPaddedSize().X * GetPaddedSize().Y);
		}

		CPUTextureRawData[0].AddDefaulted(MaxLevel + 1);
		CPUTextureRawData[1].AddDefaulted(MaxLevel + 1);
		CPUTextureRawData[2].AddDefaulted(MaxLevel + 1);
		for (int32 MipLevel = 0; MipLevel <= MaxLevel; MipLevel++)
		{
			CPUTextureRawData[0][MipLevel].AddUninitialized(GetPaddedSize().X * GetPaddedSize().Y);
			CPUTextureRawData[1][MipLevel].AddUninitialized(GetPaddedSize().X * GetPaddedSize().Y);
			CPUTextureRawData[2][MipLevel].AddUninitialized(GetPaddedSize().X * GetPaddedSize().Y);
		}
	}

	{
		FPrecomputedLightingUniformParameters Parameters;
		
		{
			Parameters.StaticShadowMapMasks = FVector4(1, 1, 1, 1);
			Parameters.InvUniformPenumbraSizes = FVector4(0, 0, 0, 0);
			Parameters.ShadowMapCoordinateScaleBias = FVector4(1, 1, 0, 0);

			const uint32 NumCoef = FMath::Max<uint32>(NUM_HQ_LIGHTMAP_COEF, NUM_LQ_LIGHTMAP_COEF);
			for (uint32 CoefIndex = 0; CoefIndex < NumCoef; ++CoefIndex)
			{
				Parameters.LightMapScale[CoefIndex] = FVector4(1, 1, 1, 1);
				Parameters.LightMapAdd[CoefIndex] = FVector4(0, 0, 0, 0);
			}

			FMemory::Memzero(Parameters.LightmapVTPackedPageTableUniform);

			for (uint32 LayerIndex = 0u; LayerIndex < 5u; ++LayerIndex)
			{
				Parameters.LightmapVTPackedUniform[LayerIndex] = FUintVector4(ForceInitToZero);
			}
		}

		Parameters.LightMapCoordinateScaleBias = LightmapCoordinateScaleBias;

		SetPrecomputedLightingBuffer(TUniformBufferRef<FPrecomputedLightingUniformParameters>::CreateUniformBufferImmediate(Parameters, UniformBuffer_MultiFrame));
	}
}

bool FLightmapRenderState::IsTileGIConverged(FTileVirtualCoordinates Coords, int32 NumGISamples)
{
	return RetrieveTileState(Coords).RenderPassIndex >= NumGISamples;
}

bool FLightmapRenderState::IsTileShadowConverged(FTileVirtualCoordinates Coords, int32 NumShadowSamples)
{
	bool bConverged = true;
	for (auto& Pair : RetrieveTileRelevantLightSampleState(Coords).RelevantDirectionalLightSampleCount)
	{
		bConverged &= Pair.Value >= NumShadowSamples;
	}
	for (auto& Pair : RetrieveTileRelevantLightSampleState(Coords).RelevantPointLightSampleCount)
	{
		bConverged &= Pair.Value >= NumShadowSamples;
	}
	for (auto& Pair : RetrieveTileRelevantLightSampleState(Coords).RelevantSpotLightSampleCount)
	{
		bConverged &= Pair.Value >= NumShadowSamples;
	}
	for (auto& Pair : RetrieveTileRelevantLightSampleState(Coords).RelevantRectLightSampleCount)
	{
		bConverged &= Pair.Value >= NumShadowSamples;
	}
	return bConverged;
}
bool FLightmapRenderState::DoesTileHaveValidCPUData(FTileVirtualCoordinates Coords, int32 CurrentRevision)
{
	return RetrieveTileState(Coords).CPURevision == CurrentRevision;
}

}
