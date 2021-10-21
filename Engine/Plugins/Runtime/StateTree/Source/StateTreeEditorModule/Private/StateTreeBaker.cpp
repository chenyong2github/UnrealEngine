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


bool FStateTreeBaker::ResolveTransitionState(const UStateTreeState& SourceState, const TCHAR* ContextStr, const FStateTreeStateLink& Link, FStateTreeHandle& OutTransitionHandle) const 
{
	if (Link.Type == EStateTreeTransitionType::GotoState)
	{
		OutTransitionHandle = GetStateHandle(Link.ID);
		if (!OutTransitionHandle.IsValid())
		{
			UE_LOG(LogStateTree, Error, TEXT("%s: '%s':%s: Failed to resolve %s to %s."), ANSI_TO_TCHAR(__FUNCTION__), *GetNameSafe(StateTree), *SourceState.Name.ToString(), ContextStr, *Link.Name.ToString());
			return false;
		}
	}
	else if (Link.Type == EStateTreeTransitionType::NextState)
	{
		// Find next state.
		const UStateTreeState* NextState = SourceState.GetNextSiblingState();
		if (NextState == nullptr)
		{
			UE_LOG(LogStateTree, Error, TEXT("%s: '%s':%s: Failed to resolve default transition, there's no next State after %s."), ANSI_TO_TCHAR(__FUNCTION__), *GetNameSafe(StateTree), *SourceState.Name.ToString(), ContextStr, *SourceState.Name.ToString());
			return false;
		}
		OutTransitionHandle = GetStateHandle(NextState->ID);
		if (!OutTransitionHandle.IsValid())
		{
			UE_LOG(LogStateTree, Error, TEXT("%s: '%s':%s: Failed to resolve default transition next state %s."), ANSI_TO_TCHAR(__FUNCTION__), *GetNameSafe(StateTree), *SourceState.Name.ToString(), ContextStr, *Link.Name.ToString());
			return false;
		}
	}
	
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

	BindingsCompiler.Init(StateTree->PropertyBindings);

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

	StateTree->InitRuntimeStorage();

	// TODO: This is just for testing. It should be called just before use.
	StateTree->ResolvePropertyPaths();

	return true;
}

bool FStateTreeBaker::CreateStates()
{
	// Create item for the runtime execution state
	StateTree->RuntimeStorageItems.Add(FInstancedStruct::Make<FStateTreeExecutionState>());

	for (UStateTreeState* Routine : TreeData->Routines)
	{
		if (Routine)
		{
			if (!CreateStateRecursive(*Routine, FStateTreeHandle::Invalid))
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
		check(SourceState);

		// Enter conditions.
		BakedState.EnterConditionsBegin = uint16(StateTree->Conditions.Num());
		for (FStateTreeConditionItem& ConditionItem : SourceState->EnterConditions)
		{
			if (!CreateCondition(ConditionItem))
			{
				UE_LOG(LogStateTree, Error, TEXT("%s: '%s':%s: Failed to create state enter condition."), ANSI_TO_TCHAR(__FUNCTION__), *GetNameSafe(StateTree), *SourceState->Name.ToString());
				return false;
			}
		}
		BakedState.EnterConditionsNum = uint8(uint16(StateTree->Conditions.Num()) - BakedState.EnterConditionsBegin);

		// Transitions
		BakedState.TransitionsBegin = uint16(StateTree->Transitions.Num());
		for (FStateTreeTransition& Transition : SourceState->Transitions)
		{
			FBakedStateTransition& BakedTransition = StateTree->Transitions.AddDefaulted_GetRef();
			BakedTransition.Event = Transition.Event;
			BakedTransition.Type = Transition.State.Type;
			BakedTransition.GateDelay = (uint8)FMath::Clamp(FMath::CeilToInt(Transition.GateDelay * 10.0f), 0, 255);
			BakedTransition.State = FStateTreeHandle::Invalid;
			if (!ResolveTransitionState(*SourceState, TEXT("transition"), Transition.State, BakedTransition.State))
			{
				return false;
			}
			// Note: Unset transition is allowed here. It can be used to mask a transition at parent.

			BakedTransition.ConditionsBegin = uint16(StateTree->Conditions.Num());
			for (FStateTreeConditionItem& ConditionItem : Transition.Conditions)
			{
				if (!CreateCondition(ConditionItem))
				{
					UE_LOG(LogStateTree, Error, TEXT("%s: '%s':%s: Failed to create transition condition to %s."), ANSI_TO_TCHAR(__FUNCTION__), *GetNameSafe(StateTree), *SourceState->Name.ToString(), *Transition.State.Name.ToString());
					return false;
				}
			}
			BakedTransition.ConditionsNum = uint8(uint16(StateTree->Conditions.Num()) - BakedTransition.ConditionsBegin);
		}
		BakedState.TransitionsNum = uint8(uint16(StateTree->Transitions.Num()) - BakedState.TransitionsBegin);
	}

	return true;
}

bool FStateTreeBaker::CreateCondition(const FStateTreeConditionItem& CondItem)
{
	if (!CondItem.Type.IsValid())
	{
		// Empty line in conditions array, just silently ignore.
		return true;
	}

	// Copy the condition
	FInstancedStruct& CondPtr = StateTree->Conditions.AddDefaulted_GetRef();
	CondPtr = CondItem.Type;
	check(CondPtr.IsValid());

	FStateTreeConditionBase& Cond = CondPtr.GetMutable<FStateTreeConditionBase>();

	// Create binding source struct descriptor. Note: not exposing the struct for reading.
	FStateTreeBindableStructDesc StructDesc;
	StructDesc.Struct = CondPtr.GetScriptStruct();
	StructDesc.Name = CondPtr.GetScriptStruct()->GetFName();
	StructDesc.ID = Cond.ID;

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

	return true;
}

bool FStateTreeBaker::IsPropertyAnyEnum(const FStateTreeBindableStructDesc& Struct, FStateTreeEditorPropertyPath Path) const
{
	bool bIsAnyEnum = false;
	TArray<FStateTreePropertySegment> Segments;
	FProperty* LeafProperty = nullptr;
	int32 LeafArrayIndex = INDEX_NONE;
	const bool bResolved = FStateTreePropertyBindingCompiler::ResolvePropertyPath(Struct, Path, Segments, LeafProperty, LeafArrayIndex);
	if (bResolved && LeafProperty)
	{
		if (FProperty* OwnerProperty = LeafProperty->GetOwnerProperty())
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
			UE_LOG(LogStateTree, Error, TEXT("%s: '%s': Failed to find container for path %s in %s."), ANSI_TO_TCHAR(__FUNCTION__), *GetNameSafe(StateTree), *Binding.SourcePath.ToString(), *TargetStruct.Name.ToString());
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
			UE_LOG(LogStateTree, Error, TEXT("%s: '%s': %s:%s is not accessible to %s:%s."), ANSI_TO_TCHAR(__FUNCTION__), *GetNameSafe(StateTree),
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

bool FStateTreeBaker::CreateExternalItemHandles(FStructView Item)
{
	check(StateTree);
	
	const UScriptStruct* ScriptStruct = Item.GetScriptStruct();
	check(ScriptStruct);

	static const FName BaseStructMetaName(TEXT("BaseStruct"));
	static const FName BaseClassMetaName(TEXT("BaseClass"));
	static const FName OptionalMetaName(TEXT("Optional"));

	//
	// Iterate over the main level properties of the struct and look for properties of type FStateTreeExternalItemHandle.
	// The meta data of the property defines what type will bound to it. Examples:
	//
	// Handle to WorldSubsystem:
	//		UPROPERTY(meta=(BaseClass="MassStateTreeSubsystem"))
	//		FStateTreeExternalItemHandle MassStateTreeSubSystemHandle;
	//
	// Handle to a struct (fragment), optional, meaning it can be null:
	//		UPROPERTY(meta=(BaseStruct="DataFragment_SmartObjectUser", Optional))
	//		FStateTreeExternalItemHandle SmartObjectUserHandle;

    for (TPropertyValueIterator<const FStructProperty> It(ScriptStruct, Item.GetMutableMemory(), EPropertyValueIteratorFlags::NoRecursion); It; ++It)
    {
    	const FStructProperty* StructProperty = It->Key;
    	check(StructProperty);
    	if (StructProperty->Struct == FStateTreeExternalItemHandle::StaticStruct())
    	{
    		// Find Class or ScriptStruct.
    		const UStruct* Struct = nullptr;
    		if (StructProperty->HasMetaData(BaseStructMetaName))
    		{
	    		const FString& BaseStructName = StructProperty->GetMetaData(BaseStructMetaName);
				Struct = FindObject<UScriptStruct>(ANY_PACKAGE, *BaseStructName);
    			if (Struct == nullptr)
    			{
    				UE_LOG(LogStateTree, Error, TEXT("%s: Struct '%s' does not exists."), ANSI_TO_TCHAR(__FUNCTION__), *BaseStructName);
    				return false;
    			}
    		}
    		else if (StructProperty->HasMetaData(BaseClassMetaName))
    		{
    			const FString& BaseClassName = StructProperty->GetMetaData(BaseClassMetaName);
				Struct = FindObject<UClass>(ANY_PACKAGE, *BaseClassName);
    			if (Struct == nullptr)
    			{
    				UE_LOG(LogStateTree, Error, TEXT("%s: Class '%s' does not exists."), ANSI_TO_TCHAR(__FUNCTION__), *BaseClassName);
    				return false;
    			}
    		}
    		else
    		{
    			UE_LOG(LogStateTree, Error, TEXT("%s: FStateTreeExternalItemHandle must have 'BaseStruct' or 'BaseClass' set."), ANSI_TO_TCHAR(__FUNCTION__));
    			return false;
    		}

    		// Parse optional
    		const bool bOptional = StructProperty->HasMetaData(OptionalMetaName);

    		// The struct/class must be accepted by the schema.
    		if (StateTree->Schema && !StateTree->Schema->IsExternalItemAllowed(*Struct))
    		{
    			UE_LOG(LogStateTree, Error, TEXT("%s: Schema %s does not allow item type %s."), ANSI_TO_TCHAR(__FUNCTION__), *GetNameSafe(StateTree->Schema), *GetNameSafe(Struct));
    			return false;
    		}

    		// Check if similar struct already exists, if not add new.
    		FStateTreeExternalItemDesc* ExternalItem = StateTree->ExternalItems.FindByPredicate([Struct](const FStateTreeExternalItemDesc& Item) { return Item.Struct == Struct; });
    		if (ExternalItem == nullptr)
    		{
    			ExternalItem = &StateTree->ExternalItems.Emplace_GetRef(Struct, bOptional);
    		
    			// The external struct pointer is stored in the same array as property binding sources.
    			// This is done also as anticipation to allow to bind to external items.
    			FStateTreeBindableStructDesc StructDesc;
    			StructDesc.Struct = Struct;
    			StructDesc.Name = FName(Struct->GetName() + TEXT("_External"));
    			StructDesc.ID = FGuid(); // Empty GUID, this item cannot be bound to.

    			const int32 StructIndex = BindingsCompiler.AddSourceStruct(StructDesc);

    			check(StructIndex <= int32(MAX_uint16));
    			ExternalItem->Handle.SetIndex(uint16(StructIndex));
			}
    		else
    		{
    			// If same type is requested as required, clear optional flag. 
    			if (!bOptional)
    			{
    				ExternalItem->bOptional = false;
    			}
    		}

    		check(ExternalItem);

    		// Update the handle on Eval/struct
    		FStateTreeExternalItemHandle& Handle = *static_cast<FStateTreeExternalItemHandle*>(const_cast<void*>(It.Value()));
    		Handle = ExternalItem->Handle;
    	}
    }

	return true;
}

bool FStateTreeBaker::CreateStateRecursive(UStateTreeState& State, const FStateTreeHandle Parent)
{
	const int32 StateIdx = StateTree->States.AddDefaulted();
	FBakedStateTreeState& BakedState = StateTree->States[StateIdx];
	BakedState.Name = State.Name;
	BakedState.Parent = Parent;

	SourceStates.Add(&State);
	IDToState.Add(State.ID, StateIdx);

	// Collect evaluators
	check(StateTree->RuntimeStorageItems.Num() <= int32(MAX_uint16));
	BakedState.EvaluatorsBegin = uint16(StateTree->RuntimeStorageItems.Num());

	for (FStateTreeEvaluatorItem& EvaluatorItem : State.Evaluators)
	{
		if (EvaluatorItem.Type.IsValid())
		{
			// Copy the evaluator
			FInstancedStruct& EvalPtr = StateTree->RuntimeStorageItems.AddDefaulted_GetRef();
			EvalPtr = EvaluatorItem.Type;
			check(EvalPtr.IsValid());
			FStateTreeEvaluatorBase& Eval = EvalPtr.GetMutable<FStateTreeEvaluatorBase>();

			// Create binding source struct descriptor.
			FStateTreeBindableStructDesc StructDesc;
			StructDesc.Struct = EvalPtr.GetScriptStruct();
			StructDesc.Name = Eval.Name;
			StructDesc.ID = Eval.ID;

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
			Eval.BindingsBatch = BatchIndex == INDEX_NONE ? FStateTreeHandle::Invalid : FStateTreeHandle(uint16(BatchIndex));
			check(SourceStructIndex <= int32(MAX_uint16));
			Eval.SourceStructIndex = uint16(SourceStructIndex);

			if (!CreateExternalItemHandles(EvalPtr))
			{
				UE_LOG(LogStateTree, Error, TEXT("%s: '%s': %s failed to create external item handles."), ANSI_TO_TCHAR(__FUNCTION__), *GetNameSafe(StateTree), *Eval.Name.ToString());
				return false;
			}
		}
	}
	const int32 EvaluatorsNum = StateTree->RuntimeStorageItems.Num() - int32(BakedState.EvaluatorsBegin);
	check(EvaluatorsNum <= int32(MAX_uint8));
	BakedState.EvaluatorsNum = uint8(EvaluatorsNum);

	// Collect tasks
	check(StateTree->RuntimeStorageItems.Num() <= int32(MAX_uint16));
	BakedState.TasksBegin = uint16(StateTree->RuntimeStorageItems.Num());

	for (FStateTreeTaskItem& TaskItem : State.Tasks)
	{
		if (TaskItem.Type.IsValid())
		{
			// Copy the task
			FInstancedStruct& TaskPtr = StateTree->RuntimeStorageItems.AddDefaulted_GetRef();
			TaskPtr = TaskItem.Type;
			check(TaskPtr.IsValid());
			FStateTreeTaskBase& Task = TaskPtr.GetMutable<FStateTreeTaskBase>();

			// Create binding source struct descriptor.
			FStateTreeBindableStructDesc StructDesc;
			StructDesc.Struct = TaskPtr.GetScriptStruct();
			StructDesc.Name = Task.Name;
			StructDesc.ID = Task.ID;

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
			Task.BindingsBatch = BatchIndex == INDEX_NONE ? FStateTreeHandle::Invalid : FStateTreeHandle(uint16(BatchIndex));
			check(SourceStructIndex <= int32(MAX_uint16));
			Task.SourceStructIndex = uint16(SourceStructIndex);

			if (!CreateExternalItemHandles(TaskPtr))
			{
				UE_LOG(LogStateTree, Error, TEXT("%s: '%s': %s failed to create external item handles."), ANSI_TO_TCHAR(__FUNCTION__), *GetNameSafe(StateTree), *Task.Name.ToString());
				return false;
			}
		}
	}
	const int32 TasksNum = StateTree->RuntimeStorageItems.Num() - int32(BakedState.TasksBegin);
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
