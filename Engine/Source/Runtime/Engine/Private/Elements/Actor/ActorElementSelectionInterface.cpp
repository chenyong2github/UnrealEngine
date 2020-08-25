// Copyright Epic Games, Inc. All Rights Reserved.

#include "ActorElementSelectionInterface.h"
#include "ActorElementData.h"
#include "GameFramework/Actor.h"

bool UActorElementSelectionInterface::IsValidSelection(const FTypedElementHandle& InElementHandle)
{
	// TODO: Validate that this actor is in a valid state to be selected?
	return true;
}

UObject* UActorElementSelectionInterface::Legacy_GetSelectionObject(const FTypedElementHandle& InElementHandle)
{
	const FActorElementData* ActorData = InElementHandle.GetData<FActorElementData>();
	return ActorData ? ActorData->Actor : nullptr;
}
