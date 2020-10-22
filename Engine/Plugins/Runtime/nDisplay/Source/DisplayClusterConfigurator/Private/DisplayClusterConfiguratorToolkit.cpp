// Copyright Epic Games, Inc. All Rights Reserved.

#include "DisplayClusterConfiguratorToolkit.h"

#include "IDisplayClusterConfiguration.h"
#include "Interfaces/IDisplayClusterConfigurator.h"
#include "DisplayClusterConfigurationTypes.h"

#include "DisplayClusterConfiguratorCommands.h"
#include "DisplayClusterConfiguratorEditor.h"
#include "DisplayClusterConfiguratorEditorData.h"
#include "DisplayClusterConfiguratorEditorSubsystem.h"
#include "DisplayClusterConfiguratorModule.h"
#include "DisplayClusterConfiguratorStyle.h"

#include "Views/General/DisplayClusterConfiguratorViewGeneral.h"
#include "Views/Details/DisplayClusterConfiguratorViewDetails.h"
#include "Views/Log/DisplayClusterConfiguratorViewLog.h"
#include "Views/OutputMapping/DisplayClusterConfiguratorViewOutputMapping.h"
#include "Views/TreeViews/Scene/DisplayClusterConfiguratorViewScene.h"
#include "Views/TreeViews/Cluster/DisplayClusterConfiguratorViewCluster.h"
#include "Views/TreeViews/Input/DisplayClusterConfiguratorViewInput.h"
#include "Views/Viewport/DisplayClusterConfiguratorViewViewport.h"

#include "ToolMenu.h"
#include "ToolMenus.h"
#include "Widgets/Docking/SDockTab.h"
#include "HAL/ConsoleManager.h"
#include "HAL/PlatformApplicationMisc.h"

#define LOCTEXT_NAMESPACE "DisplayClusterConfiguratorToolkit"


const FName FDisplayClusterConfiguratorToolkit::TabID_General			(TEXT("DisplayClusterConfiguratorTab_General"));
const FName FDisplayClusterConfiguratorToolkit::TabID_Details			(TEXT("DisplayClusterConfiguratorTab_Details"));
const FName FDisplayClusterConfiguratorToolkit::TabID_Log				(TEXT("DisplayClusterConfiguratorTab_Log"));
const FName FDisplayClusterConfiguratorToolkit::TabID_OutputMapping		(TEXT("DisplayClusterConfiguratorTab_OutputMapping"));
const FName FDisplayClusterConfiguratorToolkit::TabID_Scene				(TEXT("DisplayClusterConfiguratorTab_Scene"));
const FName FDisplayClusterConfiguratorToolkit::TabID_Cluster			(TEXT("DisplayClusterConfiguratorTab_Cluster"));
const FName FDisplayClusterConfiguratorToolkit::TabID_Input				(TEXT("DisplayClusterConfiguratorTab_Input"));
const FName FDisplayClusterConfiguratorToolkit::TabID_Viewport			(TEXT("DisplayClusterConfiguratorTab_Viewport"));

namespace
{
	static const FText ErrorLoadLatestConfigPathFromSettings(NSLOCTEXT("FDisplayClusterConfiguratorViewLog", "ErrorLoadLatestConfigPathFromSettings", "Error getting latest config path from settings"));
	static const FText ErrorImportConfig(NSLOCTEXT("FDisplayClusterConfiguratorViewLog", "ErrorImportConfig", "Error while importing the config"));
	static const FText ErrorSaveConfig(NSLOCTEXT("FDisplayClusterConfiguratorViewLog", "ErrorSaveConfig", "Error while saving the config"));
};

FDisplayClusterConfiguratorToolkit::FDisplayClusterConfiguratorToolkit(UDisplayClusterConfiguratorEditor* InAssetEditor)
	: IDisplayClusterConfiguratorToolkit(InAssetEditor)
{
	Editor = InAssetEditor;

	// All internal objects have been initialized so we can build the layout
	StandaloneDefaultLayout = BuildDefaultLayout(FString(TEXT("DisplayClusterConfigurator_v004")));
}

TSharedPtr<FTabManager::FLayout> FDisplayClusterConfiguratorToolkit::BuildDefaultLayout(const FString& LayoutName)
{
	return FTabManager::NewLayout(FName(LayoutName))
		->AddArea
		(
			FTabManager::NewPrimaryArea()->SetOrientation(Orient_Vertical)
			->Split
			(
				// Toolbar
				FTabManager::NewStack()
				->SetSizeCoefficient(0.1f)
				->AddTab(GetToolbarTabId(), ETabState::OpenedTab)
				->SetHideTabWell(true)
			)
			->Split
			(
				// Main canvas
				FTabManager::NewSplitter()->SetOrientation(Orient_Horizontal)

				// Tree
				->Split
				(
					FTabManager::NewSplitter()
					->SetSizeCoefficient(.2f)
					->SetOrientation(Orient_Vertical)
					->Split
					(
						FTabManager::NewStack()
						->SetSizeCoefficient(.3f)
						->AddTab(TabID_Cluster, ETabState::OpenedTab)
						->SetHideTabWell(false)
						)
					->Split
					(
						FTabManager::NewStack()
						->SetSizeCoefficient(.4f)
						->AddTab(TabID_Scene, ETabState::OpenedTab)
						->SetHideTabWell(false)
					)
					->Split
					(
						FTabManager::NewStack()
						->SetSizeCoefficient(.3f)
						->AddTab(TabID_Input, ETabState::OpenedTab)
						->SetHideTabWell(false)
					)
				)
				// Viewport and OutputMapping
				->Split
				(
					FTabManager::NewSplitter()
					->SetOrientation(Orient_Vertical)
					->SetSizeCoefficient(.6f)
					->Split
					(
						FTabManager::NewStack()
						->AddTab(TabID_Viewport, ETabState::OpenedTab)
						->SetSizeCoefficient(0.7f)
						->SetHideTabWell(false)
					)
					->Split
					(
						FTabManager::NewStack()
						->AddTab(TabID_OutputMapping, ETabState::OpenedTab)
						->SetSizeCoefficient(0.3f)
						->SetHideTabWell(false)
					)
				)
				// Details and Log
				->Split
				(
					FTabManager::NewSplitter()
					->SetOrientation(Orient_Vertical)
					->SetSizeCoefficient(.2f)
					->Split
					(
						FTabManager::NewStack()
						->SetSizeCoefficient(.7f)
						->AddTab(TabID_Details, ETabState::OpenedTab)
						->AddTab(TabID_General, ETabState::OpenedTab)
						->SetForegroundTab(TabID_Details)
						->SetHideTabWell(false)
					)
					->Split
					(
						FTabManager::NewStack()
						->SetSizeCoefficient(.3f)
						->AddTab(TabID_Log, ETabState::OpenedTab)
						->SetHideTabWell(true)
					)
				)
			)
		);
}


UDisplayClusterConfiguratorEditorData* FDisplayClusterConfiguratorToolkit::GetEditorData() const
{
	if (Editor != nullptr)
	{
		return Editor->GetEditingObject();
	}

	return nullptr;
}

FDelegateHandle FDisplayClusterConfiguratorToolkit::RegisterOnConfigReloaded(const FOnConfigReloadedDelegate& Delegate)
{
	return OnConfigReloaded.Add(Delegate);
}

void FDisplayClusterConfiguratorToolkit::UnregisterOnConfigReloaded(FDelegateHandle DelegateHandle)
{
	OnConfigReloaded.Remove(DelegateHandle);
}

FDelegateHandle FDisplayClusterConfiguratorToolkit::RegisterOnObjectSelected(const FOnObjectSelectedDelegate& Delegate)
{
	return OnObjectSelected.Add(Delegate);
}

void FDisplayClusterConfiguratorToolkit::UnregisterOnObjectSelected(FDelegateHandle DelegateHandle)
{
	OnObjectSelected.Remove(DelegateHandle);
}

FDelegateHandle FDisplayClusterConfiguratorToolkit::RegisterOnInvalidateViews(const FOnInvalidateViewsDelegate& Delegate)
{
	return OnInvalidateViews.Add(Delegate);
}

void FDisplayClusterConfiguratorToolkit::UnregisterOnInvalidateViews(FDelegateHandle DelegateHandle)
{
	OnInvalidateViews.Remove(DelegateHandle);
}

const TArray<UObject*>& FDisplayClusterConfiguratorToolkit::GetSelectedObjects() const
{
	return SelectedObjects;
}

void FDisplayClusterConfiguratorToolkit::SelectObjects(TArray<UObject*>& InSelectedObjects)
{
	SelectedObjects = InSelectedObjects;

	OnObjectSelected.Broadcast();

}

void FDisplayClusterConfiguratorToolkit::InvalidateViews()
{
	OnInvalidateViews.Broadcast();
}

void FDisplayClusterConfiguratorToolkit::ClearViewportSelection()
{
	OnClearViewportSelection.Broadcast();
}

UDisplayClusterConfigurationData* FDisplayClusterConfiguratorToolkit::GetConfig() const
{
	if (UDisplayClusterConfiguratorEditorData* EditorData = GetEditorData())
	{
		return EditorData->nDisplayConfig;
	}

	return nullptr;
}

TSharedRef<IDisplayClusterConfiguratorViewOutputMapping> FDisplayClusterConfiguratorToolkit::GetViewOutputMapping() const
{
	return ViewOutputMapping.ToSharedRef();
}

TSharedRef<IDisplayClusterConfiguratorViewTree> FDisplayClusterConfiguratorToolkit::GetViewCluster() const
{
	return ViewCluster.ToSharedRef();
}

TSharedRef<IDisplayClusterConfiguratorViewTree> FDisplayClusterConfiguratorToolkit::GetViewScene() const
{
	return ViewScene.ToSharedRef();
}

TSharedRef<IDisplayClusterConfiguratorViewTree> FDisplayClusterConfiguratorToolkit::GetViewInput() const
{
	return ViewInput.ToSharedRef();
}

TSharedRef<IDisplayClusterConfiguratorViewViewport> FDisplayClusterConfiguratorToolkit::GetViewViewport() const
{
	return ViewViewport.ToSharedRef();
}

TSharedRef<IDisplayClusterConfiguratorViewLog> FDisplayClusterConfiguratorToolkit::GetViewLog() const
{
	return ViewLog.ToSharedRef();
}

TSharedRef<IDisplayClusterConfiguratorViewDetails> FDisplayClusterConfiguratorToolkit::GetViewDetails() const
{
	return ViewDetails.ToSharedRef();
}

TSharedRef<IDisplayClusterConfiguratorView> FDisplayClusterConfiguratorToolkit::GetViewGeneral() const
{
	return ViewGeneral.ToSharedRef();
}

void FDisplayClusterConfiguratorToolkit::OnReadOnlyChanged(bool bReadOnly)
{
	ViewGeneral->SetEnabled(!bReadOnly);
	ViewDetails->SetEnabled(!bReadOnly);
	ViewOutputMapping->SetEnabled(!bReadOnly);
	ViewCluster->SetEnabled(!bReadOnly);
	ViewScene->SetEnabled(!bReadOnly);
	ViewInput->SetEnabled(!bReadOnly);
}

void FDisplayClusterConfiguratorToolkit::BindCommands()
{
	const FDisplayClusterConfiguratorCommands& Commands = IDisplayClusterConfigurator::Get().GetCommands();

	ToolkitCommands->MapAction(Commands.Import, FExecuteAction::CreateSP(this, &FDisplayClusterConfiguratorToolkit::ImportConfig_Clicked));
	ToolkitCommands->MapAction(Commands.SaveToFile, FExecuteAction::CreateSP(this, &FDisplayClusterConfiguratorToolkit::SaveToFile_Clicked));
	ToolkitCommands->MapAction(Commands.EditConfig, FExecuteAction::CreateSP(this, &FDisplayClusterConfiguratorToolkit::EditConfig_Clicked));
}

void FDisplayClusterConfiguratorToolkit::RegisterToolbar()
{
	const FDisplayClusterConfiguratorCommands& Commands = IDisplayClusterConfigurator::Get().GetCommands();

	const FName MenuName = GetToolMenuToolbarName();

	// Cleanup before we test re-registering menus
	UToolMenus::Get()->RemoveMenu(MenuName);

	// Setup an owner for the current scope so we can cleanup
	FToolMenuOwnerScoped Owner(MenuName);

	UToolMenu* ToolBar = UToolMenus::Get()->RegisterMenu(MenuName, "AssetEditor.DefaultToolBar", EMultiBoxType::ToolBar);

	// Extend toolbar
	FToolMenuInsert InsertAfterAssetSection("Asset", EToolMenuInsertType::After);
	{
		FToolMenuSection& Section = ToolBar->AddSection("nDisplay", TAttribute<FText>(), InsertAfterAssetSection);
		Section.AddEntry(FToolMenuEntry::InitToolBarButton(
			Commands.Import,
			TAttribute<FText>(),
			TAttribute<FText>(),
			FSlateIcon(FDisplayClusterConfiguratorStyle::GetStyleSetName(), "DisplayClusterConfigurator.Toolbar.Import")
			));

		Section.AddEntry(FToolMenuEntry::InitToolBarButton(
			Commands.SaveToFile,
			TAttribute<FText>(),
			TAttribute<FText>(),
			FSlateIcon(FDisplayClusterConfiguratorStyle::GetStyleSetName(), "DisplayClusterConfigurator.Toolbar.SaveToFile")
			));

		Section.AddEntry(FToolMenuEntry::InitToolBarButton(
			Commands.EditConfig,
			TAttribute<FText>(),
			TAttribute<FText>(),
			FSlateIcon(FDisplayClusterConfiguratorStyle::GetStyleSetName(), "DisplayClusterConfigurator.Toolbar.EditConfig")
			));
	}
}

void FDisplayClusterConfiguratorToolkit::CreateWidgets()
{
	TSharedRef<FDisplayClusterConfiguratorToolkit> ThisRef(SharedThis(this));

	ViewGeneral			= MakeShared<FDisplayClusterConfiguratorViewGeneral>(ThisRef);
	ViewDetails			= MakeShared<FDisplayClusterConfiguratorViewDetails>(ThisRef);
	ViewLog				= MakeShared<FDisplayClusterConfiguratorViewLog>(ThisRef);
	ViewOutputMapping	= MakeShared<FDisplayClusterConfiguratorViewOutputMapping>(ThisRef);
	ViewCluster			= MakeShared<FDisplayClusterConfiguratorViewCluster>(ThisRef);
	ViewScene			= MakeShared<FDisplayClusterConfiguratorViewScene>(ThisRef);
	ViewInput			= MakeShared<FDisplayClusterConfiguratorViewInput>(ThisRef);
	ViewViewport		= MakeShared<FDisplayClusterConfiguratorViewViewport>(ThisRef);

	ViewGeneral->CreateWidget();
	ViewDetails->CreateWidget();
	ViewLog->CreateWidget();
	ViewOutputMapping->CreateWidget();
	ViewCluster->CreateWidget();
	ViewScene->CreateWidget();
	ViewInput->CreateWidget();
	ViewViewport->CreateWidget();

	BindCommands();
	RegisterToolbar();

	// Register delegates
	FDisplayClusterConfiguratorModule::RegisterOnReadOnly(FOnDisplayClusterConfiguratorReadOnlyChangedDelegate::CreateSP(this, &FDisplayClusterConfiguratorToolkit::OnReadOnlyChanged));

	// Set the visibility
	{
		bool bReadOnly = FConsoleManager::Get().FindConsoleVariable(TEXT("nDisplay.configurator.ReadOnly"))->GetBool();

		ViewGeneral->SetEnabled(!bReadOnly);
		ViewDetails->SetEnabled(!bReadOnly);
		ViewOutputMapping->SetEnabled(!bReadOnly);
		ViewCluster->SetEnabled(!bReadOnly);
		ViewScene->SetEnabled(!bReadOnly);
		ViewInput->SetEnabled(!bReadOnly);
	}

	// Refresh widgets of menus and toolbars being displayed right now
	UToolMenus::Get()->RefreshAllWidgets();

	OnConfigReloaded.Broadcast();

	// Add log
	UDisplayClusterConfiguratorEditorData* EditorData = GetEditorData();
	check(EditorData != nullptr);
	ViewLog->Log(FText::Format(NSLOCTEXT("FDisplayClusterConfiguratorViewLog", "SuccessLoadFromFile", "Successfully load latest with the path: {0}"), FText::FromString(EditorData->PathToConfig)));
}

void FDisplayClusterConfiguratorToolkit::RegisterTabSpawners(const TSharedRef<FTabManager>& InTabManager)
{
	WorkspaceMenuCategory = InTabManager->AddLocalWorkspaceMenuCategory(LOCTEXT("WorkspaceMenu_DisplayClusterConfiguratorEditor", "Display Cluster Editor"));

	FBaseAssetToolkit::RegisterTabSpawners(InTabManager);

	InTabManager->UnregisterTabSpawner(DetailsTabID);

	InTabManager->RegisterTabSpawner(TabID_General, FOnSpawnTab::CreateSP(this, &FDisplayClusterConfiguratorToolkit::SpawnTab_General))
		.SetDisplayName(LOCTEXT("GeneralTabTitle", "General"))
		.SetGroup(WorkspaceMenuCategory.ToSharedRef())
		.SetIcon(FSlateIcon(FDisplayClusterConfiguratorStyle::GetStyleSetName(), "DisplayClusterConfigurator.Tabs.General"));

	InTabManager->RegisterTabSpawner(TabID_Details, FOnSpawnTab::CreateSP(this, &FDisplayClusterConfiguratorToolkit::SpawnTab_Details))
		.SetDisplayName(LOCTEXT("DetailsTabTitle", "Details"))
		.SetGroup(WorkspaceMenuCategory.ToSharedRef())
		.SetIcon(FSlateIcon(FDisplayClusterConfiguratorStyle::GetStyleSetName(), "DisplayClusterConfigurator.Tabs.Details"));

	InTabManager->RegisterTabSpawner(TabID_Log, FOnSpawnTab::CreateSP(this, &FDisplayClusterConfiguratorToolkit::SpawnTab_Log))
		.SetDisplayName(LOCTEXT("LogTabTitle", "Log"))
		.SetIcon(FSlateIcon(FDisplayClusterConfiguratorStyle::GetStyleSetName(), "DisplayClusterConfigurator.Tabs.Log"))
		.SetGroup(WorkspaceMenuCategory.ToSharedRef());

	InTabManager->RegisterTabSpawner(TabID_OutputMapping, FOnSpawnTab::CreateSP(this, &FDisplayClusterConfiguratorToolkit::SpawnTab_OutputMapping))
		.SetDisplayName(LOCTEXT("OutputMappingTabTitle", "Output Mapping"))
		.SetGroup(WorkspaceMenuCategory.ToSharedRef())
		.SetIcon(FSlateIcon(FDisplayClusterConfiguratorStyle::GetStyleSetName(), "DisplayClusterConfigurator.Tabs.OutputMapping"));

	InTabManager->RegisterTabSpawner(TabID_Scene, FOnSpawnTab::CreateSP(this, &FDisplayClusterConfiguratorToolkit::SpawnTab_Scene))
		.SetDisplayName(LOCTEXT("SceneTabTitle", "Scene"))
		.SetIcon(FSlateIcon(FDisplayClusterConfiguratorStyle::GetStyleSetName(), "DisplayClusterConfigurator.Tabs.Scene"))
		.SetGroup(WorkspaceMenuCategory.ToSharedRef());

	InTabManager->RegisterTabSpawner(TabID_Cluster, FOnSpawnTab::CreateSP(this, &FDisplayClusterConfiguratorToolkit::SpawnTab_Cluster))
		.SetDisplayName(LOCTEXT("ClusterTabTitle", "Cluster"))
		.SetIcon(FSlateIcon(FDisplayClusterConfiguratorStyle::GetStyleSetName(), "DisplayClusterConfigurator.Tabs.Cluster"))
		.SetGroup(WorkspaceMenuCategory.ToSharedRef());

	InTabManager->RegisterTabSpawner(TabID_Input, FOnSpawnTab::CreateSP(this, &FDisplayClusterConfiguratorToolkit::SpawnTab_Input))
		.SetDisplayName(LOCTEXT("InputTabTitle", "Input"))
		.SetIcon(FSlateIcon(FDisplayClusterConfiguratorStyle::GetStyleSetName(), "DisplayClusterConfigurator.Tabs.Input"))
		.SetGroup(WorkspaceMenuCategory.ToSharedRef());

	InTabManager->UnregisterTabSpawner(ViewportTabID);
	InTabManager->RegisterTabSpawner(TabID_Viewport, FOnSpawnTab::CreateSP(this, &FDisplayClusterConfiguratorToolkit::SpawnTab_Viewport))
		.SetDisplayName(LOCTEXT("ViewportTabTitle", "Viewport"))
		.SetGroup(WorkspaceMenuCategory.ToSharedRef())
		.SetIcon(FSlateIcon(FDisplayClusterConfiguratorStyle::GetStyleSetName(), "DisplayClusterConfigurator.Tabs.Viewport"));
}

void FDisplayClusterConfiguratorToolkit::SaveAsset_Execute()
{
	Editor->Save();
	FAssetEditorToolkit::SaveAsset_Execute();

	// Add log
	UDisplayClusterConfiguratorEditorData* EditorData = GetEditorData();
	check(EditorData != nullptr);
	ViewLog->Log(FText::Format(NSLOCTEXT("FDisplayClusterConfiguratorViewLog", "SuccessSaveToFile", "Successfully saved with the path: {0}"), FText::FromString(EditorData->PathToConfig)));
}

TSharedRef<SDockTab> FDisplayClusterConfiguratorToolkit::SpawnTab_General(const FSpawnTabArgs& Args)
{
	TSharedPtr<SDockTab> DetailsTab = SNew(SDockTab)
		.Icon(FDisplayClusterConfiguratorStyle::GetBrush("DisplayClusterConfigurator.Tabs.General"))
		.Label(LOCTEXT("TabGeneral", "General"))
		[
			ViewGeneral->CreateWidget()
		];

	return DetailsTab.ToSharedRef();
}

TSharedRef<SDockTab> FDisplayClusterConfiguratorToolkit::SpawnTab_Details(const FSpawnTabArgs& Args)
{
	TSharedPtr<SDockTab> DetailsTab = SNew(SDockTab)
		.Icon(FDisplayClusterConfiguratorStyle::GetBrush("DisplayClusterConfigurator.Tabs.Details"))
		.Label(LOCTEXT("TabDetails", "Details"))
		[
			ViewDetails->CreateWidget()
		];

	return DetailsTab.ToSharedRef();
}

TSharedRef<SDockTab> FDisplayClusterConfiguratorToolkit::SpawnTab_Log(const FSpawnTabArgs& Args)
{
	TSharedPtr<SDockTab> LogTab = SNew(SDockTab)
		.Icon(FDisplayClusterConfiguratorStyle::GetBrush("DisplayClusterConfigurator.Tabs.Log"))
		.Label(LOCTEXT("TabLog", "Log"))
		[
			ViewLog->CreateWidget()
		];

	return LogTab.ToSharedRef();
}

TSharedRef<SDockTab> FDisplayClusterConfiguratorToolkit::SpawnTab_OutputMapping(const FSpawnTabArgs& Args)
{
	TSharedPtr<SDockTab> OutputMappingTab = SNew(SDockTab)
		.Icon(FDisplayClusterConfiguratorStyle::GetBrush("DisplayClusterConfigurator.Tabs.OutputMapping"))
		.Label(LOCTEXT("TabOutputMapping", "Output Mapping"))
		[
			ViewOutputMapping->CreateWidget()
		];

	return OutputMappingTab.ToSharedRef();
}

TSharedRef<SDockTab> FDisplayClusterConfiguratorToolkit::SpawnTab_Scene(const FSpawnTabArgs& Args)
{
	TSharedPtr<SDockTab> TreeTab = SNew(SDockTab)
		.Icon(FDisplayClusterConfiguratorStyle::GetBrush("DisplayClusterConfigurator.Tabs.Scene"))
		.Label(LOCTEXT("TabConfigScene", "Scene"))
		[
			ViewScene->CreateWidget()
		];

	return TreeTab.ToSharedRef();
}

TSharedRef<SDockTab> FDisplayClusterConfiguratorToolkit::SpawnTab_Cluster(const FSpawnTabArgs& Args)
{
	TSharedPtr<SDockTab> TreeTab = SNew(SDockTab)
		.Icon(FDisplayClusterConfiguratorStyle::GetBrush("DisplayClusterConfigurator.Tabs.Cluster"))
		.Label(LOCTEXT("TabConfigCluster", "Cluster"))
		[
			ViewCluster->CreateWidget()
		];

	return TreeTab.ToSharedRef();
}

TSharedRef<SDockTab> FDisplayClusterConfiguratorToolkit::SpawnTab_Input(const FSpawnTabArgs& Args)
{
	TSharedPtr<SDockTab> TreeTab = SNew(SDockTab)
		.Icon(FDisplayClusterConfiguratorStyle::GetBrush("DisplayClusterConfigurator.Tabs.Input"))
		.Label(LOCTEXT("TabConfigInput", "Input"))
		[
			ViewInput->CreateWidget()
		];

	return TreeTab.ToSharedRef();
}


TSharedRef<SDockTab> FDisplayClusterConfiguratorToolkit::SpawnTab_Viewport(const FSpawnTabArgs& Args)
{
	TSharedPtr<SDockTab> ViewportTab = SNew(SDockTab)
		.Icon(FDisplayClusterConfiguratorStyle::GetBrush("DisplayClusterConfigurator.Tabs.Viewport"))
		.Label(LOCTEXT("TabViewport", "Viewport"))
		[
			ViewViewport->CreateWidget()
		];

	return ViewportTab.ToSharedRef();
}

void FDisplayClusterConfiguratorToolkit::ImportConfig_Clicked()
{
	if (Editor->LoadWithOpenFileDialog())
	{
		OnConfigReloaded.Broadcast();

		// Add log
		UDisplayClusterConfiguratorEditorData* EditorData = GetEditorData();
		check(EditorData != nullptr);
		ViewLog->Log(FText::Format(NSLOCTEXT("FDisplayClusterConfiguratorViewLog", "SuccessImportFromFile", "Successfully imported with the path: {0}"), FText::FromString(EditorData->PathToConfig)));
	}
	else
	{
		ViewLog->Log(ErrorImportConfig);
	}
}

void FDisplayClusterConfiguratorToolkit::SaveToFile_Clicked()
{
	if (Editor->SaveWithOpenFileDialog())
	{
		// Add log
		UDisplayClusterConfiguratorEditorData* EditorData = GetEditorData();
		check(EditorData != nullptr);
		ViewLog->Log(FText::Format(NSLOCTEXT("FDisplayClusterConfiguratorViewLog", "SuccessSaveToFile", "Successfully saved with the path: {0}"), FText::FromString(EditorData->PathToConfig)));
	}
	else
	{
		ViewLog->Log(ErrorSaveConfig);
	}
}

void FDisplayClusterConfiguratorToolkit::EditConfig_Clicked()
{
	UDisplayClusterConfiguratorEditorData* EditorData = GetEditorData();
	FPlatformProcess::LaunchFileInDefaultExternalApplication(*EditorData->PathToConfig, NULL, ELaunchVerb::Edit);
}

void FDisplayClusterConfiguratorToolkit::Tick(float DeltaTime)
{
}

TStatId FDisplayClusterConfiguratorToolkit::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(FDisplayClusterConfiguratorToolkit, STATGROUP_Tickables);
}

#undef LOCTEXT_NAMESPACE
