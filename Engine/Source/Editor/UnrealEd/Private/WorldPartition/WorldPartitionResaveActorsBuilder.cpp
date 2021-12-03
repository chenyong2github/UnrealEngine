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


DEFINE_LOG_CATEGORY_STATIC(LogWorldPartitionResaveActorsBuilder, All, All);

UWorldPartitionResaveActorsBuilder::UWorldPartitionResaveActorsBuilder(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{}

bool UWorldPartitionResaveActorsBuilder::PreRun(UWorld* World, FPackageSourceControlHelper& PackageHelper)
{
	FParse::Value(FCommandLine::Get(), TEXT("ActorClass="), ActorClassName);
	
	TArray<FString> Tokens, Switches;
	UCommandlet::ParseCommandLine(FCommandLine::Get(), Tokens, Switches);

	bSwitchActorPackagingSchemeToReduced = Switches.Contains(TEXT("SwitchActorPackagingSchemeToReduced"));

	return true;
}

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

	return true;
}

bool UWorldPartitionResaveActorsBuilder::RunInternal(UWorld* World, const FCellInfo& InCellInfo, FPackageSourceControlHelper& PackageHelper)
{
	FPackageSourceControlHelper SCCHelper;

	ParseCommandline();
	UPackage* WorldPackage = World->GetPackage();

	int32 LoadCount = 0;
	int32 SaveCount = 0;
	int32 SkipCount = 0;
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

		FWorldPartitionHelpers::ForEachActorWithLoading(WorldPartition, ActorClass, [this, &LoadCount, &SaveCount, &FailCount, &SkipCount, &SCCHelper, &PackagesToDelete, WorldPackage](const FWorldPartitionActorDesc* ActorDesc)
		{
			ON_SCOPE_EXIT
			{
				UE_LOG(LogWorldPartitionResaveActorsBuilder, Display, TEXT("Processed %d packages (%d Saved / %d Skipped / %d Failed)"), SaveCount + SkipCount + FailCount, SaveCount, SkipCount, FailCount);
			};

			AActor* Actor = ActorDesc->GetActor();

			if (!Actor)
			{
				UE_LOG(LogWorldPartitionResaveActorsBuilder, Error, TEXT("Error loading package %s."), *ActorDesc->GetActorPackage().ToString());
				PackagesToDelete.Add(ActorDesc->GetActorPackage().ToString());
				++FailCount;
				return true;
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
						FSavePackageArgs SaveArgs;
						SaveArgs.TopLevelFlags = RF_Standalone;
						if (!UPackage::SavePackage(Package, nullptr, *PackageFileName, SaveArgs))
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
						if (!UPackage::SavePackage(NewActorPackage, nullptr, *NewPackageFileName, SaveArgs))
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
			}

			return true;
		});
	}

	// Normal save pass
	FWorldPartitionHelpers::ForEachActorWithLoading(WorldPartition, ActorClass, [this, &LoadCount, &SaveCount, &FailCount, &SCCHelper, &PackagesToDelete](const FWorldPartitionActorDesc* ActorDesc)
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

			if (bReportOnly || SCCHelper.Checkout(Package))
			{
				bool bShouldResaveActor = !bOnlyResaveOnActorDescDiff;

				if (bOnlyResaveOnActorDescDiff)
				{
					TUniquePtr<FWorldPartitionActorDesc> CurActorDesc = Actor->CreateActorDesc();
					const FWorldPartitionActorDesc& OldActorDesc = *ActorDesc;
					const FWorldPartitionActorDesc& NewActorDesc = *CurActorDesc.Get();

					// Bounds can change if the source asset changed
					if (!OldActorDesc.GetBounds().Equals(NewActorDesc.GetBounds(), 1.0f))
					{
						bShouldResaveActor = true;
					}
					// Grid placement can have changed from defaults
					else if (OldActorDesc.GetGridPlacement() != NewActorDesc.GetGridPlacement())
					{
						bShouldResaveActor = true;
					}
					// Editor-only flag can have changed from defaults
					else if (OldActorDesc.GetActorIsEditorOnly() != NewActorDesc.GetActorIsEditorOnly())
					{
						bShouldResaveActor = true;
					}

					if (bShouldResaveActor)
					{
						UE_LOG(LogWorldPartitionResaveActorsBuilder, Verbose, TEXT("Actor descriptor mismatch: [%s]<->[%s]"), *OldActorDesc.ToString(), *NewActorDesc.ToString());
					}
				}

				// Save package
				if (!bReportOnly && bShouldResaveActor)
				{
					FString PackageFileName = SourceControlHelpers::PackageFilename(Package);
					FSavePackageArgs SaveArgs;
					SaveArgs.TopLevelFlags = RF_Standalone;
					if (!UPackage::SavePackage(Package, nullptr, *PackageFileName, SaveArgs))
					{
						UE_LOG(LogWorldPartitionResaveActorsBuilder, Error, TEXT("Error saving package %s."), *Package->GetName());
						++FailCount;
						return true;
					}
				}
				else
				{
					UE_LOG(LogWorldPartitionResaveActorsBuilder, Display, TEXT("Skipped package %s."), *Package->GetName());
					++SkipCount;
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

	if (!bReportOnly)
	{
		UPackage::WaitForAsyncFileWrites();
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
			FSavePackageArgs SaveArgs;
			SaveArgs.TopLevelFlags = RF_Standalone;
			if (!UPackage::SavePackage(WorldPackage, nullptr, *PackageFileName, SaveArgs))
			{
				UE_LOG(LogWorldPartitionResaveActorsBuilder, Error, TEXT("Error saving world %s."), *WorldPackage->GetName());
				return false;
			}
		}
	}

	

	return true;
}
