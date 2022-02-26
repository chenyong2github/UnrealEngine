// Copyright Epic Games, Inc. All Rights Reserved.
#include "WorldPartitionEditorModule.h"

#include "WorldPartition/WorldPartition.h"
#include "WorldPartition/WorldPartitionVolume.h"

#include "WorldPartition/HLOD/HLODLayerAssetTypeActions.h"

#include "WorldPartition/SWorldPartitionEditor.h"
#include "WorldPartition/SWorldPartitionEditorGridSpatialHash.h"

#include "WorldPartition/Customizations/WorldPartitionDetailsCustomization.h"

#include "WorldPartition/SWorldPartitionConvertDialog.h"
#include "WorldPartition/WorldPartitionConvertOptions.h"
#include "WorldPartition/WorldPartitionEditorSettings.h"

#include "LevelEditor.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"

#include "Engine/Level.h"

#include "Misc/MessageDialog.h"
#include "Misc/StringBuilder.h"
#include "Misc/ScopedSlowTask.h"

#include "Interfaces/IMainFrameModule.h"
#include "Widgets/SWindow.h"
#include "Commandlets/WorldPartitionConvertCommandlet.h"
#include "FileHelpers.h"
#include "ToolMenus.h"
#include "IContentBrowserSingleton.h"
#include "ContentBrowserModule.h"
#include "EditorDirectories.h"
#include "AssetRegistryModule.h"
#include "PropertyEditorModule.h"
#include "Editor/WorkspaceMenuStructure/Public/WorkspaceMenuStructure.h"
#include "Editor/WorkspaceMenuStructure/Public/WorkspaceMenuStructureModule.h"
#include "Framework/Docking/LayoutExtender.h"
#include "Widgets/Docking/SDockTab.h"

IMPLEMENT_MODULE( FWorldPartitionEditorModule, WorldPartitionEditor );

#define LOCTEXT_NAMESPACE "WorldPartition"

const FName WorldPartitionEditorTabId("WorldBrowserPartitionEditor");

// World Partition
static void OnLoadSelectedWorldPartitionVolumes(TArray<TWeakObjectPtr<AActor>> Volumes)
{
	for (TWeakObjectPtr<AActor> Actor: Volumes)
	{
		AWorldPartitionVolume* WorldPartitionVolume = CastChecked<AWorldPartitionVolume>(Actor.Get());
		WorldPartitionVolume->LoadIntersectingCells(true);
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
			"ActorTypeTools",
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
	
	if (!IsRunningGame())
	{
		FLevelEditorModule& LevelEditorModule = FModuleManager::Get().LoadModuleChecked<FLevelEditorModule>("LevelEditor");
		TArray<FLevelEditorModule::FLevelViewportMenuExtender_SelectedActors>& MenuExtenderDelegates = LevelEditorModule.GetAllLevelViewportContextMenuExtenders();


		LevelEditorModule.OnRegisterTabs().AddRaw(this, &FWorldPartitionEditorModule::RegisterWorldPartitionTabs);
		LevelEditorModule.OnRegisterLayoutExtensions().AddRaw(this, &FWorldPartitionEditorModule::RegisterWorldPartitionLayout);

		MenuExtenderDelegates.Add(FLevelEditorModule::FLevelViewportMenuExtender_SelectedActors::CreateStatic(&OnExtendLevelEditorMenu));
		LevelEditorExtenderDelegateHandle = MenuExtenderDelegates.Last().GetHandle();

		FToolMenuOwnerScoped OwnerScoped(this);
		UToolMenu* Menu = UToolMenus::Get()->ExtendMenu("LevelEditor.MainMenu.Tools");
		FToolMenuSection& Section = Menu->AddSection("WorldPartition", LOCTEXT("WorldPartition", "World Partition"));
		Section.AddEntry(FToolMenuEntry::InitMenuEntry(
			"WorldPartition",
			LOCTEXT("WorldPartitionConvertTitle", "Convert Level..."),
			LOCTEXT("WorldPartitionConvertTooltip", "Converts a Level to World Partition."),
			FSlateIcon(FEditorStyle::GetStyleSetName(), "DeveloperTools.MenuIcon"),
			FUIAction(FExecuteAction::CreateRaw(this, &FWorldPartitionEditorModule::OnConvertMap))
		));

		FEditorDelegates::MapChange.AddRaw(this, &FWorldPartitionEditorModule::OnMapChanged);
	}

	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
	HLODLayerAssetTypeActions = MakeShareable(new FHLODLayerAssetTypeActions);
	AssetTools.RegisterAssetTypeActions(HLODLayerAssetTypeActions.ToSharedRef());

	FPropertyEditorModule& PropertyEditor = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
	PropertyEditor.RegisterCustomClassLayout("WorldPartition", FOnGetDetailCustomizationInstance::CreateStatic(&FWorldPartitionDetails::MakeInstance));
}

void FWorldPartitionEditorModule::ShutdownModule()
{
	if (!IsRunningGame())
	{
		if (FLevelEditorModule* LevelEditorModule = FModuleManager::Get().GetModulePtr<FLevelEditorModule>("LevelEditor"))
		{
			LevelEditorModule->GetAllLevelViewportContextMenuExtenders().RemoveAll([=](const FLevelEditorModule::FLevelViewportMenuExtender_SelectedActors& In) { return In.GetHandle() == LevelEditorExtenderDelegateHandle; });

			LevelEditorModule->OnRegisterTabs().RemoveAll(this);
			LevelEditorModule->OnRegisterLayoutExtensions().RemoveAll(this);

			if (LevelEditorModule->GetLevelEditorTabManager())
			{
				LevelEditorModule->GetLevelEditorTabManager()->UnregisterTabSpawner(WorldPartitionEditorTabId);
			}

		}

		FEditorDelegates::MapChange.RemoveAll(this);

		UToolMenus::UnregisterOwner(this);
	}

	// Unregister the HLODLayer asset type actions
	if (HLODLayerAssetTypeActions.IsValid())
	{
		if (FModuleManager::Get().IsModuleLoaded("AssetTools"))
		{
			IAssetTools& AssetTools = FModuleManager::GetModuleChecked<FAssetToolsModule>("AssetTools").Get();
			AssetTools.UnregisterAssetTypeActions(HLODLayerAssetTypeActions.ToSharedRef());
		}
		HLODLayerAssetTypeActions.Reset();
	}

	if (FModuleManager::Get().IsModuleLoaded("PropertyEditor"))
	{
		FPropertyEditorModule& PropertyEditor = FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor");
		PropertyEditor.UnregisterCustomClassLayout("WorldPartition");
	}
}

TSharedRef<SWidget> FWorldPartitionEditorModule::CreateWorldPartitionEditor()
{
	UWorld* EditorWorld = GEditor->GetEditorWorldContext().World();
	return SNew(SWorldPartitionEditor).InWorld(EditorWorld);
}

int32 FWorldPartitionEditorModule::GetPlacementGridSize() const
{
	// Currently shares setting with Foliage. Can be changed when exposed.
	return GetDefault<UWorldPartitionEditorSettings>()->InstancedFoliageGridSize;
}

int32 FWorldPartitionEditorModule::GetInstancedFoliageGridSize() const
{
	return GetDefault<UWorldPartitionEditorSettings>()->InstancedFoliageGridSize;
}

void FWorldPartitionEditorModule::OnConvertMap()
{
	IContentBrowserSingleton& ContentBrowserSingleton = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser").Get();
	
	FOpenAssetDialogConfig Config;
	Config.bAllowMultipleSelection = false;
	FString OutPathName;
	if (FPackageName::TryConvertFilenameToLongPackageName(FEditorDirectories::Get().GetLastDirectory(ELastDirectory::LEVEL), OutPathName))
	{
		Config.DefaultPath = OutPathName;
	}	
	Config.AssetClassNames.Add(UWorld::StaticClass()->GetFName());

	TArray<FAssetData> Assets = ContentBrowserSingleton.CreateModalOpenAssetDialog(Config);
	if (Assets.Num() == 1)
	{
		ConvertMap(Assets[0].PackageName.ToString());
	}
}

bool FWorldPartitionEditorModule::ConvertMap(const FString& InLongPackageName)
{
	if (ULevel::GetIsLevelPartitionedFromPackage(FName(*InLongPackageName)))
	{
		FMessageDialog::Open(EAppMsgType::Ok, LOCTEXT("ConvertMapMsg", "Map is already using World Partition"));
		return true;
	}

	UWorldPartitionConvertOptions* DefaultConvertOptions = GetMutableDefault<UWorldPartitionConvertOptions>();
	DefaultConvertOptions->CommandletClass = GetDefault<UWorldPartitionEditorSettings>()->CommandletClass;
	DefaultConvertOptions->bInPlace = false;
	DefaultConvertOptions->bSkipStableGUIDValidation = false;
	DefaultConvertOptions->LongPackageName = InLongPackageName;

	TSharedPtr<SWindow> DlgWindow =
		SNew(SWindow)
		.Title(LOCTEXT("ConvertWindowTitle", "Convert Settings"))
		.ClientSize(SWorldPartitionConvertDialog::DEFAULT_WINDOW_SIZE)
		.SizingRule(ESizingRule::UserSized)
		.SupportsMinimize(false)
		.SupportsMaximize(false)
		.SizingRule(ESizingRule::FixedSize);

	TSharedRef<SWorldPartitionConvertDialog> ConvertDialog =
		SNew(SWorldPartitionConvertDialog)
		.ParentWindow(DlgWindow)
		.ConvertOptions(DefaultConvertOptions);

	DlgWindow->SetContent(ConvertDialog);

	IMainFrameModule& MainFrameModule = FModuleManager::LoadModuleChecked<IMainFrameModule>(TEXT("MainFrame"));
	FSlateApplication::Get().AddModalWindow(DlgWindow.ToSharedRef(), MainFrameModule.GetParentWindow());

	if (ConvertDialog->ClickedOk())
	{
		// Conversion will try to load the converted map so ask user to save dirty packages
		if (!FEditorFileUtils::SaveDirtyPackages(/*bPromptUserToSave=*/true, /*bSaveMapPackages=*/true, /*bSaveContentPackages=*/false))
		{
			return false;
		}

		// Unload any loaded map
		if (!UEditorLoadingAndSavingUtils::NewBlankMap(/*bSaveExistingMap*/false))
		{
			return false;
		}

		FProcHandle ProcessHandle;
		bool bCancelled = false;

		// Task scope
		{
			FScopedSlowTask SlowTask(0, LOCTEXT("ConvertProgress", "Converting map to world partition..."));
			SlowTask.MakeDialog(true);

			FString CurrentExecutableName = FPlatformProcess::ExecutablePath();

			// Try to provide complete Path, if we can't try with project name
			FString ProjectPath = FPaths::IsProjectFilePathSet() ? FPaths::GetProjectFilePath() : FApp::GetProjectName();

			uint32 ProcessID;
			FString Arguments = FString::Printf(TEXT("\"%s\" %s"), *ProjectPath, *DefaultConvertOptions->ToCommandletArgs());
			ProcessHandle = FPlatformProcess::CreateProc(*CurrentExecutableName, *Arguments, true, false, false, &ProcessID, 0, nullptr, nullptr);
			
			while (FPlatformProcess::IsProcRunning(ProcessHandle))
			{
				if (SlowTask.ShouldCancel())
				{
					bCancelled = true;
					FPlatformProcess::TerminateProc(ProcessHandle);
					break;
				}

				SlowTask.EnterProgressFrame(0);
				FPlatformProcess::Sleep(0.1);
			}
		}

		int32 Result = 0;
		if (!bCancelled && FPlatformProcess::GetProcReturnCode(ProcessHandle, &Result))
		{	
			if (Result == 0)
			{
				FMessageDialog::Open(EAppMsgType::Ok, LOCTEXT("ConvertMapCompleted", "Conversion succeeded."));

#if	PLATFORM_DESKTOP
				if (DefaultConvertOptions->bGenerateIni)
				{
					const FString PackageFilename = FPackageName::LongPackageNameToFilename(DefaultConvertOptions->LongPackageName);
					const FString PackageDirectory = FPaths::ConvertRelativePathToFull(FPaths::GetPath(PackageFilename));
					FPlatformProcess::ExploreFolder(*PackageDirectory);
				}
#endif
				
				
				// Force update before loading converted map
				FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
				IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();
												
				FString MapToLoad = InLongPackageName;
				if (!DefaultConvertOptions->bInPlace)
				{
					MapToLoad += UWorldPartitionConvertCommandlet::GetConversionSuffix(DefaultConvertOptions->bOnlyMergeSubLevels);
				}
				
				AssetRegistry.ScanModifiedAssetFiles({ MapToLoad });
				AssetRegistry.ScanPathsSynchronous( ULevel::GetExternalObjectsPaths(MapToLoad), true);
				
				FEditorFileUtils::LoadMap(MapToLoad);
			}
		}
		else if (bCancelled)
		{
			FMessageDialog::Open(EAppMsgType::Ok, LOCTEXT("ConvertMapCancelled", "Conversion cancelled!"));
		}
		
		if(Result != 0)
		{
			FMessageDialog::Open(EAppMsgType::Ok, LOCTEXT("ConvertMapFailed", "Conversion failed!"));
		}
	}

	return false;
}

void FWorldPartitionEditorModule::OnMapChanged(uint32 MapFlags)
{
	if (MapFlags == MapChangeEventFlags::NewMap)
	{
		FLevelEditorModule* LevelEditorModule = FModuleManager::Get().GetModulePtr<FLevelEditorModule>("LevelEditor");
	
		TSharedPtr<FTabManager> LevelEditorTabManager = LevelEditorModule ? LevelEditorModule->GetLevelEditorTabManager() : nullptr;

		// If the world opened is a world partition world spawn the world partition tab if not open.
		UWorld* EditorWorld = GEditor->GetEditorWorldContext().World();
		if (EditorWorld && EditorWorld->IsPartitionedWorld())
		{
			if(LevelEditorTabManager && !WorldPartitionTab.IsValid())
			{
				WorldPartitionTab = LevelEditorTabManager->TryInvokeTab(WorldPartitionEditorTabId);
			}
		}
		else if(TSharedPtr<SDockTab> WorldPartitionTabPin = WorldPartitionTab.Pin())
		{
			// close the WP tab if not a world partition world
			WorldPartitionTabPin->RequestCloseTab();
		}
	}
}

TSharedRef<SDockTab> FWorldPartitionEditorModule::SpawnWorldPartitionTab(const FSpawnTabArgs& Args)
{
	TSharedRef<SDockTab> NewTab =
		SNew(SDockTab)
		.Label(NSLOCTEXT("LevelEditor", "WorldBrowserPartitionTabTitle", "World Partition"))
		[
			CreateWorldPartitionEditor()
		];

	WorldPartitionTab = NewTab;
	return NewTab;
}

void FWorldPartitionEditorModule::RegisterWorldPartitionTabs(TSharedPtr<FTabManager> InTabManager)
{
	const IWorkspaceMenuStructure& MenuStructure = WorkspaceMenu::GetMenuStructure();

	const FSlateIcon WorldPartitionIcon(FEditorStyle::GetStyleSetName(), "LevelEditor.Tabs.WorldPartition");

	InTabManager->RegisterTabSpawner(WorldPartitionEditorTabId,
		FOnSpawnTab::CreateRaw(this, &FWorldPartitionEditorModule::SpawnWorldPartitionTab))
		.SetDisplayName(NSLOCTEXT("LevelEditorTabs", "WorldPartitionEditor", "World Partition Editor"))
		.SetTooltipText(NSLOCTEXT("LevelEditorTabs", "WorldPartitionEditorTooltipText", "Open the World Partition Editor."))
		.SetGroup(MenuStructure.GetLevelEditorWorldPartitionCategory())
		.SetIcon(WorldPartitionIcon);
}

void FWorldPartitionEditorModule::RegisterWorldPartitionLayout(FLayoutExtender& Extender)
{
	Extender.ExtendLayout(FTabId("LevelEditorSelectionDetails"), ELayoutExtensionPosition::After, FTabManager::FTab(WorldPartitionEditorTabId, ETabState::ClosedTab));
}

UWorldPartitionEditorSettings::UWorldPartitionEditorSettings()
{
	CommandletClass = UWorldPartitionConvertCommandlet::StaticClass();
	InstancedFoliageGridSize = 25600;
}

FString UWorldPartitionConvertOptions::ToCommandletArgs() const
{
	TStringBuilder<1024> CommandletArgsBuilder;
	CommandletArgsBuilder.Appendf(TEXT("-run=%s %s -AllowCommandletRendering"), *CommandletClass->GetName(), *LongPackageName);
	
	if (!bInPlace)
	{
		CommandletArgsBuilder.Append(TEXT(" -ConversionSuffix"));
	}

	if (bSkipStableGUIDValidation)
	{
		CommandletArgsBuilder.Append(TEXT(" -SkipStableGUIDValidation"));
	}

	if (bDeleteSourceLevels)
	{
		CommandletArgsBuilder.Append(TEXT(" -DeleteSourceLevels"));
	}
	
	if (bGenerateIni)
	{
		CommandletArgsBuilder.Append(TEXT(" -GenerateIni"));
	}
	
	if (bReportOnly)
	{
		CommandletArgsBuilder.Append(TEXT(" -ReportOnly"));
	}
	
	if (bVerbose)
	{
		CommandletArgsBuilder.Append(TEXT(" -Verbose"));
	}

	if (bOnlyMergeSubLevels)
	{
		CommandletArgsBuilder.Append(TEXT(" -OnlyMergeSubLevels"));
	}

	if (bSaveFoliageTypeToContentFolder)
	{
		CommandletArgsBuilder.Append(TEXT(" -FoliageTypePath=/Game/FoliageTypes"));
	}
	
	return CommandletArgsBuilder.ToString();
}

#undef LOCTEXT_NAMESPACE
