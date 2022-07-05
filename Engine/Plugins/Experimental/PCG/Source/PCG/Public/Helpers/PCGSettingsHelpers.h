// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PCGParamData.h"
#include "Metadata/PCGMetadataAttributeTraits.h"
#include "Metadata/PCGMetadataAttributeTpl.h"

namespace PCGSettingsHelpers
{
	template<typename T, typename TEnableIf<!TIsEnumClass<T>::Value>::Type* = nullptr>
	T GetValue(const FName& InName, const T& InValue, UPCGParamData* InParams, PCGMetadataEntryKey InKey)
	{
		if (InParams)
		{
			const FPCGMetadataAttributeBase* MatchingAttribute = InParams->Metadata ? InParams->Metadata->GetConstAttribute(InName) : nullptr;
			if (MatchingAttribute && MatchingAttribute->GetTypeId() == PCG::Private::MetadataTypes<T>::Id)
			{
				return static_cast<const FPCGMetadataAttribute<T>*>(MatchingAttribute)->GetValueFromItemKey(InKey);
			}
			else
			{
				return InValue;
			}
		}
		else
		{
			return InValue;
		}
	}

	template<typename T, typename TEnableIf<!TIsEnumClass<T>::Value>::Type* = nullptr>
	T GetValue(const FName& InName, const T& InValue, UPCGParamData* InParams)
	{
		return GetValue(InName, InValue, InParams, 0);
	}

	template<typename T, typename TEnableIf<!TIsEnumClass<T>::Value>::Type* = nullptr>
	T GetValue(const FName& InName, const T& InValue, UPCGParamData* InParams, const FName& InParamName)
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
	T GetValue(const FName& InName, const T& InValue, UPCGParamData* InParams, PCGMetadataEntryKey InKey)
	{
		return static_cast<T>(GetValue(InName, static_cast<__underlying_type(T)>(InValue), InParams, InKey));
	}

	template<typename T, typename TEnableIf<TIsEnumClass<T>::Value>::Type* = nullptr>
	T GetValue(const FName& InName, const T& InValue, UPCGParamData* InParams)
	{
		return static_cast<T>(GetValue(InName, static_cast<__underlying_type(T)>(InValue), InParams));
	}

	template<typename T, typename TEnableIf<TIsEnumClass<T>::Value>::Type* = nullptr>
	T GetValue(const FName& InName, const T& InValue, UPCGParamData* InParams, const FName& InParamName)
	{
		return static_cast<T>(GetValue(InName, static_cast<__underlying_type(T)>(InValue), InParams, InParamName));
	}

	/** Specialized version for names */
	template<>
	FORCEINLINE FName GetValue(const FName& InName, const FName& InValue, UPCGParamData* InParams, PCGMetadataEntryKey InKey)
	{
		return FName(GetValue(InName, InValue.ToString(), InParams, InKey));
	}

	template<>
	FORCEINLINE FName GetValue(const FName& InName, const FName& InValue, UPCGParamData* InParams)
	{
		return FName(GetValue(InName, InValue.ToString(), InParams));
	}

	template<>
	FORCEINLINE FName GetValue(const FName& InName, const FName& InValue, UPCGParamData* InParams, const FName& InParamName)
	{
		return FName(GetValue(InName, InValue.ToString(), InParams, InParamName));
	}

	/** Sets data from the params to a given property, matched on a name basis */
	void SetValue(UPCGParamData* Params, UObject* Object, FProperty* Property);

	/**
	* Validate that the InProperty is a PCGData supported type and call InFunc with the value of this property (with the right type).
	* The function returns the result of InFunc (or default value of the return type of InFunc if it failed).
	*/
	template <typename Func>
	inline decltype(auto) GetPropertyValueWithCallback(const UObject* InObject, const FProperty* InProperty, Func InFunc)
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
}

#define PCG_GET_OVERRIDEN_VALUE(Settings, Variable, Params) PCGSettingsHelpers::GetValue(GET_MEMBER_NAME_CHECKED(TRemovePointer<TRemoveConst<decltype(Settings)>::Type>::Type, Variable), (Settings)->Variable, Params);