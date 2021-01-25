// Copyright Epic Games, Inc. All Rights Reserved.

#include "Elements/Interfaces/TypedElementObjectInterface.h"

#include "Elements/Framework/TypedElementRegistry.h"

UObject* UTypedElementObjectInterface::GetObject(const FTypedElementHandle& InElementHandle)
{
	return nullptr;
}

UClass* UTypedElementObjectInterface::GetObjectClass(const FTypedElementHandle& InElementHandle)
{
	UObject* HandleAsObject = GetObject(InElementHandle);
	return HandleAsObject ? HandleAsObject->GetClass() : nullptr;
}
