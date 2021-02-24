// Copyright Epic Games, Inc. All Rights Reserved.

#include "RemoteControlPresetEditorToolkit.h"
#include "RemoteControlPreset.h"
#include "RemoteControlUIModule.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/SWidget.h"
#include "Framework/Docking/TabManager.h"
#include "UI/SRCPanelExposedEntitiesList.h"
#include "UI/SRCPanelInputBindings.h"
#include "UI/SRCPanelTreeNode.h"
#include "UI/SRemoteControlPanel.h"

#define LOCTEXT_NAMESPACE "RemoteControlPresetEditorToolkit"

const FName FRemoteControlPresetEditorToolkit::PanelTabId(TEXT("RemoteControl_RemoteControlPanel"));
const FName FRemoteControlPresetEditorToolkit::InputBindingsTabId(TEXT("RemoteControl_InputBindings"));
const FName FRemoteControlPresetEditorToolkit::RemoteControlPanelAppIdentifier(TEXT("RemoteControlPanel"));

TSharedRef<FRemoteControlPresetEditorToolkit> FRemoteControlPresetEditorToolkit::CreateEditor(const EToolkitMode::Type Mode, const TSharedPtr<class IToolkitHost>& InitToolkitHost, URemoteControlPreset* InPreset)
{
	TSharedRef<FRemoteControlPresetEditorToolkit> NewEditor = MakeShared<FRemoteControlPresetEditorToolkit>();

	NewEditor->InitRemoteControlPresetEditor(Mode, InitToolkitHost, InPreset);

	return NewEditor;
}

void FRemoteControlPresetEditorToolkit::InitRemoteControlPresetEditor(const EToolkitMode::Type Mode, const TSharedPtr<class IToolkitHost> & InitToolkitHost, URemoteControlPreset* InPreset)
{
	Preset = InPreset;

	PanelTab = FRemoteControlUIModule::Get().CreateRemoteControlPanel(InPreset);
	InputBindingsTab = FRemoteControlUIModule::Get().CreateInputBindingsPanel(InPreset);
	
	PanelTab->GetEntityList()->OnSelectionChange().BindRaw(this, &FRemoteControlPresetEditorToolkit::OnPanelSelectionChange);
	InputBindingsTab->GetEntityList()->OnSelectionChange().BindRaw(this, &FRemoteControlPresetEditorToolkit::OnInputBindingsSelectionChange);

	const TSharedRef<FTabManager::FLayout> StandaloneDefaultLayout = FTabManager::NewLayout("Standalone_RemoteControlPresetEditor")
		->AddArea
		(
			FTabManager::NewPrimaryArea()
			->Split
			(
				FTabManager::NewStack()
				->AddTab(PanelTabId, ETabState::OpenedTab)
				->AddTab(InputBindingsTabId, ETabState::OpenedTab)
			)
		);

	constexpr bool bCreateDefaultStandaloneMenu = true;
	constexpr bool bCreateDefaultToolbar = true;

	FAssetEditorToolkit::InitAssetEditor(Mode, InitToolkitHost, RemoteControlPanelAppIdentifier, StandaloneDefaultLayout, bCreateDefaultStandaloneMenu, bCreateDefaultStandaloneMenu, InPreset);
}

void FRemoteControlPresetEditorToolkit::RegisterTabSpawners(const TSharedRef<class FTabManager>& InTabManager)
{
	WorkspaceMenuCategory = InTabManager->AddLocalWorkspaceMenuCategory(LOCTEXT("WorkspaceMenu_RemoteControlPanel", "Remote Control Panel"));

	InTabManager->RegisterTabSpawner(PanelTabId, FOnSpawnTab::CreateSP(this, &FRemoteControlPresetEditorToolkit::HandleTabManagerSpawnPanelTab))
		.SetDisplayName(LOCTEXT("RemoteControlPanelMainTab", "Remote Control Panel"))
		.SetGroup(WorkspaceMenuCategory.ToSharedRef())
		.SetIcon(FSlateIcon(FEditorStyle::GetStyleSetName(), "LevelEditor.GameSettings.Small"));

	InTabManager->RegisterTabSpawner(InputBindingsTabId, FOnSpawnTab::CreateSP(this, &FRemoteControlPresetEditorToolkit::HandleTabManagerSpawnInputBindingsTab))
		.SetDisplayName(LOCTEXT("RemoteControlPanelInputBindings", "Input Bindings"))
		.SetGroup(WorkspaceMenuCategory.ToSharedRef())
		.SetIcon(FSlateIcon(FEditorStyle::GetStyleSetName(), "LevelEditor.GameSettings.Small"));
}

void FRemoteControlPresetEditorToolkit::UnregisterTabSpawners(const TSharedRef<class FTabManager>& InTabManager)
{
	InTabManager->UnregisterTabSpawner(InputBindingsTabId);
	InTabManager->UnregisterTabSpawner(PanelTabId);
}

bool FRemoteControlPresetEditorToolkit::OnRequestClose()
{
	return true;
}

FText FRemoteControlPresetEditorToolkit::GetBaseToolkitName() const
{
	return LOCTEXT("PanelToolkitName", "Remote Control Panel");
}

FName FRemoteControlPresetEditorToolkit::GetToolkitFName() const
{
	static const FName RemoteControlPanelName("RemoteControlPanel");
	return RemoteControlPanelName;
}

FLinearColor FRemoteControlPresetEditorToolkit::GetWorldCentricTabColorScale() const
{
	return FLinearColor(0.7, 0.0f, 0.0f, 0.5f);
}

FString FRemoteControlPresetEditorToolkit::GetWorldCentricTabPrefix() const
{
	return LOCTEXT("RemoteControlTabPrefix", "RemoteControl ").ToString();
}

TSharedRef<SDockTab> FRemoteControlPresetEditorToolkit::HandleTabManagerSpawnPanelTab(const FSpawnTabArgs& Args)
{
	check(Args.GetTabId() == PanelTabId);

	if (PanelTab && PanelTab->GetEntityList() && InputBindingsTab && InputBindingsTab->GetEntityList())
	{
		if (TSharedPtr<SRCPanelTreeNode> Node = InputBindingsTab->GetEntityList()->GetSelection())
		{
			PanelTab->GetEntityList()->SetSelection(Node->GetId());
		}
	}

	return SNew(SDockTab)
		.Label(LOCTEXT("RemoteControlPanelMainTitle", "Remote Control Panel"))
		.TabColorScale(GetTabColorScale())	
		[
			PanelTab.ToSharedRef()
		];
}

TSharedRef<SDockTab> FRemoteControlPresetEditorToolkit::HandleTabManagerSpawnInputBindingsTab(const FSpawnTabArgs& Args)
{
	check(Args.GetTabId() == InputBindingsTabId);	

	if (InputBindingsTab && InputBindingsTab->GetEntityList() && PanelTab && PanelTab->GetEntityList())
	{
		if (TSharedPtr<SRCPanelTreeNode> Node = PanelTab->GetEntityList()->GetSelection())
		{
			InputBindingsTab->GetEntityList()->SetSelection(Node->GetId());
		}
	}

	return SNew(SDockTab)
		.Label(LOCTEXT("RemoteControlPanelMainTitle", "Remote Control Panel"))
		.TabColorScale(GetTabColorScale())
		[
			InputBindingsTab.ToSharedRef()
		];
}

void FRemoteControlPresetEditorToolkit::OnPanelSelectionChange(const TSharedPtr<SRCPanelTreeNode>& Node)
{
	if (InputBindingsTab && InputBindingsTab->GetEntityList())
	{
		InputBindingsTab->GetEntityList()->SetSelection(Node->GetId());
	}
}

void FRemoteControlPresetEditorToolkit::OnInputBindingsSelectionChange(const TSharedPtr<SRCPanelTreeNode>& Node)
{
	if (PanelTab && PanelTab->GetEntityList())
	{
		PanelTab->GetEntityList()->SetSelection(Node->GetId());
	}
}

#undef LOCTEXT_NAMESPACE /*RemoteControlPresetEditorToolkit*/