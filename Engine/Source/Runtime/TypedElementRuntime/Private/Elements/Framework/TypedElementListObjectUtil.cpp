// Copyright Epic Games, Inc. All Rights Reserved.

#include "Elements/Framework/TypedElementListObjectUtil.h"
#include "Elements/Interfaces/TypedElementObjectInterface.h"

namespace TypedElementListObjectUtil
{

UObject* GetObjectOfType(const TTypedElement<UTypedElementObjectInterface>& InObjectElement, const UClass* InRequiredClass)
{
	UObject* ElementObject = InObjectElement ? InObjectElement.GetObject() : nullptr;
	return (ElementObject && (!InRequiredClass || ElementObject->IsA(InRequiredClass)))
		? ElementObject
		: nullptr;
}

bool HasObjects(const UTypedElementList* InElementList, const UClass* InRequiredClass)
{
	bool bHasObjects = false;

	ForEachObject(InElementList, [&bHasObjects](const UObject*)
	{
		bHasObjects = true;
		return false;
	}, InRequiredClass);

	return bHasObjects;
}

int32 CountObjects(const UTypedElementList* InElementList, const UClass* InRequiredClass)
{
	int32 NumObjects = 0;

	ForEachObject(InElementList, [&NumObjects](const UObject*)
	{
		++NumObjects;
		return true;
	}, InRequiredClass);

	return NumObjects;
}

void ForEachObject(const UTypedElementList* InElementList, TFunctionRef<bool(UObject*)> InCallback, const UClass* InRequiredClass)
{
	InElementList->ForEachElement<UTypedElementObjectInterface>([&InCallback, InRequiredClass](const TTypedElement<UTypedElementObjectInterface>& InObjectElement)
	{
		if (UObject* ElementObject = GetObjectOfType(InObjectElement, InRequiredClass))
		{
			return InCallback(ElementObject);
		}
		return true;
	});
}

TArray<UObject*> GetObjects(const UTypedElementList* InElementList, const UClass* InRequiredClass)
{
	TArray<UObject*> SelectedObjects;
	SelectedObjects.Reserve(InElementList->Num());

	ForEachObject(InElementList, [&SelectedObjects](UObject* InObject)
	{
		SelectedObjects.Add(InObject);
		return true;
	}, InRequiredClass);

	return SelectedObjects;
}

UObject* GetTopObject(const UTypedElementList* InElementList, const UClass* InRequiredClass)
{
	TTypedElement<UTypedElementObjectInterface> TempElement;
	for (int32 ElementIndex = 0; ElementIndex < InElementList->Num(); ++ElementIndex)
	{
		InElementList->GetElementAt(ElementIndex, TempElement);
		
		if (UObject* ElementObject = GetObjectOfType(TempElement, InRequiredClass))
		{
			return ElementObject;
		}
	}

	return nullptr;
}

UObject* GetBottomObject(const UTypedElementList* InElementList, const UClass* InRequiredClass)
{
	TTypedElement<UTypedElementObjectInterface> TempElement;
	for (int32 ElementIndex = InElementList->Num() - 1; ElementIndex >= 0; --ElementIndex)
	{
		InElementList->GetElementAt(ElementIndex, TempElement);

		if (UObject* ElementObject = GetObjectOfType(TempElement, InRequiredClass))
		{
			return ElementObject;
		}
	}

	return nullptr;
}

} // namespace TypedElementListObjectUtil
