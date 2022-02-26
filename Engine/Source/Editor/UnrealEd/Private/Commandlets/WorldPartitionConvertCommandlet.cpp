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
#include "Engine/Public/ActorReferencesUtils.h"
#include "WorldPartition/WorldPartition.h"
#include "WorldPartition/WorldPartitionSubsystem.h"
#include "WorldPartition/HLOD/HLODActor.h"
#include "WorldPartition/WorldPartitionMiniMap.h"
#include "WorldPartition/WorldPartitionMiniMapHelper.h"
#include "LevelInstance/LevelInstanceActor.h"
#include "GameFramework/WorldSettings.h"
#include "UObject/UObjectHash.h"
#include "PackageHelperFunctions.h"
#include "UObject/MetaData.h"
#include "UObject/SavePackage.h"
#include "Editor.h"
#include "HLOD/HLODEngineSubsystem.h"
#include "HierarchicalLOD.h"
#include "IHierarchicalLODUtilities.h"
#include "HierarchicalLODUtilitiesModule.h"
#include "InstancedFoliageActor.h"
#include "Engine/LevelScriptBlueprint.h"
#include "Editor/GroupActor.h"
#include "EdGraph/EdGraph.h"
#include "ProfilingDebugging/ScopedTimers.h"
#include "AssetRegistryModule.h"
#include "FoliageEditUtility.h"
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
#include "ActorFolder.h"

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
	, bConvertActorsNotReferencedByLevelScript(true)
	, WorldOrigin(FVector::ZeroVector)
	, WorldExtent(WORLDPARTITION_MAX * 0.5)
	, LandscapeGridSize(4)
{}

UWorld* UWorldPartitionConvertCommandlet::LoadWorld(const FString& LevelToLoad)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UWorldPartitionConvertCommandlet::LoadWorld);

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
	TRACE_CPUPROFILER_EVENT_SCOPE(UWorldPartitionConvertCommandlet::InitWorld);

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

UWorldPartition* UWorldPartitionConvertCommandlet::CreateWorldPartition(AWorldSettings* MainWorldSettings)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UWorldPartitionConvertCommandlet::CreateWorldPartition);

	UWorldPartition* WorldPartition = UWorldPartition::CreateOrRepairWorldPartition(MainWorldSettings, EditorHashClass, RuntimeHashClass);

	if (bDisableStreaming)
	{
		WorldPartition->bEnableStreaming = false;
		WorldPartition->bStreamingWasEnabled = false;
	}
		
	// Read the conversion config file
	if (FPlatformFileManager::Get().GetPlatformFile().FileExists(*LevelConfigFilename))
	{
		WorldPartition->EditorHash->LoadConfig(*EditorHashClass, *LevelConfigFilename);
		WorldPartition->RuntimeHash->LoadConfig(*RuntimeHashClass, *LevelConfigFilename);
		WorldPartition->DefaultHLODLayer = HLODLayers.FindRef(DefaultHLODLayerName);
	}

	if ((WorldPartition->DefaultHLODLayer == UHLODLayer::GetEngineDefaultHLODLayersSetup()) && !bDisableStreaming)
	{
		WorldPartition->DefaultHLODLayer = UHLODLayer::DuplicateHLODLayersSetup(UHLODLayer::GetEngineDefaultHLODLayersSetup(), WorldPartition->GetPackage()->GetName(), WorldPartition->GetWorld()->GetName());

		UHLODLayer* CurrentHLODLayer = WorldPartition->DefaultHLODLayer;
		while (CurrentHLODLayer)
		{
			PackagesToSave.Add(CurrentHLODLayer->GetPackage());
			CurrentHLODLayer = Cast<UHLODLayer>(CurrentHLODLayer->GetParentLayer().Get());
		}
	}

	WorldPartition->EditorHash->Initialize();
	
	return WorldPartition;
}

void UWorldPartitionConvertCommandlet::GatherAndPrepareSubLevelsToConvert(ULevel* Level, TArray<ULevel*>& SubLevels)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UWorldPartitionConvertCommandlet::GatherAndPrepareSubLevelsToConvert);

	UWorld* World = Level->GetTypedOuter<UWorld>();	

	// Set all streaming levels to be loaded/visible for next Flush
	TArray<ULevelStreaming*> StreamingLevels;
	for (ULevelStreaming* StreamingLevel : World->GetStreamingLevels())
	{
		if (ShouldConvertStreamingLevel(StreamingLevel))
		{
			StreamingLevels.Add(StreamingLevel);
			StreamingLevel->SetShouldBeLoaded(true);
			StreamingLevel->SetShouldBeVisible(true);
			StreamingLevel->SetShouldBeVisibleInEditor(true);
		}
		else
		{
			UE_LOG(LogWorldPartitionConvertCommandlet, Log, TEXT("Skipping conversion of streaming Level %s"), *StreamingLevel->GetWorldAssetPackageName());
		}
	}

	World->FlushLevelStreaming(EFlushLevelStreamingType::Full);
	
	for(ULevelStreaming* StreamingLevel: StreamingLevels)
	{
		if (PrepareStreamingLevelForConversion(StreamingLevel))
		{
			ULevel* SubLevel = StreamingLevel->GetLoadedLevel();
			check(SubLevel);

			SubLevels.Add(SubLevel);

			// Recursively obtain sub levels to convert
			GatherAndPrepareSubLevelsToConvert(SubLevel, SubLevels);
		}
	}
}

bool UWorldPartitionConvertCommandlet::PrepareStreamingLevelForConversion(ULevelStreaming* StreamingLevel)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UWorldPartitionConvertCommandlet::PrepareStreamingLevelForConversion);

	ULevel* SubLevel = StreamingLevel->GetLoadedLevel();
	check(SubLevel);

	if (bOnlyMergeSubLevels || StreamingLevel->ShouldBeAlwaysLoaded() || StreamingLevel->bDisableDistanceStreaming)
	{
		FString WorldPath = SubLevel->GetPackage()->GetName();
		UE_LOG(LogWorldPartitionConvertCommandlet, Log, TEXT("Converting %s streaming level %s"), StreamingLevel->bDisableDistanceStreaming ? TEXT("non distance-based") : TEXT("always loaded"), *StreamingLevel->GetWorldAssetPackageName());

		for (AActor* Actor: SubLevel->Actors)
		{
			if (Actor && Actor->CanChangeIsSpatiallyLoadedFlag())
			{
				Actor->SetIsSpatiallyLoaded(false);
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
	// We need to migrate transient actors as Fortnite uses a transient actor(AFortTimeOfDayManager) to handle lighting in maps and is required during the generation of MiniMap. 
	if (Actor->IsA<ALODActor>() ||
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
	TRACE_CPUPROFILER_EVENT_SCOPE(UWorldPartitionConvertCommandlet::FixupSoftObjectPaths);

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
		if (Object->HasAllFlags(RF_WasLoaded))
		{
			Object->Serialize(FixupSerializer);
		}
		return true;
	}, true, RF_NoFlags, EInternalObjectFlags::Garbage);
}

bool UWorldPartitionConvertCommandlet::DetachDependantLevelPackages(ULevel* Level)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UWorldPartitionConvertCommandlet::DetachDependantLevelPackages);

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
		if (Actor && IsValidChecked(Actor) && Actor->IsA<ALODActor>())
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
	FString NewPackageResourceName = Package->GetLoadedPath().GetPackageName().Replace(*OldPackageName, *NewPackageName);
	bRenamedSuccess = Package->Rename(*NewPackageName, nullptr, REN_NonTransactional | REN_DontCreateRedirectors | REN_ForceNoResetLoaders);
	if (!bRenamedSuccess)
	{
		UE_LOG(LogWorldPartitionConvertCommandlet, Error, TEXT("Unable to rename package to %s"), *NewPackageName);
		return false;
	}
	Package->SetLoadedPath(FPackagePath::FromPackageNameChecked(NewPackageResourceName));

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

void UWorldPartitionConvertCommandlet::SetupHLOD()
{
	// No need to spawn HLOD actors during the conversion
	GEngine->GetEngineSubsystem<UHLODEngineSubsystem>()->DisableHLODSpawningOnLoad(true);

	SetupHLODLayerAssets();
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

int32 UWorldPartitionConvertCommandlet::Main(const FString& Params)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UWorldPartitionConvertCommandlet::Main);

	UE_SCOPED_TIMER(TEXT("Conversion"), LogWorldPartitionConvertCommandlet, Display);

	TArray<FString> Tokens, Switches;
	TMap<FString, FString> Arguments;
	ParseCommandLine(*Params, Tokens, Switches, Arguments);

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

	bOnlyMergeSubLevels = Switches.Contains(TEXT("OnlyMergeSubLevels"));
	bDeleteSourceLevels = Switches.Contains(TEXT("DeleteSourceLevels"));
	bGenerateIni = Switches.Contains(TEXT("GenerateIni"));
	bReportOnly = bGenerateIni || Switches.Contains(TEXT("ReportOnly"));
	bVerbose = Switches.Contains(TEXT("Verbose"));
	bDisableStreaming = Switches.Contains(TEXT("DisableStreaming"));
	ConversionSuffix = GetConversionSuffix(bOnlyMergeSubLevels);

	FString* FoliageTypePathValue = Arguments.Find(TEXT("FoliageTypePath"));
	
	if (FoliageTypePathValue != nullptr)
	{
		FoliageTypePath = *FoliageTypePathValue;
	}

	if (!Switches.Contains(TEXT("AllowCommandletRendering")))
	{
		UE_LOG(LogWorldPartitionConvertCommandlet, Error, TEXT("The option \"-AllowCommandletRendering\" is required."));
		return 1;
	}

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

	SetupHLOD();

	// Delete existing result from running the commandlet, even if not using the suffix mode to cleanup previous conversion
	if (!bReportOnly)
	{
		UE_SCOPED_TIMER(TEXT("Deleting existing conversion results"), LogWorldPartitionConvertCommandlet, Display);

		FString OldLevelName = Tokens[0] + ConversionSuffix;
		TArray<FString> ExternalObjectsPaths = ULevel::GetExternalObjectsPaths(OldLevelName);
		for (const FString& ExternalObjectsPath : ExternalObjectsPaths)
		{
			FString ExternalObjectsFilePath = FPackageName::LongPackageNameToFilename(ExternalObjectsPath);
			if (IFileManager::Get().DirectoryExists(*ExternalObjectsFilePath))
			{
				bool bResult = IFileManager::Get().IterateDirectoryRecursively(*ExternalObjectsFilePath, [this](const TCHAR* FilenameOrDirectory, bool bIsDirectory)
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
					UE_LOG(LogWorldPartitionConvertCommandlet, Error, TEXT("Failed to delete external package(s)"));
					return 1;
				}
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

	// Make sure the world isn't already partitioned
	AWorldSettings* MainWorldSettings = MainWorld->GetWorldSettings();
	if (MainWorldSettings->IsPartitionedWorld())
	{
		UE_LOG(LogWorldPartitionConvertCommandlet, Error, TEXT("Level '%s' is already partitionned"), *Tokens[0]);
		return 1;
	}

	// Setup the world partition object, do not create world partition object if only merging sublevels
	UWorldPartition* WorldPartition = bOnlyMergeSubLevels ? nullptr : CreateWorldPartition(MainWorldSettings);
	
	if (!bOnlyMergeSubLevels && !WorldPartition)
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

	ON_SCOPE_EXIT
	{
		const bool bBroadcastWorldDestroyedEvent = false;
		MainWorld->DestroyWorld(bBroadcastWorldDestroyedEvent);
	};

	UPackage* MainPackage = MainLevel->GetPackage();
	AWorldDataLayers* MainWorldDataLayers = MainWorld->GetWorldDataLayers();
	// DataLayers are only needed if converting to WorldPartition
	check(bOnlyMergeSubLevels || MainWorldDataLayers);

	OnWorldLoaded(MainWorld);

	auto PartitionFoliage = [this, MainWorld](AInstancedFoliageActor* IFA) -> bool
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PartitionFoliage);

		TMap<UFoliageType*, TArray<FFoliageInstance>> FoliageToAdd;
		int32 NumInstances = 0;
		int32 NumInstancesProcessed = 0;

		bool bAddFoliageSucceeded = IFA->ForEachFoliageInfo([IFA, &FoliageToAdd, &NumInstances, this](UFoliageType* FoliageType, FFoliageInfo& FoliageInfo) -> bool
		{
			if (FoliageInfo.Type == EFoliageImplType::Actor)
			{
				// We don't support Actor Foliage in WP
				FoliageInfo.ExcludeActors();
				return true;
			}

			UFoliageType* FoliageTypeToAdd = FoliageType;

			if (FoliageType->GetTypedOuter<AInstancedFoliageActor>() != nullptr)
			{
				UFoliageType* NewFoliageType = nullptr;
				
				if (!FoliageTypePath.IsEmpty())
				{
					UObject* FoliageSource = FoliageType->GetSource();
					const FString BaseAssetName = (FoliageSource != nullptr) ? FoliageSource->GetName() : FoliageType->GetName();
					FString PackageName = FoliageTypePath / BaseAssetName + TEXT("_FoliageType");

					NewFoliageType = FFoliageEditUtility::DuplicateFoliageTypeToNewPackage(PackageName, FoliageType);
				}

				if (NewFoliageType == nullptr)
				{
					UE_LOG(LogWorldPartitionConvertCommandlet, Error,
						   TEXT("Level contains embedded FoliageType settings: please save the FoliageType setting assets, ")
						   TEXT("use the SaveFoliageTypeToContentFolder switch, ")
						   TEXT("specify FoliageTypePath in configuration file or the commandline."));
					return false;
				}

				FoliageTypeToAdd = NewFoliageType;
				PackagesToSave.Add(NewFoliageType->GetOutermost());
			}

			if (FoliageInfo.Instances.Num() > 0)
			{
				check(FoliageTypeToAdd->GetTypedOuter<AInstancedFoliageActor>() == nullptr);

				FoliageToAdd.FindOrAdd(FoliageTypeToAdd).Append(FoliageInfo.Instances);
				NumInstances += FoliageInfo.Instances.Num();
				UE_LOG(LogWorldPartitionConvertCommandlet, Display, TEXT("FoliageType: %s Count: %d"), *FoliageTypeToAdd->GetName(), FoliageInfo.Instances.Num());
			}

			return true;
		});

		if (!bAddFoliageSucceeded)
		{
			return false;
		}

		IFA->GetLevel()->GetWorld()->DestroyActor(IFA);

		// Add Foliage to those actors
		for (auto& InstancesPerFoliageType : FoliageToAdd)
		{
			for (const FFoliageInstance& Instance : InstancesPerFoliageType.Value)
			{
				AInstancedFoliageActor* GridIFA = AInstancedFoliageActor::Get(MainWorld, /*bCreateIfNone=*/true, MainWorld->PersistentLevel, Instance.Location);
				FFoliageInfo* NewFoliageInfo = nullptr;
				UFoliageType* NewFoliageType = GridIFA->AddFoliageType(InstancesPerFoliageType.Key, &NewFoliageInfo);
				NewFoliageInfo->AddInstance(NewFoliageType, Instance);
				NumInstancesProcessed++;
			}
		}

		check(NumInstances == NumInstancesProcessed);

		return true;
	};

	auto PartitionLandscape = [this, MainWorld](ULandscapeInfo* LandscapeInfo)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PartitionLandscape);

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

	auto PrepareLevelActors = [this, PartitionFoliage, PartitionLandscape, MainWorldDataLayers](ULevel* Level, TArray<AActor*>& Actors, bool bMainLevel) -> bool
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PrepareLevelActors);

		const FBox WorldBounds(WorldOrigin - WorldExtent, WorldOrigin + WorldExtent);

		TArray<AInstancedFoliageActor*> IFAs;
		TSet<ULandscapeInfo*> LandscapeInfos;
		for (AActor* Actor: Actors)
		{
			if (Actor && IsValidChecked(Actor))
			{
				check(Actor->GetLevel() == Level);

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
					// Only override default grid placement on actors that are spatially loaded
					else if (Actor->GetIsSpatiallyLoaded() && Actor->CanChangeIsSpatiallyLoadedFlag())
					{
						const FBox ActorBounds = Actor->GetStreamingBounds();

						if (!WorldBounds.IsInside(ActorBounds))
						{
							Actor->SetIsSpatiallyLoaded(false);
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
								DataLayer->SetIsRuntime(false);
							}
							Actor->AddDataLayer(DataLayer);
						}
					}
					// Clear actor layers as they are not supported yet in world partition, keep them if only merging
					if (!bOnlyMergeSubLevels)
					{
						Actor->Layers.Empty();
					}
				}
			}
		}

		// do loop after as it may modify Level->Actors
		if (IFAs.Num())
		{
			UE_SCOPED_TIMER(TEXT("PartitionFoliage"), LogWorldPartitionConvertCommandlet, Display);

			for (AInstancedFoliageActor* IFA : IFAs)
			{
				if (!PartitionFoliage(IFA))
				{
					return false;
				}
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

		return true;
	};

	// Gather and load sublevels
	TArray<ULevel*> SubLevelsToConvert;
	GatherAndPrepareSubLevelsToConvert(MainLevel, SubLevelsToConvert);

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
	
	if (!PrepareLevelActors(MainLevel, MainLevel->Actors, true))
	{
		return 1;
	}
	
	PackagesToSave.Add(MainLevel->GetPackage());

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

	TMap<UObject*, UObject*> PrivateRefsMap;
	for(ULevel* SubLevel : SubLevelsToConvert)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(ConvertSubLevel);

		UWorld* SubWorld = SubLevel->GetTypedOuter<UWorld>();
		UPackage* SubPackage = SubLevel->GetPackage();

		RemapSoftObjectPaths.Add(FSoftObjectPath(SubWorld).ToString(), FSoftObjectPath(MainWorld).ToString());
		RemapSoftObjectPaths.Add(FSoftObjectPath(SubLevel).ToString(), FSoftObjectPath(MainLevel).ToString());
		RemapSoftObjectPaths.Add(FSoftObjectPath(SubPackage).ToString(), FSoftObjectPath(MainPackage).ToString());

		TArray<AActor*> ActorsToConvert;
		if (LevelHasLevelScriptBlueprint(SubLevel))
		{
			MapsWithLevelScriptsBPs.Add(SubPackage->GetLoadedPath().GetPackageName());

			if (bConvertActorsNotReferencedByLevelScript)
			{
				// Gather the list of actors referenced by the level script blueprint
				TSet<AActor*> LevelScriptActorReferences;

				ALevelScriptActor* LevelScriptActor = SubLevel->GetLevelScriptActor();
				LevelScriptActorReferences.Add(LevelScriptActor);

				ULevelScriptBlueprint* LevelScriptBlueprint = SubLevel->GetLevelScriptBlueprint(true);
				LevelScriptActorReferences.Append(ActorsReferencesUtils::GetActorReferences(LevelScriptBlueprint));

				for(AActor* Actor: SubLevel->Actors)
				{
					if(IsValid(Actor))
					{
						TSet<AActor*> ActorReferences;
						ActorReferences.Append(ActorsReferencesUtils::GetActorReferences(Actor));

						for (AActor* ActorReference : ActorReferences)
						{
							if (LevelScriptActorReferences.Find(ActorReference))
							{
								LevelScriptActorReferences.Add(Actor);
								LevelScriptActorReferences.Append(ActorReferences);
								break;
							}
						}
					}
				}

				for(AActor* Actor: SubLevel->Actors)
				{
					if(IsValid(Actor))
					{
						if (!LevelScriptActorReferences.Find(Actor))
						{
							ActorsToConvert.Add(Actor);
						}
					}
				}
			}

			// Rename the world if requested
			UWorld* SubLevelWorld = SubLevel->GetTypedOuter<UWorld>();
			UPackage* SubLevelPackage = SubLevelWorld->GetPackage();

			if (bConversionSuffix)
			{
				FString OldMainWorldPath = FSoftObjectPath(SubLevelWorld).ToString();
				FString OldMainLevelPath = FSoftObjectPath(SubLevel).ToString();
				FString OldPackagePath = FSoftObjectPath(SubLevelPackage).ToString();

				if (!RenameWorldPackageWithSuffix(SubLevelWorld))
				{
					return 1;
				}

				RemapSoftObjectPaths.Add(OldMainWorldPath, FSoftObjectPath(SubLevelWorld).ToString());
				RemapSoftObjectPaths.Add(OldMainLevelPath, FSoftObjectPath(SubLevel).ToString());
				RemapSoftObjectPaths.Add(OldPackagePath, FSoftObjectPath(SubLevelPackage).ToString());
			}
			
			PackagesToSave.Add(SubLevelPackage);

			// Spawn the level instance actor
			ULevelStreaming* SubLevelStreaming = nullptr;
			for (ULevelStreaming* LevelStreaming : MainWorld->GetStreamingLevels())
			{
				if (LevelStreaming->GetLoadedLevel() == SubLevel)
				{
					SubLevelStreaming = LevelStreaming;
					break;
				}
			}
			check(SubLevelStreaming);

			FActorSpawnParameters SpawnParams;
			SpawnParams.OverrideLevel = MainLevel;
			ALevelInstance* LevelInstanceActor = MainWorld->SpawnActor<ALevelInstance>(SpawnParams);
			
			FTransform LevelTransform;
			if (SubLevelPackage->GetWorldTileInfo())
			{
				LevelTransform = FTransform(FVector(SubLevelPackage->GetWorldTileInfo()->Position));
			}
			else
			{
				LevelTransform = SubLevelStreaming->LevelTransform;
			}

			LevelInstanceActor->DesiredRuntimeBehavior = ELevelInstanceRuntimeBehavior::LevelStreaming;
			LevelInstanceActor->SetActorTransform(LevelTransform);
			LevelInstanceActor->SetWorldAsset(SubLevelWorld);
		}
		else
		{
			if (LevelHasMapBuildData(SubLevel))
			{
				MapsWithMapBuildData.Add(SubPackage->GetLoadedPath().GetPackageName());
			}

			DetachDependantLevelPackages(SubLevel);

			ActorsToConvert = SubLevel->Actors;
		}

		UE_LOG(LogWorldPartitionConvertCommandlet, Log, TEXT("Converting %s"), *SubWorld->GetName());

		PrepareLevelActors(SubLevel, ActorsToConvert, false);

		for(AActor* Actor: ActorsToConvert)
		{
			if(Actor && IsValidChecked(Actor))
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

		if (!LevelHasLevelScriptBlueprint(SubLevel))
		{
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
	}

	// Clear streaming levels
	for (ULevelStreaming* LevelStreaming: MainWorld->GetStreamingLevels())
	{
		LevelStreaming->MarkAsGarbage();
		ULevelStreaming::RemoveLevelAnnotation(LevelStreaming->GetLoadedLevel());
		MainWorld->RemoveLevel(LevelStreaming->GetLoadedLevel());
	}
	MainWorld->ClearStreamingLevels();

	// Fixup SoftObjectPaths
	FixupSoftObjectPaths(MainPackage);

	PerformAdditionalWorldCleanup(MainWorld);

	bool bForceInitializeWorld = false;
	bool bInitializedPhysicsSceneForSave = GEditor->InitializePhysicsSceneForSaveIfNecessary(MainWorld, bForceInitializeWorld);

	// After conversion, convert actors to external actors
	UPackage* LevelPackage = MainLevel->GetPackage();

	TArray<AActor*> ActorList;
	TArray<AActor*> ChildActorList;
	ActorList.Reserve(MainLevel->Actors.Num());

	// Move child actors at the end of the list
	for (AActor* Actor: MainLevel->Actors)
	{
		if (Actor && IsValidChecked(Actor))
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

	if (!bOnlyMergeSubLevels)
	{
		WorldPartition->AddToRoot();
	}

	if (!bReportOnly)
	{
		FLevelActorFoldersHelper::SetUseActorFolders(MainLevel, true);
		MainLevel->SetUseExternalActors(true);

		TSet<FGuid> ActorGuids;
		for(AActor* Actor: ActorList)
		{
			if (!Actor || !IsValidChecked(Actor) || !Actor->SupportsExternalPackaging())
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

			if (!Actor->CreateOrUpdateActorFolder())
			{
				UE_LOG(LogWorldPartitionConvertCommandlet, Error, TEXT("Failed to convert actor %s folder to persistent folder."), *Actor->GetName());
			}
			
			UPackage* ActorPackage = Actor->GetExternalPackage();
			PackagesToSave.Add(ActorPackage);

			UE_LOG(LogWorldPartitionConvertCommandlet, Log, TEXT("Extracted actor %s(guid:%s) in %s"), *Actor->GetName(), *Actor->GetActorGuid().ToString(EGuidFormats::Digits), *ActorPackage->GetName());
		}

		// Required to clear any deleted actors from the level
		CollectGarbage(RF_Standalone);

		for (AActor* Actor : ActorList)
		{
			if (!IsValid(Actor))
			{
				continue;
			}

			PerformAdditionalActorChanges(Actor);
		}

		MainLevel->ForEachActorFolder([this](UActorFolder* ActorFolder)
		{
			UPackage* ActorFolderPackage = ActorFolder->GetExternalPackage();
			check(ActorFolderPackage);
			PackagesToSave.Add(ActorFolderPackage);
			return true;
		});

		MainWorld->WorldComposition = nullptr;
		MainLevel->bIsPartitioned = !bOnlyMergeSubLevels;

		if (bDeleteSourceLevels)
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(DeleteSourceLevels);

			for (UPackage* Package : PackagesToDelete)
			{
				if (!PackageHelper.Delete(Package))
				{
					return 1;
				}
			}
		}

		// Checkout packages
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(CheckoutPackages);

			UE_LOG(LogWorldPartitionConvertCommandlet, Log, TEXT("Checking out %d packages."), PackagesToSave.Num());
			for(UPackage* Package: PackagesToSave)
			{
				FString PackageFileName = SourceControlHelpers::PackageFilename(Package);
				if (FPlatformFileManager::Get().GetPlatformFile().FileExists(*PackageFileName))
				{
					if (!PackageHelper.Checkout(Package))
					{
						return 1;
					}
				}
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
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(SavePackages);

			UE_LOG(LogWorldPartitionConvertCommandlet, Log, TEXT("Saving %d packages."), PackagesToSave.Num());
			for (UPackage* PackageToSave : PackagesToSave)
			{
				FString PackageFileName = SourceControlHelpers::PackageFilename(PackageToSave);
				FSavePackageArgs SaveArgs;
				SaveArgs.TopLevelFlags = RF_Standalone;
				SaveArgs.SaveFlags = SAVE_Async;
				if (!UPackage::SavePackage(PackageToSave, nullptr, *PackageFileName, SaveArgs))
				{
					return 1;
				}
			}
		}

		// Add packages
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(AddPackagesToSourceControl);

			// Add new packages to source control
			for(UPackage* PackageToSave: PackagesToSave)
			{
				if(!PackageHelper.AddToSourceControl(PackageToSave))
				{
					return 1;
				}
			}
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

			if (!bOnlyMergeSubLevels)
			{
				WorldPartition->EditorHash->SaveConfig(CPF_Config, *LevelConfigFilename);
				WorldPartition->RuntimeHash->SaveConfig(CPF_Config, *LevelConfigFilename);
			}
			
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

const FString UWorldPartitionConvertCommandlet::GetConversionSuffix(const bool bInOnlyMergeSubLevels)
{
	return bInOnlyMergeSubLevels ? TEXT("_OFPA") : TEXT("_WP");
}

bool UWorldPartitionConvertCommandlet::ShouldConvertStreamingLevel(ULevelStreaming* StreamingLevel)
{
	return StreamingLevel && !ExcludedLevels.Contains(StreamingLevel->GetWorldAssetPackageName());
}

