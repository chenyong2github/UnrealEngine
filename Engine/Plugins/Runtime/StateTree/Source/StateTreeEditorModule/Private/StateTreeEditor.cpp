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
#include "StateTreeEvaluatorBase.h"
#include "StateTreeTaskBase.h"
#include "StateTreeBaker.h"
#include "IDetailsView.h"
#include "Widgets/Docking/SDockTab.h"
#include "Customizations/StateTreeBindingExtension.h"
#include "Logging/MessageLog.h"


#define LOCTEXT_NAMESPACE "StateTreeEditor"

const FName StateTreeEditorAppName(TEXT("StateTreeEditorApp"));

const FName FStateTreeEditor::SelectionDetailsTabId(TEXT("StateTreeEditor_SelectionDetails"));
const FName FStateTreeEditor::AssetDetailsTabId(TEXT("StateTreeEditor_AssetDetails"));
const FName FStateTreeEditor::StateTreeViewTabId(TEXT("StateTreeEditor_StateTreeView"));

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
}

void FStateTreeEditor::UnregisterTabSpawners(const TSharedRef<class FTabManager>& InTabManager)
{
	FAssetEditorToolkit::UnregisterTabSpawners(InTabManager);

	InTabManager->UnregisterTabSpawner(SelectionDetailsTabId);
	InTabManager->UnregisterTabSpawner(AssetDetailsTabId);
	InTabManager->UnregisterTabSpawner(StateTreeViewTabId);
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


	TSharedRef<FTabManager::FLayout> StandaloneDefaultLayout = FTabManager::NewLayout("Standalone_StateTree_Layout_v2")
	->AddArea
	(
		FTabManager::NewPrimaryArea() ->SetOrientation(Orient_Vertical)
		->Split
		(
			FTabManager::NewSplitter() ->SetOrientation(Orient_Horizontal)
			->Split
			(
				FTabManager::NewStack()
				->SetSizeCoefficient(0.2f)
				->AddTab(AssetDetailsTabId, ETabState::OpenedTab)
				->SetForegroundTab(AssetDetailsTabId)
			)
			->Split
			(
				FTabManager::NewStack()
				->SetSizeCoefficient(0.5f)
				->AddTab(StateTreeViewTabId, ETabState::OpenedTab)
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
		if (StateTreeViewModel)
		{
			StateTreeViewModel->NotifyAssetChangedExternally();
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

		for (UStateTreeState* Routine : TreeData->Routines)
		{
			if (Routine)
			{
				Stack.Reset();
				Stack.Add(Routine);
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
		for (UStateTreeState* Routine : TreeData->Routines)
		{
			if (Routine)
			{
				Stack.Reset();
				Stack.Add(Routine);
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

		for (UStateTreeState* Routine : TreeData->Routines)
		{
			if (Routine)
			{
				Routine->Parent = nullptr;
				Stack.Reset();
				Stack.Add(Routine);
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

	void RemoveUnusedBindings(UStateTree& StateTree)
	{
		UStateTreeEditorData* TreeData = Cast<UStateTreeEditorData>(StateTree.EditorData);
		if (!TreeData)
		{
			return;
		}

		TMap<FGuid, const UScriptStruct*> AllStructIDs;
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
	UE::StateTree::Editor::Internal::RemoveUnusedBindings(*StateTree);
	UE::StateTree::Editor::Internal::ValidateLinkedStates(*StateTree);

	FStateTreeBaker Baker;
	if (!Baker.Bake(*StateTree))
	{
		FMessageLog EditorErrors("StateTree");
		EditorErrors.Error(FText::Format(LOCTEXT("StateTreeBakerFailed", "Failed to bake state tree asset {0}, see the log for details"), FText::FromString(StateTree->GetName())));
		EditorErrors.Notify(FText::Format(LOCTEXT("StateTreeBakerFailed", "Failed to bake state tree asset {0}, see the log for details"), FText::FromString(StateTree->GetName())));

		// Make sure not to leave stale data on failed bake.
		StateTree->ResetBaked();
	}
}


#undef LOCTEXT_NAMESPACE
