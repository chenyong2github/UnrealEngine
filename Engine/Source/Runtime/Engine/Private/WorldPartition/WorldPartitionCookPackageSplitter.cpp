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

// Register FWorldPartitionCookPackageSplitter for UWorld class
REGISTER_COOKPACKAGE_SPLITTER(FWorldPartitionCookPackageSplitter, UWorld);

bool FWorldPartitionCookPackageSplitter::ShouldSplit(UObject* SplitData)
{
	UWorld* World = Cast<UWorld>(SplitData);
	return World && World->IsPartitionedWorld();
}

FWorldPartitionCookPackageSplitter::FWorldPartitionCookPackageSplitter()
{
	FCoreUObjectDelegates::GetPreGarbageCollectDelegate().AddRaw(this, &FWorldPartitionCookPackageSplitter::PreGarbageCollect);
}

FWorldPartitionCookPackageSplitter::~FWorldPartitionCookPackageSplitter()
{
	FCoreUObjectDelegates::GetPreGarbageCollectDelegate().RemoveAll(this);
	TeardownWorldPartition();
}

void FWorldPartitionCookPackageSplitter::PreGarbageCollect()
{
	TeardownWorldPartition();
}

void FWorldPartitionCookPackageSplitter::TeardownWorldPartition()
{
	if (bWorldPartitionNeedsTeardown)
	{
		if (UWorld* LocalWorld = ReferencedWorld.Get())
		{
			UWorldPartition* WorldPartition = LocalWorld->PersistentLevel->GetWorldPartition();
			if (WorldPartition)
			{
				WorldPartition->Uninitialize();
			}
		}
		bWorldPartitionNeedsTeardown = false;
	}
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
	// until we have finished all of our TryPopulatePackage calls, because we store information on the World 
	// that is necessary for populate 
	ReferencedWorld = PartitionedWorld;

	// Manually initialize WorldPartition
	UWorldPartition* WorldPartition = PartitionedWorld->PersistentLevel->GetWorldPartition();
	// We expect the WorldPartition has not yet been initialized
	ensure(!WorldPartition->IsInitialized());
	WorldPartition->Initialize(PartitionedWorld, FTransform::Identity);

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

bool FWorldPartitionCookPackageSplitter::TryPopulatePackage(const UPackage* OwnerPackage, const UObject* OwnerObject,
	const FGeneratedPackageForPopulate& GeneratedPackage, bool bWasOwnerReloaded)
{
	if (bWasOwnerReloaded)
	{
		GetGenerateList(OwnerPackage, OwnerObject);
		// When GetGenerateList is called by CookOnTheFlyServer, it is followed up with a call to 
		// CleanupWorld when the Generator package is done saving, which calls WorldPartition->Uninitialize
		// But when we call it here to initialize our data for the populate, we need to take responsibility
		// for calling WorldPartition->Uninitialize before the next GarbageCollection
		bWorldPartitionNeedsTeardown = true;
	}

	// TODO: Make PopulateGeneratedPackageForCook const so we can honor the constness of the OwnerObject in this API function
	const UWorld* ConstPartitionedWorld = ValidateDataObject(OwnerObject);
	UWorld* PartitionedWorld = const_cast<UWorld*>(ConstPartitionedWorld);
	UWorldPartition* WorldPartition = PartitionedWorld->PersistentLevel->GetWorldPartition();
	bool bResult = WorldPartition->PopulateGeneratedPackageForCook(GeneratedPackage.Package, GeneratedPackage.RelativePath);
	return bResult;
}

void FWorldPartitionCookPackageSplitter::PreSaveGeneratorPackage(UPackage* OwnerPackage, UObject* OwnerObject,
	const TArray<FGeneratedPackageForPreSave>& GeneratedPackages)
{
	UWorld* PartitionedWorld = ValidateDataObject(OwnerObject);
	UWorldPartition* WorldPartition = PartitionedWorld->PersistentLevel->GetWorldPartition();
	WorldPartition->FinalizeGeneratorPackageForCook(GeneratedPackages);
}

#endif
