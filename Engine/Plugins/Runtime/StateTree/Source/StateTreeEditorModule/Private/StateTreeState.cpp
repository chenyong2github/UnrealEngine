// Copyright Epic Games, Inc. All Rights Reserved.

#include "StateTreeState.h"
#include "StateTreeEditorData.h"
#include "StateTree.h"
#include "StateTreeConditionBase.h"
#include "StateTreeDelegates.h"
#include "CoreMinimal.h"
#include "UObject/Field.h"

void FStateTreeStateLink::Set(const EStateTreeTransitionType InType, const UStateTreeState* InState)
{
	Type = InType;
	if (Type == EStateTreeTransitionType::GotoState)
	{
		check(InState);
		Name = InState->Name;
		ID = InState->ID;
	}
}


FStateTreeTransition2::FStateTreeTransition2(const EStateTreeTransitionEvent InEvent, const EStateTreeTransitionType InType, const UStateTreeState* InState)
{
	Event = InEvent;
	State.Set(InType, InState);
}


UStateTreeState::UStateTreeState(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, ID(FGuid::NewGuid())
	, Parent(nullptr)
{
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

	if (Property)
	{
		if (Property->GetOwnerClass() == UStateTreeState::StaticClass() &&
			Property->GetFName() == GET_MEMBER_NAME_CHECKED(UStateTreeState, Name))
		{
			UStateTree* StateTree = GetTypedOuter<UStateTree>();
			if (ensure(StateTree))
			{
				UE::StateTree::Delegates::OnIdentifierChanged.Broadcast(*StateTree);
			}
		}

		if (PropertyChangedEvent.ChangeType == EPropertyChangeType::Duplicate && MemberProperty)
		{
			// Ensure unique ID on duplicated items.
			if (MemberProperty->GetFName() == GET_MEMBER_NAME_CHECKED(UStateTreeState, Evaluators))
			{
				const int32 ArrayIndex = PropertyChangedEvent.GetArrayIndex(MemberProperty->GetFName().ToString());
				if (Evaluators.IsValidIndex(ArrayIndex))
				{
					if (UStateTreeEvaluatorBase* Eval = Evaluators[ArrayIndex])
					{
						Eval->SetNewUniqueID();
						Eval->SetName(FName(Eval->GetName().ToString() + TEXT(" Duplicate")));
					}
				}
			}
			if (MemberProperty->GetFName() == GET_MEMBER_NAME_CHECKED(UStateTreeState, Tasks))
			{
				const int32 ArrayIndex = PropertyChangedEvent.GetArrayIndex(MemberProperty->GetFName().ToString());
				if (Tasks.IsValidIndex(ArrayIndex))
				{
					if (UStateTreeTaskBase* Task = Tasks[ArrayIndex])
					{
						Task->SetNewUniqueID();
						Task->Name = FName(Task->Name.ToString() + TEXT(" Duplicate"));
					}
				}
			}
		}


		if (IsV2())
		{
			if (MemberProperty)
			{
				// Ensure unique ID on duplicated items.
				if (PropertyChangedEvent.ChangeType == EPropertyChangeType::Duplicate)
				{
					if (MemberProperty->GetFName() == GET_MEMBER_NAME_CHECKED(UStateTreeState, Evaluators2))
					{
						const int32 ArrayIndex = PropertyChangedEvent.GetArrayIndex(MemberProperty->GetFName().ToString());
						if (Evaluators2.IsValidIndex(ArrayIndex))
						{
							if (FStateTreeEvaluator2Base* Eval = Evaluators2[ArrayIndex].Type.GetMutablePtr<FStateTreeEvaluator2Base>())
							{
								Eval->ID = FGuid::NewGuid();
								Eval->Name = FName(Eval->Name.ToString() + TEXT(" Duplicate"));
							}
						}
					}
					if (MemberProperty->GetFName() == GET_MEMBER_NAME_CHECKED(UStateTreeState, Tasks2))
					{
						const int32 ArrayIndex = PropertyChangedEvent.GetArrayIndex(MemberProperty->GetFName().ToString());
						if (Tasks2.IsValidIndex(ArrayIndex))
						{
							if (FStateTreeTask2Base* Task = Tasks2[ArrayIndex].Type.GetMutablePtr<FStateTreeTask2Base>())
							{
								Task->ID = FGuid::NewGuid();
								Task->Name = FName(Task->Name.ToString() + TEXT(" Duplicate"));
							}
						}
					}
					if (MemberProperty->GetFName() == GET_MEMBER_NAME_CHECKED(UStateTreeState, EnterConditions2))
					{
						const int32 ArrayIndex = PropertyChangedEvent.GetArrayIndex(MemberProperty->GetFName().ToString());
						if (Tasks2.IsValidIndex(ArrayIndex))
						{
							if (FStateTreeConditionBase* Cond = EnterConditions2[ArrayIndex].Type.GetMutablePtr<FStateTreeConditionBase>())
							{
								Cond->ID = FGuid::NewGuid();
							}
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
	
	UStateTree* StateTree = GetTypedOuter<UStateTree>();
	if (StateTree->IsV2())
	{
		// Convert existing default transitions.
		Modify();

		if (StateFailedTransition.Type != EStateTreeTransitionType::NotSet
			&& StateFailedTransition.Type != EStateTreeTransitionType::SelectChildState)
		{
			if (StateFailedTransition.IsValid())
			{
				FStateTreeTransition2& Transition = Transitions2.AddDefaulted_GetRef();
				Transition.Event = EStateTreeTransitionEvent::OnFailed;
				Transition.State = StateFailedTransition;
			}
			// Reset
			StateFailedTransition = FStateTreeStateLink(EStateTreeTransitionType::NotSet);
		}

		if (StateDoneTransition.Type != EStateTreeTransitionType::NotSet
			&& StateDoneTransition.Type != EStateTreeTransitionType::SelectChildState) // Select child state is inferred automatically in V2
		{
			if (StateDoneTransition.IsValid())
			{
				FStateTreeTransition2& Transition = Transitions2.AddDefaulted_GetRef();
				Transition.Event = EStateTreeTransitionEvent::OnCompleted;
				Transition.State = StateDoneTransition;
			}
			// Reset
			StateDoneTransition = FStateTreeStateLink(EStateTreeTransitionType::NotSet);
		}
	}
}

#endif

void UStateTreeState::GetVisibleVariables(FStateTreeVariableLayout& Variables) const
{
	// Add input parameters
	UStateTree* StateTree = GetTypedOuter<UStateTree>();
	if (ensure(StateTree))
	{
		const FStateTreeVariableLayout& ParameterLayout = StateTree->GetInputParameterLayout();
		for (const FStateTreeVariableDesc& Var : ParameterLayout.Variables)
		{
			Variables.DefineVariable(Var);
		}
	}

	// Collect states variable descs up to the highest parent
	for (const UStateTreeState* State = this; State; State = State->Parent)
	{
		for (const UStateTreeEvaluatorBase* Evaluator : State->Evaluators)
		{
			if (Evaluator)
			{
				Evaluator->DefineOutputVariables(Variables);
			}
		}
	}
}

const UStateTreeTaskBase* UStateTreeState::GetTaskByID(FGuid InID) const
{
	const int32 Index = Tasks.IndexOfByPredicate([InID](const UStateTreeTaskBase* Task) -> bool { return Task->ID == InID; });
	return Index != INDEX_NONE ? Tasks[Index] : nullptr;
}

UStateTreeTaskBase* UStateTreeState::GetTaskByID(FGuid InID)
{
	const int32 Index = Tasks.IndexOfByPredicate([InID](const UStateTreeTaskBase* Task) -> bool { return Task->ID == InID; });
	return Index != INDEX_NONE ? Tasks[Index] : nullptr;
}

const FStateTreeTask2Base* UStateTreeState::GetTask2ByID(FGuid InID) const
{
	for (const FStateTreeTaskItem& TaskItem : Tasks2)
	{
		if (const FStateTreeTask2Base* Task = TaskItem.Type.GetPtr<FStateTreeTask2Base>())
		{
			if (Task->ID == InID)
			{
				return Task;
			}
		}
	}
	return nullptr;
}

FStateTreeTask2Base* UStateTreeState::GetTask2ByID(FGuid InID)
{
	for (FStateTreeTaskItem& TaskItem : Tasks2)
	{
		if (FStateTreeTask2Base* Task = TaskItem.Type.GetMutablePtr<FStateTreeTask2Base>())
		{
			if (Task->ID == InID)
			{
				return Task;
			}
		}
	}
	return nullptr;
}

UStateTreeState* UStateTreeState::GetNextSiblingState() const
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

bool UStateTreeState::IsV2() const
{
	if (const UStateTree* StateTree = GetTypedOuter<UStateTree>())
	{
		return StateTree->IsV2();
	}
	return false;
}