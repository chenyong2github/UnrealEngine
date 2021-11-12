// Copyright Epic Games, Inc. All Rights Reserved.

#include "StateTreeEditorData.h"
#include "StateTreeConditionBase.h"
#include "CoreMinimal.h"

void UStateTreeEditorData::GetAccessibleStructs(const FGuid TargetStructID, TArray<FStateTreeBindableStructDesc>& OutStructDescs) const
{
	// Find the states that are updated before the current state.
	TSet<const UStateTreeState*> ValidStates;
	const UStateTreeState* CurrentState = GetStateByStructID(TargetStructID);
	for (const UStateTreeState* State = CurrentState; State != nullptr; State = State->Parent)
	{
		ValidStates.Add(State);
	}

	TArray<FStateTreeBindableStructDesc> EvalDescs;
	TArray<FStateTreeBindableStructDesc> TaskDescs;

	if (ValidStates.Num() > 0)
	{
		VisitHierarchy([&ValidStates, &OutStructDescs, &EvalDescs, &TaskDescs, TargetStructID, CurrentState](const UStateTreeState& State, const FGuid& ID, const FName& Name, const UScriptStruct* ItemStruct, const UStruct* InstanceStruct) -> bool
			{
				if (ValidStates.Contains(&State))
				{
					if (ID == TargetStructID)
					{
						OutStructDescs.Append(EvalDescs);

						// Only tasks can see other tasks too.
						if (ItemStruct->IsChildOf(FStateTreeTaskBase::StaticStruct()))
						{
							OutStructDescs.Append(TaskDescs);
						}
						
						return false; // Stop visit
					}

					if (ItemStruct->IsChildOf(FStateTreeEvaluatorBase::StaticStruct()))
					{
						// All evaluators up to the target struct are accessible.
						FStateTreeBindableStructDesc& Desc = EvalDescs.AddDefaulted_GetRef();
						Desc.Struct = InstanceStruct;
						Desc.Name = Name;
						Desc.ID = ID;
					}
					else if (ItemStruct->IsChildOf(FStateTreeTaskBase::StaticStruct()))
					{
						// All tasks up to the target struct are accessible (for other tasks).
						FStateTreeBindableStructDesc& Desc = TaskDescs.AddDefaulted_GetRef();
						Desc.Struct = InstanceStruct;
						Desc.Name = Name;
						Desc.ID = ID;
					}
				}
				return true; // Continue
			});
	}
}

bool UStateTreeEditorData::GetStructByID(const FGuid StructID, FStateTreeBindableStructDesc& OutStructDesc) const
{
	bool bResult = false;

	VisitHierarchy([&bResult, &OutStructDesc, StructID](const UStateTreeState& State, const FGuid& ID, const FName& Name, const UScriptStruct* ItemStruct, const UStruct* InstanceStruct) -> bool
		{
			if (ID == StructID)
			{
				OutStructDesc.Struct = InstanceStruct;
				OutStructDesc.Name = Name;
				OutStructDesc.ID = ID;
				bResult = true;
				return false; // Stop visit
			}
			return true; // Continue
		});

	return bResult;
}

const UStateTreeState* UStateTreeEditorData::GetStateByStructID(const FGuid TargetStructID) const
{
	const UStateTreeState* Result = nullptr;

	VisitHierarchy([&Result, TargetStructID](const UStateTreeState& State, const FGuid& ID, const FName& Name, const UScriptStruct* ItemStruct, const UStruct* InstanceStruct) -> bool
		{
			if (ID == TargetStructID)
			{
				Result = &State;
				return false; // Stop visit
			}
			return true; // Continue
		});

	return Result;
}

void UStateTreeEditorData::GetAllStructIDs(TMap<FGuid, const UStruct*>& AllStructs) const
{
	AllStructs.Reset();

	VisitHierarchy([&AllStructs](const UStateTreeState& State, const FGuid& ID, const FName& Name, const UScriptStruct* ItemStruct, const UStruct* InstanceStruct) -> bool
		{
			AllStructs.Add(ID, InstanceStruct);
			return true; // Continue
		});
}

void UStateTreeEditorData::VisitHierarchy(TFunctionRef<bool(const UStateTreeState& State, const FGuid& ID, const FName& Name, const UScriptStruct* ItemStruct, const UStruct* InstanceStruct)> InFunc) const
{
	TArray<const UStateTreeState*> Stack;
	bool bContinue = true;

	for (const UStateTreeState* Routine : Routines)
	{
		if (!Routine)
		{
			continue;
		}

		Stack.Add(Routine);

		while (!Stack.IsEmpty() && bContinue)
		{
			const UStateTreeState* State = Stack[0];
			check(State);

			Stack.RemoveAt(0);

			// Evaluators
			for (const FStateTreeEvaluatorItem& Item : State->Evaluators)
			{
				if (const FStateTreeEvaluatorBase* Evaluator = Item.Item.GetPtr<FStateTreeEvaluatorBase>())
				{
					if (!InFunc(*State, Item.ID, Evaluator->Name, Item.Item.GetScriptStruct(), Evaluator->GetInstanceDataType()))
					{
						bContinue = false;
						break;
					}
				}
			}
			if (bContinue)
			{
				// Enter conditions
				for (const FStateTreeConditionItem& Item : State->EnterConditions)
				{
					if (const FStateTreeConditionBase* Cond = Item.Item.GetPtr<FStateTreeConditionBase>())
					{
						if (!InFunc(*State, Item.ID, Item.Item.GetScriptStruct()->GetFName(), Item.Item.GetScriptStruct(), Cond->GetInstanceDataType()))
						{
							bContinue = false;
							break;
						}
					}
				}
			}
			if (bContinue)
			{
				// Tasks
				for (const FStateTreeTaskItem& Item : State->Tasks)
				{
					if (const FStateTreeTaskBase* Task = Item.Item.GetPtr<FStateTreeTaskBase>())
					{
						if (!InFunc(*State, Item.ID, Task->Name, Item.Item.GetScriptStruct(), Task->GetInstanceDataType()))
						{
							bContinue = false;
							break;
						}
					}
				}
			}
			if (bContinue)
			{
				// Transitions
				for (const FStateTreeTransition& Transition : State->Transitions)
				{
					for (const FStateTreeConditionItem& Item : Transition.Conditions)
					{
						if (const FStateTreeConditionBase* Cond = Item.Item.GetPtr<FStateTreeConditionBase>())
						{
							if (!InFunc(*State, Item.ID, Item.Item.GetScriptStruct()->GetFName(), Item.Item.GetScriptStruct(), Cond->GetInstanceDataType()))
							{
								bContinue = false;
								break;
							}
						}
					}
				}
			}
			if (bContinue)
			{
				// Children
				for (const UStateTreeState* ChildState : State->Children)
				{
					Stack.Add(ChildState);
				}
			}
		}
		if (!bContinue)
		{
			break;
		}
	}
}

