// Copyright Epic Games, Inc. All Rights Reserved.

#include "WorldPartition/WorldPartitionResaveActorsBuilder.h"
#include "WorldPartition/WorldPartition.h"
#include "WorldPartition/WorldPartitionHelpers.h"
#include "PackageSourceControlHelper.h"
#include "SourceControlHelpers.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Commandlets/Commandlet.h"
#include "UObject/MetaData.h"
#include "UObject/SavePackage.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "ReferenceCluster.h"
#include "ActorFolder.h"

DEFINE_LOG_CATEGORY_STATIC(LogWorldPartitionResaveActorsBuilder, All, All);

UWorldPartitionResaveActorsBuilder::UWorldPartitionResaveActorsBuilder(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{}

bool UWorldPartitionResaveActorsBuilder::PreRun(UWorld* World, FPackageSourceControlHelper& PackageHelper)
{
	TArray<FString> Tokens, Switches;
	UCommandlet::ParseCommandLine(FCommandLine::Get(), Tokens, Switches);

	//@todo_ow: generalize to all builders
	for (const FString& Switch : Switches)
	{
		FString Key, Value;
		if (!Switch.Split(TEXT("="), &Key, &Value))
		{
			Key = Switch;
		}

		// Lookup property
		const FProperty* Property = GetClass()->FindPropertyByName(*Key);

		// If we can't find the property, try for properties with the 'b' prefix
		if (!Property)
		{
			Key = TEXT("b") + Key;
			Property = GetClass()->FindPropertyByName(*Key);
		}

		if (Property)
		{
			// If the property is a bool, treat no values as true
			if (Property->IsA(FBoolProperty::StaticClass()) && Value.IsEmpty())
			{
				Value = TEXT("True");
			}

			uint8* Container = (uint8*)this;
			if (!FBlueprintEditorUtils::PropertyValueFromString(Property, Value, Container, nullptr))
			{
				UE_LOG(LogWorldPartitionResaveActorsBuilder, Error, TEXT("Cannot set value for '%s': '%s'"), *Key, *Value);
				return false;
			}
		}
	}

	if (bSwitchActorPackagingSchemeToReduced)
	{
		if (!ActorClassName.IsEmpty())
		{
			UE_LOG(LogWorldPartitionResaveActorsBuilder, Error, TEXT("SwitchActorPackagingSchemeToReduced is not compatible with ActorClassName"));
			return false;
		}
		else if (bResaveDirtyActorDescsOnly)
		{
			UE_LOG(LogWorldPartitionResaveActorsBuilder, Error, TEXT("SwitchActorPackagingSchemeToReduced is not compatible with ResaveDirtyActorDescsOnly"));
			return false;
		}
	}

	if (bEnableActorFolders)
	{
		if (bResaveDirtyActorDescsOnly)
		{
			UE_LOG(LogWorldPartitionResaveActorsBuilder, Error, TEXT("EnableActorFolders is not compatible with ResaveDirtyActorDescsOnly"));
			return false;
		}
	}

	return true;
}

bool UWorldPartitionResaveActorsBuilder::RunInternal(UWorld* World, const FCellInfo& InCellInfo, FPackageSourceControlHelper& PackageHelper)
{
	FPackageSourceControlHelper SCCHelper;

	UPackage* WorldPackage = World->GetPackage();

	int32 LoadCount = 0;
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

	if (bEnableActorFolders)
	{
		if (World->PersistentLevel->IsUsingActorFolders())
		{
			UE_LOG(LogWorldPartitionResaveActorsBuilder, Error, TEXT("World is already using actor folder objects."));
			return false;
		}

		World->PersistentLevel->SetUseActorFolders(true);
		World->PersistentLevel->bFixupActorFoldersAtLoad = false;
	}

	TArray<FString> PackagesToDelete;

	auto AddAndSavePackage = [&SaveCount, &FailCount, &SCCHelper](UPackage* InPackage, const FString& InPackageType)
	{
		// Save the new package and add to source control if needed
		FString NewPackageFileName = SourceControlHelpers::PackageFilename(InPackage);
		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Standalone;
		if (!UPackage::SavePackage(InPackage, nullptr, *NewPackageFileName, SaveArgs))
		{
			UE_LOG(LogWorldPartitionResaveActorsBuilder, Error, TEXT("Error saving %s package %s."), *InPackageType, *InPackage->GetName());
			++FailCount;
			return true;
		}

		if (!SCCHelper.AddToSourceControl(InPackage))
		{
			UE_LOG(LogWorldPartitionResaveActorsBuilder, Error, TEXT("Error adding %s package to source control %s."), *InPackageType, *InPackage->GetName());
			++FailCount;
			return true;
		}

		UE_LOG(LogWorldPartitionResaveActorsBuilder, Display, TEXT("Saved %s package %s."), *InPackageType, *InPackage->GetName());
		++SaveCount;
		return true;
	};

	auto CheckOutAndSavePackage = [&SaveCount, &FailCount, &SCCHelper](UPackage* InPackage, const FString& InPackageType, bool bErrorAtFailCheckout)
	{
		if (SCCHelper.Checkout(InPackage))
		{
			// Save package
			FString PackageFileName = SourceControlHelpers::PackageFilename(InPackage);
			FSavePackageArgs SaveArgs;
			SaveArgs.TopLevelFlags = RF_Standalone;
			if (!UPackage::SavePackage(InPackage, nullptr, *PackageFileName, SaveArgs))
			{
				UE_LOG(LogWorldPartitionResaveActorsBuilder, Error, TEXT("Error saving %s package %s."), *InPackageType, *InPackage->GetName());
				++FailCount;
				return true;
			}

			UE_LOG(LogWorldPartitionResaveActorsBuilder, Display, TEXT("Saved %s package %s."), *InPackageType, *InPackage->GetName());
			++SaveCount;
			return true;
		}
		else
		{
			if (bErrorAtFailCheckout)
			{
				UE_LOG(LogWorldPartitionResaveActorsBuilder, Error, TEXT("Error checking out %s package %s."), *InPackageType, *InPackage->GetName());
			}
			else
			{
				UE_LOG(LogWorldPartitionResaveActorsBuilder, Warning, TEXT("Error checking out %s package %s."), *InPackageType, *InPackage->GetName());
			}

			++FailCount;
			return true;
		}
	};

	if (bSwitchActorPackagingSchemeToReduced)
	{
		World->PersistentLevel->ActorPackagingScheme = EActorPackagingScheme::Reduced;

		// Build clusters
		TArray<TPair<FGuid, TArray<FGuid>>> ActorsWithRefs;
		for (FActorDescList::TIterator<> ActorDescIterator(WorldPartition); ActorDescIterator; ++ActorDescIterator)
		{
			ActorsWithRefs.Emplace(ActorDescIterator->GetGuid(), ActorDescIterator->GetReferences());
		}

		TArray<TArray<FGuid>> Clusters = GenerateObjectsClusters(ActorsWithRefs);

		for (const TArray<FGuid>& Cluster : Clusters)
		{
			TArray<UPackage*> PackagesToSave;

			// Load actor clusters
			TArray<FWorldPartitionReference> ActorReferences;
			for (const FGuid& ActorGuid : Cluster)
			{
				ActorReferences.Emplace(WorldPartition, ActorGuid);
			}

			// Change packaging of all actors in the current cluster
			for (FWorldPartitionReference& ActorReference : ActorReferences)
			{
				const FWorldPartitionActorDesc* ActorDesc = ActorReference.Get();
				AActor* Actor = ActorDesc->GetActor();

				if (!Actor)
				{
					PackagesToDelete.Add(ActorDesc->GetActorPackage().ToString());
					++FailCount;
					continue;
				}
				else
				{
					++LoadCount;

					UPackage* Package = Actor->GetExternalPackage();
					check(Package);

					if (!bReportOnly)
					{
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

							// Move dependant objects into the new world package temporarily
							for (UObject* DependantObject : DependantObjects)
							{
								DependantObject->Rename(nullptr, WorldPackage, REN_NonTransactional | REN_DontCreateRedirectors | REN_ForceNoResetLoaders);
							}

							// Move actor in its new package
							Actor->SetPackageExternal(true);

							// Also move dependant objects into the new package
							UPackage* NewActorPackage = Actor->GetExternalPackage();
							for (UObject* DependantObject : DependantObjects)
							{
								DependantObject->Rename(nullptr, NewActorPackage, REN_NonTransactional | REN_DontCreateRedirectors | REN_ForceNoResetLoaders);
							}

							PackagesToSave.Add(NewActorPackage);
						}
						else
						{
							// It is possible the resave can't checkout everything. Continue processing.
							UE_LOG(LogWorldPartitionResaveActorsBuilder, Warning, TEXT("Error checking out package %s."), *Package->GetName());
							++FailCount;
							continue;
						}
					}
				}
			}

			// Save modified actors
			for (UPackage* PackageToSave : PackagesToSave)
			{
				AddAndSavePackage(PackageToSave, TEXT("Actor"));
			}
		}
	}
	else
	{
		FWorldPartitionHelpers::ForEachActorWithLoading(WorldPartition, ActorClass, [this, &LoadCount, &SaveCount, &FailCount, &SCCHelper, &PackagesToDelete, &CheckOutAndSavePackage](const FWorldPartitionActorDesc* ActorDesc)
		{
			ON_SCOPE_EXIT
			{
				UE_LOG(LogWorldPartitionResaveActorsBuilder, Display, TEXT("Processed %d packages (%d Saved / %d Failed)"), LoadCount, SaveCount, FailCount);
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
				++LoadCount;

				if (bEnableActorFolders)
				{
					if (!Actor->CreateOrUpdateActorFolder())
					{
						UE_LOG(LogWorldPartitionResaveActorsBuilder, Error, TEXT("Failed to create actor folder for actor %s."), *Actor->GetName());
						++FailCount;
						return true;
					}
				}

				UPackage* Package = Actor->GetExternalPackage();
				check(Package);

				if (bResaveDirtyActorDescsOnly)
				{
					TUniquePtr<FWorldPartitionActorDesc> NewActorDesc = Actor->CreateActorDesc();

					if (ActorDesc->Equals(NewActorDesc.Get()))
					{
						return true;
					}

					UE_LOG(LogWorldPartitionResaveActorsBuilder, Log, TEXT("Package %s needs to be resaved."), *Package->GetName());
				}

				if (!bReportOnly)
				{
					return CheckOutAndSavePackage(Package, TEXT("Actor"), /*bErrorAtFailCheckout*/ false);
				}
			}

			return true;
		});
	}

	if (!bReportOnly)
	{
		SCCHelper.Delete(PackagesToDelete);

		if (bEnableActorFolders)
		{
			World->PersistentLevel->ForEachActorFolder([&AddAndSavePackage](UActorFolder* ActorFolder)
			{
				UPackage* NewActorFolderPackage = ActorFolder->GetExternalPackage();
				check(NewActorFolderPackage);
				return AddAndSavePackage(NewActorFolderPackage, TEXT("Actor Folder"));
			});
		}

		const bool bNeedWorldResave = bSwitchActorPackagingSchemeToReduced || bEnableActorFolders;
		if (bNeedWorldResave)
		{
			return CheckOutAndSavePackage(WorldPackage, TEXT("World"), /*bErrorAtFailCheckout*/ true);
		}
	}

	return true;
}