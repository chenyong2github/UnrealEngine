// Copyright Epic Games, Inc. All Rights Reserved.

#include "WorldConditionQuery.h"
#include "WorldConditionContext.h"
#include "UObject/UObjectThreadContext.h"
#include "Serialization/CustomVersion.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(WorldConditionQuery)

struct FWorldConditionCustomVersion
{
	enum Type
	{
		// Before any version changes were made in the plugin
		BeforeCustomVersionWasAdded = 0,
		// Changed shared definition to a struct.
		StructSharedDefinition = 1,
		// -----<new versions can be added above this line>-------------------------------------------------
		VersionPlusOne,
		LatestVersion = VersionPlusOne - 1
	};
	// The GUID for this custom version number
	const static FGuid GUID;
private:
	FWorldConditionCustomVersion() {}
};

const FGuid FWorldConditionCustomVersion::GUID(0x2C28AC22, 0x15CF46FE, 0xBD19F011, 0x652A3C05);

// Register the custom version with core
FCustomVersionRegistration GWorldConditionCustomVersion(FWorldConditionCustomVersion::GUID, FWorldConditionCustomVersion::LatestVersion, TEXT("WorldConditionCustomVersion"));


//
// FWorldConditionResultInvalidationHandle
//

void FWorldConditionResultInvalidationHandle::InvalidateResult() const
{
	if (CachedResult && Item)
	{
		*CachedResult = EWorldConditionResultValue::Invalid;
		Item->CachedResult = EWorldConditionResultValue::Invalid;
	}
}


//
// FWorldConditionQueryState
//

FWorldConditionQueryState::~FWorldConditionQueryState()
{
	// Usually the game code should call Deactivate() as part of the uninitialization of the owner of the query state.
	// Try to clean up the best we can in case things are still running.
	if (IsInitialized() && SharedDefinition.IsValid())
	{
		if (IsValid(Owner))
		{
			// We can call Deactivate(), but we dont know the context data, so some uninitialization can potentially be incomplete.
			UE_LOG(LogWorldCondition, Warning, TEXT("World Condition: State %p owned by %s is still active on destructor, calling Deactivate() without context data, might leak memory or resources."), this, *GetFullNameSafe(Owner));
			const FWorldConditionContextData ContextData(*SharedDefinition->GetSchemaClass().GetDefaultObject());
			const FWorldConditionContext Context(*this, ContextData);
			Context.Deactivate();
		}
		else
		{
			// The owner is not valid, os we cannot call Deactivate(), the best we can do is to clean up the memory properly, as the shared definition is valid. 
			UE_LOG(LogWorldCondition, Warning, TEXT("World Condition: State %p owned by %s is still active on destructor, failed to call Deactivate() due to invalid owner, calling Free(), might leak memory or resources."), this, *GetFullNameSafe(Owner));
			Free();
		}
	}

	if (Memory)
	{
		// Something went very wrong, this should never happen.
		UE_LOG(LogWorldCondition, Error, TEXT("World Condition: State %p has still allocated memory in destructor, might leak memory."), Memory);
		FMemory::Free(Memory);
		Memory = nullptr;
	}
}
	
void FWorldConditionQueryState::Initialize(const UObject& InOwner, const FWorldConditionQueryDefinition& QueryDefinition)
{
	InitializeInternal(&InOwner, QueryDefinition.GetSharedDefinitionPtr());
}

void FWorldConditionQueryState::InitializeInternal(const UObject* InOwner, const TSharedPtr<FWorldConditionQuerySharedDefinition>& InSharedDefinition)
{
	if (IsInitialized())
	{
		Free();
	}

	Owner = InOwner;

	if (!InSharedDefinition.IsValid()
		|| InSharedDefinition->GetStateSize() == 0)
	{
		// Empty condition
		SharedDefinition = nullptr;
		NumConditions = 0;
		bIsInitialized = true;
		return;
	}

	SharedDefinition = InSharedDefinition;

	const FInstancedStructContainer& Conditions = SharedDefinition->GetConditions(); 

	// Cache num conditions so that we can access the items and cached data without touching the definition.
	NumConditions = IntCastChecked<uint8>(Conditions.Num());
	
	Memory = static_cast<uint8*>(FMemory::Malloc(SharedDefinition->GetStateSize(), SharedDefinition->GetStateMinAlignment()));

	// Init cached result
	EWorldConditionResultValue& CachedResult = *reinterpret_cast<EWorldConditionResultValue*>(Memory + CachedResultOffset);
	CachedResult = EWorldConditionResultValue::Invalid;
	
	// Initialize items
	for (int32 Index = 0; Index < static_cast<int32>(NumConditions); Index++)
	{
		new (Memory + ItemsOffset + sizeof(FWorldConditionItem) * Index) FWorldConditionItem();
	}

	// Initialize state
	for (int32 Index = 0; Index < static_cast<int32>(NumConditions); Index++)
	{
		const FWorldConditionBase& Condition = Conditions[Index].Get<const FWorldConditionBase>();
		if (Condition.GetStateDataOffset() == 0)
		{
			continue;
		}
		uint8* StateMemory = Memory + Condition.GetStateDataOffset();
		if (Condition.IsStateObject())
		{
			new (StateMemory) FWorldConditionStateObject();
			FWorldConditionStateObject& StateObject = *reinterpret_cast<FWorldConditionStateObject*>(StateMemory);
			const UClass* StateClass = Cast<UClass>(Condition.GetRuntimeStateType());
			StateObject.Object = NewObject<UObject>(const_cast<UObject*>(Owner.Get()), StateClass);  
		}
		else
		{
			const UScriptStruct* StateScriptStruct = Cast<UScriptStruct>(Condition.GetRuntimeStateType());
			StateScriptStruct->InitializeStruct(StateMemory);
		}
	}

	bIsInitialized = true;
}

void FWorldConditionQueryState::Free()
{
	if (Memory == nullptr)
	{
		NumConditions = 0;
		SharedDefinition = nullptr;
		bIsInitialized = false;
		return;
	}

	check(SharedDefinition.IsValid());

	// Items don't need destructing.

	// Destroy state
	const FInstancedStructContainer& Conditions = SharedDefinition->GetConditions();
	for (int32 Index = 0; Index < static_cast<int32>(NumConditions); Index++)
	{
		const FWorldConditionBase& Condition = Conditions[Index].Get<const FWorldConditionBase>();
		if (Condition.GetStateDataOffset() == 0)
		{
			continue;
		}
		uint8* StateMemory = Memory + Condition.GetStateDataOffset();
		if (Condition.IsStateObject())
		{
			FWorldConditionStateObject& StateObject = *reinterpret_cast<FWorldConditionStateObject*>(Memory + Condition.GetStateDataOffset());
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
	SharedDefinition = nullptr;
	bIsInitialized = false;
}

void FWorldConditionQueryState::AddStructReferencedObjects(class FReferenceCollector& Collector)
{
	Collector.AddReferencedObject(Owner);

	if (SharedDefinition.IsValid())
	{
		Collector.AddPropertyReferencesWithStructARO(FWorldConditionQuerySharedDefinition::StaticStruct(), SharedDefinition.Get());
	}
	
	if (Memory == nullptr || !SharedDefinition.IsValid())
	{
		return;
	}

	const FInstancedStructContainer& Conditions = SharedDefinition->GetConditions();
	
	check(NumConditions == Conditions.Num());

	for (int32 Index = 0, Num = Conditions.Num(); Index < Num; Index++)
	{
		const FWorldConditionBase& Condition = Conditions[Index].Get<const FWorldConditionBase>();
		if (Condition.GetStateDataOffset() == 0)
		{
			continue;
		}
		
		uint8* StateMemory = Memory + Condition.GetStateDataOffset();
		if (Condition.IsStateObject())
		{
			FWorldConditionStateObject& StateObject = *reinterpret_cast<FWorldConditionStateObject*>(StateMemory);
			Collector.AddReferencedObject(StateObject.Object);
		}
		else
		{
			if (const UScriptStruct* StateScriptStruct = Cast<UScriptStruct>(Condition.GetRuntimeStateType()))
			{
				Collector.AddReferencedObject(StateScriptStruct, Owner);
				Collector.AddPropertyReferencesWithStructARO(StateScriptStruct, StateMemory, Owner);
			}
		}
	}
}

FWorldConditionResultInvalidationHandle FWorldConditionQueryState::GetInvalidationHandle(const FWorldConditionBase& Condition) const
{
	check(bIsInitialized);
	check(Memory && Condition.GetConditionIndex() < NumConditions);

	EWorldConditionResultValue* CachedResult = reinterpret_cast<EWorldConditionResultValue*>(Memory + CachedResultOffset);
	FWorldConditionItem* Item = reinterpret_cast<FWorldConditionItem*>(Memory + ItemsOffset + Condition.GetConditionIndex() * sizeof(FWorldConditionItem));

	return FWorldConditionResultInvalidationHandle(CachedResult, Item);
}


//
// UWorldConditionQuerySharedDefinition
//

void FWorldConditionQuerySharedDefinition::Set(const TSubclassOf<UWorldConditionSchema> InSchema, const TArrayView<FConstStructView> InConditions)
{
	SchemaClass = InSchema;
	Conditions = InConditions;
	StateMinAlignment = 8;
	StateSize = 0;
}

void FWorldConditionQuerySharedDefinition::Set(const TSubclassOf<UWorldConditionSchema> InSchema, const TArrayView<FStructView> InConditions)
{
	SchemaClass = InSchema;
	Conditions = InConditions;
	StateMinAlignment = 8;
	StateSize = 0;
}

void FWorldConditionQuerySharedDefinition::PostSerialize(const FArchive& Ar)
{
	const FUObjectSerializeContext* LoadContext = FUObjectThreadContext::Get().GetSerializeContext();
	const UObject* SerializedObject = LoadContext ? LoadContext->SerializedObject : nullptr;
	
	if (!Link())
	{
		UE_LOG(LogWorldCondition, Error, TEXT("World Condition: Failed to link query for %s."),
			*GetNameSafe(SerializedObject));
	}
}

bool FWorldConditionQuerySharedDefinition::Link()
{
	bool bResult = true;

	StateMinAlignment = 0;
	StateSize = 0;
	bIsLinked = false;

	const UWorldConditionSchema* Schema = SchemaClass.GetDefaultObject();
	if (!Schema)
	{
		return false;
	}

	// Calculate layout
	int32 MinAlignment = 8;
	int32 Offset = 0;

	// Reserve space for cached result
	Offset += sizeof(EWorldConditionResultValue);
	
	// Reserve space for condition items.
	Offset = Align(Offset, alignof(FWorldConditionItem));
	check(Offset == FWorldConditionQueryState::ItemsOffset);
	Offset += sizeof(FWorldConditionItem) * Conditions.Num(); 

	// Reserve space for all runtime data.
	for (int32 Index = 0; Index < Conditions.Num(); Index++)
	{
		FWorldConditionBase& Condition = Conditions[Index].Get<FWorldConditionBase>();
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
		}
		else
		{
			Condition.StateDataOffset = 0;
		}
	}

	StateMinAlignment = uint8(MinAlignment);
	StateSize = uint16(Offset);
	
	for (int32 Index = 0; Index < Conditions.Num(); Index++)
	{
		FWorldConditionBase& Condition = Conditions[Index].Get<FWorldConditionBase>();
		bResult &= Condition.Initialize(*Schema);
	}

	bIsLinked = bResult;

	return bResult;
}

//
// FWorldConditionQueryDefinition
//

bool FWorldConditionQueryDefinition::IsValid() const
{
	return SharedDefinition.IsValid() && SharedDefinition->IsLinked();
}

void FWorldConditionQueryDefinition::SetSchemaClass(const TSubclassOf<UWorldConditionSchema> InSchema)
{
	SchemaClass = InSchema;
}

#if WITH_EDITORONLY_DATA
bool FWorldConditionQueryDefinition::Initialize(const UObject* Outer, const TSubclassOf<UWorldConditionSchema> InSchemaClass, const TConstArrayView<FWorldConditionEditable> InConditions)
{
	SchemaClass = InSchemaClass;
	EditableConditions = InConditions;
	return Initialize(Outer);
}
#endif

bool FWorldConditionQueryDefinition::Initialize(const UObject* Outer)
{
	bool bResult = true;

#if WITH_EDITORONLY_DATA
	TSharedPtr<FWorldConditionQuerySharedDefinition> OldSharedDefinition = SharedDefinition;
	SharedDefinition = nullptr;

	if (!::IsValid(SchemaClass))
	{
		UE_LOG(LogWorldCondition, Warning, TEXT("World Condition: Failed to initialize query for %s due to missing schema."), *GetFullNameSafe(Outer));
		return false;
	}

	const UWorldConditionSchema* Schema = SchemaClass.GetDefaultObject();
	
	// Append only valid condition.
	TArray<FStructView> ValidConditions;
	ValidConditions.Reserve(EditableConditions.Num());
	for (FWorldConditionEditable& EditableCondition : EditableConditions)
	{
		if (EditableCondition.Condition.IsValid())
		{
			if (Schema->IsStructAllowed(EditableCondition.Condition.GetScriptStruct()))
			{
				FWorldConditionBase& Condition = EditableCondition.Condition.GetMutable<FWorldConditionBase>();
				// Store expression depth temporarily into NextExpressionDepth, it will be update below.
				Condition.NextExpressionDepth = EditableCondition.ExpressionDepth;
				Condition.Operator = EditableCondition.Operator;
				Condition.bInvert = EditableCondition.bInvert;

				ValidConditions.Add(EditableCondition.Condition);
			}
			else
			{
				UE_LOG(LogWorldCondition, Warning, TEXT("World Condition: Query for %s contains condition of type %s that is not allowed by schema %s."),
					*GetFullNameSafe(Outer), *GetNameSafe(EditableCondition.Condition.GetScriptStruct()), *GetNameSafe(Schema));
			}
		}
	}
	
	if (ValidConditions.IsEmpty())
	{
		// Empty query, do not create definition.
		return true;
	}

	
	// Prepare the conditions for evaluation.
	if (ValidConditions.Num() > 0)
	{
		FWorldConditionBase& Condition = ValidConditions[0].Get<FWorldConditionBase>();
		Condition.Operator = EWorldConditionOperator::Copy;
	}

	for (int32 Index = 0; Index < ValidConditions.Num(); Index++)
	{
		uint8 NextExpressionDepth = 0;
		if ((Index + 1) < ValidConditions.Num())
		{
			const FWorldConditionBase& NextCondition = ValidConditions[Index + 1].Get<FWorldConditionBase>();
			NextExpressionDepth = NextCondition.NextExpressionDepth;
		}
		
		FWorldConditionBase& Condition = ValidConditions[Index].Get<FWorldConditionBase>();
		Condition.NextExpressionDepth = NextExpressionDepth;

		Condition.ConditionIndex = Index;
	}

	// Create a new shared definition to allow the allocated states to deactivate properly event if we update the definition.
	SharedDefinition = MakeShared<FWorldConditionQuerySharedDefinition>();
	SharedDefinition->Set(SchemaClass, ValidConditions);
	SharedDefinition->Link();

#endif
	
	return bResult;
}

bool FWorldConditionQueryDefinition::Serialize(FArchive& Ar)
{
	Ar.UsingCustomVersion(FWorldConditionCustomVersion::GUID);

	// Use default serialization for most properties for
	// archive configurations supported by UStruct::SerializeVersionedTaggedProperties 
	if (Ar.IsLoading() || Ar.IsSaving() || Ar.IsCountingMemory() || Ar.IsObjectReferenceCollector())
	{
		StaticStruct()->SerializeTaggedProperties(Ar, (uint8*)this, StaticStruct(), nullptr);
	}

	if (Ar.CustomVer(FWorldConditionCustomVersion::GUID) >= FWorldConditionCustomVersion::StructSharedDefinition)
	{
		// Serialize shared definition
		bool bHasSharedDefinition = SharedDefinition.IsValid();
		Ar << bHasSharedDefinition;
		
		if (Ar.IsLoading())
		{
			if (bHasSharedDefinition)
			{
				SharedDefinition = MakeShared<FWorldConditionQuerySharedDefinition>();
			}
			else
			{
				SharedDefinition = nullptr;
			}
		}
		
		if (SharedDefinition.IsValid()
			&& (Ar.IsLoading() || Ar.IsSaving() || Ar.IsCountingMemory() || Ar.IsObjectReferenceCollector()))
		{
			UScriptStruct* Struct = TBaseStructure<FWorldConditionQuerySharedDefinition>::Get();
			Struct->SerializeTaggedProperties(Ar, (uint8*)SharedDefinition.Get(), Struct, nullptr);
			// SerializeTaggedProperties does not call PostSerialize() on the struct it's called (calls in items), call it manually.
			SharedDefinition->PostSerialize(Ar);
		}
	}
#if WITH_EDITOR
	if (Ar.IsLoading()
		|| (Ar.IsSaving() && Ar.IsPersistent()))
	{
 
		// If not initialized yet, but has data, initialize on load in editor.
		if (!SharedDefinition.IsValid() && EditableConditions.Num() > 0)
		{
			const FUObjectSerializeContext* LoadContext = FUObjectThreadContext::Get().GetSerializeContext();
			const UObject* Outer = LoadContext ? LoadContext->SerializedObject : nullptr;
			Initialize(Outer);
		}
	}
#endif
	
	return true;
}

bool FWorldConditionQueryDefinition::ImportTextItem(const TCHAR*& Buffer, int32 PortFlags, UObject* Parent, FOutputDevice* ErrorText, FArchive* InSerializingArchive)
{
	if (const TCHAR* Result = TBaseStructure<FWorldConditionQueryDefinition>::Get()->ImportText(Buffer, this, Parent, PortFlags, ErrorText, TEXT("FWorldConditionQueryDefinition"), /*bAllowNativeOverride*/false))
	{
		Initialize(Parent);
		Buffer = Result;
		return true;
	}
	
	return false;
}

void FWorldConditionQueryDefinition::AddStructReferencedObjects(FReferenceCollector& Collector) const
{
	if (SharedDefinition.IsValid())
	{
		Collector.AddPropertyReferencesWithStructARO(FWorldConditionQuerySharedDefinition::StaticStruct(), SharedDefinition.Get());
	}
}


//
// FWorldConditionQuery
//

#if WITH_EDITORONLY_DATA
bool FWorldConditionQuery::DebugInitialize(const UObject* Outer, const TSubclassOf<UWorldConditionSchema> InSchemaClass, const TConstArrayView<FWorldConditionEditable> InConditions)
{
	if (IsActive())
	{
		return false;
	}

	return QueryDefinition.Initialize(Outer, InSchemaClass, InConditions);
}
#endif // WITH_EDITORONLY_DATA

bool FWorldConditionQuery::Activate(const UObject& InOwner, const FWorldConditionContextData& ContextData) const
{
	QueryState.Initialize(InOwner, QueryDefinition);
	if (!QueryState.IsInitialized())
	{
		return false;
	}

	const FWorldConditionContext Context(QueryState, ContextData);
	return Context.Activate();
}

bool FWorldConditionQuery::IsTrue(const FWorldConditionContextData& ContextData) const
{
	const FWorldConditionContext Context(QueryState, ContextData);
	return Context.IsTrue();
}

void FWorldConditionQuery::Deactivate(const FWorldConditionContextData& ContextData) const
{
	const FWorldConditionContext Context(QueryState, ContextData);
	return Context.Deactivate();
}

bool FWorldConditionQuery::IsActive() const
{
	return QueryState.IsInitialized();
}
