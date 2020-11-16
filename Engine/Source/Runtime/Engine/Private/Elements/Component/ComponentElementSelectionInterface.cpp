// Copyright Epic Games, Inc. All Rights Reserved.

#include "Elements/Component/ComponentElementSelectionInterface.h"
#include "Elements/Component/ComponentElementData.h"
#include "Components/ActorComponent.h"
#include "Elements/Framework/TypedElementList.h"

int32 UComponentElementSelectionInterface::GetNumSelectedComponents(const UTypedElementList* InCurrentSelection)
{
	int32 NumSelected = 0;
	InCurrentSelection->ForEachElementHandle([&NumSelected](const FTypedElementHandle& InSelectedElement)
	{
		if (InSelectedElement.GetData<FComponentElementData>(/*bSilent*/true))
		{
			++NumSelected;
		}
		return true;
	});
	return NumSelected;
}

bool UComponentElementSelectionInterface::HasSelectedComponents(const UTypedElementList* InCurrentSelection)
{
	bool bHasSelectedComponents = false;
	InCurrentSelection->ForEachElementHandle([&bHasSelectedComponents](const FTypedElementHandle& InSelectedElement)
	{
		bHasSelectedComponents = InSelectedElement.GetData<FComponentElementData>(/*bSilent*/true) != nullptr;
		return !bHasSelectedComponents;
	});
	return bHasSelectedComponents;
}
