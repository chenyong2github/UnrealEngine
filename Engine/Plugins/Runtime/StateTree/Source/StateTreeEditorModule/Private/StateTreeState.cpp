// Copyright Epic Games, Inc. All Rights Reserved.

#include "StateTreeState.h"
#include "StateTree.h"
#include "StateTreeConditionBase.h"
#include "StateTreeDelegates.h"
#include "CoreMinimal.h"
#include "UObject/Field.h"

//////////////////////////////////////////////////////////////////////////
// FStateTreeStateLink

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


//////////////////////////////////////////////////////////////////////////
// FStateTreeTransition

FStateTreeTransition::FStateTreeTransition(const EStateTreeTransitionEvent InEvent, const EStateTreeTransitionType InType, const UStateTreeState* InState)
{
	Event = InEvent;
	State.Set(InType, InState);
}


//////////////////////////////////////////////////////////////////////////
// UStateTreeState

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

		if (MemberProperty)
		{
			// Ensure unique ID on duplicated items.
			if (PropertyChangedEvent.ChangeType == EPropertyChangeType::Duplicate)
			{
				if (MemberProperty->GetFName() == GET_MEMBER_NAME_CHECKED(UStateTreeState, Evaluators))
				{
					const int32 ArrayIndex = PropertyChangedEvent.GetArrayIndex(MemberProperty->GetFName().ToString());
					if (Evaluators.IsValidIndex(ArrayIndex))
					{
						if (FStateTreeEvaluator2Base* Eval = Evaluators[ArrayIndex].Type.GetMutablePtr<FStateTreeEvaluator2Base>())
						{
							Eval->ID = FGuid::NewGuid();
							Eval->Name = FName(Eval->Name.ToString() + TEXT(" Duplicate"));
						}
					}
				}
				if (MemberProperty->GetFName() == GET_MEMBER_NAME_CHECKED(UStateTreeState, Tasks))
				{
					const int32 ArrayIndex = PropertyChangedEvent.GetArrayIndex(MemberProperty->GetFName().ToString());
					if (Tasks.IsValidIndex(ArrayIndex))
					{
						if (FStateTreeTask2Base* Task = Tasks[ArrayIndex].Type.GetMutablePtr<FStateTreeTask2Base>())
						{
							Task->ID = FGuid::NewGuid();
							Task->Name = FName(Task->Name.ToString() + TEXT(" Duplicate"));
						}
					}
				}
				if (MemberProperty->GetFName() == GET_MEMBER_NAME_CHECKED(UStateTreeState, EnterConditions))
				{
					const int32 ArrayIndex = PropertyChangedEvent.GetArrayIndex(MemberProperty->GetFName().ToString());
					if (EnterConditions.IsValidIndex(ArrayIndex))
					{
						if (FStateTreeConditionBase* Cond = EnterConditions[ArrayIndex].Type.GetMutablePtr<FStateTreeConditionBase>())
						{
							Cond->ID = FGuid::NewGuid();
						}
					}
				}
				// TODO: Transition conditions.
			}
		}
	}
}

#endif

const FStateTreeTask2Base* UStateTreeState::GetTaskByID(FGuid InID) const
{
	for (const FStateTreeTaskItem& TaskItem : Tasks)
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

FStateTreeTask2Base* UStateTreeState::GetTaskByID(FGuid InID)
{
	for (FStateTreeTaskItem& TaskItem : Tasks)
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
