// Copyright Epic Games, Inc. All Rights Reserved.

#include "StructDeserializer.h"
#include "UObject/UnrealType.h"
#include "IStructDeserializerBackend.h"
#include "UObject/PropertyPortFlags.h"


/* Internal helpers
 *****************************************************************************/

namespace StructDeserializer
{
	/**
	 * Structure for the read state stack.
	 */
	struct FReadState
	{
		/** Holds the property's current array index. */
		int32 ArrayIndex = 0;

		/** Holds a pointer to the property's data. */
		void* Data = nullptr;

		/** Holds the property's meta data. */
		FProperty* Property = nullptr;

		/** Holds a pointer to the UStruct describing the data. */
		UStruct* TypeInfo = nullptr;
	};


	/**
	 * Finds the class for the given stack state.
	 *
	 * @param State The stack state to find the class for.
	 * @return The class, if found.
	 */
	UStruct* FindClass( const FReadState& State )
	{
		UStruct* Class = nullptr;

		if (State.Property != nullptr)
		{
			FProperty* ParentProperty = State.Property;

			if (FArrayProperty* ArrayProperty = CastField<FArrayProperty>(ParentProperty))
			{
				ParentProperty = ArrayProperty->Inner;
			}

			FStructProperty* StructProperty = CastField<FStructProperty>(ParentProperty);
			FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(ParentProperty);

			if (StructProperty != nullptr)
			{
				Class = StructProperty->Struct;
			}
			else if (ObjectProperty != nullptr)
			{
				Class = ObjectProperty->PropertyClass;
			}
		}
		else
		{
			UObject* RootObject = static_cast<UObject*>(State.Data);
			Class = RootObject->GetClass();
		}

		return Class;
	}
}


/* FStructDeserializer static interface
 *****************************************************************************/

bool FStructDeserializer::Deserialize( void* OutStruct, UStruct& TypeInfo, IStructDeserializerBackend& Backend, const FStructDeserializerPolicies& Policies )
{
	using namespace StructDeserializer;

	check(OutStruct != nullptr);

	// initialize deserialization
	FReadState CurrentState;
	{
		CurrentState.ArrayIndex = 0;
		CurrentState.Data = OutStruct;
		CurrentState.Property = nullptr;
		CurrentState.TypeInfo = &TypeInfo;
	}

	TArray<FReadState> StateStack;
	EStructDeserializerBackendTokens Token;

	// process state stack
	while (Backend.GetNextToken(Token))
	{
		FString PropertyName = Backend.GetCurrentPropertyName();

		switch (Token)
		{
		case EStructDeserializerBackendTokens::ArrayEnd:
			{
				// rehash the set now that we are done with it
				if (FSetProperty* SetProperty = CastField<FSetProperty>(CurrentState.Property))
				{
					FScriptSetHelper SetHelper(SetProperty, CurrentState.Data);
					SetHelper.Rehash();
				}

				if (StateStack.Num() == 0)
				{
					UE_LOG(LogSerialization, Verbose, TEXT("Malformed input: Found ArrayEnd without matching ArrayStart"));

					return false;
				}

				CurrentState = StateStack.Pop(/*bAllowShrinking*/ false);
			}
			break;

		case EStructDeserializerBackendTokens::ArrayStart:
			{
				FReadState NewState;

				NewState.Property = FindField<FProperty>(CurrentState.TypeInfo, *PropertyName);

				if (NewState.Property != nullptr)
				{
					if (Policies.PropertyFilter && !Policies.PropertyFilter(NewState.Property, CurrentState.Property))
					{
						Backend.SkipArray();
						continue;
					}

					// handle set property
					if (FSetProperty* SetProperty = CastField<FSetProperty>(NewState.Property))
					{
						NewState.Data = SetProperty->ContainerPtrToValuePtr<void>(CurrentState.Data, CurrentState.ArrayIndex);
						FScriptSetHelper SetHelper(SetProperty, NewState.Data);
						SetHelper.EmptyElements();
					}
					// handle array property
					else if (NewState.Property != nullptr)
					{
					NewState.Data = CurrentState.Data;
					}

					NewState.TypeInfo = FindClass(NewState);
					StateStack.Push(CurrentState);
					CurrentState = NewState;
				}
				else
				{
					// error: array property not found
					if (Policies.MissingFields != EStructDeserializerErrorPolicies::Ignore)
					{
						UE_LOG(LogSerialization, Verbose, TEXT("The array property '%s' does not exist"), *PropertyName);
					}

					if (Policies.MissingFields == EStructDeserializerErrorPolicies::Error)
					{
						return false;
					}

					Backend.SkipArray();
				}
			}
			break;

		case EStructDeserializerBackendTokens::Error:
			{
				return false;
			}

		case EStructDeserializerBackendTokens::Property:
			{
				// Set are serialized as array, so no property name will be set for each entry
				if (PropertyName.IsEmpty() && (CurrentState.Property != nullptr) && (CurrentState.Property->GetClass() == FSetProperty::StaticClass()))
				{
					// handle set element
					FSetProperty* SetProperty = CastField<FSetProperty>(CurrentState.Property);
					FScriptSetHelper SetHelper(SetProperty, CurrentState.Data);
					FProperty* Property = SetProperty->ElementProp;

					const int32 ElementIndex = SetHelper.AddDefaultValue_Invalid_NeedsRehash();
					uint8* ElementPtr = SetHelper.GetElementPtr(ElementIndex);

					if (!Backend.ReadProperty(Property, CurrentState.Property, ElementPtr, CurrentState.ArrayIndex))
					{
						UE_LOG(LogSerialization, Verbose, TEXT("An item in Set '%s' could not be read (%s)"), *PropertyName, *Backend.GetDebugString());
					}
				}
				// Otherwise we are dealing with dynamic or static array
				else if (PropertyName.IsEmpty())
				{
					// handle array element
					FArrayProperty* ArrayProperty = CastField<FArrayProperty>(CurrentState.Property);
					FProperty* Property = nullptr;

					if (ArrayProperty != nullptr)
					{
						// dynamic array element
						Property = ArrayProperty->Inner;
					}
					else
					{
						// static array element
						Property = CurrentState.Property;
					}

					if (Property == nullptr)
					{
						// error: no meta data for array element
						if (Policies.MissingFields != EStructDeserializerErrorPolicies::Ignore)
						{
							UE_LOG(LogSerialization, Verbose, TEXT("Failed to serialize array element %i"), CurrentState.ArrayIndex);
						}

						return false;
					}
					else if (!Backend.ReadProperty(Property, CurrentState.Property, CurrentState.Data, CurrentState.ArrayIndex))
					{
						UE_LOG(LogSerialization, Verbose, TEXT("The array element '%s[%i]' could not be read (%s)"), *PropertyName, CurrentState.ArrayIndex, *Backend.GetDebugString());
					}

					++CurrentState.ArrayIndex;
				}
				else if ((CurrentState.Property != nullptr) && (CurrentState.Property->GetClass() == FMapProperty::StaticClass()))
				{
					// handle map element
					FMapProperty* MapProperty = CastField<FMapProperty>(CurrentState.Property);
					FScriptMapHelper MapHelper(MapProperty, CurrentState.Data);
					FProperty* Property = MapProperty->ValueProp;

					int32 PairIndex = MapHelper.AddDefaultValue_Invalid_NeedsRehash();
					uint8* PairPtr = MapHelper.GetPairPtr(PairIndex);

					MapProperty->KeyProp->ImportText(*PropertyName, PairPtr, PPF_None, nullptr);

					if (!Backend.ReadProperty(Property, CurrentState.Property, PairPtr, CurrentState.ArrayIndex))
					{
						UE_LOG(LogSerialization, Verbose, TEXT("An item in map '%s' could not be read (%s)"), *PropertyName, *Backend.GetDebugString());
					}
				}
				else
				{
					// handle scalar property
					FProperty* Property = FindField<FProperty>(CurrentState.TypeInfo, *PropertyName);

					if (Property != nullptr)
					{
						if (Policies.PropertyFilter && !Policies.PropertyFilter(Property, CurrentState.Property))
						{
							continue;
						}

						if (!Backend.ReadProperty(Property, CurrentState.Property, CurrentState.Data, CurrentState.ArrayIndex))
						{
							UE_LOG(LogSerialization, Verbose, TEXT("The property '%s' could not be read (%s)"), *PropertyName, *Backend.GetDebugString());
						}
					}
					else
					{
						// error: scalar property not found
						if (Policies.MissingFields != EStructDeserializerErrorPolicies::Ignore)
						{
							UE_LOG(LogSerialization, Verbose, TEXT("The property '%s' does not exist"), *PropertyName);
						}

						if (Policies.MissingFields == EStructDeserializerErrorPolicies::Error)
						{
							return false;
						}
					}
				}
			}
			break;

		case EStructDeserializerBackendTokens::StructureEnd:
			{
				// rehash if value was a map
				FMapProperty* MapProperty = CastField<FMapProperty>(CurrentState.Property);
				if (MapProperty != nullptr)
				{			
					FScriptMapHelper MapHelper(MapProperty, CurrentState.Data);
					MapHelper.Rehash();
				}

				// ending of root structure
				if (StateStack.Num() == 0)
				{
					return true;
				}

				CurrentState = StateStack.Pop(/*bAllowShrinking*/ false);
			}
			break;

		case EStructDeserializerBackendTokens::StructureStart:
			{
				FReadState NewState;

				if (PropertyName.IsEmpty())
				{
					// skip root structure
					if (CurrentState.Property == nullptr)
					{
						check(StateStack.Num() == 0);
						continue;
					}

					// handle struct element inside set
					if (FSetProperty* SetProperty = CastField<FSetProperty>(CurrentState.Property))
					{
						FScriptSetHelper SetHelper(SetProperty, CurrentState.Data);
						const int32 ElementIndex = SetHelper.AddDefaultValue_Invalid_NeedsRehash();
						uint8* ElementPtr = SetHelper.GetElementPtr(ElementIndex);

						NewState.Data = ElementPtr;
						NewState.Property = SetProperty->ElementProp;
					}
					// handle struct element inside array
					else if (FArrayProperty* ArrayProperty = CastField<FArrayProperty>(CurrentState.Property))
					{
						FScriptArrayHelper ArrayHelper(ArrayProperty, ArrayProperty->ContainerPtrToValuePtr<void>(CurrentState.Data));
						const int32 ArrayIndex = ArrayHelper.AddValue();

						NewState.Property = ArrayProperty->Inner;
						NewState.Data = ArrayHelper.GetRawPtr(ArrayIndex);
					}
					else
					{
						UE_LOG(LogSerialization, Verbose, TEXT("Found unnamed value outside of array or set."));
						return false;
					}
				}
				// handle map or struct element inside map
				else if ((CurrentState.Property != nullptr) && (CurrentState.Property->GetClass() == FMapProperty::StaticClass()))
				{
					FMapProperty* MapProperty = CastField<FMapProperty>(CurrentState.Property);
					FScriptMapHelper MapHelper(MapProperty, CurrentState.Data);
					int32 PairIndex = MapHelper.AddDefaultValue_Invalid_NeedsRehash();
					uint8* PairPtr = MapHelper.GetPairPtr(PairIndex);
					
					NewState.Data = PairPtr + MapHelper.MapLayout.ValueOffset;
					NewState.Property = MapProperty->ValueProp;

					MapProperty->KeyProp->ImportText(*PropertyName, PairPtr, PPF_None, nullptr);
				}
				else
				{
					NewState.Property = FindField<FProperty>(CurrentState.TypeInfo, *PropertyName);

					// unrecognized property
					if (NewState.Property == nullptr)
					{
						// error: map or struct property not found
						if (Policies.MissingFields != EStructDeserializerErrorPolicies::Ignore)
						{
							UE_LOG(LogSerialization, Verbose, TEXT("Map, Set, or struct property '%s' not found"), *PropertyName);
						}

						if (Policies.MissingFields == EStructDeserializerErrorPolicies::Error)
						{
							return false;
						}
					}
					// handle map property start
					else if (FMapProperty* MapProperty = CastField<FMapProperty>(NewState.Property))
					{
						NewState.Data = MapProperty->ContainerPtrToValuePtr<void>(CurrentState.Data, CurrentState.ArrayIndex);
						FScriptMapHelper MapHelper(MapProperty, NewState.Data);
						MapHelper.EmptyValues();
					}
					// handle struct property
					else
					{
						NewState.Data = NewState.Property->ContainerPtrToValuePtr<void>(CurrentState.Data);
					}
				}

				if (NewState.Property != nullptr)
				{
					// skip struct property if property filter is set and rejects it
					if (Policies.PropertyFilter && !Policies.PropertyFilter(NewState.Property, CurrentState.Property))
					{
						Backend.SkipStructure();
						continue;
					}

					NewState.ArrayIndex = 0;
					NewState.TypeInfo = FindClass(NewState);

					StateStack.Push(CurrentState);
					CurrentState = NewState;
				}
				else
				{
					// error: structured property not found
					Backend.SkipStructure();

					if (Policies.MissingFields != EStructDeserializerErrorPolicies::Ignore)
					{
						UE_LOG(LogSerialization, Verbose, TEXT("Structured property '%s' not found"), *PropertyName);
					}

					if (Policies.MissingFields == EStructDeserializerErrorPolicies::Error)
					{
						return false;
					}
				}
			}

		default:

			continue;
		}
	}

	// root structure not completed
	return false;
}
