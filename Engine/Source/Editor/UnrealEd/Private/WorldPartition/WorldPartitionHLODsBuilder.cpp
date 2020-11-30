// Copyright Epic Games, Inc. All Rights Reserved.

#include "WorldPartition/WorldPartitionHLODsBuilder.h"

#include "CoreMinimal.h"
#include "EngineUtils.h"
#include "Editor.h"
#include "Logging/LogMacros.h"

#include "WorldPartition/WorldPartition.h"
#include "WorldPartition/WorldPartitionSubsystem.h"
#include "WorldPartition/HLOD/HLODActor.h"
#include "WorldPartition/HLOD/HLODActorDesc.h"
#include "WorldPartition/WorldPartitionActorDescIterator.h"

DEFINE_LOG_CATEGORY_STATIC(LogWorldPartitionHLODsBuilder, All, All);

class FSourceControlHelper : public ISourceControlHelper
{
public:
	FSourceControlHelper(FPackageSourceControlHelper& InPackageHelper)
		: PackageHelper(InPackageHelper)
	{}

	virtual FString GetFilename(const FString& PackageName) const override
	{
		return SourceControlHelpers::PackageFilename(PackageName);
	}

	virtual FString GetFilename(UPackage* Package) const override
	{
		return SourceControlHelpers::PackageFilename(Package);
	}

	virtual bool Checkout(UPackage* Package) const override
	{
		return PackageHelper.Checkout(Package);
	}

	virtual bool Add(UPackage* Package) const override
	{
		return PackageHelper.AddToSourceControl(Package);
	}

	virtual bool Delete(const FString& PackageName) const override
	{
		return PackageHelper.Delete(PackageName);
	}

	virtual bool Delete(UPackage* Package) const override
	{
		return PackageHelper.Delete(Package);
	}

private:
	FPackageSourceControlHelper& PackageHelper;
};

UWorldPartitionHLODsBuilder::UWorldPartitionHLODsBuilder(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

bool UWorldPartitionHLODsBuilder::Run(UWorld* World, FPackageSourceControlHelper& PackageHelper)
{
	UWorldPartitionSubsystem* WorldPartitionSubsystem = World->GetSubsystem<UWorldPartitionSubsystem>();
	check(WorldPartitionSubsystem);

	UWorldPartition* WorldPartition = World->GetWorldPartition();
	check(WorldPartition);

	FSourceControlHelper SourceControlHelper(PackageHelper);

	// Rebuild HLOD for the whole world
	WorldPartition->GenerateHLOD(&SourceControlHelper);
	
	return true;
}