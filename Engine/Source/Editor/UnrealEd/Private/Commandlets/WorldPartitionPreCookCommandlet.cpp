// Copyright Epic Games, Inc. All Rights Reserved.

#include "Commandlets/WorldPartitionPreCookCommandlet.h"
#include "WorldPartition/WorldPartitionRuntimeHash.h"
#include "WorldPartition/WorldPartition.h"
#include "WorldPartition/WorldPartitionLevelHelper.h"
#include "LevelInstance/LevelInstanceActor.h"
#include "Engine/LevelStreamingDynamic.h"
#include "LevelUtils.h"
#include "Editor.h"
#include "AssetRegistryModule.h"

DEFINE_LOG_CATEGORY_STATIC(LogWorldPartitionPreCookCommandlet, All, All);

UWorldPartitionPreCookCommandlet::UWorldPartitionPreCookCommandlet(const FObjectInitializer& ObjectInitializer)
: Super(ObjectInitializer)
, MainWorld(nullptr)
{
}

void UWorldPartitionPreCookCommandlet::OnLevelInstanceActorPostLoad(ALevelInstance* LevelInstanceActor)
{
	if (LevelInstanceActor->IsLevelInstancePathValid())
	{
		if (ULevel::GetIsLevelPartitionedFromPackage(FName(*LevelInstanceActor->GetWorldAssetPackage())))
		{
			const TSoftObjectPtr<UWorld>& WorldAsset = LevelInstanceActor->GetWorldAsset();
			PartitionedWorldsToGenerate.Add(WorldAsset.GetLongPackageName());

			FString WorldAssetPath = WorldAsset.ToString();
			FString PackageName = FPackageName::ObjectPathToPackageName(WorldAssetPath);
			check(PackageName != WorldAssetPath);
			FString ObjectName = FPackageName::ObjectPathToObjectName(WorldAsset.ToString());
			check(ObjectName != WorldAssetPath);

			FString PackagePath = FPackageName::GetLongPackagePath(*PackageName);
			FName PackageShortName = FPackageName::GetShortFName(*PackageName);

			FString NewLevelInstance = FString::Printf(TEXT("%s/%s/%s/%s_Main.%s_Main"), *PackagePath, *PackageShortName.ToString(), FWorldPartitionLevelHelper::GetSavedLevelOutputSubFolderName(), *PackageShortName.ToString(), *ObjectName);
			LevelInstanceActor->SetWorldAsset(TSoftObjectPtr<UWorld>(FSoftObjectPath(NewLevelInstance)));
		}
	}
}

bool UWorldPartitionPreCookCommandlet::SaveLevel(ULevel* InLevel)
{
	// Resave the map
	UPackage* Package = InLevel->GetPackage();
	FString MapPackageName = Package->GetName();
	FString MapFilename = FPaths::GetBaseFilename(*Package->FileName.ToString());
	FString MapPackageFilename = FPackageName::LongPackageNameToFilename(Package->FileName.ToString());
	FString NewWorldName = FString::Printf(TEXT("%s_Main"), *MapFilename);

	UWorld* World = InLevel->GetWorld();
	World->Rename(*NewWorldName);
	Package->Rename(*FString::Printf(TEXT("%s/%s/%s"), *MapPackageName, FWorldPartitionLevelHelper::GetSavedLevelOutputSubFolderName(), *NewWorldName));

	if (!GEditor->Exec(nullptr, *FString::Printf(TEXT("OBJ SAVEPACKAGE PACKAGE=\"%s\" FILE=\"%s\""), *Package->GetName(), *FString::Printf(TEXT("%s/%s/%s%s"), *MapPackageFilename, FWorldPartitionLevelHelper::GetSavedLevelOutputSubFolderName(), *NewWorldName, *FPackageName::GetMapPackageExtension()))))
	{
		UE_LOG(LogWorldPartitionPreCookCommandlet, Error, TEXT("Error saving %s."), *MapPackageName);
		return false;
	}

	return true;
}

ULevel* UWorldPartitionPreCookCommandlet::LoadLevel(const FString& InLevelName)
{
	check(!MainWorld);
	SET_WARN_COLOR(COLOR_WHITE);
	UE_LOG(LogWorldPartitionPreCookCommandlet, Log, TEXT("Loading level %s."), *InLevelName);
	CLEAR_WARN_COLOR();

	UPackage* MapPackage = LoadPackage(nullptr, *InLevelName, LOAD_None);
	if (!MapPackage)
	{
		UE_LOG(LogWorldPartitionPreCookCommandlet, Error, TEXT("Error loading %s."), *InLevelName);
		return nullptr;
	}

	UWorld* World = UWorld::FindWorldInPackage(MapPackage);
	if (World)
	{
		// Setup the world.
		World->WorldType = EWorldType::Editor;
		World->AddToRoot();
		if (!World->bIsWorldInitialized)
		{
			UWorld::InitializationValues IVS;
			IVS.RequiresHitProxies(false);
			IVS.ShouldSimulatePhysics(false);
			IVS.EnableTraceCollision(false);
			IVS.CreateNavigation(false);
			IVS.CreateAISystem(false);
			IVS.AllowAudioPlayback(false);
			IVS.CreatePhysicsScene(true);

			World->InitWorld(IVS);
			World->PersistentLevel->UpdateModelComponents();
			World->UpdateWorldComponents(true, false);
		}

		ULevel* Level = World->PersistentLevel;
		MainWorld = World;
		return Level;
	}

	UE_LOG(LogWorldPartitionPreCookCommandlet, Error, TEXT("Unknown level '%s'"), *InLevelName);
	return nullptr;
}

ULevel* UWorldPartitionPreCookCommandlet::LoadSubLevel(const FString& InLevelName)
{
	check(MainWorld);
	ULevelStreamingDynamic* StreamingLevel = NewObject<ULevelStreamingDynamic>(MainWorld, NAME_None, RF_NoFlags, NULL);
	StreamingLevel->SetWorldAssetByPackageName(FName(InLevelName));
	StreamingLevel->bInitiallyLoaded = true;
	StreamingLevel->bInitiallyVisible = true;
	StreamingLevel->bShouldBlockOnLoad = true;
	StreamingLevel->SetShouldBeLoaded(true);
	StreamingLevel->SetShouldBeVisible(true);
	const FName PackageNameToLoad = FName(InLevelName);
	FString PackageFileName;
	if (!FPackageName::DoesPackageExist(PackageNameToLoad.ToString(), nullptr, &PackageFileName))
	{
		UE_LOG(LogWorldPartitionPreCookCommandlet, Error, TEXT("Unknown level %s"), *PackageNameToLoad.ToString());
		return nullptr;
	}
	StreamingLevel->PackageNameToLoad = FName(*FPackageName::FilenameToLongPackageName(PackageFileName));
	MainWorld->AddStreamingLevel(StreamingLevel);
	MainWorld->FlushLevelStreaming(EFlushLevelStreamingType::Full);
	ULevel* SubLevel = StreamingLevel->GetLoadedLevel();
	check(SubLevel);
	return SubLevel;
}

void UWorldPartitionPreCookCommandlet::RemoveSubLevel(ULevel* InLevel)
{
	check(MainWorld);
	ULevelStreaming* StreamingLevel = FLevelUtils::FindStreamingLevel(InLevel);
	check(StreamingLevel);
	ensure(MainWorld->RemoveStreamingLevel(StreamingLevel));
}

bool UWorldPartitionPreCookCommandlet::PreCookLevelAndSave(ULevel* InLevel)
{
	if (!InLevel)
	{
		return false;
	}

	UWorldPartition* WorldPartition = InLevel->GetWorldPartition();
	if (!WorldPartition)
	{
		UE_LOG(LogWorldPartitionPreCookCommandlet, Error, TEXT("WorldPartitionPreCook only works on partitioned maps"));
		return false;
	}
	check(!WorldPartition->IsPreCooked());
	check(WorldPartition->IsInitialized());

	// Generate runtime streaming cells
	if (!WorldPartition->GenerateStreaming(EWorldPartitionStreamingMode::RuntimeStreamingCells))
	{
		UE_LOG(LogWorldPartitionPreCookCommandlet, Error, TEXT("Error while generating streaming grid."));
		return false;
	}

	// Mark partition as pre-cooked and save
	WorldPartition->SetIsPreCooked(true);
	return SaveLevel(InLevel);
}

int32 UWorldPartitionPreCookCommandlet::Main(const FString& Params)
{
	TArray<FString> Tokens, Switches;
	ParseCommandLine(*Params, Tokens, Switches);

	if (Tokens.Num() != 1)
	{
		UE_LOG(LogWorldPartitionPreCookCommandlet, Error, TEXT("%s specified."), Tokens.Num() ? TEXT("Too many map packages") : TEXT("No map package"));
		return 1;
	}

	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	AssetRegistryModule.Get().SearchAllAssets(true);

	ALevelInstance::OnLevelInstanceActorPostLoad.AddUObject(this, &UWorldPartitionPreCookCommandlet::OnLevelInstanceActorPostLoad);

	ULevel* MainLevel = LoadLevel(Tokens[0]);
	if (!PreCookLevelAndSave(MainLevel))
	{
		return 1;
	}
	// At this point, main world is saved. We can clear its streaming levels (typically ULevelStreamingAlwaysLoaded) to avoid loading/processing them in subsequent LoadSubLevel calls which calls FlushLevelStremaing.
	MainWorld->ClearStreamingLevels();

	TSet<FString> PartitionedWorldsGenerated;
	while (PartitionedWorldsToGenerate.Num() > PartitionedWorldsGenerated.Num())
	{
		// Find a partitioned world to generate
		TSet<FString> ToGenerate = PartitionedWorldsToGenerate.Difference(PartitionedWorldsGenerated);
		FString SubLevelName = *ToGenerate.CreateIterator();
		ULevel* SubLevel = LoadSubLevel(SubLevelName);
		if (!PreCookLevelAndSave(SubLevel))
		{
			return 1;
		}
		PartitionedWorldsGenerated.Add(SubLevelName);
		RemoveSubLevel(SubLevel);
	}
	return 0;
}