// Copyright Epic Games, Inc. All Rights Reserved.

#include "StateTreeEditorData.h"
#include "StateTree.h"
#include "StateTreeConditionBase.h"
#include "StateTreeDelegates.h"
#include "StateTreeEvaluatorBase.h"
#include "StateTreeTaskBase.h"
#include "Algo/LevenshteinDistance.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(StateTreeEditorData)

void UStateTreeEditorData::PostInitProperties()
{
	Super::PostInitProperties();

	RootParameters.ID = FGuid::NewGuid();
}

#if WITH_EDITOR
void UStateTreeEditorData::PostEditChangeChainProperty(FPropertyChangedChainEvent& PropertyChangedEvent)
{
	Super::PostEditChangeChainProperty(PropertyChangedEvent);

	FProperty* Property = PropertyChangedEvent.Property;
	FProperty* MemberProperty = nullptr;
	if (PropertyChangedEvent.PropertyChain.GetActiveMemberNode())
	{
		MemberProperty = PropertyChangedEvent.PropertyChain.GetActiveMemberNode()->GetValue();
	}

	if (MemberProperty && Property)
	{
		const UStateTree* StateTree = GetTypedOuter<UStateTree>();
		checkf(StateTree, TEXT("UStateTreeEditorData should only be allocated within a UStateTree"));
		
		const FName MemberName = MemberProperty->GetFName();
		if (MemberName == GET_MEMBER_NAME_CHECKED(UStateTreeEditorData, Schema))
		{			
			UE::StateTree::Delegates::OnSchemaChanged.Broadcast(*StateTree);
		}
		else if (MemberName == GET_MEMBER_NAME_CHECKED(UStateTreeEditorData, RootParameters))
		{
			UE::StateTree::Delegates::OnParametersChanged.Broadcast(*StateTree);
		}

		// Ensure unique ID on duplicated items.
		if (PropertyChangedEvent.ChangeType == EPropertyChangeType::Duplicate)
		{
			if (MemberName == GET_MEMBER_NAME_CHECKED(UStateTreeEditorData, Evaluators))
			{
				const int32 ArrayIndex = PropertyChangedEvent.GetArrayIndex(MemberProperty->GetFName().ToString());
				if (Evaluators.IsValidIndex(ArrayIndex))
				{
					if (FStateTreeEvaluatorBase* Eval = Evaluators[ArrayIndex].Node.GetMutablePtr<FStateTreeEvaluatorBase>())
					{
						Eval->Name = FName(Eval->Name.ToString() + TEXT(" Duplicate"));
					}
					
					const FGuid OldStructID = Evaluators[ArrayIndex].ID;
					Evaluators[ArrayIndex].ID = FGuid::NewGuid();
					EditorBindings.CopyBindings(OldStructID, Evaluators[ArrayIndex].ID);
				}
			}
			else if (MemberName == GET_MEMBER_NAME_CHECKED(UStateTreeEditorData, GlobalTasks))
			{
				const int32 ArrayIndex = PropertyChangedEvent.GetArrayIndex(MemberProperty->GetFName().ToString());
				if (GlobalTasks.IsValidIndex(ArrayIndex))
				{
					if (FStateTreeTaskBase* Task = GlobalTasks[ArrayIndex].Node.GetMutablePtr<FStateTreeTaskBase>())
					{
						Task->Name = FName(Task->Name.ToString() + TEXT(" Duplicate"));
					}
					
					const FGuid OldStructID = GlobalTasks[ArrayIndex].ID;
					GlobalTasks[ArrayIndex].ID = FGuid::NewGuid();
					EditorBindings.CopyBindings(OldStructID, GlobalTasks[ArrayIndex].ID);
				}
			}
		}
		else if (PropertyChangedEvent.ChangeType == EPropertyChangeType::ArrayRemove)
		{
			if (MemberProperty->GetFName() == GET_MEMBER_NAME_CHECKED(UStateTreeEditorData, Evaluators)
				|| MemberProperty->GetFName() == GET_MEMBER_NAME_CHECKED(UStateTreeEditorData, GlobalTasks))
			{
				TMap<FGuid, const FStateTreeDataView> AllStructValues;
				GetAllStructValues(AllStructValues);
				EditorBindings.RemoveUnusedBindings(AllStructValues);
			}
		}
	}
}
#endif // WITH_EDITOR

void UStateTreeEditorData::GetAccessibleStructs(const FGuid TargetStructID, TArray<FStateTreeBindableStructDesc>& OutStructDescs) const
{
	// Find the states that are updated before the current state.
	TArray<const UStateTreeState*> Path;
	const UStateTreeState* State = GetStateByStructID(TargetStructID);
	while (State != nullptr)
	{
		Path.Insert(State, 0);

		// Stop at subtree root.
		if (State->Type == EStateTreeStateType::Subtree)
		{
			break;
		}

		State = State->Parent;
	}
	
	GetAccessibleStructs(Path, TargetStructID, OutStructDescs);
}

void UStateTreeEditorData::GetAccessibleStructs(const TConstArrayView<const UStateTreeState*> Path, const FGuid TargetStructID, TArray<FStateTreeBindableStructDesc>& OutStructDescs) const
{
	const UStateTree* StateTree = GetTypedOuter<UStateTree>();
	checkf(StateTree, TEXT("UStateTreeEditorData should only be allocated within a UStateTree"));


	EStateTreeVisitor BaseProgress = VisitGlobalNodes([&OutStructDescs, TargetStructID]
		(const UStateTreeState* State, const FStateTreeBindableStructDesc& Desc, const FStateTreeDataView Value)
	{
		if (Desc.ID == TargetStructID)
		{
			return EStateTreeVisitor::Break;
		}
		
		OutStructDescs.Add(Desc);
		
		return EStateTreeVisitor::Continue;
	});


	if (BaseProgress == EStateTreeVisitor::Continue)
	{
		TArray<FStateTreeBindableStructDesc> TaskDescs;

		for (const UStateTreeState* State : Path)
		{
			if (State == nullptr)
			{
				continue;
			}
			
			const EStateTreeVisitor StateProgress = VisitStateNodes(*State, [&OutStructDescs, &TaskDescs, TargetStructID]
				(const UStateTreeState* State, const FStateTreeBindableStructDesc& Desc, const FStateTreeDataView Value)
				{
					// Stop iterating as soon as we find the target node.
					if (Desc.ID == TargetStructID)
					{
						OutStructDescs.Append(TaskDescs);
						return EStateTreeVisitor::Break;
					}

					// Not at target yet, collect all bindable source accessible so far.
					if (Desc.DataSource == EStateTreeBindableStructSource::Task
						|| Desc.DataSource == EStateTreeBindableStructSource::State)
					{
						TaskDescs.Add(Desc);
					}
							
					return EStateTreeVisitor::Continue;
				});
			
			if (StateProgress == EStateTreeVisitor::Break)
			{
				break;
			}
		}
	}
	
	OutStructDescs.StableSort([](const FStateTreeBindableStructDesc& A, const FStateTreeBindableStructDesc& B)
	{
		return (uint8)A.DataSource < (uint8)B.DataSource;
	});
}

FStateTreeBindableStructDesc UStateTreeEditorData::FindContextData(const UStruct* ObjectType, const FString ObjectNameHint) const
{
	if (Schema == nullptr)
	{
		return FStateTreeBindableStructDesc();
	}

	// Find candidates based on type.
	TArray<FStateTreeBindableStructDesc> Candidates;
	for (const FStateTreeExternalDataDesc& Desc : Schema->GetContextDataDescs())
	{
		if (Desc.Struct->IsChildOf(ObjectType))
		{
			Candidates.Emplace(Desc.Name, Desc.Struct, EStateTreeBindableStructSource::Context, Desc.ID);
		}
	}

	// Handle trivial cases.
	if (Candidates.IsEmpty())
	{
		return FStateTreeBindableStructDesc();
	}

	if (Candidates.Num() == 1)
	{
		return Candidates[0];
	}
	
	check(!Candidates.IsEmpty());
	
	// Multiple candidates, pick one that is closest match based on name.
	auto CalculateScore = [](const FString& Name, const FString& CandidateName)
	{
		if (CandidateName.IsEmpty())
		{
			return 1.0f;
		}
		const float WorstCase = static_cast<float>(Name.Len() + CandidateName.Len());
		return 1.0f - (Algo::LevenshteinDistance(Name, CandidateName) / WorstCase);
	};
	
	const FString ObjectNameLowerCase = ObjectNameHint.ToLower();
	
	int32 HighestScoreIndex = 0;
	float HighestScore = CalculateScore(ObjectNameLowerCase, Candidates[0].Name.ToString().ToLower());
	
	for (int32 Index = 1; Index < Candidates.Num(); Index++)
	{
		const float Score = CalculateScore(ObjectNameLowerCase, Candidates[Index].Name.ToString().ToLower());
		if (Score > HighestScore)
		{
			HighestScore = Score;
			HighestScoreIndex = Index;
		}
	}
	
	return Candidates[HighestScoreIndex];
}

bool UStateTreeEditorData::GetStructByID(const FGuid StructID, FStateTreeBindableStructDesc& OutStructDesc) const
{
	bool bResult = false;

	VisitAllNodes([&OutStructDesc, StructID](const UStateTreeState* State, const FStateTreeBindableStructDesc& Desc, const FStateTreeDataView Value)
	{
		if (Desc.ID == StructID)
		{
			OutStructDesc = Desc;
			return EStateTreeVisitor::Break;
		}
		return EStateTreeVisitor::Continue;
	});
	
	return OutStructDesc.IsValid();
}

const UStateTreeState* UStateTreeEditorData::GetStateByStructID(const FGuid TargetStructID) const
{
	const UStateTreeState* Result = nullptr;

	VisitHierarchyNodes([&Result, TargetStructID](const UStateTreeState* State, const FStateTreeBindableStructDesc& Desc, const FStateTreeDataView Value)
		{
			if (Desc.ID == TargetStructID)
			{
				Result = State;
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

	VisitAllNodes([&AllStructs](const UStateTreeState* State, const FStateTreeBindableStructDesc& Desc, const FStateTreeDataView Value)
		{
			AllStructs.Emplace(Desc.ID, Desc.Struct);
			return EStateTreeVisitor::Continue;
		});
}

void UStateTreeEditorData::GetAllStructValues(TMap<FGuid, const FStateTreeDataView>& AllValues) const
{
	AllValues.Reset();

	const UStateTree* StateTree = GetTypedOuter<UStateTree>();
	checkf(StateTree, TEXT("UStateTreeEditorData should only be allocated within a UStateTree"));

	VisitAllNodes([&AllValues](const UStateTreeState* State, const FStateTreeBindableStructDesc& Desc, const FStateTreeDataView Value)
		{
			AllValues.Emplace(Desc.ID, Value);
			return EStateTreeVisitor::Continue;
		});
}

EStateTreeVisitor UStateTreeEditorData::VisitStateNodes(const UStateTreeState& State, TFunctionRef<EStateTreeVisitor(const UStateTreeState* State, const FGuid& ID, const FName& Name, const EStateTreeNodeType NodeType, const UScriptStruct* NodeStruct, const UStruct* InstanceStruct)> InFunc) const
{
	bool bContinue = true;

	if (bContinue)
	{
		// Enter conditions
		for (const FStateTreeEditorNode& Node : State.EnterConditions)
		{
			if (const FStateTreeConditionBase* Cond = Node.Node.GetPtr<FStateTreeConditionBase>())
			{
				if (InFunc(&State, Node.ID, Node.Node.GetScriptStruct()->GetFName(), EStateTreeNodeType::EnterCondition, Node.Node.GetScriptStruct(), Cond->GetInstanceDataType()) == EStateTreeVisitor::Break)
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
				if (InFunc(&State, Node.ID, Task->Name, EStateTreeNodeType::Task, Node.Node.GetScriptStruct(), Task->GetInstanceDataType()) == EStateTreeVisitor::Break)
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
			if (InFunc(&State, State.SingleTask.ID, Task->Name, EStateTreeNodeType::Task, State.SingleTask.Node.GetScriptStruct(), Task->GetInstanceDataType()) == EStateTreeVisitor::Break)
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
					if (InFunc(&State, Node.ID, Node.Node.GetScriptStruct()->GetFName(), EStateTreeNodeType::TransitionCondition, Node.Node.GetScriptStruct(), Cond->GetInstanceDataType()) == EStateTreeVisitor::Break)
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
			if (InFunc(&State, State.Parameters.ID, State.Name, EStateTreeNodeType::StateParameters, nullptr, State.Parameters.Parameters.GetPropertyBagStruct()) == EStateTreeVisitor::Break)
			{
				bContinue = false;
			}
		}
	}

	return bContinue ? EStateTreeVisitor::Continue : EStateTreeVisitor::Break;
}


EStateTreeVisitor UStateTreeEditorData::VisitStateNodes(const UStateTreeState& State, TFunctionRef<EStateTreeVisitor(const UStateTreeState* State, const FStateTreeBindableStructDesc& Desc, const FStateTreeDataView Value)> InFunc) const
{
	bool bContinue = true;

	if (bContinue)
	{
		// Bindable state parameters for subtree or linked tree.
		if ((State.Type == EStateTreeStateType::Subtree
				|| State.Type == EStateTreeStateType::Linked)
			&& State.Parameters.Parameters.IsValid())
		{
			FStateTreeBindableStructDesc Desc;
			Desc.Struct = State.Parameters.Parameters.GetPropertyBagStruct();
			Desc.Name = State.Name;
			Desc.ID = State.Parameters.ID;
			Desc.DataSource = EStateTreeBindableStructSource::State;

			if (InFunc(&State, Desc, FStateTreeDataView(const_cast<FInstancedPropertyBag&>(State.Parameters.Parameters).GetMutableValue())) == EStateTreeVisitor::Break)
			{
				bContinue = false;
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
				FStateTreeBindableStructDesc Desc;
				Desc.Struct = Cond->GetInstanceDataType();
				Desc.Name = Cond->Name;
				Desc.ID = Node.ID;
				Desc.DataSource = EStateTreeBindableStructSource::Condition;

				if (InFunc(&State, Desc, Node.GetInstance()) == EStateTreeVisitor::Break)
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
				FStateTreeBindableStructDesc Desc;
				Desc.Struct = Task->GetInstanceDataType();
				Desc.Name = Task->Name;
				Desc.ID = Node.ID;
				Desc.DataSource = EStateTreeBindableStructSource::Task;

				if (InFunc(&State, Desc, Node.GetInstance()) == EStateTreeVisitor::Break)
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
			FStateTreeBindableStructDesc Desc;
			Desc.Struct = Task->GetInstanceDataType();
			Desc.Name = Task->Name;
			Desc.ID = State.SingleTask.ID;
			Desc.DataSource = EStateTreeBindableStructSource::Task;

			if (InFunc(&State, Desc, State.SingleTask.GetInstance()) == EStateTreeVisitor::Break)
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
					FStateTreeBindableStructDesc Desc;
					Desc.Struct = Cond->GetInstanceDataType();
					Desc.Name = Cond->Name;
					Desc.ID = Node.ID;
					Desc.DataSource = EStateTreeBindableStructSource::Condition;

					if (InFunc(&State, Desc, Node.GetInstance()) == EStateTreeVisitor::Break)
					{
						bContinue = false;
						break;
					}
				}
			}
		}
	}

	return bContinue ? EStateTreeVisitor::Continue : EStateTreeVisitor::Break;
}


EStateTreeVisitor UStateTreeEditorData::VisitHierarchy(TFunctionRef<EStateTreeVisitor(UStateTreeState& State, UStateTreeState* ParentState)> InFunc) const
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

	return EStateTreeVisitor::Continue;
}

void UStateTreeEditorData::VisitHierarchyNodes(TFunctionRef<EStateTreeVisitor(const UStateTreeState* State, const FGuid& ID, const FName& Name, const EStateTreeNodeType NodeType, const UScriptStruct* NodeStruct, const UStruct* InstanceStruct)> InFunc) const
{
	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	VisitHierarchy([this, &InFunc](const UStateTreeState& State, UStateTreeState* /*ParentState*/)
	{
		return VisitStateNodes(State, InFunc);
	});
	PRAGMA_ENABLE_DEPRECATION_WARNINGS
}

EStateTreeVisitor UStateTreeEditorData::VisitGlobalNodes(TFunctionRef<EStateTreeVisitor(const UStateTreeState* State, const FStateTreeBindableStructDesc& Desc, const FStateTreeDataView Value)> InFunc) const
{
	// Root parameters
	{
		FStateTreeBindableStructDesc Desc;
		Desc.Struct = RootParameters.Parameters.GetPropertyBagStruct();
		Desc.Name = FName(TEXT("Parameters"));
		Desc.ID = RootParameters.ID;
		Desc.DataSource = EStateTreeBindableStructSource::Parameter;
		
		if (InFunc(nullptr, Desc, FStateTreeDataView(const_cast<FInstancedPropertyBag&>(RootParameters.Parameters).GetMutableValue())) == EStateTreeVisitor::Break)
		{
			return EStateTreeVisitor::Break;
		}
	}

	// All named external data items declared by the schema
	if (Schema != nullptr)
	{
		for (const FStateTreeExternalDataDesc& ContextDesc : Schema->GetContextDataDescs())
		{
			FStateTreeBindableStructDesc Desc;
			Desc.Struct = ContextDesc.Struct;
			Desc.Name = ContextDesc.Name;
			Desc.ID = ContextDesc.ID;
			Desc.DataSource = EStateTreeBindableStructSource::Context;
			
			if (InFunc(nullptr, Desc, FStateTreeDataView()) == EStateTreeVisitor::Break)
			{
				return EStateTreeVisitor::Break;
			}
		}
	}

	// Evaluators
	for (const FStateTreeEditorNode& Node : Evaluators)
	{
		if (const FStateTreeEvaluatorBase* Evaluator = Node.Node.GetPtr<FStateTreeEvaluatorBase>())
		{
			FStateTreeBindableStructDesc Desc;
			Desc.Struct = Evaluator->GetInstanceDataType();
			Desc.Name = Evaluator->Name;
			Desc.ID = Node.ID;
			Desc.DataSource = EStateTreeBindableStructSource::Evaluator;

			if (InFunc(nullptr, Desc, Node.GetInstance()) == EStateTreeVisitor::Break)
			{
				return EStateTreeVisitor::Break;
			}
		}
	}

	// Global tasks
	for (const FStateTreeEditorNode& Node : GlobalTasks)
	{
		if (const FStateTreeTaskBase* Task = Node.Node.GetPtr<FStateTreeTaskBase>())
		{
			FStateTreeBindableStructDesc Desc;
			Desc.Struct = Task->GetInstanceDataType();
			Desc.Name = Task->Name;
			Desc.ID = Node.ID;
			Desc.DataSource = EStateTreeBindableStructSource::GlobalTask;

			if (InFunc(nullptr, Desc, Node.GetInstance()) == EStateTreeVisitor::Break)
			{
				return EStateTreeVisitor::Break;
			}
		}
	}

	return  EStateTreeVisitor::Continue;
}

EStateTreeVisitor UStateTreeEditorData::VisitHierarchyNodes(TFunctionRef<EStateTreeVisitor(const UStateTreeState* State, const FStateTreeBindableStructDesc& Desc, const FStateTreeDataView Value)> InFunc) const
{
	return VisitHierarchy([this, &InFunc](const UStateTreeState& State, UStateTreeState* /*ParentState*/)
	{
		return VisitStateNodes(State, InFunc);
	});
}

EStateTreeVisitor UStateTreeEditorData::VisitAllNodes(TFunctionRef<EStateTreeVisitor(const UStateTreeState* State, const FStateTreeBindableStructDesc& Desc, const FStateTreeDataView Value)> InFunc) const
{
	if (VisitGlobalNodes(InFunc) == EStateTreeVisitor::Break)
	{
		return EStateTreeVisitor::Break;
	}

	return VisitHierarchyNodes(InFunc);
}

PRAGMA_DISABLE_DEPRECATION_WARNINGS
void UStateTreeEditorData::AddPropertyBinding(const FStateTreeEditorPropertyPath& SourcePath, const FStateTreeEditorPropertyPath& TargetPath)
{
	EditorBindings.AddPropertyBinding(UE::StateTree::Private::ConvertEditorPath(SourcePath), UE::StateTree::Private::ConvertEditorPath(TargetPath));
}
PRAGMA_ENABLE_DEPRECATION_WARNINGS