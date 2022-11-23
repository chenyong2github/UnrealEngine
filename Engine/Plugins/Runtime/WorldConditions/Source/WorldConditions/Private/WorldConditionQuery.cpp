// Copyright Epic Games, Inc. All Rights Reserved.

#include "WorldConditionQuery.h"
#include "WorldConditionContext.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(WorldConditionQuery)

FWorldConditionQueryState::~FWorldConditionQueryState()
{
	if (Memory)
	{
		UE_LOG(LogWorldCondition, Error, TEXT("Expected World Condition state %p to have been freed, might leak memory."), Memory);
		FMemory::Free(Memory);
		Memory = nullptr;
	}
}
	
void FWorldConditionQueryState::Initialize(const UObject& Owner, const FWorldConditionQueryDefinition& QueryDefinition)
{
	if (IsInitialized())
	{
		Free(QueryDefinition);
	}

	if (!QueryDefinition.IsValid())
	{
		bIsInitialized = true;
		return;
	}

	NumConditions = IntCastChecked<uint8>(QueryDefinition.Conditions.Num());

	int32 MinAlignment = 8;
	int32 Offset = 0;
	
	// Reserve space for condition items.
	Offset += sizeof(FWorldConditionItem) * static_cast<int32>(NumConditions); 

	bHasPerConditionState = false;
	
	// Reserve space for all runtime data.
	for (int32 Index = 0; Index < static_cast<int32>(NumConditions); Index++)
	{
		FWorldConditionBase& Condition = QueryDefinition.Conditions[Index].GetMutable<FWorldConditionBase>();
		if (const UStruct* StateStruct = Condition.GetRuntimeStateType())
		{
			int32 StructMinAlignment = 0;
			int32 StructSize = 0;

			if (const UScriptStruct* ScriptStruct = Cast<const UScriptStruct>(StateStruct))
			{
				StructMinAlignment = ScriptStruct->GetMinAlignment();
				StructSize = ScriptStruct->GetStructureSize();
				Condition.bIsStateObject = false;
			}
			else if (const UClass* Class = Cast<const UClass>(StateStruct))
			{
				StructMinAlignment = FWorldConditionStateObject::StaticStruct()->GetMinAlignment();
				StructSize = FWorldConditionStateObject::StaticStruct()->GetStructureSize();
				Condition.bIsStateObject = true;
			}
			
			check(StructMinAlignment > 0 && StructSize > 0);

			Offset = Align(Offset, StructMinAlignment);
			Condition.StateDataOffset = IntCastChecked<uint16>(Offset);

			Offset += StructSize;
			MinAlignment = FMath::Max(MinAlignment, StructMinAlignment);

			bHasPerConditionState = true;
		}
		else
		{
			Condition.StateDataOffset = 0;
		}
	}

	const int32 TotalSize = Offset;
	Memory = (uint8*)FMemory::Malloc(TotalSize, MinAlignment);

	// Initialize items
	for (int32 Index = 0; Index < static_cast<int32>(NumConditions); Index++)
	{
		new (Memory + sizeof(FWorldConditionItem) * Index) FWorldConditionItem();
	}

	// Initialize state
	for (int32 Index = 0; Index < static_cast<int32>(NumConditions); Index++)
	{
		FWorldConditionBase& Condition = QueryDefinition.Conditions[Index].GetMutable<FWorldConditionBase>();
		if (Condition.StateDataOffset == 0)
		{
			continue;
		}
		uint8* StateMemory = Memory + Condition.StateDataOffset;
		if (Condition.bIsStateObject)
		{
			new (StateMemory) FWorldConditionStateObject();
			FWorldConditionStateObject& StateObject = *reinterpret_cast<FWorldConditionStateObject*>(StateMemory);
			const UClass* StateClass = Cast<UClass>(Condition.GetRuntimeStateType());
			StateObject.Object = NewObject<UObject>(const_cast<UObject*>(&Owner), StateClass);  
		}
		else
		{
			const UScriptStruct* StateScriptStruct = Cast<UScriptStruct>(Condition.GetRuntimeStateType());
			StateScriptStruct->InitializeStruct(StateMemory);
		}
	}

	bIsInitialized = true;
}

void FWorldConditionQueryState::Free(const FWorldConditionQueryDefinition& QueryDefinition)
{
	if (Memory == nullptr)
	{
		NumConditions = 0;
		CachedResult = EWorldConditionResult::Invalid;
		bHasPerConditionState = false;
		bIsInitialized = false;
		return;
	}
	
	// Items don't need destructing.

	// Destroy state
	for (int32 Index = 0; Index < static_cast<int32>(NumConditions); Index++)
	{
		FWorldConditionBase& Condition = QueryDefinition.Conditions[Index].GetMutable<FWorldConditionBase>();
		if (Condition.StateDataOffset == 0)
		{
			continue;
		}
		uint8* StateMemory = Memory + Condition.StateDataOffset;
		if (Condition.bIsStateObject)
		{
			FWorldConditionStateObject& StateObject = *reinterpret_cast<FWorldConditionStateObject*>(Memory + Condition.StateDataOffset);
			StateObject.~FWorldConditionStateObject();
		}
		else
		{
			const UScriptStruct* StateScriptStruct = Cast<UScriptStruct>(Condition.GetRuntimeStateType());
			StateScriptStruct->DestroyStruct(StateMemory);
		}
	}

	FMemory::Free(Memory);
	Memory = nullptr;
	NumConditions = 0;
	CachedResult = EWorldConditionResult::Invalid;
	bHasPerConditionState = false;
	bIsInitialized = false;
}

void FWorldConditionQueryState::AddReferencedObjects(const FWorldConditionQueryDefinition& QueryDefinition, class FReferenceCollector& Collector) const
{
	if (Memory == nullptr)
	{
		return;
	}
	
	check(NumConditions == QueryDefinition.Conditions.Num());
	
	for (int32 Index = 0; Index < QueryDefinition.Conditions.Num(); Index++)
	{
		FWorldConditionBase& Condition = QueryDefinition.Conditions[Index].GetMutable<FWorldConditionBase>();
		if (Condition.StateDataOffset == 0)
		{
			continue;
		}
		uint8* StateMemory = Memory + Condition.StateDataOffset;
		if (Condition.bIsStateObject)
		{
			FWorldConditionStateObject& StateObject = *reinterpret_cast<FWorldConditionStateObject*>(Memory + Condition.StateDataOffset);
			Collector.AddReferencedObject(StateObject.Object);
		}
		else
		{
			const UScriptStruct* StateScriptStruct = Cast<UScriptStruct>(Condition.GetRuntimeStateType());
			Collector.AddReferencedObjects(StateScriptStruct, StateMemory);
		}
	}
	
}



//
// FWorldConditionQueryDefinition
//

bool FWorldConditionQueryDefinition::IsValid() const
{
	return SchemaClass && Conditions.Num() > 0;
}

bool FWorldConditionQueryDefinition::Initialize()
{
	bool bResult = true;
#if WITH_EDITORONLY_DATA
	Conditions.Reset();

	if (SchemaClass == nullptr)
	{
		return false;
	}

	const UWorldConditionSchema* Schema = SchemaClass.GetDefaultObject();
	
	// Append only valid condition.
	TArray<FConstStructView> ValidConditions;
	ValidConditions.Reserve(EditableConditions.Num());
	for (const FWorldConditionEditable& EditableCondition : EditableConditions)
	{
		if (EditableCondition.Condition.IsValid())
		{
			if (Schema->IsStructAllowed(EditableCondition.Condition.GetScriptStruct()))
			{
				FWorldConditionBase& Condition = EditableCondition.Condition.GetMutable<FWorldConditionBase>();
				// Store expression depth temporarily into NextExpressionDepth, it will be update below.
				Condition.NextExpressionDepth = EditableCondition.ExpressionDepth;
				Condition.Operator = EditableCondition.Operator;

				ValidConditions.Add(EditableCondition.Condition);
			}
			else
			{
				UE_LOG(LogWorldCondition, Warning, TEXT("World condition contains condition of type %s that is not allowed by schema %s."),
					*GetNameSafe(EditableCondition.Condition.GetScriptStruct()), *GetNameSafe(Schema));
			}
		}
	}
	if (ValidConditions.IsEmpty())
	{
		return true;
	}
	
	Conditions.Append(ValidConditions);

	if (Conditions.Num() > 0)
	{
		FWorldConditionBase& Condition = Conditions[0].GetMutable<FWorldConditionBase>();
		Condition.Operator = EWorldConditionOperator::Copy;
	}

	for (int32 Index = 0; Index < Conditions.Num(); Index++)
	{
		uint8 NextExpressionDepth = 0;
		if ((Index + 1) < Conditions.Num())
		{
			const FWorldConditionBase& NextCondition = Conditions[Index + 1].GetMutable<FWorldConditionBase>();
			NextExpressionDepth = NextCondition.NextExpressionDepth;
		}
		
		FWorldConditionBase& Condition = Conditions[Index].GetMutable<FWorldConditionBase>();
		Condition.NextExpressionDepth = NextExpressionDepth;

		Condition.ConditionIndex = Index;
	}

	for (int32 Index = 0; Index < Conditions.Num(); Index++)
	{
		FWorldConditionBase& Condition = Conditions[Index].GetMutable<FWorldConditionBase>();
		bResult &= Condition.Initialize(*Schema);
	}
#endif
	
	return bResult;
}

void FWorldConditionQueryDefinition::PostSerialize(const FArchive& Ar)
{
	if (Ar.IsLoading())
	{
#if WITH_EDITOR
		// Initialize on load in editor.
		if (Conditions.Num() == 0 && EditableConditions.Num() > 0)
		{
			Initialize();
		}
#endif
	}
}


//
// FWorldConditionQuery
//

FWorldConditionQuery::~FWorldConditionQuery()
{
	if (IsActive())
	{
		UE_LOG(LogWorldCondition, Error, TEXT("Active World Condition %p is still active on destructor, calling Deactivate() without context data, might leak memory."), this);
		check(QueryDefinition.SchemaClass);
		const FWorldConditionContextData ContextData(*QueryDefinition.SchemaClass.GetDefaultObject());
		Deactivate(ContextData);
	}
}

bool FWorldConditionQuery::DebugInitialize(const TSubclassOf<UWorldConditionSchema> InSchemaClass, const TConstArrayView<FWorldConditionEditable> InConditions)
{
	if (IsActive())
	{
		return false;
	}

	QueryDefinition.SchemaClass = InSchemaClass;
#if WITH_EDITORONLY_DATA
	QueryDefinition.EditableConditions = InConditions;
#endif
	return QueryDefinition.Initialize();
}

bool FWorldConditionQuery::Activate(const UObject& InOwner, const FWorldConditionContextData& ContextData)
{
	Owner = &InOwner;

	QueryState.Initialize(*Owner, QueryDefinition);
	check(QueryState.GetNumConditions() == QueryDefinition.Conditions.Num());

	const FWorldConditionContext Context(*Owner, QueryDefinition, QueryState, ContextData);
	return Context.Activate();
}

bool FWorldConditionQuery::IsTrue(const FWorldConditionContextData& ContextData) const
{
	const FWorldConditionContext Context(*Owner, QueryDefinition, QueryState, ContextData);
	return Context.IsTrue();
}

void FWorldConditionQuery::Deactivate(const FWorldConditionContextData& ContextData) const
{
	const FWorldConditionContext Context(*Owner, QueryDefinition, QueryState, ContextData);
	return Context.Deactivate();
}

bool FWorldConditionQuery::IsActive() const
{
	return QueryState.IsInitialized();
}

void FWorldConditionQuery::AddStructReferencedObjects(class FReferenceCollector& Collector) const
{
	if (QueryState.IsInitialized())
	{
		QueryState.AddReferencedObjects(QueryDefinition, Collector);
	}
}
