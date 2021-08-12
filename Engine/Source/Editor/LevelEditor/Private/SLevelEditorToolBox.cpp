// Copyright Epic Games, Inc. All Rights Reserved.

#include "SLevelEditorToolBox.h"
#include "Framework/MultiBox/MultiBoxDefs.h"
#include "Widgets/Text/STextBlock.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Framework/Commands/InputBindingManager.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "Modules/ModuleManager.h"
#include "Widgets/Layout/SBorder.h"
#include "EditorStyleSet.h"
#include "Editor/EditorPerProjectUserSettings.h"
#include "LevelEditor.h"
#include "LevelEditorActions.h"
#include "LevelEditorModesActions.h"
#include "Classes/EditorStyleSettings.h"
#include "EdMode.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/Layout/SUniformWrapPanel.h"
#include "StatusBarSubsystem.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SExpandableArea.h"
#include "WorkspaceMenuStructure.h"
#include "WorkspaceMenuStructureModule.h"
#include <Misc/Attribute.h>

#define LOCTEXT_NAMESPACE "SLevelEditorToolBox"

FLevelEditorModeUILayer::FLevelEditorModeUILayer(const IToolkitHost* InToolkitHost)
	: FAssetEditorModeUILayer(InToolkitHost)
{
	FLevelEditorModule& LevelEditorModule = FModuleManager::GetModuleChecked<FLevelEditorModule>("LevelEditor");
	LevelEditorModule.OnRegisterLayoutExtensions().AddRaw(this, &FLevelEditorModeUILayer::RegisterLayoutExtensions);
}


FLevelEditorModeUILayer::~FLevelEditorModeUILayer()
{
	FLevelEditorModule& LevelEditorModule = FModuleManager::GetModuleChecked<FLevelEditorModule>("LevelEditor");
	LevelEditorModule.OnRegisterLayoutExtensions().RemoveAll(this);
}

void FLevelEditorModeUILayer::OnToolkitHostingStarted(const TSharedRef<IToolkit>& Toolkit)
{
	if (!Toolkit->IsAssetEditor())
	{
		FAssetEditorModeUILayer::OnToolkitHostingStarted(Toolkit);
		HostedToolkit = Toolkit;
		Toolkit->SetModeUILayer(SharedThis(this));
		Toolkit->RegisterTabSpawners(ToolkitHost->GetTabManager().ToSharedRef());
		RegisterModeTabSpawners();
		OnToolkitHostReadyForUI.ExecuteIfBound();
	}

}

void FLevelEditorModeUILayer::OnToolkitHostingFinished(const TSharedRef<IToolkit>& Toolkit)
{
	if (HostedToolkit.IsValid() && HostedToolkit.Pin() == Toolkit)
	{
		FAssetEditorModeUILayer::OnToolkitHostingFinished(Toolkit);
	}
}



void FLevelEditorModeUILayer::RegisterLayoutExtensions(FLayoutExtender& Extender)
{
	Extender.ExtendLayout(LevelEditorTabIds::PlacementBrowser, ELayoutExtensionPosition::Before, FTabManager::FTab(TopLeftTabID, ETabState::ClosedTab));
	Extender.ExtendStack("BottomLeftPanel", ELayoutExtensionPosition::Before, FTabManager::FTab(BottomLeftTabID, ETabState::ClosedTab));
	Extender.ExtendStack("VerticalToolbar", ELayoutExtensionPosition::Before, FTabManager::FTab(VerticalToolbarID, ETabState::ClosedTab));
	Extender.ExtendLayout(LevelEditorTabIds::LevelEditorSceneOutliner, ELayoutExtensionPosition::Before, FTabManager::FTab(TopRightTabID, ETabState::ClosedTab));
	Extender.ExtendLayout(LevelEditorTabIds::LevelEditorSelectionDetails, ELayoutExtensionPosition::Before, FTabManager::FTab(BottomRightTabID, ETabState::ClosedTab));
}




TSharedPtr<FWorkspaceItem> FLevelEditorModeUILayer::GetModeMenuCategory()
{
	const IWorkspaceMenuStructure& MenuStructure = WorkspaceMenu::GetMenuStructure();
	return MenuStructure.GetLevelEditorModesCategory();
}


#undef LOCTEXT_NAMESPACE

