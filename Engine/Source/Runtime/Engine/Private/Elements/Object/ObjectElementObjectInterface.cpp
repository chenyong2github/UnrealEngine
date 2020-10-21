// Copyright Epic Games, Inc. All Rights Reserved.

#include "Elements/Object/ObjectElementObjectInterface.h"
#include "Elements/Object/ObjectElementData.h"

UObject* UObjectElementObjectInterface::GetObject(const FTypedElementHandle& InElementHandle)
{
	const FObjectElementData* ObjectData = InElementHandle.GetData<FObjectElementData>();
	return ObjectData ? ObjectData->Object : nullptr;
}
