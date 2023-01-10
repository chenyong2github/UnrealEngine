// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PCGParamData.h"
#include "Metadata/PCGMetadataAttributeTraits.h"
#include "Metadata/PCGMetadataAttributeTpl.h"

#include <type_traits>

class UPCGComponent;
class UPCGNode;
class UPCGPin;
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

	int ComputeSeedWithOverride(const UPCGSettings* InSettings, const UPCGComponent* InComponent, UPCGParamData* InParams);

	FORCEINLINE int ComputeSeedWithOverride(const UPCGSettings* InSettings, TWeakObjectPtr<UPCGComponent> InComponent, UPCGParamData* InParams)
	{
		return ComputeSeedWithOverride(InSettings, InComponent.Get(), InParams);
	}

	/** Utility to call from before-node-update deprecation. A dedicated pin for params will be added when the pins are updated. Here we detect any params
	*   connections to the In pin and disconnect them, and move the first params connection to a new params pin.
	*/
	void DeprecationBreakOutParamsToNewPin(UPCGNode* InOutNode, TArray<TObjectPtr<UPCGPin>>& InputPins, TArray<TObjectPtr<UPCGPin>>& OutputPins);
}

#define PCG_GET_OVERRIDEN_VALUE(Settings, Variable, Params) PCGSettingsHelpers::GetValue(GET_MEMBER_NAME_CHECKED(TRemovePointer<TRemoveConst<decltype(Settings)>::Type>::Type, Variable), (Settings)->Variable, Params)
