// Copyright Epic Games, Inc. All Rights Reserved.

#include "Elements/Actor/ActorElementSelectionInterface.h"
#include "Elements/Actor/ActorElementData.h"
#include "GameFramework/Actor.h"
#include "Elements/Framework/TypedElementList.h"

int32 UActorElementSelectionInterface::GetNumSelectedActors(const UTypedElementList* InCurrentSelection)
{
	return InCurrentSelection->CountElementsOfType(NAME_Actor);
}

bool UActorElementSelectionInterface::HasSelectedActors(const UTypedElementList* InCurrentSelection)
{
	return InCurrentSelection->HasElementsOfType(NAME_Actor);
}
