// Copyright Epic Games, Inc. All Rights Reserved.

#include "StateTreeBaker.h"
#include "StateTree.h"
#include "StateTreeEditorData.h"
#include "StateTreeTypes.h"
#include "Conditions/StateTreeCondition_Common.h"
#include "StateTreeEvaluatorBase.h"
#include "StateTreeTaskBase.h"
#include "StateTreeConditionBase.h"
#include "StateTreeState.h"
#include "StateTreeExecutionContext.h"
#include "CoreMinimal.h"
#include "StateTreePropertyBindingCompiler.h"


bool FStateTreeBaker::Bake(UStateTree& InStateTree)
{
	StateTree = &InStateTree;
	TreeData = Cast<UStateTreeEditorData>(StateTree->EditorData);
	if (!TreeData)
	{
		return false;
	}

	// Cleanup existing state
	StateTree->ResetBaked();

	BindingsCompiler.Init(StateTree->PropertyBindings, Log);

	if (!CreateStates())
	{
		StateTree->ResetBaked();
		return false;
	}

	if (!CreateStateTransitions())
	{
		StateTree->ResetBaked();
		return false;
	}

	BindingsCompiler.Finalize();

	// TODO: This is just for testing. It should be called just before use.
	StateTree->ResolvePropertyPaths();

	StateTree->Link();
	
	StateTree->InitInstanceStorageType();

	return true;
}

FStateTreeHandle FStateTreeBaker::GetStateHandle(const FGuid& StateID) const
{
	const int32* Idx = IDToState.Find(StateID);
	if (Idx == nullptr)
	{
		return FStateTreeHandle::Invalid;
	}

	return FStateTreeHandle(uint16(*Idx));
}

bool FStateTreeBaker::CreateStates()
{
	// Create item for the runtime execution state
	StateTree->Instances.Add(FInstancedStruct::Make<FStateTreeExecutionState>());

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
	
	return true;
}

bool FStateTreeBaker::CreateStateTransitions()
{
	for (int32 i = 0; i < StateTree->States.Num(); i++)
	{
		FBakedStateTreeState& BakedState = StateTree->States[i];
		UStateTreeState* SourceState = SourceStates[i];
		check(SourceState != nullptr);

		FStateTreeCompilerLogStateScope LogStateScope(SourceState, Log);
		
		// Enter conditions.
		BakedState.EnterConditionsBegin = uint16(StateTree->Items.Num());
		for (FStateTreeEditorNode& CondNode : SourceState->EnterConditions)
		{
			if (!CreateCondition(CondNode))
			{
				Log.Reportf(EMessageSeverity::Error,
					TEXT("Failed to create state enter condition."));
				return false;
			}
		}
		BakedState.EnterConditionsNum = uint8(uint16(StateTree->Items.Num()) - BakedState.EnterConditionsBegin);

		// Transitions
		BakedState.TransitionsBegin = uint16(StateTree->Transitions.Num());
		for (FStateTreeTransition& Transition : SourceState->Transitions)
		{
			FBakedStateTransition& BakedTransition = StateTree->Transitions.AddDefaulted_GetRef();
			BakedTransition.Event = Transition.Event;
			BakedTransition.Type = Transition.State.Type;
			BakedTransition.GateDelay = (uint8)FMath::Clamp(FMath::CeilToInt(Transition.GateDelay * 10.0f), 0, 255);
			BakedTransition.State = FStateTreeHandle::Invalid;
			if (!ResolveTransitionState(*SourceState, Transition.State, BakedTransition.State))
			{
				return false;
			}
			// Note: Unset transition is allowed here. It can be used to mask a transition at parent.

			BakedTransition.ConditionsBegin = uint16(StateTree->Items.Num());
			for (FStateTreeEditorNode& CondNode : Transition.Conditions)
			{
				if (!CreateCondition(CondNode))
				{
					Log.Reportf(EMessageSeverity::Error,
						TEXT("Failed to create condition for transition to %s."),
						*Transition.State.Name.ToString());
					return false;
				}
			}
			BakedTransition.ConditionsNum = uint8(uint16(StateTree->Items.Num()) - BakedTransition.ConditionsBegin);
		}
		BakedState.TransitionsNum = uint8(uint16(StateTree->Transitions.Num()) - BakedState.TransitionsBegin);
	}

	// @todo: Add test to check that all success/failure transition is possible (see editor).
	
	return true;
}

bool FStateTreeBaker::ResolveTransitionState(const UStateTreeState& SourceState, const FStateTreeStateLink& Link, FStateTreeHandle& OutTransitionHandle) const 
{
	if (Link.Type == EStateTreeTransitionType::GotoState)
	{
		OutTransitionHandle = GetStateHandle(Link.ID);
		if (!OutTransitionHandle.IsValid())
		{
			Log.Reportf(EMessageSeverity::Error,
				TEXT("Failed to resolve transition to state %s."),
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
				TEXT("Failed to resolve transition next state, no handle found for %s."),
				*NextState->Name.ToString());
			return false;
		}
	}
	
	return true;
}

bool FStateTreeBaker::CreateCondition(const FStateTreeEditorNode& CondNode)
{
	if (!CondNode.Node.IsValid())
	{
		// Empty line in conditions array, just silently ignore.
		return true;
	}

	FStateTreeBindableStructDesc StructDesc;
	StructDesc.ID = CondNode.ID;
	StructDesc.Name = CondNode.Node.GetScriptStruct()->GetFName();

	// Check that item has valid instance initialized.
	if (!CondNode.Instance.IsValid() && CondNode.InstanceObject == nullptr)
	{
		Log.Reportf(EMessageSeverity::Error, StructDesc,
			TEXT("Malformed condition, missing instance value."));
		return false;
	}

	// Copy the condition
	FInstancedStruct& Item = StateTree->Items.AddDefaulted_GetRef();
	Item = CondNode.Node;

	FStateTreeConditionBase& Cond = Item.GetMutable<FStateTreeConditionBase>();

	if (CondNode.Instance.IsValid())
	{
		// Struct instance
		FInstancedStruct& Instance = StateTree->Instances.AddDefaulted_GetRef();
		const int32 InstanceIndex = StateTree->Instances.Num() - 1;
		
		Instance = CondNode.Instance;
	
		// Create binding source struct descriptor.
		StructDesc.Struct = Instance.GetScriptStruct();
		StructDesc.Name = StructDesc.Struct->GetFName();

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
		const int32 InstanceIndex = StateTree->InstanceObjects.Num() - 1;
		
		// Create binding source struct descriptor.
		StructDesc.Struct = Instance->GetClass();
		StructDesc.Name = StructDesc.Struct->GetFName();

		check(InstanceIndex <= int32(MAX_uint16));
		Cond.InstanceIndex = uint16(InstanceIndex);
		Cond.bInstanceIsObject = true;
	}

	// Mark the struct as binding source.
	const int32 SourceStructIndex = BindingsCompiler.AddSourceStruct(StructDesc);

	// Check that the bindings for this struct are still all valid.
	TArray<FStateTreeEditorPropertyBinding> Bindings;
	if (!GetAndValidateBindings(StructDesc, Bindings))
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

bool FStateTreeBaker::CreateTask(const FStateTreeEditorNode& TaskNode)
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

	// Check that item has valid instance initialized.
	if (!TaskNode.Instance.IsValid() && TaskNode.InstanceObject == nullptr)
	{
		Log.Reportf(EMessageSeverity::Error, StructDesc,
			TEXT("Malformed evaluator, missing instance value."));
		return false;
	}

	// Copy the task
	FInstancedStruct& Item = StateTree->Items.AddDefaulted_GetRef();
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
	if (!GetAndValidateBindings(StructDesc, Bindings))
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

bool FStateTreeBaker::CreateEvaluator(const FStateTreeEditorNode& EvalNode)
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

    // Check that item has valid instance initialized.
    if (!EvalNode.Instance.IsValid() && EvalNode.InstanceObject == nullptr)
    {
        Log.Reportf(EMessageSeverity::Error, StructDesc,
        	TEXT("Malformed evaluator, missing instance value."));
        return false;
    }

	// Copy the evaluator
	FInstancedStruct& Item = StateTree->Items.AddDefaulted_GetRef();
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
	if (!GetAndValidateBindings(StructDesc, Bindings))
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

bool FStateTreeBaker::IsPropertyAnyEnum(const FStateTreeBindableStructDesc& Struct, FStateTreeEditorPropertyPath Path) const
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

bool FStateTreeBaker::GetAndValidateBindings(const FStateTreeBindableStructDesc& TargetStruct, TArray<FStateTreeEditorPropertyBinding>& OutBindings) const
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
						TEXT("Failed to find binding source %s:%s."),
						*TargetStruct.Name.ToString(), *Binding.SourcePath.ToString());
			return false;
		}
		const FStateTreeBindableStructDesc& SourceStruct = BindingsCompiler.GetSourceStructDesc(SourceStructIdx);

		// Source must be accessible by the target struct.
		TArray<FStateTreeBindableStructDesc> AccessibleStructs;
		TreeData->GetAccessibleStructs(Binding.TargetPath.StructID, AccessibleStructs);
		const bool SourceAccessible = AccessibleStructs.ContainsByPredicate([SourceStructID](const FStateTreeBindableStructDesc& Structs)
			{
				return (Structs.ID == SourceStructID);
			});
		if (!SourceAccessible)
		{
			Log.Reportf(EMessageSeverity::Error, TargetStruct,
        				TEXT("%s:%s is not accessible to %s:%s."),
        				*SourceStruct.Name.ToString(), *Binding.SourcePath.ToString(), *TargetStruct.Name.ToString(), *Binding.TargetPath.ToString());
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

bool FStateTreeBaker::CreateStateRecursive(UStateTreeState& State, const FStateTreeHandle Parent)
{
	FStateTreeCompilerLogStateScope LogStateScope(&State, Log);

	const int32 StateIdx = StateTree->States.AddDefaulted();
	FBakedStateTreeState& BakedState = StateTree->States[StateIdx];
	BakedState.Name = State.Name;
	BakedState.Parent = Parent;

	SourceStates.Add(&State);
	IDToState.Add(State.ID, StateIdx);

	check(StateTree->Instances.Num() <= int32(MAX_uint16));

	// Collect evaluators
	check(StateTree->Items.Num() <= int32(MAX_uint16));
	BakedState.EvaluatorsBegin = uint16(StateTree->Items.Num());

	for (FStateTreeEditorNode& EvalNode : State.Evaluators)
	{
		if (!CreateEvaluator(EvalNode))
		{
			return false;
		}
	}
	
	const int32 EvaluatorsNum = StateTree->Items.Num() - int32(BakedState.EvaluatorsBegin);
	check(EvaluatorsNum <= int32(MAX_uint8));
	BakedState.EvaluatorsNum = uint8(EvaluatorsNum);

	// Collect tasks
	check(StateTree->Items.Num() <= int32(MAX_uint16));
	BakedState.TasksBegin = uint16(StateTree->Items.Num());

	for (FStateTreeEditorNode& TaskNode : State.Tasks)
	{
		if (!CreateTask(TaskNode))
		{
			return false;
		}
	}

	if (!CreateTask(State.SingleTask))
	{
		return false;
	}
	
	const int32 TasksNum = StateTree->Items.Num() - int32(BakedState.TasksBegin);
	check(TasksNum <= int32(MAX_uint8));
	BakedState.TasksNum = uint8(TasksNum);

	// Child states
	check(StateTree->States.Num() <= int32(MAX_uint16));
	BakedState.ChildrenBegin = uint16(StateTree->States.Num());
	for (UStateTreeState* Child : State.Children)
	{
		if (Child)
		{
			if (!CreateStateRecursive(*Child, FStateTreeHandle((uint16)StateIdx)))
			{
				return false;
			}
		}
	}
	check(StateTree->States.Num() <= int32(MAX_uint16));
	StateTree->States[StateIdx].ChildrenEnd = uint16(StateTree->States.Num()); // Cannot use BakedState here, it may be invalid due to array resize.

	return true;
}
