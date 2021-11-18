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
{}

bool UWorldPartitionResaveActorsBuilder::OnPartitionBuildStarted(UWorld* World, FPackageSourceControlHelper& PackageHelper)
{
	FParse::Value(FCommandLine::Get(), TEXT("ActorClass="), ActorClassName);
	
	TArray<FString> Tokens, Switches;
	UCommandlet::ParseCommandLine(FCommandLine::Get(), Tokens, Switches);

	bSwitchActorPackagingSchemeToReduced = Switches.Contains(TEXT("SwitchActorPackagingSchemeToReduced"));

	return true;
}

bool UWorldPartitionResaveActorsBuilder::RunInternal(UWorld* World, const FCellInfo& InCellInfo, FPackageSourceControlHelper& PackageHelper)
{
	FPackageSourceControlHelper SCCHelper;

	UPackage* WorldPackage = World->GetPackage();

	int32 SaveCount = 0;
	int32 FailCount = 0;

	// Actor Class Filter
	UClass* ActorClass = AActor::StaticClass();

	// @todo_ow: support BP Classes when the ActorDesc have that information
	if (!ActorClassName.IsEmpty())
	{
		if (bSwitchActorPackagingSchemeToReduced)
		{
			UE_LOG(LogWorldPartitionResaveActorsBuilder, Error, TEXT("Changing the actor packaging scheme can't be executed on a subset of actors."));
			return false;
		}

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

	if (bSwitchActorPackagingSchemeToReduced)
	{
		if (World->PersistentLevel->GetActorPackagingScheme() == EActorPackagingScheme::Reduced)
		{
			UE_LOG(LogWorldPartitionResaveActorsBuilder, Error, TEXT("World is already using the reduced actor packaging scheme."));
			return false;
		}

		World->PersistentLevel->ActorPackagingScheme = EActorPackagingScheme::Reduced;
	}

	TArray<FString> PackagesToDelete;

	// Rename pass
	if (bSwitchActorPackagingSchemeToReduced)
	{
		World->PersistentLevel->ActorPackagingScheme = EActorPackagingScheme::Reduced;

		FWorldPartitionHelpers::ForEachActorWithLoading(WorldPartition, ActorClass, [this, &SaveCount, &FailCount, &SCCHelper, &PackagesToDelete, WorldPackage](const FWorldPartitionActorDesc* ActorDesc)
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
					// Always mark this package for deletion, as it will contain a temporary redirector to fixup references
					PackagesToDelete.Add(ActorDesc->GetActorPackage().ToString());

					// Move actor back into world's package
					Actor->SetPackageExternal(false);

					// Gather dependant objects that also needs to be moved along with the actor
					TArray<UObject*> DependantObjects;
					ForEachObjectWithPackage(Package, [&DependantObjects](UObject* Object)
					{
						if (!Cast<UMetaData>(Object))
						{
							DependantObjects.Add(Object);
						}
						return true;
					}, false);

					// Move dependant objects into the new world package temporarily, so we can save the original actor package with its redirector
					for (UObject* DependantObject : DependantObjects)
					{
						DependantObject->Rename(nullptr, WorldPackage, REN_NonTransactional | REN_DontCreateRedirectors | REN_ForceNoResetLoaders);
					}

					// Replace old actor package with a redirector to the new actor
					UObjectRedirector* Redirector = NewObject<UObjectRedirector>(Actor->GetWorld(), Actor->GetFName(), RF_Standalone | RF_Public);
					Redirector->DestinationObject = Actor;
					Redirector->SetExternalPackage(Package);

					// Save original actor package containing a single redirector to the actor
					FString PackageFileName = SourceControlHelpers::PackageFilename(Package);
					if (!UPackage::SavePackage(Package, nullptr, RF_Standalone, *PackageFileName))
					{
						UE_LOG(LogWorldPartitionResaveActorsBuilder, Error, TEXT("Error saving package redirector %s."), *Package->GetName());
						++FailCount;
						return true;
					}

					// Move actor in its new package
					Actor->SetPackageExternal(true);

					// Also move dependant objects into the new package
					UPackage* NewActorPackage = Actor->GetExternalPackage();
					for (UObject* DependantObject : DependantObjects)
					{
						DependantObject->Rename(nullptr, NewActorPackage, REN_NonTransactional | REN_DontCreateRedirectors | REN_ForceNoResetLoaders);
					}

					// Save the new actor package and add to source control if needed
					FString NewPackageFileName = SourceControlHelpers::PackageFilename(NewActorPackage);
					if (!UPackage::SavePackage(NewActorPackage, nullptr, RF_Standalone, *NewPackageFileName))
					{
						UE_LOG(LogWorldPartitionResaveActorsBuilder, Error, TEXT("Error saving package %s."), *NewActorPackage->GetName());
						++FailCount;
						return true;
					}

					if (!SCCHelper.AddToSourceControl(NewActorPackage))
					{
						UE_LOG(LogWorldPartitionResaveActorsBuilder, Error, TEXT("Error adding package to source control %s."), *NewActorPackage->GetName());
						++FailCount;
						return true;
					}

					UE_LOG(LogWorldPartitionResaveActorsBuilder, Display, TEXT("Saved package %s."), *NewActorPackage->GetName());
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
	}

	// Normal save pass
	FWorldPartitionHelpers::ForEachActorWithLoading(WorldPartition, ActorClass, [this, &SaveCount, &FailCount, &SCCHelper, &PackagesToDelete](const FWorldPartitionActorDesc* ActorDesc)
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

	SCCHelper.Delete(PackagesToDelete);

	if (bSwitchActorPackagingSchemeToReduced)
	{
		// checkout world package
		if (!SCCHelper.Checkout(WorldPackage))
		{
			UE_LOG(LogWorldPartitionResaveActorsBuilder, Error, TEXT("Error checking out world package %s."), *WorldPackage->GetName());
			return false;
		}

		// Save world package
		FString PackageFileName = SourceControlHelpers::PackageFilename(WorldPackage);
		if (!UPackage::SavePackage(WorldPackage, nullptr, RF_Standalone, *PackageFileName))
		{
			UE_LOG(LogWorldPartitionResaveActorsBuilder, Error, TEXT("Error saving world %s."), *WorldPackage->GetName());
			return false;
		}
	}

	return true;
}
