// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PCGParamData.h"
#include "Metadata/PCGMetadataAttributeTraits.h"
#include "Metadata/PCGMetadataAttributeTpl.h"

#include <type_traits>

class UPCGComponent;
class UPCGSettings;

namespace PCGSettingsHelpers
{
	/** Utility function to get the value of type T from a param data or a default value
	* @param InName - Attribute to get from the param
	* @param InValue - Default value to return if the param doesn't have the given attribute
	* @param InParams - ParamData to get the value from.
	* @param InKey - Metadata Entry Key to get the value from.
	*/
	template<typename T, typename TEnableIf<!TIsEnumClass<T>::Value>::Type* = nullptr>
	T GetValue(const FName& InName, const T& InValue, const UPCGParamData* InParams, PCGMetadataEntryKey InKey)
	{

		if (InParams && InParams->Metadata)
		{
			const FPCGMetadataAttributeBase* MatchingAttribute = InParams->Metadata->GetConstAttribute(InName);

			if (!MatchingAttribute)
			{
				return InValue;
			}

			auto GetTypedValue = [MatchingAttribute, &InValue, InKey](auto DummyValue) -> T
			{
				using AttributeType = decltype(DummyValue);

				const FPCGMetadataAttribute<AttributeType>* ParamAttribute = static_cast<const FPCGMetadataAttribute<AttributeType>*>(MatchingAttribute);

				if constexpr (std::is_same_v<T, AttributeType>)
				{
					return ParamAttribute->GetValueFromItemKey(InKey);
				}
				else if constexpr (std::is_constructible_v<T, AttributeType>)
				{
					UE_LOG(LogPCG, Verbose, TEXT("[GetAttributeValue] Matching attribute was found, but not the right type. Implicit conversion done (%d vs %d)"), MatchingAttribute->GetTypeId(), PCG::Private::MetadataTypes<T>::Id);
					return T(ParamAttribute->GetValueFromItemKey(InKey));
				}
				else
				{
					UE_LOG(LogPCG, Error, TEXT("[GetAttributeValue] Matching attribute was found, but not the right type. %d vs %d"), MatchingAttribute->GetTypeId(), PCG::Private::MetadataTypes<T>::Id);
					return InValue;
				}
			};

			return PCGMetadataAttribute::CallbackWithRightType(MatchingAttribute->GetTypeId(), GetTypedValue);
		}
		else
		{
			return InValue;
		}
	}

	template<typename T, typename TEnableIf<!TIsEnumClass<T>::Value>::Type* = nullptr>
	T GetValue(const FName& InName, const T& InValue, const UPCGParamData* InParams)
	{
		return GetValue(InName, InValue, InParams, 0);
	}

	template<typename T, typename TEnableIf<!TIsEnumClass<T>::Value>::Type* = nullptr>
	T GetValue(const FName& InName, const T& InValue, const UPCGParamData* InParams, const FName& InParamName)
	{
		if (InParams && InParamName != NAME_None)
		{
			return GetValue(InName, InValue, InParams, InParams->FindMetadataKey(InParamName));
		}
		else
		{
			return InValue;
		}
	}

	/** Specialized versions for enums */
	template<typename T, typename TEnableIf<TIsEnumClass<T>::Value>::Type* = nullptr>
	T GetValue(const FName& InName, const T& InValue, const UPCGParamData* InParams, PCGMetadataEntryKey InKey)
	{
		return static_cast<T>(GetValue(InName, static_cast<__underlying_type(T)>(InValue), InParams, InKey));
	}

	template<typename T, typename TEnableIf<TIsEnumClass<T>::Value>::Type* = nullptr>
	T GetValue(const FName& InName, const T& InValue, const UPCGParamData* InParams)
	{
		return static_cast<T>(GetValue(InName, static_cast<__underlying_type(T)>(InValue), InParams));
	}

	template<typename T, typename TEnableIf<TIsEnumClass<T>::Value>::Type* = nullptr>
	T GetValue(const FName& InName, const T& InValue, const UPCGParamData* InParams, const FName& InParamName)
	{
		return static_cast<T>(GetValue(InName, static_cast<__underlying_type(T)>(InValue), InParams, InParamName));
	}

	/** Specialized version for names */
	template<>
	FORCEINLINE FName GetValue(const FName& InName, const FName& InValue, const UPCGParamData* InParams, PCGMetadataEntryKey InKey)
	{
		return FName(GetValue(InName, InValue.ToString(), InParams, InKey));
	}

	template<>
	FORCEINLINE FName GetValue(const FName& InName, const FName& InValue, const UPCGParamData* InParams)
	{
		return FName(GetValue(InName, InValue.ToString(), InParams));
	}

	template<>
	FORCEINLINE FName GetValue(const FName& InName, const FName& InValue, const UPCGParamData* InParams, const FName& InParamName)
	{
		return FName(GetValue(InName, InValue.ToString(), InParams, InParamName));
	}

	/** Sets data from the params to a given property, matched on a name basis */
	void SetValue(UPCGParamData* Params, UObject* Object, FProperty* Property);

	/**
	* Validate that the InProperty is a PCGData supported type and call InFunc with the value of this property (with the right type).
	* The function returns the result of InFunc (or default value of the return type of InFunc if it failed).
	*/
	template <typename ObjectType, typename Func>
	inline decltype(auto) GetPropertyValueWithCallback(const ObjectType* InObject, const FProperty* InProperty, Func InFunc)
	{
		// Double property is supported by PCG, we use a dummy double to deduce the return type of InFunc.
		using ReturnType = decltype(InFunc(0.0));

		if (!InObject || !InProperty)
		{
			return ReturnType();
		}

		const void* PropertyAddressData = InProperty->ContainerPtrToValuePtr<void>(InObject);

		if (const FNumericProperty* NumericProperty = CastField<FNumericProperty>(InProperty))
		{
			if (NumericProperty->IsFloatingPoint())
			{
				return InFunc(NumericProperty->GetFloatingPointPropertyValue(PropertyAddressData));
			}
			else if (NumericProperty->IsInteger())
			{
				return InFunc(NumericProperty->GetSignedIntPropertyValue(PropertyAddressData));
			}
		}
		else if (const FBoolProperty* BoolProperty = CastField<FBoolProperty>(InProperty))
		{
			return InFunc(BoolProperty->GetPropertyValue(PropertyAddressData));
		}
		else if (const FStrProperty* StringProperty = CastField<FStrProperty>(InProperty))
		{
			return InFunc(StringProperty->GetPropertyValue(PropertyAddressData));
		}
		else if (const FNameProperty* NameProperty = CastField<FNameProperty>(InProperty))
		{
			return InFunc(NameProperty->GetPropertyValue(PropertyAddressData));
		}
		else if (const FEnumProperty* EnumProperty = CastField<FEnumProperty>(InProperty))
		{
			return InFunc(EnumProperty->GetUnderlyingProperty()->GetSignedIntPropertyValue(PropertyAddressData));
		}
		else if (const FStructProperty* StructProperty = CastField<FStructProperty>(InProperty))
		{
			if (StructProperty->Struct == TBaseStructure<FVector>::Get())
			{
				return InFunc(*reinterpret_cast<const FVector*>(PropertyAddressData));
			}
			else if (StructProperty->Struct == TBaseStructure<FVector4>::Get())
			{
				return InFunc(*reinterpret_cast<const FVector4*>(PropertyAddressData));
			}
			else if (StructProperty->Struct == TBaseStructure<FQuat>::Get())
			{
				return InFunc(*reinterpret_cast<const FQuat*>(PropertyAddressData));
			}
			else if (StructProperty->Struct == TBaseStructure<FTransform>::Get())
			{
				return InFunc(*reinterpret_cast<const FTransform*>(PropertyAddressData));
			}
			else if (StructProperty->Struct == TBaseStructure<FRotator>::Get())
			{
				return InFunc(*reinterpret_cast<const FRotator*>(PropertyAddressData));
			}
			else if (StructProperty->Struct == TBaseStructure<FSoftObjectPath>::Get())
			{
				// Soft object path are transformed to strings
				return InFunc(reinterpret_cast<const FSoftObjectPath*>(PropertyAddressData)->ToString());
			}
			else if (StructProperty->Struct == TBaseStructure<FSoftClassPath>::Get())
			{
				// Soft class path are transformed to strings
				return InFunc(reinterpret_cast<const FSoftClassPath*>(PropertyAddressData)->ToString());
			}
			//else if (StructProperty->Struct == TBaseStructure<FColor>::Get())
			//else if (StructProperty->Struct == TBaseStructure<FLinearColor>::Get())
		}
		else if (const FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(InProperty))
		{
			if (const UObject* Object = ObjectProperty->GetObjectPropertyValue(PropertyAddressData))
			{
				// Object are transformed into their soft path name (as a string attribute)
				return InFunc(Object->GetPathName());
			}
		}

		return ReturnType();
	}

	int ComputeSeedWithOverride(const UPCGSettings* InSettings, const UPCGComponent* InComponent, UPCGParamData* InParams);

	FORCEINLINE int ComputeSeedWithOverride(const UPCGSettings* InSettings, TWeakObjectPtr<UPCGComponent> InComponent, UPCGParamData* InParams)
	{
		return ComputeSeedWithOverride(InSettings, InComponent.Get(), InParams);
	}
}

#define PCG_GET_OVERRIDEN_VALUE(Settings, Variable, Params) PCGSettingsHelpers::GetValue(GET_MEMBER_NAME_CHECKED(TRemovePointer<TRemoveConst<decltype(Settings)>::Type>::Type, Variable), (Settings)->Variable, Params)