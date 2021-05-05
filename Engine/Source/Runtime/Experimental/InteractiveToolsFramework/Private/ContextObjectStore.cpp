// Copyright Epic Games, Inc. All Rights Reserved.

#include "ContextObjectStore.h"

UObject* UContextObjectStore::FindContextByClass(UClass* InClass) const
{
	for (UObject* ContextObject : ContextObjects)
	{
		if (ContextObject && ContextObject->IsA(InClass))
		{
			return ContextObject;
		}
	}

	return nullptr;
}

bool UContextObjectStore::AddContextObject(UObject* InContextObject)
{
	if (InContextObject)
	{
		return ContextObjects.AddUnique(InContextObject) != INDEX_NONE;
	}

	return false;
}

bool UContextObjectStore::RemoveContextObject(UObject* InContextObject)
{
	return ContextObjects.RemoveSingle(InContextObject) != 0;
}

bool UContextObjectStore::RemoveContextObjectsOfType(const UClass* InClass)
{
	return (ContextObjects.RemoveAll([InClass](UObject* InObject)
	{
		return InObject->IsA(InClass);
	}) > 0);
}

void UContextObjectStore::Shutdown()
{
	ContextObjects.Empty();
}
