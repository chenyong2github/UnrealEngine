// Copyright Epic Games, Inc. All Rights Reserved.

#include "Elements/Component/ComponentElementSelectionInterface.h"
#include "Elements/Component/ComponentElementData.h"
#include "Components/ActorComponent.h"
#include "TypedElementList.h"

UObject* UComponentElementSelectionInterface::Legacy_GetSelectionObject(const FTypedElementHandle& InElementHandle)
{
	const FComponentElementData* ComponentData = InElementHandle.GetData<FComponentElementData>();
	return ComponentData ? ComponentData->Component : nullptr;
}

int32 UComponentElementSelectionInterface::GetNumSelectedComponents(const UTypedElementList* InCurrentSelection)
{
	int32 NumSelected = 0;
	for (const FTypedElementHandle& SelectedElement : *InCurrentSelection)
	{
		if (SelectedElement.GetData<FComponentElementData>(/*bSilent*/true))
		{
			++NumSelected;
		}
	}
	return NumSelected;
}

bool UComponentElementSelectionInterface::HasSelectedComponents(const UTypedElementList* InCurrentSelection)
{
	for (const FTypedElementHandle& SelectedElement : *InCurrentSelection)
	{
		if (SelectedElement.GetData<FComponentElementData>(/*bSilent*/true))
		{
			return true;
		}
	}
	return false;
}
