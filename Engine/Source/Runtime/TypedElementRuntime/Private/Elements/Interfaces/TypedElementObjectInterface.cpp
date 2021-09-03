// Copyright Epic Games, Inc. All Rights Reserved.

#include "Elements/Interfaces/TypedElementObjectInterface.h"

#include "Elements/Framework/TypedElementRegistry.h"

UObject* ITypedElementObjectInterface::GetObject(const FTypedElementHandle& InElementHandle)
{
	return nullptr;
}

UClass* ITypedElementObjectInterface::GetObjectClass(const FTypedElementHandle& InElementHandle)
{
	UObject* HandleAsObject = GetObject(InElementHandle);
	return HandleAsObject ? HandleAsObject->GetClass() : nullptr;
}
