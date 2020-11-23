// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
 WorldPartitionConvertCommandlet.cpp: Commandlet used to convert levels to partition
=============================================================================*/

#include "Commandlets/WorldPartitionConvertCommandlet.h"
#include "Algo/Transform.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/Paths.h"
#include "Misc/ConfigCacheIni.h"
#include "Engine/Level.h"
#include "Engine/World.h"
#include "Engine/LevelBounds.h"
#include "Engine/LevelScriptActor.h"
#include "Engine/LODActor.h"
#include "Engine/LevelStreaming.h"
#include "Engine/MapBuildDataRegistry.h"
#include "WorldPartition/WorldPartition.h"
#include "WorldPartition/WorldPartitionSubsystem.h"
#include "WorldPartition/HLOD/HLODActor.h"
#include "WorldPartition/WorldPartitionMiniMap.h"
#include "WorldPartition/WorldPartitionMiniMapHelper.h"
#include "GameFramework/WorldSettings.h"
#include "UObject/UObjectHash.h"
#include "PackageHelperFunctions.h"
#include "UObject/MetaData.h"
#include "Editor.h"
#include "HierarchicalLOD.h"
#include "IHierarchicalLODUtilities.h"
#include "HierarchicalLODUtilitiesModule.h"
#include "InstancedFoliageActor.h"
#include "Engine/LevelScriptBlueprint.h"
#include "Editor/GroupActor.h"
#include "EdGraph/EdGraph.h"
#include "ProfilingDebugging/ScopedTimers.h"
#include "AssetRegistryModule.h"
#include "FoliageHelper.h"
#include "Engine/WorldComposition.h"
#include "ActorPartition/ActorPartitionSubsystem.h"
#include "InstancedFoliage.h"
#include "Landscape.h"
#include "LandscapeStreamingProxy.h"
#include "LandscapeInfo.h"
#include "LandscapeConfigHelper.h"
#include "ILandscapeSplineInterface.h"
#include "LandscapeSplineActor.h"
#include "LandscapeSplinesComponent.h"
#include "LandscapeSplineControlPoint.h"
#include "LandscapeGizmoActor.h"
#include "WorldPartition/DataLayer/DataLayer.h"
#include "WorldPartition/DataLayer/WorldDataLayers.h"

DEFINE_LOG_CATEGORY(LogWorldPartitionConvertCommandlet);

class FArchiveGatherPrivateImports : public FArchiveUObject
{
	AActor* Root;
	UPackage* RootPackage;
	UObject* CurrentObject;
	TMap<UObject*, UObject*>& PrivateRefsMap;
	TSet<FString>& ActorsReferencesToActors;

	void HandleObjectReference(UObject* Obj)
	{
		if(!Obj->HasAnyMarks(OBJECTMARK_TagImp))
		{
			UObject* OldCurrentObject = CurrentObject;
			CurrentObject = Obj;
			Obj->Mark(OBJECTMARK_TagImp);
			Obj->Serialize(*this);
			CurrentObject = OldCurrentObject;
		}
	}

public:
	FArchiveGatherPrivateImports(AActor* InRoot, TMap<UObject*, UObject*>& InPrivateRefsMap, TSet<FString>& InActorsReferencesToActors)
		: Root(InRoot)
		, RootPackage(InRoot->GetPackage())
		, CurrentObject(nullptr)
		, PrivateRefsMap(InPrivateRefsMap)
		, ActorsReferencesToActors(InActorsReferencesToActors)
	{
		SetIsSaving(true);
		SetIsPersistent(true);
		ArIsObjectReferenceCollector = true;
		ArShouldSkipBulkData = true;
		UnMarkAllObjects();
	}

	~FArchiveGatherPrivateImports()
	{
		UnMarkAllObjects();
	}

	virtual FArchive& operator<<(UObject*& Obj) override
	{
		if(Obj)
		{
			if(Obj->IsIn(Root) || (CurrentObject && Obj->IsIn(CurrentObject)))
			{
				HandleObjectReference(Obj);
			}
			else if(Obj->IsInPackage(RootPackage) && !Obj->HasAnyFlags(RF_Standalone))
			{
				if(!Obj->GetTypedOuter<AActor>())
				{
					UObject** OriginalRoot = PrivateRefsMap.Find(Obj);
					if(OriginalRoot && (*OriginalRoot != Root))
					{
						SET_WARN_COLOR(COLOR_RED);
						UE_LOG(LogWorldPartitionConvertCommandlet, Error, TEXT("Duplicate reference %s.%s(%s) (first referenced by %s)"), *Root->GetName(), *Obj->GetName(), *Obj->GetClass()->GetName(), *(*OriginalRoot)->GetName());
						CLEAR_WARN_COLOR();
					}
					else if(!OriginalRoot)
					{
						// Actor references will be extracted by the caller, ignore them
						if(Obj->IsA<AActor>() && !Obj->HasAnyFlags(RF_ClassDefaultObject | RF_ArchetypeObject) && Obj->GetTypedOuter<ULevel>())
						{
							AActor* ActorRef = (AActor*)Obj;
							ActorsReferencesToActors.Add(
								FString::Printf(
									TEXT("%s, %s, %s, %s, %.2f"), 
									*RootPackage->GetName(), 
									CurrentObject ? *CurrentObject->GetName() : *Root->GetName(), 
									CurrentObject ? *Root->GetName() : TEXT("null"),
									*Obj->GetName(), 
									(ActorRef->GetActorLocation() - Root->GetActorLocation()).Size())
							);
						}
						else if(!Obj->IsA<ULevel>())
						{
							if(!CurrentObject || !Obj->IsIn(CurrentObject))
							{
								PrivateRefsMap.Add(Obj, Root);

								SET_WARN_COLOR(COLOR_WHITE);
								UE_LOG(LogWorldPartitionConvertCommandlet, Warning, TEXT("Encountered reference %s.%s(%s)"), *Root->GetName(), *Obj->GetName(), *Obj->GetClass()->GetName());
								CLEAR_WARN_COLOR();
							}

							HandleObjectReference(Obj);
						}
					}
				}
			}
		}
		return *this;
	}
};

UWorldPartitionConvertCommandlet::UWorldPartitionConvertCommandlet(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, bConversionSuffix(false)
	, ConversionSuffix(TEXT("_WP"))
	, WorldOrigin(FVector::ZeroVector)
	, WorldExtent(HALF_WORLD_MAX)
	, LandscapeGridSize(4)
{}

UWorld* UWorldPartitionConvertCommandlet::LoadWorld(const FString& LevelToLoad)
{
	SET_WARN_COLOR(COLOR_WHITE);
	UE_LOG(LogWorldPartitionConvertCommandlet, Log, TEXT("Loading level %s."), *LevelToLoad);
	CLEAR_WARN_COLOR();

	UPackage* MapPackage = LoadPackage(nullptr, *LevelToLoad, LOAD_None);
	if (!MapPackage)
	{
		UE_LOG(LogWorldPartitionConvertCommandlet, Error, TEXT("Error loading %s."), *LevelToLoad);
		return nullptr;
	}

	return UWorld::FindWorldInPackage(MapPackage);
}

ULevel* UWorldPartitionConvertCommandlet::InitWorld(UWorld* World)
{
	SET_WARN_COLOR(COLOR_WHITE);
	UE_LOG(LogWorldPartitionConvertCommandlet, Log, TEXT("Initializing level %s."), *World->GetName());
	CLEAR_WARN_COLOR();

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

		World->FlushLevelStreaming(EFlushLevelStreamingType::Full);
	}

	return World->PersistentLevel;
}

UWorldPartition* UWorldPartitionConvertCommandlet::CreateWorldPartition(AWorldSettings* MainWorldSettings, UWorldComposition* WorldComposition) const
{
	UWorldPartition* WorldPartition = NewObject<UWorldPartition>(MainWorldSettings);
	MainWorldSettings->SetWorldPartition(WorldPartition);

	WorldPartition->EditorHash = NewObject<UWorldPartitionEditorHash>(WorldPartition, EditorHashClass);
	WorldPartition->RuntimeHash = NewObject<UWorldPartitionRuntimeHash>(WorldPartition, RuntimeHashClass);

	// Read the conversion config file
	if (FPlatformFileManager::Get().GetPlatformFile().FileExists(*LevelConfigFilename))
	{
		WorldPartition->EditorHash->LoadConfig(*EditorHashClass, *LevelConfigFilename);
		WorldPartition->RuntimeHash->LoadConfig(*RuntimeHashClass, *LevelConfigFilename);
		WorldPartition->DefaultHLODLayer = HLODLayers.FindRef(DefaultHLODLayerName);
	}
	else
	{
		WorldPartition->EditorHash->SetDefaultValues();
		WorldPartition->RuntimeHash->SetDefaultValues();
		WorldPartition->DefaultHLODLayer = nullptr;
	}

	WorldPartition->RuntimeHash->ImportFromWorldComposition(WorldComposition);

	return WorldPartition;
}

void UWorldPartitionConvertCommandlet::GatherAndPrepareSubLevelsToConvert(const UWorldPartition* WorldPartition, ULevel* Level, TArray<ULevel*>& SubLevels)
{
	UWorld* World = Level->GetTypedOuter<UWorld>();	

	// Set all streaming levels to be loaded/visible for next Flush
	for (ULevelStreaming* StreamingLevel : World->GetStreamingLevels())
	{
		StreamingLevel->SetShouldBeLoaded(true);
		StreamingLevel->SetShouldBeVisible(true);
	}

	World->FlushLevelStreaming(EFlushLevelStreamingType::Full);
	
	for(ULevelStreaming* StreamingLevel: World->GetStreamingLevels())
	{
		if (PrepareStreamingLevelForConversion(WorldPartition, StreamingLevel))
		{
			ULevel* SubLevel = StreamingLevel->GetLoadedLevel();
			check(SubLevel);

			SubLevels.Add(SubLevel);

			// Recursively obtain sub levels to convert
			GatherAndPrepareSubLevelsToConvert(WorldPartition, SubLevel, SubLevels);
		}
	}
}

EActorGridPlacement UWorldPartitionConvertCommandlet::GetLevelGridPlacement(ULevel* Level, EActorGridPlacement DefaultGridPlacement)
{
	FString WorldPath = Level->GetPackage()->GetName();
	if (EActorGridPlacement* CustomLevelGridPlacement = LevelsGridPlacement.Find(*WorldPath))
	{
		return *CustomLevelGridPlacement;
	}
	return DefaultGridPlacement;
}

bool UWorldPartitionConvertCommandlet::PrepareStreamingLevelForConversion(const UWorldPartition* WorldPartition, ULevelStreaming* StreamingLevel)
{
	ULevel* SubLevel = StreamingLevel->GetLoadedLevel();
	check(SubLevel);

	if (StreamingLevel->ShouldBeAlwaysLoaded() || StreamingLevel->bDisableDistanceStreaming)
	{
		FString WorldPath = SubLevel->GetPackage()->GetName();
		if (!LevelsGridPlacement.Contains(*WorldPath))
		{
			UE_LOG(LogWorldPartitionConvertCommandlet, Log, TEXT("Converting %s streaming level %s"), StreamingLevel->bDisableDistanceStreaming ? TEXT("non distance-based") : TEXT("always loaded"), *StreamingLevel->GetWorldAssetPackageName());

			for (AActor* Actor: SubLevel->Actors)
			{
				if (Actor)
				{
					Actor->GridPlacement = EActorGridPlacement::AlwaysLoaded;
				}
			}
		}
	}
	else
	{
		UE_LOG(LogWorldPartitionConvertCommandlet, Log, TEXT("Converting dynamic streaming level %s"), *StreamingLevel->GetWorldAssetPackageName());

		const UWorldPartitionRuntimeHash* RuntimeHash = WorldPartition->RuntimeHash;
		check(RuntimeHash);

		for (AActor* Actor : SubLevel->Actors)
		{
			if (Actor)
			{
				Actor->RuntimeGrid = RuntimeHash->GetActorRuntimeGrid(Actor);
			}
		}
	}

	return true;
}

bool UWorldPartitionConvertCommandlet::GetAdditionalLevelsToConvert(ULevel* Level, TArray<ULevel*>& SubLevels)
{
	return true;
}

bool UWorldPartitionConvertCommandlet::ShouldDeleteActor(AActor* Actor, bool bMainLevel) const
{
	if (Actor->HasAllFlags(RF_Transient) ||
		Actor->IsA<ALODActor>() ||
		Actor->IsA<ALevelBounds>() ||
		Actor->IsA<ALandscapeGizmoActor>())
	{
		return true;
	}

	if (!bMainLevel)
	{
		// Only delete these actors if they aren't in the main level
		if (Actor->IsA<ALevelScriptActor>() ||
			Actor->IsA<AWorldSettings>() ||
			Actor == (AActor*)Actor->GetLevel()->GetDefaultBrush())
		{
			return true;
		}
	}

	return false;
}

void UWorldPartitionConvertCommandlet::PerformAdditionalWorldCleanup(UWorld* World) const
{
}

void UWorldPartitionConvertCommandlet::OutputConversionReport() const
{
	UE_LOG(LogWorldPartitionConvertCommandlet, Display, TEXT("WorldPartitionConvertCommandlet report:"));

	auto OutputReport = [](const TCHAR* Msg, const TSet<FString>& Values)
	{
		if (Values.Num() != 0)
		{
			UE_LOG(LogWorldPartitionConvertCommandlet, Display, TEXT("- Found %s:"), Msg);
			TArray<FString> Array = Values.Array();
			Array.Sort();
			for (const FString& Name : Array)
			{
				UE_LOG(LogWorldPartitionConvertCommandlet, Display, TEXT("  * %s"), *Name);
			}
			UE_LOG(LogWorldPartitionConvertCommandlet, Display, TEXT(""));
		}	
	};

	OutputReport(TEXT("sublevels containing LevelScriptBPs"), MapsWithLevelScriptsBPs);
	OutputReport(TEXT("sublevels containing MapBuildData"), MapsWithMapBuildData);
	OutputReport(TEXT("actors with child actors"), ActorsWithChildActors);
	OutputReport(TEXT("group actors"), GroupActors);
	OutputReport(TEXT("actors in actor groups"), ActorsInGroupActors);
	OutputReport(TEXT("actor referencing other actors"), ActorsReferencesToActors);
}

bool LevelHasLevelScriptBlueprint(ULevel* Level)
{
	if (ULevelScriptBlueprint* LevelScriptBP = Level->GetLevelScriptBlueprint(true))
	{
		TArray<UEdGraph*> AllGraphs;
		LevelScriptBP->GetAllGraphs(AllGraphs);
		for (UEdGraph* CurrentGraph : AllGraphs)
		{
			for (UEdGraphNode* Node : CurrentGraph->Nodes)
			{
				if (!Node->IsAutomaticallyPlacedGhostNode())
				{
					return true;
				}
			}
		}
	}

	return false;
}

bool LevelHasMapBuildData(ULevel* Level)
{
	return Level->MapBuildData != nullptr;
}

void UWorldPartitionConvertCommandlet::ChangeObjectOuter(UObject* Object, UObject* NewOuter)
{
	FString OldPath = FSoftObjectPath(Object).ToString();
	Object->Rename(nullptr, NewOuter, REN_DontCreateRedirectors);
	FString NewPath = FSoftObjectPath(Object).ToString();
	RemapSoftObjectPaths.Add(OldPath, NewPath);
}

void UWorldPartitionConvertCommandlet::FixupSoftObjectPaths(UPackage* OuterPackage)
{
	UE_SCOPED_TIMER(TEXT("FixupSoftObjectPaths"), LogWorldPartitionConvertCommandlet, Display);

	struct FSoftPathFixupSerializer : public FArchiveUObject
	{
		FSoftPathFixupSerializer(TMap<FString, FString>& InRemapSoftObjectPaths)
		: RemapSoftObjectPaths(InRemapSoftObjectPaths)
		{
			this->SetIsSaving(true);
		}

		FArchive& operator<<(FSoftObjectPath& Value)
		{
			if (Value.IsNull())
			{
				return *this;
			}

			FString OriginalValue = Value.ToString();

			auto GetSourceString = [this]()
			{
				FString DebugStackString;
				for (const FName& DebugData: DebugDataStack)
				{
					DebugStackString += DebugData.ToString();
					DebugStackString += TEXT(".");
				}
				DebugStackString.RemoveFromEnd(TEXT("."));
				return DebugStackString;
			};

			if (FString* RemappedValue = RemapSoftObjectPaths.Find(OriginalValue))
			{
				Value.SetPath(*RemappedValue);
			}
			else if (Value.GetSubPathString().StartsWith(TEXT("PersistentLevel.")))
			{
				int32 DotPos = Value.GetSubPathString().Find(TEXT("."), ESearchCase::IgnoreCase, ESearchDir::FromStart);
				if (DotPos != INDEX_NONE)
				{
					RemappedValue = RemapSoftObjectPaths.Find(Value.GetAssetPathName().ToString());
					if (RemappedValue)
					{
						FString NewPath = *RemappedValue + ':' + Value.GetSubPathString();
						Value.SetPath(NewPath);
					}
				}

				FString NewValue = Value.ToString();
				if (NewValue == OriginalValue)
				{
					Value.Reset();
					UE_LOG(LogWorldPartitionConvertCommandlet, Warning, TEXT("Error remapping SoftObjectPath %s"), *OriginalValue);
					UE_LOG(LogWorldPartitionConvertCommandlet, Warning, TEXT("  Source: %s"), *GetSourceString());
				}
			}

			if (!Value.IsNull())
			{
				FString NewValue = Value.ToString();
				if (NewValue != OriginalValue)
				{
					UE_LOG(LogWorldPartitionConvertCommandlet, Verbose, TEXT("Remapped SoftObjectPath %s to %s"), *OriginalValue, *NewValue);
					UE_LOG(LogWorldPartitionConvertCommandlet, Verbose, TEXT("  Source: %s"), *GetSourceString());
				}
			}

			return *this;
		}

	private:
		virtual void PushDebugDataString(const FName& DebugData) override
		{
			DebugDataStack.Add(DebugData);
		}

		virtual void PopDebugDataString() override
		{
			DebugDataStack.Pop();
		}

		TArray<FName> DebugDataStack;
		TMap<FString, FString>& RemapSoftObjectPaths;
	};

	FSoftPathFixupSerializer FixupSerializer(RemapSoftObjectPaths);

	ForEachObjectWithPackage(OuterPackage, [&](UObject* Object)
	{
		Object->Serialize(FixupSerializer);
		return true;
	}, true, RF_NoFlags, EInternalObjectFlags::PendingKill);
}

bool UWorldPartitionConvertCommandlet::DetachDependantLevelPackages(ULevel* Level)
{
	if (Level->MapBuildData && (Level->MapBuildData->GetPackage() != Level->GetPackage()))
	{
		PackagesToDelete.Add(Level->MapBuildData->GetPackage());
		Level->MapBuildData = nullptr;
	}

	// Try to delete matching HLOD package
	FHierarchicalLODUtilitiesModule& Module = FModuleManager::LoadModuleChecked<FHierarchicalLODUtilitiesModule>("HierarchicalLODUtilities");
	IHierarchicalLODUtilities* Utilities = Module.GetUtilities();

	const int32 NumHLODLevels = Level->GetWorldSettings()->GetNumHierarchicalLODLevels();

	for (int32 HLODIndex=0; HLODIndex<NumHLODLevels; HLODIndex++)
	{
		if (UPackage* HLODPackage = Utilities->RetrieveLevelHLODPackage(Level, HLODIndex))
		{
			PackagesToDelete.Add(HLODPackage);
		}
	}

	for (AActor* Actor: Level->Actors)
	{
		if (Actor && !Actor->IsPendingKill() && Actor->IsA<ALODActor>())
		{
			Level->GetWorld()->DestroyActor(Actor);
		}
	}

	Level->GetWorldSettings()->ResetHierarchicalLODSetup();

	return true;
}

bool UWorldPartitionConvertCommandlet::RenameWorldPackageWithSuffix(UWorld* World)
{
	bool bRenamedSuccess = false;
	UPackage* Package = World->GetPackage();

	FString OldWorldName = World->GetName();
	FString NewWorldName = OldWorldName + ConversionSuffix;
	bRenamedSuccess = World->Rename(*NewWorldName, nullptr, REN_NonTransactional | REN_DontCreateRedirectors | REN_ForceNoResetLoaders);
	if (!bRenamedSuccess)
	{
		UE_LOG(LogWorldPartitionConvertCommandlet, Error, TEXT("Unable to rename world to %s"), *NewWorldName);
		return false;
	}

	FString OldPackageName = Package->GetName();
	FString NewPackageName = OldPackageName + ConversionSuffix;
	FString NewPackageFilename = Package->FileName.ToString().Replace(*OldPackageName, *NewPackageName);
	bRenamedSuccess = Package->Rename(*NewPackageName, nullptr, REN_NonTransactional | REN_DontCreateRedirectors | REN_ForceNoResetLoaders);
	if (!bRenamedSuccess)
	{
		UE_LOG(LogWorldPartitionConvertCommandlet, Error, TEXT("Unable to rename package to %s"), *NewPackageName);
		return false;
	}
	Package->FileName = *NewPackageFilename;

	return true;
}


UHLODLayer* UWorldPartitionConvertCommandlet::CreateHLODLayerFromINI(const FString& InHLODLayerName)
{
	const FString PackagePath = HLODLayerAssetsPath / InHLODLayerName;
	UPackage* AssetPackage = CreatePackage(*PackagePath);
	if (!AssetPackage)
	{
		UE_LOG(LogWorldPartitionConvertCommandlet, Error, TEXT("Package \"%s\" creation failed"), *PackagePath);
		return nullptr;
	}

	// Make sure we overwrite any existing HLODLayer asset package
	AssetPackage->MarkAsFullyLoaded();

	UHLODLayer* HLODLayer = NewObject<UHLODLayer>(AssetPackage, *InHLODLayerName, RF_Public | RF_Standalone);
	if (!HLODLayer)
	{
		UE_LOG(LogWorldPartitionConvertCommandlet, Error, TEXT("HLODLayer \"%s\" creation failed"), *InHLODLayerName);
		return nullptr;
	}

	HLODLayer->LoadConfig(nullptr, *LevelConfigFilename);

	// Notify the asset registry
	FAssetRegistryModule::AssetCreated(HLODLayer);

	// Mark the package dirty...
	HLODLayer->Modify();

	PackagesToSave.Add(HLODLayer->GetOutermost());

	return HLODLayer;
}

void UWorldPartitionConvertCommandlet::SetupHLODLayerAssets()
{
	TArray<FString> HLODLayerSectionsNames;
	if (GConfig->GetPerObjectConfigSections(LevelConfigFilename, TEXT("HLODLayer"), HLODLayerSectionsNames))
	{
		for(const FString& HLODLayerSectionName : HLODLayerSectionsNames)
		{
			FString HLODLayerName(*HLODLayerSectionName.Left(HLODLayerSectionName.Find(TEXT(" "))));
			UHLODLayer* HLODLayer = CreateHLODLayerFromINI(HLODLayerName);
			HLODLayers.Add(HLODLayerName, HLODLayer);
		}
	}

	// Assign HLOD layers to the classes listed in the level config
	for (const FHLODLayerActorMapping& Entry : HLODLayersForActorClasses)
	{
		UHLODLayer* HLODLayer = HLODLayers.FindRef(Entry.HLODLayer);
		if (!ensure(HLODLayer))
		{
			continue;
		}

		// Load the BP class & assign 
		if (UClass* LoadedObject = Entry.ActorClass.LoadSynchronous())
		{
			if (AActor* CDO = CastChecked<AActor>(LoadedObject->GetDefaultObject()))
			{
				if (CDO->GetHLODLayer() != HLODLayer)
				{
					CDO->SetHLODLayer(HLODLayer);
					CDO->MarkPackageDirty();
					PackagesToSave.Add(CDO->GetPackage());
				}
			}
		}
	}
}

void UWorldPartitionConvertCommandlet::SetActorGuid(AActor* Actor, const FGuid& NewGuid)
{
	FSetActorGuid SetActorGuid(Actor, NewGuid);
}

void UWorldPartitionConvertCommandlet::OnWorldLoaded(UWorld* World)
{
	if (UWorldComposition* WorldComposition = World->WorldComposition)
	{
		// Add tiles streaming levels to world
		World->SetStreamingLevels(WorldComposition->TilesStreaming);

		// Make sure to force bDisableDistanceStreaming on streaming levels of World Composition non distance dependent tiles (for the rest of the process to handle streaming level as always loaded)
		UWorldComposition::FTilesList& Tiles = WorldComposition->GetTilesList();
		for (int32 TileIdx = 0; TileIdx < Tiles.Num(); TileIdx++)
		{
			FWorldCompositionTile& Tile = Tiles[TileIdx];
			ULevelStreaming* StreamingLevel = WorldComposition->TilesStreaming[TileIdx];
			if (StreamingLevel && !WorldComposition->IsDistanceDependentLevel(Tile.PackageName))
			{
				StreamingLevel->bDisableDistanceStreaming = true;
			}
		}
	}
}

void UWorldPartitionConvertCommandlet::CreateWorldMiniMapTexture(UWorld* World)
{
	AWorldPartitionMiniMap* WorldMiniMap = FWorldPartitionMiniMapHelper::GetWorldPartitionMiniMap(World,true);
	if (!WorldMiniMap)
	{
		UE_LOG(LogWorldPartitionConvertCommandlet, Error, TEXT("Failed to create Minimap. WorldPartitionMiniMap actor not found in the persistent level."));
		return;
	}

	FWorldPartitionMiniMapHelper::CaptureWorldMiniMapToTexture(World, WorldMiniMap, WorldMiniMap->MiniMapSize, WorldMiniMap->MiniMapTexture, WorldMiniMap->MiniMapWorldBounds);
}

int32 UWorldPartitionConvertCommandlet::Main(const FString& Params)
{
	UE_SCOPED_TIMER(TEXT("Conversion"), LogWorldPartitionConvertCommandlet, Display);

	TArray<FString> Tokens, Switches;
	ParseCommandLine(*Params, Tokens, Switches);

	if (Tokens.Num() != 1)
	{
		UE_LOG(LogWorldPartitionConvertCommandlet, Error, TEXT("ConvertToPartitionedLevel bad parameters"));
		return 1;
	}

	// This will convert incomplete package name to a fully qualified path, avoiding calling it several times (takes ~50s)
	if (!FPackageName::SearchForPackageOnDisk(Tokens[0], &Tokens[0]))
	{
		UE_LOG(LogWorldPartitionConvertCommandlet, Error, TEXT("Unknown level '%s'"), *Tokens[0]);
		return 1;
	}

	bDeleteSourceLevels = Switches.Contains(TEXT("DeleteSourceLevels"));
	bGenerateIni = Switches.Contains(TEXT("GenerateIni"));
	bReportOnly = bGenerateIni || Switches.Contains(TEXT("ReportOnly"));
	bVerbose = Switches.Contains(TEXT("Verbose"));
	bAllowCommandletRendering = Switches.Contains(TEXT("AllowCommandletRendering"));

	ReadAdditionalTokensAndSwitches(Tokens, Switches);

	if (bVerbose)
	{
		LogWorldPartitionConvertCommandlet.SetVerbosity(ELogVerbosity::Verbose);
	}

	bConversionSuffix = Switches.Contains(TEXT("ConversionSuffix"));

	// Load configuration file
	FString LevelLongPackageName;
	if (FPackageName::SearchForPackageOnDisk(Tokens[0], nullptr, &LevelLongPackageName))
	{
		LevelConfigFilename = FPaths::ChangeExtension(LevelLongPackageName, TEXT("ini"));

		if (FPlatformFileManager::Get().GetPlatformFile().FileExists(*LevelConfigFilename))
		{
			LoadConfig(GetClass(), *LevelConfigFilename);
		}
		else
		{
			EditorHashClass = FindObject<UClass>(ANY_PACKAGE, TEXT("WorldPartitionEditorSpatialHash"));
			RuntimeHashClass = FindObject<UClass>(ANY_PACKAGE, TEXT("WorldPartitionRuntimeSpatialHash"));
		}
	}

	if (!EditorHashClass)
	{
		UE_LOG(LogWorldPartitionConvertCommandlet, Error, TEXT("Missing or invalid editor hash class"));
		return 1;
	}

	if (!RuntimeHashClass)
	{
		UE_LOG(LogWorldPartitionConvertCommandlet, Error, TEXT("Missing or invalid runtime hash class"));
		return 1;
	}

	SetupHLODLayerAssets();

	// Delete existing result from running the commandlet, even if not using the suffix mode to cleanup previous conversion
	if (!bReportOnly)
	{
		UE_SCOPED_TIMER(TEXT("Deleting existing conversion results"), LogWorldPartitionConvertCommandlet, Display);

		FString OldLevelName = Tokens[0] + ConversionSuffix;
		FString ExternalActorsPath = ULevel::GetExternalActorsPath(OldLevelName);
		FString ExternalActorsFilePath = FPackageName::LongPackageNameToFilename(ExternalActorsPath);

		if (IFileManager::Get().DirectoryExists(*ExternalActorsFilePath))
		{
			bool bResult = IFileManager::Get().IterateDirectoryRecursively(*ExternalActorsFilePath, [this](const TCHAR* FilenameOrDirectory, bool bIsDirectory)
			{
				if (!bIsDirectory)
				{
					FString Filename(FilenameOrDirectory);
					if (Filename.EndsWith(FPackageName::GetAssetPackageExtension()))
					{
						return PackageHelper.Delete(Filename);
					}
				}
				return true;
			});

			if (!bResult)
			{
				UE_LOG(LogWorldPartitionConvertCommandlet, Error, TEXT("Failed to delete external actor package(s)"));
				return 1;
			}
		}

		if (FPackageName::SearchForPackageOnDisk(OldLevelName, &OldLevelName))
		{
			if (!PackageHelper.Delete(OldLevelName))
			{
				UE_LOG(LogWorldPartitionConvertCommandlet, Error, TEXT("Failed to delete previously converted level '%s'"), *Tokens[0]);
				return 1;
			}
		}
	}

	// Load world
	UWorld* MainWorld = LoadWorld(Tokens[0]);
	if (!MainWorld)
	{
		UE_LOG(LogWorldPartitionConvertCommandlet, Error, TEXT("Unknown world '%s'"), *Tokens[0]);
		return 1;
	}

	// Make sure the world isn't already partitionned
	AWorldSettings* MainWorldSettings = MainWorld->GetWorldSettings();
	if (MainWorldSettings->GetWorldPartition())
	{
		UE_LOG(LogWorldPartitionConvertCommandlet, Error, TEXT("Level '%s' is already partitionned"), *Tokens[0]);
		return 1;
	}

	// Setup the world partition object
	UWorldPartition* WorldPartition = CreateWorldPartition(MainWorldSettings, MainWorld->WorldComposition);
	if (!WorldPartition)
	{
		return 1;
	}

	// Initialize the world, create subsystems, etc.
	ULevel* MainLevel = InitWorld(MainWorld);
	if (!MainLevel)
	{
		UE_LOG(LogWorldPartitionConvertCommandlet, Error, TEXT("Unknown level '%s'"), *Tokens[0]);
		return 1;
	}

	UPackage* MainPackage = MainLevel->GetPackage();
	AWorldDataLayers* MainWorldDataLayers = AWorldDataLayers::Get(MainWorld, /*bCreateIfNotFound*/true);

	OnWorldLoaded(MainWorld);

	UActorPartitionSubsystem* ActorPartitionSubsystem = MainWorld->GetSubsystem<UActorPartitionSubsystem>();
	auto PartitionFoliage = [this, MainWorld, ActorPartitionSubsystem](AInstancedFoliageActor* IFA)
	{
		for (auto& Pair : IFA->FoliageInfos)
		{
			FFoliageInfo* FoliageInfo = &Pair.Value.Get();
			if (FoliageInfo)
			{
				if (FoliageInfo->Type == EFoliageImplType::Actor)
				{
					// We don't support Actor Foliage in WP
					FoliageInfo->ExcludeActors();
				}
				else if (FoliageInfo->Instances.Num())
				{
					FBox InstanceBounds = FoliageInfo->GetApproximatedInstanceBounds();
					FActorPartitionGridHelper::ForEachIntersectingCell(AInstancedFoliageActor::StaticClass(), InstanceBounds, MainWorld->PersistentLevel, [IFA, FoliageInfo, ActorPartitionSubsystem](const UActorPartitionSubsystem::FCellCoord& InCellCoord, const FBox& InCellBounds)
					{
						TArray<int32> Indices;
						FoliageInfo->GetInstancesInsideBounds(InCellBounds, Indices);
						if (Indices.Num())
						{
							AInstancedFoliageActor* CellIFA = Cast<AInstancedFoliageActor>(ActorPartitionSubsystem->GetActor(AInstancedFoliageActor::StaticClass(), InCellCoord, true));
							check(CellIFA);
							FoliageInfo->MoveInstances(IFA, CellIFA, TSet<int32>(Indices), false);
						}

						return true;
					});
					check(!FoliageInfo->Instances.Num());
				}
			}
		}

		IFA->GetLevel()->GetWorld()->DestroyActor(IFA);
	};

	auto PartitionLandscape = [this, MainWorld](ULandscapeInfo* LandscapeInfo)
	{
		// Handle Landscapes with missing LandscapeActor(s)
		if (!LandscapeInfo->LandscapeActor.Get())
		{
			// Use the first proxy as the landscape template
			ALandscapeProxy* FirstProxy = LandscapeInfo->Proxies[0];

			FActorSpawnParameters SpawnParams;
			FTransform LandscapeTransform = FirstProxy->LandscapeActorToWorld();
			ALandscape* NewLandscape = MainWorld->SpawnActor<ALandscape>(ALandscape::StaticClass(), LandscapeTransform, SpawnParams);
			
			NewLandscape->GetSharedProperties(FirstProxy);

			LandscapeInfo->RegisterActor(NewLandscape);
		}

		TSet<AActor*> NewSplineActors;
						
		auto MoveControlPointToNewSplineActor = [&NewSplineActors, LandscapeInfo](ULandscapeSplineControlPoint* ControlPoint)
		{
			AActor* CurrentOwner = ControlPoint->GetTypedOuter<AActor>();
			// Control point as already been moved through its connected segments
			if (NewSplineActors.Contains(CurrentOwner))
			{
				return;
			}
			
			const FTransform LocalToWorld = ControlPoint->GetOuterULandscapeSplinesComponent()->GetComponentTransform();
			const FVector NewActorLocation = LocalToWorld.TransformPosition(ControlPoint->Location);
						
			ALandscapeSplineActor* NewSplineActor = LandscapeInfo->CreateSplineActor(NewActorLocation);

			// ULandscapeSplinesComponent doesn't assign SplineEditorMesh when IsCommandlet() is true.
			NewSplineActor->GetSplinesComponent()->SetDefaultEditorSplineMesh();

			NewSplineActors.Add(NewSplineActor);
			LandscapeInfo->MoveSpline(ControlPoint, NewSplineActor);
		};
				
		// Iterate on copy since we are creating new spline actors
		TArray<TScriptInterface<ILandscapeSplineInterface>> OldSplineActors(LandscapeInfo->GetSplineActors());
		for (TScriptInterface<ILandscapeSplineInterface> PreviousSplineActor : OldSplineActors)
		{
			if (ULandscapeSplinesComponent* SplineComponent = PreviousSplineActor->GetSplinesComponent())
			{
				SplineComponent->ForEachControlPoint(MoveControlPointToNewSplineActor);
			}
		}

		TSet<AActor*> ActorsToDelete;
		FLandscapeConfigHelper::ChangeGridSize(LandscapeInfo, LandscapeGridSize, ActorsToDelete);
		for (AActor* ActorToDelete : ActorsToDelete)
		{
			MainWorld->DestroyActor(ActorToDelete);
		}
	};

	auto PrepareLevelActors = [this, PartitionFoliage, PartitionLandscape, MainWorldDataLayers](ULevel* Level, bool bMainLevel, EActorGridPlacement DefaultGridPlacement)
	{
		const FBox WorldBounds(WorldOrigin - WorldExtent, WorldOrigin + WorldExtent);

		TArray<AInstancedFoliageActor*> IFAs;
		TSet<ULandscapeInfo*> LandscapeInfos;
		for (AActor* Actor: Level->Actors)
		{
			if (Actor && !Actor->IsPendingKill())
			{
				if (ShouldDeleteActor(Actor, bMainLevel))
				{
					Level->GetWorld()->DestroyActor(Actor);
				}
				else 
				{
					if (AInstancedFoliageActor* IFA = Cast<AInstancedFoliageActor>(Actor))
					{
						IFAs.Add(IFA);
					}
					else if (ALandscapeProxy* LandscapeProxy = Cast<ALandscapeProxy>(Actor))
					{
						ULandscapeInfo* LandscapeInfo = LandscapeProxy->GetLandscapeInfo();
						check(LandscapeInfo);
						LandscapeInfos.Add(LandscapeInfo);
					}
					// Only override default grid placement on actors that are not marked as always loaded
					else if (Actor->GridPlacement != EActorGridPlacement::AlwaysLoaded)
					{
						FVector BoundsLocation;
						FVector BoundsExtent;
						Actor->GetActorLocationBounds(/*bOnlyCollidingComponents*/false, BoundsLocation, BoundsExtent, /*bIncludeFromChildActors*/true);

						const FBox ActorBounds(BoundsLocation - BoundsExtent, BoundsLocation + BoundsExtent);

						if (!WorldBounds.IsInside(ActorBounds))
						{
							Actor->GridPlacement = EActorGridPlacement::AlwaysLoaded;
						}
						else
						{
							Actor->GridPlacement = DefaultGridPlacement;
						}
					}

					// Convert Layers into DataLayers with DynamicallyLoaded flag disabled
					if (Actor->IsValidForDataLayer())
					{
						for (FName Layer : Actor->Layers)
						{
							UDataLayer* DataLayer = const_cast<UDataLayer*>(MainWorldDataLayers->GetDataLayerFromLabel(Layer));
							if (!DataLayer)
							{
								DataLayer = MainWorldDataLayers->CreateDataLayer();
								DataLayer->SetDataLayerLabel(Layer);
								DataLayer->SetIsDynamicallyLoaded(false);
							}
							Actor->AddDataLayer(DataLayer);
						}
					}
					// Clear actor layers as they are not supported yet in world partition
					Actor->Layers.Empty();
				}
			}
		}

		// do loop after as it may modify Level->Actors
		if (IFAs.Num())
		{
			UE_SCOPED_TIMER(TEXT("PartitionFoliage"), LogWorldPartitionConvertCommandlet, Display);

			for (AInstancedFoliageActor* IFA : IFAs)
			{
				PartitionFoliage(IFA);
			}
		}

		if (LandscapeInfos.Num())
		{
			UE_SCOPED_TIMER(TEXT("PartitionLandscape"), LogWorldPartitionConvertCommandlet, Display);

			for (ULandscapeInfo* LandscapeInfo : LandscapeInfos)
			{
				PartitionLandscape(LandscapeInfo);
			}
		}
	};

	// Gather and load sublevels
	TArray<ULevel*> SubLevelsToConvert;
	GatherAndPrepareSubLevelsToConvert(WorldPartition, MainLevel, SubLevelsToConvert);

	if (!GetAdditionalLevelsToConvert(MainLevel, SubLevelsToConvert))
	{
		return 1;
	}

	// Validate levels for conversion
	bool bSkipStableGUIDValidation = Switches.Contains(TEXT("SkipStableGUIDValidation"));
	if (!bSkipStableGUIDValidation)
	{
		bool bNeedsResaveSubLevels = false;

		for (ULevel* Level: SubLevelsToConvert)
		{
			if (!Level->bContainsStableActorGUIDs)
			{
				bNeedsResaveSubLevels |= true;
				UE_LOG(LogWorldPartitionConvertCommandlet, Error, TEXT("Unable to convert level '%s' with non-stable actor GUIDs. Resave the level before converting."), *Level->GetPackage()->GetName());
			}
		}

		if (bNeedsResaveSubLevels)
		{
			return 1;
		}
	}

	// Prepare levels for conversion
	DetachDependantLevelPackages(MainLevel);
	PrepareLevelActors(MainLevel, true, GetLevelGridPlacement(MainLevel, SubLevelsToConvert.Num() ? EActorGridPlacement::AlwaysLoaded : EActorGridPlacement::Bounds));

	if (bConversionSuffix)
	{
		FString OldMainWorldPath = FSoftObjectPath(MainWorld).ToString();
		FString OldMainLevelPath = FSoftObjectPath(MainLevel).ToString();
		FString OldPackagePath = FSoftObjectPath(MainPackage).ToString();

		if (!RenameWorldPackageWithSuffix(MainWorld))
		{
			return 1;
		}

		RemapSoftObjectPaths.Add(OldMainWorldPath, FSoftObjectPath(MainWorld).ToString());
		RemapSoftObjectPaths.Add(OldMainLevelPath, FSoftObjectPath(MainLevel).ToString());
		RemapSoftObjectPaths.Add(OldPackagePath, FSoftObjectPath(MainPackage).ToString());
	}

	for (ULevel* SubLevel : SubLevelsToConvert)
	{
		DetachDependantLevelPackages(SubLevel);
		PrepareLevelActors(SubLevel, false, GetLevelGridPlacement(SubLevel, EActorGridPlacement::Bounds));
	}

	TMap<UObject*, UObject*> PrivateRefsMap;
	for(ULevel* SubLevel : SubLevelsToConvert)
	{
		UWorld* SubWorld = SubLevel->GetTypedOuter<UWorld>();
		UPackage* SubPackage = SubLevel->GetPackage();

		RemapSoftObjectPaths.Add(FSoftObjectPath(SubWorld).ToString(), FSoftObjectPath(MainWorld).ToString());
		RemapSoftObjectPaths.Add(FSoftObjectPath(SubLevel).ToString(), FSoftObjectPath(MainLevel).ToString());
		RemapSoftObjectPaths.Add(FSoftObjectPath(SubPackage).ToString(), FSoftObjectPath(MainPackage).ToString());

		if (LevelHasLevelScriptBlueprint(SubLevel))
		{
			MapsWithLevelScriptsBPs.Add(SubPackage->FileName.ToString());
		}

		if (LevelHasMapBuildData(SubLevel))
		{
			MapsWithMapBuildData.Add(SubPackage->FileName.ToString());
		}

		UE_LOG(LogWorldPartitionConvertCommandlet, Log, TEXT("Converting %s"), *SubWorld->GetName());

		for(AActor* Actor: SubLevel->Actors)
		{
			if(Actor && !Actor->IsPendingKill())
			{
				check(Actor->GetOuter() == SubLevel);
				check(!ShouldDeleteActor(Actor, false));
				
				if (Actor->IsA(AGroupActor::StaticClass()))
				{
					GroupActors.Add(*Actor->GetFullName());
				}

				if (Actor->GroupActor)
				{
					ActorsInGroupActors.Add(*Actor->GetFullName());
				}

				TArray<AActor*> ChildActors;
				Actor->GetAllChildActors(ChildActors, false);

				if (ChildActors.Num())
				{
					ActorsWithChildActors.Add(*Actor->GetFullName());
				}

				FArchiveGatherPrivateImports Ar(Actor, PrivateRefsMap, ActorsReferencesToActors);
				Actor->Serialize(Ar);

				// Even after Foliage Partitioning it is possible some Actors still have a FoliageTag. Make sure it is removed.
				if (FFoliageHelper::IsOwnedByFoliage(Actor))
				{
					FFoliageHelper::SetIsOwnedByFoliage(Actor, false);
				}

				ChangeObjectOuter(Actor, MainLevel);

				// Migrate blueprint classes
				UClass* ActorClass = Actor->GetClass();
				if (!ActorClass->IsNative() && (ActorClass->GetPackage() == SubPackage))
				{
					ChangeObjectOuter(ActorClass, MainPackage);
					UE_LOG(LogWorldPartitionConvertCommandlet, Log, TEXT("Extracted non-native class %s"), *ActorClass->GetName());
				}
			}
		}

		if (!bReportOnly)
		{
			TArray<UObject*> ObjectsToRename;
			ForEachObjectWithPackage(SubPackage, [&](UObject* Object)
			{
				if(!Object->IsA<AActor>() && !Object->IsA<ULevel>() && !Object->IsA<UWorld>() && !Object->IsA<UMetaData>())
				{
					ObjectsToRename.Add(Object);
				}
				return true;
			}, /*bIncludeNestedObjects*/false);

			for(UObject* ObjectToRename: ObjectsToRename)
			{
				ChangeObjectOuter(ObjectToRename, MainPackage);
				UE_LOG(LogWorldPartitionConvertCommandlet, Warning, TEXT("Renamed orphan object %s"), *ObjectToRename->GetName());
			}

			PackagesToDelete.Add(SubLevel->GetPackage());
		}
	}

	// Clear streaming levels
	for (ULevelStreaming* LevelStreaming: MainLevel->GetWorld()->GetStreamingLevels())
	{
		LevelStreaming->MarkPendingKill();
	}
	MainLevel->GetWorld()->ClearStreamingLevels();

	// Fixup SoftObjectPaths
	FixupSoftObjectPaths(MainPackage);

	PerformAdditionalWorldCleanup(MainWorld);

	bool bForceInitializeWorld = false;
	bool bInitializedPhysicsSceneForSave = GEditor->InitializePhysicsSceneForSaveIfNecessary(MainWorld, bForceInitializeWorld);

	//Create MiniMap of World
	bool bSkipMiniMapGeneration = Switches.Contains(TEXT("SkipMiniMapGeneration"));
	if (!bSkipMiniMapGeneration)
	{
		if (!bAllowCommandletRendering)
		{
			UE_LOG(LogWorldPartitionConvertCommandlet, Error, TEXT("AllowCommandletRendering option is required to generate MiniMap. Use SkipMiniMapGeneration option to skip MiniMap generation."));
			return 1;
		}
		CreateWorldMiniMapTexture(MainWorld);
	}

	// After conversion, convert actors to external actors
	UPackage* LevelPackage = MainLevel->GetPackage();

	TArray<AActor*> ActorList;
	TArray<AActor*> ChildActorList;
	ActorList.Reserve(MainLevel->Actors.Num());

	// Move child actors at the end of the list
	for (AActor* Actor: MainLevel->Actors)
	{
		if (Actor && !Actor->IsPendingKill())
		{
			check(Actor->GetLevel() == MainLevel);
			check(Actor->GetActorGuid().IsValid());

			if (Actor->GetParentActor())
			{
				ChildActorList.Add(Actor);
			}
			else
			{
				ActorList.Add(Actor);
			}
		}
	}

	ActorList.Append(ChildActorList);
	ChildActorList.Empty();

	WorldPartition->AddToRoot();

	if (!bReportOnly)
	{
		TSet<FGuid> ActorGuids;
		
		for(AActor* Actor: ActorList)
		{
			if (!Actor || Actor->IsPendingKill() || !Actor->SupportsExternalPackaging())
			{
				continue;
			}

			bool bAlreadySet = false;
			ActorGuids.Add(Actor->GetActorGuid(), &bAlreadySet);
			if (bAlreadySet)
			{
				UE_LOG(LogWorldPartitionConvertCommandlet, Error, TEXT("Duplicated guid actor %s(guid:%s) can't extract actor"), *Actor->GetName(), *Actor->GetActorGuid().ToString(EGuidFormats::Digits));
				return 1;
			}

			if (Actor->IsPackageExternal())
			{
				PackagesToDelete.Add(Actor->GetPackage());
				Actor->SetPackageExternal(false);
			}
						
			Actor->SetPackageExternal(true);
			
			UPackage* ActorPackage = Actor->GetExternalPackage();
			PackagesToSave.Add(ActorPackage);

			UE_LOG(LogWorldPartitionConvertCommandlet, Log, TEXT("Extracted actor %s(guid:%s) in %s"), *Actor->GetName(), *Actor->GetActorGuid().ToString(EGuidFormats::Digits), *ActorPackage->GetName());
		}

		// Required to clear any deleted actors from the level
		CollectGarbage(RF_Standalone);

		MainLevel->bUseExternalActors = true;
		MainWorld->WorldComposition = nullptr;
		MainLevel->bIsPartitioned = true;

		if (bDeleteSourceLevels)
		{
			for (UPackage* Package : PackagesToDelete)
			{
				if (!PackageHelper.Delete(Package))
				{
					return 1;
				}
			}
		}

		UE_LOG(LogWorldPartitionConvertCommandlet, Log, TEXT("Checking out %d actor packages."), PackagesToSave.Num());
		for(UPackage* Package: PackagesToSave)
		{
			if (!PackageHelper.Checkout(Package))
			{
				return 1;
			}
		}

		for (TMap<UObject*, UObject*>::TConstIterator It(PrivateRefsMap); It; ++It)
		{
			SET_WARN_COLOR(COLOR_YELLOW);
			UE_LOG(LogWorldPartitionConvertCommandlet, Warning, TEXT("Renaming %s from %s to %s"), *It->Key->GetName(), *It->Key->GetPackage()->GetName(), *It->Value->GetPackage()->GetName());
			CLEAR_WARN_COLOR();

			It->Key->SetExternalPackage(It->Value->GetPackage());
		}
	
		// Save packages
		UE_LOG(LogWorldPartitionConvertCommandlet, Log, TEXT("Saving %d packages."), PackagesToSave.Num());
		for (UPackage* PackageToSave : PackagesToSave)
		{
			FString PackageFileName = SourceControlHelpers::PackageFilename(PackageToSave);
			if (!UPackage::SavePackage(PackageToSave, nullptr, RF_Standalone, *PackageFileName, GError, nullptr, false, true, SAVE_Async))
			{
				return 1;
			}
		}

		// Add new packages to source control
		for(UPackage* PackageToSave: PackagesToSave)
		{
			if(!PackageHelper.AddToSourceControl(PackageToSave))
			{
				return 1;
			}
		}

		// Checkout level
		UE_LOG(LogWorldPartitionConvertCommandlet, Log, TEXT("Saving %s."), *MainPackage->GetName());

		if (!PackageHelper.Checkout(MainPackage))
		{
			return 1;
		}

		// Save level
		FString PackageFileName = SourceControlHelpers::PackageFilename(MainPackage);
		if (!UPackage::SavePackage(MainPackage, nullptr, RF_Standalone, *PackageFileName, GError, nullptr, false, true, SAVE_Async))
		{
			return 1;
		}

		if(bInitializedPhysicsSceneForSave)
		{
			GEditor->CleanupPhysicsSceneThatWasInitializedForSave(MainWorld, bForceInitializeWorld);
		}

		UPackage::WaitForAsyncFileWrites();

		UE_LOG(LogWorldPartitionConvertCommandlet, Log, TEXT("######## CONVERSION COMPLETED SUCCESSFULLY ########"));
	}

	if (bGenerateIni || !bReportOnly)
	{
		if (bGenerateIni || !FPlatformFileManager::Get().GetPlatformFile().FileExists(*LevelConfigFilename))
		{
			SaveConfig(CPF_Config, *LevelConfigFilename);
			WorldPartition->EditorHash->SaveConfig(CPF_Config, *LevelConfigFilename);
			WorldPartition->RuntimeHash->SaveConfig(CPF_Config, *LevelConfigFilename);
			
			for(const auto& Pair : HLODLayers)
			{
				Pair.Value->SaveConfig(CPF_Config, *LevelConfigFilename);
			}
		}
	}

	UPackage::WaitForAsyncFileWrites();

	OutputConversionReport();

	return 0;
}
