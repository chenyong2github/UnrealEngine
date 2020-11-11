// Copyright Epic Games, Inc. All Rights Reserved.

#include "Toolkits/LevelSnapshotsEditorToolkit.h"

#include "LevelSnapshotsEditorCommands.h"
#include "LevelSnapshotsEditorStyle.h"
#include "LevelSnapshotsEditorData.h"
#include "LevelSnapshotsFunctionLibrary.h"
#include "Misc/LevelSnapshotsEditorContext.h"
#include "Views/Input/LevelSnapshotsEditorInput.h"
#include "Views/Filter/LevelSnapshotsEditorFilters.h"
#include "Views/Results/LevelSnapshotsEditorResults.h"

#include "Editor.h"
#include "Engine/World.h"

#include "LevelSnapshot.h"

#define LOCTEXT_NAMESPACE "FLevelSnapshotsToolkit"

const FName FLevelSnapshotsEditorToolkit::InputTabID(TEXT("BaseAssetToolkit_Input"));
const FName FLevelSnapshotsEditorToolkit::FilterTabID(TEXT("BaseAssetToolkit_Filter"));
const FName FLevelSnapshotsEditorToolkit::ResultsTabID(TEXT("BaseAssetToolkit_Results"));

FLevelSnapshotsEditorToolkit::FLevelSnapshotsEditorToolkit()
{
}

FLevelSnapshotsEditorToolkit::~FLevelSnapshotsEditorToolkit()
{
}

void FLevelSnapshotsEditorToolkit::Initialize(const EToolkitMode::Type Mode, const TSharedPtr<IToolkitHost>& InitToolkitHost, ULevelSnapshotsEditorData* InEditorData)
{
	EditorData = InEditorData;

	const bool bCreateDefaultStandaloneMenu = true;
	const bool bCreateDefaultToolbar = true;

	EditorContext = MakeShared<FLevelSnapshotsEditorContext>();
	ViewBuilder = MakeShared<FLevelSnapshotsEditorViewBuilder>();
	ViewBuilder->EditorContextPtr = EditorContext;
	ViewBuilder->EditorDataPtr = InEditorData;

	// Initialize views
	EditorInput		= MakeShared<FLevelSnapshotsEditorInput>(ViewBuilder.ToSharedRef());
	EditorFilters	= MakeShared<FLevelSnapshotsEditorFilters>(ViewBuilder.ToSharedRef());
	EditorResults	= MakeShared<FLevelSnapshotsEditorResults>(ViewBuilder.ToSharedRef());

	ViewBuilder->OnSnapshotSelected.AddSP(this, &FLevelSnapshotsEditorToolkit::SnapshotSelected);

	FString LayoutString = TEXT("Standalone_Test_Layout_4");
	const TSharedRef<FTabManager::FLayout> StandaloneDefaultLayout = FTabManager::NewLayout(FName(LayoutString))
		->AddArea
		(
			FTabManager::NewPrimaryArea()->SetOrientation(Orient_Vertical)
			->Split
			(
				FTabManager::NewStack()
				->SetSizeCoefficient(0.1f)
				->AddTab(GetToolbarTabId(), ETabState::OpenedTab)
				->SetHideTabWell(true)
				)
			->Split
			(
				FTabManager::NewSplitter()->SetOrientation(Orient_Horizontal)
				->Split
				(
					FTabManager::NewStack()
					->SetSizeCoefficient(0.2f)
					->AddTab(InputTabID, ETabState::OpenedTab)
					->SetHideTabWell(true)
					)
				->Split
				(
					FTabManager::NewStack()
					->SetSizeCoefficient(0.3f)
					->AddTab(FilterTabID, ETabState::OpenedTab)
					->SetHideTabWell(true)
					)
				->Split
				(
					FTabManager::NewStack()
					->SetSizeCoefficient(0.5f)
					->AddTab(ResultsTabID, ETabState::OpenedTab)
					->SetHideTabWell(true)
					)
				)
			);

	InitAssetEditor(EToolkitMode::Standalone, TSharedPtr<IToolkitHost>(), FName("BaseAssetEditor"), StandaloneDefaultLayout, bCreateDefaultStandaloneMenu, bCreateDefaultToolbar, EditorData, false);

	SetupCommands();
	ExtendToolbar();
	RegenerateMenusAndToolbars();
}

void FLevelSnapshotsEditorToolkit::RegisterTabSpawners(const TSharedRef<FTabManager>& InTabManager)
{
	FAssetEditorToolkit::RegisterTabSpawners(InTabManager);

	if (!AssetEditorTabsCategory.IsValid())
	{
		const TArray<TSharedRef<FWorkspaceItem>>& LocalCategories = InTabManager->GetLocalWorkspaceMenuRoot()->GetChildItems();
		AssetEditorTabsCategory = LocalCategories.Num() > 0 ? LocalCategories[0] : InTabManager->GetLocalWorkspaceMenuRoot();
	}

	InTabManager->RegisterTabSpawner(InputTabID, FOnSpawnTab::CreateSP(this, &FLevelSnapshotsEditorToolkit::SpawnTab_Input))
		.SetDisplayName(LOCTEXT("InputTab", "Input"))
		.SetGroup(AssetEditorTabsCategory.ToSharedRef())
		.SetIcon(FSlateIcon(FEditorStyle::GetStyleSetName(), "LevelEditor.Tabs.Input"));

	InTabManager->RegisterTabSpawner(FilterTabID, FOnSpawnTab::CreateSP(this, &FLevelSnapshotsEditorToolkit::SpawnTab_Filter))
		.SetDisplayName(LOCTEXT("Filter", "Filter"))
		.SetGroup(AssetEditorTabsCategory.ToSharedRef())
		.SetIcon(FSlateIcon(FEditorStyle::GetStyleSetName(), "LevelEditor.Tabs.Filter"));

	InTabManager->RegisterTabSpawner(ResultsTabID, FOnSpawnTab::CreateSP(this, &FLevelSnapshotsEditorToolkit::SpawnTab_Results))
		.SetDisplayName(LOCTEXT("Result", "Result"))
		.SetGroup(AssetEditorTabsCategory.ToSharedRef())
		.SetIcon(FSlateIcon(FEditorStyle::GetStyleSetName(), "LevelEditor.Tabs.Result"));
}

void FLevelSnapshotsEditorToolkit::UnregisterTabSpawners(const TSharedRef<FTabManager>& InTabManager)
{
	InTabManager->UnregisterTabSpawner(InputTabID);
	InTabManager->UnregisterTabSpawner(FilterTabID);
	InTabManager->UnregisterTabSpawner(ResultsTabID);
}

TSharedRef<SDockTab> FLevelSnapshotsEditorToolkit::SpawnTab_Input(const FSpawnTabArgs& Args)
{	
	TSharedPtr<SDockTab> DetailsTab = SNew(SDockTab)
		.Icon(FEditorStyle::GetBrush("LevelEditor.Tabs.Input"))
		.Label(LOCTEXT("BaseAssetViewport", "Input"))
		[
			EditorInput->GetOrCreateWidget()
		];

	return DetailsTab.ToSharedRef();
}

TSharedRef<SDockTab> FLevelSnapshotsEditorToolkit::SpawnTab_Filter(const FSpawnTabArgs& Args)
{
	TSharedPtr<SDockTab> DetailsTab = SNew(SDockTab)
		.Icon(FEditorStyle::GetBrush("LevelEditor.Tabs.Filter"))
		.Label(LOCTEXT("BaseDetailsTitle", "Filter"))
		[
			EditorFilters->GetOrCreateWidget()
		];

	return DetailsTab.ToSharedRef();
}

TSharedRef<SDockTab> FLevelSnapshotsEditorToolkit::SpawnTab_Results(const FSpawnTabArgs& Args)
{
	TSharedPtr<SDockTab> DetailsTab = SNew(SDockTab)
		.Icon(FEditorStyle::GetBrush("LevelEditor.Tabs.Result"))
		.Label(LOCTEXT("BaseDetailsTitle", "Result"))
		[
			EditorResults->GetOrCreateWidget()
		];

	return DetailsTab.ToSharedRef();
}

void FLevelSnapshotsEditorToolkit::ExtendToolbar()
{
	struct Local
	{
		static void FillToolbar(FToolBarBuilder& ToolbarBuilder, FLevelSnapshotsEditorToolkit* Toolkit)
		{
			ToolbarBuilder.BeginSection("Apply");
			{
				ToolbarBuilder.AddToolBarButton(FLevelSnapshotsEditorCommands::Get().Apply,
					NAME_None, TAttribute<FText>(), TAttribute<FText>(),
					FSlateIcon(FLevelSnapshotsEditorStyle::GetStyleSetName(), "LevelSnapshotsEditor.Toolbar.Apply"),
					FName(TEXT("ApplySnapshotToWorld")));
			}
			ToolbarBuilder.EndSection();
		}
	};


	TSharedPtr<FExtender> ToolbarExtender = MakeShareable(new FExtender);

	ToolbarExtender->AddToolBarExtension(
		"Asset",
		EExtensionHook::After,
		GetToolkitCommands(),
		FToolBarExtensionDelegate::CreateStatic(&Local::FillToolbar, this)
		);

	AddToolbarExtender(ToolbarExtender);
}

void FLevelSnapshotsEditorToolkit::SetupCommands()
{
	GetToolkitCommands()->MapAction(
		FLevelSnapshotsEditorCommands::Get().Apply,
		FExecuteAction::CreateRaw(this, &FLevelSnapshotsEditorToolkit::ApplyToWorld));
}

#pragma optimize("", off)
void FLevelSnapshotsEditorToolkit::ApplyToWorld()
{
	if (ULevelSnapshot* ActiveLevelSnapshot = ActiveLevelSnapshotPtr.Get())
	{
		UWorld* World = GEditor->GetEditorWorldContext().World();
		ULevelSnapshotsFunctionLibrary::ApplySnapshotToWorld(World, ActiveLevelSnapshot, nullptr);
	}
}

#pragma optimize("", on)

void FLevelSnapshotsEditorToolkit::SnapshotSelected(ULevelSnapshot* InLevelSnapshot)
{
	ActiveLevelSnapshotPtr = InLevelSnapshot;
}

#undef LOCTEXT_NAMESPACE