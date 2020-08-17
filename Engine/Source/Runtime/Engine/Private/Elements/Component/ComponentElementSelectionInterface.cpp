// Copyright Epic Games, Inc. All Rights Reserved.

#include "ComponentElementSelectionInterface.h"
#include "ComponentElementData.h"
#include "Components/ActorComponent.h"

bool UComponentElementSelectionInterface::IsValidSelection(const FTypedElementHandle& InElementHandle)
{
	// TODO: Validate that this component is in a valid state to be selected?
	return true;
}

UObject* UComponentElementSelectionInterface::Legacy_GetSelectionObject(const FTypedElementHandle& InElementHandle)
{
	const FComponentElementData* ComponentData = InElementHandle.GetDataScript<FComponentElementData>();
	return ComponentData ? ComponentData->Component : nullptr;
}
