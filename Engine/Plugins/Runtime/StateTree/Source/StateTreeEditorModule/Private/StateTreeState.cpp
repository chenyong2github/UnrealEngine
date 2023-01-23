// Copyright Epic Games, Inc. All Rights Reserved.

#include "StateTreeState.h"
#include "StateTree.h"
#include "StateTreeEditorData.h"
#include "StateTreeConditionBase.h"
#include "StateTreeTaskBase.h"
#include "StateTreeDelegates.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(StateTreeState)

//////////////////////////////////////////////////////////////////////////
// FStateTreeTransition

FStateTreeTransition::FStateTreeTransition(const EStateTreeTransitionTrigger InTrigger, const EStateTreeTransitionType InType, const UStateTreeState* InState)
	: Trigger(InTrigger)
{
	State = InState ? InState->GetLinkToState() : FStateTreeStateLink(InType);
}

FStateTreeTransition::FStateTreeTransition(const EStateTreeTransitionTrigger InTrigger, const FGameplayTag InEventTag, const EStateTreeTransitionType InType, const UStateTreeState* InState)
	: Trigger(InTrigger)
	, EventTag(InEventTag)
{
	State = InState ? InState->GetLinkToState() : FStateTreeStateLink(InType);
}


//////////////////////////////////////////////////////////////////////////
// UStateTreeState

UStateTreeState::UStateTreeState(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, ID(FGuid::NewGuid())
{
	Parameters.ID = FGuid::NewGuid();
}

#if WITH_EDITOR
void UStateTreeState::PostEditChangeChainProperty(FPropertyChangedChainEvent& PropertyChangedEvent)
{
	Super::PostEditChangeChainProperty(PropertyChangedEvent);

	FProperty* Property = PropertyChangedEvent.Property;
	FProperty* MemberProperty = nullptr;
	if (PropertyChangedEvent.PropertyChain.GetActiveMemberNode())
	{
		MemberProperty = PropertyChangedEvent.PropertyChain.GetActiveMemberNode()->GetValue();
	}

	auto TryGetConditionIndex = [&PropertyChangedEvent]() -> int32
	{
		check (PropertyChangedEvent.PropertyChain.GetActiveMemberNode());
		FEditPropertyChain::TDoubleLinkedListNode* ConditionPropertyNode = PropertyChangedEvent.PropertyChain.GetActiveMemberNode()->GetNextNode();
		FProperty* ConditionsProperty = ConditionPropertyNode ? ConditionPropertyNode->GetValue() : nullptr;
		if (ConditionsProperty && ConditionsProperty->GetFName() == GET_MEMBER_NAME_CHECKED(FStateTreeTransition, Conditions))
		{
			return PropertyChangedEvent.GetArrayIndex(ConditionsProperty->GetFName().ToString());
		}
		return INDEX_NONE;
	};

	auto CopyBindings = [this](const FGuid FromStructID, const FGuid ToStructID)
	{
		if (UStateTreeEditorData* EditorData = GetTypedOuter<UStateTreeEditorData>())
		{
		    if (FromStructID.IsValid())
            {
                if (FStateTreeEditorPropertyBindings* Bindings = EditorData->GetPropertyEditorBindings())
                {
                	Bindings->CopyBindings(FromStructID, ToStructID);
                }
            }
		}
	};

	
	if (Property)
	{
		if (Property->GetOwnerClass() == UStateTreeState::StaticClass()
			&& Property->GetFName() == GET_MEMBER_NAME_CHECKED(UStateTreeState, Name))
		{
			UStateTree* StateTree = GetTypedOuter<UStateTree>();
			if (ensure(StateTree))
			{
				UE::StateTree::Delegates::OnIdentifierChanged.Broadcast(*StateTree);
			}
		}

		if (Property->GetOwnerClass() == UStateTreeState::StaticClass()
			&& Property->GetFName() == GET_MEMBER_NAME_CHECKED(UStateTreeState, Type))
		{
			// Remove any tasks and evaluators when they are not used.
			if (Type == EStateTreeStateType::Group || Type == EStateTreeStateType::Linked)
			{
				Tasks.Reset();
			}

			// If transitioning from linked state, reset the linked state.
			if (Type != EStateTreeStateType::Linked)
			{
				LinkedSubtree = FStateTreeStateLink();
			}

			if (Type == EStateTreeStateType::Linked)
			{
				// Linked parameter layout is fixed, and copied from the linked target state.
				Parameters.bFixedLayout = true;
				UpdateParametersFromLinkedSubtree();
			}
			else if (Type == EStateTreeStateType::Subtree)
			{
				// Subtree parameter layout can be edited
				Parameters.bFixedLayout = false;
			}
			else
			{
				Parameters.Reset();
			}
		}

		if (Property->GetOwnerClass() == UStateTreeState::StaticClass()
			&& Property->GetFName() == GET_MEMBER_NAME_CHECKED(UStateTreeState, LinkedSubtree))
		{
			// When switching to new state, update the parameters.
			if (Type == EStateTreeStateType::Linked)
			{
				UpdateParametersFromLinkedSubtree();
			}
		}

		if (Property->GetOwnerClass() == UStateTreeState::StaticClass()
			&& Property->GetFName() == GET_MEMBER_NAME_CHECKED(UStateTreeState, Parameters))
		{
			if (Type == EStateTreeStateType::Subtree)
			{
				// Broadcast subtree parameter edits so that the linked states can adapt.
				const UStateTree* StateTree = GetTypedOuter<UStateTree>();
				if (ensure(StateTree))
				{
					UE::StateTree::Delegates::OnStateParametersChanged.Broadcast(*StateTree, ID);
				}
			}
		}

		if (MemberProperty)
		{
			// Ensure unique ID on duplicated items.
			if (PropertyChangedEvent.ChangeType == EPropertyChangeType::Duplicate)
			{
				if (MemberProperty->GetFName() == GET_MEMBER_NAME_CHECKED(UStateTreeState, Tasks))
				{
					const int32 ArrayIndex = PropertyChangedEvent.GetArrayIndex(MemberProperty->GetFName().ToString());
					if (Tasks.IsValidIndex(ArrayIndex))
					{
						if (FStateTreeTaskBase* Task = Tasks[ArrayIndex].Node.GetMutablePtr<FStateTreeTaskBase>())
						{
							Task->Name = FName(Task->Name.ToString() + TEXT(" Duplicate"));
						}
						const FGuid OldStructID = Tasks[ArrayIndex].ID; 
						Tasks[ArrayIndex].ID = FGuid::NewGuid();
						CopyBindings(OldStructID, Tasks[ArrayIndex].ID);
					}
				}
				if (MemberProperty->GetFName() == GET_MEMBER_NAME_CHECKED(UStateTreeState, EnterConditions))
				{
					const int32 ArrayIndex = PropertyChangedEvent.GetArrayIndex(MemberProperty->GetFName().ToString());
					if (EnterConditions.IsValidIndex(ArrayIndex))
					{
						if (FStateTreeConditionBase* Condition = EnterConditions[ArrayIndex].Node.GetMutablePtr<FStateTreeConditionBase>())
						{
							Condition->Name = FName(Condition->Name.ToString() + TEXT(" Duplicate"));
						}
						const FGuid OldStructID = EnterConditions[ArrayIndex].ID; 
						EnterConditions[ArrayIndex].ID = FGuid::NewGuid();
						CopyBindings(OldStructID, EnterConditions[ArrayIndex].ID);
					}
				}
				if (MemberProperty->GetFName() == GET_MEMBER_NAME_CHECKED(UStateTreeState, Transitions))
				{
					const int32 TransitionsIndex = PropertyChangedEvent.GetArrayIndex(MemberProperty->GetFName().ToString());
					const int32 ConditionsIndex = TryGetConditionIndex();

					if (Transitions.IsValidIndex(TransitionsIndex))
					{
						FStateTreeTransition& Transition = Transitions[TransitionsIndex];
						if (Transition.Conditions.IsValidIndex(ConditionsIndex))
						{
							if (FStateTreeConditionBase* Condition = Transition.Conditions[ConditionsIndex].Node.GetMutablePtr<FStateTreeConditionBase>())
							{
								Condition->Name = FName(Condition->Name.ToString() + TEXT(" Duplicate"));
							}
							const FGuid OldStructID = Transition.Conditions[ConditionsIndex].ID;
							Transition.Conditions[ConditionsIndex].ID = FGuid::NewGuid();
							CopyBindings(OldStructID, Transition.Conditions[ConditionsIndex].ID);
						}
					}
				}
			}
			else if (PropertyChangedEvent.ChangeType == EPropertyChangeType::ArrayAdd)
			{
				if (MemberProperty->GetFName() == GET_MEMBER_NAME_CHECKED(UStateTreeState, Transitions))
				{
					const int32 TransitionsIndex = PropertyChangedEvent.GetArrayIndex(MemberProperty->GetFName().ToString());
					if (Transitions.IsValidIndex(TransitionsIndex))
					{
						// Set default transition on newly created states.
						FStateTreeTransition& Transition = Transitions[TransitionsIndex];
						Transition.Trigger = EStateTreeTransitionTrigger::OnStateCompleted;
						const UStateTreeState* RootState = GetRootState();
						Transition.State = RootState->GetLinkToState();
					}
				}
			}
			else if (PropertyChangedEvent.ChangeType == EPropertyChangeType::ArrayRemove)
			{
				if (MemberProperty->GetFName() == GET_MEMBER_NAME_CHECKED(UStateTreeState, Tasks)
					|| MemberProperty->GetFName() == GET_MEMBER_NAME_CHECKED(UStateTreeState, EnterConditions)
					|| MemberProperty->GetFName() == GET_MEMBER_NAME_CHECKED(UStateTreeState, Transitions))
				{
					if (UStateTreeEditorData* TreeData = GetTypedOuter<UStateTreeEditorData>())
					{
						TreeData->Modify();
						FStateTreeEditorPropertyBindings* Bindings = TreeData->GetPropertyEditorBindings();
						check(Bindings);
						TMap<FGuid, const UStruct*> AllStructIDs;
						TreeData->GetAllStructIDs(AllStructIDs);
						Bindings->RemoveUnusedBindings(AllStructIDs);
					}
				}
			}
			else
			{
				if (MemberProperty->GetFName() == GET_MEMBER_NAME_CHECKED(UStateTreeState, Transitions))
				{
					const int32 TransitionsIndex = PropertyChangedEvent.GetArrayIndex(MemberProperty->GetFName().ToString());
					if (Transitions.IsValidIndex(TransitionsIndex))
					{
						FStateTreeTransition& Transition = Transitions[TransitionsIndex];

						if (EnumHasAnyFlags(Transition.Trigger, EStateTreeTransitionTrigger::OnStateCompleted))
						{
							// Reset delay on completion transitions
							Transition.bDelayTransition = false;
						}
					}
				}
			}
		}
	}
}

void UStateTreeState::PostLoad()
{
	Super::PostLoad();

	// Make sure state has transactional flags to make it work with undo (to fix a bug where root states were created without this flag).
	if (!HasAnyFlags(RF_Transactional))
	{
		SetFlags(RF_Transactional);
	}

#if WITH_EDITORONLY_DATA
	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	// Move deprecated evaluators to editor data.
	if (Evaluators_DEPRECATED.Num() > 0)
	{
		if (UStateTreeEditorData* TreeData = GetTypedOuter<UStateTreeEditorData>())
		{
			TreeData->Evaluators.Append(Evaluators_DEPRECATED);
			Evaluators_DEPRECATED.Reset();
		}		
	}
	PRAGMA_ENABLE_DEPRECATION_WARNINGS
#endif
}

void UStateTreeState::UpdateParametersFromLinkedSubtree()
{
	if (const UStateTreeEditorData* TreeData = GetTypedOuter<UStateTreeEditorData>())
	{
		if (const UStateTreeState* LinkTargetState = TreeData->GetStateByID(LinkedSubtree.ID))
		{
			Parameters.Parameters.MigrateToNewBagInstance(LinkTargetState->Parameters.Parameters);
		}
	}
}

#endif

const UStateTreeState* UStateTreeState::GetRootState() const
{
	const UStateTreeState* RootState = this;
	while (RootState->Parent != nullptr)
	{
		RootState = RootState->Parent;
	}
	return RootState;
}

const UStateTreeState* UStateTreeState::GetNextSiblingState() const
{
	if (!Parent)
	{
		return nullptr;
	}
	for (int32 ChildIdx = 0; ChildIdx < Parent->Children.Num(); ChildIdx++)
	{
		if (Parent->Children[ChildIdx] == this)
		{
			const int NextIdx = ChildIdx + 1;
			if (NextIdx < Parent->Children.Num())
			{
				return Parent->Children[NextIdx];
			}
			break;
		}
	}
	return nullptr;
}

FStateTreeStateLink UStateTreeState::GetLinkToState() const
{
	FStateTreeStateLink Link(EStateTreeTransitionType::GotoState);
	Link.Name = Name;
	Link.ID = ID;
	return Link;
}
