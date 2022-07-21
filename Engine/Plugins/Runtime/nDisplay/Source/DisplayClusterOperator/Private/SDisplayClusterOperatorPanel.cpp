// Copyright Epic Games, Inc. All Rights Reserved.

#include "SDisplayClusterOperatorPanel.h"

#include "IDisplayClusterOperator.h"
#include "SDisplayClusterOperatorToolbar.h"

#include "Styling/AppStyle.h"
#include "Framework/Docking/TabManager.h"
#include "Framework/Docking/LayoutExtender.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "SKismetInspector.h"
#include "Widgets/Docking/SDockTab.h"
#include "WorkspaceMenuStructure.h"
#include "WorkspaceMenuStructureModule.h"

#include "Components/ActorComponent.h"

#define LOCTEXT_NAMESPACE "SDisplayClusterOperatorPanel"

const FName SDisplayClusterOperatorPanel::TabName = TEXT("DisplayClusterOperatorTab");
const FName SDisplayClusterOperatorPanel::ToolbarTabId = TEXT("OperatorToolbar");
const FName SDisplayClusterOperatorPanel::DetailsTabId = TEXT("OperatorDetails");
const FName SDisplayClusterOperatorPanel::TabExtensionId = TEXT("OperatorTabStack");

void SDisplayClusterOperatorPanel::RegisterTabSpawner()
{
	FGlobalTabmanager::Get()->RegisterNomadTabSpawner(TabName, FOnSpawnTab::CreateStatic(&SDisplayClusterOperatorPanel::SpawnInTab))
			.SetDisplayName(LOCTEXT("TabDisplayName", "nDisplay Operator"))
			.SetTooltipText(LOCTEXT("TabTooltip", "Open the nDisplay Operator tab."))
			.SetGroup(WorkspaceMenu::GetMenuStructure().GetLevelEditorVirtualProductionCategory());
}

void SDisplayClusterOperatorPanel::UnregisterTabSpawner()
{
	FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(TabName);
}

TSharedRef<SDockTab> SDisplayClusterOperatorPanel::SpawnInTab(const FSpawnTabArgs& SpawnTabArgs)
{
	const TSharedRef<SDockTab> MajorTab = SNew(SDockTab)
		.TabRole(ETabRole::NomadTab);

	MajorTab->SetContent(SNew(SDisplayClusterOperatorPanel, MajorTab, SpawnTabArgs.GetOwnerWindow()));

	return MajorTab;
}

SDisplayClusterOperatorPanel::~SDisplayClusterOperatorPanel()
{
	IDisplayClusterOperator::Get().OnDetailObjectsChanged().Remove(DetailObjectsChangedHandle);
}

void SDisplayClusterOperatorPanel::Construct(const FArguments& InArgs, const TSharedRef<SDockTab>& MajorTabOwner, const TSharedPtr<SWindow>& WindowOwner)
{
	TabManager = FGlobalTabmanager::Get()->NewTabManager(MajorTabOwner);
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
						FTabManager::NewStack()
							->SetExtensionId(TabExtensionId)
							->SetSizeCoefficient(0.67f)
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
		TabManager->RestoreFrom(Layout, WindowOwner).ToSharedRef()
	];

	// TODO: Move ownership of active root controller to a view model object, instead of letting the toolbar control which root actor is active
	if (ToolbarContainer.IsValid())
	{
		// Create toolbar after tab has been restored so child windows can register their toolbar extensions
		ToolbarContainer->SetContent(
		SAssignNew(Toolbar, SDisplayClusterOperatorToolbar)
		.CommandList(TSharedPtr<FUICommandList>()));

		TWeakObjectPtr<ADisplayClusterRootActor> ActiveRootActor = Toolbar->GetActiveRootActor();
		if (ActiveRootActor.IsValid())
		{
			IDisplayClusterOperator::Get().OnActiveRootActorChanged().Broadcast(ActiveRootActor.Get());
		}
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
