// Copyright Epic Games, Inc. All Rights Reserved.

#include "Elements/SMInstance/SMInstanceElementSelectionInterface.h"
#include "Elements/SMInstance/SMInstanceElementData.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Elements/Framework/TypedElementList.h"

bool USMInstanceElementSelectionInterface::SelectElement(const FTypedElementHandle& InElementHandle, UTypedElementList* InSelectionSet, const FTypedElementSelectionOptions& InSelectionOptions)
{
	FSMInstanceManager SMInstance = SMInstanceElementDataUtil::GetSMInstanceFromHandle(InElementHandle);
	if (SMInstance && Super::SelectElement(InElementHandle, InSelectionSet, InSelectionOptions))
	{
		SMInstance.NotifySMInstanceSelectionChanged(/*bIsSelected*/true);
		return true;
	}
	return false;
}

bool USMInstanceElementSelectionInterface::DeselectElement(const FTypedElementHandle& InElementHandle, UTypedElementList* InSelectionSet, const FTypedElementSelectionOptions& InSelectionOptions)
{
	FSMInstanceManager SMInstance = SMInstanceElementDataUtil::GetSMInstanceFromHandle(InElementHandle);
	if (SMInstance && Super::DeselectElement(InElementHandle, InSelectionSet, InSelectionOptions))
	{
		SMInstance.NotifySMInstanceSelectionChanged(/*bIsSelected*/false);
		return true;
	}
	return false;
}
