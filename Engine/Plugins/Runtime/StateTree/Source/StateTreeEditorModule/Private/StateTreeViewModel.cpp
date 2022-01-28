// Copyright Epic Games, Inc. All Rights Reserved.

#include "StateTreeViewModel.h"
#include "Templates/SharedPointer.h"
#include "StateTree.h"
#include "StateTreeEditorData.h"
#include "StateTreeState.h"
#include "StateTreeTaskBase.h"
#include "StateTreeDelegates.h"
#include "Editor.h"
#include "EditorSupportDelegates.h"
#include "ScopedTransaction.h"
#include "Modules/ModuleManager.h"
#include "StateTreeEditorModule.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"


#define LOCTEXT_NAMESPACE "StateTreeEditor"


namespace FStateTreeViewUtilities
{
	// Remove state from the array. Recurses into children if no match found.
	bool RemoveRecursive(TArray<UStateTreeState*>& Array, UStateTreeState* StateToRemove)
	{
		if (!StateToRemove)
		{
			return false;
		}

		int32 ItemIndex = Array.Find(StateToRemove);
		if (ItemIndex != INDEX_NONE)
		{
			Array.RemoveAt(ItemIndex);
			StateToRemove->Parent = nullptr;
			return true;
		}

		// Did not successfully remove an item. Try all the children.
		for (int32 i = 0; i < Array.Num(); ++i)
		{
			if (RemoveRecursive(Array[i]->Children, StateToRemove))
			{
				return true;
			}
		}

		return false;
	}

	// Insert state in the array relative to target state. Recurses into children if no match found.
	bool InsertRecursive(UStateTreeState* ParentState, TArray<UStateTreeState*>& ParentArray, UStateTreeState* TargetState, UStateTreeState* StateToInsert, int32 RelativeLocation)
	{
		if (!StateToInsert || !TargetState)
		{
			return false;
		}

		const int32 TargetIndex = ParentArray.Find(TargetState);
		if (TargetIndex != INDEX_NONE)
		{
			if (RelativeLocation == -1)
			{
				ParentArray.Insert(StateToInsert, TargetIndex);
				StateToInsert->Parent = ParentState;
			}
			else if (RelativeLocation == 1)
			{
				ParentArray.Insert(StateToInsert, TargetIndex + 1);
				StateToInsert->Parent = ParentState;
			}
			else
			{
				ensure(RelativeLocation == 0);
				ParentArray[TargetIndex]->Children.Insert(StateToInsert, 0);
				StateToInsert->Parent = ParentArray[TargetIndex];
			}
			return true;
		}

		for (int32 i = 0; i < ParentArray.Num(); ++i)
		{
			if (InsertRecursive(ParentArray[i], ParentArray[i]->Children, TargetState, StateToInsert, RelativeLocation))
			{
				return true;
			}
		}

		return false;
	}

	// Removes states from the array which are children of any other state.
	void RemoveContainedChildren(TArray<UStateTreeState*>& States)
	{
		TSet<UStateTreeState*> UniqueStates;
		for (UStateTreeState* State : States)
		{
			UniqueStates.Add(State);
		}

		for (int32 i = 0; i < States.Num(); )
		{
			UStateTreeState* State = States[i];

			// Walk up the parent state sand if the current state
			// exists in any of them, remove it.
			UStateTreeState* StateParent = State->Parent;
			bool bShouldRemove = false;
			while (StateParent)
			{
				if (UniqueStates.Contains(StateParent))
				{
					bShouldRemove = true;
					break;
				}
				StateParent = StateParent->Parent;
			}

			if (bShouldRemove)
			{
				States.RemoveAt(i);
			}
			else
			{
				i++;
			}
		}
	}

	// Returns true if the state is child of parent state.
	bool IsChildOf(const UStateTreeState* ParentState, const UStateTreeState* State)
	{
		for (const UStateTreeState* Child : ParentState->Children)
		{
			if (Child == State)
			{
				return true;
			}
			if (IsChildOf(Child, State))
			{
				return true;
			}
		}
		return false;
	}

};


FStateTreeViewModel::FStateTreeViewModel()
	: TreeData(nullptr)
{
}

FStateTreeViewModel::~FStateTreeViewModel()
{
	GEditor->UnregisterForUndo(this);

	UE::StateTree::Delegates::OnIdentifierChanged.RemoveAll(this);
}

void FStateTreeViewModel::Init(UStateTreeEditorData* InTreeData)
{
	TreeData = InTreeData;

	GEditor->RegisterForUndo(this);

	UE::StateTree::Delegates::OnIdentifierChanged.AddSP(this, &FStateTreeViewModel::HandleIdentifierChanged);
}

void FStateTreeViewModel::HandleIdentifierChanged(const UStateTree& StateTree)
{
	const UStateTree* OuterStateTree = TreeData ? Cast<UStateTree>(TreeData->GetOuter()) : nullptr;
	if (OuterStateTree == &StateTree)
	{
		OnAssetChanged.Broadcast();
	}
}

void FStateTreeViewModel::NotifyAssetChangedExternally()
{
	OnAssetChanged.Broadcast();
}

void FStateTreeViewModel::NotifyStatesChangedExternally(const TSet<UStateTreeState*>& ChangedStates, const FPropertyChangedEvent& PropertyChangedEvent)
{
	OnStatesChanged.Broadcast(ChangedStates, PropertyChangedEvent);
}

TArray<UStateTreeState*>* FStateTreeViewModel::GetSubTrees()
{
	return TreeData != nullptr ? &TreeData->SubTrees : nullptr;
}

int32 FStateTreeViewModel::GetSubTreeCount() const
{
	return TreeData != nullptr ? TreeData->SubTrees.Num() : 0;
}

void FStateTreeViewModel::PostUndo(bool bSuccess)
{
	// TODO: see if we can narrow this down.
	OnAssetChanged.Broadcast();
}

void FStateTreeViewModel::PostRedo(bool bSuccess)
{
	OnAssetChanged.Broadcast();
}

void FStateTreeViewModel::ClearSelection()
{
	SelectedStates.Reset();

	TArray<UStateTreeState*> SelectedStatesArr;
	OnSelectionChanged.Broadcast(SelectedStatesArr);
}

void FStateTreeViewModel::SetSelection(UStateTreeState* SelectedState)
{
	SelectedStates.Reset();

	SelectedStates.Add(SelectedState);

	TArray<UStateTreeState*> SelectedStatesArr;
	SelectedStatesArr.Add(SelectedState);
	OnSelectionChanged.Broadcast(SelectedStatesArr);
}

void FStateTreeViewModel::SetSelection(const TArray<UStateTreeState*>& InSelectedStates)
{
	SelectedStates.Reset();

	for (UStateTreeState* State : InSelectedStates)
	{
		if (State)
		{
			SelectedStates.Add(State);
		}
	}

	TArray<FGuid> SelectedTaskIDArr;
	OnSelectionChanged.Broadcast(InSelectedStates);
}

bool FStateTreeViewModel::IsSelected(const UStateTreeState* State) const
{
	return SelectedStates.Contains(State);
}

bool FStateTreeViewModel::IsChildOfSelection(const UStateTreeState* State) const
{
	for (const FWeakObjectPtr& WeakSelectedState : SelectedStates)
	{
		if (const UStateTreeState* SelectedState = Cast<UStateTreeState>(WeakSelectedState.Get()))
		{
			if (SelectedState == State)
			{
				return true;
			}
			else
			{
				if (FStateTreeViewUtilities::IsChildOf(SelectedState, State))
				{
					return true;
				}
			}
		}
	}
	return false;
}

void FStateTreeViewModel::GetSelectedStates(TArray<UStateTreeState*>& OutSelectedStates)
{
	OutSelectedStates.Reset();
	for (FWeakObjectPtr& WeakState : SelectedStates)
	{
		if (UStateTreeState* State = Cast<UStateTreeState>(WeakState.Get()))
		{
			OutSelectedStates.Add(State);
		}
	}
}

bool FStateTreeViewModel::HasSelection() const
{
	return SelectedStates.Num() > 0;
}

void FStateTreeViewModel::GetPersistentExpandedStates(TSet<UStateTreeState*>& OutExpandedStates)
{
	OutExpandedStates.Reset();
	if (!TreeData)
	{
		return;
	}
	for (UStateTreeState* SubTree : TreeData->SubTrees)
	{
		GetExpandedStatesRecursive(SubTree, OutExpandedStates);
	}
}

void FStateTreeViewModel::GetExpandedStatesRecursive(UStateTreeState* State, TSet<UStateTreeState*>& OutExpandedStates)
{
	if (State->bExpanded)
	{
		OutExpandedStates.Add(State);
	}
	for (UStateTreeState* Child : State->Children)
	{
		GetExpandedStatesRecursive(Child, OutExpandedStates);
	}
}

void FStateTreeViewModel::SetPersistentExpandedStates(TSet<UStateTreeState*>& InExpandedStates)
{
	if (!TreeData)
	{
		return;
	}

	TreeData->Modify();

	for (UStateTreeState* State : InExpandedStates)
	{
		if (State)
		{
			State->bExpanded = true;
		}
	}
}


void FStateTreeViewModel::AddState(UStateTreeState* AfterState)
{
	if (!TreeData)
	{
		return;
	}

	const FScopedTransaction Transaction(LOCTEXT("AddStateTransaction", "Add State"));

	UStateTreeState* NewState = NewObject<UStateTreeState>(TreeData, FName(), RF_Transactional);
	UStateTreeState* ParentState = nullptr;

	if (AfterState)
	{
		ParentState = AfterState->Parent;
		if (ParentState)
		{
			ParentState->Modify();
			NewState->Parent = ParentState;
		}
		else
		{
			TreeData->Modify();
			NewState->Parent = nullptr;
		}

		TArray<UStateTreeState*>& ArrayToAddTo = ParentState ? ParentState->Children : TreeData->SubTrees;
		FStateTreeViewUtilities::InsertRecursive(ParentState, ArrayToAddTo, AfterState, NewState, 1); // Insert after
	}
	else
	{
		TreeData->Modify();
		NewState->Parent = nullptr;
		TreeData->SubTrees.Add(NewState);
	}

	OnStateAdded.Broadcast(ParentState, NewState);
}

void FStateTreeViewModel::AddChildState(UStateTreeState* ParentState)
{
	if (!TreeData || !ParentState)
	{
		return;
	}

	const FScopedTransaction Transaction(LOCTEXT("AddChildStateTransaction", "Add Child State"));

	TreeData->Modify();

	UStateTreeState* NewState = NewObject<UStateTreeState>(TreeData, FName(), RF_Transactional);

	ParentState->Children.Add(NewState);
	NewState->Parent = ParentState;

	OnStateAdded.Broadcast(ParentState, NewState);
}

void FStateTreeViewModel::RenameState(UStateTreeState* State, FName NewName)
{
	if (!State)
	{
		return;
	}

	const FScopedTransaction Transaction(LOCTEXT("RenameTransaction", "Rename"));
	State->Modify();
	State->Name = NewName;

	TSet<UStateTreeState*> AffectedStates;
	AffectedStates.Add(State);

	FProperty* NameProperty = FindFProperty<FProperty>(UStateTreeState::StaticClass(), GET_MEMBER_NAME_CHECKED(UStateTreeState, Name));
	FPropertyChangedEvent PropertyChangedEvent(NameProperty, EPropertyChangeType::ValueSet);
	OnStatesChanged.Broadcast(AffectedStates, PropertyChangedEvent);
}

void FStateTreeViewModel::RemoveSelectedStates()
{
	if (!TreeData)
	{
		return;
	}

	TArray<UStateTreeState*> States;
	GetSelectedStates(States);

	// Remove items whose parent also exists in the selection.
	FStateTreeViewUtilities::RemoveContainedChildren(States);

	if (States.Num() > 0)
	{
		const FScopedTransaction Transaction(LOCTEXT("DeleteStateTransaction", "Delete State"));

		TSet<UStateTreeState*> AffectedParents;

		for (UStateTreeState* State : States)
		{
			if (State)
			{
				if (UStateTreeState* ParentState = State->Parent)
				{
					AffectedParents.Add(ParentState);
					ParentState->Modify();
					FStateTreeViewUtilities::RemoveRecursive(ParentState->Children, State);
				}
				else
				{
					AffectedParents.Add(nullptr);
					TreeData->Modify();
					FStateTreeViewUtilities::RemoveRecursive(TreeData->SubTrees, State);
				}
			}
		}

		OnStatesRemoved.Broadcast(AffectedParents);

		ClearSelection();
	}
}

void FStateTreeViewModel::MoveSelectedStatesBefore(UStateTreeState* TargetState)
{
	MoveSelectedStates(TargetState, -1);
}

void FStateTreeViewModel::MoveSelectedStatesAfter(UStateTreeState* TargetState)
{
	MoveSelectedStates(TargetState, 1);
}

void FStateTreeViewModel::MoveSelectedStatesInto(UStateTreeState* TargetState)
{
	MoveSelectedStates(TargetState, 0);
}

void FStateTreeViewModel::MoveSelectedStates(UStateTreeState* TargetState, int32 RelativeLocation)
{
	if (!TreeData || !TargetState)
	{
		return;
	}

	TArray<UStateTreeState*> States;
	GetSelectedStates(States);

	UStateTreeState* TargetParent = TargetState->Parent;

	// Remove child items whose parent also exists in the selection.
	FStateTreeViewUtilities::RemoveContainedChildren(States);
	FStateTreeViewUtilities::RemoveRecursive(States, TargetState);

	if (States.Num() > 0 && TargetState)
	{
		const FScopedTransaction Transaction(LOCTEXT("MoveTransaction", "Move"));

		TSet<UStateTreeState*> AffectedParents;
		TSet<UStateTreeState*> AffectedStates;

		AffectedParents.Add(TargetParent);
		for (int32 i = States.Num() - 1; i >= 0; i--)
		{
			if (UStateTreeState* State = States[i])
			{
				AffectedParents.Add(State->Parent);
			}
		}

		for (UStateTreeState* Parent : AffectedParents)
		{
			if (Parent)
			{
				Parent->Modify();
			}
			else
			{
				TreeData->Modify();
			}
		}

		// Add in reverse order to keep the original order.
		for (int32 i = States.Num() - 1; i >= 0; i--)
		{
			if (UStateTreeState* SelectedState = States[i])
			{
				UStateTreeState* SelectedParent = SelectedState->Parent;
				AffectedStates.Add(SelectedState);

				TArray<UStateTreeState*>& ArrayToRemoveFrom = SelectedParent ? SelectedParent->Children : TreeData->SubTrees;
				TArray<UStateTreeState*>& ArrayToMoveTo = TargetParent ? TargetParent->Children : TreeData->SubTrees;
				FStateTreeViewUtilities::RemoveRecursive(ArrayToRemoveFrom, SelectedState);
				FStateTreeViewUtilities::InsertRecursive(TargetParent, ArrayToMoveTo, TargetState, SelectedState, RelativeLocation);
			}
		}

		OnStatesMoved.Broadcast(AffectedParents, AffectedStates);

		SetSelection(States);
	}
}


#undef LOCTEXT_NAMESPACE
