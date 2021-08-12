// Copyright Epic Games, Inc. All Rights Reserved.

#include "Toolkits/AssetEditorModeUILayer.h"
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

#define LOCTEXT_NAMESPACE "SLevelEditorToolBox"

const FName FAssetEditorModeUILayer::TopLeftTabID(TEXT("TopLeftModeTab"));
const FName FAssetEditorModeUILayer::BottomLeftTabID(TEXT("BottomLeftModeTab"));
const FName FAssetEditorModeUILayer::TopRightTabID(TEXT("TopRightModeTab"));
const FName FAssetEditorModeUILayer::BottomRightTabID(TEXT("BottomRightModeTab"));
const FName FAssetEditorModeUILayer::VerticalToolbarID = TEXT("VerticalModeToolbar");

FAssetEditorModeUILayer::FAssetEditorModeUILayer(const IToolkitHost* InToolkitHost)
	: ToolkitHost(InToolkitHost)
{
	RequestedTabInfo.Add(VerticalToolbarID, FMinorTabConfig(VerticalToolbarID));
	RequestedTabInfo.Add(TopLeftTabID, FMinorTabConfig(TopLeftTabID));
	RequestedTabInfo.Add(BottomLeftTabID, FMinorTabConfig(BottomLeftTabID));
	RequestedTabInfo.Add(TopRightTabID, FMinorTabConfig(TopRightTabID));
	RequestedTabInfo.Add(BottomRightTabID, FMinorTabConfig(BottomRightTabID));
}

void FAssetEditorModeUILayer::OnToolkitHostingStarted(const TSharedRef<IToolkit>& Toolkit)
{
	GetTabManager()->UnregisterTabSpawner(TopLeftTabID);
	GetTabManager()->UnregisterTabSpawner(BottomLeftTabID);
	GetTabManager()->UnregisterTabSpawner(VerticalToolbarID);
	GetTabManager()->UnregisterTabSpawner(TopRightTabID);
	GetTabManager()->UnregisterTabSpawner(BottomRightTabID);

}

void FAssetEditorModeUILayer::OnToolkitHostingFinished(const TSharedRef<IToolkit>& Toolkit)
{
	if (HostedToolkit.IsValid() && HostedToolkit.Pin() == Toolkit)
	{
		for (TPair<FName, FMinorTabConfig>& TabSpawnerInfo : RequestedTabInfo)
		{
			TabSpawnerInfo.Value = FMinorTabConfig(TabSpawnerInfo.Key);
		}
		for (TPair<FName, TWeakPtr<SDockTab>>& SpawnedTab : SpawnedTabs)
		{
			if (SpawnedTab.Value.IsValid())
			{
				SpawnedTab.Value.Pin()->SetContent(SNullWidget::NullWidget);
				SpawnedTab.Value.Pin()->RequestCloseTab();
			}
		}

		SpawnedTabs.Empty();
	}
}

TSharedPtr<FTabManager> FAssetEditorModeUILayer::GetTabManager()
{
	return ToolkitHost ? ToolkitHost->GetTabManager() : TSharedPtr<FTabManager>();
}


TSharedPtr<FWorkspaceItem> FAssetEditorModeUILayer::GetModeMenuCategory()
{
	return TSharedPtr<FWorkspaceItem>();
}

void FAssetEditorModeUILayer::RegisterModeTabSpawners()
{
	RegisterModeTabSpawner(TopLeftTabID);
	RegisterModeTabSpawner(BottomLeftTabID);
	RegisterModeTabSpawner(VerticalToolbarID);
	RegisterModeTabSpawner(TopRightTabID);
	RegisterModeTabSpawner(BottomRightTabID);

}

void FAssetEditorModeUILayer::RegisterModeTabSpawner(const FName TabID)
{
	TSharedRef<FWorkspaceItem> MenuGroup = GetModeMenuCategory().ToSharedRef();
	bool bShowMenuOption = GetStoredSpawner(TabID).IsBound();
	GetTabManager()->RegisterTabSpawner(TabID,
		FOnSpawnTab::CreateSP(this, &FAssetEditorModeUILayer::SpawnStoredTab, TabID),
		FCanSpawnTab::CreateSP(this, &FAssetEditorModeUILayer::CanSpawnStoredTab, TabID))
		.SetDisplayNameAttribute(MakeAttributeSP(this, &FAssetEditorModeUILayer::GetTabSpawnerName, TabID))
		.SetTooltipTextAttribute(MakeAttributeSP(this, &FAssetEditorModeUILayer::GetTabSpawnerTooltip, TabID))
		.SetAutoGenerateMenuEntry(bShowMenuOption)
		.SetGroup(MenuGroup);
}

void FAssetEditorModeUILayer::SetModePanelInfo(const FName InTabSpawnerID, const FMinorTabConfig& InTabInfo)
{
	RequestedTabInfo.Emplace(InTabSpawnerID, InTabInfo);
}

TMap<FName, TWeakPtr<SDockTab>> FAssetEditorModeUILayer::GetSpawnedTabs()
{
	return SpawnedTabs;
}

const FOnSpawnTab& FAssetEditorModeUILayer::GetStoredSpawner(const FName TabID)
{
	return RequestedTabInfo[TabID].OnSpawnTab;
}

TSharedRef<SDockTab> FAssetEditorModeUILayer::SpawnStoredTab(const FSpawnTabArgs& Args, const FName TabID)
{
	TSharedRef<SDockTab> SpawnedTab = SNew(SDockTab);
	if (GetStoredSpawner(TabID).IsBound())
	{
		FOnSpawnTab& StoredTabSpawner = RequestedTabInfo[TabID].OnSpawnTab;
		SpawnedTab = StoredTabSpawner.Execute(Args);
	}
	SpawnedTabs.Emplace(TabID, SpawnedTab);
	return SpawnedTab;
}

bool FAssetEditorModeUILayer::CanSpawnStoredTab(const FSpawnTabArgs& Args, const FName TabID)
{
	bool bCanSpawnTab = GetStoredSpawner(TabID).IsBound();
	if (RequestedTabInfo[TabID].CanSpawnTab.IsBound())
	{
		bCanSpawnTab |= RequestedTabInfo[TabID].CanSpawnTab.Execute(Args);
	}
	return bCanSpawnTab;
}

FText FAssetEditorModeUILayer::GetTabSpawnerName(const FName TabID) const
{
	if (!RequestedTabInfo[TabID].TabLabel.IsEmpty())
	{
		return RequestedTabInfo[TabID].TabLabel;
	}
	return FText::GetEmpty();
}

FText FAssetEditorModeUILayer::GetTabSpawnerTooltip(const FName TabID) const
{
	if (!RequestedTabInfo[TabID].TabTooltip.IsEmpty())
	{
		return RequestedTabInfo[TabID].TabTooltip;
	}
	return FText::GetEmpty();
}

#undef LOCTEXT_NAMESPACE

