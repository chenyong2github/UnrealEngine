// Copyright Epic Games, Inc. All Rights Reserved.

#include "WorldPartition/WorldPartitionMiniMapBuilder.h"

#include "CoreMinimal.h"
#include "Logging/LogMacros.h"
#include "Misc/CommandLine.h"

#include "Engine/World.h"
#include "SourceControlHelpers.h"

#include "ISourceControlModule.h"
#include "ISourceControlProvider.h"
#include "SourceControlOperations.h"

#include "WorldPartition/ActorDescContainer.h"
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
	bUseOnlyHLODs = FParse::Param(FCommandLine::Get(), TEXT("UseOnlyHLODs"));
	bAutoSubmit = bUseOnlyHLODs = FParse::Param(FCommandLine::Get(), TEXT("AutoSubmit"));
}

bool UWorldPartitionMiniMapBuilder::Run(UWorld* World, FPackageSourceControlHelper& PackageHelper)
{
	AWorldPartitionMiniMap* WorldMiniMap = FWorldPartitionMiniMapHelper::GetWorldPartitionMiniMap(World,true);
	if (!WorldMiniMap)
	{
		UE_LOG(LogWorldPartitionMiniMapBuilder, Error, TEXT("Failed to create Minimap. WorldPartitionMiniMap actor not found in the persistent level."));
		return false;
	}

	// Load HLOD actors first if using only HLODs to generate MiniMap
	TArray<FWorldPartitionReference> HLODActors;
	if (bUseOnlyHLODs)
	{
		UWorldPartition* WorldPartition = World->GetWorldPartition();

		TSet<FGuid> AllSubActors;
		for (UActorDescContainer::TIterator<AWorldPartitionHLOD> HLODIterator(WorldPartition); HLODIterator; ++HLODIterator)
		{
			AllSubActors.Append(HLODIterator->GetSubActors());
		}

		for (UActorDescContainer::TIterator<AWorldPartitionHLOD> HLODIterator(WorldPartition); HLODIterator; ++HLODIterator)
		{
			// Only include top level HLODs - If an HLOD actor isn't included as a subactor it means it is top level
			if (!AllSubActors.Contains(HLODIterator->GetGuid()))
			{
				FWorldPartitionReference HLODActorRef(WorldPartition, HLODIterator->GetGuid());

				AWorldPartitionHLOD* Actor = Cast<AWorldPartitionHLOD>(HLODActorRef->GetActor());
				Actor->SetIsTemporarilyHiddenInEditor(false);

				HLODActors.Add(HLODActorRef);
			}
		}
	}

	WorldMiniMap->MiniMapSize = this->MiniMapSize;
	FWorldPartitionMiniMapHelper::CaptureWorldMiniMapToTexture(World, WorldMiniMap, WorldMiniMap->MiniMapSize, static_cast<UTexture2D*&>(WorldMiniMap->MiniMapTexture), WorldMiniMap->MiniMapWorldBounds);

	// Save MiniMap Package
	auto WorldMiniMapExternalPackage = WorldMiniMap->GetExternalPackage();
	FString PackageFileName = SourceControlHelpers::PackageFilename(WorldMiniMapExternalPackage);

	if (!PackageHelper.Checkout(WorldMiniMapExternalPackage))
	{
		UE_LOG(LogWorldPartitionMiniMapBuilder, Error, TEXT("Error checking out package %s."), *WorldMiniMapExternalPackage->GetName());
		return false;
	}
	
	if (!UPackage::SavePackage(WorldMiniMapExternalPackage, nullptr, RF_Standalone, *PackageFileName, GError, nullptr, false, true, SAVE_Async))
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
