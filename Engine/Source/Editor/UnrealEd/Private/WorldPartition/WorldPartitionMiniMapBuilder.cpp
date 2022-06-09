// Copyright Epic Games, Inc. All Rights Reserved.

#include "WorldPartition/WorldPartitionMiniMapBuilder.h"

#include "CoreMinimal.h"
#include "Logging/LogMacros.h"
#include "Misc/CommandLine.h"

#include "AssetCompilingManager.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Factories/TextureFactory.h"
#include "SourceControlHelpers.h"
#include "UObject/SavePackage.h"
#include "UObject/StrongObjectPtr.h"

#include "ISourceControlModule.h"
#include "ISourceControlProvider.h"
#include "SourceControlOperations.h"

#include "WorldPartition/ActorDescContainer.h"
#include "WorldPartition/DataLayer/ActorDataLayer.h"
#include "WorldPartition/DataLayer/DataLayerInstance.h"
#include "WorldPartition/DataLayer/DataLayerSubsystem.h"
#include "WorldPartition/HLOD/HLODActor.h"
#include "WorldPartition/HLOD/HLODActorDesc.h"
#include "WorldPartition/WorldPartition.h"
#include "WorldPartition/WorldPartitionHandle.h"
#include "WorldPartition/WorldPartitionMiniMap.h"
#include "WorldPartition/WorldPartitionMiniMapHelper.h"
#include "WorldPartition/WorldPartitionMiniMapVolume.h"
#include "LevelInstance/LevelInstanceSubsystem.h"

DEFINE_LOG_CATEGORY_STATIC(LogWorldPartitionMiniMapBuilder, All, All);

UWorldPartitionMiniMapBuilder::UWorldPartitionMiniMapBuilder(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

bool UWorldPartitionMiniMapBuilder::PreRun(UWorld* World, FPackageSourceControlHelper& PackageHelper)
{
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
	{
		IterativeCellSize = WorldMiniMap->BuilderCellSize;

		WorldMiniMap->MiniMapTexture = nullptr;

		FBox WorldBounds(ForceInit);

		// Override the minimap bounds it a world partiion minimap volume exists
		for (TActorIterator<AWorldPartitionMiniMapVolume> It(World); It; ++It)
		{
			if (AWorldPartitionMiniMapVolume* WorldPartitionMiniMapVolume = *It)
			{
				WorldBounds += WorldPartitionMiniMapVolume->GetBounds().GetBox();
			}
		}

		if (!WorldBounds.IsValid)
		{
			WorldBounds = World->GetWorldPartition()->GetEditorWorldBounds();
		}

		MinimapImageSizeX = WorldBounds.GetSize().X / WorldMiniMap->WorldUnitsPerPixel;
		MinimapImageSizeY = WorldBounds.GetSize().Y / WorldMiniMap->WorldUnitsPerPixel;

		// For now, let's clamp to the maximum supported texture size
		MinimapImageSizeX = FMath::Min(MinimapImageSizeX, UTexture::GetMaximumDimensionOfNonVT());
		MinimapImageSizeY = FMath::Min(MinimapImageSizeY, UTexture::GetMaximumDimensionOfNonVT());
		WorldUnitsPerPixel = FMath::CeilToInt(FMath::Max(WorldBounds.GetSize().X / MinimapImageSizeX, WorldBounds.GetSize().Y / MinimapImageSizeY));
		MinimapImageSizeX = WorldBounds.GetSize().X / WorldUnitsPerPixel;
		MinimapImageSizeY = WorldBounds.GetSize().Y / WorldUnitsPerPixel;

		TStrongObjectPtr<UTextureFactory> Factory(NewObject<UTextureFactory>());
		WorldMiniMap->MiniMapTexture = Factory->CreateTexture2D(WorldMiniMap, TEXT("MinimapTexture"), RF_NoFlags);
		WorldMiniMap->MiniMapTexture->Source.Init(MinimapImageSizeX, MinimapImageSizeY, 1, 1, TSF_BGRA8);
		WorldMiniMap->MiniMapWorldBounds = WorldBounds;
		MiniMapSourcePtr = WorldMiniMap->MiniMapTexture->Source.LockMip(0);

		WorldToMinimap = FReversedZOrthoMatrix(WorldBounds.Min.X, WorldBounds.Max.X, WorldBounds.Min.Y, WorldBounds.Max.Y, 1.0f, 0.0f);

		FVector3d Translation(WorldBounds.Max.X / WorldBounds.GetSize().X, WorldBounds.Max.Y / WorldBounds.GetSize().Y, 0);
		FVector3d Scaling(MinimapImageSizeX, MinimapImageSizeY, 1);

		WorldToMinimap *= FTranslationMatrix(Translation);
		WorldToMinimap *= FScaleMatrix(Scaling);
	}

	UDataLayerSubsystem* DataLayerSubSystem = UWorld::GetSubsystem<UDataLayerSubsystem>(World);
	for (const FActorDataLayer& ActorDataLayer : WorldMiniMap->ExcludedDataLayers)
	{
		const UDataLayerInstance* DataLayerInstance = DataLayerSubSystem->GetDataLayerInstance(ActorDataLayer.Name);

		if (DataLayerInstance != nullptr)
		{
			ExcludedDataLayerShortNames.Add(FName(DataLayerInstance->GetDataLayerShortName()));
		}
	}

	return true;
}

bool UWorldPartitionMiniMapBuilder::RunInternal(UWorld* World, const FCellInfo& InCellInfo, FPackageSourceControlHelper& PackageHelper)
{
	check(World != nullptr);

	// World X,Y to minimap X,Y
	const FVector3d MinimapMin = WorldToMinimap.TransformPosition(InCellInfo.Bounds.Min);
	const FVector3d MinimapMax = WorldToMinimap.TransformPosition(InCellInfo.Bounds.Max);
	const FIntVector2 DstMin(FMath::Floor(MinimapMin.X), FMath::Floor(MinimapMin.Y));
	const FIntVector2 DstMax(FMath::Floor(MinimapMax.X), FMath::Floor(MinimapMax.Y));
	const FIntVector2 DstClampedMin = FIntVector2(FMath::Clamp(DstMin.X, 0, MinimapImageSizeX), FMath::Clamp(DstMin.Y, 0, MinimapImageSizeY));
	const FIntVector2 DstClampedMax = FIntVector2(FMath::Clamp(DstMax.X, 0, MinimapImageSizeX), FMath::Clamp(DstMax.Y, 0, MinimapImageSizeY));
	const uint32 CaptureWidthPixels = DstClampedMax.X - DstClampedMin.X;
	const uint32 CaptureHeightPixels = DstClampedMax.Y - DstClampedMin.Y;

	// Capture a tile if the region to capture is not empty
	if (CaptureWidthPixels > 0 && CaptureHeightPixels > 0)
	{
		FString TextureName = FString::Format(TEXT("MinimapTile_{0}_{1}_{2}"), { InCellInfo.Location.X, InCellInfo.Location.Y, InCellInfo.Location.Z });

		UTexture2D* TileTexture = NewObject<UTexture2D>(GetTransientPackage(), FName(TextureName), RF_Transient);
		TileTexture->Source.Init(CaptureWidthPixels, CaptureHeightPixels, 1, 1, TSF_BGRA8);
		TileTexture->PowerOfTwoMode = ETexturePowerOfTwoSetting::PadToPowerOfTwo;

		FWorldPartitionMiniMapHelper::CaptureBoundsMiniMapToTexture(World, GetTransientPackage(), CaptureWidthPixels, CaptureHeightPixels, TileTexture, TextureName, InCellInfo.Bounds, WorldMiniMap->CaptureSource, WorldMiniMap->CaptureWarmupFrames);

		// Copy captured image to VT minimap
		const uint32 BPP = TileTexture->Source.GetBytesPerPixel();
		const uint32 CopyWidthBytes = CaptureWidthPixels * BPP;

		const uint8* SrcDataPtr = TileTexture->Source.LockMipReadOnly(0);
		check(SrcDataPtr);

		const uint32 DstDataStrideBytes = WorldMiniMap->MiniMapTexture->Source.GetSizeX() * BPP;
		uint8* const DstDataPtr = MiniMapSourcePtr + (DstClampedMin.Y * DstDataStrideBytes) + (DstClampedMin.X * BPP);
		check(DstDataPtr);

		for (uint32 RowIdx = 0; RowIdx < CaptureHeightPixels; ++RowIdx)
		{
			uint8* DstCopy = DstDataPtr + DstDataStrideBytes * RowIdx;
			const uint8* SrcCopy = SrcDataPtr + CopyWidthBytes * RowIdx;
			
			check(DstCopy >= DstDataPtr);
			check(DstCopy + CopyWidthBytes <= DstDataPtr + WorldMiniMap->MiniMapTexture->Source.CalcMipSize(0));

			check(SrcCopy >= SrcDataPtr);
			check(SrcCopy + CopyWidthBytes <= SrcDataPtr + TileTexture->Source.CalcMipSize(0));

			FMemory::Memcpy(DstCopy, SrcCopy, CopyWidthBytes);
		}

		TileTexture->Source.UnlockMip(0);
	}

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

	// Finalize texture
	{
		WorldMiniMap->MiniMapTexture->Source.UnlockMip(0);
		WorldMiniMap->MiniMapTexture->PowerOfTwoMode = ETexturePowerOfTwoSetting::PadToPowerOfTwo;	// Required for VTs
		WorldMiniMap->MiniMapTexture->AdjustMinAlpha = 1.f;
		WorldMiniMap->MiniMapTexture->LODGroup = TEXTUREGROUP_UI;
		WorldMiniMap->MiniMapTexture->VirtualTextureStreaming = true;
		WorldMiniMap->MiniMapTexture->UpdateResource();
	}

	// Compute relevant UV space for the minimap
	{
		FVector2D TexturePOW2ScaleFactor = FVector2D((float)MinimapImageSizeX / FMath::RoundUpToPowerOfTwo(MinimapImageSizeX),
			(float)MinimapImageSizeY / FMath::RoundUpToPowerOfTwo(MinimapImageSizeY));

		WorldMiniMap->UVOffset.Min = FVector2d(0, 0);
		WorldMiniMap->UVOffset.Max = TexturePOW2ScaleFactor;
		WorldMiniMap->UVOffset.bIsValid = true;
	}

	// Make sure the minimap texture is ready before saving
	FAssetCompilingManager::Get().FinishAllCompilation();

	// Save MiniMap Package
	UPackage* WorldMiniMapExternalPackage = WorldMiniMap->GetExternalPackage();
	FString PackageFileName = SourceControlHelpers::PackageFilename(WorldMiniMapExternalPackage);

	if (!PackageHelper.Checkout(WorldMiniMapExternalPackage))
	{
		UE_LOG(LogWorldPartitionMiniMapBuilder, Error, TEXT("Error checking out package %s."), *WorldMiniMapExternalPackage->GetName());
		return false;
	}

	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Standalone;
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
