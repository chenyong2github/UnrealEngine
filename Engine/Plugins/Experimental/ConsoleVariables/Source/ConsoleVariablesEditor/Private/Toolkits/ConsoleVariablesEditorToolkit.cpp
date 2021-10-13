// Copyright Epic Games, Inc. All Rights Reserved.

#include "Toolkits/ConsoleVariablesEditorToolkit.h"

#include "ConsoleVariablesAsset.h"
#include "Modules/ModuleManager.h"
#include "Views/MainPanel/ConsoleVariablesEditorMainPanel.h"
#include "Widgets/Docking/SDockTab.h"

#include "LevelEditor.h"

#define LOCTEXT_NAMESPACE "FConsoleVariablesToolkit"

const FName FConsoleVariablesEditorToolkit::AppIdentifier(TEXT("ConsoleVariablesToolkit"));
const FName FConsoleVariablesEditorToolkit::ConsoleVariablesToolkitPanelTabId(TEXT("ConsoleVariablesToolkitPanel"));

TSharedPtr<FConsoleVariablesEditorToolkit> FConsoleVariablesEditorToolkit::CreateConsoleVariablesEditor(const EToolkitMode::Type Mode, const TSharedPtr<class IToolkitHost>& InitToolkitHost)
{
	TSharedPtr<FConsoleVariablesEditorToolkit> Result = MakeShared<FConsoleVariablesEditorToolkit>();

	Result->Initialize(Mode, InitToolkitHost);
	return Result;
}

UConsoleVariablesAsset* FConsoleVariablesEditorToolkit::AllocateTransientPreset() const
{
	static const TCHAR* PackageName = TEXT("/Temp/ConsoleVariablesUI/PendingConsoleVariablesCollections");

	static FName DesiredName = "PendingConsoleVariablesCollection";

	UPackage* NewPackage = CreatePackage(PackageName);
	NewPackage->SetFlags(RF_Transient);
	NewPackage->AddToRoot();

	UConsoleVariablesAsset* NewPreset = NewObject<UConsoleVariablesAsset>(NewPackage, DesiredName, RF_Transient | RF_Transactional | RF_Standalone);

	return NewPreset;
}

void FConsoleVariablesEditorToolkit::RegisterTabSpawners(const TSharedRef<FTabManager>& InTabManager)
{
	Super::RegisterTabSpawners(InTabManager);
	
	InTabManager->RegisterTabSpawner(ConsoleVariablesToolkitPanelTabId, FOnSpawnTab::CreateSP(this, &FConsoleVariablesEditorToolkit::SpawnTab_MainPanel))
        .SetDisplayName(LOCTEXT("MainPanelTabTitle", "Console Variables UI"))
        .SetGroup(AssetEditorTabsCategory.ToSharedRef());
}

void FConsoleVariablesEditorToolkit::UnregisterTabSpawners(const TSharedRef<FTabManager>& InTabManager)
{
	Super::UnregisterTabSpawners(InTabManager);
	
	InTabManager->UnregisterTabSpawner(ConsoleVariablesToolkitPanelTabId);
}

FText FConsoleVariablesEditorToolkit::GetToolkitName() const
{
	return LOCTEXT("EditorNameKey", "Console Variables UI");
}

FText FConsoleVariablesEditorToolkit::GetToolkitToolTipText() const
{
	return LOCTEXT("ConsoleVariablesTooltipKey", "Console Variables UI");
}

FConsoleVariablesEditorToolkit::~FConsoleVariablesEditorToolkit()
{
	if (FModuleManager::Get().IsModuleLoaded("LevelEditor"))
	{
		FLevelEditorModule& LevelEditorModule = FModuleManager::GetModuleChecked<FLevelEditorModule>("LevelEditor");
		if (TSharedPtr<FTabManager> EditorTabManager = LevelEditorModule.GetLevelEditorTabManager())
		{
			UnregisterTabSpawners(EditorTabManager.ToSharedRef());
			if (TSharedPtr<SDockTab> Tab = EditorTabManager->FindExistingLiveTab(ConsoleVariablesToolkitPanelTabId))
			{
				Tab->RequestCloseTab();
			}
		}
	}
}

void FConsoleVariablesEditorToolkit::Initialize(const EToolkitMode::Type Mode, const TSharedPtr<class IToolkitHost>& InitToolkitHost)
{
	UConsoleVariablesAsset* EditingAsset = AllocateTransientPreset();

	// Create our content
	const TSharedRef<FTabManager::FLayout> Layout = []()
	{
		const FName& LayoutString = TEXT("ConsoleVariables_Layout");

		TSharedRef<FTabManager::FLayout> Layout = FTabManager::NewLayout(LayoutString)
            ->AddArea
            (
                FTabManager::NewPrimaryArea()
                ->Split
                (
                    FTabManager::NewStack()
                    ->AddTab(ConsoleVariablesToolkitPanelTabId, ETabState::OpenedTab)
				)
			);
		Layout = FLayoutSaveRestore::LoadFromConfig(GEditorLayoutIni, Layout);

		return Layout;
	}();

	// Required, will cause the previous toolkit to close bringing down the panel and unsubscribing the
	// tab spawner. Without this, the InitAssetEditor call below will trigger an ensure as the panel
	// tab ID will already be registered within EditorTabManager
	
	const FLevelEditorModule& LevelEditorModule = FModuleManager::GetModuleChecked<FLevelEditorModule>("LevelEditor");
	const TSharedPtr<FTabManager> EditorTabManager = LevelEditorModule.GetLevelEditorTabManager();
	if (EditorTabManager->FindExistingLiveTab(ConsoleVariablesToolkitPanelTabId).IsValid())
	{
		EditorTabManager->TryInvokeTab(ConsoleVariablesToolkitPanelTabId)->RequestCloseTab();
	}

	const bool bCreateDefaultStandaloneMenu = false;
	const bool bCreateDefaultToolbar = false;
	FAssetEditorToolkit::InitAssetEditor(
			Mode,
			InitToolkitHost,
			AppIdentifier,
			Layout,
			bCreateDefaultStandaloneMenu,
			bCreateDefaultToolbar,
			EditingAsset
			);

	MainPanel = MakeShared<FConsoleVariablesEditorMainPanel>(EditingAsset);

	InvokePanelTab();
}

TSharedRef<SDockTab> FConsoleVariablesEditorToolkit::SpawnTab_MainPanel(const FSpawnTabArgs& Args) const
{
	// Spawn null widget tab that is immediately closed in favor if an automatically invoked nomad tab
	return SNew(SDockTab)
		.TabRole(ETabRole::NomadTab)
        .Label(LOCTEXT("MainPanelTabTitle", "Console Variables UI"))
        [
			SNullWidget::NullWidget
        ];
}

void FConsoleVariablesEditorToolkit::InvokePanelTab()
{
	struct Local
	{
		static void OnPresetTabClosed(TSharedRef<SDockTab> DockTab, TWeakPtr<IAssetEditorInstance> InAssetEditorInstance)
		{
			if (const TSharedPtr<IAssetEditorInstance> AssetEditorInstance = InAssetEditorInstance.Pin())
			{
				InAssetEditorInstance.Pin()->CloseWindow();
			}
		}
	};

	check(MainPanel.IsValid());

	// Create a new DockTab and add the RemoteControlPreset widget to it.
	FLevelEditorModule& LevelEditorModule = FModuleManager::GetModuleChecked<FLevelEditorModule>("LevelEditor");
	TSharedPtr<FTabManager> EditorTabManager = LevelEditorModule.GetLevelEditorTabManager();

	if (TSharedPtr<SDockTab> Tab = EditorTabManager->TryInvokeTab(ConsoleVariablesToolkitPanelTabId))
	{
		Tab->SetContent(MainPanel->GetOrCreateWidget());
		Tab->SetOnTabClosed(SDockTab::FOnTabClosedCallback::CreateStatic(&Local::OnPresetTabClosed, TWeakPtr<IAssetEditorInstance>(SharedThis(this))));
	}
}

#undef LOCTEXT_NAMESPACE
