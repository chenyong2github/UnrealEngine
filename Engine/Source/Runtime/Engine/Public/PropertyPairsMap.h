// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"

/** A simple name/value pairs map */
class FPropertyPairsMap
{
public:
	/**
	 * Add a property/value pair to the map. This will replace any existing property with the new value.
	 *
	 * @param InName Name of the property.
	 * @param InValue Optional value to be associated with the property.
	 */
	FORCEINLINE void AddProperty(FName InName, FName InValue = NAME_None)
	{
		Properties.Add(InName, InValue);
	}

	/**
	 * Test if the provided property exists in the map.
	 *
	 * @param InName Name of the property.
	 * @returns True if the property exist in the map.
	 */
	FORCEINLINE bool HasProperty(FName InName) const
	{
		return GetProperty(InName);
	}

	FORCEINLINE bool HasProperty(const TCHAR* InName) const
	{
		FName PropertyName(InName, FNAME_Find);
		return !PropertyName.IsNone() && GetProperty(PropertyName);
	}

	/**
	 * Gets the value associated with the provided property.
	 *
	 * @param InName Name of the property.
	 * @out OutValue Value associated with the provided property.
	 * @returns True if the property exist in the map.
	 */
	FORCEINLINE bool GetProperty(FName InName, FName* OutValue = nullptr) const
	{
		if (const FName* ValuePtr = Properties.Find(InName))
		{
			if (OutValue)
			{
				*OutValue = *ValuePtr;
			}
			return true;
		}
		return false;
	}

	FORCEINLINE bool GetProperty(const TCHAR* InName, FName* OutValue = nullptr) const
	{
		FName PropertyName(InName, FNAME_Find);
		return !PropertyName.IsNone() && GetProperty(PropertyName, OutValue);
	}

	/**
	 * Compare this map with another for equality, ignoring ordering.
	 *
	 * @param InOther Other property map to compare against.
	 * @returns True if the property maps contains the same properties and values.
	 */
	bool operator==(const FPropertyPairsMap& InOther) const
	{
		return Properties.OrderIndependentCompareEqual(InOther.Properties);
	}

	bool operator!=(const FPropertyPairsMap& InOther) const
	{
		return !(*this == InOther);
	}

	/**
	 * Serialize this property map to the provided archive.
	 *
	 * @param Ar Archive to serialize to.
	 * @param InPropertyPairsMap Property map to serialize.
	 * @returns Archive used to serialize the property map.
	 */
	FORCEINLINE friend FArchive& operator<<(FArchive& InAr, FPropertyPairsMap& InPropertyPairsMap)
	{
		return InAr << InPropertyPairsMap.Properties;
	}

	/** @returns True if the property map is empty. */
	FORCEINLINE bool IsEmpty() const
	{
		return Properties.IsEmpty();
	}

	/** @return The number of properties in the map. */
	FORCEINLINE int32 Num() const
	{
		return Properties.Num();
	}

protected:
	TMap<FName, FName> Properties;
};