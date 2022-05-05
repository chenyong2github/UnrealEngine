// Copyright Epic Games, Inc. All Rights Reserved.

#include "StateTreeEditorData.h"
#include "StateTreeConditionBase.h"
#include "StateTreeDelegates.h"
#include "StateTreeEvaluatorBase.h"
#include "StateTreeTaskBase.h"

void UStateTreeEditorData::PostInitProperties()
{
	Super::PostInitProperties();

	RootParameters.ID = FGuid::NewGuid();
}

#if WITH_EDITOR
void UStateTreeEditorData::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	
	if (PropertyChangedEvent.MemberProperty && PropertyChangedEvent.Property)
	{
		const UStateTree* StateTree = GetTypedOuter<UStateTree>();
		checkf(StateTree, TEXT("UStateTreeEditorData should only be allocated within a UStateTree"));
		
		const FName MemberName = PropertyChangedEvent.MemberProperty->GetFName();
		if (MemberName == GET_MEMBER_NAME_CHECKED(UStateTreeEditorData, Schema))
		{			
			UE::StateTree::Delegates::OnSchemaChanged.Broadcast(*StateTree);
		}
		else if (MemberName == GET_MEMBER_NAME_CHECKED(UStateTreeEditorData, RootParameters))
		{
			UE::StateTree::Delegates::OnParametersChanged.Broadcast(*StateTree);
		}
	}
}
#endif // WITH_EDITOR

void UStateTreeEditorData::GetAccessibleStructs(const FGuid TargetStructID, TArray<FStateTreeBindableStructDesc>& OutStructDescs) const
{
	// Find the states that are updated before the current state.
	TArray<const UStateTreeState*> Path;
	const UStateTreeState* CurrentState = GetStateByStructID(TargetStructID);
	for (const UStateTreeState* State = CurrentState; State != nullptr; State = State->Parent)
	{
		Path.Insert(State, 0);

		// Stop at subtree root.
		if (State->Type == EStateTreeStateType::Subtree)
		{
			break;
		}
	}
	
	GetAccessibleStructs(Path, TargetStructID, OutStructDescs);
}

void UStateTreeEditorData::GetAccessibleStructs(const TConstArrayView<const UStateTreeState*> Path, const FGuid TargetStructID, TArray<FStateTreeBindableStructDesc>& OutStructDescs) const
{
	const UStateTree* StateTree = GetTypedOuter<UStateTree>();
	checkf(StateTree, TEXT("UStateTreeEditorData should only be allocated within a UStateTree"));
	
	// All parameters are accessible
	if (const UScriptStruct* PropertyBagStruct = RootParameters.Parameters.GetPropertyBagStruct())
	{
		OutStructDescs.Emplace(TEXT("Parameters"), PropertyBagStruct, EStateTreeBindableStructSource::TreeParameter, RootParameters.ID);
	}

	// All named external data items declared by the schema are accessible
	if (Schema != nullptr)
	{
		for (const FStateTreeExternalDataDesc& Desc : Schema->GetNamedExternalDataDescs())
		{
			OutStructDescs.Emplace(Desc.Name, Desc.Struct, EStateTreeBindableStructSource::TreeData, Desc.ID);
		}	
	}

	// State parameters
	const UStateTreeState* RootState = Path.Num() > 0 ? Path[0] : nullptr;
	if (RootState != nullptr
		&& RootState->Type == EStateTreeStateType::Subtree
		&& RootState->Parameters.Parameters.GetPropertyBagStruct() != nullptr)
	{
		if (const UScriptStruct* PropertyBagStruct = RootState->Parameters.Parameters.GetPropertyBagStruct())
		{
			OutStructDescs.Emplace(RootState->Name, PropertyBagStruct, EStateTreeBindableStructSource::StateParameter, RootState->Parameters.ID);
		}
	}

	TArray<FStateTreeBindableStructDesc> EvalDescs;
	TArray<FStateTreeBindableStructDesc> TaskDescs;

	for (const UStateTreeState* State : Path)
	{
		if (State == nullptr)
		{
			continue;
		}
		
		VisitStateNodes(*State, [&OutStructDescs, &EvalDescs, &TaskDescs, TargetStructID]
			(const UStateTreeState& State, const FGuid& ID, const FName& Name, const EStateTreeNodeType NodeType, const UScriptStruct* NodeStruct, const UStruct* InstanceStruct)
			{
				if (ID == TargetStructID)
				{
					if (NodeType == EStateTreeNodeType::EnterCondition
						|| NodeType == EStateTreeNodeType::Task
						|| NodeType == EStateTreeNodeType::TransitionCondition
						|| NodeType == EStateTreeNodeType::StateParameters)
					{
						// These can only see both evaluators and tasks
						OutStructDescs.Append(EvalDescs);
						OutStructDescs.Append(TaskDescs);
					}
					else if (NodeType == EStateTreeNodeType::Evaluator)
					{
						// Evaluators can only see other evaluators
						OutStructDescs.Append(EvalDescs);
					}
					
					return EStateTreeVisitor::Break;
				}

				// Not at target yet, collect all evaluators and tasks visible so far.
				if (NodeStruct->IsChildOf(FStateTreeEvaluatorBase::StaticStruct()))
				{
					FStateTreeBindableStructDesc& Desc = EvalDescs.AddDefaulted_GetRef();
					Desc.Struct = InstanceStruct;
					Desc.Name = Name;
					Desc.ID = ID;
					Desc.DataSource = EStateTreeBindableStructSource::Evaluator;
				}
				else if (NodeStruct->IsChildOf(FStateTreeTaskBase::StaticStruct()))
				{
					FStateTreeBindableStructDesc& Desc = TaskDescs.AddDefaulted_GetRef();
					Desc.Struct = InstanceStruct;
					Desc.Name = Name;
					Desc.ID = ID;
					Desc.DataSource = EStateTreeBindableStructSource::Task;
				}
				return EStateTreeVisitor::Continue;
			});
	}

	OutStructDescs.StableSort([](const FStateTreeBindableStructDesc& A, const FStateTreeBindableStructDesc& B)
	{
		return (uint8)A.DataSource < (uint8)B.DataSource;
	});
}

bool UStateTreeEditorData::GetStructByID(const FGuid StructID, FStateTreeBindableStructDesc& OutStructDesc) const
{
	bool bResult = false;

	VisitHierarchyNodes([&bResult, &OutStructDesc, StructID](const UStateTreeState& State, const FGuid& ID, const FName& Name, const EStateTreeNodeType NodeType, const UScriptStruct* NodeStruct, const UStruct* InstanceStruct)
		{
			if (ID == StructID)
			{
				OutStructDesc.Struct = InstanceStruct;
				OutStructDesc.Name = Name;
				OutStructDesc.ID = ID;
				bResult = true;
				return EStateTreeVisitor::Break;
			}
			return EStateTreeVisitor::Continue;
		});

	return bResult;
}

const UStateTreeState* UStateTreeEditorData::GetStateByStructID(const FGuid TargetStructID) const
{
	const UStateTreeState* Result = nullptr;

	VisitHierarchyNodes([&Result, TargetStructID](const UStateTreeState& State, const FGuid& ID, const FName& Name, const EStateTreeNodeType NodeType, const UScriptStruct* NodeStruct, const UStruct* InstanceStruct)
		{
			if (ID == TargetStructID)
			{
				Result = &State;
				return EStateTreeVisitor::Break;
			}
			return EStateTreeVisitor::Continue;
		});

	return Result;
}

const UStateTreeState* UStateTreeEditorData::GetStateByID(const FGuid StateID) const
{
	const UStateTreeState* Result = nullptr;
	
	VisitHierarchy([&Result, &StateID](const UStateTreeState& State, UStateTreeState* /*ParentState*/)
	{
		if (State.ID == StateID)
		{
			Result = &State;
			return EStateTreeVisitor::Break;
		}

		return EStateTreeVisitor::Continue;
	});

	return Result;
}

void UStateTreeEditorData::GetAllStructIDs(TMap<FGuid, const UStruct*>& AllStructs) const
{
	AllStructs.Reset();

	const UStateTree* StateTree = GetTypedOuter<UStateTree>();
	checkf(StateTree, TEXT("UStateTreeEditorData should only be allocated within a UStateTree"));

	// All parameters
	AllStructs.Emplace(RootParameters.ID, RootParameters.Parameters.GetPropertyBagStruct());

	// All named external data items declared by the schema
	if (Schema != nullptr)
	{
		for (const FStateTreeExternalDataDesc& Desc : Schema->GetNamedExternalDataDescs())
		{
			AllStructs.Emplace(Desc.ID, Desc.Struct);
		}	
	}

	VisitHierarchyNodes([&AllStructs](const UStateTreeState& State, const FGuid& ID, const FName& Name, const EStateTreeNodeType NodeType, const UScriptStruct* NodeStruct, const UStruct* InstanceStruct)
		{
			AllStructs.Add(ID, InstanceStruct);
			return EStateTreeVisitor::Continue;
		});
}

EStateTreeVisitor UStateTreeEditorData::VisitStateNodes(const UStateTreeState& State, TFunctionRef<EStateTreeVisitor(const UStateTreeState& State, const FGuid& ID, const FName& Name, const EStateTreeNodeType NodeType, const UScriptStruct* NodeStruct, const UStruct* InstanceStruct)> InFunc) const
{
	bool bContinue = true;

	// Evaluators
	for (const FStateTreeEditorNode& Node : State.Evaluators)
	{
		if (const FStateTreeEvaluatorBase* Evaluator = Node.Node.GetPtr<FStateTreeEvaluatorBase>())
		{
			if (InFunc(State, Node.ID, Evaluator->Name, EStateTreeNodeType::Evaluator, Node.Node.GetScriptStruct(), Evaluator->GetInstanceDataType()) == EStateTreeVisitor::Break)
			{
				bContinue = false;
				break;
			}
		}
	}
	if (bContinue)
	{
		// Enter conditions
		for (const FStateTreeEditorNode& Node : State.EnterConditions)
		{
			if (const FStateTreeConditionBase* Cond = Node.Node.GetPtr<FStateTreeConditionBase>())
			{
				if (InFunc(State, Node.ID, Node.Node.GetScriptStruct()->GetFName(), EStateTreeNodeType::EnterCondition, Node.Node.GetScriptStruct(), Cond->GetInstanceDataType()) == EStateTreeVisitor::Break)
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
		for (const FStateTreeEditorNode& Node : State.Tasks)
		{
			if (const FStateTreeTaskBase* Task = Node.Node.GetPtr<FStateTreeTaskBase>())
			{
				if (InFunc(State, Node.ID, Task->Name, EStateTreeNodeType::Task, Node.Node.GetScriptStruct(), Task->GetInstanceDataType()) == EStateTreeVisitor::Break)
				{
					bContinue = false;
					break;
				}
			}
		}
	}
	if (bContinue)
	{
		if (const FStateTreeTaskBase* Task = State.SingleTask.Node.GetPtr<FStateTreeTaskBase>())
		{
			if (InFunc(State, State.SingleTask.ID, Task->Name, EStateTreeNodeType::Task, State.SingleTask.Node.GetScriptStruct(), Task->GetInstanceDataType()) == EStateTreeVisitor::Break)
			{
				bContinue = false;
			}
		}

	}
	if (bContinue)
	{
		// Transitions
		for (const FStateTreeTransition& Transition : State.Transitions)
		{
			for (const FStateTreeEditorNode& Node : Transition.Conditions)
			{
				if (const FStateTreeConditionBase* Cond = Node.Node.GetPtr<FStateTreeConditionBase>())
				{
					if (InFunc(State, Node.ID, Node.Node.GetScriptStruct()->GetFName(), EStateTreeNodeType::TransitionCondition, Node.Node.GetScriptStruct(), Cond->GetInstanceDataType()) == EStateTreeVisitor::Break)
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
		// Bindable state parameters
		if (State.Type == EStateTreeStateType::Linked && State.Parameters.Parameters.IsValid())
		{
			if (InFunc(State, State.Parameters.ID, State.Name, EStateTreeNodeType::StateParameters, nullptr, State.Parameters.Parameters.GetPropertyBagStruct()) == EStateTreeVisitor::Break)
			{
				bContinue = false;
			}
		}
	}

	return bContinue ? EStateTreeVisitor::Continue : EStateTreeVisitor::Break;
}

void UStateTreeEditorData::VisitHierarchy(TFunctionRef<EStateTreeVisitor(UStateTreeState& State, UStateTreeState* ParentState)> InFunc) const
{
	using FStatePair = TTuple<UStateTreeState*, UStateTreeState*>; 
	TArray<FStatePair> Stack;
	bool bContinue = true;

	for (UStateTreeState* SubTree : SubTrees)
	{
		if (!SubTree)
		{
			continue;
		}

		Stack.Add( FStatePair(nullptr, SubTree));

		while (!Stack.IsEmpty() && bContinue)
		{
			FStatePair Current = Stack[0];
			UStateTreeState* ParentState = Current.Get<0>();
			UStateTreeState* State = Current.Get<1>();
			check(State);

			Stack.RemoveAt(0);

			bContinue = InFunc(*State, ParentState) == EStateTreeVisitor::Continue;
			
			if (bContinue)
			{
				// Children
				for (UStateTreeState* ChildState : State->Children)
				{
					Stack.Add(FStatePair(State, ChildState));
				}
			}
		}
		
		if (!bContinue)
		{
			break;
		}
	}
}

void UStateTreeEditorData::VisitHierarchyNodes(TFunctionRef<EStateTreeVisitor(const UStateTreeState& State, const FGuid& ID, const FName& Name, const EStateTreeNodeType NodeType, const UScriptStruct* NodeStruct, const UStruct* InstanceStruct)> InFunc) const
{
	VisitHierarchy([this, &InFunc](const UStateTreeState& State, UStateTreeState* /*ParentState*/)
	{
		return VisitStateNodes(State, InFunc);
	});
}

