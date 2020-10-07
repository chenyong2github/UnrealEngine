// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
 ConvertWaterBodyActorsCommandlet: Commandlet used to convert water bodies to typed water bodies
=============================================================================*/

#include "ConvertWaterBodyActorsCommandlet.h"

#if WITH_EDITOR
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "Misc/ConfigCacheIni.h"
#include "Editor.h"
#include "Engine/Level.h"
#include "Engine/World.h"
#include "Engine/LevelStreaming.h"
#include "UObject/UObjectHash.h"
#include "PackageHelperFunctions.h"
#include "ISourceControlOperation.h"
#include "SourceControlOperations.h"
#include "SourceControlHelpers.h"
#include "ISourceControlModule.h"
#include "UObject/MetaData.h"
#include "WaterBodyActor.h"
#include "WaterBodyRiverActor.h"
#include "WaterBodyOceanActor.h"
#include "WaterBodyLakeActor.h"
#include "WaterBodyCustomActor.h"
#include "ProfilingDebugging/ScopedTimers.h"
#endif

UConvertWaterBodyActorsCommandlet::UConvertWaterBodyActorsCommandlet(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

#if WITH_EDITOR

DEFINE_LOG_CATEGORY_STATIC(LogConvertWaterBodyActorsCommandlet, All, All);

ULevel* UConvertWaterBodyActorsCommandlet::LoadLevel(const FString& LevelToLoad) const
{
	SET_WARN_COLOR(COLOR_WHITE);
	UE_LOG(LogConvertWaterBodyActorsCommandlet, Log, TEXT("Loading level %s."), *LevelToLoad);
	CLEAR_WARN_COLOR();

	FString MapLoadCommand = FString::Printf(TEXT("MAP LOAD FILE=%s TEMPLATE=0 SHOWPROGRESS=0 FEATURELEVEL=3"), *LevelToLoad);
	GEditor->Exec(nullptr, *MapLoadCommand, *GError);
	FlushAsyncLoading();

	FString PackageName = FPackageName::FilenameToLongPackageName(LevelToLoad);
	UPackage* MapPackage = FindPackage(nullptr, *PackageName);
	UWorld* World = MapPackage ? UWorld::FindWorldInPackage(MapPackage) : nullptr;
	return World ? World->PersistentLevel : nullptr;
}

void UConvertWaterBodyActorsCommandlet::GetSubLevelsToConvert(ULevel* MainLevel, TSet<ULevel*>& SubLevels, bool bRecursive)
{
	UWorld* World = MainLevel->GetTypedOuter<UWorld>();
	for(ULevelStreaming* StreamingLevel: World->GetStreamingLevels())
	{
		if (ULevel* SubLevel = StreamingLevel->GetLoadedLevel())
		{
			SubLevels.Add(SubLevel);
			if (bRecursive)
			{
				// Recursively obtain sub levels to convert
				GetSubLevelsToConvert(SubLevel, SubLevels, bRecursive);
			}
		}
	}
}

int32 UConvertWaterBodyActorsCommandlet::Main(const FString& Params)
{
	FAutoScopedDurationTimer ConversionTimer;

	TArray<FString> Tokens, Switches;
	ParseCommandLine(*Params, Tokens, Switches);

	// Need at least the level to convert
	if (Tokens.Num() < 1)
	{
		UE_LOG(LogConvertWaterBodyActorsCommandlet, Error, TEXT("ConvertLevelToExternalActors bad parameters"));
		return 1;
	}

	bool bNoSourceControl = Switches.Contains(TEXT("nosourcecontrol"));
	bool bConvertSubLevel = Switches.Contains(TEXT("convertsublevels"));
	bool bRecursiveSubLevel = Switches.Contains(TEXT("recursive"));

	FScopedSourceControl SourceControl;
	SourceControlProvider = bNoSourceControl ? nullptr : &ISourceControlModule::Get().GetProvider();

	// Load persistent level
	ULevel* MainLevel = LoadLevel(Tokens[0]);
	if (!MainLevel)
	{
		UE_LOG(LogConvertWaterBodyActorsCommandlet, Error, TEXT("Unable to load level '%s'"), *Tokens[0]);
		return 1;
	}

	UWorld* MainWorld = MainLevel->GetWorld();
	UPackage* MainPackage = MainLevel->GetPackage();

	TSet<ULevel*> LevelsToConvert;
	LevelsToConvert.Add(MainLevel);
	if (bConvertSubLevel)
	{
		GetSubLevelsToConvert(MainLevel, LevelsToConvert, bRecursiveSubLevel);
	}
	
	TArray<UPackage*> PackagesToSave;
	for(ULevel* Level : LevelsToConvert)
	{
		TArray<AActor*> CurrentActors = Level->Actors;
		for (AActor* Actor : CurrentActors)
		{
			if (AWaterBody* OldActor = Cast<AWaterBody>(Actor))
			{
				UWorld* World = OldActor->GetWorld();
				UClass* SpawnClass = nullptr;
				switch (OldActor->GetWaterBodyType())
				{
					case EWaterBodyType::River: SpawnClass = AWaterBodyRiver::StaticClass(); break;
					case EWaterBodyType::Ocean: SpawnClass = AWaterBodyOcean::StaticClass(); break;
					case EWaterBodyType::Lake: SpawnClass = AWaterBodyLake::StaticClass(); break;
					case EWaterBodyType::Transition: SpawnClass = AWaterBodyCustom::StaticClass(); break;
				}
				if (SpawnClass)
				{
					FActorSpawnParameters SpawnInfo;
					SpawnInfo.OverrideLevel = OldActor->GetLevel();
					SpawnInfo.Owner = OldActor->GetOwner();
					SpawnInfo.Name = OldActor->GetFName();
					SpawnInfo.Instigator = OldActor->GetInstigator();
					SpawnInfo.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
					if (!OldActor->IsListedInSceneOutliner())
					{
						SpawnInfo.bHideFromSceneOutliner = true;
					}

					OldActor->Rename(nullptr, OldActor->GetOuter(), REN_DoNotDirty | REN_DontCreateRedirectors | REN_ForceNoResetLoaders);
					FVector  Location = FVector::ZeroVector;
					FRotator Rotation = FRotator::ZeroRotator;
					if (USceneComponent* OldRootComponent = OldActor->GetRootComponent())
					{
						// We need to make sure that the GetComponentTransform() transform is up to date, but we don't want to run any initialization logic
						// so we silence the update, cache it off, revert the change (so no events are raised), and then directly update the transform
						// with the value calculated in ConditionalUpdateComponentToWorld:
						FScopedMovementUpdate SilenceMovement(OldRootComponent);

						OldRootComponent->ConditionalUpdateComponentToWorld();
						FTransform OldComponentToWorld = OldRootComponent->GetComponentTransform();
						SilenceMovement.RevertMove();

						OldRootComponent->SetComponentToWorld(OldComponentToWorld);
						Location = OldActor->GetActorLocation();
						Rotation = OldActor->GetActorRotation();
					}

					AActor* NewActor = World->SpawnActor(SpawnClass, &Location, &Rotation, SpawnInfo);
					check(NewActor != nullptr);

					OldActor->DestroyConstructedComponents(); // don't want to serialize components from the old actor. Unregister native components so we don't copy any sub-components they generate for themselves (like UCameraComponent does)
					OldActor->UnregisterAllComponents();
					// Unregister any native components, might have cached state based on properties we are going to overwrite
					NewActor->UnregisterAllComponents();
					
					// Copy properties
					UEngine::FCopyPropertiesForUnrelatedObjectsParams CPFUOParams;
					CPFUOParams.bPreserveRootComponent = true;
					CPFUOParams.bAggressiveDefaultSubobjectReplacement = true;
					CPFUOParams.bNotifyObjectReplacement = true;
					CPFUOParams.bDoDelta = false;
					UEngine::CopyPropertiesForUnrelatedObjects(OldActor, NewActor, CPFUOParams);
					
					// Reset properties/streams
					NewActor->ResetPropertiesForConstruction();
					// Register native components
					NewActor->RegisterAllComponents();
					
					// Remove old actor
					World->EditorDestroyActor(OldActor, /*bShouldModifyLevel =*/true);
				}
			}
		}

		UPackage* LevelPackage = Level->GetPackage();
		PackagesToSave.Add(LevelPackage);
		PackagesToSave.Append(Level->GetLoadedExternalActorPackages());
	}

	if (UseSourceControl())
	{
		FEditorFileUtils::CheckoutPackages(PackagesToSave, nullptr, false);
	}
	else
	{
		for (UPackage* Package : PackagesToSave)
		{
			FString PackageFilename = SourceControlHelpers::PackageFilename(Package);
			if (IPlatformFile::GetPlatformPhysical().FileExists(*PackageFilename))
			{
				if (!IPlatformFile::GetPlatformPhysical().SetReadOnly(*PackageFilename, false))
				{
					UE_LOG(LogConvertWaterBodyActorsCommandlet, Error, TEXT("Error setting %s writable"), *PackageFilename);
					return 1;
				}
			}
		}
	}
	FEditorFileUtils::PromptForCheckoutAndSave(PackagesToSave, false, false, nullptr, true, false);
	UE_LOG(LogConvertWaterBodyActorsCommandlet, Log, TEXT("Conversion took %.2f seconds"), ConversionTimer.GetTime());

	return 0;
}

#endif