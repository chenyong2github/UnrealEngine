// Copyright Epic Games, Inc. All Rights Reserved.

#include "Elements/Actor/ActorElementSelectionInterface.h"
#include "Elements/Actor/ActorElementData.h"
#include "GameFramework/Actor.h"
#include "TypedElementList.h"

UObject* UActorElementSelectionInterface::Legacy_GetSelectionObject(const FTypedElementHandle& InElementHandle)
{
	const FActorElementData* ActorData = InElementHandle.GetData<FActorElementData>();
	return ActorData ? ActorData->Actor : nullptr;
}

int32 UActorElementSelectionInterface::GetNumSelectedActors(const UTypedElementList* InCurrentSelection)
{
	int32 NumSelected = 0;
	for (const FTypedElementHandle& SelectedElement : *InCurrentSelection)
	{
		if (SelectedElement.GetData<FActorElementData>(/*bSilent*/true))
		{
			++NumSelected;
		}
	}
	return NumSelected;
}

bool UActorElementSelectionInterface::HasSelectedActors(const UTypedElementList* InCurrentSelection)
{
	for (const FTypedElementHandle& SelectedElement : *InCurrentSelection)
	{
		if (SelectedElement.GetData<FActorElementData>(/*bSilent*/true))
		{
			return true;
		}
	}
	return false;
}
