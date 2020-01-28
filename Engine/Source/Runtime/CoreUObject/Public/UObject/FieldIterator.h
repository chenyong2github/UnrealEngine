// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
FieldIterator.h: FField iterators.
=============================================================================*/

#pragma once

#include "CoreMinimal.h"
#include "UObject/Field.h"
#include "UObject/UObjectIterator.h"

//
// For iterating through all fields in all structs.
//
template <class T>
class TAllFieldsIterator
{
private:
	TObjectIterator<UStruct> StructIterator;
	TFieldIterator<T> FieldIterator;

public:
	TAllFieldsIterator(EObjectFlags AdditionalExclusionFlags = RF_ClassDefaultObject, EInternalObjectFlags InternalExclusionFlags = EInternalObjectFlags::None)
		: StructIterator(AdditionalExclusionFlags, /*bIncludeDerivedClasses =*/ true, InternalExclusionFlags)
		, FieldIterator(nullptr)
	{
		InitFieldIterator();
	}

	/** conversion to "bool" returning true if the iterator is valid. */
	FORCEINLINE explicit operator bool() const
	{
		return (bool)FieldIterator || (bool)StructIterator;
	}
	/** inverse of the "bool" operator */
	FORCEINLINE bool operator !() const
	{
		return !(bool)*this;
	}

	inline friend bool operator==(const TAllFieldsIterator<T>& Lhs, const TAllFieldsIterator<T>& Rhs) { return *Lhs.FieldIterator == *Rhs.FieldIterator; }
	inline friend bool operator!=(const TAllFieldsIterator<T>& Lhs, const TAllFieldsIterator<T>& Rhs) { return *Lhs.FieldIterator != *Rhs.FieldIterator; }

	inline void operator++()
	{
		++FieldIterator;
		IterateToNext();
	}
	inline T* operator*()
	{
		return *FieldIterator;
	}
	inline T* operator->()
	{
		return *FieldIterator;
	}
protected:
	inline void InitFieldIterator()
	{
		FieldIterator.~TFieldIterator<T>();
		while (StructIterator)
		{
			new (&FieldIterator) TFieldIterator<T>(*StructIterator, EFieldIteratorFlags::ExcludeSuper, EFieldIteratorFlags::IncludeDeprecated, EFieldIteratorFlags::IncludeInterfaces);
			if (!FieldIterator)
			{
				++StructIterator;
			}
			else
			{
				break;
			}
		}
	}
	inline void IterateToNext()
	{
		if (!FieldIterator)
		{
			++StructIterator;
			InitFieldIterator();
		}
	}
};
