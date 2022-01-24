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
	FMinimapTile MinimapTile;
	
	MinimapTile.Coordinates.X = InCellInfo.Location.X;
	MinimapTile.Coordinates.Y = InCellInfo.Location.Y;

	EditorBounds = InCellInfo.EditorBounds;
	IterativeCellSize = InCellInfo.IterativeCellSize;

	FString TextureName = FString::Format(TEXT("MinimapTile_{0}_{1}_{2}"), { InCellInfo.Location.X, InCellInfo.Location.Y, InCellInfo.Location.Z });

	FWorldPartitionMiniMapHelper::CaptureBoundsMiniMapToTexture(World, WorldMiniMap, WorldMiniMap->MiniMapTileSize, static_cast<UTexture2D*&>(MinimapTile.Texture), TextureName, InCellInfo.Bounds);

	MiniMapTiles.Add(MoveTemp(MinimapTile));

	return true;
}

bool UWorldPartitionMiniMapBuilder::PostRun(UWorld* World, FPackageSourceControlHelper& PackageHelper, const bool bInRunSuccess)
{
	TArray<FTextureSourceBlock> SourceBlocks;
	TArray<const uint8*>		SourceImageData;

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

	SourceBlocks.Reserve(MiniMapTiles.Num());
	SourceImageData.Reserve(MiniMapTiles.Num());

	FTileCoordinates Min = { FMath::FloorToInt(EditorBounds.Min.X / IterativeCellSize), FMath::FloorToInt(EditorBounds.Min.Y / IterativeCellSize) };
	FTileCoordinates Max = { FMath::FloorToInt(EditorBounds.Max.X / IterativeCellSize), FMath::FloorToInt(EditorBounds.Max.Y / IterativeCellSize) };

	FVector2D				Offset;
	ETextureSourceFormat	Format = TSF_BGRA8;

	for (FMinimapTile& Tile : MiniMapTiles)
	{
		FTextureSourceBlock* Block = new(SourceBlocks) FTextureSourceBlock();
		bool bIsOrigin = (Tile.Coordinates.X == 0) && (Tile.Coordinates.Y == 0);

		Format = (Tile.Texture != nullptr) ? Tile.Texture->Source.GetFormat() : Format;

		// Offset X coordinates and reverse Y
		Tile.Coordinates.X = Tile.Coordinates.X - Min.X;
		Tile.Coordinates.Y = Max.Y - Tile.Coordinates.Y;

		if (bIsOrigin)
		{
			Offset.X = Tile.Coordinates.X;
			Offset.Y = -Tile.Coordinates.Y;
		}

		Block->BlockX = Tile.Coordinates.X;
		Block->BlockY = Tile.Coordinates.Y;
		Block->SizeX = Tile.Texture->GetSizeX();
		Block->SizeY = Tile.Texture->GetSizeY();
		Block->NumSlices = 1;
		Block->NumMips = 1;

		const uint8* DataPtr = Tile.Texture->Source.LockMip(0);
		SourceImageData.Add(DataPtr);
	}

	TStrongObjectPtr<UTextureFactory> Factory(NewObject<UTextureFactory>());

	WorldMiniMap->MiniMapTexture = Factory->CreateTexture2D(WorldMiniMap, TEXT("MinimapTexture"), RF_NoFlags);
	WorldMiniMap->MiniMapTexture->Source.InitBlocked(&Format, SourceBlocks.GetData(), 1, SourceBlocks.Num(), SourceImageData.GetData());

	for (const FMinimapTile& Tile : MiniMapTiles)
	{
		Tile.Texture->Source.UnlockMip(0);
	}

	WorldMiniMap->MiniMapTexture->AdjustMinAlpha = 1.f;
	WorldMiniMap->MiniMapTexture->LODGroup = TEXTUREGROUP_UI;
	WorldMiniMap->MiniMapTexture->VirtualTextureStreaming = true;
	WorldMiniMap->MiniMapTexture->UpdateResource();

	FBox2D UVOffset(FVector2D(EditorBounds.Min.X / IterativeCellSize + Offset.X, EditorBounds.Min.Y / IterativeCellSize + Offset.Y),
					FVector2D(EditorBounds.Max.X / IterativeCellSize + Offset.X, EditorBounds.Max.Y / IterativeCellSize + Offset.Y));

	UVOffset.bIsValid = true;
	WorldMiniMap->UVOffset = UVOffset;

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
