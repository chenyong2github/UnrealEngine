// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * Base class for all slate metadata
 */
class ISlateMetaData
{
public:

	/** Get the metadata's Type ID and those of its parents. */
	virtual void GetMetaDataTypeIds(TArray<FName>& OutMetaDataTypeIds) const { }

	/**
	 * Checks whether this metadata is of specified type.
	 */
	virtual bool IsOfTypeName(const FName& Type) const
	{
		return false;
	}

	/** Check if this metadata operation can cast safely to the specified template type */
	template<class TType>
	bool IsOfType() const
	{
		return IsOfTypeName(TType::GetTypeId());
	}

	/** Virtual destructor. */
	virtual ~ISlateMetaData() { }

};

/**
 * All metadata-derived classes must include this macro.
 * Example Usage:
 *	class FMyMetaData : public ISlateMetaData
 *	{
 *	public:
 *		SLATE_METADATA_TYPE(FMyMetaData, ISlateMetaData)
 *		...
 *	};
 */
#define SLATE_METADATA_TYPE(TYPE, BASE) \
	static const FName& GetTypeId() { static FName Type(TEXT(#TYPE)); return Type; } \
	virtual void GetMetaDataTypeIds(TArray<FName>& OutMetaDataTypeIds) const override { OutMetaDataTypeIds.Add(GetTypeId()); BASE::GetMetaDataTypeIds(OutMetaDataTypeIds); } \
	virtual bool IsOfTypeName(const FName& Type) const override { return GetTypeId() == Type || BASE::IsOfTypeName(Type); }

 /**
  * Simple tagging metadata
  */
class FTagMetaData : public ISlateMetaData
{
public:
	SLATE_METADATA_TYPE(FTagMetaData, ISlateMetaData)

		FTagMetaData(FName InTag)
		: Tag(InTag)
	{
	}

	/** Tag name for a widget */
	FName Tag;
};