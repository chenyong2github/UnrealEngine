// Copyright Epic Games, Inc. All Rights Reserved.

#include "SDisplayClusterOperatorPanel.h"

#include "IDisplayClusterOperator.h"
#include "IDisplayClusterOperatorViewModel.h"
#include "SDisplayClusterOperatorToolbar.h"
#include "SDisplayClusterOperatorStatusBar.h"
#include "DisplayClusterOperatorStatusBarExtender.h"

#include "Styling/AppStyle.h"
#include "Framework/Docking/TabManager.h"
#include "Framework/Docking/LayoutExtender.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "SKismetInspector.h"
#include "Widgets/Docking/SDockTab.h"

#include "Components/ActorComponent.h"

#define LOCTEXT_NAMESPACE "SDisplayClusterOperatorPanel"

const FName SDisplayClusterOperatorPanel::ToolbarTabId = TEXT("OperatorToolbar");
const FName SDisplayClusterOperatorPanel::DetailsTabId = TEXT("OperatorDetails");
const FName SDisplayClusterOperatorPanel::PrimaryTabExtensionId = TEXT("PrimaryOperatorTabStack");
const FName SDisplayClusterOperatorPanel::AuxilliaryTabExtensionId = TEXT("AuxilliaryOperatorTabStack");

SDisplayClusterOperatorPanel::~SDisplayClusterOperatorPanel()
{
	IDisplayClusterOperator::Get().OnDetailObjectsChanged().Remove(DetailObjectsChangedHandle);
}

void SDisplayClusterOperatorPanel::Construct(const FArguments& InArgs, const TSharedRef<FTabManager>& InTabManager, const TSharedPtr<SWindow>& WindowOwner)
{
	TabManager = InTabManager;
	TSharedRef<FWorkspaceItem> AppMenuGroup = TabManager->AddLocalWorkspaceMenuCategory(LOCTEXT("OperatorMenuGroupName", "nDisplay Operator"));
	TabManager->SetAllowWindowMenuBar(true);

	TabManager->RegisterTabSpawner(ToolbarTabId, FOnSpawnTab::CreateSP(this, &SDisplayClusterOperatorPanel::SpawnToolbarTab))
		.SetDisplayName(LOCTEXT("ToolbarTabTitle", "Toolbar"))
		.SetIcon(FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Settings"))
		.SetGroup(AppMenuGroup);

	TabManager->RegisterTabSpawner(DetailsTabId, FOnSpawnTab::CreateSP(this, &SDisplayClusterOperatorPanel::SpawnDetailsTab))
		.SetDisplayName(LOCTEXT("DetailsTabTitle", "Details"))
		.SetIcon(FSlateIcon(FAppStyle::GetAppStyleSetName(), "LevelEditor.Tabs.Details"))
		.SetGroup(AppMenuGroup);

	const TSharedRef<FTabManager::FLayout> Layout = FTabManager::NewLayout("nDisplayOperatorLayout")
		->AddArea
		(
			FTabManager::NewPrimaryArea()
			->SetOrientation(EOrientation::Orient_Vertical)
			->Split
			(
				FTabManager::NewStack()
					->AddTab(ToolbarTabId, ETabState::OpenedTab)
					->SetHideTabWell(true)
			)
			->Split
			(
				FTabManager::NewSplitter()
					->SetOrientation(Orient_Horizontal)
					->Split
					(
						FTabManager::NewSplitter()
						->SetOrientation(Orient_Vertical)
						->SetSizeCoefficient(0.67f)
						->Split
						(
							FTabManager::NewStack()
								->SetExtensionId(PrimaryTabExtensionId)
						)
						->Split
						(
							FTabManager::NewStack()
								->SetExtensionId(AuxilliaryTabExtensionId)
						)
					)
					->Split
					(
						FTabManager::NewStack()
							->AddTab(DetailsTabId, ETabState::OpenedTab)
							->SetHideTabWell(true)
							->SetSizeCoefficient(0.33f)
					)
			)
		);

	LayoutExtender = MakeShared<FLayoutExtender>();
	IDisplayClusterOperator::Get().OnRegisterLayoutExtensions().Broadcast(*LayoutExtender);
	Layout->ProcessExtensions(*LayoutExtender);

	DetailObjectsChangedHandle = IDisplayClusterOperator::Get().OnDetailObjectsChanged().AddSP(this, &SDisplayClusterOperatorPanel::DisplayObjectsInDetailsPanel);

	ChildSlot
	[
		SNew(SVerticalBox)

		+SVerticalBox::Slot()
		.Padding(4.0f, 2.f, 4.f, 2.f)
		.FillHeight(1.0f)
		[
			TabManager->RestoreFrom(Layout, WindowOwner).ToSharedRef()
		]

		+SVerticalBox::Slot()
		.Padding(0.0f, 2.0f, 0.0f, 0.0f)
		.AutoHeight()
		[
			SAssignNew(StatusBar, SDisplayClusterOperatorStatusBar)
		]
	];

	// Allow any external modules to register extensions to the operator panel's status bar
	FDisplayClusterOperatorStatusBarExtender StatusBarExtender;
	IDisplayClusterOperator::Get().OnRegisterStatusBarExtensions().Broadcast(StatusBarExtender);
	StatusBarExtender.RegisterExtensions(StatusBar.ToSharedRef());

	if (ToolbarContainer.IsValid())
	{
		// Create toolbar after tab has been restored so child windows can register their toolbar extensions
		ToolbarContainer->SetContent(
		SAssignNew(Toolbar, SDisplayClusterOperatorToolbar)
		.CommandList(TSharedPtr<FUICommandList>()));
	}
}

void SDisplayClusterOperatorPanel::ForceDismissDrawers()
{
	if (StatusBar.IsValid())
	{
		StatusBar->DismissDrawer(nullptr);
	}
}

TSharedRef<SDockTab> SDisplayClusterOperatorPanel::SpawnToolbarTab(const FSpawnTabArgs& Args)
{
	return SNew(SDockTab)
		.ShouldAutosize(true)
		.TabRole(ETabRole::PanelTab)
		[
			SAssignNew(ToolbarContainer, SBox)
		];
}

TSharedRef<SDockTab> SDisplayClusterOperatorPanel::SpawnDetailsTab(const FSpawnTabArgs& Args)
{
	SAssignNew(DetailsView, SKismetInspector)
	.HideNameArea(true);

	return SNew(SDockTab)
		.TabRole(ETabRole::PanelTab)
		[
			DetailsView.ToSharedRef()
		];
}

void SDisplayClusterOperatorPanel::DisplayObjectsInDetailsPanel(const TArray<UObject*>& Objects)
{
	if (DetailsView.IsValid())
	{
		SKismetInspector::FShowDetailsOptions Options;
		Options.bShowComponents = false;
		DetailsView->ShowDetailsForObjects(Objects, MoveTemp(Options));
	}
}

#undef LOCTEXT_NAMESPACE
