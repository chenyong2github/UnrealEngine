// Copyright Epic Games, Inc. All Rights Reserved.
#include "WorldPartitionEditorModule.h"

#include "WorldPartition/WorldPartition.h"
#include "WorldPartition/WorldPartitionVolume.h"

#include "WorldPartition/HLOD/HLODLayerAssetTypeActions.h"

#include "WorldPartition/SWorldPartitionEditor.h"
#include "WorldPartition/SWorldPartitionEditorGridSpatialHash.h"

#include "LevelEditor.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"

IMPLEMENT_MODULE( FWorldPartitionEditorModule, WorldPartitionEditor );

#define LOCTEXT_NAMESPACE "WorldPartition"

// World Partition
static void OnLoadSelectedWorldPartitionVolumes(TArray<TWeakObjectPtr<AActor>> Volumes)
{
	for (TWeakObjectPtr<AActor> Actor: Volumes)
	{
		AWorldPartitionVolume* WorldPartitionVolume = CastChecked<AWorldPartitionVolume>(Actor.Get());
		WorldPartitionVolume->LoadIntersectingCells();
	}
}

static void CreateLevelViewportContextMenuEntries(FMenuBuilder& MenuBuilder, TArray<TWeakObjectPtr<AActor>> Volumes)
{
	MenuBuilder.BeginSection("WorldPartition", LOCTEXT("WorldPartition", "World Partition"));

	MenuBuilder.AddMenuEntry(
		LOCTEXT("WorldPartitionLoad", "Load selected world partition volumes"),
		LOCTEXT("WorldPartitionLoad_Tooltip", "Load selected world partition volumes"),
		FSlateIcon(),
		FExecuteAction::CreateStatic(OnLoadSelectedWorldPartitionVolumes, Volumes),
		NAME_None,
		EUserInterfaceActionType::Button);

	MenuBuilder.EndSection();
}

static TSharedRef<FExtender> OnExtendLevelEditorMenu(const TSharedRef<FUICommandList> CommandList, TArray<AActor*> SelectedActors)
{
	TSharedRef<FExtender> Extender(new FExtender());

	TArray<TWeakObjectPtr<AActor> > Volumes;
	for (AActor* Actor : SelectedActors)
	{
		if (Actor->IsA(AWorldPartitionVolume::StaticClass()))
		{
			Volumes.Add(Actor);
		}
	}

	if (Volumes.Num())
	{
		Extender->AddMenuExtension(
			"SelectActorGeneral",
			EExtensionHook::After,
			nullptr,
			FMenuExtensionDelegate::CreateStatic(&CreateLevelViewportContextMenuEntries, Volumes));
	}

	return Extender;
}

void FWorldPartitionEditorModule::StartupModule()
{
	SWorldPartitionEditorGrid::RegisterPartitionEditorGridCreateInstanceFunc(NAME_None, &SWorldPartitionEditorGrid::CreateInstance);
	SWorldPartitionEditorGrid::RegisterPartitionEditorGridCreateInstanceFunc(TEXT("SpatialHash"), &SWorldPartitionEditorGridSpatialHash::CreateInstance);
	
	FLevelEditorModule& LevelEditorModule = FModuleManager::Get().LoadModuleChecked<FLevelEditorModule>("LevelEditor");
	TArray<FLevelEditorModule::FLevelViewportMenuExtender_SelectedActors>& MenuExtenderDelegates = LevelEditorModule.GetAllLevelViewportContextMenuExtenders();

	MenuExtenderDelegates.Add(FLevelEditorModule::FLevelViewportMenuExtender_SelectedActors::CreateStatic(&OnExtendLevelEditorMenu));
	LevelEditorExtenderDelegateHandle = MenuExtenderDelegates.Last().GetHandle();
	
	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
	HLODLayerAssetTypeActions = MakeShareable(new FHLODLayerAssetTypeActions);
	AssetTools.RegisterAssetTypeActions(HLODLayerAssetTypeActions.ToSharedRef());
}

void FWorldPartitionEditorModule::ShutdownModule()
{
	UWorldPartition::WorldPartitionChangedEvent.RemoveAll(this);
	
	if (FLevelEditorModule* LevelEditorModule = FModuleManager::Get().GetModulePtr<FLevelEditorModule>("LevelEditor"))
	{
		LevelEditorModule->GetAllLevelViewportContextMenuExtenders().RemoveAll([=](const FLevelEditorModule::FLevelViewportMenuExtender_SelectedActors& In) { return In.GetHandle() == LevelEditorExtenderDelegateHandle; });
	}

	IAssetTools* AssetToolsModule = nullptr;
	if (FModuleManager::Get().IsModuleLoaded("AssetTools"))
	{
		AssetToolsModule = &FModuleManager::GetModuleChecked<FAssetToolsModule>("AssetTools").Get();
	}

	// Unregister the HLODLayer asset type actions
	if (HLODLayerAssetTypeActions.IsValid())
	{
		if (AssetToolsModule)
		{
			AssetToolsModule->UnregisterAssetTypeActions(HLODLayerAssetTypeActions.ToSharedRef());
		}
		HLODLayerAssetTypeActions.Reset();
	}
}

TSharedRef<SWidget> FWorldPartitionEditorModule::CreateWorldPartitionEditor()
{
	UWorld* EditorWorld = GEditor->GetEditorWorldContext().World();
	return SNew(SWorldPartitionEditor).InWorld(EditorWorld);
}

#undef LOCTEXT_NAMESPACE
