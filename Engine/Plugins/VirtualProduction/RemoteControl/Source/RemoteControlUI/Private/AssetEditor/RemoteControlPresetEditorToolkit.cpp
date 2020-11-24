// Copyright Epic Games, Inc. All Rights Reserved.

#include "RemoteControlPresetEditorToolkit.h"
#include "RemoteControlPreset.h"
#include "RemoteControlUIModule.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/SWidget.h"
#include "Framework/Docking/TabManager.h"
#include "UI/SRemoteControlPanel.h"

#define LOCTEXT_NAMESPACE "RemoteControlPresetEditorToolkit"

const FName FRemoteControlPresetEditorToolkit::RemoteControlPanelTabId(TEXT("RemoteControl_RemoteControlPanel"));
const FName FRemoteControlPresetEditorToolkit::RemoteControlPanelAppIdentifier(TEXT("RemoteControlPanel"));

TSharedRef<FRemoteControlPresetEditorToolkit> FRemoteControlPresetEditorToolkit::CreateEditor(const EToolkitMode::Type Mode, const TSharedPtr< class IToolkitHost >& InitToolkitHost, URemoteControlPreset* InPreset)
{
	TSharedRef<FRemoteControlPresetEditorToolkit> NewEditor = MakeShared<FRemoteControlPresetEditorToolkit>();

	NewEditor->InitRemoteControlPresetEditor(Mode, InitToolkitHost, InPreset);

	return NewEditor;
}

void FRemoteControlPresetEditorToolkit::InitRemoteControlPresetEditor(const EToolkitMode::Type Mode, const TSharedPtr< class IToolkitHost >& InitToolkitHost, URemoteControlPreset* InPreset)
{
	struct Local
	{
		static void OnRemoteControlPresetClosed(TSharedRef<SDockTab> DockTab, TWeakPtr<IAssetEditorInstance> InRemoteControlPreset)
		{
			TSharedPtr<IAssetEditorInstance> AssetEditorInstance = InRemoteControlPreset.Pin();

			if (AssetEditorInstance.IsValid())
			{
				InRemoteControlPreset.Pin()->CloseWindow();
			}
		}
	};

	const TSharedRef<FTabManager::FLayout> StandaloneDefaultLayout = FTabManager::NewLayout("Standalone_RemoteControlPresetEditor")
		->AddArea
		(
			FTabManager::NewPrimaryArea()
			->Split
			(
				FTabManager::NewStack()
				->AddTab(RemoteControlPanelTabId, ETabState::OpenedTab)
			)
		);

	// Create a new DockTab and add the RemoteControlPreset widget to it.
	FLevelEditorModule& LevelEditorModule = FModuleManager::GetModuleChecked<FLevelEditorModule>("LevelEditor");
	TSharedPtr<FTabManager> EditorTabManager = LevelEditorModule.GetLevelEditorTabManager();

	// Required, will cause the previous toolkit to close bringing down the RemoteControlPreset and unsubscribing the
	// tab spawner. Without this, the InitAssetEditor call below will trigger an ensure as the RemoteControlPreset
	// tab ID will already be registered within EditorTabManager
	if (EditorTabManager->FindExistingLiveTab(RemoteControlPanelTabId).IsValid())
	{
		EditorTabManager->TryInvokeTab(RemoteControlPanelTabId)->RequestCloseTab();
	}

	PanelWidget = FRemoteControlUIModule::Get().CreateRemoteControlPanel(InPreset);

	FAssetEditorToolkit::InitAssetEditor(Mode, InitToolkitHost, RemoteControlPanelAppIdentifier, StandaloneDefaultLayout, /*Standalone=*/false, /*CreateDefaultToolbar=*/false, InPreset);

	if (TSharedPtr<SDockTab> Tab = EditorTabManager->TryInvokeTab(RemoteControlPanelTabId))
	{
		Tab->SetLabel(FText::FromName(InPreset->GetFName()));
		Tab->SetContent(PanelWidget.ToSharedRef());
		Tab->SetOnTabClosed(SDockTab::FOnTabClosedCallback::CreateStatic(&Local::OnRemoteControlPresetClosed, TWeakPtr<IAssetEditorInstance>(SharedThis(this))));
	}
}

void FRemoteControlPresetEditorToolkit::RegisterTabSpawners(const TSharedRef<class FTabManager>& InTabManager)
{
	WorkspaceMenuCategory = InTabManager->AddLocalWorkspaceMenuCategory(LOCTEXT("WorkspaceMenu_RemoteControlPanel", "Remote Control Panel"));

	InTabManager->RegisterTabSpawner(RemoteControlPanelTabId, FOnSpawnTab::CreateSP(this, &FRemoteControlPresetEditorToolkit::HandleTabManagerSpawnTab))
		.SetDisplayName(LOCTEXT("RemoteControlPanelMainTab", "Remote Control Panel"))
		.SetGroup(WorkspaceMenuCategory.ToSharedRef())
		.SetIcon(FSlateIcon(FEditorStyle::GetStyleSetName(), "LevelEditor.GameSettings.Small"));
}

void FRemoteControlPresetEditorToolkit::UnregisterTabSpawners(const TSharedRef<class FTabManager>& InTabManager)
{
	InTabManager->UnregisterTabSpawner(RemoteControlPanelTabId);
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

TSharedRef<SDockTab> FRemoteControlPresetEditorToolkit::HandleTabManagerSpawnTab(const FSpawnTabArgs& Args)
{
	TSharedPtr<SWidget> TabWidget = SNullWidget::NullWidget;

	if (Args.GetTabId() == RemoteControlPanelTabId)
	{
		TabWidget = PanelWidget;
	}

	return SNew(SDockTab)
		.Label(LOCTEXT("RemoteControlPanelMainTitle", "Remote Control Panel"))
		.TabColorScale(GetTabColorScale())
		.TabRole(ETabRole::NomadTab)
		[
			TabWidget.ToSharedRef()
		];
}

#undef LOCTEXT_NAMESPACE /*RemoteControlPresetEditorToolkit*/