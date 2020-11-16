// Copyright Epic Games, Inc. All Rights Reserved.

#include "Elements/Actor/ActorElementSelectionInterface.h"
#include "Elements/Actor/ActorElementData.h"
#include "GameFramework/Actor.h"
#include "Elements/Framework/TypedElementList.h"

int32 UActorElementSelectionInterface::GetNumSelectedActors(const UTypedElementList* InCurrentSelection)
{
	int32 NumSelected = 0;
	InCurrentSelection->ForEachElementHandle([&NumSelected](const FTypedElementHandle& InSelectedElement)
	{
		if (InSelectedElement.GetData<FActorElementData>(/*bSilent*/true))
		{
			++NumSelected;
		}
		return true;
	});
	return NumSelected;
}

bool UActorElementSelectionInterface::HasSelectedActors(const UTypedElementList* InCurrentSelection)
{
	bool bHasSelectedActors = false;
	InCurrentSelection->ForEachElementHandle([&bHasSelectedActors](const FTypedElementHandle& InSelectedElement)
	{
		bHasSelectedActors = InSelectedElement.GetData<FActorElementData>(/*bSilent*/true) != nullptr;
		return !bHasSelectedActors;
	});
	return bHasSelectedActors;
}
