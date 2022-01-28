// Copyright Epic Games, Inc. All Rights Reserved.

#include "StateTreeEditor.h"
#include "Modules/ModuleManager.h"
#include "EditorStyleSet.h"
#include "SStateTreeView.h"
#include "PropertyEditorModule.h"
#include "StateTreeEditorModule.h"
#include "StateTree.h"
#include "StateTreeEditorData.h"
#include "StateTreeDelegates.h"
#include "StateTreeState.h"
#include "StateTreeTaskBase.h"
#include "StateTreeBaker.h"
#include "StateTreeTypes.h"
#include "IDetailsView.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Customizations/StateTreeBindingExtension.h"
#include "Misc/UObjectToken.h"

#include "Developer/MessageLog/Public/IMessageLogListing.h"
#include "Developer/MessageLog/Public/MessageLogInitializationOptions.h"
#include "Developer/MessageLog/Public/MessageLogModule.h"

#define LOCTEXT_NAMESPACE "StateTreeEditor"

const FName StateTreeEditorAppName(TEXT("StateTreeEditorApp"));

const FName FStateTreeEditor::SelectionDetailsTabId(TEXT("StateTreeEditor_SelectionDetails"));
const FName FStateTreeEditor::AssetDetailsTabId(TEXT("StateTreeEditor_AssetDetails"));
const FName FStateTreeEditor::StateTreeViewTabId(TEXT("StateTreeEditor_StateTreeView"));
const FName FStateTreeEditor::StateTreeStatisticsTabId(TEXT("StateTreeEditor_StateTreeStatistics"));
const FName FStateTreeEditor::CompilerResultsTabId(TEXT("StateTreeEditor_CompilerResults"));

void FStateTreeEditor::PostUndo(bool bSuccess)
{
}

void FStateTreeEditor::PostRedo(bool bSuccess)
{
}

void FStateTreeEditor::AddReferencedObjects(FReferenceCollector& Collector)
{
	if (StateTree != nullptr)
	{
		Collector.AddReferencedObject(StateTree);
	}
}

void FStateTreeEditor::RegisterTabSpawners(const TSharedRef<class FTabManager>& InTabManager)
{
	WorkspaceMenuCategory = InTabManager->AddLocalWorkspaceMenuCategory(LOCTEXT("WorkspaceMenu_StateTreeEditor", "StateTree Editor"));
	auto WorkspaceMenuCategoryRef = WorkspaceMenuCategory.ToSharedRef();

	FAssetEditorToolkit::RegisterTabSpawners(InTabManager);

	InTabManager->RegisterTabSpawner(SelectionDetailsTabId, FOnSpawnTab::CreateSP(this, &FStateTreeEditor::SpawnTab_SelectionDetails) )
		.SetDisplayName( NSLOCTEXT("StateTreeEditor", "SelectionDetailsTab", "Details" ) )
		.SetGroup(WorkspaceMenuCategoryRef)
		.SetIcon(FSlateIcon(FEditorStyle::GetStyleSetName(), "LevelEditor.Tabs.Details"));

	InTabManager->RegisterTabSpawner(AssetDetailsTabId, FOnSpawnTab::CreateSP(this, &FStateTreeEditor::SpawnTab_AssetDetails))
		.SetDisplayName(NSLOCTEXT("StateTreeEditor", "AssetDetailsTab", "Asset Details"))
		.SetGroup(WorkspaceMenuCategoryRef)
		.SetIcon(FSlateIcon(FEditorStyle::GetStyleSetName(), "LevelEditor.Tabs.Details"));

	InTabManager->RegisterTabSpawner(StateTreeViewTabId, FOnSpawnTab::CreateSP(this, &FStateTreeEditor::SpawnTab_StateTreeView))
		.SetDisplayName(NSLOCTEXT("StateTreeEditor", "StateTreeViewTab", "StateTree"))
		.SetGroup(WorkspaceMenuCategoryRef)
		.SetIcon(FSlateIcon(FEditorStyle::GetStyleSetName(), "LevelEditor.Tabs.Outliner"));
	InTabManager->RegisterTabSpawner(StateTreeStatisticsTabId, FOnSpawnTab::CreateSP(this, &FStateTreeEditor::SpawnTab_StateTreeStatistics))
		.SetDisplayName(NSLOCTEXT("StateTreeEditor", "StatisticsTab", "StateTree Statistics"))
		.SetGroup(WorkspaceMenuCategoryRef)
		.SetIcon(FSlateIcon(FEditorStyle::GetStyleSetName(), "LevelEditor.Tabs.Outliner"));
	InTabManager->RegisterTabSpawner(CompilerResultsTabId, FOnSpawnTab::CreateSP(this, &FStateTreeEditor::SpawnTab_CompilerResults))
		.SetDisplayName(NSLOCTEXT("StateTreeEditor", "CompilerResultsTab", "Compiler Results"))
		.SetGroup(WorkspaceMenuCategoryRef)
		.SetIcon(FSlateIcon(FEditorStyle::GetStyleSetName(), "LevelEditor.Tabs.Outliner"));
}


void FStateTreeEditor::UnregisterTabSpawners(const TSharedRef<class FTabManager>& InTabManager)
{
	FAssetEditorToolkit::UnregisterTabSpawners(InTabManager);

	InTabManager->UnregisterTabSpawner(SelectionDetailsTabId);
	InTabManager->UnregisterTabSpawner(AssetDetailsTabId);
	InTabManager->UnregisterTabSpawner(StateTreeViewTabId);
	InTabManager->UnregisterTabSpawner(StateTreeStatisticsTabId);
	InTabManager->UnregisterTabSpawner(CompilerResultsTabId);
}

void FStateTreeEditor::InitEditor( const EToolkitMode::Type Mode, const TSharedPtr< class IToolkitHost >& InitToolkitHost, UStateTree* InStateTree)
{
	StateTree = InStateTree;
	check(StateTree != NULL);

	UStateTreeEditorData* EditorData = Cast<UStateTreeEditorData>(StateTree->EditorData);
	if (EditorData == NULL)
	{
		EditorData = NewObject<UStateTreeEditorData>(StateTree, FName(), RF_Transactional);
		StateTree->EditorData = EditorData;
	}

	StateTreeViewModel = MakeShareable(new FStateTreeViewModel());
	StateTreeViewModel->Init(EditorData);

	StateTreeViewModel->GetOnAssetChanged().AddSP(this, &FStateTreeEditor::HandleModelAssetChanged);
	StateTreeViewModel->GetOnSelectionChanged().AddSP(this, &FStateTreeEditor::HandleModelSelectionChanged);

	FMessageLogModule& MessageLogModule = FModuleManager::LoadModuleChecked<FMessageLogModule>("MessageLog");
	FMessageLogInitializationOptions LogOptions;
	// Show Pages so that user is never allowed to clear log messages
	LogOptions.bShowPages = false;
	LogOptions.bShowFilters = false;
	LogOptions.bAllowClear = false;
	LogOptions.MaxPageCount = 1;
	CompilerResultsListing = MessageLogModule.CreateLogListing("StateTreeCompiler", LogOptions);
	CompilerResults = MessageLogModule.CreateLogListingWidget(CompilerResultsListing.ToSharedRef());

	CompilerResultsListing->OnMessageTokenClicked().AddSP(this, &FStateTreeEditor::HandleMessageTokenClicked);

	
	TSharedRef<FTabManager::FLayout> StandaloneDefaultLayout = FTabManager::NewLayout("Standalone_StateTree_Layout_v3")
	->AddArea
	(
		FTabManager::NewPrimaryArea() ->SetOrientation(Orient_Vertical)
		->Split
		(
			FTabManager::NewSplitter() ->SetOrientation(Orient_Horizontal)
			->Split
			(
				FTabManager::NewSplitter()->SetOrientation(Orient_Vertical)
				->SetSizeCoefficient(0.2f)
				->Split
				(
					FTabManager::NewStack()
					->SetSizeCoefficient(0.65f)
					->AddTab(AssetDetailsTabId, ETabState::OpenedTab)
				)
				->Split
				(
					FTabManager::NewStack()
					->SetSizeCoefficient(0.35f)
					->AddTab(StateTreeStatisticsTabId, ETabState::OpenedTab)
				)
			)
			->Split
			(
				FTabManager::NewSplitter()->SetOrientation(Orient_Vertical)
				->SetSizeCoefficient(0.5f)
				->Split
				(
					FTabManager::NewStack()
					->SetSizeCoefficient(0.75f)
					->AddTab(StateTreeViewTabId, ETabState::OpenedTab)
				)
				->Split
				(
					FTabManager::NewStack()
					->SetSizeCoefficient(0.25f)
					->AddTab(CompilerResultsTabId, ETabState::ClosedTab)
				)
			)
			->Split
			(
				FTabManager::NewStack()
				->SetSizeCoefficient(0.3f)
				->AddTab(SelectionDetailsTabId, ETabState::OpenedTab)
				->SetForegroundTab(SelectionDetailsTabId)
			)
		)
	);


	const bool bCreateDefaultStandaloneMenu = true;
	const bool bCreateDefaultToolbar = true;
	FAssetEditorToolkit::InitAssetEditor(Mode, InitToolkitHost, StateTreeEditorAppName, StandaloneDefaultLayout, bCreateDefaultStandaloneMenu, bCreateDefaultToolbar, StateTree);
	
	FStateTreeEditorModule& StateTreeEditorModule = FModuleManager::LoadModuleChecked<FStateTreeEditorModule>("StateTreeEditorModule");
	AddMenuExtender(StateTreeEditorModule.GetMenuExtensibilityManager()->GetAllExtenders(GetToolkitCommands(), GetEditingObjects()));

	RegenerateMenusAndToolbars();

	// TODO: should only do validation here to not edit asset each time it is opened.
	UpdateAsset();

	UE::StateTree::Delegates::OnIdentifierChanged.AddSP(this, &FStateTreeEditor::OnIdentifierChanged);
	UE::StateTree::Delegates::OnSchemaChanged.AddSP(this, &FStateTreeEditor::OnSchemaChanged);
}

FName FStateTreeEditor::GetToolkitFName() const
{
	return FName("StateTreeEditor");
}

FText FStateTreeEditor::GetBaseToolkitName() const
{
	return NSLOCTEXT("StateTreeEditor", "AppLabel", "State Tree");
}

FString FStateTreeEditor::GetWorldCentricTabPrefix() const
{
	return NSLOCTEXT("StateTreeEditor", "WorldCentricTabPrefix", "State Tree").ToString();
}

FLinearColor FStateTreeEditor::GetWorldCentricTabColorScale() const
{
	return FLinearColor( 0.0f, 0.0f, 0.2f, 0.5f );
}

void FStateTreeEditor::HandleMessageTokenClicked(const TSharedRef<IMessageToken>& InMessageToken)
{
	if (InMessageToken->GetType() == EMessageToken::Object)
	{
		const TSharedRef<FUObjectToken> ObjectToken = StaticCastSharedRef<FUObjectToken>(InMessageToken);
		UStateTreeState* State = Cast<UStateTreeState>(ObjectToken->GetObject().Get());
		if (State)
		{
			StateTreeViewModel->SetSelection(State);
		}
	}
}

TSharedRef<SDockTab> FStateTreeEditor::SpawnTab_StateTreeView(const FSpawnTabArgs& Args)
{
	check(Args.GetTabId() == StateTreeViewTabId);

	return SNew(SDockTab)
		.Label(NSLOCTEXT("StateTreeEditor", "StateTreeViewTab", "State Tree"))
		.TabColorScale(GetTabColorScale())
		[
			SAssignNew(StateTreeView, SStateTreeView, StateTreeViewModel.ToSharedRef())
		];
}


TSharedRef<SDockTab> FStateTreeEditor::SpawnTab_SelectionDetails(const FSpawnTabArgs& Args)
{
	check(Args.GetTabId() == SelectionDetailsTabId);

	FPropertyEditorModule& PropertyEditorModule = FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor");
	FDetailsViewArgs DetailsViewArgs;
	DetailsViewArgs.NameAreaSettings = FDetailsViewArgs::HideNameArea;

	SelectionDetailsView = PropertyEditorModule.CreateDetailView(DetailsViewArgs);
	SelectionDetailsView->SetObject(nullptr);
	SelectionDetailsView->OnFinishedChangingProperties().AddSP(this, &FStateTreeEditor::OnSelectionFinishedChangingProperties);

	SelectionDetailsView->SetExtensionHandler(MakeShared<FStateTreeBindingExtension>());

	TSharedRef<SDockTab> SpawnedTab = SNew(SDockTab)
		.Label(NSLOCTEXT("StateTreeEditor", "SelectionDetailsTab", "Details"))
		[
			SelectionDetailsView.ToSharedRef()
		];

	return SpawnedTab;
}

TSharedRef<SDockTab> FStateTreeEditor::SpawnTab_AssetDetails(const FSpawnTabArgs& Args)
{
	check(Args.GetTabId() == AssetDetailsTabId);

	FPropertyEditorModule& PropertyEditorModule = FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor");
	FDetailsViewArgs DetailsViewArgs;
	DetailsViewArgs.NameAreaSettings = FDetailsViewArgs::HideNameArea;

	AssetDetailsView = PropertyEditorModule.CreateDetailView(DetailsViewArgs);
	AssetDetailsView->SetObject(StateTree);
	AssetDetailsView->OnFinishedChangingProperties().AddSP(this, &FStateTreeEditor::OnAssetFinishedChangingProperties);

	TSharedRef<SDockTab> SpawnedTab = SNew(SDockTab)
		.Label(NSLOCTEXT("StateTreeEditor", "AssetDetailsTab", "StateTree"))
		[
			AssetDetailsView.ToSharedRef()
		];

	return SpawnedTab;
}

TSharedRef<SDockTab> FStateTreeEditor::SpawnTab_StateTreeStatistics(const FSpawnTabArgs& Args)
{
	check(Args.GetTabId() == StateTreeStatisticsTabId);
	TSharedRef<SDockTab> SpawnedTab = SNew(SDockTab)
		.Label(LOCTEXT("StatisticsTitle", "StateTree Statistics"))
		[
			SNew(SMultiLineEditableTextBox)
			.Padding(10.0f)
			.Style(FEditorStyle::Get(), "Log.TextBox")
			.Font(FCoreStyle::GetDefaultFontStyle("Mono", 9))
			.ForegroundColor(FLinearColor::Gray)
			.IsReadOnly(true)
			.Text(this, &FStateTreeEditor::GetStatisticsText)
		];
	return SpawnedTab;
}

TSharedRef<SDockTab> FStateTreeEditor::SpawnTab_CompilerResults(const FSpawnTabArgs& Args)
{
	check(Args.GetTabId() == CompilerResultsTabId);
	TSharedRef<SDockTab> SpawnedTab = SNew(SDockTab)
		.Label(LOCTEXT("CompilerResultsTitle", "Compiler Results"))
		[
			SNew(SBox)
			[
				CompilerResults.ToSharedRef()
			]
		];
	return SpawnedTab;
}

FText FStateTreeEditor::GetStatisticsText() const
{
	if (!StateTree)
	{
		return FText::GetEmpty();
	}

	if (const UScriptStruct* StorageStruct = StateTree->GetInstanceStorageStruct())
	{
		const FText SizeText = FText::AsMemory((uint64)StorageStruct->GetStructureSize());
		const FText NumItemsText = FText::AsNumber(StateTree->GetNumInstances());

		return FText::Format(LOCTEXT("RuntimeSize", "Runtime size: {0}, {1} items"), SizeText, NumItemsText);
	}
	
	return FText::GetEmpty();
}

void FStateTreeEditor::HandleModelAssetChanged()
{
	UpdateAsset();
}

void FStateTreeEditor::HandleModelSelectionChanged(const TArray<UStateTreeState*>& SelectedStates)
{
	if (SelectionDetailsView)
	{
		TArray<UObject*> Selected;
		for (UStateTreeState* State : SelectedStates)
		{
			Selected.Add(State);
		}
		SelectionDetailsView->SetObjects(Selected);
	}
}

void FStateTreeEditor::SaveAsset_Execute()
{
	// Remember the treview expnasion state
	if (StateTreeView)
	{
		StateTreeView->SavePersistentExpandedStates();
	}

	UpdateAsset();

	// save it
	FAssetEditorToolkit::SaveAsset_Execute();
}

void FStateTreeEditor::OnIdentifierChanged(const UStateTree& InStateTree)
{
	if (StateTree == &InStateTree)
	{
		UpdateAsset();
	}
}

void FStateTreeEditor::OnSchemaChanged(const UStateTree& InStateTree)
{
	if (StateTree == &InStateTree)
	{
		UpdateAsset();
		
		if (StateTreeViewModel)
		{
			StateTreeViewModel->NotifyAssetChangedExternally();
		}

		if (SelectionDetailsView.IsValid())
		{
			SelectionDetailsView->ForceRefresh();
		}
	}
}

void FStateTreeEditor::OnAssetFinishedChangingProperties(const FPropertyChangedEvent& PropertyChangedEvent)
{
	// Make sure nodes get updates when properties are changed.
	if (StateTreeViewModel)
	{
		StateTreeViewModel->NotifyAssetChangedExternally();
	}
}

void FStateTreeEditor::OnSelectionFinishedChangingProperties(const FPropertyChangedEvent& PropertyChangedEvent)
{
	// Make sure nodes get updates when properties are changed.
	if (StateTreeViewModel)
	{
		const TArray<TWeakObjectPtr<UObject>>& SelectedObjects = SelectionDetailsView->GetSelectedObjects();
		TSet<UStateTreeState*> ChangedStates;
		for (const TWeakObjectPtr<UObject>& WeakObject : SelectedObjects)
		{
			if (UObject* Object = WeakObject.Get())
			{
				if (UStateTreeState* State = Cast<UStateTreeState>(Object))
				{
					ChangedStates.Add(State);
				}
			}
		}
		if (ChangedStates.Num() > 0)
		{
			StateTreeViewModel->NotifyStatesChangedExternally(ChangedStates, PropertyChangedEvent);
		}
	}
}

namespace UE::StateTree::Editor::Internal
{
	bool FixChangedStateLinkName(FStateTreeStateLink& StateLink, const TMap<FGuid, FName>& IDToName)
	{
		if (StateLink.ID.IsValid())
		{
			const FName* Name = IDToName.Find(StateLink.ID);
			if (Name == nullptr)
			{
				// Missing link, we'll show these in the UI
				return false;
			}
			if (StateLink.Name != *Name)
			{
				// Name changed, fix!
				StateLink.Name = *Name;
				return true;
			}
		}
		return false;
	}

	void ValidateLinkedStates(UStateTree& StateTree)
	{
		UStateTreeEditorData* TreeData = Cast<UStateTreeEditorData>(StateTree.EditorData);
		if (!TreeData)
		{
			return;
		}

		// Make sure all state links are valid and update the names if needed.

		// Create ID to state name map.
		TMap<FGuid, FName> IDToName;
		TArray<UStateTreeState*> Stack;

		for (UStateTreeState* SubTree : TreeData->SubTrees)
		{
			if (SubTree)
			{
				Stack.Reset();
				Stack.Add(SubTree);
				while (Stack.Num() > 0)
				{
					UStateTreeState* CurState = Stack.Pop();
					IDToName.Add(CurState->ID, CurState->Name);
					for (UStateTreeState* ChildState : CurState->Children)
					{
						if (ChildState)
						{
							Stack.Append(CurState->Children);
						}
					}
				}
			}
		}

		// Fix changed names.
		for (UStateTreeState* SubTree : TreeData->SubTrees)
		{
			if (SubTree)
			{
				Stack.Reset();
				Stack.Add(SubTree);
				while (Stack.Num() > 0)
				{
					UStateTreeState* CurState = Stack.Pop();

					for (FStateTreeTransition& Transition : CurState->Transitions)
					{
						FixChangedStateLinkName(Transition.State, IDToName);
					}

					for (UStateTreeState* ChildState : CurState->Children)
					{
						if (ChildState)
						{
							Stack.Append(CurState->Children);
						}
					}
				}
			}
		}
	}

	void UpdateParents(UStateTree& StateTree)
	{
		UStateTreeEditorData* TreeData = Cast<UStateTreeEditorData>(StateTree.EditorData);
		if (!TreeData)
		{
			return;
		}

		TArray<UStateTreeState*> Stack;

		for (UStateTreeState* SubTree : TreeData->SubTrees)
		{
			if (SubTree)
			{
				SubTree->Parent = nullptr;
				Stack.Reset();
				Stack.Add(SubTree);
				while (Stack.Num() > 0)
				{
					UStateTreeState* CurState = Stack.Pop();
					for (UStateTreeState* ChildState : CurState->Children)
					{
						if (ChildState)
						{
							ChildState->Parent = CurState;
							Stack.Append(CurState->Children);
						}
					}
				}
			}
		}
	}

	void ApplySchema(UStateTree& StateTree)
	{
		UStateTreeEditorData* TreeData = Cast<UStateTreeEditorData>(StateTree.EditorData);
		if (!TreeData)
		{
			return;
		}
		
		const UStateTreeSchema* Schema = StateTree.GetSchema();
		if (!Schema)
		{
			return;
		}

		TArray<UStateTreeState*> Stack;

		for (UStateTreeState* SubTree : TreeData->SubTrees)
		{
			if (SubTree)
			{
				Stack.Reset();
				Stack.Add(SubTree);
				while (Stack.Num() > 0)
				{
					UStateTreeState* CurState = Stack.Pop();

					// Clear enter conditions if not allowed.
					if (Schema->AllowEnterConditions() == false && CurState->EnterConditions.Num() > 0)
					{
						UE_LOG(LogStateTree, Warning, TEXT("%s: Resetting Enter Conditions in state %s due to current schema restrictions."), *GetNameSafe(&StateTree), *GetNameSafe(CurState));
						CurState->EnterConditions.Reset();
					}

					// Clear evaluators if not allowed.
					if (Schema->AllowEvaluators() == false && CurState->Evaluators.Num() > 0)
					{
						UE_LOG(LogStateTree, Warning, TEXT("%s: Resetting Evaluators in state %s due to current schema restrictions."), *GetNameSafe(&StateTree), *GetNameSafe(CurState));
						CurState->Evaluators.Reset();
					}

					// Keep single and many tasks based on what is allowed.
					if (Schema->AllowMultipleTasks() == false)
					{
						if (CurState->Tasks.Num() > 0)
						{
							CurState->Tasks.Reset();
							UE_LOG(LogStateTree, Warning, TEXT("%s: Resetting Tasks in state %s due to current schema restrictions."), *GetNameSafe(&StateTree), *GetNameSafe(CurState));
						}
						
						// Task name is the same as state name.
						if (FStateTreeTaskBase* Task = CurState->SingleTask.Item.GetMutablePtr<FStateTreeTaskBase>())
						{
							Task->Name = CurState->Name;
						}
					}
					else
					{
						if (CurState->SingleTask.Item.IsValid())
						{
							CurState->SingleTask.Reset();
							UE_LOG(LogStateTree, Warning, TEXT("%s: Resetting Single Task in state %s due to current schema restrictions."), *GetNameSafe(&StateTree), *GetNameSafe(CurState));
						}
					}
					
					for (UStateTreeState* ChildState : CurState->Children)
					{
						if (ChildState)
						{
							Stack.Append(CurState->Children);
						}
					}
				}
			}
		}

	}

	void RemoveUnusedBindings(UStateTree& StateTree)
	{
		UStateTreeEditorData* TreeData = Cast<UStateTreeEditorData>(StateTree.EditorData);
		if (!TreeData)
		{
			return;
		}

		TMap<FGuid, const UStruct*> AllStructIDs;
		TreeData->GetAllStructIDs(AllStructIDs);
		TreeData->GetPropertyEditorBindings()->RemoveUnusedBindings(AllStructIDs);
	}
}

void FStateTreeEditor::UpdateAsset()
{
	if (!StateTree)
	{
		return;
	}

	UE::StateTree::Editor::Internal::UpdateParents(*StateTree);
	UE::StateTree::Editor::Internal::ApplySchema(*StateTree);
	UE::StateTree::Editor::Internal::RemoveUnusedBindings(*StateTree);
	UE::StateTree::Editor::Internal::ValidateLinkedStates(*StateTree);

	if (CompilerResultsListing.IsValid())
	{
		CompilerResultsListing->ClearMessages();
	}

	FStateTreeCompilerLog Log;
	FStateTreeBaker Baker(Log);

	const bool bSuccess = Baker.Bake(*StateTree);

	if (CompilerResultsListing.IsValid())
	{
		Log.AppendToLog(CompilerResultsListing.Get());
	}

	if (!bSuccess)
	{
		// Make sure not to leave stale data on failed bake.
		StateTree->ResetBaked();

		// Show log
		TabManager->TryInvokeTab(CompilerResultsTabId);
	}
}


#undef LOCTEXT_NAMESPACE
