// Copyright Epic Games, Inc. All Rights Reserved.

#include "WorldPartition/WorldPartitionCookPackageSplitter.h"

#if WITH_EDITOR

#include "Misc/ConfigCacheIni.h"
#include "WorldPartition/WorldPartitionRuntimeLevelStreamingCell.h"
#include "WorldPartition/WorldPartitionLevelStreamingDynamic.h"
#include "WorldPartition/WorldPartitionRuntimeCell.h"
#include "WorldPartition/WorldPartitionRuntimeHash.h"
#include "WorldPartition/WorldPartitionSubsystem.h"
#include "WorldPartition/WorldPartition.h"
#include "Editor.h"

// Register FWorldPartitionCookPackageSplitter for UWorld class
REGISTER_COOKPACKAGE_SPLITTER(FWorldPartitionCookPackageSplitter, UWorld);

bool FWorldPartitionCookPackageSplitter::ShouldSplit(UObject* SplitData)
{
	UWorld* World = Cast<UWorld>(SplitData);
	return World && World->IsPartitionedWorld();
}

FWorldPartitionCookPackageSplitter::FWorldPartitionCookPackageSplitter()
{
}

FWorldPartitionCookPackageSplitter::~FWorldPartitionCookPackageSplitter()
{
	check(!ReferencedWorld);
}

void FWorldPartitionCookPackageSplitter::Teardown(ETeardown Status)
{
 	if (bInitializedWorldPartition)
	{
		if (UWorld* LocalWorld = ReferencedWorld.Get())
		{
			UWorldPartition* WorldPartition = LocalWorld->PersistentLevel->GetWorldPartition();
			if (WorldPartition)
			{
				WorldPartition->Uninitialize();
			}
		}
		bInitializedWorldPartition = false;
	}

	if (bInitializedPhysicsSceneForSave)
	{
		GEditor->CleanupPhysicsSceneThatWasInitializedForSave(ReferencedWorld.Get(), bForceInitializedWorld);
		bInitializedPhysicsSceneForSave = false;
		bForceInitializedWorld = false;
	}

	ReferencedWorld = nullptr;
}

void FWorldPartitionCookPackageSplitter::AddReferencedObjects(FReferenceCollector& Collector)
{
	Collector.AddReferencedObject(ReferencedWorld);
}

FString FWorldPartitionCookPackageSplitter::GetReferencerName() const
{
	return TEXT("FWorldPartitionCookPackageSplitter");
}

UWorld* FWorldPartitionCookPackageSplitter::ValidateDataObject(UObject* SplitData)
{
	UWorld* PartitionedWorld = CastChecked<UWorld>(SplitData);
	check(PartitionedWorld);
	check(PartitionedWorld->PersistentLevel);
	check(PartitionedWorld->IsPartitionedWorld());
	return PartitionedWorld;
}

const UWorld* FWorldPartitionCookPackageSplitter::ValidateDataObject(const UObject* SplitData)
{
	return ValidateDataObject(const_cast<UObject*>(SplitData));
}

TArray<ICookPackageSplitter::FGeneratedPackage> FWorldPartitionCookPackageSplitter::GetGenerateList(const UPackage* OwnerPackage, const UObject* OwnerObject)
{
	// TODO: Make WorldPartition functions const so we can honor the constness of the OwnerObject in this API function
	const UWorld* ConstPartitionedWorld = ValidateDataObject(OwnerObject);
	UWorld* PartitionedWorld = const_cast<UWorld*>(ConstPartitionedWorld);

	// Store the World pointer to declare it to GarbageCollection; we do not want to allow the World to be Garbage Collected
	// until we have finished all of our PreSaveGeneratedPackage calls, because we store information on the World 
	// that is necessary for populate 
	ReferencedWorld = PartitionedWorld;

	check(!bInitializedPhysicsSceneForSave && !bForceInitializedWorld);
	bInitializedPhysicsSceneForSave = GEditor->InitializePhysicsSceneForSaveIfNecessary(PartitionedWorld, bForceInitializedWorld);

	// Manually initialize WorldPartition
	UWorldPartition* WorldPartition = PartitionedWorld->PersistentLevel->GetWorldPartition();
	// We expect the WorldPartition has not yet been initialized
	ensure(!WorldPartition->IsInitialized());
	WorldPartition->Initialize(PartitionedWorld, FTransform::Identity);
	bInitializedWorldPartition = true;

	TArray<FString> WorldPartitionGeneratedPackages;
	WorldPartition->GenerateStreaming(&WorldPartitionGeneratedPackages);

	TArray<ICookPackageSplitter::FGeneratedPackage> PackagesToGenerate;
	PackagesToGenerate.Reserve(WorldPartitionGeneratedPackages.Num());
	for (const FString& PackageName : WorldPartitionGeneratedPackages)
	{
		ICookPackageSplitter::FGeneratedPackage& GeneratedPackage = PackagesToGenerate.Emplace_GetRef();
		GeneratedPackage.RelativePath = PackageName;
		// all packages we generate get a ULevel from CreateEmptyLevelForRuntimeCell and are hence maps
		GeneratedPackage.SetCreateAsMap(true);
		// @todo_ow: Set dependencies once we get iterative cooking working
	}
	return PackagesToGenerate;
}

bool FWorldPartitionCookPackageSplitter::PopulateGeneratedPackage(UPackage* OwnerPackage, UObject* OwnerObject,
	const FGeneratedPackageForPopulate& GeneratedPackage, TArray<UObject*>& OutObjectsToMove,
	TArray<UPackage*>& OutModifiedPackages)
{
	UWorld* PartitionedWorld = ValidateDataObject(OwnerObject);
	UWorldPartition* WorldPartition = PartitionedWorld->PersistentLevel->GetWorldPartition();
	return WorldPartition->PopulateGeneratedPackageForCook(GeneratedPackage.Package, GeneratedPackage.RelativePath, OutModifiedPackages);
}

bool FWorldPartitionCookPackageSplitter::PopulateGeneratorPackage(UPackage* OwnerPackage, UObject* OwnerObject,
	const TArray<ICookPackageSplitter::FGeneratedPackageForPreSave>& GeneratedPackages, TArray<UObject*>& OutObjectsToMove,
	TArray<UPackage*>& OutModifiedPackages)
{
	UWorld* PartitionedWorld = ValidateDataObject(OwnerObject);
	UWorldPartition* WorldPartition = PartitionedWorld->PersistentLevel->GetWorldPartition();
	return WorldPartition->PopulateGeneratorPackageForCook(GeneratedPackages, OutModifiedPackages);
}

void FWorldPartitionCookPackageSplitter::OnOwnerReloaded(UPackage* OwnerPackage, UObject* OwnerObject)
{
	// It should not be possible for the owner to reload due to garbage collection while we are active and keeping it referenced
	check(!ReferencedWorld); 
}
#endif
