// Copyright Epic Games, Inc. All Rights Reserved.

#include "WorldPartition/WorldPartitionMiniMapBuilder.h"

#include "CoreMinimal.h"
#include "Logging/LogMacros.h"
#include "Misc/CommandLine.h"

#include "AssetCompilingManager.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "Factories/TextureFactory.h"
#include "SourceControlHelpers.h"
#include "UObject/SavePackage.h"
#include "UObject/StrongObjectPtr.h"

#include "ISourceControlModule.h"
#include "ISourceControlProvider.h"
#include "SourceControlOperations.h"

#include "WorldPartition/ActorDescContainer.h"
#include "WorldPartition/DataLayer/ActorDataLayer.h"
#include "WorldPartition/DataLayer/DataLayer.h"
#include "WorldPartition/DataLayer/WorldDataLayers.h"
#include "WorldPartition/HLOD/HLODActor.h"
#include "WorldPartition/HLOD/HLODActorDesc.h"
#include "WorldPartition/WorldPartition.h"
#include "WorldPartition/WorldPartitionHandle.h"
#include "WorldPartition/WorldPartitionMiniMap.h"
#include "WorldPartition/WorldPartitionMiniMapHelper.h"

DEFINE_LOG_CATEGORY_STATIC(LogWorldPartitionMiniMapBuilder, All, All);

UWorldPartitionMiniMapBuilder::UWorldPartitionMiniMapBuilder(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

bool UWorldPartitionMiniMapBuilder::PreRun(UWorld* World, FPackageSourceControlHelper& PackageHelper)
{
	bAutoSubmit = FParse::Param(FCommandLine::Get(), TEXT("AutoSubmit"));
	
	if (WorldMiniMap == nullptr)
	{
		WorldMiniMap = FWorldPartitionMiniMapHelper::GetWorldPartitionMiniMap(World, true);
	}

	if (!WorldMiniMap)
	{
		UE_LOG(LogWorldPartitionMiniMapBuilder, Error, TEXT("Failed to create Minimap. WorldPartitionMiniMap actor not found in the persistent level."));
		return false;
	}

	// Reset minimap resources
	WorldMiniMap->MiniMapTexture = nullptr;
	MiniMapTiles.Empty();

	AWorldDataLayers* WorldDataLayers = World->GetWorldDataLayers();

	if (WorldDataLayers == nullptr)
	{
		UE_LOG(LogWorldPartitionMiniMapBuilder, Error, TEXT("Failed to retrieve WorldDataLayers."));
		return false;
	}

	for (const FActorDataLayer& ActorDataLayer : WorldMiniMap->ExcludedDataLayers)
	{
		const UDataLayer* DataLayer = WorldDataLayers->GetDataLayerFromName(ActorDataLayer.Name);

		if (DataLayer != nullptr)
		{
			ExcludedDataLayerLabels.Add(DataLayer->GetDataLayerLabel());
		}
	}

	return true;
}

bool UWorldPartitionMiniMapBuilder::RunInternal(UWorld* World, const FCellInfo& InCellInfo, FPackageSourceControlHelper& PackageHelper)
{
	EditorBounds = InCellInfo.EditorBounds;
	IterativeCellSize = InCellInfo.IterativeCellSize;

	FString TextureName = FString::Format(TEXT("MinimapTile_{0}_{1}_{2}"), { InCellInfo.Location.X, InCellInfo.Location.Y, InCellInfo.Location.Z });

	UTexture2D* TileTexture = nullptr;
	FWorldPartitionMiniMapHelper::CaptureBoundsMiniMapToTexture(World, WorldMiniMap, WorldMiniMap->MiniMapTileSize, TileTexture, TextureName, InCellInfo.Bounds);

	FMinimapTile MinimapTile;
	MinimapTile.Texture = TStrongObjectPtr<UTexture2D>(TileTexture);
	MinimapTile.Coordinates.X = InCellInfo.Location.X;
	MinimapTile.Coordinates.Y = InCellInfo.Location.Y;
	MiniMapTiles.Add(MoveTemp(MinimapTile));

	return true;
}

bool UWorldPartitionMiniMapBuilder::PostRun(UWorld* World, FPackageSourceControlHelper& PackageHelper, const bool bInRunSuccess)
{
	if (!bInRunSuccess)
	{
		return false;
	}

	// Make sure all assets and textures are ready
	FAssetCompilingManager::Get().FinishAllCompilation();

	WorldMiniMap->MiniMapWorldBounds = EditorBounds;

	if (MiniMapTiles.IsEmpty())
	{
		UE_LOG(LogWorldPartitionMiniMapBuilder, Error, TEXT("No tiles were rendered, cannot compose Virtual Texture."));
		return false;
	}

	FIntVector2 MinTileCoords(INT32_MAX);
	FIntVector2 MaxTileCoords(-INT32_MAX);
	for (const FMinimapTile& Tile : MiniMapTiles)
	{
		MinTileCoords = FIntVector2(FMath::Min(MinTileCoords.X, Tile.Coordinates.X), FMath::Min(MinTileCoords.Y, Tile.Coordinates.Y));
		MaxTileCoords = FIntVector2(FMath::Max(MaxTileCoords.X, Tile.Coordinates.X), FMath::Max(MaxTileCoords.Y, Tile.Coordinates.Y));
	}

	const FIntVector2 NumTilesCoords = FIntVector2(MaxTileCoords.X - MinTileCoords.X + 1, MaxTileCoords.Y - MinTileCoords.Y + 1);
	const int32 MinimapTileSize = WorldMiniMap->MiniMapTileSize;
	const int32 MinimapImageSizeX = NumTilesCoords.X * MinimapTileSize;
	const int32 MinimapImageSizeY = NumTilesCoords.Y * MinimapTileSize;
	
	// Copy all tiles to the final minimap texture
	{
		TStrongObjectPtr<UTextureFactory> Factory(NewObject<UTextureFactory>());
		UTexture2D* MiniMapTexture = Factory->CreateTexture2D(WorldMiniMap, TEXT("MinimapTexture"), RF_NoFlags);
		MiniMapTexture->Source.Init(MinimapImageSizeX, MinimapImageSizeY, 1, 1, TSF_BGRA8);

	    const uint32 DstDataStride = MiniMapTexture->Source.GetSizeX() * MiniMapTexture->Source.GetBytesPerPixel();
	    uint8* DstDataBasePtr = MiniMapTexture->Source.LockMip(0);
    
	    for (const FMinimapTile& Tile : MiniMapTiles)
	    {
		    const uint32 SrcDataStride = MinimapTileSize * Tile.Texture->Source.GetBytesPerPixel();
		    const uint8* SrcDataPtr = Tile.Texture->Source.LockMipReadOnly(0);
    
		    const int32 TileOffsetX = Tile.Coordinates.X - MinTileCoords.X;
		    const int32 TileOffsetY = Tile.Coordinates.Y - MinTileCoords.Y;
    
		    uint8* DstDataPtr = DstDataBasePtr + (TileOffsetY * MinimapTileSize * DstDataStride) + (TileOffsetX * SrcDataStride);
    
		    for (int RowIdx = 0; RowIdx < MinimapTileSize; ++RowIdx)
		    {
			    FMemory::Memcpy(DstDataPtr + DstDataStride * RowIdx,
							    SrcDataPtr + SrcDataStride * RowIdx,
							    SrcDataStride);
		    }
    
		    Tile.Texture->Source.UnlockMip(0);
	    }

		MiniMapTexture->Source.UnlockMip(0);
		MiniMapTexture->PowerOfTwoMode = ETexturePowerOfTwoSetting::PadToPowerOfTwo;	// Required for VTs
		MiniMapTexture->AdjustMinAlpha = 1.f;
		MiniMapTexture->LODGroup = TEXTUREGROUP_UI;
		MiniMapTexture->VirtualTextureStreaming = true;
		MiniMapTexture->UpdateResource();

		WorldMiniMap->MiniMapTexture = MiniMapTexture;
	}

	// Compute relevant UV space for the minimap
	{
		FBox2D MinimapWorldBounds = FBox2D({ EditorBounds.Min.X, EditorBounds.Min.Y }, { EditorBounds.Max.X, EditorBounds.Max.Y });
		FBox2D ImageWorldBounds = FBox2D(FVector2D(MinTileCoords.X, MinTileCoords.Y) * IterativeCellSize, FVector2D(MaxTileCoords.X + 1, MaxTileCoords.Y + 1) * IterativeCellSize);

		FVector2D TexturePOW2ScaleFactor = FVector2D((float)MinimapImageSizeX / FMath::RoundUpToPowerOfTwo(MinimapImageSizeX),
			(float)MinimapImageSizeY / FMath::RoundUpToPowerOfTwo(MinimapImageSizeY));

		FBox2D UVOffset(FVector2D((MinimapWorldBounds.Min - ImageWorldBounds.Min) / ImageWorldBounds.GetSize()),
			FVector2D((MinimapWorldBounds.Max - ImageWorldBounds.Min) / ImageWorldBounds.GetSize()));

		UVOffset.Min *= TexturePOW2ScaleFactor;
		UVOffset.Max *= TexturePOW2ScaleFactor;

		WorldMiniMap->UVOffset = UVOffset;
	}

	// Make sure the minimap texture is ready before saving
	FAssetCompilingManager::Get().FinishAllCompilation();

	// Save MiniMap Package
	auto WorldMiniMapExternalPackage = WorldMiniMap->GetExternalPackage();
	FString PackageFileName = SourceControlHelpers::PackageFilename(WorldMiniMapExternalPackage);

	if (!PackageHelper.Checkout(WorldMiniMapExternalPackage))
	{
		UE_LOG(LogWorldPartitionMiniMapBuilder, Error, TEXT("Error checking out package %s."), *WorldMiniMapExternalPackage->GetName());
		return false;
	}

	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Standalone;
	SaveArgs.SaveFlags = SAVE_Async;
	if (!UPackage::SavePackage(WorldMiniMapExternalPackage, nullptr, *PackageFileName, SaveArgs))
	{
		UE_LOG(LogWorldPartitionMiniMapBuilder, Error, TEXT("Error saving package %s."), *WorldMiniMapExternalPackage->GetName());
		return false;
	}

	if (!PackageHelper.AddToSourceControl(WorldMiniMapExternalPackage))
	{
		UE_LOG(LogWorldPartitionMiniMapBuilder, Error, TEXT("Error adding package %s to source control."), *WorldMiniMapExternalPackage->GetName());
		return false;
	}

	UPackage::WaitForAsyncFileWrites();

	if (bAutoSubmit)
	{
		FText ChangelistDescription = FText::FromString(FString::Printf(TEXT("Rebuilt minimap for \"%s\" at %s"), *World->GetName(), *FEngineVersion::Current().ToString()));

		TSharedRef<FCheckIn, ESPMode::ThreadSafe> CheckInOperation = ISourceControlOperation::Create<FCheckIn>();
		CheckInOperation->SetDescription(ChangelistDescription);
		if (ISourceControlModule::Get().GetProvider().Execute(CheckInOperation, PackageFileName) != ECommandResult::Succeeded)
		{
			UE_LOG(LogWorldPartitionMiniMapBuilder, Error, TEXT("Failed to submit minimap (%s) to source control."), *PackageFileName);
			return false;
		}
		else
		{
			UE_LOG(LogWorldPartitionMiniMapBuilder, Display, TEXT("#### Submitted minimap (%s) to source control ####"), *PackageFileName);
		}
	}

	return true;
}