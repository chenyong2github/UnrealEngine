// Copyright Epic Games, Inc. All Rights Reserved.

#include "WorldPartition/WorldPartitionCookPackageSplitter.h"

#if WITH_EDITOR

#include "WorldPartition/WorldPartitionRuntimeLevelStreamingCell.h"
#include "WorldPartition/WorldPartitionLevelStreamingDynamic.h"
#include "WorldPartition/WorldPartitionRuntimeCell.h"
#include "WorldPartition/WorldPartitionRuntimeHash.h"
#include "WorldPartition/WorldPartitionSubsystem.h"
#include "WorldPartition/WorldPartition.h"
#include "WorldPartition/HLOD/HLODActor.h"
#include "WorldPartition/HLOD/HLODActorDescFactory.h"
#include "WorldPartition/LevelInstance/LevelInstanceActorDescFactory.h"
#include "WorldPartition/Landscape/LandscapeActorDescFactory.h"
#include "WorldPartition/ActorPartition/PartitionActorDescFactory.h"
#include "LevelInstance/LevelInstanceActor.h"
#include "LandscapeProxy.h"
#include "ActorPartition/PartitionActor.h"

// Register FWorldPartitionCookPackageSplitter for UWorld class
REGISTER_COOKPACKAGE_SPLITTER(FWorldPartitionCookPackageSplitter, UWorld);

static FHLODActorDescFactory GHLODActorDescFactory;
static FLevelInstanceActorDescFactory GLevelInstanceActorDescFactory;
static FLandscapeActorDescFactory GLandscapeActorDescFactory;
static FPartitionActorDescFactory GPartitionActorDescFactory;

bool FWorldPartitionCookPackageSplitter::ShouldSplit(UObject* SplitData)
{
	UWorld* World = Cast<UWorld>(SplitData);
	return World && !!World->GetWorldPartition();
}

void FWorldPartitionCookPackageSplitter::SetDataObject(UObject* SplitData)
{
	PartitionedWorld = CastChecked<UWorld>(SplitData);
	check(PartitionedWorld);
	check(PartitionedWorld->PersistentLevel);
	check(PartitionedWorld->PersistentLevel->GetWorldPartition());
}

TArray<ICookPackageSplitter::FGeneratedPackage> FWorldPartitionCookPackageSplitter::GetGenerateList()
{
	// World is not initialized
	ensure(!PartitionedWorld->bIsWorldInitialized);

	// Manually register ActorDesc factories.
	// @todo_ow: Revisit how these factories are declared. This could be automatic.
	UWorldPartition::RegisterActorDescFactory(AWorldPartitionHLOD::StaticClass(), &GHLODActorDescFactory);
	UWorldPartition::RegisterActorDescFactory(ALevelInstance::StaticClass(), &GLevelInstanceActorDescFactory);
	UWorldPartition::RegisterActorDescFactory(ALandscapeProxy::StaticClass(), &GLandscapeActorDescFactory);
	UWorldPartition::RegisterActorDescFactory(APartitionActor::StaticClass(), &GPartitionActorDescFactory);

	// Manually initialize WorldPartition
	UWorldPartition* WorldPartition = PartitionedWorld->PersistentLevel->GetWorldPartition();
	WorldPartition->Initialize(PartitionedWorld, FTransform::Identity);

	TArray<FString> WorldPartitionGeneratedPackages;
	WorldPartition->GenerateStreaming(EWorldPartitionStreamingMode::RuntimeStreamingCells, &WorldPartitionGeneratedPackages);

	TArray<ICookPackageSplitter::FGeneratedPackage> PackagesToGenerate;
	PackagesToGenerate.Reserve(WorldPartitionGeneratedPackages.Num());
	for (const FString& PackageName : WorldPartitionGeneratedPackages)
	{
		PackagesToGenerate.Emplace_GetRef().RelativePath = PackageName;
		// @todo_ow: Set dependencies once we get iterative cooking working
	}
	return PackagesToGenerate;
}

bool FWorldPartitionCookPackageSplitter::TryPopulatePackage(UPackage* GeneratedPackage, const FStringView& RelativePath, const FStringView& GeneratedPackageCookName)
{
	UWorldPartition* WorldPartition = PartitionedWorld->PersistentLevel->GetWorldPartition();
	return WorldPartition->PopulateGeneratedPackageForCook(GeneratedPackage, FString(RelativePath), FString(GeneratedPackageCookName));
}

void FWorldPartitionCookPackageSplitter::FinalizeGeneratorPackage()
{
	UWorldPartition* WorldPartition = PartitionedWorld->PersistentLevel->GetWorldPartition();
	WorldPartition->FinalizeGeneratedPackageForCook();
}

#endif