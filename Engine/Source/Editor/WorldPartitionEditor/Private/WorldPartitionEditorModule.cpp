// Copyright Epic Games, Inc. All Rights Reserved.
#include "WorldPartitionEditorModule.h"

#include "WorldPartition/WorldPartition.h"
#include "WorldPartition/WorldPartitionVolume.h"

#include "WorldPartition/WorldPartitionHLODsBuilder.h"
#include "WorldPartition/WorldPartitionMiniMapBuilder.h"

#include "WorldPartition/HLOD/HLODLayerAssetTypeActions.h"

#include "WorldPartition/SWorldPartitionEditor.h"
#include "WorldPartition/SWorldPartitionEditorGridSpatialHash.h"

#include "WorldPartition/Customizations/WorldPartitionDetailsCustomization.h"

#include "WorldPartition/SWorldPartitionConvertDialog.h"
#include "WorldPartition/WorldPartitionConvertOptions.h"
#include "WorldPartition/WorldPartitionEditorSettings.h"

#include "WorldPartition/HLOD/SWorldPartitionBuildHLODsDialog.h"

#include "LevelEditor.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"

#include "Engine/Level.h"

#include "Misc/MessageDialog.h"
#include "Misc/StringBuilder.h"
#include "Misc/ScopedSlowTask.h"
#include "Internationalization/Regex.h"

#include "Interfaces/IMainFrameModule.h"
#include "Widgets/SWindow.h"
#include "Commandlets/WorldPartitionConvertCommandlet.h"
#include "FileHelpers.h"
#include "ToolMenus.h"
#include "IContentBrowserSingleton.h"
#include "IDirectoryWatcher.h"
#include "DirectoryWatcherModule.h"
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

DEFINE_LOG_CATEGORY_STATIC(LogWorldPartitionEditor, All, All);

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

static bool UnloadCurrentMap(bool bAskSaveContentPackages)
{
	// Ask user to save dirty packages
	if (!FEditorFileUtils::SaveDirtyPackages(/*bPromptUserToSave=*/true, /*bSaveMapPackages=*/true, bAskSaveContentPackages))
	{
		return false;
	}

	// Unload any loaded map
	if (!UEditorLoadingAndSavingUtils::NewBlankMap(/*bSaveExistingMap*/false))
	{
		return false;
	}

	return true;
}

static void RescanAssetsAndLoadMap(const FString& MapToLoad)
{
	// Force a directory watcher tick for the asset registry to get notified of the changes
	FDirectoryWatcherModule& DirectoryWatcherModule = FModuleManager::Get().LoadModuleChecked<FDirectoryWatcherModule>(TEXT("DirectoryWatcher"));
	DirectoryWatcherModule.Get()->Tick(-1.0f);

	// Force update before loading converted map
	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();

	TArray<FString> ExternalObjectsPaths = ULevel::GetExternalObjectsPaths(MapToLoad);

	AssetRegistry.ScanModifiedAssetFiles({ MapToLoad });
	AssetRegistry.ScanPathsSynchronous(ExternalObjectsPaths, true);

	FEditorFileUtils::LoadMap(MapToLoad);
}

static void RunCommandletAsExternalProcess(const FString& InCommandletArgs, const FText& InOperationDescription, int32& OutResult, bool& bOutCancelled, FString& OutCommandletOutput)
{
	OutResult = 0;
	bOutCancelled = false;
	OutCommandletOutput.Empty();

	FProcHandle ProcessHandle;

	FScopedSlowTask SlowTask(1.0f, InOperationDescription);
	SlowTask.MakeDialog(true);

	void* ReadPipe = nullptr;
	void* WritePipe = nullptr;
	verify(FPlatformProcess::CreatePipe(ReadPipe, WritePipe));

	FString CurrentExecutableName = FPlatformProcess::ExecutablePath();

	// Try to provide complete Path, if we can't try with project name
	FString ProjectPath = FPaths::IsProjectFilePathSet() ? FPaths::GetProjectFilePath() : FApp::GetProjectName();

	uint32 ProcessID;
	FString Arguments = FString::Printf(TEXT("\"%s\" %s"), *ProjectPath, *InCommandletArgs);
	
	UE_LOG(LogWorldPartitionEditor, Display, TEXT("Running commandlet: %s %s"), *CurrentExecutableName, *Arguments);
	ProcessHandle = FPlatformProcess::CreateProc(*CurrentExecutableName, *Arguments, true, false, false, &ProcessID, 0, nullptr, WritePipe, ReadPipe);

	while (FPlatformProcess::IsProcRunning(ProcessHandle))
	{
		if (SlowTask.ShouldCancel())
		{
			bOutCancelled = true;
			FPlatformProcess::TerminateProc(ProcessHandle);
			break;
		}

		const FString LogString = FPlatformProcess::ReadPipe(ReadPipe);
		if (!LogString.IsEmpty())
		{
			OutCommandletOutput.Append(LogString);
		}

		// Parse output, look for progress indicator in the log (in the form "Display: [i / N] Msg...\n")
		const FRegexPattern LogProgressPattern(TEXT("Display:\\s\\[([0-9]+)\\s\\/\\s([0-9]+)\\]\\s(.+)?(?=\\.{3}$)"));
		FRegexMatcher Regex(LogProgressPattern, *LogString);
		while (Regex.FindNext())
		{
			// Update slow task progress & message
			SlowTask.CompletedWork = FCString::Atoi(*Regex.GetCaptureGroup(1));
			SlowTask.TotalAmountOfWork = FCString::Atoi(*Regex.GetCaptureGroup(2));
			SlowTask.DefaultMessage = FText::FromString(Regex.GetCaptureGroup(3));
		}

		SlowTask.EnterProgressFrame(0);
		FPlatformProcess::Sleep(0.1);
	}

	UE_LOG(LogWorldPartitionEditor, Display, TEXT("#### Begin commandlet output ####\n%s"), *OutCommandletOutput);
	UE_LOG(LogWorldPartitionEditor, Display, TEXT("#### End commandlet output ####"));

	FPlatformProcess::ClosePipe(ReadPipe, WritePipe);
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
		if (!UnloadCurrentMap(/*bAskSaveContentPackages=*/false))
		{
			return false;
		}

		const FString CommandletArgs = *DefaultConvertOptions->ToCommandletArgs();
		const FText OperationDescription = LOCTEXT("ConvertProgress", "Converting map to world partition...");
		
		int32 Result;
		bool bCancelled;
		FString CommandletOutput;
		RunCommandletAsExternalProcess(CommandletArgs, OperationDescription, Result, bCancelled, CommandletOutput);
		if (!bCancelled && Result == 0)
		{	
			FMessageDialog::Open(EAppMsgType::Ok, FText::Format(LOCTEXT("ConvertMapCompleted", "Conversion completed:\n{0}"), FText::FromString(CommandletOutput)));

#if	PLATFORM_DESKTOP
			if (DefaultConvertOptions->bGenerateIni)
			{
				const FString PackageFilename = FPackageName::LongPackageNameToFilename(DefaultConvertOptions->LongPackageName);
				const FString PackageDirectory = FPaths::ConvertRelativePathToFull(FPaths::GetPath(PackageFilename));
				FPlatformProcess::ExploreFolder(*PackageDirectory);
			}
#endif				
				
			FString MapToLoad = InLongPackageName;
			if (!DefaultConvertOptions->bInPlace)
			{
				MapToLoad += UWorldPartitionConvertCommandlet::GetConversionSuffix(DefaultConvertOptions->bOnlyMergeSubLevels);
			}

			RescanAssetsAndLoadMap(MapToLoad);
		}
		else if (bCancelled)
		{
			FMessageDialog::Open(EAppMsgType::Ok, LOCTEXT("ConvertMapCancelled", "Conversion cancelled!"));
		}
		else if(Result != 0)
		{
			FMessageDialog::Open(EAppMsgType::Ok, FText::Format(LOCTEXT("ConvertMapFailed", "Conversion failed:\n{0}"), FText::FromString(CommandletOutput)));
		}
	}

	return false;
}

bool FWorldPartitionEditorModule::RunBuilder(TSubclassOf<UWorldPartitionBuilder> InWorldPartitionBuilder, const FString& InLongPackageName)
{
	// Ideally this should be improved to automatically register all builders & present their options in a consistent way...

	if (InWorldPartitionBuilder == UWorldPartitionHLODsBuilder::StaticClass())
	{
		return BuildHLODs(InLongPackageName);
	}
	
	if (InWorldPartitionBuilder == UWorldPartitionMiniMapBuilder::StaticClass())
	{
		return BuildMinimap(InLongPackageName);
	}

	return false;
}

bool FWorldPartitionEditorModule::BuildHLODs(const FString& InMapToProcess)
{
	TSharedPtr<SWindow> DlgWindow =
		SNew(SWindow)
		.Title(LOCTEXT("BuildHLODsWindowTitle", "Build HLODs"))
		.ClientSize(SWorldPartitionBuildHLODsDialog::DEFAULT_WINDOW_SIZE)
		.SizingRule(ESizingRule::UserSized)
		.SupportsMinimize(false)
		.SupportsMaximize(false)
		.SizingRule(ESizingRule::FixedSize);

	TSharedRef<SWorldPartitionBuildHLODsDialog> BuildHLODsDialog =
		SNew(SWorldPartitionBuildHLODsDialog)
		.ParentWindow(DlgWindow);

	DlgWindow->SetContent(BuildHLODsDialog);

	IMainFrameModule& MainFrameModule = FModuleManager::LoadModuleChecked<IMainFrameModule>(TEXT("MainFrame"));
	FSlateApplication::Get().AddModalWindow(DlgWindow.ToSharedRef(), MainFrameModule.GetParentWindow());

	if (BuildHLODsDialog->GetDialogResult() != SWorldPartitionBuildHLODsDialog::DialogResult::Cancel)
	{
		if (!UnloadCurrentMap(/*bAskSaveContentPackages=*/true))
		{
			return false;
		}

		const FString BuildArgs = BuildHLODsDialog->GetDialogResult() == SWorldPartitionBuildHLODsDialog::DialogResult::BuildHLODs ? "-SetupHLODs -BuildHLODs -AllowCommandletRendering" : "-DeleteHLODs";
		const FString CommandletArgs = InMapToProcess + " -run=WorldPartitionBuilderCommandlet -Builder=WorldPartitionHLODsBuilder " + BuildArgs;
		const FText OperationDescription = LOCTEXT("HLODBuildProgress", "Building HLODs...");

		int32 Result;
		bool bCancelled;
		FString CommandletOutput;
		RunCommandletAsExternalProcess(CommandletArgs, OperationDescription, Result, bCancelled, CommandletOutput);

		bool bSuccess = !bCancelled && Result == 0;
		if (bSuccess)
		{
			FMessageDialog::Open(EAppMsgType::Ok, FText::Format(LOCTEXT("HLODBuildCompleted", "HLOD build completed:\n{0}"), FText::FromString(CommandletOutput)));
			RescanAssetsAndLoadMap(InMapToProcess);
		}
		else if (bCancelled)
		{
			FMessageDialog::Open(EAppMsgType::Ok, LOCTEXT("HLODBuildCancelled", "HLOD build cancelled!"));
		}
		else if (Result != 0)
		{
			FMessageDialog::Open(EAppMsgType::Ok, FText::Format(LOCTEXT("HLODBuildFailed", "HLOD build failed:\n{0}"), FText::FromString(CommandletOutput)));
		}

		return bSuccess;
	}

	return false;
}

bool FWorldPartitionEditorModule::BuildMinimap(const FString& InMapToProcess)
{
	if (!UnloadCurrentMap(/*bAskSaveContentPackages=*/true))
	{
		return false;
	}

	const FString CommandletArgs = InMapToProcess + " -run=WorldPartitionBuilderCommandlet -Builder=WorldPartitionMinimapBuilder -AllowCommandletRendering";
	const FText OperationDescription = LOCTEXT("MinimapBuildProgress", "Building minimap...");

	int32 Result;
	bool bCancelled;
	FString CommandletOutput;
	RunCommandletAsExternalProcess(CommandletArgs, OperationDescription, Result, bCancelled, CommandletOutput);
	
	bool bSuccess = !bCancelled && Result == 0;
	if (bSuccess)
	{
		FMessageDialog::Open(EAppMsgType::Ok, FText::Format(LOCTEXT("MinimapBuildCompleted", "Minimap build completed:\n{0}"), FText::FromString(CommandletOutput)));
		RescanAssetsAndLoadMap(InMapToProcess);
	}
	else if (bCancelled)
	{
		FMessageDialog::Open(EAppMsgType::Ok, LOCTEXT("MinimapBuildCancelled", "Minimap build cancelled!"));
	}
	else if (Result != 0)
	{
		FMessageDialog::Open(EAppMsgType::Ok, FText::Format(LOCTEXT("MinimapBuildFailed", "Minimap build failed:\n{0}"), FText::FromString(CommandletOutput)));
	}

	return bSuccess;
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
