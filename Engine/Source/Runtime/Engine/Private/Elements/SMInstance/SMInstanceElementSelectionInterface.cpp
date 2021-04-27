// Copyright Epic Games, Inc. All Rights Reserved.

#include "Elements/SMInstance/SMInstanceElementSelectionInterface.h"
#include "Elements/SMInstance/SMInstanceElementData.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Elements/Framework/TypedElementList.h"

int32 USMInstanceElementSelectionInterface::GetNumSelectedSMInstances(const UTypedElementList* InCurrentSelection)
{
	return InCurrentSelection->CountElementsOfType(NAME_SMInstance);
}

bool USMInstanceElementSelectionInterface::HasSelectedSMInstances(const UTypedElementList* InCurrentSelection)
{
	return InCurrentSelection->HasElementsOfType(NAME_SMInstance);
}
