// Copyright Epic Games, Inc. All Rights Reserved.

#include "Elements/SMInstance/SMInstanceElementSelectionInterface.h"
#include "Elements/SMInstance/SMInstanceElementData.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Elements/Framework/TypedElementList.h"

int32 USMInstanceElementSelectionInterface::GetNumSelectedSMInstances(const UTypedElementList* InCurrentSelection)
{
	int32 NumSelected = 0;
	InCurrentSelection->ForEachElementHandle([&NumSelected](const FTypedElementHandle& InSelectedElement)
	{
		if (SMInstanceElementDataUtil::GetSMInstanceFromHandle(InSelectedElement, /*bSilent*/true))
		{
			++NumSelected;
		}
		return true;
	});
	return NumSelected;
}

bool USMInstanceElementSelectionInterface::HasSelectedSMInstances(const UTypedElementList* InCurrentSelection)
{
	bool bHasSelectedSMInstances = false;
	InCurrentSelection->ForEachElementHandle([&bHasSelectedSMInstances](const FTypedElementHandle& InSelectedElement)
	{
		bHasSelectedSMInstances = !!SMInstanceElementDataUtil::GetSMInstanceFromHandle(InSelectedElement, /*bSilent*/true);
		return !bHasSelectedSMInstances;
	});
	return bHasSelectedSMInstances;
}
