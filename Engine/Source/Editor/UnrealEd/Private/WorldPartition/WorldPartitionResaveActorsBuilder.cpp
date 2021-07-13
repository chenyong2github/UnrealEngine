// Copyright Epic Games, Inc. All Rights Reserved.

#include "WorldPartition/WorldPartitionResaveActorsBuilder.h"

#include "WorldPartition/WorldPartition.h"
#include "WorldPartition/WorldPartitionHelpers.h"
#include "PackageSourceControlHelper.h"
#include "SourceControlHelpers.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"

DEFINE_LOG_CATEGORY_STATIC(LogWorldPartitionResaveActorsBuilder, All, All);

UWorldPartitionResaveActorsBuilder::UWorldPartitionResaveActorsBuilder(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	FParse::Value(FCommandLine::Get(), TEXT("ActorClass="), ActorClassName);
}

bool UWorldPartitionResaveActorsBuilder::RunInternal(UWorld* World, const FBox& Bounds, FPackageSourceControlHelper& PackageHelper)
{
	FPackageSourceControlHelper SCCHelper;

	int32 SaveCount = 0;
	int32 FailCount = 0;

	// Actor Class Filter
	UClass* ActorClass = AActor::StaticClass();
	// @todo_ow: support BP Classes when the ActorDesc have that information
	if (!ActorClassName.IsEmpty())
	{
		ActorClass = FindObject<UClass>(ANY_PACKAGE, *ActorClassName);
		if (!ActorClass)
		{
			UE_LOG(LogWorldPartitionResaveActorsBuilder, Error, TEXT("Failed to find Actor Class: %s."), *ActorClassName);
			return false;
		}
	}

	UWorldPartition* WorldPartition = World->GetWorldPartition();
	if (!WorldPartition)
	{
		UE_LOG(LogWorldPartitionResaveActorsBuilder, Error, TEXT("Failed to retrieve WorldPartition."));
		return false;
	}

	TArray<FString> PackagesToDelete;
	FWorldPartitionHelpers::ForEachActorWithLoading(WorldPartition, ActorClass, [&SaveCount, &FailCount, &SCCHelper, &PackagesToDelete](const FWorldPartitionActorDesc* ActorDesc)
	{
		ON_SCOPE_EXIT
		{
			UE_LOG(LogWorldPartitionResaveActorsBuilder, Display, TEXT("Processed %d packages (%d Saved / %d Failed)"), SaveCount + FailCount, SaveCount, FailCount);
		};

		AActor* Actor = ActorDesc->GetActor();

		if (!Actor)
		{
			PackagesToDelete.Add(ActorDesc->GetActorPackage().ToString());
			++FailCount;
			return true;
		}
		else
		{		
			UPackage* Package = Actor->GetExternalPackage();
			check(Package);

			if (SCCHelper.Checkout(Package))
			{
				// Save package
				FString PackageFileName = SourceControlHelpers::PackageFilename(Package);
				if (!UPackage::SavePackage(Package, nullptr, RF_Standalone, *PackageFileName))
				{
					UE_LOG(LogWorldPartitionResaveActorsBuilder, Error, TEXT("Error saving package %s."), *Package->GetName());
					++FailCount;
					return true;
				}

				// It is possible the resave can't checkout everything. Continue processing.
				UE_LOG(LogWorldPartitionResaveActorsBuilder, Display, TEXT("Saved package %s."), *Package->GetName());
				++SaveCount;
				return true;
			}
			else
			{
				// It is possible the resave can't checkout everything. Continue processing.
				UE_LOG(LogWorldPartitionResaveActorsBuilder, Warning, TEXT("Error checking out package %s."), *Package->GetName());
				++FailCount;
				return true;
			}
		}

		return true;
	});

	UPackage::WaitForAsyncFileWrites();

	for (const FString& PackageToDelete : PackagesToDelete)
	{
		if (SCCHelper.Delete(PackageToDelete))
		{
			UE_LOG(LogWorldPartitionResaveActorsBuilder, Error, TEXT("Error deleting package %s."), *PackageToDelete);
			return true;
		}
	}

	return true;
}

