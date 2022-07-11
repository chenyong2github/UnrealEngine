// Copyright Epic Games, Inc. All Rights Reserved.

#include "SMassDebugger.h"
#include "SMassProcessorsView.h"
#include "SMassProcessingView.h"
#include "SMassArchetypesView.h"
#include "MassDebuggerModel.h"
#include "Engine/Engine.h"
#include "CoreGlobals.h"
#include "Framework/Docking/LayoutService.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Widgets/Input/SComboBox.h"
#if WITH_EDITOR
#include "Editor.h"
#include "UnrealEdMisc.h"
#endif // WITH_EDITOR

#define LOCTEXT_NAMESPACE "SMassDebugger"

//----------------------------------------------------------------------//
// FMassDebuggerCommands
//----------------------------------------------------------------------//
FMassDebuggerCommands::FMassDebuggerCommands()
	: TCommands<FMassDebuggerCommands>("MassDebugger", LOCTEXT("MassDebuggerName", "Mass Debugger"), NAME_None, "MassDebuggerStyle")
{ }

void FMassDebuggerCommands::RegisterCommands() 
{
	UI_COMMAND(RefreshData, "RecacheData", "Recache data", EUserInterfaceActionType::Button, FInputChord());
}

namespace UE::Mass::Debugger::Private
{
	const FName ToolbarTabId("Toolbar");
	const FName ProcessorsTabId("Processors");
	const FName ProcessingGraphTabId("Processing Graphs");
	const FName ArchetypesTabId("Archetypes");

	bool IsSupportedWorldType(const EWorldType::Type WorldType)
	{
		return WorldType == EWorldType::Game || WorldType == EWorldType::Editor || WorldType == EWorldType::PIE;
	}
}

//----------------------------------------------------------------------//
// SMassDebugger
//----------------------------------------------------------------------//
SMassDebugger::SMassDebugger()
	: SCompoundWidget(), CommandList(MakeShareable(new FUICommandList))
	, DebuggerModel(MakeShareable(new FMassDebuggerModel))
{
}

SMassDebugger::~SMassDebugger()
{
#if WITH_EDITOR
	FEditorDelegates::EndPIE.Remove(PIEEndHandle);
	FWorldDelegates::OnPostDuplicate.Remove(PIEWorldInitialize);
#endif // WITH_EDITOR
	GEngine->OnWorldAdded().Remove(OnWorldAddedHandle);
	GEngine->OnWorldDestroyed().Remove(OnWorldDestroyedHandle);
}

void SMassDebugger::Construct(const FArguments& InArgs, const TSharedRef<SDockTab>& ConstructUnderMajorTab, const TSharedPtr<SWindow>& ConstructUnderWindow)
{
	using namespace UE::Mass::Debugger::Private;

	BindDelegates();

	const FMassDebuggerCommands& Commands = FMassDebuggerCommands::Get();
	FUICommandList& ActionList = *CommandList;

	ActionList.MapAction(Commands.RefreshData
		, FExecuteAction::CreateLambda([this]() 
		{
				DebuggerModel->RefreshAll();
		})
		// check if there's any mass environment picked
		, FCanExecuteAction::CreateLambda([this]()->bool { return true; })
		, FIsActionChecked::CreateLambda([]()->bool { return false; })
		, FIsActionButtonVisible::CreateLambda([this]()->bool { return true; }));

	// Tab Spawners
	TabManager = FGlobalTabmanager::Get()->NewTabManager(ConstructUnderMajorTab);
	TSharedRef<FWorkspaceItem> AppMenuGroup = TabManager->AddLocalWorkspaceMenuCategory(LOCTEXT("MassDebuggerGroupName", "Mass Debugger"));

	TabManager->RegisterTabSpawner(ToolbarTabId, FOnSpawnTab::CreateRaw(this, &SMassDebugger::SpawnToolbar))
		.SetDisplayName(LOCTEXT("ToolbarTabTitle", "Toolbar"))
		.SetGroup(AppMenuGroup);

	TabManager->RegisterTabSpawner(ProcessorsTabId, FOnSpawnTab::CreateRaw(this, &SMassDebugger::SpawnProcessorsTab))
		.SetDisplayName(LOCTEXT("ProcessorsTabTitle", "Processors"))
		.SetGroup(AppMenuGroup);

	TabManager->RegisterTabSpawner(ProcessingGraphTabId, FOnSpawnTab::CreateRaw(this, &SMassDebugger::SpawnProcessingTab))
		.SetDisplayName(LOCTEXT("ProcessingTabTitle", "Processing Graphs"))
		.SetGroup(AppMenuGroup);

	TabManager->RegisterTabSpawner(ArchetypesTabId, FOnSpawnTab::CreateRaw(this, &SMassDebugger::SpawnArchetypesTab))
		.SetDisplayName(LOCTEXT("ArchetypesTabTitle", "Archetypes"))
		.SetGroup(AppMenuGroup);


	// Default Layout
	TSharedRef<FTabManager::FLayout> Layout = FTabManager::NewLayout("MassDebuggerLayout_v1.0")
		->AddArea
		(
			FTabManager::NewPrimaryArea()
			->SetOrientation(Orient_Vertical)
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
						->AddTab(ProcessorsTabId, ETabState::OpenedTab)
						->AddTab(ProcessingGraphTabId, ETabState::OpenedTab)
						->AddTab(ArchetypesTabId, ETabState::OpenedTab)
						->SetForegroundTab(ProcessorsTabId)
				)
			)
		);

	Layout = FLayoutSaveRestore::LoadFromConfig(GEditorLayoutIni, Layout);

	ChildSlot
		[
			TabManager->RestoreFrom(Layout, ConstructUnderWindow).ToSharedRef()
		];

	TabManager->SetOnPersistLayout(
		FTabManager::FOnPersistLayout::CreateStatic(
			[](const TSharedRef<FTabManager::FLayout>& InLayout)
			{
#if WITH_EDITOR
				if (FUnrealEdMisc::Get().IsSavingLayoutOnClosedAllowed() == false)
				{
					return;
				}
#endif // WITH_EDITOR
				if (InLayout->GetPrimaryArea().Pin().IsValid())
				{
					FLayoutSaveRestore::SaveToConfig(GEditorLayoutIni, InLayout);
				}
			}
		)
	);
}

TSharedRef<SDockTab> SMassDebugger::SpawnToolbar(const FSpawnTabArgs& Args)
{
	const TSharedRef<SDockTab> MajorTab = SNew(SDockTab)
		.TabRole(ETabRole::PanelTab)
		.ShouldAutosize(true);

	FSlimHorizontalToolBarBuilder ToolBarBuilder(CommandList, FMultiBoxCustomization::None);
	ToolBarBuilder.BeginSection("Debugger");
	{
		ToolBarBuilder.AddToolBarButton(FMassDebuggerCommands::Get().RefreshData, NAME_None, LOCTEXT("RefreshData", "Refresh"), LOCTEXT("RefreshDebuggerTooltip", "Refreshes data cached by the debugger instance"));//, FSlateIcon(FLogVisualizerStyle::Get().GetStyleSetName(), TEXT("Toolbar.Update")));
	}

	RebuildEnvironmentsList();
	
	MajorTab->SetContent(
		SNew(SHorizontalBox)
		+SHorizontalBox::Slot()
		[
			ToolBarBuilder.MakeWidget()
		]
		+SHorizontalBox::Slot()
		.HAlign(HAlign_Right)
		.VAlign(VAlign_Center)
		.Padding(2.f)
		.AutoWidth()
		[
			SAssignNew(EnvironmentComboBox, SComboBox<TSharedPtr<FMassDebuggerEnvironment>>)
			.OptionsSource(&EnvironmentsList)
			.OnGenerateWidget_Lambda([](TSharedPtr<FMassDebuggerEnvironment> Item)
				{
					check(Item);
					return SNew(STextBlock).Text(FText::FromString(Item->GetDisplayName()));
				})
			.OnSelectionChanged(this, &SMassDebugger::HandleEnvironmentChanged)
			.ToolTipText(LOCTEXT("Environment_Tooltip", "Pick where to get the data from"))
			[
				SAssignNew(EnvironmentComboLabel, STextBlock)
				.Text(LOCTEXT("PickEnvironment", "Pick Environment"))
			]
		]
	);

	return MajorTab;
}

void SMassDebugger::OnWorldAdded(UWorld* NewWorld)
{
	if (NewWorld &&  UE::Mass::Debugger::Private::IsSupportedWorldType(NewWorld->WorldType))
	{
		EnvironmentsList.Add(MakeShareable(new FMassDebuggerEnvironment(NewWorld)));
	}
}

void SMassDebugger::OnWorldDestroyed(UWorld* InWorld)
{
	if (InWorld == nullptr || UE::Mass::Debugger::Private::IsSupportedWorldType(InWorld->WorldType) == false)
	{
		return;
	}

	FMassDebuggerEnvironment InEnvironment(InWorld);

	if (EnvironmentsList.RemoveAll([&InEnvironment](const TSharedPtr<FMassDebuggerEnvironment>& Element)
		{
			return *Element.Get() == InEnvironment;
		}) > 0)
	{
		if (DebuggerModel->IsCurrentEnvironment(InEnvironment))
		{
			EnvironmentComboLabel->SetText(FText::FromString(TEXT("Stale")));
		}
	}
}

void SMassDebugger::HandleEnvironmentChanged(TSharedPtr<FMassDebuggerEnvironment> Item, ESelectInfo::Type SelectInfo)
{
	DebuggerModel->SetEnvironment(Item);
	EnvironmentComboLabel->SetText(DebuggerModel->GetDisplayName());
}

void SMassDebugger::RebuildEnvironmentsList()
{
	const TIndirectArray<FWorldContext>& WorldContexts = GEngine->GetWorldContexts();
	EnvironmentsList.Reset();
	for (const FWorldContext& Context : WorldContexts)
	{
		if (UE::Mass::Debugger::Private::IsSupportedWorldType(Context.WorldType))
		{
			EnvironmentsList.Add(MakeShareable(new FMassDebuggerEnvironment(Context.World())));
		}
	}
}

TSharedRef<SDockTab> SMassDebugger::SpawnProcessorsTab(const FSpawnTabArgs& Args)
{
	const TSharedRef<SDockTab> MajorTab = SNew(SDockTab)
		.TabRole(ETabRole::PanelTab);

	TSharedPtr<SWidget> TabContent = SNew(SMassProcessorsView, DebuggerModel);
	MajorTab->SetContent(TabContent.ToSharedRef());

	return MajorTab;
}

TSharedRef<SDockTab> SMassDebugger::SpawnProcessingTab(const FSpawnTabArgs& Args)
{
	const TSharedRef<SDockTab> MajorTab = SNew(SDockTab)
		.TabRole(ETabRole::PanelTab);

	TSharedPtr<SWidget> TabContent = SNew(SMassProcessingView, DebuggerModel);
	MajorTab->SetContent(TabContent.ToSharedRef());

	return MajorTab;
}

TSharedRef<SDockTab> SMassDebugger::SpawnArchetypesTab(const FSpawnTabArgs& Args)
{
	const TSharedRef<SDockTab> MajorTab = SNew(SDockTab)
		.TabRole(ETabRole::PanelTab);

	TSharedPtr<SWidget> TabContent = SNew(SMassArchetypesView, DebuggerModel);
	MajorTab->SetContent(TabContent.ToSharedRef());

	return MajorTab;
}

void SMassDebugger::BindDelegates()
{
#if WITH_EDITOR
	PIEWorldInitialize = FWorldDelegates::OnPostDuplicate.AddLambda([this](UWorld* World, bool bDuplicateForPIE, TMap<UObject*, UObject*>& ReplacementMap, TArray<UObject*>& ObjectsToFixReferences)
		{
			if (World && bDuplicateForPIE)
			{
				EnvironmentsList.Add(MakeShareable(new FMassDebuggerEnvironment(World)));
			}
		});

	PIEEndHandle = FEditorDelegates::EndPIE.AddLambda([this](const bool bIsSimulating)
		{
			const TIndirectArray<FWorldContext>& WorldContexts = GEngine->GetWorldContexts();
			EnvironmentsList.Reset();
			for (const FWorldContext& Context : WorldContexts)
			{
				if (Context.WorldType == EWorldType::Game || Context.WorldType == EWorldType::Editor)
				{
					EnvironmentsList.Add(MakeShareable(new FMassDebuggerEnvironment(Context.World())));
				}
				else if (Context.WorldType == EWorldType::PIE)
				{
					FMassDebuggerEnvironment TempEnvironment(Context.World());
					if (DebuggerModel->IsCurrentEnvironment(TempEnvironment))
					{
						DebuggerModel->MarkAsStale();
					}
				}
			}
			EnvironmentComboLabel->SetText(DebuggerModel->GetDisplayName());
		});
#endif
	OnWorldAddedHandle = GEngine->OnWorldAdded().AddRaw(this, &SMassDebugger::OnWorldAdded);
	OnWorldDestroyedHandle = GEngine->OnWorldDestroyed().AddRaw(this, &SMassDebugger::OnWorldDestroyed);
}

#undef LOCTEXT_NAMESPACE
