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
}

#define PCG_GET_OVERRIDEN_VALUE(Settings, Variable, Params) PCGSettingsHelpers::GetValue(GET_MEMBER_NAME_CHECKED(TRemovePointer<TRemoveConst<decltype(Settings)>::Type>::Type, Variable), (Settings)->Variable, Params);