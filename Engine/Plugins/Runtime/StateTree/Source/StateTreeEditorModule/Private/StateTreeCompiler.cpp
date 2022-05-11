// Copyright Epic Games, Inc. All Rights Reserved.

#include "StateTreeCompiler.h"
#include "StateTree.h"
#include "StateTreeEditorData.h"
#include "StateTreeTypes.h"
#include "Conditions/StateTreeCondition_Common.h"
#include "StateTreeEvaluatorBase.h"
#include "StateTreeTaskBase.h"
#include "StateTreeConditionBase.h"
#include "StateTreeState.h"
#include "StateTreeExecutionContext.h"
#include "StateTreePropertyBindingCompiler.h"

bool FStateTreeCompiler::Compile(UStateTree& InStateTree)
{
	StateTree = &InStateTree;
	TreeData = Cast<UStateTreeEditorData>(StateTree->EditorData);
	if (!TreeData)
	{
		return false;
	}

	// Cleanup existing state
	StateTree->ResetCompiled();

	if (!BindingsCompiler.Init(StateTree->PropertyBindings, Log))
	{
		StateTree->ResetCompiled();
		return false;
	}

	// Copy schema the EditorData
	StateTree->Schema = DuplicateObject(TreeData->Schema, StateTree);

	// Copy parameters from EditorData	
	StateTree->Parameters = TreeData->RootParameters.Parameters;
	
	// Mark parameters as binding source
	StateTree->DefaultParametersDataViewIndex = BindingsCompiler.AddSourceStruct(
		{ TEXT("Parameters"), StateTree->Parameters.GetPropertyBagStruct(), EStateTreeBindableStructSource::TreeParameter, TreeData->RootParameters.ID });
	
	// Mark all named external values as binding source
	if (StateTree->Schema)
	{
		StateTree->NamedExternalDataDescs = StateTree->Schema->GetNamedExternalDataDescs();
		for (FStateTreeExternalDataDesc& Desc : StateTree->NamedExternalDataDescs)
		{
			Desc.Handle.DataViewIndex = BindingsCompiler.AddSourceStruct({Desc.Name, Desc.Struct, EStateTreeBindableStructSource::TreeData, Desc.ID});
		} 
	}
	
	if (!CreateStates())
	{
		StateTree->ResetCompiled();
		return false;
	}

	if (!CreateStateEvaluators())
	{
		StateTree->ResetCompiled();
		return false;
	}

	if (!CreateStateTasksAndParameters())
	{
		StateTree->ResetCompiled();
		return false;
	}

	if (!CreateStateTransitions())
	{
		StateTree->ResetCompiled();
		return false;
	}

	BindingsCompiler.Finalize();

	if (!StateTree->Link())
	{
		StateTree->ResetCompiled();
		return false;
	}
	
	return true;
}

FStateTreeHandle FStateTreeCompiler::GetStateHandle(const FGuid& StateID) const
{
	const int32* Idx = IDToState.Find(StateID);
	if (Idx == nullptr)
	{
		return FStateTreeHandle::Invalid;
	}

	return FStateTreeHandle(uint16(*Idx));
}

UStateTreeState* FStateTreeCompiler::GetState(const FGuid& StateID)
{
	const int32* Idx = IDToState.Find(StateID);
	if (Idx == nullptr)
	{
		return nullptr;
	}

	return SourceStates[*Idx];
}

bool FStateTreeCompiler::CreateStates()
{
	// Create item for the runtime execution state
	StateTree->Instances.Add(FInstancedStruct::Make<FStateTreeExecutionState>());

	// Create main tree (omit subtrees)
	for (UStateTreeState* SubTree : TreeData->SubTrees)
	{
		if (SubTree != nullptr)
		{
			if (!CreateStateRecursive(*SubTree, FStateTreeHandle::Invalid))
			{
				return false;
			}
		}
	}

	// Create Subtrees
	for (UStateTreeState* SubTree : TreeData->SubTrees)
	{
		TArray<UStateTreeState*> Stack;
		Stack.Push(SubTree);
		while (!Stack.IsEmpty())
		{
			if (UStateTreeState* State = Stack.Pop())
			{
				if (State->Type == EStateTreeStateType::Subtree)
				{
					if (!CreateStateRecursive(*State, FStateTreeHandle::Invalid))
					{
						return false;
					}
				}
				Stack.Append(State->Children);
			}
		}
	}

	return true;
}

bool FStateTreeCompiler::CreateStateRecursive(UStateTreeState& State, const FStateTreeHandle Parent)
{
	FStateTreeCompilerLogStateScope LogStateScope(&State, Log);

	const int32 StateIdx = StateTree->States.AddDefaulted();
	FCompactStateTreeState& CompactState = StateTree->States[StateIdx];
	CompactState.Name = State.Name;
	CompactState.Parent = Parent;

	CompactState.Type = State.Type; 
	
	SourceStates.Add(&State);
	IDToState.Add(State.ID, StateIdx);

	check(StateTree->Instances.Num() <= int32(MAX_uint16));

	// Child states
	check(StateTree->States.Num() <= int32(MAX_uint16));
	CompactState.ChildrenBegin = uint16(StateTree->States.Num());
	for (UStateTreeState* Child : State.Children)
	{
		if (Child != nullptr && Child->Type != EStateTreeStateType::Subtree)
		{
			if (!CreateStateRecursive(*Child, FStateTreeHandle((uint16)StateIdx)))
			{
				return false;
			}
		}
	}
	check(StateTree->States.Num() <= int32(MAX_uint16));
	StateTree->States[StateIdx].ChildrenEnd = uint16(StateTree->States.Num()); // Cannot use CompactState here, it may be invalid due to array resize.

	return true;
}

bool FStateTreeCompiler::CreateConditions(UStateTreeState& State, TConstArrayView<FStateTreeEditorNode> Conditions)
{
	for (int32 Index = 0; Index < Conditions.Num(); Index++)
	{
		const bool bIsFirst = Index == 0;
		const FStateTreeEditorNode& CondNode = Conditions[Index];
		// First operand should be copy as we dont have a previous item to operate on.
		const EStateTreeConditionOperand Operand = bIsFirst ? EStateTreeConditionOperand::Copy : CondNode.ConditionOperand;
		// First indent must be 0 to make the parentheses calculation match.
		const int32 CurrIndent = bIsFirst ? 0 : CondNode.ConditionIndent;
		// Next indent, or terminate at zero.
		const int32 NextIndent = Conditions.IsValidIndex(Index + 1) ? Conditions[Index].ConditionIndent : 0;
		const int32 DeltaIndent = NextIndent - CurrIndent;
		
		check(DeltaIndent >= MIN_int8 && DeltaIndent <= MAX_int8);

		if (!CreateCondition(State, CondNode, Operand, (int8)DeltaIndent))
		{
			return false;
		}
	}

	return true;
}

bool FStateTreeCompiler::CreateStateTasksAndParameters()
{
	for (int32 i = 0; i < StateTree->States.Num(); i++)
	{
		FCompactStateTreeState& CompactState = StateTree->States[i];
		UStateTreeState* SourceState = SourceStates[i];
		check(SourceState != nullptr);

		FStateTreeCompilerLogStateScope LogStateScope(SourceState, Log);

		// Create parameters
		if (SourceState->Type == EStateTreeStateType::Linked || SourceState->Type == EStateTreeStateType::Subtree)
		{
			// Both linked and subtree has instance data describing their parameters.
			// This allows to resolve the binding paths and lets us have bindable parameters when transitioned into a parameterized subtree directly.
			FInstancedStruct& Instance = StateTree->Instances.AddDefaulted_GetRef();
			const int32 InstanceIndex = StateTree->Instances.Num() - 1;

			check(InstanceIndex <= int32(MAX_uint16));
			CompactState.ParameterInstanceIndex = InstanceIndex;
		
			Instance.InitializeAs<FCompactStateTreeParameters>();
			FCompactStateTreeParameters& CompactParams = Instance.GetMutable<FCompactStateTreeParameters>();

			CompactParams.Parameters = SourceState->Parameters.Parameters;

			if (SourceState->Type == EStateTreeStateType::Subtree)
			{
				// Register a binding source
				CompactState.ParameterDataViewIndex = BindingsCompiler.AddSourceStruct({ SourceState->Name, SourceState->Parameters.Parameters.GetPropertyBagStruct(), EStateTreeBindableStructSource::StateParameter, SourceState->Parameters.ID });
			}
			else if (SourceState->Type == EStateTreeStateType::Linked)
			{
				// Binding target
				FStateTreeBindableStructDesc StructDesc;
				StructDesc.ID = SourceState->Parameters.ID;
				StructDesc.Name = SourceState->Name;
				StructDesc.DataSource = EStateTreeBindableStructSource::StateParameter;
				StructDesc.Struct = SourceState->Parameters.Parameters.GetPropertyBagStruct();

				// Check that the bindings for this struct are still all valid.
				TArray<FStateTreeEditorPropertyBinding> Bindings;
				if (!GetAndValidateBindings(*SourceState, StructDesc, Bindings))
				{
					return false;
				}

				int32 BatchIndex = INDEX_NONE;
				if (!BindingsCompiler.CompileBatch(StructDesc, Bindings, BatchIndex))
				{
					return false;
				}
				check(BatchIndex < int32(MAX_uint16));
				CompactParams.BindingsBatch = BatchIndex == INDEX_NONE ? FStateTreeHandle::Invalid : FStateTreeHandle(uint16(BatchIndex));
			}
		}
		
		// Create tasks
		check(StateTree->Nodes.Num() <= int32(MAX_uint16));
		CompactState.TasksBegin = uint16(StateTree->Nodes.Num());

		for (FStateTreeEditorNode& TaskNode : SourceState->Tasks)
		{
			if (!CreateTask(*SourceState, TaskNode))
			{
				return false;
			}
		}

		if (!CreateTask(*SourceState, SourceState->SingleTask))
		{
			return false;
		}
	
		const int32 TasksNum = StateTree->Nodes.Num() - int32(CompactState.TasksBegin);
		check(TasksNum <= int32(MAX_uint8));
		CompactState.TasksNum = uint8(TasksNum);
	}
	
	return true;
}

bool FStateTreeCompiler::CreateStateEvaluators()
{
	for (int32 i = 0; i < StateTree->States.Num(); i++)
	{
		FCompactStateTreeState& CompactState = StateTree->States[i];
		UStateTreeState* SourceState = SourceStates[i];
		check(SourceState != nullptr);

		FStateTreeCompilerLogStateScope LogStateScope(SourceState, Log);
		
		// Collect evaluators
		check(StateTree->Nodes.Num() <= int32(MAX_uint16));
		CompactState.EvaluatorsBegin = uint16(StateTree->Nodes.Num());

		for (FStateTreeEditorNode& EvalNode : SourceState->Evaluators)
		{
			if (!CreateEvaluator(*SourceState, EvalNode))
			{
				return false;
			}
		}
		
		const int32 EvaluatorsNum = StateTree->Nodes.Num() - int32(CompactState.EvaluatorsBegin);
		check(EvaluatorsNum <= int32(MAX_uint8));
		CompactState.EvaluatorsNum = uint8(EvaluatorsNum);
	}

	return true;
}

bool FStateTreeCompiler::CreateStateTransitions()
{
	for (int32 i = 0; i < StateTree->States.Num(); i++)
	{
		FCompactStateTreeState& CompactState = StateTree->States[i];
		UStateTreeState* SourceState = SourceStates[i];
		check(SourceState != nullptr);

		FStateTreeCompilerLogStateScope LogStateScope(SourceState, Log);
		
		// Enter conditions.
		CompactState.EnterConditionsBegin = uint16(StateTree->Nodes.Num());
		if (!CreateConditions(*SourceState, SourceState->EnterConditions))
		{
			Log.Reportf(EMessageSeverity::Error,
				TEXT("Failed to create state enter condition."));
			return false;
		}
		CompactState.EnterConditionsNum = uint8(uint16(StateTree->Nodes.Num()) - CompactState.EnterConditionsBegin);

		// Linked state
		if (SourceState->Type == EStateTreeStateType::Linked)
		{
			// Make sure the linked state is not self or parent to this state.
			const UStateTreeState* LinkedParentState = nullptr;
			for (const UStateTreeState* State = SourceState; State != nullptr; State = State->Parent)
			{
				if (State->ID == SourceState->LinkedState.ID)
				{
					LinkedParentState = State;
					break;
				}
			}
			
			if (LinkedParentState != nullptr)
			{
				Log.Reportf(EMessageSeverity::Error,
					TEXT("State is linked to it's parent state '%s', which will create infinite loop."),
					*LinkedParentState->Name.ToString());
				return false;
			}
			
			CompactState.LinkedState = GetStateHandle(SourceState->LinkedState.ID);
			
			if (!CompactState.LinkedState.IsValid())
			{
				Log.Reportf(EMessageSeverity::Error,
					TEXT("Failed to resolve linked state '%s'."),
					*SourceState->LinkedState.Name.ToString());
				return false;
			}
		}
		
		// Transitions
		CompactState.TransitionsBegin = uint16(StateTree->Transitions.Num());
		for (FStateTreeTransition& Transition : SourceState->Transitions)
		{
			FCompactStateTransition& BakedTransition = StateTree->Transitions.AddDefaulted_GetRef();
			BakedTransition.Event = Transition.Event;
			BakedTransition.Type = Transition.State.Type;
			BakedTransition.GateDelay = (uint8)FMath::Clamp(FMath::CeilToInt(Transition.GateDelay * 10.0f), 0, 255);
			BakedTransition.State = FStateTreeHandle::Invalid;
			if (!ResolveTransitionState(*SourceState, Transition.State, BakedTransition.State))
			{
				return false;
			}
			// Note: Unset transition is allowed here. It can be used to mask a transition at parent.

			BakedTransition.ConditionsBegin = uint16(StateTree->Nodes.Num());
			if (!CreateConditions(*SourceState, Transition.Conditions))
			{
				Log.Reportf(EMessageSeverity::Error,
					TEXT("Failed to create condition for transition to '%s'."),
					*Transition.State.Name.ToString());
				return false;
			}
			BakedTransition.ConditionsNum = uint8(uint16(StateTree->Nodes.Num()) - BakedTransition.ConditionsBegin);
		}
		CompactState.TransitionsNum = uint8(uint16(StateTree->Transitions.Num()) - CompactState.TransitionsBegin);
	}

	// @todo: Add test to check that all success/failure transition is possible (see editor).
	
	return true;
}

bool FStateTreeCompiler::ResolveTransitionState(const UStateTreeState& SourceState, const FStateTreeStateLink& Link, FStateTreeHandle& OutTransitionHandle) const 
{
	if (Link.Type == EStateTreeTransitionType::GotoState)
	{
		OutTransitionHandle = GetStateHandle(Link.ID);
		if (!OutTransitionHandle.IsValid())
		{
			Log.Reportf(EMessageSeverity::Error,
				TEXT("Failed to resolve transition to state '%s'."),
				*Link.Name.ToString());
			return false;
		}
	}
	else if (Link.Type == EStateTreeTransitionType::NextState)
	{
		// Find next state.
		const UStateTreeState* NextState = SourceState.GetNextSiblingState();
		if (NextState == nullptr)
		{
			Log.Reportf(EMessageSeverity::Error,
				TEXT("Failed to resolve transition, there's no next state."));
			return false;
		}
		OutTransitionHandle = GetStateHandle(NextState->ID);
		if (!OutTransitionHandle.IsValid())
		{
			Log.Reportf(EMessageSeverity::Error,
				TEXT("Failed to resolve transition next state, no handle found for '%s'."),
				*NextState->Name.ToString());
			return false;
		}
	}
	
	return true;
}

bool FStateTreeCompiler::CreateCondition(UStateTreeState& State, const FStateTreeEditorNode& CondNode, const EStateTreeConditionOperand Operand, const int8 DeltaIndent)
{
	if (!CondNode.Node.IsValid())
	{
		// Empty line in conditions array, just silently ignore.
		return true;
	}

	FStateTreeBindableStructDesc StructDesc;
	StructDesc.ID = CondNode.ID;
	StructDesc.Name = CondNode.Node.GetScriptStruct()->GetFName();
	StructDesc.DataSource = EStateTreeBindableStructSource::Condition;

	// Check that item has valid instance initialized.
	if (!CondNode.Instance.IsValid() && CondNode.InstanceObject == nullptr)
	{
		Log.Reportf(EMessageSeverity::Error, StructDesc,
			TEXT("Malformed condition, missing instance value."));
		return false;
	}

	// Copy the condition
	FInstancedStruct& Item = StateTree->Nodes.AddDefaulted_GetRef();
	Item = CondNode.Node;

	FStateTreeConditionBase& Cond = Item.GetMutable<FStateTreeConditionBase>();

	Cond.Operand = Operand;
	Cond.DeltaIndent = DeltaIndent;
	
	if (CondNode.Instance.IsValid())
	{
		// Struct instance
		FInstancedStruct& Instance = StateTree->SharedInstances.AddDefaulted_GetRef();
		const int32 InstanceIndex = StateTree->SharedInstances.Num() - 1;
		
		Instance = CondNode.Instance;
	
		// Create binding source struct descriptor.
		StructDesc.Struct = Instance.GetScriptStruct();
		StructDesc.Name = Cond.Name;

		check(InstanceIndex <= int32(MAX_uint16));
		Cond.InstanceIndex = uint16(InstanceIndex);
		Cond.bInstanceIsObject = false;
	}
	else
	{
		// Object Instance
		check(CondNode.InstanceObject != nullptr);
		
		UObject* Instance = DuplicateObject(CondNode.InstanceObject, StateTree);
		
		StateTree->InstanceObjects.Add(Instance);
		const int32 InstanceIndex = StateTree->SharedInstanceObjects.Num() - 1;
		
		// Create binding source struct descriptor.
		StructDesc.Struct = Instance->GetClass();
		StructDesc.Name = Cond.Name;

		check(InstanceIndex <= int32(MAX_uint16));
		Cond.InstanceIndex = uint16(InstanceIndex);
		Cond.bInstanceIsObject = true;
	}

	// Mark the struct as binding source.
	const int32 SourceStructIndex = BindingsCompiler.AddSourceStruct(StructDesc);

	// Check that the bindings for this struct are still all valid.
	TArray<FStateTreeEditorPropertyBinding> Bindings;
	if (!GetAndValidateBindings(State, StructDesc, Bindings))
	{
		return false;
	}

	// Compile batch copy for this struct, we pass in all the bindings, the compiler will pick up the ones for the target structs.
	int32 BatchIndex = INDEX_NONE;
	if (!BindingsCompiler.CompileBatch(StructDesc, Bindings, BatchIndex))
	{
		return false;
	}
	check(BatchIndex < int32(MAX_uint16));
	Cond.BindingsBatch = BatchIndex == INDEX_NONE ? FStateTreeHandle::Invalid : FStateTreeHandle(uint16(BatchIndex));

	check(SourceStructIndex <= int32(MAX_uint16));
	Cond.DataViewIndex = uint16(SourceStructIndex);
	
	return true;
}

bool FStateTreeCompiler::CreateTask(UStateTreeState& State, const FStateTreeEditorNode& TaskNode)
{
	// Silently ignore empty items.
	if (!TaskNode.Node.IsValid())
	{
		return true;
	}
	
	// Create binding source struct descriptor.
	FStateTreeBindableStructDesc StructDesc;
	StructDesc.ID = TaskNode.ID;
	StructDesc.Name = TaskNode.Node.GetScriptStruct()->GetFName();
	StructDesc.DataSource = EStateTreeBindableStructSource::Task;

	// Check that item has valid instance initialized.
	if (!TaskNode.Instance.IsValid() && TaskNode.InstanceObject == nullptr)
	{
		Log.Reportf(EMessageSeverity::Error, StructDesc,
			TEXT("Malformed task, missing instance value."));
		return false;
	}

	// Copy the task
	FInstancedStruct& Item = StateTree->Nodes.AddDefaulted_GetRef();
	Item = TaskNode.Node;
	
	FStateTreeTaskBase& Task = Item.GetMutable<FStateTreeTaskBase>();

	if (TaskNode.Instance.IsValid())
	{
		// Struct Instance
		FInstancedStruct& Instance = StateTree->Instances.AddDefaulted_GetRef();
		const int32 InstanceIndex = StateTree->Instances.Num() - 1;

		Instance = TaskNode.Instance;

		// Create binding source struct descriptor.
		StructDesc.Struct = Instance.GetScriptStruct();
		StructDesc.Name = Task.Name;

		check(InstanceIndex <= int32(MAX_uint16));
		Task.InstanceIndex = uint16(InstanceIndex);
		Task.bInstanceIsObject = false;
	}
	else
	{
		// Object Instance
		check(TaskNode.InstanceObject != nullptr);

		UObject* Instance = DuplicateObject(TaskNode.InstanceObject, StateTree);

		StateTree->InstanceObjects.Add(Instance);
		const int32 InstanceIndex = StateTree->InstanceObjects.Num() - 1;

		// Create binding source struct descriptor.
		StructDesc.Struct = Instance->GetClass();
		StructDesc.Name = Task.Name;

		check(InstanceIndex <= int32(MAX_uint16));
		Task.InstanceIndex = uint16(InstanceIndex);
		Task.bInstanceIsObject = true;
	}

	// Mark the instance as binding source.
	const int32 SourceStructIndex = BindingsCompiler.AddSourceStruct(StructDesc);

	// Check that the bindings for this struct are still all valid.
	TArray<FStateTreeEditorPropertyBinding> Bindings;
	if (!GetAndValidateBindings(State, StructDesc, Bindings))
	{
		return false;
	}

	// Compile batch copy for this struct, we pass in all the bindings, the compiler will pick up the ones for the target structs.
	int32 BatchIndex = INDEX_NONE;
	if (!BindingsCompiler.CompileBatch(StructDesc, Bindings, BatchIndex))
	{
		return false;
	}
	
	check(BatchIndex < int32(MAX_uint16));
	Task.BindingsBatch = BatchIndex == INDEX_NONE ? FStateTreeHandle::Invalid : FStateTreeHandle(uint16(BatchIndex));
	
	check(SourceStructIndex <= int32(MAX_uint16));
	Task.DataViewIndex = uint16(SourceStructIndex);

	return true;
}

bool FStateTreeCompiler::CreateEvaluator(UStateTreeState& State, const FStateTreeEditorNode& EvalNode)
{
	// Silently ignore empty items.
	if (!EvalNode.Node.IsValid())
	{
		return true;
	}
	
	// Create binding source struct descriptor.
	FStateTreeBindableStructDesc StructDesc;
    StructDesc.ID = EvalNode.ID;
    StructDesc.Name = EvalNode.Node.GetScriptStruct()->GetFName();
	StructDesc.DataSource = EStateTreeBindableStructSource::Evaluator;

    // Check that item has valid instance initialized.
    if (!EvalNode.Instance.IsValid() && EvalNode.InstanceObject == nullptr)
    {
        Log.Reportf(EMessageSeverity::Error, StructDesc,
        	TEXT("Malformed evaluator, missing instance value."));
        return false;
    }

	// Copy the evaluator
	FInstancedStruct& Item = StateTree->Nodes.AddDefaulted_GetRef();
	Item = EvalNode.Node;

	FStateTreeEvaluatorBase& Eval = Item.GetMutable<FStateTreeEvaluatorBase>();

	if (EvalNode.Instance.IsValid())
	{
		// Struct Instance
		FInstancedStruct& Instance = StateTree->Instances.AddDefaulted_GetRef();
		const int32 InstanceIndex = StateTree->Instances.Num() - 1;

		Instance = EvalNode.Instance;

		// Create binding source struct descriptor.
		StructDesc.Struct = Instance.GetScriptStruct();
		StructDesc.Name = Eval.Name;

		check(InstanceIndex <= int32(MAX_uint16));
		Eval.InstanceIndex = uint16(InstanceIndex);
		Eval.bInstanceIsObject = false;
	}
	else
	{
		// Object Instance
		check(EvalNode.InstanceObject != nullptr);

		UObject* Instance = DuplicateObject(EvalNode.InstanceObject, StateTree);

		StateTree->InstanceObjects.Add(Instance);
		const int32 InstanceIndex = StateTree->InstanceObjects.Num() - 1;

		// Create binding source struct descriptor.
		StructDesc.Struct = Instance->GetClass();
		StructDesc.Name = Eval.Name;

		check(InstanceIndex <= int32(MAX_uint16));
		Eval.InstanceIndex = uint16(InstanceIndex);
		Eval.bInstanceIsObject = true;
	}
		
	// Mark the instance as binding source.
	const int32 SourceStructIndex = BindingsCompiler.AddSourceStruct(StructDesc);

	// Check that the bindings for this struct are still all valid.
	TArray<FStateTreeEditorPropertyBinding> Bindings;
	if (!GetAndValidateBindings(State, StructDesc, Bindings))
	{
		return false;
	}

	// Compile batch copy for this struct, we pass in all the bindings, the compiler will pick up the ones for the target structs.
	int32 BatchIndex = INDEX_NONE;
	if (!BindingsCompiler.CompileBatch(StructDesc, Bindings, BatchIndex))
	{
		return false;
	}
	
	check(BatchIndex < int32(MAX_uint16));
	Eval.BindingsBatch = BatchIndex == INDEX_NONE ? FStateTreeHandle::Invalid : FStateTreeHandle(uint16(BatchIndex));
	
	check(SourceStructIndex <= int32(MAX_uint16));
	Eval.DataViewIndex = uint16(SourceStructIndex);

	return true;
}

bool FStateTreeCompiler::IsPropertyAnyEnum(const FStateTreeBindableStructDesc& Struct, FStateTreeEditorPropertyPath Path) const
{
	bool bIsAnyEnum = false;
	TArray<FStateTreePropertySegment> Segments;
	const FProperty* LeafProperty = nullptr;
	int32 LeafArrayIndex = INDEX_NONE;
	const bool bResolved = FStateTreePropertyBindingCompiler::ResolvePropertyPath(Struct, Path, Segments, LeafProperty, LeafArrayIndex);
	if (bResolved && LeafProperty)
	{
		if (const FProperty* OwnerProperty = LeafProperty->GetOwnerProperty())
		{
			if (const FStructProperty* OwnerStructProperty = CastField<FStructProperty>(OwnerProperty))
			{
				bIsAnyEnum = OwnerStructProperty->Struct == FStateTreeAnyEnum::StaticStruct();
			}
		}
	}
	return bIsAnyEnum;
}

bool FStateTreeCompiler::GetAndValidateBindings(UStateTreeState& State, const FStateTreeBindableStructDesc& TargetStruct, TArray<FStateTreeEditorPropertyBinding>& OutBindings) const
{
	OutBindings.Reset();
	
	for (const FStateTreeEditorPropertyBinding& Binding : TreeData->EditorBindings.GetBindings())
	{
		if (Binding.TargetPath.StructID != TargetStruct.ID)
		{
			continue;
		}

		// Source must be one of the source structs we have discovered in the tree.
		const FGuid SourceStructID = Binding.SourcePath.StructID;
		const int32 SourceStructIdx = BindingsCompiler.GetSourceStructIndexByID(SourceStructID);
		if (SourceStructIdx == INDEX_NONE)
		{
			Log.Reportf(EMessageSeverity::Error, TargetStruct,
						TEXT("Failed to find binding source property '%s' for target '%s:%s'."),
						*Binding.SourcePath.ToString(), *TargetStruct.Name.ToString(), *Binding.TargetPath.ToString());
			return false;
		}
		const FStateTreeBindableStructDesc& SourceStruct = BindingsCompiler.GetSourceStructDesc(SourceStructIdx);

		// Source must be accessible by the target struct via all execution paths.
		TArray<FStateTreeBindableStructDesc> AccessibleStructs;
		TreeData->GetAccessibleStructs(Binding.TargetPath.StructID, AccessibleStructs);

		const bool bSourceAccessible = AccessibleStructs.ContainsByPredicate([SourceStructID](const FStateTreeBindableStructDesc& Structs)
			{
				return (Structs.ID == SourceStructID);
			});

		if (!bSourceAccessible)
		{
			Log.Reportf(EMessageSeverity::Error, TargetStruct,
						TEXT("Property '%s:%s' cannot be bound to '%s:%s', because the binding source '%s' is not updated before '%s' in the tree."),
						*SourceStruct.Name.ToString(), *Binding.SourcePath.ToString(),
						*TargetStruct.Name.ToString(), *Binding.TargetPath.ToString(),
						*SourceStruct.Name.ToString(), *TargetStruct.Name.ToString());
			return false;
		}
		
		// Special case fo AnyEnum. StateTreeBindingExtension allows AnyEnums to bind to other enum types.
		// The actual copy will be done via potential type promotion copy, into the value property inside the AnyEnum.
		// We amend the paths here to point to the 'Value' property.
		const bool bSourceIsAnyEnum = IsPropertyAnyEnum(SourceStruct, Binding.SourcePath);
		const bool bTargetIsAnyEnum = IsPropertyAnyEnum(TargetStruct, Binding.TargetPath);
		if (bSourceIsAnyEnum || bTargetIsAnyEnum)
		{
			FStateTreeEditorPropertyBinding ModifiedBinding(Binding);
			if (bSourceIsAnyEnum)
			{
				ModifiedBinding.SourcePath.Path.Add(GET_MEMBER_NAME_STRING_CHECKED(FStateTreeAnyEnum, Value));
			}
			if (bTargetIsAnyEnum)
			{
				ModifiedBinding.TargetPath.Path.Add(GET_MEMBER_NAME_STRING_CHECKED(FStateTreeAnyEnum, Value));
			}
			OutBindings.Add(ModifiedBinding);
		}
		else
		{
			OutBindings.Add(Binding);
		}
	}

	return true;
}
