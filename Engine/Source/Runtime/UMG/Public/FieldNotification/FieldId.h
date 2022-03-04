// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "FieldNotification/FieldVariant.h"
#include "UObject/Object.h"


namespace UE::FieldNotification
{
	
struct FFieldId
{
public:
	FFieldId() = default;

	explicit FFieldId(FName InFieldName, int32 InBitNumber)
		: FieldName(InFieldName)
		, BitNumber(InBitNumber)
	{
	}


public:
	bool IsValid() const
	{
		return !FieldName.IsNone();
	}

	int32 GetIndex() const
	{
		return BitNumber;
	}
	
	FName GetName() const
	{
		return FieldName;
	}

	FFieldVariant ToVariant(UObject* InContainer) const
	{
		if (InContainer && IsValid())
		{
			if (UFunction* Function = InContainer->GetClass()->FindFunctionByName(FieldName))
			{
				return FFieldVariant(Function);
			}
			else if (FProperty* Property = InContainer->GetClass()->FindPropertyByName(FieldName))
			{
				return FFieldVariant(Property);
			}
		}
		return FFieldVariant();
	}

	bool operator==(const FFieldId& Other) const
	{
		return FieldName == Other.FieldName;
	}

	bool operator!=(const FFieldId& Other) const
	{
		return FieldName != Other.FieldName;
	}

	friend int32 GetTypeHash(const FFieldId& Value)
	{
		return GetTypeHash(Value.FieldName);
	}

private:
	/** Name of the field. It can be a FProperty or UFunction. */
	FName FieldName;
	/** The bit this field is linked to. */
	int32 BitNumber;
};

} //namespace