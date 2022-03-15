// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Algo/AnyOf.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

namespace UE::Json
{
	// @note: Concepts are an easier way (than type traits) of checking if a call or specialization is valid
	namespace Concepts
	{
		/** Concept to check if T has NumericLimits, and isn't a bool. */
		struct CNumerical
		{
			template <typename T>
			auto Requires() -> decltype(
				TAnd<
					TIsSame<typename TNumericLimits<T>::NumericType, T>,
					TNot<TIsSame<T, bool>>>::Value);
		};

		/** Describes a type that provides a FromJson function. */
		struct CFromJsonable
		{
			template <typename T>
			auto Requires(T& Val, const TSharedRef<FJsonObject>& Arg) -> decltype(
				Val.FromJson(Arg)
			);
		};

		/** Describes a type the is derived from TMap. */
		struct CMap 
		{
			template <typename T>
			auto Requires() -> decltype(
				TOr<
					TIsTMap<T>,
					TIsDerivedFrom<T, TMap<typename T::KeyType, typename T::ValueType>>>::Value);
		};
	}
	
	namespace TypeTraits
	{
		/** String-like value types. */
		template <typename ValueType, typename Enable = void>
		struct TIsStringLike
		{
			enum { Value = false };
		};

		template <typename ValueType>
		struct TIsStringLike<
				ValueType,
				typename TEnableIf<
					TOr<
						TIsSame<ValueType, FString>,
						TIsSame<ValueType, FName>,
						TIsSame<ValueType, FText>>::Value>::Type>
		{
			enum
			{
				Value = true
			};
		};

		/** Numeric value types. */
		template <typename ValueType>
		using TIsNumeric = TAnd<TModels<Concepts::CNumerical, std::decay_t<ValueType>>>;

		/** ValueType has FromJson. */
		template <typename ValueType>
		using THasFromJson = TModels<Concepts::CFromJsonable, std::decay_t<ValueType>>;

		/** ValueType is derived from TMap. */
		template <typename ValueType>
		using TIsDerivedFromMap = TModels<Concepts::CMap, std::decay_t<ValueType>>;
	}

	/** Contains either an object constructed in place, or a reference to an object declared elsewhere. */
	template<class ObjectType>
	class TJsonReference
	{
	public:
		using ElementType = ObjectType;
		static constexpr ESPMode Mode = ESPMode::ThreadSafe;
			
		/** Constructs an empty Json Reference. */
		TJsonReference()
			: Object(nullptr)
		{
		}

		/** Sets the object path and flags it as a pending reference (to be resolved later). */
		void ResolveDeferred(const FString& InJsonPath)
		{
			bHasPendingResolve = true;
			Path = InJsonPath;
		}

		/** Attempts to resolve the reference, returns true if successful or already set. */
		bool TryResolve(const TSharedRef<FJsonObject>& InRootObject, const TFunctionRef<ObjectType(TSharedRef<FJsonObject>&)>& InSetter)
		{
			// Already attempted to resolve, return result or nullptr
			if(!bHasPendingResolve)
			{
				return Object.IsValid();
			}

			TArray<FString> PathSegments;
			GetPathSegments(PathSegments);
			PathSegments.RemoveAt(0); // Remove #/

			TSharedPtr<FJsonObject> SubObject = InRootObject;
			for(const FString& PathSegment : PathSegments)
			{
				if(!SubObject->TryGetObjectField(PathSegment, SubObject))
				{
					return false;
				}
			}

			Object = MoveTemp(InSetter(SubObject.ToSharedRef()));
			return Object.IsValid();
		}

		/** Returns the object referenced by this Json Reference, creating it if it doesn't exist. */
		ObjectType* Get()
		{
			if(!Object.IsValid())
			{
				Object = MakeShared<ObjectType, Mode>();
			}
				
			return Object.Get();
		}

		/** Returns the object referenced by this Json Reference, creating it if it doesn't exist. */
		TSharedPtr<ObjectType> GetShared()
		{
			if(!Object.IsValid())
			{
				Object = MakeShared<ObjectType, Mode>();
			}
				
			return Object;
		}

		/** Returns the object referenced by this Json Reference, or nullptr if it doesn't exist. */
		ObjectType* Get() const
		{
			return Object.Get();
		}

		/** Returns the object referenced by this Json Reference, or nullptr if it doesn't exist. */
		TSharedPtr<ObjectType> GetShared() const
		{
			return Object;
		}

		/** Set's the object if it's not already set. */
		bool Set(ObjectType&& InObject)
		{
			if(Object.IsValid())
			{
				return false;
			}
				
			bHasPendingResolve = false;
			Object = MoveTemp(InObject);
			return true;
		}

		/** Set's the object if it's not already set. */
		bool Set(TSharedPtr<ObjectType>&& InObject)
		{
			if(Object.IsValid())
			{
				return false;
			}
				
			bHasPendingResolve = false;
			Object = MoveTemp(InObject);
			return true;
		}

		/** Set's the object if it's not already set. */
		bool Set(const TSharedPtr<ObjectType>& InObject)
		{
			if(Object.IsValid())
			{
				return false;
			}
				
			bHasPendingResolve = false;
			Object = InObject;
			return true;
		}

		/** Checks if the underlying object has been set. */
		bool IsSet() const
		{
			return Object.IsValid();
		}

		/** Checks to see if this is actually pointing to an object. */
		explicit operator bool() const
		{
			return Object != nullptr;
		}

		/** Checks if the object is valid, or it's pending reference resolution. */
		bool IsValid() const { return Object != nullptr || bHasPendingResolve; }

		/** Dereferences the object*/
		ObjectType& operator*() const
		{
			check(IsValid());
			return *Object; 
		}

		/** Pointer to the underlying object. */
		ObjectType* operator->() const
		{
			check(IsValid());
			return Get();
		}

		const FString& GetPath() const
		{
			return Path;
		}

		/** Returns true if there were one or more segments. */
		bool GetPathSegments(TArray<FString>& OutSegments) const
		{
			return Path.ParseIntoArray(OutSegments, TEXT("/")) > 0;
		}

		FString GetLastPathSegment() const
		{
			TArray<FString> PathSegments;
			if(GetPathSegments(PathSegments))
			{
				return PathSegments.Last();
			}
			return TEXT("");
		}

	protected:
		/** The specified path to the actual object definition. */
		FString Path;

		/** Flag indicating this is a reference but not yet resolved. */
		bool bHasPendingResolve = false;

		/** Underling object pointer. */
		TSharedPtr<ObjectType> Object;
	};
		
	// @note: ideal changes to Json (engine module)
	// - Support TVariant (same field name, different types)
	// - Support TOptional for Get* and TryGet*

	// @todo: somehow account for TOptional or not in same call or minimal overload?

	// template <typename ValueType>
	// void As(const TSharedPtr<FJsonValue>& InJsonValue, ValueType& OutValue)
	// {
	// 	unimplemented();
	// }

	// Numeric
	template <typename ValueType>
	constexpr typename TEnableIf<TypeTraits::TIsNumeric<ValueType>::Value, void>::Type
	As(const TSharedPtr<FJsonValue>& InJsonValue, ValueType& OutValue)
	{
		OutValue = InJsonValue->AsNumber();
	}

	// String
	template <typename ValueType>
	constexpr typename TEnableIf<TypeTraits::TIsStringLike<ValueType>::Value, void>::Type
	As(const TSharedPtr<FJsonValue>& InJsonValue, ValueType& OutValue)
	{
		OutValue = InJsonValue->AsString();
	}

	// Bool
	template <typename ValueType>
	constexpr typename TEnableIf<TIsSame<ValueType, bool>::Value, void>::Type
	As(const TSharedPtr<FJsonValue>& InJsonValue, ValueType& OutValue)
	{
		OutValue = InJsonValue->AsBool();
	}

	// Numeric
	template <typename ValueType>
	constexpr typename TEnableIf<TypeTraits::TIsNumeric<ValueType>::Value, bool>::Type
	TryGet(const TSharedPtr<FJsonValue>& InJsonValue, ValueType& OutValue)
	{
		return InJsonValue->TryGetNumber(OutValue);
	}

	template <typename ValueType>
	constexpr typename TEnableIf<TypeTraits::TIsNumeric<ValueType>::Value, bool>::Type
	TryGetField(const TSharedPtr<FJsonObject>& InJsonObject, const FString& InFieldName, ValueType& OutValue)
	{
		return InJsonObject->TryGetNumberField(InFieldName, OutValue);
	}

	// Enum
	template <typename EnumType>
	constexpr typename TEnableIf<TIsEnumClass<EnumType>::Value, bool>::Type
	TryGet(const TSharedPtr<FJsonValue>& InJsonValue, EnumType& OutValue, const TMap<FString, EnumType>& InNameToValue = {})
	{
		// Try int first
		uint8 IntValue;
		bool bResult = InJsonValue->TryGetNumber(IntValue);
		if(bResult)
		{
			OutValue = StaticCast<EnumType>(IntValue);	
		}
		else
		{
			if(InNameToValue.IsEmpty())
			{
				UE_LOG(LogWebAPI, Error, TEXT("Tried to convert enum from string, but no lookup was provided."));
				return bResult;  
			}

			FString StrValue;
			bResult = InJsonValue->TryGetString(StrValue);
			if(bResult)
			{
				if(const EnumType* NameToValue = InNameToValue.Find(StrValue))
				{
					OutValue = *NameToValue;
				}
			}
		}			
		return bResult;
	}

	template <typename EnumType>
	constexpr typename TEnableIf<TIsEnumClass<EnumType>::Value, bool>::Type
	TryGetField(const TSharedPtr<FJsonObject>& InJsonObject, const FString& InFieldName, EnumType& OutValue, const TMap<FString, EnumType>& InNameToValue = {})
	{
		if(const TSharedPtr<FJsonValue> JsonField = InJsonObject->TryGetField(InFieldName))
		{
			return TryGet(JsonField, OutValue.Get(), InNameToValue);
		}
		return false;
	}

	// String
	template <typename ValueType>
	constexpr typename TEnableIf<TypeTraits::TIsStringLike<ValueType>::Value, bool>::Type
	TryGet(const TSharedPtr<FJsonValue>& InJsonValue, ValueType& OutValue)
	{
		return InJsonValue->TryGetString(OutValue);
	}

	template <typename ValueType>
	constexpr typename TEnableIf<TypeTraits::TIsStringLike<ValueType>::Value, bool>::Type
	TryGetField(const TSharedPtr<FJsonObject>& InJsonObject, const FString& InFieldName, ValueType& OutValue)
	{
		return InJsonObject->TryGetStringField(InFieldName, OutValue);
	}

	// Bool
	template <typename ValueType>
	constexpr typename TEnableIf<TIsSame<ValueType, bool>::Value, bool>::Type
	TryGet(const TSharedPtr<FJsonValue>& InJsonValue, ValueType& OutValue)
	{
		return InJsonValue->TryGetBool(OutValue);
	}

	template <typename ValueType>
	constexpr typename TEnableIf<TIsSame<ValueType, bool>::Value, bool>::Type
	TryGetField(const TSharedPtr<FJsonObject>& InJsonObject, const FString& InFieldName, ValueType& OutValue)
	{
		return InJsonObject->TryGetBoolField(InFieldName, OutValue);
	}

	// Array
	template <typename ContainerType>
	constexpr typename TEnableIf<TIsTArray<ContainerType>::Value, bool>::Type
	TryGet(const TSharedPtr<FJsonValue>& InJsonValue, ContainerType& OutValues)
	{
		using ValueType = typename ContainerType::ElementType;
		const TArray<TSharedPtr<FJsonValue>>* Values;
		if(InJsonValue->TryGetArray(Values))
		{
			bool bSuccess = true;
			if(Values->Num() > 0)
			{
				OutValues.Reserve(Values->Num());
				for(const TSharedPtr<FJsonValue>& JsonValue : *Values)
				{
					ValueType& Value = OutValues.Emplace_GetRef();
					bSuccess &= TryGet(JsonValue, Value);
				}
			}
			return bSuccess;
		}
		return false;
	}

	template <typename ContainerType>
	constexpr typename TEnableIf<TIsTArray<ContainerType>::Value, bool>::Type
	TryGetField(const TSharedPtr<FJsonObject>& InJsonObject, const FString& InFieldName, ContainerType& OutValues)
	{
		if(const TSharedPtr<FJsonValue> JsonField = InJsonObject->TryGetField(InFieldName))
		{
			using ValueType = typename ContainerType::ElementType;
			return TryGet<ContainerType>(JsonField, OutValues);
		}
		return false;
	}

	// Map
	template <typename KeyType, typename ValueType>
	constexpr typename TEnableIf<TypeTraits::TIsStringLike<KeyType>::Value, bool>::Type // Key must be string
	TryGet(const TSharedPtr<FJsonValue>& InJsonValue, TMap<KeyType, ValueType>& OutValues)
	{
		const TSharedPtr<FJsonObject>* JsonObject;
		if(InJsonValue->TryGetObject(JsonObject))
		{
			OutValues.Reserve(JsonObject->Get()->Values.Num());
			for(TPair<FString, TSharedPtr<FJsonValue, ESPMode::ThreadSafe>>& FieldValuePair : JsonObject->Get()->Values)
			{
				ValueType& ItemValue = OutValues.Emplace(MoveTemp(FieldValuePair.Key));
				TryGet(FieldValuePair.Value.ToSharedRef(), ItemValue);
			}
			return true;
		}
		return false;
	}

	template <typename KeyType, typename ValueType>
	constexpr typename TEnableIf<TypeTraits::TIsStringLike<KeyType>::Value, bool>::Type // Key must be string
	TryGetField(const TSharedPtr<FJsonObject>& InJsonObject, const FString& InFieldName, TMap<KeyType, ValueType>& OutValues) // Key must be string
	{
		if(const TSharedPtr<FJsonValue> JsonField = InJsonObject->TryGetField(InFieldName))
		{
			return TryGet(JsonField, OutValues);
		}
		return false;
	}
	
	// template <typename ContainerType>
	// constexpr typename TEnableIf<
	// 	TAnd<
	// 		TIsTMap<ContainerType>,
	// 		TypeTraits::TIsStringLike<typename ContainerType::KeyType>>::Value, bool>::Type // Key must be string
	// TryGet(const TSharedPtr<FJsonValue>& InJsonValue, ContainerType& OutValues)
	// {
	// 	using KeyType = typename ContainerType::KeyType;
	// 	using ValueType = typename ContainerType::ValueType;
	//
	// 	const TSharedPtr<FJsonObject>* Value;
	// 	if(InJsonValue->TryGetObject(Value))
	// 	{
	// 		OutValues.Reserve((*Value)->Values.Num());
	// 		for(TTuple<FString, TSharedPtr<FJsonValue, ESPMode::ThreadSafe>>& FieldValuePair : (*Value)->Values)
	// 		{
	// 			ValueType& ItemValue = OutValues.Add(FieldValuePair.Key, ValueType{});
	// 			TryGet(FieldValuePair.Value.ToSharedRef(), ItemValue);
	// 		}
	// 		return true;
	// 	}
	// 	return false;
	// }
	//
	// template <typename ContainerType>
	// constexpr typename TEnableIf<
	// 	TAnd<
	// 		TIsTMap<ContainerType>,
	// 		TypeTraits::TIsStringLike<typename ContainerType::KeyType>>::Value, bool>::Type
	// TryGetField(const TSharedPtr<FJsonObject>& InJsonObject, const FString& InFieldName, ContainerType& OutValues) // Key must be string
	// {
	// 	if(const TSharedPtr<FJsonValue> JsonField = InJsonObject->TryGetField(InFieldName))
	// 	{
	// 		using KeyType = typename ContainerType::KeyType;
	// 		using ValueType = typename ContainerType::ValueType;
	// 		return TryGet(JsonField, OutValues);
	// 	}
	// 	return false;
	// }

	// Object (with FromJson)
	template <typename ValueType>
	// typename TEnableIf<
	// 	TAnd<
	// 		TNot<
	// 			TAnd<
	// 				TIsTMap<ValueType>,
	// 				TIsDerivedFrom<ValueType, TMap<typename ValueType::KeyType, typename ValueType::ValueType>>>>,
	// 		TypeTraits::THasFromJson<ValueType>>::Value, bool>::Type
	typename TEnableIf<
		TAnd<
			TNot<TypeTraits::TIsDerivedFromMap<ValueType>>,
			TypeTraits::THasFromJson<ValueType>>::Value, bool>::Type
	TryGet(const TSharedPtr<FJsonValue>& InJsonValue, ValueType& OutValue);

	template <typename ValueType>
	typename TEnableIf<
		TAnd<
			TNot<TypeTraits::TIsDerivedFromMap<ValueType>>,
			TypeTraits::THasFromJson<ValueType>>::Value, bool>::Type
	TryGetField(const TSharedPtr<FJsonObject>& InJsonObject, const FString& InFieldName, ValueType& OutValue);

	// Variant
	template <typename... ValueTypes>
	bool TryGet(const TSharedPtr<FJsonValue>& InJsonValue, TVariant<ValueTypes...>& OutValue);

	template <typename... ValueTypes>
	bool TryGetField(const TSharedPtr<FJsonObject>& InJsonObject, const FString& InFieldName, TVariant<ValueTypes...>& OutValue);

	// UniqueObj
	template <typename ValueType>
	bool TryGet(const TSharedPtr<FJsonValue>& InJsonValue, TUniqueObj<ValueType>& OutValue);

	template <typename ValueType>
	bool TryGetField(const TSharedPtr<FJsonObject>& InJsonObject, const FString& InFieldName, TUniqueObj<ValueType>& OutValue);

	// Object (with FromJson)
	template <typename ValueType>
	typename TEnableIf<
		TAnd<
			TNot<TypeTraits::TIsDerivedFromMap<ValueType>>,
			TypeTraits::THasFromJson<ValueType>>::Value, bool>::Type
	TryGet(const TSharedPtr<FJsonValue>& InJsonValue, ValueType& OutValue)
	{
		const TSharedPtr<FJsonObject>* Value;
		if(InJsonValue->TryGetObject(Value))
		{
			return OutValue.FromJson(Value->ToSharedRef());
		}
		return false;
	}

	template <typename ValueType>
	typename TEnableIf<
		TAnd<
			TNot<TypeTraits::TIsDerivedFromMap<ValueType>>,
			TypeTraits::THasFromJson<ValueType>>::Value, bool>::Type
	TryGetField(const TSharedPtr<FJsonObject>& InJsonObject, const FString& InFieldName, ValueType& OutValue)
	{
		if(const TSharedPtr<FJsonValue> JsonField = InJsonObject->TryGetField(InFieldName))
		{
			return TryGet(JsonField, OutValue);
		}
		return false;
	}

	// Object (without FromJson)
	template <typename ValueType>
	typename TEnableIf<
		TAnd<
			TNot<TIsTArray<ValueType>>,
			TNot<TypeTraits::TIsStringLike<ValueType>>,
			TNot<TIsPODType<ValueType>>,
			TNot<TypeTraits::THasFromJson<ValueType>>>::Value, bool>::Type
	TryGet(const TSharedPtr<FJsonValue>& InJsonValue, ValueType& OutValue)
	{
		static_assert(TypeTraits::THasFromJson<ValueType>::Value, "ValueType must implement FromJson");
		return false;
	}

	// TIsTMap<ContainerType>,
	template <typename ValueType>
	typename TEnableIf<
		TAnd<
			TNot<TIsTArray<ValueType>>,
			TNot<TypeTraits::TIsStringLike<ValueType>>,
			TNot<TIsPODType<ValueType>>,
			TNot<TypeTraits::THasFromJson<ValueType>>>::Value, bool>::Type
	TryGetField(const TSharedPtr<FJsonObject>& InJsonObject, const FString& InFieldName, ValueType& OutValue)
	{
		static_assert(TypeTraits::THasFromJson<ValueType>::Value, "ValueType must implement FromJson");
		return false;
	}

	// UniqueObj
	template <typename ValueType>
	bool TryGet(const TSharedPtr<FJsonValue>& InJsonValue, TUniqueObj<ValueType>& OutValue)
	{
		return Json::TryGet(InJsonValue, OutValue.Get());
	}
	
	template <typename ValueType>
	bool TryGetField(const TSharedPtr<FJsonObject>& InJsonObject, const FString& InFieldName, TUniqueObj<ValueType>& OutValue)
	{
		if(const TSharedPtr<FJsonValue> JsonField = InJsonObject->TryGetField(InFieldName))
		{
			return TryGet(JsonField, OutValue.Get());
		}
		return false;
	}

	// TJsonReference
	template <typename ValueType>
	bool TryGet(const TSharedPtr<FJsonValue>& InJsonValue, TJsonReference<ValueType>& OutValue)
	{
		const TSharedPtr<FJsonObject>* JsonObject;
		if(InJsonValue->TryGetObject(JsonObject))
		{
			FString ReferencePath;
			if(JsonObject->Get()->TryGetStringField(TEXT("$ref"), ReferencePath))
			{
				OutValue.ResolveDeferred(ReferencePath);
				return true;
			}
		}
		return Json::TryGet(InJsonValue, *OutValue.Get());
	}
	
	template <typename ValueType>
	bool TryGetField(const TSharedPtr<FJsonObject>& InJsonObject, const FString& InFieldName, TJsonReference<ValueType>& OutValue)
	{
		if(const TSharedPtr<FJsonValue> JsonField = InJsonObject->TryGetField(InFieldName))
		{
			return TryGet(JsonField, OutValue);
		}
		return false;
	}

	// SharedPtr
	template <typename ValueType>
	bool TryGet(const TSharedPtr<FJsonValue>& InJsonValue, TSharedPtr<ValueType>& OutValue)
	{
		// Initialize if invalid
		if(!OutValue.IsValid())
		{
			OutValue = MakeShared<ValueType>();
		}
			
		return Json::TryGet(InJsonValue, *OutValue.Get());
	}
	
	template <typename ValueType>
	bool TryGetField(const TSharedPtr<FJsonObject>& InJsonObject, const FString& InFieldName, TSharedPtr<ValueType>& OutValue)
	{
		if(const TSharedPtr<FJsonValue> JsonField = InJsonObject->TryGetField(InFieldName))
		{
			return TryGet<ValueType>(JsonField, OutValue);
		}
		return false;
	}

	// SharedRef
	template <typename ValueType>
	bool TryGet(const TSharedPtr<FJsonValue>& InJsonValue, TSharedRef<ValueType>& OutValue)
	{
		return Json::TryGet(InJsonValue, OutValue.Get());
	}
	
	template <typename ValueType>
	bool TryGetField(const TSharedPtr<FJsonObject>& InJsonObject, const FString& InFieldName, TSharedRef<ValueType>& OutValue)
	{
		if(const TSharedPtr<FJsonValue> JsonField = InJsonObject->TryGetField(InFieldName))
		{
			return TryGet(JsonField, OutValue);
		}
		return false;
	}

	// Optional
	template <typename ValueType>
	constexpr typename TEnableIf<!TIsEnumClass<ValueType>::Value, bool>::Type
	TryGet(const TSharedPtr<FJsonValue>& InJsonValue, TOptional<ValueType>& OutValue)
	{
		ValueType Value;
		if(TryGet<TDecay<ValueType>>(InJsonValue, Value))
		{
			OutValue = Value;
			return true;
		}
		return false;
	}
	
	template <typename ValueType>
	constexpr typename TEnableIf<!TIsEnumClass<ValueType>::Value, bool>::Type
	TryGetField(const TSharedPtr<FJsonObject>& InJsonObject, const FString& InFieldName, TOptional<ValueType>& OutValue)
	{
		if(const TSharedPtr<FJsonValue> JsonField = InJsonObject->TryGetField(InFieldName))
		{
			if(!OutValue.IsSet())
			{
				OutValue = ValueType{};
			}
				
			return TryGet(JsonField, OutValue.GetValue());
		}
		return false;
	}

	// Optional Enum
	template <typename EnumType>
	constexpr typename TEnableIf<TIsEnumClass<EnumType>::Value, bool>::Type
	TryGet(const TSharedPtr<FJsonValue>& InJsonValue, TOptional<EnumType>& OutValue, const TMap<FString, EnumType>& InNameToValue = {})
	{
		EnumType Value;
		if(TryGet<TDecay<EnumType>>(InJsonValue, Value, InNameToValue))
		{
			OutValue = Value;
			return true;
		}
		return false;
	}
	
	template <typename EnumType>
	constexpr typename TEnableIf<TIsEnumClass<EnumType>::Value, bool>::Type
	TryGetField(const TSharedPtr<FJsonObject>& InJsonObject, const FString& InFieldName, TOptional<EnumType>& OutValue, const TMap<FString, EnumType>& InNameToValue = {})
	{
		if(const TSharedPtr<FJsonValue> JsonField = InJsonObject->TryGetField(InFieldName))
		{
			if(!OutValue.IsSet())
			{
				OutValue = EnumType{};
			}
				
			return TryGet(JsonField, OutValue.GetValue(), InNameToValue);
		}
		return false;
	}

	// Optional UniqueObj
	template <typename ValueType>
	bool TryGet(const TSharedPtr<FJsonValue>& InJsonValue, TOptional<TUniqueObj<ValueType>>& OutValue)
	{
		if(OutValue.IsSet())
		{
			TUniqueObj<ValueType>& Value = OutValue.GetValue();
			if(TryGet(InJsonValue, OutValue.GetValue()))
			{
				OutValue = Value;
				return true;
			}
		}
		else
		{
			TUniqueObj<ValueType> Value;
			if(TryGet(InJsonValue, Value))
			{
				OutValue = Value;
				return true;
			}
		}	

		return false;
	}
	
	template <typename ValueType>
	bool TryGetField(const TSharedPtr<FJsonObject>& InJsonObject, const FString& InFieldName, TOptional<TUniqueObj<ValueType>>& OutValue)
	{
		if(const TSharedPtr<FJsonValue> JsonField = InJsonObject->TryGetField(InFieldName))
		{
			return TryGet(JsonField, OutValue);
		}
		return false;
	}

	// Optional SharedRef
	template <typename ValueType>
	bool TryGet(const TSharedPtr<FJsonValue>& InJsonValue, TOptional<TSharedRef<ValueType>>& OutValue)
	{
		TSharedRef<ValueType> Value = MakeShared<ValueType>();
		if(TryGet(InJsonValue, Value))
		{
			OutValue = Value;
			return true;
		}
		return false;
	}
	
	template <typename ValueType>
	bool TryGetField(const TSharedPtr<FJsonObject>& InJsonObject, const FString& InFieldName, TOptional<TSharedRef<ValueType>>& OutValue)
	{
		if(const TSharedPtr<FJsonValue> JsonField = InJsonObject->TryGetField(InFieldName))
		{
			return TryGet(JsonField, OutValue);
		}
		return false;
	}

	// JsonObject
	template <typename ValueType, typename = typename TEnableIf<TIsSame<ValueType, TSharedPtr<FJsonObject>>::Value>::Type>
	bool TryGet(const TSharedPtr<FJsonValue>& InJsonValue, ValueType& OutValue)
	{
		const TSharedPtr<FJsonObject>* Value;
		if(InJsonValue->TryGetObject(Value))
		{
			OutValue = *Value;
			return true;
		}
		return false;
	}

	template <typename ValueType, typename = typename TEnableIf<TIsSame<ValueType, TSharedPtr<FJsonObject>>::Value>::Type>
	bool TryGetField(const TSharedPtr<FJsonObject>& InJsonObject, const FString& InFieldName, TSharedPtr<FJsonObject>& OutValue)
	{
		if(const TSharedPtr<FJsonValue> JsonField = InJsonObject->TryGetField(InFieldName))
		{
			return TryGet<>(JsonField, OutValue);
		}
		return false;
	}

	// Variant
	struct FVariantValueVisitor
	{
		explicit FVariantValueVisitor(const TSharedPtr<FJsonValue>& InJsonValue)
			: JsonValue(InJsonValue)
		{
		}

		template <typename ValueType>
		bool operator()(ValueType& OutValue)
		{
			RunCount++;
			if(bFoundMatch)
			{
				return bFoundMatch;
			}

			ValueType TempValue;
			if(UE::Json::TryGet(JsonValue, TempValue))
			{
				OutValue = MoveTemp(TempValue);
				bFoundMatch = true;
				return true;
			}

			return false;
		}

		// @todo: for debug, remove
		uint32 RunCount = 0;
		bool bFoundMatch = false;
		const TSharedPtr<FJsonValue> JsonValue;
	};

	struct FVariantObjectVisitor
	{
		explicit FVariantObjectVisitor(const TSharedPtr<FJsonObject>& InJsonObject, const FString& InFieldName)
			: JsonObject(InJsonObject)
			, FieldName(InFieldName)
		{
		}

		template <typename ValueType>
		bool operator()(ValueType& OutValue)
		{
			if(bFoundMatch)
			{
				return bFoundMatch;
			}
				
			if(UE::Json::TryGetField(JsonObject, FieldName, OutValue))
			{
				bFoundMatch = true;
				return true;
			}

			return false;
		}

		bool bFoundMatch = false;
		const TSharedPtr<FJsonObject> JsonObject;
		const FString FieldName;
	};

	template <typename... ValueTypes>
	bool TryGet(const TSharedPtr<FJsonValue>& InJsonValue, TVariant<ValueTypes...>& OutValue)
	{
		FVariantValueVisitor Visitor(InJsonValue);

		// @todo: asked Stefan Boberg about this, only ever tests a single type
		return Visit(Visitor, OutValue);
	}
	
	template <typename... ValueTypes>
	bool TryGetField(const TSharedPtr<FJsonObject>& InJsonObject, const FString& InFieldName, TVariant<ValueTypes...>& OutValue)
	{
		if(const TSharedPtr<FJsonValue> JsonField = InJsonObject->TryGetField(InFieldName))
		{
			return TryGet(JsonField, OutValue);
		}
		return false;
	}

	/*
		// Optional Object Ptr
		template <typename ValueType>
		constexpr
		typename TEnableIf<!TIsTrivial<ValueType>::Value, bool>::Type
		TryGetField(const TSharedPtr<FJsonObject>& InJsonObject, const FString& InFieldName, TOptional<TUniqueObj<ValueType>>& OutValue)
		{
		const TSharedPtr<FJsonObject>* Value;
		if(InJsonObject->TryGetObjectField(InFieldName, Value))
		{
		OutValue.Emplace()->FromJson(Value->ToSharedRef());
		return true;
		}
		return false;
		}

		// Optional Array of Strings
		template <typename ValueType, typename JsonObjectType>
		constexpr
		typename TEnableIf<TIsSame<ValueType, FString>::Value, bool>::Type
		TryGetField(const TSharedPtr<JsonObjectType>& InJsonObject, const FString& InFieldName, TOptional<TArray<ValueType>>& OutValue)
		{
		TArray<ValueType> Value;
		if(InJsonObject->TryGetStringArrayField(InFieldName, Value))
		{
		OutValue.Emplace(MoveTemp(Value));
		return true; 
		}

		// Optional Array of Variant Ptr
		template <typename... ValueTypes>
		constexpr bool
		TryGetField(const TSharedPtr<FJsonObject>& InJsonObject, const FString& InFieldName, TOptional<TArray<TUniqueObj<TVariant<ValueTypes...>>>>& OutValue)
		{
		const TArray<TSharedPtr<FJsonValue>>* ValueArray;
		if(InJsonObject->TryGetArrayField(InFieldName, ValueArray))
		{
		if(ValueArray->Num() > 0)
		{
		OutValue.Emplace({}); // initialize array
		static constexpr std::initializer_list<ValueTypes...> VariantTypes;
		for(const TSharedPtr<FJsonValue>& JsonValue : *ValueArray)
		{
		for(auto& VariantType : VariantTypes)
		{
		using ValueType = decltype(VariantType);
		ValueType Value;
		if(TryGet(JsonValue, Value))
		{
		// It was this type, so exit loop!
		OutValue->Emplace(MoveTemp(Value));
		break;
		}
		}
		}
		}
		return true;
		}
		return false;
		}
		*/
}
