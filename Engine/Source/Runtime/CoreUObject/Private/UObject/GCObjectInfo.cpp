// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	GCObjectInfo.cpp: Info about object participating in Garbage Collection code.
=============================================================================*/

#include "UObject/GCObjectInfo.h"

UObject* FGCObjectInfo::TryResolveObject()
{
	return StaticFindObject(UObject::StaticClass(), nullptr, *GetPathName());
}

void FGCObjectInfo::GetPathName(FStringBuilderBase& ResultString) const
{
	if (this != nullptr)
	{
		if (Outer)
		{
			Outer->GetPathName(ResultString);

			// SUBOBJECT_DELIMITER_CHAR is used to indicate that this object's outer is not a UPackage
			if (Outer->Class->Name != NAME_Package && Outer->Outer->Class->Name == NAME_Package)
			{
				ResultString << SUBOBJECT_DELIMITER_CHAR;
			}
			else
			{
				ResultString << TEXT('.');
			}
		}
		Name.AppendString(ResultString);
	}
	else
	{
		ResultString << TEXT("None");
	}
}

FString FGCObjectInfo::GetPathName() const
{
	TStringBuilder<256> ResultBuilder;
	GetPathName(ResultBuilder);
	return ResultBuilder.ToString();
}

FString FGCObjectInfo::GetFullName() const
{
	return FString::Printf(TEXT("%s %s"), *GetClassName(), *GetPathName());
}

FGCObjectInfo* FGCObjectInfo::FindOrAddInfoHelper(UObject* InObject, TMap<UObject*, FGCObjectInfo*>& InOutObjectToInfoMap)
{
	FGCObjectInfo** ExistingObjInfo = InOutObjectToInfoMap.Find(InObject);
	if (ExistingObjInfo)
	{
		return *ExistingObjInfo;
	}

	FGCObjectInfo* NewInfo = new FGCObjectInfo(InObject);
	InOutObjectToInfoMap.Add(InObject, NewInfo);

	NewInfo->Class = FindOrAddInfoHelper(InObject->GetClass(), InOutObjectToInfoMap);
	NewInfo->Outer = InObject->GetOuter() ? FindOrAddInfoHelper(InObject->GetOuter(), InOutObjectToInfoMap) : nullptr;

	return NewInfo;
};