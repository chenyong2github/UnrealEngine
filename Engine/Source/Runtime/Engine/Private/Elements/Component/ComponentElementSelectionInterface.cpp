// Copyright Epic Games, Inc. All Rights Reserved.

#include "Elements/Component/ComponentElementSelectionInterface.h"
#include "Elements/Component/ComponentElementData.h"
#include "Components/ActorComponent.h"
#include "Elements/Framework/TypedElementList.h"

int32 UComponentElementSelectionInterface::GetNumSelectedComponents(const UTypedElementList* InCurrentSelection)
{
	return InCurrentSelection->CountElementsOfType(NAME_Components);
}

bool UComponentElementSelectionInterface::HasSelectedComponents(const UTypedElementList* InCurrentSelection)
{
	return InCurrentSelection->HasElementsOfType(NAME_Components);
}
