// Copyright Epic Games, Inc. All Rights Reserved.

#include "StructSerializer.h"
#include "UObject/UnrealType.h"
#include "IStructSerializerBackend.h"


/* Internal helpers
 *****************************************************************************/

namespace StructSerializer
{
	/**
	 * Gets the value from the given property.
	 *
	 * @param PropertyType The type name of the property.
	 * @param State The stack state that holds the property's data.
	 * @param Property A pointer to the property.
	 * @return A pointer to the property's value, or nullptr if it couldn't be found.
	 */
	template<typename FPropertyType, typename PropertyType>
	PropertyType* GetPropertyValue( const FStructSerializerState& State, FProperty* Property )
	{
		PropertyType* ValuePtr = nullptr;
		FArrayProperty* ArrayProperty = CastField<FArrayProperty>(State.ValueProperty);

		if (ArrayProperty)
		{
			check(ArrayProperty->Inner == Property);

			FScriptArrayHelper ArrayHelper(ArrayProperty, ArrayProperty->template ContainerPtrToValuePtr<void>(State.ValueData));
			int32 Index = ArrayHelper.AddValue();
		
			ValuePtr = (PropertyType*)ArrayHelper.GetRawPtr( Index );
		}
		else
		{
			FPropertyType* TypedProperty = CastField<FPropertyType>(Property);
			check(TypedProperty != nullptr);

			ValuePtr = TypedProperty->template ContainerPtrToValuePtr<PropertyType>(State.ValueData);
		}

		return ValuePtr;
	}
}


/* FStructSerializer static interface
 *****************************************************************************/

void FStructSerializer::Serialize(const void* Struct, UStruct& TypeInfo, IStructSerializerBackend& Backend, const FStructSerializerPolicies& Policies)
{
	using namespace StructSerializer;

	check(Struct != nullptr);

	// initialize serialization
	TArray<FStructSerializerState> StateStack;
	{
		FStructSerializerState NewState;
		{
			NewState.HasBeenProcessed = false;
			NewState.KeyData = nullptr;
			NewState.KeyProperty = nullptr;
			NewState.ValueData = Struct;
			NewState.ValueProperty = nullptr;
			NewState.ValueType = &TypeInfo;
		}

		StateStack.Push(NewState);
	}

	// process state stack
	while (StateStack.Num() > 0)
	{
		FStructSerializerState CurrentState = StateStack.Pop(/*bAllowShrinking=*/ false);

		// structures
		if ((CurrentState.ValueProperty == nullptr) || CastField<FStructProperty>(CurrentState.ValueProperty))
		{
			if (!CurrentState.HasBeenProcessed)
			{
				const void* ValueData = CurrentState.ValueData;

				// write object start
				if (CurrentState.ValueProperty != nullptr)
				{
					FFieldVariant Outer = CurrentState.ValueProperty->GetOwnerVariant();

					if ((Outer.ToField() == nullptr) || (Outer.ToField()->GetClass() != FArrayProperty::StaticClass()))
					{
						ValueData = CurrentState.ValueProperty->ContainerPtrToValuePtr<void>(CurrentState.ValueData);
					}
				}

				Backend.BeginStructure(CurrentState);

				CurrentState.HasBeenProcessed = true;
				StateStack.Push(CurrentState);

				// serialize fields
				if (CurrentState.ValueProperty != nullptr)
				{
					FStructProperty* StructProperty = CastField<FStructProperty>(CurrentState.ValueProperty);

					if (StructProperty != nullptr)
					{
						CurrentState.ValueType = StructProperty->Struct;
					}
					else
					{
						FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(CurrentState.ValueProperty);

						if (ObjectProperty != nullptr)
						{
							CurrentState.ValueType = ObjectProperty->PropertyClass;
						}
					}
				}

				TArray<FStructSerializerState> NewStates;

				if (CurrentState.ValueType)
				{
					for (TFieldIterator<FProperty> It(CurrentState.ValueType, EFieldIteratorFlags::IncludeSuper); It; ++It)
					{
						// Skip property if the filter function is set and rejects it.
						if (Policies.PropertyFilter && !Policies.PropertyFilter(*It, CurrentState.ValueProperty))
						{
							continue;
						}

						FStructSerializerState NewState;
						{
							NewState.HasBeenProcessed = false;
							NewState.KeyData = nullptr;
							NewState.KeyProperty = nullptr;
							NewState.ValueData = ValueData;
							NewState.ValueProperty = *It;
							NewState.ValueType = nullptr;
							NewState.FieldType = It->GetClass();
						}

						NewStates.Add(NewState);
					}
				}

				// push child properties on stack (in reverse order)
				for (int32 Index = NewStates.Num() - 1; Index >= 0; --Index)
				{
					StateStack.Push(NewStates[Index]);
				}
			}
			else
			{
				Backend.EndStructure(CurrentState);
			}
		}

		// dynamic arrays
		else if (CastField<FArrayProperty>(CurrentState.ValueProperty))
		{
			if (!CurrentState.HasBeenProcessed)
			{
				Backend.BeginArray(CurrentState);

				CurrentState.HasBeenProcessed = true;
				StateStack.Push(CurrentState);

				FArrayProperty* ArrayProperty = CastField<FArrayProperty>(CurrentState.ValueProperty);
				FScriptArrayHelper ArrayHelper(ArrayProperty, ArrayProperty->ContainerPtrToValuePtr<void>(CurrentState.ValueData));
				FProperty* ValueProperty = ArrayProperty->Inner;

				// push elements on stack (in reverse order)
				for (int32 Index = ArrayHelper.Num() - 1; Index >= 0; --Index)
				{
					FStructSerializerState NewState;
					{
						NewState.HasBeenProcessed = false;
						NewState.KeyData = nullptr;
						NewState.KeyProperty = nullptr;
						NewState.ValueData = ArrayHelper.GetRawPtr(Index);
						NewState.ValueProperty = ValueProperty;
						NewState.ValueType = nullptr;
						NewState.FieldType = ValueProperty->GetClass();
					}

					StateStack.Push(NewState);
				}
			}
			else
			{
				Backend.EndArray(CurrentState);
			}
		}

		// maps
		else if (CastField<FMapProperty>(CurrentState.ValueProperty))
		{
			if (!CurrentState.HasBeenProcessed)
			{
				Backend.BeginStructure(CurrentState);

				CurrentState.HasBeenProcessed = true;
				StateStack.Push(CurrentState);

				FMapProperty* MapProperty = CastField<FMapProperty>(CurrentState.ValueProperty);
				FScriptMapHelper MapHelper(MapProperty, MapProperty->ContainerPtrToValuePtr<void>(CurrentState.ValueData));
				FProperty* ValueProperty = MapProperty->ValueProp;

				// push key-value pairs on stack (in reverse order)
				for (int32 Index = MapHelper.GetMaxIndex() - 1; Index >= 0; --Index)
				{
					if (MapHelper.IsValidIndex(Index))
					{
						uint8* PairPtr = MapHelper.GetPairPtr(Index);

						FStructSerializerState NewState;
						{
							NewState.HasBeenProcessed = false;
							NewState.KeyData = PairPtr;
							NewState.KeyProperty = MapProperty->KeyProp;
							NewState.ValueData = PairPtr;
							NewState.ValueProperty = ValueProperty;
							NewState.ValueType = nullptr;
							NewState.FieldType = ValueProperty->GetClass();
						}

						StateStack.Push(NewState);
					}
				}
			}
			else
			{
				Backend.EndStructure(CurrentState);
			}
		}

		// sets
		else if (CastField<FSetProperty>(CurrentState.ValueProperty))
		{
			if (!CurrentState.HasBeenProcessed)
			{
				Backend.BeginArray(CurrentState);

				CurrentState.HasBeenProcessed = true;
				StateStack.Push(CurrentState);

				FSetProperty* SetProperty = CastFieldChecked<FSetProperty>(CurrentState.ValueProperty);
				FScriptSetHelper SetHelper(SetProperty, SetProperty->ContainerPtrToValuePtr<void>(CurrentState.ValueData));
				FProperty* ValueProperty = SetProperty->ElementProp;

				// push elements on stack
				for (int32 Index = SetHelper.GetMaxIndex() - 1; Index >= 0; --Index)
				{
					if (SetHelper.IsValidIndex(Index))
					{
						FStructSerializerState NewState;
						{
							NewState.HasBeenProcessed = false;
							NewState.KeyData = nullptr;
							NewState.KeyProperty = nullptr;
							NewState.ValueData = SetHelper.GetElementPtr(Index);
							NewState.ValueProperty = ValueProperty;
							NewState.ValueType = nullptr;
							NewState.FieldType = ValueProperty->GetClass();
						}

						StateStack.Push(NewState);
					}
				}
			}
			else
			{
				Backend.EndArray(CurrentState);
			}
		}

		// static arrays
		else if (CurrentState.ValueProperty->ArrayDim > 1)
		{
			Backend.BeginArray(CurrentState);

			for (int32 ArrayIndex = 0; ArrayIndex < CurrentState.ValueProperty->ArrayDim; ++ArrayIndex)
			{
				Backend.WriteProperty(CurrentState, ArrayIndex);
			}

			Backend.EndArray(CurrentState);
		}

		// all other properties
		else
		{
			Backend.WriteProperty(CurrentState);
		}
	}
}

void FStructSerializer::SerializeElement(const void* Address, FProperty* Property, int32 ElementIndex, IStructSerializerBackend& Backend, const FStructSerializerPolicies& Policies)
{
	using namespace StructSerializer;

	check(Address != nullptr);
	check(Property != nullptr);
	
	// Always encompass the element in an object
	Backend.BeginStructure(FStructSerializerState());

	// initialize serialization
	TArray<FStructSerializerState> StateStack;
	{
		//Initial state with the desired property info
		FStructSerializerState NewState;
		{
			NewState.ValueData = Address;
			NewState.ElementIndex = ElementIndex;
			NewState.StateFlags = ElementIndex != INDEX_NONE ? EStructSerializerStateFlags::WritingContainerElement : EStructSerializerStateFlags::None;
			NewState.ValueProperty = Property;
			NewState.FieldType = Property->GetClass();
		}

		StateStack.Push(NewState);
	}

	// process state stack
	while (StateStack.Num() > 0)
	{
		FStructSerializerState CurrentState = StateStack.Pop(/*bAllowShrinking=*/ false);

		// structures
		if (CastField<FStructProperty>(CurrentState.ValueProperty))
		{
			//static array of structures
			if (CurrentState.ValueProperty->ArrayDim > 1 && CurrentState.ElementIndex == INDEX_NONE)
			{
				if (!CurrentState.HasBeenProcessed)
				{
					//Push ourself to close the array
					CurrentState.HasBeenProcessed = true;
					StateStack.Push(CurrentState);

					Backend.BeginArray(CurrentState);

					//Template of the sub state. Only element index will vary
					FStructSerializerState NewState;
					{
						NewState.HasBeenProcessed = false;
						NewState.ValueData = CurrentState.ValueData;
						NewState.ValueProperty = CurrentState.ValueProperty;
					}

					// push elements on stack (in reverse order)
					for (int32 Index = CurrentState.ValueProperty->ArrayDim - 1; Index >= 0; --Index)
					{
						NewState.ElementIndex = Index;
						StateStack.Push(NewState);
					}
				}
				else
				{
					Backend.EndArray(CurrentState);
				}
			}
			else
			{
				if (!CurrentState.HasBeenProcessed)
				{
					const void* ValueData = CurrentState.ValueData;

					if (CurrentState.ValueProperty)
					{
						FFieldVariant Outer = CurrentState.ValueProperty->GetOwnerVariant();
						if ((Outer.ToField() == nullptr) || (Outer.ToField()->GetClass() != FArrayProperty::StaticClass()))
						{
							const int32 ContainerAddressIndex = CurrentState.ElementIndex != INDEX_NONE ? CurrentState.ElementIndex : 0;
							ValueData = CurrentState.ValueProperty->ContainerPtrToValuePtr<void>(CurrentState.ValueData, ContainerAddressIndex);
						}
					}

					Backend.BeginStructure(CurrentState);

					CurrentState.HasBeenProcessed = true;
					StateStack.Push(CurrentState);

					// serialize fields
					if (CurrentState.ValueProperty != nullptr)
					{
						//Get the type to iterate over the fields
						FStructProperty* StructProperty = CastField<FStructProperty>(CurrentState.ValueProperty);
						if (StructProperty != nullptr)
						{
							CurrentState.ValueType = StructProperty->Struct;
						}
						else
						{
							FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(CurrentState.ValueProperty);
							if (ObjectProperty != nullptr)
							{
								CurrentState.ValueType = ObjectProperty->PropertyClass;
							}
						}
					}

					TArray<FStructSerializerState> NewStates;

					if (CurrentState.ValueType)
					{
						for (TFieldIterator<FProperty> It(CurrentState.ValueType, EFieldIteratorFlags::IncludeSuper); It; ++It)
						{
							// Skip property if the filter function is set and rejects it.
							if (Policies.PropertyFilter && !Policies.PropertyFilter(*It, CurrentState.ValueProperty))
							{
								continue;
							}

							FStructSerializerState NewState;
							NewState.ValueData = ValueData;
							NewState.ValueProperty = *It;
							NewState.FieldType = It->GetClass();
							NewStates.Emplace(MoveTemp(NewState));
						}
					}

					// push child properties on stack (in reverse order)
					for (int32 Index = NewStates.Num() - 1; Index >= 0; --Index)
					{
						StateStack.Push(NewStates[Index]);
					}
				}
				else
				{
					Backend.EndStructure(CurrentState);
				}
			}
		}

		// dynamic arrays
		else if (CastField<FArrayProperty>(CurrentState.ValueProperty))
		{
			if (!CurrentState.HasBeenProcessed)
			{
				CurrentState.HasBeenProcessed = true;
				StateStack.Push(CurrentState);
				
				FArrayProperty* ArrayProperty = CastField<FArrayProperty>(CurrentState.ValueProperty);
				FScriptArrayHelper ArrayHelper(ArrayProperty, ArrayProperty->ContainerPtrToValuePtr<void>(CurrentState.ValueData));
				FProperty* ValueProperty = ArrayProperty->Inner;

				const auto FillArrayItemState = [&ArrayHelper, &ValueProperty](int32 InElementIndex, EStructSerializerStateFlags InFlags, FStructSerializerState& OutState)
				{
					OutState.ValueData = ArrayHelper.GetRawPtr(InElementIndex);
					OutState.ValueProperty = ValueProperty;
					OutState.FieldType = ValueProperty->GetClass();
					OutState.StateFlags = InFlags;
				};
				
				//If a specific index is asked and it's not valid, skip the property
				if (CurrentState.ElementIndex != INDEX_NONE)
				{
					if (ArrayHelper.IsValidIndex(CurrentState.ElementIndex))
					{
						FStructSerializerState NewState;
						FillArrayItemState(CurrentState.ElementIndex, EStructSerializerStateFlags::WritingContainerElement, NewState);
						StateStack.Push(NewState);
					}
				}
				else
				{
					Backend.BeginArray(CurrentState);

					// push elements on stack (in reverse order)
					for (int32 Index = ArrayHelper.Num() - 1; Index >= 0; --Index)
					{
						FStructSerializerState NewState;
						FillArrayItemState(Index, EStructSerializerStateFlags::None, NewState);
						StateStack.Push(NewState);
					}
				}
			}
			else
			{
				//Close array only if we were not targeting a single element
				if (!EnumHasAnyFlags(CurrentState.StateFlags,EStructSerializerStateFlags::WritingContainerElement))
				{
					Backend.EndArray(CurrentState);
				}
			}
		}

		// maps
		else if (CastField<FMapProperty>(CurrentState.ValueProperty))
		{
			if (Policies.MapSerialization != EStructSerializerMapPolicies::Array)
			{
				UE_LOG(LogSerialization, Verbose, TEXT("SerializeElement skipped map property %s. Only supports maps as array."), *CurrentState.ValueProperty->GetFName().ToString());
				continue;
			}

			if (!CurrentState.HasBeenProcessed)
			{
				CurrentState.HasBeenProcessed = true;
				StateStack.Push(CurrentState);

				FMapProperty* MapProperty = CastField<FMapProperty>(CurrentState.ValueProperty);
				FScriptMapHelper MapHelper(MapProperty, MapProperty->ContainerPtrToValuePtr<void>(CurrentState.ValueData));
				FProperty* ValueProperty = MapProperty->ValueProp;

				const auto FillMapItemState = [&MapHelper, &ValueProperty](int32 InElementIndex, EStructSerializerStateFlags InFlags, FStructSerializerState& OutState)
				{
					OutState.ValueData = MapHelper.GetPairPtr(InElementIndex);
					OutState.ValueProperty = ValueProperty;
					OutState.FieldType = ValueProperty->GetClass();
					OutState.StateFlags = InFlags;
				};

				//If a specific index is asked only push that one on the stack
				if (CurrentState.ElementIndex != INDEX_NONE)
				{
					if (MapHelper.IsValidIndex(CurrentState.ElementIndex))
					{
						FStructSerializerState NewState;
						FillMapItemState(CurrentState.ElementIndex, EStructSerializerStateFlags::WritingContainerElement, NewState);
						StateStack.Push(NewState);
					}
				}
				else
				{
					//Only supports maps as array for now to support round tripping
					Backend.BeginArray(CurrentState);
					
					// push values on stack (in reverse order)
					for (int32 Index = MapHelper.GetMaxIndex() - 1; Index >= 0; --Index)
					{
						if (MapHelper.IsValidIndex(Index))
						{
							FStructSerializerState NewState;
							FillMapItemState(Index, EStructSerializerStateFlags::None, NewState);
							StateStack.Push(NewState);
						}
					}
				}
			}
			else
			{
				//Close map array only if we were not targeting a single element
				if (!EnumHasAnyFlags(CurrentState.StateFlags, EStructSerializerStateFlags::WritingContainerElement))
				{
					Backend.EndArray(CurrentState);
				}
			}
		}

		// sets
		else if (CastField<FSetProperty>(CurrentState.ValueProperty))
		{
			if (!CurrentState.HasBeenProcessed)
			{
				CurrentState.HasBeenProcessed = true;
				StateStack.Push(CurrentState);
				
				FSetProperty* SetProperty = CastFieldChecked<FSetProperty>(CurrentState.ValueProperty);
				FScriptSetHelper SetHelper(SetProperty, SetProperty->ContainerPtrToValuePtr<void>(CurrentState.ValueData));
				FProperty* ValueProperty = SetProperty->ElementProp;
				
				const auto FillSetItemState = [&SetHelper, &ValueProperty](int32 InElementIndex, EStructSerializerStateFlags InFlags, FStructSerializerState& OutState)
				{
					OutState.ValueData = SetHelper.GetElementPtr(InElementIndex);
					OutState.ValueProperty = ValueProperty;
					OutState.FieldType = ValueProperty->GetClass();
					OutState.StateFlags = InFlags;
				};

				//If a specific index is asked just push that one on the stack
				if (CurrentState.ElementIndex != INDEX_NONE)
				{
					if (SetHelper.IsValidIndex(CurrentState.ElementIndex))
					{
						FStructSerializerState NewState;
						FillSetItemState(CurrentState.ElementIndex, EStructSerializerStateFlags::WritingContainerElement, NewState);
						StateStack.Push(NewState);
					}
				}
				else
				{
					Backend.BeginArray(CurrentState);
				
					// push elements on stack
					for (int32 Index = SetHelper.GetMaxIndex() - 1; Index >= 0; --Index)
					{
						if (SetHelper.IsValidIndex(Index))
						{
							FStructSerializerState NewState;
							FillSetItemState(Index, EStructSerializerStateFlags::None, NewState);
							StateStack.Push(NewState);
						}
					}
				}				
			}
			else
			{
				//Close set array only if we were not targeting a single element
				if (!EnumHasAnyFlags(CurrentState.StateFlags, EStructSerializerStateFlags::WritingContainerElement))
				{
					Backend.EndArray(CurrentState);
				}
			}
		}

		// static arrays of simple properties
		else if (CurrentState.ValueProperty->ArrayDim > 1)
		{
			if (CurrentState.ElementIndex != INDEX_NONE)
			{
				if (CurrentState.ElementIndex < CurrentState.ValueProperty->ArrayDim)
				{
					Backend.WriteProperty(CurrentState, CurrentState.ElementIndex);
				}
			}
			else
			{
				Backend.BeginArray(CurrentState);
				for (int32 ArrayIndex = 0; ArrayIndex < CurrentState.ValueProperty->ArrayDim; ++ArrayIndex)
				{
					Backend.WriteProperty(CurrentState, ArrayIndex);
				}
				Backend.EndArray(CurrentState);
			}
		}

		// all other properties
		else
		{
			Backend.WriteProperty(CurrentState);
		}
	}
	
	Backend.EndStructure(FStructSerializerState());
}
