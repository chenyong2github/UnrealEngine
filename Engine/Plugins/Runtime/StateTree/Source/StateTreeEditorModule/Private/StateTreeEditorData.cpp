// Copyright Epic Games, Inc. All Rights Reserved.

#include "StateTreeEditorData.h"
#include "StateTreeConditionBase.h"
#include "StateTreeEvaluatorBase.h"
#include "StateTreeTaskBase.h"
#include "CoreMinimal.h"


void UStateTreeEditorData::PostInitProperties()
{
	Super::PostInitProperties();
	
	if (!HasAnyFlags(RF_ClassDefaultObject) && !(GetOuter() && GetOuter()->HasAnyFlags(RF_ClassDefaultObject|RF_ArchetypeObject)))
	{
		// Add root node by default.
		AddRootState();
	}
}

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
		VisitHierarchy([&ValidStates, &OutStructDescs, &EvalDescs, &TaskDescs, TargetStructID, CurrentState]
			(const UStateTreeState& State, const FGuid& ID, const FName& Name, const EStateTreeNodeType NodeType, const UScriptStruct* NodeStruct, const UStruct* InstanceStruct) -> bool
			{
				if (ValidStates.Contains(&State))
				{
					if (ID == TargetStructID)
					{
						if (NodeType == EStateTreeNodeType::EnterCondition)
						{
							// Enter conditions can only see evaluators
							OutStructDescs.Append(EvalDescs);
						}
						else if (NodeType == EStateTreeNodeType::Evaluator)
						{
							// Evaluators can only see other evaluators
							OutStructDescs.Append(EvalDescs);
						}
						else if (NodeType == EStateTreeNodeType::Task)
						{
							// Tasks can see evals and tasks.
							OutStructDescs.Append(EvalDescs);
							OutStructDescs.Append(TaskDescs);
						}
						else if (NodeType == EStateTreeNodeType::TransitionCondition)
						{
							// Transitions can see evals and tasks.
							OutStructDescs.Append(EvalDescs);
							OutStructDescs.Append(TaskDescs);
						}
						
						return false; // Stop visit
					}

					// Not at target yet, collect all evaluators and tasks visible so far.
					if (NodeStruct->IsChildOf(FStateTreeEvaluatorBase::StaticStruct()))
					{
						FStateTreeBindableStructDesc& Desc = EvalDescs.AddDefaulted_GetRef();
						Desc.Struct = InstanceStruct;
						Desc.Name = Name;
						Desc.ID = ID;
					}
					else if (NodeStruct->IsChildOf(FStateTreeTaskBase::StaticStruct()))
					{
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

	VisitHierarchy([&bResult, &OutStructDesc, StructID](const UStateTreeState& State, const FGuid& ID, const FName& Name, const EStateTreeNodeType NodeType, const UScriptStruct* NodeStruct, const UStruct* InstanceStruct) -> bool
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

	VisitHierarchy([&Result, TargetStructID](const UStateTreeState& State, const FGuid& ID, const FName& Name, const EStateTreeNodeType NodeType, const UScriptStruct* NodeStruct, const UStruct* InstanceStruct) -> bool
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

	VisitHierarchy([&AllStructs](const UStateTreeState& State, const FGuid& ID, const FName& Name, const EStateTreeNodeType NodeType, const UScriptStruct* NodeStruct, const UStruct* InstanceStruct) -> bool
		{
			AllStructs.Add(ID, InstanceStruct);
			return true; // Continue
		});
}

void UStateTreeEditorData::VisitHierarchy(TFunctionRef<bool(const UStateTreeState& State, const FGuid& ID, const FName& Name, const EStateTreeNodeType NodeType, const UScriptStruct* NodeStruct, const UStruct* InstanceStruct)> InFunc) const
{
	TArray<const UStateTreeState*> Stack;
	bool bContinue = true;

	for (const UStateTreeState* SubTree : SubTrees)
	{
		if (!SubTree)
		{
			continue;
		}

		Stack.Add(SubTree);

		while (!Stack.IsEmpty() && bContinue)
		{
			const UStateTreeState* State = Stack[0];
			check(State);

			Stack.RemoveAt(0);

			// Evaluators
			for (const FStateTreeEditorNode& Node : State->Evaluators)
			{
				if (const FStateTreeEvaluatorBase* Evaluator = Node.Node.GetPtr<FStateTreeEvaluatorBase>())
				{
					if (!InFunc(*State, Node.ID, Evaluator->Name, EStateTreeNodeType::Evaluator, Node.Node.GetScriptStruct(), Evaluator->GetInstanceDataType()))
					{
						bContinue = false;
						break;
					}
				}
			}
			if (bContinue)
			{
				// Enter conditions
				for (const FStateTreeEditorNode& Node : State->EnterConditions)
				{
					if (const FStateTreeConditionBase* Cond = Node.Node.GetPtr<FStateTreeConditionBase>())
					{
						if (!InFunc(*State, Node.ID, Node.Node.GetScriptStruct()->GetFName(), EStateTreeNodeType::EnterCondition, Node.Node.GetScriptStruct(), Cond->GetInstanceDataType()))
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
				for (const FStateTreeEditorNode& Node : State->Tasks)
				{
					if (const FStateTreeTaskBase* Task = Node.Node.GetPtr<FStateTreeTaskBase>())
					{
						if (!InFunc(*State, Node.ID, Task->Name, EStateTreeNodeType::Task, Node.Node.GetScriptStruct(), Task->GetInstanceDataType()))
						{
							bContinue = false;
							break;
						}
					}
				}
			}
			if (bContinue)
			{
				if (const FStateTreeTaskBase* Task = State->SingleTask.Node.GetPtr<FStateTreeTaskBase>())
				{
					if (!InFunc(*State, State->SingleTask.ID, Task->Name, EStateTreeNodeType::Task, State->SingleTask.Node.GetScriptStruct(), Task->GetInstanceDataType()))
					{
						bContinue = false;
						break;
					}
				}

			}
			if (bContinue)
			{
				// Transitions
				for (const FStateTreeTransition& Transition : State->Transitions)
				{
					for (const FStateTreeEditorNode& Node : Transition.Conditions)
					{
						if (const FStateTreeConditionBase* Cond = Node.Node.GetPtr<FStateTreeConditionBase>())
						{
							if (!InFunc(*State, Node.ID, Node.Node.GetScriptStruct()->GetFName(), EStateTreeNodeType::TransitionCondition, Node.Node.GetScriptStruct(), Cond->GetInstanceDataType()))
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

