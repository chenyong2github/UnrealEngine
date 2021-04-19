// Copyright Epic Games, Inc. All Rights Reserved.

#include "Toolkits/LevelSnapshotsEditorToolkit.h"

#include "Data/FilteredResults.h"
#include "Data/LevelSnapshot.h"
#include "Data/LevelSnapshotsEditorData.h"
#include "LevelSnapshotsEditorModule.h"
#include "LevelSnapshotsEditorStyle.h"
#include "LevelSnapshotsStats.h"
#include "Misc/LevelSnapshotsEditorContext.h"
#include "Views/Filter/LevelSnapshotsEditorFilters.h"
#include "Views/Filter/SLevelSnapshotsEditorFilters.h"
#include "Views/Results/LevelSnapshotsEditorResults.h"
#include "Views/Input/LevelSnapshotsEditorInput.h"

#include "Editor.h"
#include "EditorFontGlyphs.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Framework/Docking/LayoutService.h"
#include "Stats/StatsMisc.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Notifications/SNotificationList.h"

#define LOCTEXT_NAMESPACE "FLevelSnapshotsToolkit"

const FName FLevelSnapshotsEditorToolkit::AppIdentifier(TEXT("LevelSnapshotsToolkit"));
const FName FLevelSnapshotsEditorToolkit::ToolbarTabId(TEXT("LevelsnapshotsToolkit_Toolbar"));
const FName FLevelSnapshotsEditorToolkit::InputTabID(TEXT("BaseAssetToolkit_Input"));
const FName FLevelSnapshotsEditorToolkit::FilterTabID(TEXT("BaseAssetToolkit_Filter"));
const FName FLevelSnapshotsEditorToolkit::ResultsTabID(TEXT("BaseAssetToolkit_Results"));

TSharedPtr<FLevelSnapshotsEditorToolkit> FLevelSnapshotsEditorToolkit::CreateSnapshotEditor(ULevelSnapshotsEditorData* EditorData)
{
	TSharedPtr<FLevelSnapshotsEditorToolkit> Result = MakeShared<FLevelSnapshotsEditorToolkit>();
	Result->Initialize(EditorData);
	return Result;
}

void FLevelSnapshotsEditorToolkit::Initialize(ULevelSnapshotsEditorData* InEditorData)
{
	EditorData = InEditorData;

	EditorContext = MakeShared<FLevelSnapshotsEditorContext>();
	ViewBuilder = MakeShared<FLevelSnapshotsEditorViewBuilder>();
	ViewBuilder->EditorContextPtr = EditorContext;
	ViewBuilder->EditorDataPtr = InEditorData;

	
	// Initialize views
	EditorInput		= MakeShared<FLevelSnapshotsEditorInput>(ViewBuilder.ToSharedRef());
	EditorFilters	= MakeShared<FLevelSnapshotsEditorFilters>(ViewBuilder.ToSharedRef());
	EditorResults	= MakeShared<FLevelSnapshotsEditorResults>(ViewBuilder.ToSharedRef());


	// Create our content
	const TSharedRef<FTabManager::FLayout> Layout = []()
	{
		const FString LayoutString = TEXT("Levelsnapshots_Layout_2");

		TSharedRef<FTabManager::FLayout> Layout = FTabManager::NewLayout(FName(LayoutString))
            ->AddArea
            (
                FTabManager::NewPrimaryArea()->SetOrientation(Orient_Vertical)
                ->Split
                (
                    FTabManager::NewStack()
                    ->SetSizeCoefficient(0.1f)
                    ->AddTab(ToolbarTabId, ETabState::OpenedTab)
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
                        ->SetSizeCoefficient(0.45f)
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
		Layout = FLayoutSaveRestore::LoadFromConfig(GEditorLayoutIni, Layout);

		return Layout;
	}();

	const bool bCreateDefaultStandaloneMenu = false;
	const bool bCreateDefaultToolbar = false;
	FAssetEditorToolkit::InitAssetEditor(
			EToolkitMode::Standalone,
			nullptr,
			AppIdentifier,
			Layout,
			bCreateDefaultToolbar,
			bCreateDefaultStandaloneMenu,
			InEditorData
			);
}

void FLevelSnapshotsEditorToolkit::RegisterTabSpawners(const TSharedRef<FTabManager>& InTabManager)
{
	Super::RegisterTabSpawners(InTabManager);
	
	InTabManager->RegisterTabSpawner(ToolbarTabId, FOnSpawnTab::CreateSP(this, &FLevelSnapshotsEditorToolkit::SpawnTab_CustomToolbar))
        .SetDisplayName(LOCTEXT("ToolbarTab", "Toolbar"))
        .SetGroup(AssetEditorTabsCategory.ToSharedRef());
	
	InTabManager->RegisterTabSpawner(InputTabID, FOnSpawnTab::CreateSP(this, &FLevelSnapshotsEditorToolkit::SpawnTab_Input))
        .SetDisplayName(LOCTEXT("InputTab", "Input"))
        .SetGroup(AssetEditorTabsCategory.ToSharedRef());

	InTabManager->RegisterTabSpawner(FilterTabID, FOnSpawnTab::CreateSP(this, &FLevelSnapshotsEditorToolkit::SpawnTab_Filter))
        .SetDisplayName(LOCTEXT("Filter", "Filter"))
        .SetGroup(AssetEditorTabsCategory.ToSharedRef());

	InTabManager->RegisterTabSpawner(ResultsTabID, FOnSpawnTab::CreateSP(this, &FLevelSnapshotsEditorToolkit::SpawnTab_Results))
        .SetDisplayName(LOCTEXT("Result", "Result"))
        .SetGroup(AssetEditorTabsCategory.ToSharedRef());
}

void FLevelSnapshotsEditorToolkit::UnregisterTabSpawners(const TSharedRef<FTabManager>& InTabManager)
{
	Super::UnregisterTabSpawners(InTabManager);
	
	InTabManager->UnregisterTabSpawner(ToolbarTabId);
	InTabManager->UnregisterTabSpawner(InputTabID);
	InTabManager->UnregisterTabSpawner(FilterTabID);
	InTabManager->UnregisterTabSpawner(ResultsTabID);
}

FText FLevelSnapshotsEditorToolkit::GetToolkitName() const
{
	return LOCTEXT("EditorNameKey", "Level Snapshots");
}

FText FLevelSnapshotsEditorToolkit::GetToolkitToolTipText() const
{
	const FName SnapshotName = [this]() -> FName
	{
		if (EditorData.IsValid())
		{
			const TOptional<ULevelSnapshot*> SelectedSnapshot = EditorData->GetActiveSnapshot();
			return SelectedSnapshot ? SelectedSnapshot.GetValue()->GetFName() : NAME_None;
		}
		return NAME_None;
	}();
	
	FFormatNamedArguments Arguments;
	Arguments.Add(TEXT("SnapshotName"), FText::FromName(SnapshotName));
	return FText::Format(LOCTEXT("SelectedSnapshotKey", "Selected snapshot: {SnapshotName}"), Arguments);
}

TSharedRef<SDockTab> FLevelSnapshotsEditorToolkit::SpawnTab_CustomToolbar(const FSpawnTabArgs& Args)
{
	struct Local
	{
		static TSharedRef<SWidget> CreatePlusText(const FText& Text)
		{
			return SNew(SHorizontalBox)
                    + SHorizontalBox::Slot()
                    .HAlign(HAlign_Center)
                    .AutoWidth()
                    .Padding(FMargin(1.f, 1.f))
                    [
	                    SNew(STextBlock)
						.Font(FEditorStyle::Get().GetFontStyle("FontAwesome.10"))
	                    .TextStyle(FEditorStyle::Get(), "NormalText.Important")
	                    .Text(FEditorFontGlyphs::Plus)
                    ]

                    + SHorizontalBox::Slot()
                    .HAlign(HAlign_Left)
                    .AutoWidth()
                    .Padding(2.f, 1.f)
                    [
                        SNew(STextBlock)
                        .Justification(ETextJustify::Center)
                        .TextStyle(FEditorStyle::Get(), "NormalText.Important")
                        .Text(Text)
                    ];
		}
	};
	
	return SNew(SDockTab)
        .Label(LOCTEXT("Levelsnapshots.Toolkit.ToolbarTitle", "Toolbar"))
		.ShouldAutosize(true)
        [
	        SNew(SBorder)
	        .Padding(0)
	        .BorderImage(FEditorStyle::GetBrush("NoBorder"))
	        [
				SNew(SHorizontalBox)

				// Take snapshot
				+ SHorizontalBox::Slot()
					.AutoWidth()
					.HAlign(HAlign_Left)
					.Padding(2.f, 2.f)
				[
					SNew(SButton)
					.VAlign(VAlign_Center)
	                .ButtonStyle(FEditorStyle::Get(), "FlatButton.Success")
	                .ForegroundColor(FSlateColor::UseForeground())
	                .OnClicked(this, &FLevelSnapshotsEditorToolkit::OnClickTakeSnapshot)
	                [
						Local::CreatePlusText(LOCTEXT("TakeSnapshot", "Take Snapshot"))
	                ]
				]

				// Update results
				+ SHorizontalBox::Slot()
					.AutoWidth()
                    .HAlign(HAlign_Left)
					.Padding(2.f, 2.f)
                [
                    SNew(SButton)
                    .ButtonStyle(FEditorStyle::Get(), "FlatButton.Success")
                    .ForegroundColor(FSlateColor::UseForeground())
                    .OnClicked_Raw(this, &FLevelSnapshotsEditorToolkit::OnClickApplyToWorld)
                    [
	                    SNew(SHorizontalBox)
	                    +SHorizontalBox::Slot()
	                        .AutoWidth()
	                        .VAlign(VAlign_Center)
	                    [
		                    SNew(STextBlock)
			                    .Justification(ETextJustify::Center)
			                    .TextStyle(FEditorStyle::Get(), "NormalText.Important")
			                    .Text(LOCTEXT("ApplyToWorld", "Apply to World"))
                    	]
                    ]
                ]
        	]
        ];
}

TSharedRef<SDockTab> FLevelSnapshotsEditorToolkit::SpawnTab_Input(const FSpawnTabArgs& Args)
{	
	TSharedPtr<SDockTab> DetailsTab = SNew(SDockTab)
		.Label(LOCTEXT("Levelsnapshots.Toolkit.InputTitle", "Input"))
		.ShouldAutosize(true)
		[
			EditorInput->GetOrCreateWidget()
		];

	return DetailsTab.ToSharedRef();
}

TSharedRef<SDockTab> FLevelSnapshotsEditorToolkit::SpawnTab_Filter(const FSpawnTabArgs& Args)
{
	TSharedPtr<SDockTab> DetailsTab = SNew(SDockTab)
		.Label(LOCTEXT("Levelsnapshots.Toolkit.FilterTitle", "Filter"))
		[
			EditorFilters->GetOrCreateWidget()
		];

	return DetailsTab.ToSharedRef();
}

TSharedRef<SDockTab> FLevelSnapshotsEditorToolkit::SpawnTab_Results(const FSpawnTabArgs& Args)
{
	TSharedPtr<SDockTab> DetailsTab = SNew(SDockTab)
		.Label(LOCTEXT("Levelsnapshots.Toolkit.ResultTitle", "Result"))
		[
			EditorResults->GetOrCreateWidget()
		];

	return DetailsTab.ToSharedRef();
}

FReply FLevelSnapshotsEditorToolkit::OnClickTakeSnapshot()
{
	FLevelSnapshotsEditorModule::Get().BuildPathsToSaveSnapshotWithOptionalForm();
	return FReply::Handled();
}

FReply FLevelSnapshotsEditorToolkit::OnClickApplyToWorld()
{
	if (!ensure(EditorData.IsValid()))
	{
		FReply::Handled();
	}

	const TOptional<ULevelSnapshot*> ActiveLevelSnapshot = EditorData->GetActiveSnapshot();
	if (ActiveLevelSnapshot.IsSet())
	{
		if (!ensure(EditorResults.IsValid()))
		{
			FReply::Handled();
		}

		DECLARE_SCOPE_CYCLE_COUNTER(TEXT("OnClickApplyToWorld"), STAT_LevelSnapshots, STATGROUP_LevelSnapshots);
		{
			// Measure how long it takes to get all selected properties from UI
			DECLARE_SCOPE_CYCLE_COUNTER(TEXT("BuildSelectionSetFromSelectedProperties"), STAT_BuildSelectionSetFromSelectedProperties, STATGROUP_LevelSnapshots);
			EditorResults->BuildSelectionSetFromSelectedPropertiesInEachActorGroup();
		}
		
		UWorld* World = GEditor->GetEditorWorldContext().World();
		ActiveLevelSnapshot.GetValue()->ApplySnapshotToWorld(World, EditorData->GetFilterResults()->GetPropertiesToRollback());
 
		EditorResults->RefreshResults();
	}
	else
	{
		FNotificationInfo Info(LOCTEXT("SelectSnapshotFirst", "Select a snapshot first."));
		Info.ExpireDuration = 5.f;
		FSlateNotificationManager::Get().AddNotification(Info);
	}
	return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE
