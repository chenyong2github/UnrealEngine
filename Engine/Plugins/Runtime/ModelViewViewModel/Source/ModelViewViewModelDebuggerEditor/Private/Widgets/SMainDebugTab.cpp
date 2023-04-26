// Copyright Epic Games, Inc. All Rights Reserved.

#include "Widgets/SMainDebugTab.h"

#include "MVVMDebugSnapshot.h"

#include "ToolMenus.h"

#include "Framework/Docking/LayoutExtender.h"
#include "Framework/Docking/LayoutService.h"
#include "Framework/Docking/TabManager.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Styling/AppStyle.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SDetailsTab.h"
#include "Widgets/SMessagesLog.h"
#include "Widgets/SSelectionTab.h"
#include "Widgets/SViewModelBindingDetail.h"


#define LOCTEXT_NAMESPACE "MVVMDebuggerMainDebug"

namespace UE::MVVM
{

namespace Private
{
const FLazyName Stack_Selection = "Selection";
const FLazyName Stack_Binding = "Binding";
const FLazyName Stack_Detail = "Detail";
const FLazyName Stack_Messages = "Messages";
}

void SMainDebug::Construct(const FArguments& InArgs, const TSharedRef<SDockTab>& InParentTab)
{
	//ULevelEditorMenuContext* LevelEditorMenuContext = NewObject<ULevelEditorMenuContext>();
	//FToolMenuContext MenuContext = FToolMenuContext(LevelEditorMenuContext);
	FToolMenuContext MenuContext;
	BuildToolMenu();

	FMenuBarBuilder MenuBarBuilder = FMenuBarBuilder(TSharedPtr<FUICommandList>());
	MenuBarBuilder.AddPullDownMenu(
		LOCTEXT("WindowMenuLabel", "Window"),
		FText::GetEmpty(),
		FNewMenuDelegate::CreateSP(this, &SMainDebug::HandlePullDownWindowMenu),
		"Window"
	);

	const TSharedRef<SWidget> MenuWidget = MenuBarBuilder.MakeWidget();

	TSharedRef<SWidget> DockingArea = CreateDockingArea(InParentTab);
	TabManager->SetMenuMultiBox(MenuBarBuilder.GetMultiBox(), MenuWidget);

	ChildSlot
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			MenuWidget
		]
		+ SVerticalBox::Slot()
		.Padding(FMargin(0.0f, 0.0f, 0.0f, 2.0f))
		.AutoHeight()
		[
			SNew(SBorder)
			.Padding(0)
			.BorderImage(FAppStyle::Get().GetBrush("NoBorder"))
			[
				UToolMenus::Get()->GenerateWidget("ModelViewViewModel.Debug.Toolbar", MenuContext)
			]
			]
			+ SVerticalBox::Slot()
			.Padding(4.0f, 2.f, 4.f, 2.f)
			.FillHeight(1.0f)
			[
				DockingArea
			]
	];
}


void SMainDebug::HandlePullDownWindowMenu(FMenuBuilder& MenuBuilder)
{
	if (!TabManager.IsValid())
	{
		return;
	}

	TabManager->PopulateLocalTabSpawnerMenu(MenuBuilder);
}


void SMainDebug::BuildToolMenu()
{
	UToolMenu* ModesToolbar = UToolMenus::Get()->ExtendMenu("ModelViewViewModel.Debug.Toolbar");
	check(ModesToolbar);

	{
		FToolMenuSection& Section = ModesToolbar->AddSection("Snapshot"); 

		FToolMenuEntry TakeSnapshotButtonEntry = FToolMenuEntry::InitToolBarButton(
			"TakeSnapshot",
			FUIAction(
				FExecuteAction::CreateSP(this, &SMainDebug::HandleTakeSnapshot)
			),
			LOCTEXT("TakesnapshotLabel", "Take snapshot"),
			LOCTEXT("TakesnapshotTooltip", "Take snapshot"),
			FSlateIcon("MVVMDebuggerEditorStyle", "Viewmodel.TabIcon")
		);
		Section.AddEntry(TakeSnapshotButtonEntry);

		FToolMenuEntry ConfigureSnapshotMenuEntry = FToolMenuEntry::InitComboButton(
			"ConfigureSnapshot",
			FUIAction(),
			FOnGetContent::CreateSP(this, &SMainDebug::HandleSnapshotMenuContent),
			LOCTEXT("ConfigureSnapshotLabel", "Configure snapshot"),
			LOCTEXT("ConfigureSnapshotTooltip", "Configure snapshot"),
			FSlateIcon(),
			true //bInSimpleComboBox
		);
		ConfigureSnapshotMenuEntry.StyleNameOverride = "CalloutToolbar";
		Section.AddEntry(ConfigureSnapshotMenuEntry);

		//Section.AddEntry(FToolMenuEntry::InitToolBarButton(
		//	"LoadSnapshot",
		//	FUIAction(
		//		FExecuteAction::CreateSP(this, &SMainDebug::HandleLoadSnapshot)
		//		),
		//	LOCTEXT("LoadSnapshotLabel", "Load Snapshot"),
		//	LOCTEXT("LoadSnapshotTooltip", "Load Snapshot"),
		//	FSlateIcon(FAppStyle::Get().GetStyleSetName(), "Icons.Import")
		//));

		//Section.AddEntry(FToolMenuEntry::InitToolBarButton(
		//	"SaveSnapshot",
		//	FUIAction(
		//		FExecuteAction::CreateSP(this, &SMainDebug::HandleSaveSnapshot),
		//		FCanExecuteAction::CreateSP(this, &SMainDebug::HasValidSnapshot)
		//	),
		//	LOCTEXT("SaveSnapshotLabel", "Save Snapshot"),
		//	LOCTEXT("SaveSnapshotTooltip", "Save Snapshot"),
		//	FSlateIcon(FAppStyle::Get().GetStyleSetName(), "Icons.Save")
		//));
	}
}


TSharedRef<SWidget> SMainDebug::HandleSnapshotMenuContent()
{
	const bool bShouldCloseWindowAfterMenuSelection = true;
	FMenuBuilder MenuBuilder(bShouldCloseWindowAfterMenuSelection, nullptr);

	MenuBuilder.BeginSection("Profile", LOCTEXT("SnapshotContextMenuSectionName", "Snapshot"));
	{
		//MenuBuilder.AddMenuEntry(
		//	LOCTEXT("CreateMenuLabel", "New Empty Media Profile"),
		//	LOCTEXT("CreateMenuTooltip", "Create a new Media Profile asset."),
		//	FSlateIcon(FMediaFrameworkUtilitiesEditorStyle::GetStyleSetName(), TEXT("ClassIcon.MediaProfile")),
		//	FUIAction(FExecuteAction::CreateRaw(this, &FMediaProfileMenuEntryImpl::CreateNewProfile))
		//);
	}
	MenuBuilder.EndSection();

	return MenuBuilder.MakeWidget();
}


void SMainDebug::HandleTakeSnapshot()
{
	Snapshot = FDebugSnapshot::CreateSnapshot();
	if (TSharedPtr<SSelectionTab> SelectionViewPtr = SelectionView.Pin())
	{
		SelectionViewPtr->SetSnapshot(Snapshot);
	}
}


void SMainDebug::HandleLoadSnapshot()
{
	//FNotificationInfo Info(LOCTEXT("LoadFail", "Failed to load snapshot from disk."));
	//Info.ExpireDuration = 2.0f;
	//FSlateNotificationManager::Get().AddNotification(Info)->SetCompletionState(SNotificationItem::CS_Fail);
}


void SMainDebug::HandleSaveSnapshot()
{}


bool SMainDebug::HasValidSnapshot() const
{
	return Snapshot.IsValid();
}


void SMainDebug::HandleObjectSelectionChanged()
{
	if (Snapshot == nullptr)
	{
		if (TSharedPtr<SDetailsTab> DetailViewPtr = DetailView.Pin())
		{
			DetailViewPtr->SetObjects(TArray<UObject*>());
		}
		if (TSharedPtr<SViewModelBindingDetail> ViewModelBindingDetailPtr = ViewModelBindingDetail.Pin())
		{
			ViewModelBindingDetailPtr->SetViewModels(TArray<TSharedPtr<FMVVMViewModelDebugEntry>>());
		}
	}
	else
	{
		if (TSharedPtr<SSelectionTab> SelectionPtr = SelectionView.Pin())
		{
			if (TSharedPtr<SDetailsTab> DetailViewPtr = DetailView.Pin())
			{
				DetailViewPtr->SetObjects(SelectionPtr->GetSelectedObjects());
			}

			if (TSharedPtr<SViewModelBindingDetail> ViewModelBindingDetailPtr = ViewModelBindingDetail.Pin())
			{
				TArray<TSharedPtr<FMVVMViewModelDebugEntry>> ViewModels;
				for (FDebugItemId DebugItem : SelectionPtr->GetSelectedItems())
				{
					if (DebugItem.Type == FDebugItemId::EType::ViewModel)
					{
						if (TSharedPtr<FMVVMViewModelDebugEntry> Found = Snapshot->FindViewModel(DebugItem.Id))
						{
							ViewModels.Add(Found);
						}
					}
				}
				ViewModelBindingDetailPtr->SetViewModels(ViewModels);
			}
		}
	}
}


TSharedRef<SWidget> SMainDebug::CreateDockingArea(const TSharedRef<SDockTab>& InParentTab)
{
	const FName LayoutName = TEXT("MVVMDebugger_Layout_v1.8");
	TSharedRef<FTabManager::FLayout> DefaultLayer = FTabManager::NewLayout(LayoutName)
		->AddArea
		(
			FTabManager::NewPrimaryArea()
			->SetOrientation(Orient_Vertical)
			->Split
			(
				FTabManager::NewSplitter()
				->SetOrientation(Orient_Horizontal)
				->Split
				(
					FTabManager::NewStack()
					->SetSizeCoefficient(0.25f)
					->SetHideTabWell(true)
					->AddTab(Private::Stack_Selection, ETabState::OpenedTab)
				)
				->Split
				(
					FTabManager::NewStack()
					->SetSizeCoefficient(0.5f)
					->SetHideTabWell(true)
					->AddTab(Private::Stack_Binding, ETabState::OpenedTab)
				)
				->Split
				(
					FTabManager::NewStack()
					->SetSizeCoefficient(0.25f)
					->SetHideTabWell(true)
					->AddTab(Private::Stack_Detail, ETabState::OpenedTab)
				)
			)
			->Split
			(
				FTabManager::NewStack()
				->SetSizeCoefficient(1.0f)
				->SetHideTabWell(true)
				->AddTab(Private::Stack_Messages, ETabState::OpenedTab)
			)
		);

	TSharedRef<FTabManager::FLayout> Layout = FLayoutSaveRestore::LoadFromConfig(GEditorLayoutIni, DefaultLayer);
	FLayoutExtender LayoutExtender;
	Layout->ProcessExtensions(LayoutExtender);

	TabManager = FGlobalTabmanager::Get()->NewTabManager(InParentTab);

	TabManager->RegisterTabSpawner(Private::Stack_Selection, FOnSpawnTab::CreateSP(this, &SMainDebug::SpawnSelectionTab))
		.SetDisplayName(LOCTEXT("SelectionTab", "Selection"));
	TabManager->RegisterTabSpawner(Private::Stack_Binding, FOnSpawnTab::CreateSP(this, &SMainDebug::SpawnBindingTab))
		.SetDisplayName(LOCTEXT("BindingTab", "Bindings"));
	TabManager->RegisterTabSpawner(Private::Stack_Detail, FOnSpawnTab::CreateSP(this, &SMainDebug::SpawnDetailTab))
		.SetDisplayName(LOCTEXT("DetailTab", "Details"));
	TabManager->RegisterTabSpawner(Private::Stack_Messages, FOnSpawnTab::CreateSP(this, &SMainDebug::SpawnMessagesTab))
		.SetDisplayName(LOCTEXT("MessageLogTab", "Messages Log"));

	return TabManager->RestoreFrom(Layout, nullptr).ToSharedRef();
}


TSharedRef<SDockTab> SMainDebug::SpawnSelectionTab(const FSpawnTabArgs& Args)
{
	TSharedRef<SSelectionTab> LocalSelectionView = SNew(SSelectionTab)
		.OnSelectionChanged(this, &SMainDebug::HandleObjectSelectionChanged);
	SelectionView = LocalSelectionView;
	LocalSelectionView->SetSnapshot(Snapshot);
	return SNew(SDockTab)
		.Label(LOCTEXT("BindingTab", "Bindings"))
		[
			LocalSelectionView
		];
}


TSharedRef<SDockTab> SMainDebug::SpawnBindingTab(const FSpawnTabArgs& Args)
{
	return SNew(SDockTab)
		.Label(LOCTEXT("DetailTab", "Bindings"))
		[
			SAssignNew(ViewModelBindingDetail, SViewModelBindingDetail)
		];
}


TSharedRef<SDockTab> SMainDebug::SpawnDetailTab(const FSpawnTabArgs& Args)
{
	return SNew(SDockTab)
		.Label(LOCTEXT("DetailTab", "Details"))
		[
			SAssignNew(DetailView, SDetailsTab)
		];
}


TSharedRef<SDockTab> SMainDebug::SpawnMessagesTab(const FSpawnTabArgs& Args)
{
	return SNew(SDockTab)
		.Label(LOCTEXT("MessageLogTab", "Messages Log"))
		.ShouldAutosize(true)
		[
			SAssignNew(MessageLog, SMessagesLog)
		];
}

} //namespace

#undef LOCTEXT_NAMESPACE