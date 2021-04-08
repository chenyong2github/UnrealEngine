// Copyright Epic Games, Inc. All Rights Reserved.

#include "Toolkits/LevelSnapshotsEditorToolkit.h"

#include "FilteredResults.h"
#include "Misc/LevelSnapshotsEditorContext.h"
#include "LevelSnapshot.h"
#include "LevelSnapshotsEditorData.h"
#include "LevelSnapshotsEditorModule.h"
#include "LevelSnapshotsEditorStyle.h"
#include "LevelSnapshotsFunctionLibrary.h"
#include "LevelSnapshotsEditorInput.h"
#include "LevelSnapshotsEditorFilters.h"
#include "LevelSnapshotsEditorResults.h"
#include "LevelSnapshotsStats.h"
#include "SLevelSnapshotsEditorFilters.h"

#include "Editor.h"
#include "EditorFontGlyphs.h"
#include "EditorModeManager.h"
#include "Framework/Notifications/NotificationManager.h"
#include "ToolMenus.h"
#include "Framework/Docking/LayoutService.h"
#include "Stats/StatsMisc.h"
#include "Toolkits/ToolkitManager.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Notifications/SNotificationList.h"

#define LOCTEXT_NAMESPACE "FLevelSnapshotsToolkit"

namespace
{
	const FName AppIdentifier("LevelsnapshotsApp");
}

/* FToolkitManager keeps a reference to this widget. In turn, the widget keeps FLevelSnapshotsEditorToolkit alive. */
class SLevelSnapshotsEditorHost : public SCompoundWidget, public IToolkitHost
{
public:

	SLATE_BEGIN_ARGS(SLevelSnapshotsEditorHost) {}
	SLATE_END_ARGS()
	
	void Construct(const FArguments& InArgs, const TSharedPtr<FLevelSnapshotsEditorToolkit>& InOwnedEditor, const TSharedPtr<FTabManager>& InTabManager)
	{
		OwnedEditor = InOwnedEditor;
		TabManager = InTabManager;
	}

	void SetContent(const TSharedRef<FTabManager::FLayout>& Layout, const TSharedPtr<SWindow>& OwningWindow)
	{
		ChildSlot
		[
			TabManager->RestoreFrom(Layout, OwningWindow).ToSharedRef()
		];
	}

	//~ Begin IToolkitHost Interface
	virtual TSharedRef<SWidget> GetParentWidget() override
	{
		return AsShared();
	}
	virtual void BringToFront() override
	{
		OwnedEditor->BringToFront();
	}
	
	virtual TSharedPtr<FTabManager> GetTabManager() const override
	{
		return TabManager;
	}

	virtual FEditorModeTools& GetEditorModeManager() const override
	{
		// TODO: UE5 LevelSnapshot 
		static FEditorModeTools EditorModeManager;
		return EditorModeManager;
	}

	virtual UTypedElementCommonActions* GetCommonActions() const override
	{
		// TODO: UE5 LevelSnapshot 
		return nullptr;
	}

	
	virtual void OnToolkitHostingStarted(const TSharedRef<IToolkit>& Toolkit) override
	{
		Toolkit->RegisterTabSpawners(TabManager.ToSharedRef());
	}
	virtual void OnToolkitHostingFinished(const TSharedRef<IToolkit>& Toolkit) override
	{
		Toolkit->UnregisterTabSpawners(TabManager.ToSharedRef());
		
		OwnedEditor.Reset();
		TabManager.Reset();
	}

	virtual FOnActiveViewportChanged& OnActiveViewportChanged() override
	{
		return OnActiveViewportChangedDelegate;
	}
	
	virtual UWorld* GetWorld() const override
	{
		return nullptr;
	}
	//~ Begin IToolkitHost Interface

private:

	/* Keeps the editor alive */
	TSharedPtr<FLevelSnapshotsEditorToolkit> OwnedEditor;

	TSharedPtr<FTabManager> TabManager;

	/** A delegate which is called any time the LevelEditor's active viewport changes. */
	FOnActiveViewportChanged OnActiveViewportChangedDelegate;	
};

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

FEditorModeTools& FLevelSnapshotsEditorToolkit::GetEditorModeManager() const
{
	// TODO: UE5 LevelSnapshot 
	static FEditorModeTools EditorModeManager;
	return EditorModeManager;
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


	
	// Create and show new window
	MajorTabRoot = SNew(SDockTab)
        .ContentPadding(0.0f)
        .TabRole(ETabRole::NomadTab)
        .Label(LOCTEXT("LevelsnapshotEditorTabTitle", "Level Snapshots"));
	MajorTabRoot->SetCanCloseTab(SDockTab::FCanCloseTab::CreateRaw(this, &FLevelSnapshotsEditorToolkit::OnRequestClose));
	
	TabManager = FGlobalTabmanager::Get()->NewTabManager(MajorTabRoot.ToSharedRef());

	MajorTabRoot->SetContent
        ( 
            SAssignNew(StandaloneHost, SLevelSnapshotsEditorHost, SharedThis(this), TabManager)
        );
	ToolkitHost = StandaloneHost;
	// FToolkitManager keeps a reference to this widget. In turn, the widget keeps FLevelSnapshotsEditorToolkit alive.
	FToolkitManager::Get().RegisterNewToolkit(SharedThis(this));



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
	StandaloneHost->SetContent(Layout, MajorTabRoot->GetParentWindow());

	

	// Show ourselves
	ShowEditor();
}

void FLevelSnapshotsEditorToolkit::ShowEditor()
{
	if (ensure(MajorTabRoot.IsValid()))
	{
		FName PlaceholderId(TEXT("StandaloneToolkit"));
		TSharedPtr<FTabManager::FSearchPreference> SearchPreference = MakeShareable(new FTabManager::FRequireClosedTab());

		const TSharedRef<FGlobalTabmanager>& GlobalTabmanager = FGlobalTabmanager::Get();
		if  (!GlobalTabmanager->FindExistingLiveTab(PlaceholderId))
		{
			GlobalTabmanager->InsertNewDocumentTab(PlaceholderId, *SearchPreference, MajorTabRoot.ToSharedRef());
		}

		BringToFront();
	}
}

void FLevelSnapshotsEditorToolkit::BringToFront()
{
	TSharedPtr<SWindow> Window = MajorTabRoot->GetParentWindow();
	if (Window.IsValid())
	{
		Window->BringToFront();
	}
}

void FLevelSnapshotsEditorToolkit::RegisterTabSpawners(const TSharedRef<FTabManager>& InTabManager)
{
	const auto& LocalCategories = InTabManager->GetLocalWorkspaceMenuRoot()->GetChildItems();
	TSharedPtr<FWorkspaceItem> AssetEditorTabsCategory = LocalCategories.Num() > 0 ? LocalCategories[0] : InTabManager->GetLocalWorkspaceMenuRoot();
	
	InTabManager->RegisterTabSpawner(ToolbarTabId, FOnSpawnTab::CreateSP(this, &FLevelSnapshotsEditorToolkit::SpawnTab_Toolbar))
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
	InTabManager->UnregisterTabSpawner(ToolbarTabId);
	InTabManager->UnregisterTabSpawner(InputTabID);
	InTabManager->UnregisterTabSpawner(FilterTabID);
	InTabManager->UnregisterTabSpawner(ResultsTabID);
}

TSharedRef<SDockTab> FLevelSnapshotsEditorToolkit::SpawnTab_Toolbar(const FSpawnTabArgs& Args)
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
	FLevelSnapshotsEditorModule::Get().CallTakeSnapshot();
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
		ULevelSnapshotsFunctionLibrary::ApplySnapshotToWorld(World, *ActiveLevelSnapshot, EditorData->GetFilterResults()->GetSelectionSet());

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

bool FLevelSnapshotsEditorToolkit::OnRequestClose()
{
	if (EditorData.IsValid())
	{
		EditorData->CleanupAfterEditorClose();
	}

	FToolkitManager::Get().CloseToolkit( AsShared() );
	return true;
}

#undef LOCTEXT_NAMESPACE
