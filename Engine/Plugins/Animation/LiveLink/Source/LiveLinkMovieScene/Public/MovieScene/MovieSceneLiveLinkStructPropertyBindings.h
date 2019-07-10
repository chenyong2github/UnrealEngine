// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "Channels/MovieSceneBoolChannel.h"
#include "Channels/MovieSceneByteChannel.h"
#include "Channels/MovieSceneFloatChannel.h"
#include "Channels/MovieSceneIntegerChannel.h"
#include "Sections/MovieSceneStringChannel.h"
#include "LiveLinkTypes.h"


/**
 * Manages bindings to keyed properties of LiveLink UStructs.
 */
class LIVELINKMOVIESCENE_API FLiveLinkStructPropertyBindings
{
public:
	FLiveLinkStructPropertyBindings(FName InPropertyName, const FString& InPropertyPath);

	/**
	 * Rebuilds the property mappings for a specific UStruct, and adds them to the cache
	 *
	 * @param InContainer The type to cache property for
	 */
	void CacheBinding(const UScriptStruct& InStruct);

	/**
	 * Gets the UProperty that is bound to the container
	 *
	 * @param InContainer	The Struct that owns the property
	 * @return				The property on the Struct if it exists
	 */
	UProperty* GetProperty(const UScriptStruct& InStruct) const;

	/**
	 * Gets the current value of a property on a UStruct
	 *
	 * @param InStruct	The struct to get the property from
	 * @param InSourceAddress	The source address of the struct instance
	 * @return ValueType	The current value
	 */
	template <typename ValueType>
	ValueType GetCurrentValue(const UScriptStruct& InStruct, const void* InSourceAddress)
	{
		FPropertyWrapper Property = FindOrAdd(InStruct);

		const ValueType* ValuePtr = Property.GetPropertyAddress<ValueType>(InSourceAddress);
		return ValuePtr ? *ValuePtr : ValueType();
	}

	/**
	 * Gets the current value of a property at desired Index. Must be on ArrayProperty
	 *
	 * @param InIndex	The index of the desired value in the ArrayProperty
	 * @param InStruct	The struct to get the property from
	 * @param InSourceAddress	The source address of the struct instance
	 * @return ValueType	The current value
	 */
	template <typename ValueType>
	ValueType GetCurrentValueAt(int32 InIndex, const UScriptStruct& InStruct, const void* InSourceAddress)
	{
		FPropertyWrapper FoundProperty = FindOrAdd(InStruct);

		if (UProperty* Property = FoundProperty.GetProperty())
		{
			UArrayProperty* ArrayProperty = CastChecked<UArrayProperty>(Property);
			FScriptArrayHelper ArrayHelper(ArrayProperty, ArrayProperty->ContainerPtrToValuePtr<ValueType>(InSourceAddress));
			const ValueType* ValuePtr = reinterpret_cast<const ValueType*>(ArrayHelper.GetRawPtr(InIndex));
			return ValuePtr ? *ValuePtr : ValueType();
		}
		return ValueType();
	}

	/**
	 * Gets the current value of an enum property on a struct
	 *
	 * @param InStruct	The struct to get the property from
	 * @param InSourceAddress	The address of the instanced struct
	 * @return ValueType	The current value
	 */
	int64 GetCurrentValueForEnum(const UScriptStruct& InStruct, const void* InSourceAddress);

	/**
	 * Gets the current value of an enum property at desired index
	 *
	 * @param InIndex	The index of the desired value in the ArrayProperty
	 * @param InStruct	The struct to get the property from
	 * @param InSourceAddress	The address of the instanced struct
	 * @return ValueType	The current value
	 */
	int64 GetCurrentValueForEnumAt(int32 InIndex, const UScriptStruct& InStruct, const void* InSourceAddress);


	/**
	 * Sets the current value of a property on an instance of a UStruct
	 *
	 * @param InStruct	The struct to set the property on
	 * @param InSourceAddress	The address of the instanced struct
	 * @param InValue   The value to set
	 */
	template <typename ValueType>
	void SetCurrentValue(const UScriptStruct& InStruct, void* InSourceAddress, typename TCallTraits<ValueType>::ParamType InValue)
	{
		FPropertyWrapper Property = FindOrAdd(InStruct);

		if (ValueType* ValuePtr = Property.GetPropertyAddress<ValueType>(InSourceAddress))
		{
			*ValuePtr = InValue;
		}
	}

	/**
	 * Sets the current value of a property on an instance of a UStruct
	 *
	 * @param InIndex	The index in the array property to set the value on
	 * @param InStruct	The struct to set the property on
	 * @param InSourceAddress	The address of the instanced struct
	 * @param InValue   The value to set
	 */
	template <typename ValueType>
	void SetCurrentValueAt(int32 InIndex, const UScriptStruct& InStruct, void* InSourceAddress, typename TCallTraits<ValueType>::ParamType InValue)
	{
		FPropertyWrapper FoundProperty = FindOrAdd(InStruct);

		if (UProperty* Property = FoundProperty.GetProperty())
		{
			UArrayProperty* ArrayProperty = CastChecked<UArrayProperty>(Property);
			FScriptArrayHelper ArrayHelper(ArrayProperty, ArrayProperty->ContainerPtrToValuePtr<ValueType>(InSourceAddress));
			ValueType* ValuePtr = reinterpret_cast<ValueType*>(ArrayHelper.GetRawPtr(InIndex));
			if (ValuePtr)
			{
				*ValuePtr = InValue;
			}
		}
	}

	/**
	 * Sets the current value of an Enum property on an instance of a UStruct
	 *
	 * @param InStruct	The struct to set the property on
	 * @param InSourceAddress	The address of the instanced struct
	 * @param InValue   The value to set
	 */
	void SetCurrentValueForEnum(const UScriptStruct& InStruct, void* InSourceAddress, int64 InValue);

	/** @return the property path that this binding was initialized from */
	const FString& GetPropertyPath() const
	{
		return PropertyPath;
	}

	/** @return the property name that this binding was initialized from */
	const FName& GetPropertyName() const
	{
		return PropertyName;
	}

private:
	struct FPropertyNameKey
	{
		FPropertyNameKey(FName InStructName, FName InPropertyName)
			: StructName(InStructName), PropertyName(InPropertyName)
		{}

		bool operator==(const FPropertyNameKey& Other) const
		{
			return (StructName == Other.StructName) && (PropertyName == Other.PropertyName);
		}

		friend uint32 GetTypeHash(const FPropertyNameKey& Key)
		{
			return HashCombine(GetTypeHash(Key.StructName), GetTypeHash(Key.PropertyName));
		}

		FName StructName;
		FName PropertyName;
	};

	struct FPropertyWrapper
	{
		TWeakObjectPtr<UProperty> Property;

		UProperty* GetProperty() const
		{
			UProperty* PropertyPtr = Property.Get();
			if (PropertyPtr && !PropertyPtr->HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed))
			{
				return PropertyPtr;
			}
			return nullptr;
		}

		template<typename ValueType>
		ValueType* GetPropertyAddress(void* InContainerPtr) const
		{
			UProperty* PropertyPtr = GetProperty();
			return PropertyPtr ? PropertyPtr->ContainerPtrToValuePtr<ValueType>(InContainerPtr) : nullptr;
		}

		template<typename ValueType>
		const ValueType* GetPropertyAddress(const void* InContainerPtr) const
		{
			UProperty* PropertyPtr = GetProperty();
			return PropertyPtr ? PropertyPtr->ContainerPtrToValuePtr<const ValueType>(InContainerPtr) : nullptr;
		}
		
		FPropertyWrapper()
			: Property(nullptr)
		{}
	};

	static FPropertyWrapper FindProperty(const UScriptStruct& InStruct, const FName InPropertyName);

	/** Find or add the FPropertyWrapper for the specified struct */
	FPropertyWrapper FindOrAdd(const UScriptStruct& InStruct)
	{
		FPropertyNameKey Key(InStruct.GetFName(), PropertyName);

		const FPropertyWrapper* PropertyWrapper = PropertyCache.Find(Key);
		if (PropertyWrapper)
		{
			return *PropertyWrapper;
		}

		CacheBinding(InStruct);
		return PropertyCache.FindRef(Key);
	}

private:
	/** Mapping of UStructs property */
	static TMap<FPropertyNameKey, FPropertyWrapper> PropertyCache;

	/** Path to the property we are bound to */
	FString PropertyPath;

	/** Actual name of the property we are bound to */
	FName PropertyName;
};

/** Explicit specializations for bools */
template<> LIVELINKMOVIESCENE_API bool FLiveLinkStructPropertyBindings::GetCurrentValue<bool>(const UScriptStruct& InStruct, const void* InSourceAddress);
template<> LIVELINKMOVIESCENE_API void FLiveLinkStructPropertyBindings::SetCurrentValue<bool>(const UScriptStruct& InStruct, void* InSourceAddress, TCallTraits<bool>::ParamType InValue);
